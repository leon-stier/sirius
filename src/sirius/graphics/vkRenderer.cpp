#include "vkRenderer.h"

#include "vulkan/vulkan_core.h"

#include "window/wndProc.h"
#include <vulkan/vulkan_win32.h>

namespace sirius {
void VkRenderer::Init() {
    CreateInstance();
    SetupDebugMessenger();
    CreateSurface();
    PickPhysicalDevice();
    CreateLogicalDevice();
    CreateSwapChain();
    CreateImageViews();
    CreateGraphicsPipeline();
}

void VkRenderer::Draw() {
}

void VkRenderer::CreateInstance() {
    constexpr vk::ApplicationInfo appInfo{
        .pApplicationName = "Hello World",
        .applicationVersion = vk::makeVersion(0, 1, 0),
        .pEngineName = "Sirius",
        .engineVersion = vk::makeVersion(1, 0, 0),
        .apiVersion = vk::ApiVersion14
    };

    vk::InstanceCreateInfo instanceCreateInfo{
        .pApplicationInfo = &appInfo
    };

    std::vector requiredExtensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
    };
    if (enableValidationLayers) requiredExtensions.push_back(vk::EXTDebugUtilsExtensionName);

    instanceCreateInfo.enabledExtensionCount = requiredExtensions.size();
    instanceCreateInfo.ppEnabledExtensionNames = requiredExtensions.data();

