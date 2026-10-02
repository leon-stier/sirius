#include "vulkanContext.h"

#include <iostream>
#include <ranges>
#include <stdexcept>
#include "window/wndProc.h"

const std::vector kValidationLayers = {
    "VK_LAYER_KHRONOS_validation",
};

constexpr uint32_t kMaxFramesInFlight{2};


#ifdef NDEBUG
constexpr bool kEnableValidationLayers = false;
#else
constexpr bool kEnableValidationLayers = true;
#endif


// Helper functions to be able to define physical device requirements once and then check and use them with one call where needed
namespace {
bool SupportsCoreFeatures(const vk::PhysicalDeviceFeatures2& required, const vk::PhysicalDeviceFeatures2& queried) {
    constexpr size_t count = sizeof(vk::PhysicalDeviceFeatures2) / sizeof(vk::Bool32);

    const auto reqFlags = std::span(reinterpret_cast<const vk::Bool32*>(&required.features), count);
    const auto queriedFlags = std::span(reinterpret_cast<const vk::Bool32*>(&queried.features), count);

    for (size_t i = 0; i < count; ++i) {
        if (reqFlags[i] == vk::True && queriedFlags[i] != vk::True) {
            return false;
        }
    }
    return true;
}

template<typename T>
bool SupportsAllRequiredFeatures(const T& required, const T& queried) {
    constexpr size_t headerSize = sizeof(vk::StructureType) + sizeof(void*);

    const auto reqFlags = std::span(
        reinterpret_cast<const vk::Bool32*>(reinterpret_cast<const char*>(&required) + headerSize),
        (sizeof(T) - headerSize) / sizeof(vk::Bool32)
    );

    const auto queriedFlags = std::span(
        reinterpret_cast<const vk::Bool32*>(reinterpret_cast<const char*>(&queried) + headerSize),
        (sizeof(T) - headerSize) / sizeof(vk::Bool32)
    );

    for (size_t i = 0; i < reqFlags.size(); ++i) {
        if (reqFlags[i] == vk::True && queriedFlags[i] != vk::True) {
            return false;
        }
    }
    return true;
}

// 3. Helper to chain pNext pointers in a std::tuple
template<typename Tuple, std::size_t... Is>
void ChainPNext(Tuple& tuple, std::index_sequence<Is...>) {
    ((std::get<Is>(tuple).pNext = &std::get<Is + 1>(tuple)), ...);
}

template<typename Tuple>
void SetupFeatureChain(Tuple& tuple) {
    constexpr auto size = std::tuple_size_v<Tuple>;
    if constexpr (size > 1) {
        ChainPNext(tuple, std::make_index_sequence<size - 1>{});
    }
}

// 4. Automated dynamic features check
template<typename Tuple, std::size_t... Is>
auto QueryFeaturesChain(const vk::raii::PhysicalDevice& device, std::index_sequence<Is...>) {
    return device.getFeatures2<
        vk::PhysicalDeviceFeatures2,
        std::tuple_element_t<Is, Tuple>...
    >();
}

template<typename Tuple>
bool CheckTupleFeatures(
    const vk::raii::PhysicalDevice& device,
    const vk::PhysicalDeviceFeatures2& requiredCoreFeatures,
    const Tuple& requiredTuple
) {
    // 1. Query the device (vk::PhysicalDeviceFeatures2 is automatically the head of chain)
    auto chain = QueryFeaturesChain<Tuple>(
        device,
        std::make_index_sequence<std::tuple_size_v<Tuple>>{}
    );

    // 2. Validate core Vulkan 1.0 features
    if (!SupportsCoreFeatures(requiredCoreFeatures, chain.template get<vk::PhysicalDeviceFeatures2>())) {
        return false;
    }

    // 3. Validate extension features
    return std::apply([&]<typename... T0>(const T0&... reqStructs) {
        return (SupportsAllRequiredFeatures(reqStructs, chain.template get<std::decay_t<T0>>()) && ...);
    }, requiredTuple);
}
}

