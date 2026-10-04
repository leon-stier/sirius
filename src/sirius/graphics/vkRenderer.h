#pragma once

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include <fstream>
#include <filesystem>
#include <vulkan/vk_platform.h>

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

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;

    static vk::VertexInputBindingDescription GetBindingDescription() {
        return {
            .binding = 0,
            .stride = sizeof(Vertex),
            .inputRate = vk::VertexInputRate::eVertex
        };
    }

    static std::array<vk::VertexInputAttributeDescription, 3> GetAttributeDescriptions() {
        return {
            {
                {
                    .location = 0,
                    .binding = 0,
                    .format = vk::Format::eR32G32B32Sfloat,
                    .offset = offsetof(Vertex, pos)
                },
                {
                    .location = 1,
                    .binding = 0,
                    .format = vk::Format::eR32G32B32Sfloat,
                    .offset = offsetof(Vertex, color)
                },
                {
                    .location = 2,
                    .binding = 0,
                    .format = vk::Format::eR32G32Sfloat,
                    .offset = offsetof(Vertex, texCoord)
                }
            }
        };
    }
};


struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
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

    void CreateTextureImage(const uint8_t* pixelData, uint32_t texWidth, uint32_t texHeight, vk::Format textureFormat = vk::Format::eR8G8B8A8Srgb);

    void KtxTextureLoader();

    void CreateTextureSampler();

    void CreateVertexBuffer();

    void CreateIndexBuffer();

    void CreateUniformBuffers();

    void CreateDescriptorPool();

    void CreateDescriptorSets();

    std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> CreateBuffer(vk::DeviceSize size, vk::BufferUsageFlags bufferUsage, vk::MemoryPropertyFlags memoryProperties) const;

    //////////// Scene ////////////
    void LoadModel();

    /////////// Drawing ///////////
    void RecordCommandBuffer(uint32_t imageIndex, uint32_t currentFrameIndex) const;

    void UpdateUniformBuffer(uint32_t currentImage, uint32_t currentFrameIndex);

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

    void CopyBufferToImage(const vk::raii::CommandBuffer& commandBuffer, const vk::raii::Buffer& buffer, const vk::raii::Image& image, uint32_t width, uint32_t height);

    vk::raii::CommandBuffer BeginSingleTimeCommands() const;

    void EndSingleTimeCommands(vk::raii::CommandBuffer&& commandBuffer) const;

    static bool EqualsExt(const std::filesystem::path& p, std::string_view expected_ext);

    VulkanContext* context_{nullptr};
    // Members are declared before the objects that depend on them so reverse
    // declaration-order destruction releases Vulkan dependencies first.
    vk::raii::SwapchainKHR swapChain_{nullptr};
    std::vector<vk::Image> swapChainImages_;
    vk::SurfaceFormat2KHR swapChainSurfaceFormat_;
    vk::Extent2D swapChainExtent_;
    std::vector<vk::raii::ImageView> swapChainImageViews_;

    vk::raii::DescriptorSetLayout descriptorSetLayout_{nullptr};
    vk::raii::DescriptorPool descriptorPool_{nullptr};
    vk::raii::PipelineLayout pipelineLayout_{nullptr};
    vk::raii::Pipeline graphicsPipeline_{nullptr};

    vk::raii::DeviceMemory textureImageMemory_{nullptr};
    vk::raii::Image textureImage_{nullptr};
    vk::raii::ImageView textureImageView_{nullptr};
    vk::raii::Sampler textureSampler_{nullptr};

    vk::raii::DeviceMemory depthImageMemory_{nullptr};
    vk::raii::Image depthImage_{nullptr};
    vk::raii::ImageView depthImageView_{nullptr};

    std::vector<Vertex> vertices_;
    std::vector<uint32_t> indices_;
    vk::raii::DeviceMemory vertexBufferMemory_{nullptr};
    vk::raii::Buffer vertexBuffer_{nullptr};
    vk::raii::DeviceMemory indexBufferMemory_{nullptr};
    vk::raii::Buffer indexBuffer_{nullptr};
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
