#pragma once

#include <glm/fwd.hpp>


import vulkan;


struct PhysicalDeviceRequirements {
    static constexpr uint32_t minApiVersion = vk::ApiVersion14;

    // Only device extensions. Instance extensions are directly in CreateInstance()
    static inline const std::vector<const char*> extensions = {
        vk::KHRSwapchainExtensionName,
    };

    static constexpr auto queueFlagBits{
        vk::QueueFlagBits::eGraphics
    };

    static constexpr vk::PhysicalDeviceFeatures2 requiredCoreFeatures{
        .features = {.samplerAnisotropy = vk::True}
    };

    static constexpr auto requiredFeatures = std::make_tuple(
        vk::PhysicalDeviceVulkan11Features{.shaderDrawParameters = vk::True},
        vk::PhysicalDeviceVulkan12Features{.timelineSemaphore = vk::True},
        vk::PhysicalDeviceVulkan13Features{.synchronization2 = vk::True, .dynamicRendering = vk::True},
        vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT{.extendedDynamicState = vk::True},
        vk::PhysicalDeviceMaintenance5FeaturesKHR{.maintenance5 = vk::True}
    );
};


class VulkanContext {
public:
    VulkanContext();

    ~VulkanContext() = default;

    vk::raii::Instance& Instance();

    vk::raii::PhysicalDevice& PhysicalDevice();

    vk::raii::Device& Device();

    vk::raii::Queue& GraphicsQueue();

    glm::uint32_t GraphicsQueueFamily() const;

private:
    void CreateInstance();

    void SetupDebugMessenger();
    vk::Bool32 VulkanContext::DebugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT type, const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);

    void CreateSurface();

    void PickPhysicalDevice();

    void CreateLogicalDevice();

    bool IsDeviceSuitable(vk::raii::PhysicalDevice const& physicalDevice) const;

    vk::raii::Context context_;
    vk::raii::Instance instance_{nullptr};
    vk::raii::DebugUtilsMessengerEXT debugMessenger_{nullptr};
    vk::raii::SurfaceKHR surface_{nullptr};

    vk::raii::PhysicalDevice physicalDevice_{nullptr};
    vk::raii::Device device_{nullptr};

    vk::raii::Queue graphicsQueue_{nullptr};
    uint32_t graphicsQueueIndex_{0};
    uint32_t graphicsQueueFamily_{0};
};