void VulkanContext::Init() {
    CreateInstance();
    CreateSurface();
    PickPhysicalDevice();
    CreateLogicalDevice();
}

vk::raii::Instance& VulkanContext::Instance() {
    return instance_;
}

vk::raii::PhysicalDevice& VulkanContext::PhysicalDevice() {
    return physicalDevice_;
}

vk::raii::Device& VulkanContext::Device() {
    return device_;
}

vk::raii::Queue& VulkanContext::GraphicsQueue() {
    return graphicsQueue_;
}

glm::uint32_t VulkanContext::GraphicsQueueIndex() const {
    return graphicsQueueIndex_;
}

vk::raii::SurfaceKHR& VulkanContext::Surface() {
    return surface_;
}

void VulkanContext::CreateInstance() {
    constexpr vk::ApplicationInfo appInfo{
        .pApplicationName = "Hello World",
        .applicationVersion = vk::makeVersion(0, 1, 0),
        .pEngineName = "Sirius",
        .engineVersion = vk::makeVersion(1, 0, 0),
        .apiVersion = PhysicalDeviceRequirements::minApiVersion
    };

    vk::InstanceCreateInfo instanceCreateInfo{
        .pApplicationInfo = &appInfo
    };

    std::vector requiredExtensions = {
        vk::KHRSurfaceExtensionName,
        vk::KHRWin32SurfaceExtensionName,
        vk::EXTDebugUtilsExtensionName,
        vk::KHRGetSurfaceCapabilities2ExtensionName
    };
    if (kEnableValidationLayers) requiredExtensions.push_back(vk::EXTDebugUtilsExtensionName);

    instanceCreateInfo.enabledExtensionCount = requiredExtensions.size();
    instanceCreateInfo.ppEnabledExtensionNames = requiredExtensions.data();

    // Get validation layers
    std::vector<char const*> requiredLayers;
    if (kEnableValidationLayers) {
        requiredLayers.assign(kValidationLayers.begin(), kValidationLayers.end());
    }

    // Verify layer support & throw directly if any layer is missing
    auto layerProperties = context_.enumerateInstanceLayerProperties();

    if (const auto it = std::ranges::find_if_not(requiredLayers, [&layerProperties](const std::string_view required) {
        return std::ranges::contains(layerProperties, required, [](const auto& prop) {
            return std::string_view(prop.layerName);
        });
    }); it != requiredLayers.end()) {
        throw std::runtime_error(std::format("Required layer not supported: {}", *it));
    }

    instanceCreateInfo.enabledLayerCount = static_cast<uint32_t>(requiredLayers.size());
    instanceCreateInfo.ppEnabledLayerNames = requiredLayers.data();

    instance_ = vk::raii::Instance(context_, instanceCreateInfo);
}

void VulkanContext::SetupDebugMessenger() {
    if (!kEnableValidationLayers) return;
    constexpr vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
    constexpr vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);
    constexpr vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfo{
        .messageSeverity = severityFlags,
        .messageType = messageTypeFlags,
        .pfnUserCallback = &DebugCallback
    };
    debugMessenger_ = instance_.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfo);
}

vk::Bool32 VulkanContext::DebugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity, const vk::DebugUtilsMessageTypeFlagsEXT type, const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
    std::cerr << "Validation Error of type " << to_string(type) << ". msg: " << pCallbackData->pMessage << std::endl;
    if (pCallbackData->messageIdNumber == 0xde900250) { // WARNING-legacy-gpdp2
        // #if defined(_MSC_VER)
        //         __debugbreak(); // Forces Visual Studio / CLion debugger to pause here
        // #else
        //         __builtin_trap(); // Forces Clang / GCC debugger to pause here
        // #endif
    }

    return vk::False;
}

