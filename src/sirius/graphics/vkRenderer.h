#pragma once

#include "input/input_manager.h"
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

import vulkan;
#include <vulkan/vk_platform.h>


namespace sirius {
struct InputEvent;

const std::vector kValidationLayers = {
    "VK_LAYER_KHRONOS_validation",
};

constexpr uint32_t kMaxFramesInFlight{2};


#ifdef NDEBUG
constexpr bool kEnableValidationLayers = false;
#else
constexpr bool kEnableValidationLayers = true;
#endif


template<class... Ts>
struct Overload : Ts...{
    using Ts::operator()...;
};

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

struct Vertex
{
    glm::vec3 pos;
    glm::vec3 color;

    static vk::VertexInputBindingDescription GetBindingDescription()
    {
        return {
            .binding = 0,
            .stride = sizeof(Vertex),
            .inputRate = vk::VertexInputRate::eVertex
        };
    }

    static std::array<vk::VertexInputAttributeDescription, 2> GetAttributeDescriptions()
    {
        return {{
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
        }};
    }
};

const std::vector<Vertex> kVertices = {
    // Front face (0-3)
    {{-0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}},
    {{ 0.5f, -0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}},
    {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}},
    {{-0.5f,  0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}},

    // Back face (4-7)
    {{ 0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
    {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
    {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}},
    {{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}}
};

const std::vector<uint16_t> kIndices = {
    // Front
    0, 1, 2,  2, 3, 0,
    // Back
    4, 5, 6,  6, 7, 4,
    // Top
    3, 2, 7,  7, 6, 3,
    // Bottom
    5, 4, 1,  1, 0, 5,
    // Right
    1, 4, 7,  7, 2, 1,
    // Left
    5, 0, 3,  3, 6, 5
};

struct UniformBufferObject
{
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

struct FrameContext {
    vk::raii::CommandPool commandPool{nullptr};
    vk::raii::CommandBuffer commandBuffer{nullptr};

    vk::raii::Buffer uniformBuffer{nullptr};
    vk::raii::DeviceMemory uniformBufferMemory{nullptr};
    void* uniformBufferMapped{nullptr};

    vk::raii::DescriptorSet descriptorSet{nullptr};

    vk::raii::Semaphore imageAcquiredSemaphore{nullptr};
};

struct PhysicalDeviceRequirements {
    static constexpr uint32_t minApiVersion = vk::ApiVersion14;

    static inline const std::vector<const char*> extensions = {
        vk::KHRSwapchainExtensionName,
        vk::EXTExtendedDynamicStateExtensionName,
        vk::KHRMaintenance5ExtensionName
    };

    static constexpr auto queueFlagBits{
        vk::QueueFlagBits::eGraphics
    };

    static constexpr auto requiredFeatures = std::make_tuple(
        vk::PhysicalDeviceVulkan11Features{ .shaderDrawParameters = vk::True },
        vk::PhysicalDeviceVulkan12Features{ .timelineSemaphore = vk::True},
        vk::PhysicalDeviceVulkan13Features{ .synchronization2 = vk::True, .dynamicRendering = vk::True },
        vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT{ .extendedDynamicState = vk::True },
        vk::PhysicalDeviceMaintenance5FeaturesKHR{ .maintenance5 = vk::True }
    );
};


class VkRenderer {
public:
    void Init();
    void Draw();

    ~VkRenderer() {
        device_.waitIdle();
    }

private:
    //////// Initialization ////////
    void CreateInstance();
    void CreateSurface();
    void PickPhysicalDevice();

    static bool IsDeviceSuitable(vk::raii::PhysicalDevice const& physicalDevice);
    void CreateLogicalDevice();
    void CreateSwapChain();
    void RecreateSwapChain();

    static vk::SurfaceFormatKHR ChooseSwapSurfaceFormat(std::vector<vk::SurfaceFormatKHR> const &availableFormats);
    static vk::PresentModeKHR ChooseSwapPresentMode(std::vector<vk::PresentModeKHR> const &availablePresentModes);
    static vk::Extent2D ChooseSwapExtent(vk::SurfaceCapabilitiesKHR const &capabilities);
    static uint32_t ChooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const &capabilities);

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
    void CreateVertexBuffer();
    void CreateIndexBuffer();
    void CreateUniformBuffers();
    void CreateDescriptorPool();
    void CreateDescriptorSets();
    std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> CreateBuffer(vk::DeviceSize size, vk::BufferUsageFlags bufferUsage, vk::MemoryPropertyFlags memoryProperties) const;
    /////////// Drawing ///////////
    void RecordCommandBuffer(uint32_t imageIndex, uint32_t currentFrameIndex) const;
    void UpdateUniformBuffer(uint32_t currentImage, uint32_t currentFrameIndex) const;
    void DoDraw();

    void ProcessCameraEvent(InputEvent event);
    // Utils
    void SetupDebugMessenger();
    static VKAPI_ATTR vk::Bool32 VKAPI_CALL DebugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT type, const vk::DebugUtilsMessengerCallbackDataEXT * pCallbackData, void * pUserData);

    void TransitionImageLayout(
        vk::Image image,
        vk::ImageLayout oldLayout,
        vk::ImageLayout newLayout,
        vk::AccessFlags2 srcAccessMask,
        vk::AccessFlags2 dstAccessMask,
        vk::PipelineStageFlags2 srcStageMask,
        vk::PipelineStageFlags2 dstStageMask,
        vk::ImageAspectFlags imageAspectFlags,
        uint32_t currentFrameIndex
    ) const;

    uint32_t FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const;

    void CopyBuffer(const vk::raii::Buffer& srcBuffer, const vk::raii::Buffer& dstBuffer, vk::DeviceSize size) const;

    // Declaration order dictates cleanup order
    vk::raii::Context context_;
    vk::raii::Instance instance_{nullptr};
    vk::raii::DebugUtilsMessengerEXT debugMessenger_{nullptr};
    vk::raii::SurfaceKHR surface_{nullptr};

    vk::raii::PhysicalDevice physicalDevice_{nullptr};
    vk::raii::Device device_{nullptr};
    vk::raii::Queue graphicsQueue_{nullptr};
    uint32_t graphicsQueueIndex_{0};

    vk::raii::SwapchainKHR swapChain_{nullptr};
    std::vector<vk::Image> swapChainImages_;
    vk::SurfaceFormatKHR swapChainSurfaceFormat_;
    vk::Extent2D swapChainExtent_;
    std::vector<vk::raii::ImageView> swapChainImageViews_;

    vk::raii::DescriptorSetLayout descriptorSetLayout_{nullptr};
    vk::raii::DescriptorPool descriptorPool_{nullptr};
    vk::raii::PipelineLayout pipelineLayout_{nullptr};
    vk::raii::Pipeline graphicsPipeline_{nullptr};

    vk::raii::Image        depthImage_       = nullptr;
    vk::raii::DeviceMemory depthImageMemory_ = nullptr;
    vk::raii::ImageView    depthImageView_   = nullptr;

    vk::raii::Buffer vertexBuffer_{nullptr};
    vk::raii::DeviceMemory vertexBufferMemory_{nullptr};
    vk::raii::Buffer indexBuffer_{nullptr};
    vk::raii::DeviceMemory indexBufferMemory_{nullptr};
    std::array<FrameContext, kMaxFramesInFlight> frames_;

    vk::raii::CommandPool ephemeralCommandPool_{nullptr};

    vk::raii::Semaphore timelineSemaphore_{nullptr};
    std::vector<vk::raii::Semaphore> renderCompleteSemaphores_;

    uint64_t frameIndex_{0};
    uint64_t nextSignalValue_{kMaxFramesInFlight + 1};
    bool requireSwapChainRecreate_{false};

    glm::vec3 cameraCoords_ {2.0f , 2.0f, 2.0f};
};
}