    // Get validation layers
    std::vector<char const*> requiredLayers;
    if (enableValidationLayers) {
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

void VkRenderer::CreateSurface() {
    vk::Win32SurfaceCreateInfoKHR surfaceCreateInfo{
        .hinstance = hInstance,
        .hwnd = hwndMain
    };

    surface_ = instance_.createWin32SurfaceKHR(surfaceCreateInfo);
}

void VkRenderer::PickPhysicalDevice() {
    auto physicalDevices = instance_.enumeratePhysicalDevices();
    if (physicalDevices.empty()) throw std::runtime_error("Failed to find GPUs with Vulkan support!");

    // Get the first device that satisfies the conditions
    if (const auto devIter = std::ranges::find_if(physicalDevices, [this](const auto& dev) { return IsDeviceSuitable(dev); }); devIter != physicalDevices.end()) {
        physicalDevice_ = *devIter;
    } else {
        throw std::runtime_error("failed to find a suitable GPU!");
    }
}

bool VkRenderer::IsDeviceSuitable(vk::raii::PhysicalDevice const& physicalDevice) {
    // 1. API version check
    bool supportsVulkan13 = physicalDevice.getProperties().apiVersion >= vk::ApiVersion13;

    // 2. Queue family check
    auto queueFamilies = physicalDevice.getQueueFamilyProperties();
    bool supportsGraphics = std::ranges::any_of(queueFamilies, [](const auto& qfp) {
        return static_cast<bool>(qfp.queueFlags & vk::QueueFlagBits::eGraphics);
    });

    // 3. Extension check
    auto availableExtensions = physicalDevice.enumerateDeviceExtensionProperties();
    bool supportsAllRequiredExtensions = std::ranges::all_of(requiredDeviceExtensions_, [&](std::string_view required) {
        return std::ranges::contains(availableExtensions, required, [](const auto& ext) {
            return std::string_view(ext.extensionName);
        });
    });

    // 4. Feature checks (Note: extendedDynamicState is Vulkan 1.3 core)
    auto features = physicalDevice.getFeatures2<
        vk::PhysicalDeviceFeatures2,
        vk::PhysicalDeviceVulkan11Features,
        vk::PhysicalDeviceVulkan13Features,
        vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT,
        vk::PhysicalDeviceMaintenance5FeaturesKHR
    >();

    bool supportsRequiredFeatures =
        features.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
        features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
        features.get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState &&
        features.get<vk::PhysicalDeviceMaintenance5FeaturesKHR>().maintenance5;
    return supportsVulkan13 && supportsGraphics && supportsAllRequiredExtensions && supportsRequiredFeatures;

}

void VkRenderer::CreateLogicalDevice() {
    std::vector queueFamilyProperties = physicalDevice_.getQueueFamilyProperties();

    auto enumeratedProperties = queueFamilyProperties | std::views::enumerate;

    auto it = std::ranges::find_if(enumeratedProperties, [this](const auto& tuple) {
        auto [index, qfp] = tuple;

        const bool supportsGraphics = static_cast<bool>(qfp.queueFlags & vk::QueueFlagBits::eGraphics);
        const bool supportsSurface  = physicalDevice_.getSurfaceSupportKHR(static_cast<uint32_t>(index), *surface_);

        return supportsGraphics && supportsSurface;
    });

    if (it == enumeratedProperties.end()) {
        throw std::runtime_error("Failed to find a queue family supporting graphics and presentation!");
    }

    const uint32_t graphicsIndex = static_cast<uint32_t>(std::get<0>(*it));

    vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT, vk::PhysicalDeviceMaintenance5FeaturesKHR> featureChain = {
        {},                                    // vk::PhysicalDeviceFeatures2
        {.shaderDrawParameters = true},        // vk::PhysicalDeviceVulkan11Features
        {.dynamicRendering = true},            // vk::PhysicalDeviceVulkan13Features
        {.extendedDynamicState = true},         // vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
        {.maintenance5 = true}
    };

    float queuePriority = 0.5f;
    vk::DeviceQueueCreateInfo deviceQueueCreateInfo{
        .queueFamilyIndex = graphicsIndex,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority
    };

    vk::PhysicalDeviceFeatures deviceFeatures{};    // Filled as features are needed

    vk::DeviceCreateInfo deviceCreateInfo{
        .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &deviceQueueCreateInfo,
        .enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtensions_.size()),
        .ppEnabledExtensionNames = requiredDeviceExtensions_.data()
    };

    device_ = vk::raii::Device(physicalDevice_, deviceCreateInfo);
    queue_ = vk::raii::Queue(device_, graphicsIndex, 0);
}

void VkRenderer::CreateSwapChain() {
    const auto surfaceCapabilities = physicalDevice_.getSurfaceCapabilitiesKHR(*surface_);
    swapChainExtent_ = ChooseSwapExtent(surfaceCapabilities);
    const uint32_t minImageCount = ChooseSwapMinImageCount(surfaceCapabilities);

    const std::vector availableFormats = physicalDevice_.getSurfaceFormatsKHR(*surface_);
    swapChainSurfaceFormat_ = ChooseSwapSurfaceFormat(availableFormats);

    const std::vector availablePresentModes = physicalDevice_.getSurfacePresentModesKHR(*surface_);
    const vk::PresentModeKHR presentMode = ChooseSwapPresentMode(availablePresentModes);

    const vk::SwapchainCreateInfoKHR swapChainCreateInfo{
        .surface = *surface_,
        .minImageCount = minImageCount,
        .imageFormat = swapChainSurfaceFormat_.format,
        .imageColorSpace = swapChainSurfaceFormat_.colorSpace,
        .imageExtent = swapChainExtent_,
        .imageArrayLayers = 1,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode = vk::SharingMode::eExclusive,
        .preTransform = surfaceCapabilities.currentTransform,
        .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode = presentMode,
        .clipped = true
    };

    swapChain_ = vk::raii::SwapchainKHR(device_, swapChainCreateInfo);
    swapChainImages_ = swapChain_.getImages();
}

vk::SurfaceFormatKHR VkRenderer::ChooseSwapSurfaceFormat(std::vector<vk::SurfaceFormatKHR> const& availableFormats) {
    const auto formatIt = std::ranges::find_if(availableFormats,[](const auto &format) {
        return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
    });
    return formatIt != availableFormats.end() ? *formatIt : availableFormats.front();
}
/*
 * Fifo:    First-in-first-out queue to which the program submits frames. They are presented in that order while waiting for a vertical blank for each presentation
 * Mailbox: Fifo but if the display is waiting for the program (queue empty), present the next frame as soon as it's available without waiting for a vertical blank
 * Pick mailbox if it's available, use Fifo as a fallback. Fifo is guaranteed to be available
 */
vk::PresentModeKHR VkRenderer::ChooseSwapPresentMode(std::vector<vk::PresentModeKHR> const& availablePresentModes) {
    return std::ranges::any_of(availablePresentModes, [](const vk::PresentModeKHR value) {
        return vk::PresentModeKHR::eMailbox == value;
    }) ? vk::PresentModeKHR::eMailbox : vk::PresentModeKHR::eFifo;
}

/*
 * A currentExtent of 0xFFFFFFFF (max of uin32_t) indicates that the extent is not dictated by the window manager, we set it ourselves
 * We set it to something that works well for the window
 * Relevant for high-dpi displays where window coordinates differ from pixel dimensions
 */
vk::Extent2D VkRenderer::ChooseSwapExtent(vk::SurfaceCapabilitiesKHR const& capabilities) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
    {
        return capabilities.currentExtent;
    }
    RECT rect{};
    GetClientRect(hwndMain, &rect);

    const auto width  = static_cast<uint32_t>(rect.right - rect.left);
    const auto height = static_cast<uint32_t>(rect.bottom - rect.top);

    return vk::Extent2D{
        .width  = std::clamp(width,  capabilities.minImageExtent.width,  capabilities.maxImageExtent.width),
        .height = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
    };
}