void VulkanContext::CreateSurface() {
    const vk::Win32SurfaceCreateInfoKHR surfaceCreateInfo{
        .hinstance = hInstance,
        .hwnd = hwndMain
    };

    surface_ = instance_.createWin32SurfaceKHR(surfaceCreateInfo);
}

void VulkanContext::PickPhysicalDevice() {
    auto physicalDevices = instance_.enumeratePhysicalDevices();
    if (physicalDevices.empty()) throw std::runtime_error("Failed to find GPUs with Vulkan support!");

    // Get the first device that satisfies the conditions
    if (const auto devIter = std::ranges::find_if(physicalDevices, [this](const auto& dev) { return IsDeviceSuitable(dev); }); devIter != physicalDevices.end()) {
        physicalDevice_ = *devIter;
    } else {
        throw std::runtime_error("Failed to find a suitable GPU!");
    }
}

void VulkanContext::CreateLogicalDevice() {
    std::vector queueFamilyProperties = physicalDevice_.getQueueFamilyProperties2();

    auto enumeratedProperties = queueFamilyProperties | std::views::enumerate;

    const auto it = std::ranges::find_if(enumeratedProperties, [this](const auto& tuple) {
        auto [index, qfp] = tuple;

        const bool supportsGraphics = static_cast<bool>(qfp.queueFamilyProperties.queueFlags & vk::QueueFlagBits::eGraphics);
        const bool supportsSurface = physicalDevice_.getSurfaceSupportKHR(static_cast<uint32_t>(index), *surface_);

        return supportsGraphics && supportsSurface;
    });

    if (it == enumeratedProperties.end()) {
        throw std::runtime_error("Failed to find a queue family supporting graphics and presentation!");
    }

    graphicsQueueIndex_ = static_cast<uint32_t>(std::get<0>(*it));

    float queuePriority = 0.5f;
    vk::DeviceQueueCreateInfo deviceQueueCreateInfo{
        .queueFamilyIndex = graphicsQueueIndex_,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority
    };

    auto featuresChain = PhysicalDeviceRequirements::requiredFeatures;
    SetupFeatureChain(featuresChain);


    vk::PhysicalDeviceFeatures2 deviceFeatures{
        .pNext = &std::get<0>(featuresChain),
        .features = PhysicalDeviceRequirements::requiredCoreFeatures.features
    };


    vk::DeviceCreateInfo deviceCreateInfo{
        .pNext = &deviceFeatures,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &deviceQueueCreateInfo,
        .enabledExtensionCount = static_cast<uint32_t>(PhysicalDeviceRequirements::extensions.size()),
        .ppEnabledExtensionNames = PhysicalDeviceRequirements::extensions.data()
    };

    device_ = vk::raii::Device(physicalDevice_, deviceCreateInfo);
    graphicsQueue_ = vk::raii::Queue(device_, graphicsQueueIndex_, 0);
}

bool VulkanContext::IsDeviceSuitable(vk::raii::PhysicalDevice const& physicalDevice) const {
    using Req = PhysicalDeviceRequirements;
    // 1. API version check
    if (physicalDevice.getProperties2().properties.apiVersion < Req::minApiVersion) return false;


    // 2. Queue family check
    auto queueFamilies = physicalDevice.getQueueFamilyProperties2();
    if (!std::ranges::any_of(queueFamilies, [](const auto& qfp) {
        return static_cast<bool>(qfp.queueFamilyProperties.queueFlags & Req::queueFlagBits);
    }))
        return false; // Return early if queues are not supported

    // 3. Extension check
    auto availableExtensions = physicalDevice.enumerateDeviceExtensionProperties();
    if (!std::ranges::all_of(Req::extensions, [&](const std::string_view required) {
        return std::ranges::contains(availableExtensions, required, [](const auto& ext) {
            return std::string_view(ext.extensionName);
        });
    }))
        return false; // Return early if extensions are not supported

    return CheckTupleFeatures(physicalDevice, Req::requiredCoreFeatures, Req::requiredFeatures);
}