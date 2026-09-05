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
    InitCommandBuffers();
    CreateSyncObjects();
}

void VkRenderer::Draw() {
    const uint32_t frameIndex = frameIndex_++ % kMaxFramesInFlight;
    const uint64_t signalValue = nextSignalValue_++;
    const uint64_t waitValue = signalValue - kMaxFramesInFlight;

    const vk::Semaphore semHandle = *timelineSemaphore_;

    vk::SemaphoreWaitInfo waitInfo {
        .flags = {},
        .semaphoreCount = 1,
        .pSemaphores = &semHandle,
        .pValues = &waitValue
    };
    const vk::Result waitResult{ device_.waitSemaphores(waitInfo, std::numeric_limits<uint64_t>::max()) };
    if (waitResult != vk::Result::eSuccess) {
        // Handle unexpected results (e.g., eTimeout or device loss)
        throw std::runtime_error("Failed or timed out waiting for timeline semaphore!");
    }

    auto [acquireResult, imageIndex] = swapChain_.acquireNextImage(std::numeric_limits<uint64_t>::max(), *frames_[frameIndex].imageAcquiredSemaphore, nullptr);
    // Result can also indicate out of date images
    if (acquireResult != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to acquire next image!");
    }

    RecordCommandBuffer(imageIndex, frameIndex);

    const vk::SemaphoreSubmitInfo imageAcquireWaitInfo {
        .semaphore = *frames_[frameIndex].imageAcquiredSemaphore,
        .stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput
    };

    const std::array<vk::SemaphoreSubmitInfo, 2> semaphoreSignals{{
        {
            .semaphore = *renderCompleteSemaphores_[imageIndex],
            .stageMask = vk::PipelineStageFlagBits2::eAllGraphics
        },
        {
            .semaphore = *timelineSemaphore_,
            .value     = signalValue,
            .stageMask = vk::PipelineStageFlagBits2::eAllCommands
        }
    }};

    const vk::CommandBufferSubmitInfo cmdSubmitInfo{
        .commandBuffer = *frames_.at(frameIndex).commandBuffer
    };

    // 2. Modern SubmitInfo2
    const vk::SubmitInfo2 submitInfo{
        .waitSemaphoreInfoCount   = 1,
        .pWaitSemaphoreInfos      = &imageAcquireWaitInfo,
        .commandBufferInfoCount   = 1,
        .pCommandBufferInfos      = &cmdSubmitInfo,
        .signalSemaphoreInfoCount = static_cast<uint32_t>(semaphoreSignals.size()),
        .pSignalSemaphoreInfos    = semaphoreSignals.data()
    };

    // 3. Modern Queue Submit
    graphicsQueue_.submit2(submitInfo, nullptr);

    const vk::PresentInfoKHR presentInfoKHR{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &*renderCompleteSemaphores_[imageIndex],
        .swapchainCount     = 1,
        .pSwapchains        = &*swapChain_,
        .pImageIndices      = &imageIndex};

    vk::Result presentResult = graphicsQueue_.presentKHR(presentInfoKHR);

    switch (presentResult)
    {
        case vk::Result::eSuccess:
            break;
        case vk::Result::eSuboptimalKHR:
            std::cout << "vk::Queue::presentKHR returned vk::Result::eSuboptimalKHR !\n";
            break;
        default:
            break;        // an unexpected result is returned!
    }
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
        const bool supportsSurface = physicalDevice_.getSurfaceSupportKHR(static_cast<uint32_t>(index), *surface_);

        return supportsGraphics && supportsSurface;
    });

    if (it == enumeratedProperties.end()) {
        throw std::runtime_error("Failed to find a queue family supporting graphics and presentation!");
    }

    graphicsQueueIndex_ = static_cast<uint32_t>(std::get<0>(*it));

    vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT, vk::PhysicalDeviceMaintenance5FeaturesKHR> featureChain = {
        {}, // vk::PhysicalDeviceFeatures2
        {.shaderDrawParameters = true}, // vk::PhysicalDeviceVulkan11Features
        {.timelineSemaphore = true},
        {.dynamicRendering = true}, // vk::PhysicalDeviceVulkan13Features
        {.extendedDynamicState = true}, // vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
        {.maintenance5 = true}
    };

    float queuePriority = 0.5f;
    vk::DeviceQueueCreateInfo deviceQueueCreateInfo{
        .queueFamilyIndex = graphicsQueueIndex_,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority
    };

    vk::PhysicalDeviceFeatures deviceFeatures{}; // Filled as features are needed

    vk::DeviceCreateInfo deviceCreateInfo{
        .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &deviceQueueCreateInfo,
        .enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtensions_.size()),
        .ppEnabledExtensionNames = requiredDeviceExtensions_.data()
    };

    device_ = vk::raii::Device(physicalDevice_, deviceCreateInfo);
    graphicsQueue_ = vk::raii::Queue(device_, graphicsQueueIndex_, 0);
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
    const auto formatIt = std::ranges::find_if(availableFormats, [](const auto& format) {
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
    })
               ? vk::PresentModeKHR::eMailbox
               : vk::PresentModeKHR::eFifo;
}

