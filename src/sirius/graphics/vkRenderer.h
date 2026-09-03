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
    void SetupDebugMessenger();
    static VKAPI_ATTR vk::Bool32 VKAPI_CALL DebugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT type, const vk::DebugUtilsMessengerCallbackDataEXT * pCallbackData, void * pUserData);

    std::vector<const char*> requiredDeviceExtensions_ = {vk::KHRSwapchainExtensionName};
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

};
}
