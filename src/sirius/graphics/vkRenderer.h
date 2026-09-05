#pragma once

import vulkan;
#include "vulkan/vk_platform.h"

namespace sirius {

const std::vector kValidationLayers = {
    "VK_LAYER_KHRONOS_validation",
};

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
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


class VkRenderer {
public:
    void Init();
    void Draw();

private:
    void CreateInstance();
    void CreateSurface();
    void PickPhysicalDevice();

    bool IsDeviceSuitable(vk::raii::PhysicalDevice const& physicalDevice);
    void CreateLogicalDevice();
    void CreateSwapChain();
    vk::SurfaceFormatKHR ChooseSwapSurfaceFormat(std::vector<vk::SurfaceFormatKHR> const &availableFormats);
    vk::PresentModeKHR ChooseSwapPresentMode(std::vector<vk::PresentModeKHR> const &availablePresentModes);
    vk::Extent2D ChooseSwapExtent(vk::SurfaceCapabilitiesKHR const &capabilities);
    uint32_t ChooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const &capabilities);
    void CreateImageViews();
    void CreateGraphicsPipeline();


    void SetupDebugMessenger();
    static VKAPI_ATTR vk::Bool32 VKAPI_CALL DebugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT type, const vk::DebugUtilsMessengerCallbackDataEXT * pCallbackData, void * pUserData);

    std::vector<const char*> requiredDeviceExtensions_ = {vk::KHRSwapchainExtensionName, vk::KHRMaintenance5ExtensionName};
    // Declaration order dictates cleanup order
    vk::raii::Context context_;
    vk::raii::Instance instance_{nullptr};
    vk::raii::SurfaceKHR surface_{nullptr};
    vk::raii::PhysicalDevice physicalDevice_{nullptr};
    vk::raii::DebugUtilsMessengerEXT debugMessenger_{nullptr};
    vk::raii::Device device_{nullptr};
    vk::raii::Queue queue_{nullptr};
    vk::raii::SwapchainKHR swapChain_{nullptr};
    vk::Extent2D swapChainExtent_;
    std::vector<vk::Image> swapChainImages_;
    vk::SurfaceFormatKHR swapChainSurfaceFormat_;
    std::vector<vk::raii::ImageView> swapChainImageViews_;
    vk::raii::PipelineLayout pipelineLayout_{nullptr};
    vk::raii::Pipeline graphicsPipeline_ = nullptr;


};
}
