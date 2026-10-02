#pragma once

#include <glm/fwd.hpp>

#include "vulkan/vulkan_raii.hpp"


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
    ~VulkanContext() {
        if (*device_) {
            device_.waitIdle();
        }
    }

    void Init();

    vk::raii::Instance& Instance();

    vk::raii::PhysicalDevice& PhysicalDevice();

    vk::raii::Device& Device();

    vk::raii::Queue& GraphicsQueue();

    glm::uint32_t GraphicsQueueIndex() const;

    vk::raii::SurfaceKHR& Surface();

private:
    void CreateInstance();

    void SetupDebugMessenger();

    static VKAPI_ATTR vk::Bool32 VKAPI_CALL DebugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT type, const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);

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
};
