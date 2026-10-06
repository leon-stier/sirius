#pragma once

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <vulkan/vk_platform.h>

#include "AssetManager.h"
#include "GpuAssetUploader.h"
#include "RenderWorld.h"
#include "graphics/camera.h"
#include "vulkanContext.h"

class VulkanContext;

namespace sirius {
struct InputEvent;

constexpr uint32_t kMaxFramesInFlight{2};


static std::vector<uint32_t> ReadFile(const std::filesystem::path& filePath) {
    if (!std::filesystem::exists(filePath)) {
        throw std::runtime_error("SPIR-V file does not exist: " + filePath.string());
    }

    const auto fileSize = std::filesystem::file_size(filePath);

    if (fileSize % sizeof(uint32_t) != 0) {
        throw std::runtime_error("Corrupt SPIR-V file (size is not a multiple of 4 bytes): " + filePath.string());
    }

    std::ifstream file(filePath, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open SPIR-V file: " + filePath.string());
    }

    std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(fileSize));

    return buffer;
}

struct FrameUniforms {
    glm::mat4 view;
    glm::mat4 projection;
};

struct DrawConstants {
    glm::mat4 model;
    glm::vec4 baseColor;
};

struct FrameContext {
    vk::raii::CommandPool commandPool{nullptr};
    vk::raii::CommandBuffer commandBuffer{nullptr};

    vk::raii::DeviceMemory uniformBufferMemory{nullptr};
    vk::raii::Buffer uniformBuffer{nullptr};
    void* uniformBufferMapped{nullptr};

    vk::raii::DescriptorSet descriptorSet{nullptr};

    vk::raii::Semaphore imageAcquiredSemaphore{nullptr};
};

class VkRenderer {
public:
    void Init(VulkanContext& context);

    void Draw();

    RenderInstanceHandle LoadModelInstance(const std::filesystem::path& path, const glm::mat4& transform = glm::mat4(1.0f));
    void DestroyInstance(RenderInstanceHandle handle);
    void SetInstanceTransform(RenderInstanceHandle handle, const glm::mat4& transform);

    ~VkRenderer() {
        if (context_ != nullptr) {
            context_->Device().waitIdle();
        }
    }

private:
    //////// Initialization ////////
    void CreateSwapChain();

    void RecreateSwapChain();

    static vk::SurfaceFormat2KHR ChooseSwapSurfaceFormat(std::vector<vk::SurfaceFormat2KHR> const& availableFormats);

    static vk::PresentModeKHR ChooseSwapPresentMode(std::vector<vk::PresentModeKHR> const& availablePresentModes);

    static vk::Extent2D ChooseSwapExtent(vk::SurfaceCapabilitiesKHR const& capabilities);

    static uint32_t ChooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const& capabilities);

    void CreateImageViews();

    vk::raii::ImageView CreateImageView(vk::Image const& image, vk::Format format, vk::ImageAspectFlags aspectFlags) const;

    void CreateDescriptorSetLayout();

    void CreateGraphicsPipeline();

    void InitCommandBuffers();

    std::pair<vk::raii::Image, vk::raii::DeviceMemory> CreateImage(uint32_t width, uint32_t height, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties) const;

    vk::Format FindSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features) const;

    vk::Format FindDepthFormat() const;

    void CreateDepthResources();

    void CreateSyncObjects();

    void CreateUniformBuffers();

    void CreateDescriptorPool();

    void CreateDescriptorSets();

    std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> CreateBuffer(vk::DeviceSize size, vk::BufferUsageFlags bufferUsage, vk::MemoryPropertyFlags memoryProperties) const;

    /////////// Drawing ///////////
    void RecordCommandBuffer(uint32_t imageIndex, uint32_t currentFrameIndex) const;

    void UpdateUniformBuffer(uint32_t currentFrameIndex);

    void DoDraw();

    void ProcessCameraEvent(InputEvent event);

    //////////// Utils ////////////
    void TransitionImageLayout(
        vk::Image image,
        const vk::raii::CommandBuffer& commandBuffer,
        vk::ImageLayout oldLayout,
        vk::ImageLayout newLayout,
        vk::AccessFlags2 srcAccessMask,
        vk::AccessFlags2 dstAccessMask,
        vk::PipelineStageFlags2 srcStageMask,
        vk::PipelineStageFlags2 dstStageMask,
        vk::ImageAspectFlags imageAspectFlags
    ) const;

    uint32_t FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const;

    void CopyBuffer(const vk::raii::Buffer& srcBuffer, const vk::raii::Buffer& dstBuffer, vk::DeviceSize size) const;

    vk::raii::CommandBuffer BeginSingleTimeCommands() const;

    void EndSingleTimeCommands(vk::raii::CommandBuffer&& commandBuffer) const;

    VulkanContext* context_{nullptr};

    AssetManager assetManager_;
    GpuAssetUploader assetUploader_;
    RenderWorld renderWorld_;
    std::unordered_map<uint32_t, GpuAssetHandle> uploadedAssets_;

    vk::raii::SwapchainKHR swapChain_{nullptr};
    std::vector<vk::Image> swapChainImages_;
    vk::SurfaceFormat2KHR swapChainSurfaceFormat_;
    vk::Extent2D swapChainExtent_;
    std::vector<vk::raii::ImageView> swapChainImageViews_;

    vk::raii::DescriptorSetLayout descriptorSetLayout_{nullptr};
    vk::raii::DescriptorPool descriptorPool_{nullptr};
    vk::raii::PipelineLayout pipelineLayout_{nullptr};
    vk::raii::Pipeline graphicsPipeline_{nullptr};

    vk::raii::DeviceMemory depthImageMemory_{nullptr};
    vk::raii::Image depthImage_{nullptr};
    vk::raii::ImageView depthImageView_{nullptr};

    std::array<FrameContext, kMaxFramesInFlight> frames_;

    vk::raii::CommandPool ephemeralCommandPool_{nullptr};

    vk::raii::Semaphore timelineSemaphore_{nullptr};
    std::vector<vk::raii::Semaphore> renderCompleteSemaphores_;

    uint64_t frameIndex_{0};
    uint64_t nextSignalValue_{kMaxFramesInFlight + 1};
    bool requireSwapChainRecreate_{false};

    glm::vec3 cameraCoords_{2.0f, 2.0f, 2.0f};
    Camera defaultCamera_{};

};
}