uint32_t VkRenderer::ChooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const& capabilities) {
    auto minImageCount = std::max(3u, capabilities.minImageCount);
    if ((0 < capabilities.maxImageCount) && (capabilities.maxImageCount < minImageCount))
    {
        minImageCount = capabilities.maxImageCount;
    }
    return minImageCount;
}

void VkRenderer::CreateImageViews() {
    vk::ImageViewCreateInfo imageViewCreateInfo {
        .viewType = vk::ImageViewType::e2D,
        .format = swapChainSurfaceFormat_.format,
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    for (auto &image : swapChainImages_) {
        imageViewCreateInfo.image = image;
        swapChainImageViews_.emplace_back(device_, imageViewCreateInfo);
    }
}

void VkRenderer::CreateGraphicsPipeline() {
    std::vector<uint32_t> shaderCode = ReadFile("shaders/shader.spv");

    // Creating shader modules is deprecated in 1.4, just pass the create info directly to the pipeline
    vk::ShaderModuleCreateInfo shaderModuleCreateInfo {
        .codeSize = shaderCode.size() * sizeof(uint32_t),
        .pCode = shaderCode.data()
    };

    vk::PipelineShaderStageCreateInfo vertStageCreateInfo {
        .pNext = shaderModuleCreateInfo,
        .stage = vk::ShaderStageFlagBits::eVertex,
        .module = nullptr,
        .pName = "vertMain"
    };

    vk::PipelineShaderStageCreateInfo fragStageCreateInfo {
        .pNext = shaderModuleCreateInfo,
        .stage = vk::ShaderStageFlagBits::eFragment,
        .module = nullptr,
        .pName = "fragMain"
    };
    vk::PipelineShaderStageCreateInfo shaderStages[] = { vertStageCreateInfo, fragStageCreateInfo };

    // Specify which states are dynamic, i.e., will be skipped in the pipeline creation and must be specified at draw time. This allows for changing them without having to recreate the pipeline.
    std::vector dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamicStateCreateInfo{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()
    };

    // No vertex input for now as it's hardcoded in the shader
    vk::PipelineVertexInputStateCreateInfo vertexInputStateCreateInfo;

    vk::PipelineInputAssemblyStateCreateInfo inputAssemblyStateCreateInfo{
        .topology = vk::PrimitiveTopology::eTriangleList,
    };

    vk::PipelineViewportStateCreateInfo viewportStateCreateInfo{
        .viewportCount = 1,
        .scissorCount = 1
    };


    vk::PipelineRasterizationStateCreateInfo rasterizerCreateInfo{
        .depthClampEnable = vk::False,
        .rasterizerDiscardEnable = vk::False,
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eBack,
        .frontFace = vk::FrontFace::eClockwise,
        .depthBiasEnable = vk::False,
        .lineWidth = 1.0f
    };

    vk::PipelineMultisampleStateCreateInfo multisampling{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable = vk::False
    };

    vk::PipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable    = vk::False,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
    };

    vk::PipelineColorBlendStateCreateInfo colorBlendingCreateInfo{
        .logicOpEnable = vk::False,
        .logicOp = vk::LogicOp::eCopy,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment
    };

    vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo {
        .setLayoutCount = 0,
        .pushConstantRangeCount = 0
    };
    pipelineLayout_ = vk::raii::PipelineLayout(device_, pipelineLayoutCreateInfo);

    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipelineCreateInfoChain = {
        {
            .stageCount          = 2,
            .pStages             = shaderStages,
            .pVertexInputState   = &vertexInputStateCreateInfo,
            .pInputAssemblyState = &inputAssemblyStateCreateInfo,
            .pViewportState      = &viewportStateCreateInfo,
            .pRasterizationState = &rasterizerCreateInfo,
            .pMultisampleState   = &multisampling,
            .pColorBlendState    = &colorBlendingCreateInfo,
            .pDynamicState       = &dynamicStateCreateInfo,
            .layout              = pipelineLayout_,
            .renderPass          = nullptr
        },
    {
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &swapChainSurfaceFormat_.format
        }
    };

    graphicsPipeline_ = vk::raii::Pipeline(device_, nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());

}

void VkRenderer::SetupDebugMessenger() {
    if (!enableValidationLayers) return;
    constexpr vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
    constexpr vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);
    constexpr vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfo{
        .messageSeverity = severityFlags,
        .messageType = messageTypeFlags,
        .pfnUserCallback = &DebugCallback
    };
    debugMessenger_ = instance_.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfo);
}

vk::Bool32 VkRenderer::DebugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT type, const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
    std::cerr << "validation layer: type " << to_string(type) << " msg: " << pCallbackData->pMessage << std::endl;

    return vk::False;
}


}