/*
 * A currentExtent of 0xFFFFFFFF (max of uin32_t) indicates that the extent is not dictated by the window manager, we set it ourselves
 * We set it to something that works well for the window
 * Relevant for high-dpi displays where window coordinates differ from pixel dimensions
 */
vk::Extent2D VkRenderer::ChooseSwapExtent(vk::SurfaceCapabilitiesKHR const& capabilities) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }
    RECT rect{};
    GetClientRect(hwndMain, &rect);

    const auto width = static_cast<uint32_t>(rect.right - rect.left);
    const auto height = static_cast<uint32_t>(rect.bottom - rect.top);

    return vk::Extent2D{
        .width = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
        .height = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
    };
}

uint32_t VkRenderer::ChooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const& capabilities) {
    auto minImageCount = std::max(3u, capabilities.minImageCount);
    if ((0 < capabilities.maxImageCount) && (capabilities.maxImageCount < minImageCount)) {
        minImageCount = capabilities.maxImageCount;
    }
    return minImageCount;
}

void VkRenderer::CreateImageViews() {
    vk::ImageViewCreateInfo imageViewCreateInfo{
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

    for (auto& image : swapChainImages_) {
        imageViewCreateInfo.image = image;
        swapChainImageViews_.emplace_back(device_, imageViewCreateInfo);
    }
}

void VkRenderer::CreateGraphicsPipeline() {
    std::vector<uint32_t> shaderCode = ReadFile("shaders/shader.spv");

    // Creating shader modules is deprecated in 1.4, just pass the create info directly to the pipeline
    vk::ShaderModuleCreateInfo shaderModuleCreateInfo{
        .codeSize = shaderCode.size() * sizeof(uint32_t),
        .pCode = shaderCode.data()
    };

    vk::PipelineShaderStageCreateInfo vertStageCreateInfo{
        .pNext = shaderModuleCreateInfo,
        .stage = vk::ShaderStageFlagBits::eVertex,
        .module = nullptr,
        .pName = "vertMain"
    };

    vk::PipelineShaderStageCreateInfo fragStageCreateInfo{
        .pNext = shaderModuleCreateInfo,
        .stage = vk::ShaderStageFlagBits::eFragment,
        .module = nullptr,
        .pName = "fragMain"
    };
    vk::PipelineShaderStageCreateInfo shaderStages[] = {vertStageCreateInfo, fragStageCreateInfo};

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
        .blendEnable = vk::False,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
    };

    vk::PipelineColorBlendStateCreateInfo colorBlendingCreateInfo{
        .logicOpEnable = vk::False,
        .logicOp = vk::LogicOp::eCopy,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment
    };

    vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo{
        .setLayoutCount = 0,
        .pushConstantRangeCount = 0
    };
    pipelineLayout_ = vk::raii::PipelineLayout(device_, pipelineLayoutCreateInfo);

    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipelineCreateInfoChain = {
        {
            .stageCount = 2,
            .pStages = shaderStages,
            .pVertexInputState = &vertexInputStateCreateInfo,
            .pInputAssemblyState = &inputAssemblyStateCreateInfo,
            .pViewportState = &viewportStateCreateInfo,
            .pRasterizationState = &rasterizerCreateInfo,
            .pMultisampleState = &multisampling,
            .pColorBlendState = &colorBlendingCreateInfo,
            .pDynamicState = &dynamicStateCreateInfo,
            .layout = pipelineLayout_,
            .renderPass = nullptr
        },
        {
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &swapChainSurfaceFormat_.format
        }
    };

    graphicsPipeline_ = vk::raii::Pipeline(device_, nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
}

void VkRenderer::InitCommandBuffers() {
    for (FrameData &frame : frames_) {
        const vk::CommandPoolCreateInfo poolCreateInfo{
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = graphicsQueueIndex_
        };
        frame.commandPool = vk::raii::CommandPool(device_, poolCreateInfo);

        const vk::CommandBufferAllocateInfo allocInfo {
            .commandPool = frame.commandPool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = kMaxFramesInFlight
        };

        frame.commandBuffer = std::move(vk::raii::CommandBuffers(device_, allocInfo).front());
    }
}

void VkRenderer::CreateSyncObjects() {
    vk::SemaphoreTypeCreateInfo semaphoreTypeCreateInfo{
        .semaphoreType = vk::SemaphoreType::eTimeline,
        .initialValue = kMaxFramesInFlight,
    };
    const vk::SemaphoreCreateInfo timelineSemaphoreCreateInfo{
        .sType = vk::StructureType::eSemaphoreCreateInfo,
        .pNext = &semaphoreTypeCreateInfo
    };
    timelineSemaphore_ = vk::raii::Semaphore(device_, timelineSemaphoreCreateInfo);

    for (FrameData &frame : frames_) {
        frame.imageAcquiredSemaphore = vk::raii::Semaphore(device_, vk::SemaphoreCreateInfo());
    }

    for (size_t i = 0; i < swapChainImages_.size(); i++)
    {
        renderCompleteSemaphores_.emplace_back(device_, vk::SemaphoreCreateInfo());
    }
}

void VkRenderer::RecordCommandBuffer(uint32_t imageIndex, uint32_t frameIndex) const {

    frames_.at(frameIndex).commandBuffer.begin({});

    // Transition the image layout for rendering
    TransitionImageLayout(
        imageIndex,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eColorAttachmentOptimal,
        {},
        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput
    );

    // Set up the color attachment
    constexpr vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);

    vk::RenderingAttachmentInfo attachmentInfo = {
        .imageView = swapChainImageViews_.at(imageIndex),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = clearColor
    };

    // Set up the rendering info
    const vk::RenderingInfo renderingInfo = {
        .renderArea = {
            .offset = {
                .x = 0,
                .y = 0
            },
            .extent = swapChainExtent_
        },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachmentInfo
    };

    // Begin rendering
    frames_.at(frameIndex).commandBuffer.beginRendering(renderingInfo);

    frames_.at(frameIndex).commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *graphicsPipeline_);
    frames_.at(frameIndex).commandBuffer.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(swapChainExtent_.width), static_cast<float>(swapChainExtent_.height), 0.0f, 1.0f));
    frames_.at(frameIndex).commandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), swapChainExtent_));
    frames_.at(frameIndex).commandBuffer.draw(3, 1, 0, 0);

    // End rendering
    frames_.at(frameIndex).commandBuffer.endRendering();

    // Transition the image layout for presentation
    TransitionImageLayout(
        imageIndex,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::ePresentSrcKHR,
        vk::AccessFlagBits2::eColorAttachmentWrite,
        {},
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::PipelineStageFlagBits2::eBottomOfPipe
    );

    frames_.at(frameIndex).commandBuffer.end();
}

