#pragma once

import vulkan;
#include "vulkan/vk_platform.h"

namespace sirius {

const std::vector kValidationLayers = {
    "VK_LAYER_KHRONOS_validation",
};

constexpr uint32_t kMaxFramesInFlight{2};


#ifdef NDEBUG
constexpr bool kEnableValidationLayers = false;
#else
constexpr bool kEnableValidationLayers = true;
#endif



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

struct FrameData {
    vk::raii::CommandPool commandPool{nullptr};
    vk::raii::CommandBuffer commandBuffer{nullptr};
    vk::raii::Semaphore imageAcquiredSemaphore{nullptr};
};

struct PhysicalDeviceRequirements {
    static constexpr uint32_t minApiVersion = vk::ApiVersion13;

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

private:
    //////// Initialization ////////
    void CreateInstance();
    void CreateSurface();
    void PickPhysicalDevice();
    bool IsDeviceSuitable(vk::raii::PhysicalDevice const& physicalDevice);
    void CreateLogicalDevice();
    void CreateSwapChain();
    void DestroySwapChain();
    vk::SurfaceFormatKHR ChooseSwapSurfaceFormat(std::vector<vk::SurfaceFormatKHR> const &availableFormats);
    vk::PresentModeKHR ChooseSwapPresentMode(std::vector<vk::PresentModeKHR> const &availablePresentModes);
    vk::Extent2D ChooseSwapExtent(vk::SurfaceCapabilitiesKHR const &capabilities);
    uint32_t ChooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const &capabilities);
    void CreateImageViews();
    void CreateGraphicsPipeline();
    void InitCommandBuffers();
    void CreateSyncObjects();

    /////////// Drawing ///////////
    void RecordCommandBuffer(uint32_t imageIndex, uint32_t currentFrameIndex) const;


    void SetupDebugMessenger();
    static VKAPI_ATTR vk::Bool32 VKAPI_CALL DebugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT type, const vk::DebugUtilsMessengerCallbackDataEXT * pCallbackData, void * pUserData);

    void TransitionImageLayout(
        uint32_t imageIndex,
        vk::ImageLayout oldLayout,
        vk::ImageLayout newLayout,
        vk::AccessFlags2 srcAccessMask,
        vk::AccessFlags2 dstAccessMask,
        vk::PipelineStageFlags2 srcStageMask,
        vk::PipelineStageFlags2 dstStageMask,
        uint32_t currentFrameIndex
    ) const;

    std::vector<const char*> requiredDeviceExtensions_ = {vk::KHRSwapchainExtensionName, vk::KHRMaintenance5ExtensionName};
    // Declaration order dictates cleanup order
    vk::raii::Context context_;
    vk::raii::Instance instance_{nullptr};
    vk::raii::SurfaceKHR surface_{nullptr};
    vk::raii::PhysicalDevice physicalDevice_{nullptr};
    vk::raii::DebugUtilsMessengerEXT debugMessenger_{nullptr};
    vk::raii::Device device_{nullptr};
    vk::raii::Queue graphicsQueue_{nullptr};
    uint32_t graphicsQueueIndex_{0};
    vk::raii::SwapchainKHR swapChain_{nullptr};
    vk::Extent2D swapChainExtent_;
    std::vector<vk::Image> swapChainImages_;
    vk::SurfaceFormatKHR swapChainSurfaceFormat_;
    std::vector<vk::raii::ImageView> swapChainImageViews_;
    vk::raii::PipelineLayout pipelineLayout_{nullptr};
    vk::raii::Pipeline graphicsPipeline_{nullptr};
    vk::raii::Semaphore timelineSemaphore_{nullptr};
    std::vector<vk::raii::Semaphore> renderCompleteSemaphores_;
    std::array<FrameData, kMaxFramesInFlight> frames_;

    uint64_t frameIndex_{0};
    uint64_t nextSignalValue_{kMaxFramesInFlight + 1};
    bool requireSwapChainRecreate_{false};
};
}