void VkRenderer::SetupDebugMessenger() {
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

vk::Bool32 VkRenderer::DebugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT type, const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
    std::cerr << "validation layer: type " << to_string(type) << " msg: " << pCallbackData->pMessage << std::endl;

    return vk::False;
}

void VkRenderer::TransitionImageLayout(
    const uint32_t imageIndex,
    const vk::ImageLayout oldLayout,
    const vk::ImageLayout newLayout,
    const vk::AccessFlags2 srcAccessMask,
    const vk::AccessFlags2 dstAccessMask,
    const vk::PipelineStageFlags2 srcStageMask,
    const vk::PipelineStageFlags2 dstStageMask) const {

    vk::ImageMemoryBarrier2 barrier = {
        .srcStageMask        = srcStageMask,
        .srcAccessMask       = srcAccessMask,
        .dstStageMask        = dstStageMask,
        .dstAccessMask       = dstAccessMask,
        .oldLayout           = oldLayout,
        .newLayout           = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = swapChainImages_[imageIndex],
        .subresourceRange    = {
            .aspectMask     = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1
        }
    };

    const vk::DependencyInfo dependencyInfo = {
        .dependencyFlags         = {},
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &barrier
    };

    frames_.at(frameIndex_).commandBuffer.pipelineBarrier2(dependencyInfo);
}
}
