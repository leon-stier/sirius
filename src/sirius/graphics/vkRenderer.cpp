#define TINYOBJLOADER_IMPLEMENTATION

#include "vkRenderer.h"
#include "window/wndProc.h"

#include <glm/gtc/matrix_transform.hpp>




namespace {
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
    // Unpacks tuple element types: device.getFeatures2<T0, T1, T2...>()
    return device.getFeatures2<
        vk::PhysicalDeviceFeatures2,
        std::tuple_element_t<Is, Tuple>...
    >();
}

template<typename Tuple>
bool CheckTupleFeatures(const vk::raii::PhysicalDevice& device, const Tuple& requiredTuple) {
    // 1. Query the device using the types inside requiredTuple
    auto chain = QueryFeaturesChain<Tuple>(
        device,
        std::make_index_sequence<std::tuple_size_v<Tuple>>{}
    );

    // 2. Validate every required feature struct against what getFeatures2 returned
    return std::apply([&]<typename... T0>(const T0&... reqStructs) {
        // chain.get<T>() retrieves the queried structure for type T from the returned chain
        return (SupportsAllRequiredFeatures(reqStructs, chain.template get<std::decay_t<T0>>()) && ...);
    }, requiredTuple);
}
}

namespace sirius {
void VkRenderer::Init() {
    CreateInstance();
    SetupDebugMessenger();
    CreateSurface();
    PickPhysicalDevice();
    CreateLogicalDevice();
    CreateSwapChain();
    CreateImageViews();
    CreateDescriptorSetLayout();
    CreateGraphicsPipeline();
    InitCommandBuffers();
    CreateDepthResources();
    LoadModel();
    CreateVertexBuffer();
    CreateIndexBuffer();
    CreateUniformBuffers();
    CreateDescriptorPool();
    CreateDescriptorSets();
    CreateSyncObjects();
    InputManager::Subscribe([this](const InputEvent& e) { ProcessCameraEvent(e); });
}

void VkRenderer::Draw() {
    if (pauseRendering) {
        requireSwapChainRecreate_ = true;
        WaitMessage();
        return;
    }
    try {
        DoDraw();
    } catch (vk::SystemError& err) {
        const auto result = static_cast<vk::Result>(err.code().value());

        std::cerr << "Vulkan SystemError\n"
                << "  Message: " << err.what() << "\n"
                << "  Result Code: " << vk::to_string(result) << " (" << err.code().value() << ")\n";
    } catch (const std::exception& err) {
        // Fallback for non-Vulkan standard exceptions
        std::cerr << "Non-Vulkan Exception while Rendering: " << err.what() << "\n";
    }
}

void VkRenderer::CreateInstance() {
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
        vk::EXTDebugUtilsExtensionName
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
    const vk::Win32SurfaceCreateInfoKHR surfaceCreateInfo{
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
        throw std::runtime_error("Failed to find a suitable GPU!");
    }
}

bool VkRenderer::IsDeviceSuitable(vk::raii::PhysicalDevice const& physicalDevice) {
    using Req = PhysicalDeviceRequirements;
    // 1. API version check
    if (physicalDevice.getProperties().apiVersion < Req::minApiVersion) return false;


    // 2. Queue family check
    auto queueFamilies = physicalDevice.getQueueFamilyProperties();
    if (!std::ranges::any_of(queueFamilies, [](const auto& qfp) {
        return static_cast<bool>(qfp.queueFlags & Req::queueFlagBits);
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

    return CheckTupleFeatures(physicalDevice, Req::requiredFeatures);
}

void VkRenderer::CreateLogicalDevice() {
    std::vector queueFamilyProperties = physicalDevice_.getQueueFamilyProperties();

    auto enumeratedProperties = queueFamilyProperties | std::views::enumerate;

    const auto it = std::ranges::find_if(enumeratedProperties, [this](const auto& tuple) {
        auto [index, qfp] = tuple;

        const bool supportsGraphics = static_cast<bool>(qfp.queueFlags & vk::QueueFlagBits::eGraphics);
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

    vk::PhysicalDeviceFeatures2 deviceFeatures;
    deviceFeatures.pNext = &std::get<0>(featuresChain);


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

void VkRenderer::RecreateSwapChain() {
    device_.waitIdle();
    swapChainImageViews_.clear();
    swapChain_ = nullptr;
    CreateSwapChain();
    CreateImageViews();
    requireSwapChainRecreate_ = false;
    std::cout << "Recreated SwapChain" << std::endl;
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
    if (0 < capabilities.maxImageCount && capabilities.maxImageCount < minImageCount) {
        minImageCount = capabilities.maxImageCount;
    }
    return minImageCount;
}

void VkRenderer::CreateImageViews() {
    swapChainImageViews_.reserve(swapChainImages_.size());

    for (const auto& image : swapChainImages_) {
        swapChainImageViews_.emplace_back(CreateImageView(image, swapChainSurfaceFormat_.format, vk::ImageAspectFlagBits::eColor));
    }
}

vk::raii::ImageView VkRenderer::CreateImageView(vk::Image const& image, const vk::Format format, const vk::ImageAspectFlags aspectFlags) const {
    const vk::ImageViewCreateInfo viewCreateInfo {
        .image = image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange = {
            .aspectMask = aspectFlags,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount =  1
        }
    };

    return vk::raii::ImageView(device_, viewCreateInfo);
}

void VkRenderer::CreateDescriptorSetLayout() {
    vk::DescriptorSetLayoutBinding uboLayoutBinding {
        .binding = 0,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eVertex
    };
    vk::DescriptorSetLayoutCreateInfo layoutCreateInfo{
        .bindingCount = 1,
        .pBindings = &uboLayoutBinding
    };
    descriptorSetLayout_ = vk::raii::DescriptorSetLayout(device_, layoutCreateInfo);
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
    auto bindingDescription = Vertex::GetBindingDescription();
    auto attributeDescriptions = Vertex::GetAttributeDescriptions();
    vk::PipelineVertexInputStateCreateInfo vertexInputStateCreateInfo{
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &bindingDescription,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
        .pVertexAttributeDescriptions = attributeDescriptions.data()
    };

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
        .frontFace = vk::FrontFace::eCounterClockwise,
        .depthBiasEnable = vk::False,
        .lineWidth = 1.0f
    };

    vk::PipelineMultisampleStateCreateInfo multisampling{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable = vk::False
    };

    vk::Format depthFormat = FindDepthFormat();
    vk::PipelineDepthStencilStateCreateInfo depthStencil{
        .depthTestEnable = vk::True,
        .depthWriteEnable = vk::True,
        .depthCompareOp = vk::CompareOp::eLess,
        .depthBoundsTestEnable = vk::False,
        .stencilTestEnable = vk::False
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
        .setLayoutCount = 1,
        .pSetLayouts = &*descriptorSetLayout_,
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
            .pDepthStencilState = &depthStencil,
            .pColorBlendState = &colorBlendingCreateInfo,
            .pDynamicState = &dynamicStateCreateInfo,
            .layout = pipelineLayout_,
            .renderPass = nullptr
        },
        {
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &swapChainSurfaceFormat_.format,
            .depthAttachmentFormat = depthFormat
        }
    };

    graphicsPipeline_ = vk::raii::Pipeline(device_, nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
}

void VkRenderer::InitCommandBuffers() {
    for (FrameContext& frame : frames_) {
        const vk::CommandPoolCreateInfo poolCreateInfo{
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = graphicsQueueIndex_
        };
        frame.commandPool = vk::raii::CommandPool(device_, poolCreateInfo);

        const vk::CommandBufferAllocateInfo allocInfo{
            .commandPool = frame.commandPool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = 1
        };

        frame.commandBuffer = std::move(vk::raii::CommandBuffers(device_, allocInfo).front());
    }

    const vk::CommandPoolCreateInfo poolCreateInfo{
        .flags = vk::CommandPoolCreateFlagBits::eTransient,
        .queueFamilyIndex = graphicsQueueIndex_
    };

    ephemeralCommandPool_ = vk::raii::CommandPool(device_, poolCreateInfo);
}

std::pair<vk::raii::Image, vk::raii::DeviceMemory> VkRenderer::CreateImage(const uint32_t width, const uint32_t height, const vk::Format format, const vk::ImageTiling tiling, const vk::ImageUsageFlags usage, const vk::MemoryPropertyFlags properties) const {
    const vk::ImageCreateInfo imageInfo{
        .imageType   = vk::ImageType::e2D,
        .format      = format,
        .extent      = {.width = width, .height = height, .depth = 1},
        .mipLevels   = 1,
        .arrayLayers = 1,
        .samples     = vk::SampleCountFlagBits::e1,
        .tiling      = tiling,
        .usage       = usage,
        .sharingMode = vk::SharingMode::eExclusive
    };

    auto image = vk::raii::Image(device_, imageInfo);

    const vk::MemoryRequirements memRequirements = image.getMemoryRequirements();
    const vk::MemoryAllocateInfo allocInfo{
        .allocationSize  = memRequirements.size,
        .memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties)
    };
    auto imageMemory = vk::raii::DeviceMemory(device_, allocInfo);
    image.bindMemory(imageMemory, 0);

    return {std::move(image), std::move(imageMemory)};
}

vk::Format VkRenderer::FindSupportedFormat(const std::vector<vk::Format>& candidates, const vk::ImageTiling tiling, const vk::FormatFeatureFlags features) const {
    for (const auto format : candidates) {
        vk::FormatProperties props = physicalDevice_.getFormatProperties(format);

        if ((
            tiling == vk::ImageTiling::eLinear && (props.linearTilingFeatures & features) == features) ||
            (tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features))
        {
            return format;
        }
    }

    throw std::runtime_error("Failed to find supported Format!");
}

vk::Format VkRenderer::FindDepthFormat() const {
    return FindSupportedFormat({vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint},vk::ImageTiling::eOptimal, vk::FormatFeatureFlagBits::eDepthStencilAttachment);
}


void VkRenderer::CreateDepthResources() {
    const vk::Format depthFormat = FindDepthFormat();

    std::tie(depthImage_, depthImageMemory_) = CreateImage(swapChainExtent_.width, swapChainExtent_.height, depthFormat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::MemoryPropertyFlagBits::eDeviceLocal);
    depthImageView_ = CreateImageView(depthImage_, depthFormat, vk::ImageAspectFlagBits::eDepth);
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

    for (FrameContext& frame : frames_) {
        frame.imageAcquiredSemaphore = vk::raii::Semaphore(device_, vk::SemaphoreCreateInfo());
    }

    for (size_t i = 0; i < swapChainImages_.size(); i++) {
        renderCompleteSemaphores_.emplace_back(device_, vk::SemaphoreCreateInfo());
    }
}

void VkRenderer::CreateVertexBuffer() {
    const vk::DeviceSize bufferSize = sizeof(vertices_.at(0)) * vertices_.size();

    auto [stagingBuffer, stagingBufferMemory] = CreateBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void* dataStaging = stagingBufferMemory.mapMemory(0, bufferSize);
    memcpy(dataStaging, vertices_.data(), bufferSize);
    stagingBufferMemory.unmapMemory();

    std::tie(vertexBuffer_, vertexBufferMemory_) = CreateBuffer(bufferSize, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);

    CopyBuffer(stagingBuffer, vertexBuffer_, bufferSize);
}

void VkRenderer::CreateIndexBuffer() {
    const vk::DeviceSize bufferSize = sizeof(indices_.at(0)) * indices_.size();

    auto [stagingBuffer, stagingBufferMemory] = CreateBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void* dataStaging = stagingBufferMemory.mapMemory(0, bufferSize);
    memcpy(dataStaging, indices_.data(), bufferSize);
    stagingBufferMemory.unmapMemory();

    std::tie(indexBuffer_, indexBufferMemory_) = CreateBuffer(bufferSize, vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);

    CopyBuffer(stagingBuffer, indexBuffer_, bufferSize);
}

void VkRenderer::CreateUniformBuffers() {
    for (auto& frame : frames_)
    {
        constexpr vk::DeviceSize bufferSize = sizeof(UniformBufferObject);
        auto [buffer, bufferMem]  = CreateBuffer(bufferSize, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        frame.uniformBuffer = std::move(buffer);
        frame.uniformBufferMemory = std::move(bufferMem);
        frame.uniformBufferMapped = frame.uniformBufferMemory.mapMemory(0, bufferSize);
    }
}

void VkRenderer::CreateDescriptorPool() {
    vk::DescriptorPoolSize poolSize {
        .type = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = kMaxFramesInFlight
    };
    vk::DescriptorPoolCreateInfo poolCreateInfo {
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = kMaxFramesInFlight,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize
    };
    descriptorPool_ = vk::raii::DescriptorPool(device_, poolCreateInfo);
}

void VkRenderer::CreateDescriptorSets() {
    std::vector<vk::DescriptorSetLayout> layouts(kMaxFramesInFlight, *descriptorSetLayout_);
    vk::DescriptorSetAllocateInfo allocInfo{
        .descriptorPool = descriptorPool_,
        .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
        .pSetLayouts = layouts.data()
    };

    for (auto&& [frame, set] : std::views::zip(frames_, device_.allocateDescriptorSets(allocInfo))) {
        frame.descriptorSet = std::move(set);
    }

    for (auto& frame : frames_) {
        vk::DescriptorBufferInfo bufferInfo {
            .buffer = frame.uniformBuffer,
            .offset = 0,
            .range =  sizeof(UniformBufferObject)
        };
        vk::WriteDescriptorSet descriptorWrite {
            .dstSet = frame.descriptorSet,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .pBufferInfo = &bufferInfo
        };
        device_.updateDescriptorSets(descriptorWrite, {});
    }
}

std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> VkRenderer::CreateBuffer(const vk::DeviceSize size, const vk::BufferUsageFlags bufferUsage, const vk::MemoryPropertyFlags memoryProperties) const {
    const vk::BufferCreateInfo bufferInfo{
        .size = size,
        .usage = bufferUsage,
        .sharingMode = vk::SharingMode::eExclusive
    };
    auto buffer = vk::raii::Buffer(device_, bufferInfo);

    const vk::MemoryRequirements memRequirements = buffer.getMemoryRequirements();
    const vk::MemoryAllocateInfo memoryAllocateInfo{
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, memoryProperties)
    };
    auto bufferMemory{vk::raii::DeviceMemory(device_, memoryAllocateInfo)};
    buffer.bindMemory(bufferMemory, 0);

    return {std::move(buffer), std::move(bufferMemory)};
}

void VkRenderer::LoadModel() {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, "../../resources/viking_room.obj"))
    {
        throw std::runtime_error(warn + err);
    }

    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            Vertex vertex{};

            vertex.pos = {
                attrib.vertices[3 * index.vertex_index + 0],
                attrib.vertices[3 * index.vertex_index + 1],
                attrib.vertices[3 * index.vertex_index + 2]
            };
            vertex.color = {1.0f, 1.0f, 1.0f};

            vertices_.push_back(vertex);
            indices_.push_back(indices_.size());
        }
    }
}

void VkRenderer::RecordCommandBuffer(const uint32_t imageIndex, const uint32_t currentFrameIndex) const {
    auto& buffer = frames_.at(currentFrameIndex).commandBuffer;
    buffer.begin({});

    // Transition the image layout for rendering
    TransitionImageLayout(
        swapChainImages_[imageIndex],
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eColorAttachmentOptimal,
        {},
        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::ImageAspectFlagBits::eColor,
        currentFrameIndex
    );

    // Transition the depth image to depth attachment optimal layout
    TransitionImageLayout(
        *depthImage_,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eDepthAttachmentOptimal,
        vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
        vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
        vk::ImageAspectFlagBits::eDepth,
        currentFrameIndex
    );

    // Set up the color attachment
    constexpr vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
    constexpr vk::ClearValue clearDepth = vk::ClearDepthStencilValue(1.0f, 0);

    vk::RenderingAttachmentInfo attachmentInfo = {
        .imageView = swapChainImageViews_.at(imageIndex),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = clearColor
    };

    vk::RenderingAttachmentInfo depthAttachmentInfo {
        .imageView = depthImageView_,
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
        .clearValue = clearDepth
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
        .pColorAttachments = &attachmentInfo,
        .pDepthAttachment = &depthAttachmentInfo
    };

    // Begin rendering
    buffer.beginRendering(renderingInfo);

    buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *graphicsPipeline_);
    buffer.setViewport(0, vk::Viewport(0.0f, static_cast<float>(swapChainExtent_.height), static_cast<float>(swapChainExtent_.width), -static_cast<float>(swapChainExtent_.height), 0.0f, 1.0f)); // Inverted height because glm and Vulkan disagree where down is
    buffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), swapChainExtent_));
    buffer.bindVertexBuffers(0, *vertexBuffer_, {0});
    buffer.bindIndexBuffer(*indexBuffer_, 0, vk::IndexTypeValue<decltype(indices_)::value_type>::value);
    buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout_, 0, *frames_.at(currentFrameIndex).descriptorSet, nullptr);

    buffer.drawIndexed(static_cast<uint32_t>(indices_.size()), 1, 0, 0, 0);

    // End rendering
    buffer.endRendering();

    // Transition the image layout for presentation
    TransitionImageLayout(
        swapChainImages_[imageIndex],
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::ePresentSrcKHR,
        vk::AccessFlagBits2::eColorAttachmentWrite,
        {},
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::PipelineStageFlagBits2::eBottomOfPipe,
        vk::ImageAspectFlagBits::eColor,
        currentFrameIndex
    );

    buffer.end();
}

void VkRenderer::UpdateUniformBuffer(uint32_t currentImage, const uint32_t currentFrameIndex) const {
    static auto startTime = std::chrono::high_resolution_clock::now();

    const auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float>(currentTime - startTime).count();
    time = 1;
    UniformBufferObject ubo{};
    ubo.model = rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    ubo.view = lookAt(cameraCoords_, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    ubo.proj = glm::perspective(glm::radians(45.0f), static_cast<float>(swapChainExtent_.width) / static_cast<float>(swapChainExtent_.height), 0.1f, 10.0f);

    memcpy(frames_.at(currentFrameIndex).uniformBufferMapped, &ubo, sizeof(ubo));
}

void VkRenderer::DoDraw() {
    if (requireSwapChainRecreate_) RecreateSwapChain();

    const uint32_t currentFrameIndex = frameIndex_++ % kMaxFramesInFlight;
    const uint64_t signalValue = nextSignalValue_++;
    const uint64_t waitValue = signalValue - kMaxFramesInFlight;

    const vk::Semaphore semHandle = *timelineSemaphore_;

    const vk::SemaphoreWaitInfo waitInfo{
        .flags = {},
        .semaphoreCount = 1,
        .pSemaphores = &semHandle,
        .pValues = &waitValue
    };
    const vk::Result waitResult{device_.waitSemaphores(waitInfo, std::numeric_limits<uint64_t>::max())};
    if (waitResult != vk::Result::eSuccess) {
        // Handle unexpected results (e.g., eTimeout or device loss)
        throw std::runtime_error("Failed or timed out waiting for timeline semaphore!");
    }

    auto [acquireResult, imageIndex] = swapChain_.acquireNextImage(std::numeric_limits<uint64_t>::max(), *frames_[currentFrameIndex].imageAcquiredSemaphore, nullptr);
    // Result can also indicate out of date images
    if (acquireResult == vk::Result::eErrorOutOfDateKHR) {
        requireSwapChainRecreate_ = true;
        return;
    }
    if (acquireResult == vk::Result::eSuboptimalKHR) {
        requireSwapChainRecreate_ = true;
    }
    if (acquireResult != vk::Result::eSuccess && acquireResult != vk::Result::eSuboptimalKHR) {
        throw vk::SystemError(acquireResult, "Failed to acquire next swapchain image");
    }

    UpdateUniformBuffer(imageIndex, currentFrameIndex);

    RecordCommandBuffer(imageIndex, currentFrameIndex);

    const vk::SemaphoreSubmitInfo imageAcquireWaitInfo{
        .semaphore = *frames_[currentFrameIndex].imageAcquiredSemaphore,
        .stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput
    };

    const std::array<vk::SemaphoreSubmitInfo, 2> semaphoreSignals{
        {
            {
                .semaphore = *renderCompleteSemaphores_[imageIndex],
                .stageMask = vk::PipelineStageFlagBits2::eAllGraphics
            },
            {
                .semaphore = *timelineSemaphore_,
                .value = signalValue,
                .stageMask = vk::PipelineStageFlagBits2::eAllCommands
            }
        }
    };

    const vk::CommandBufferSubmitInfo cmdSubmitInfo{
        .commandBuffer = *frames_.at(currentFrameIndex).commandBuffer
    };

    // 2. Modern SubmitInfo2
    const vk::SubmitInfo2 submitInfo{
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &imageAcquireWaitInfo,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cmdSubmitInfo,
        .signalSemaphoreInfoCount = static_cast<uint32_t>(semaphoreSignals.size()),
        .pSignalSemaphoreInfos = semaphoreSignals.data()
    };

    // 3. Modern Queue Submit
    graphicsQueue_.submit2(submitInfo, nullptr);

    const vk::PresentInfoKHR presentInfoKHR{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &*renderCompleteSemaphores_[imageIndex],
        .swapchainCount = 1,
        .pSwapchains = &*swapChain_,
        .pImageIndices = &imageIndex
    };

    const vk::Result presentResult = graphicsQueue_.presentKHR(presentInfoKHR);

    switch (presentResult) {
        case vk::Result::eSuccess:
            break;
        case vk::Result::eSuboptimalKHR:
        case vk::Result::eErrorOutOfDateKHR:
            requireSwapChainRecreate_ = true;
            break;
        default:
            throw vk::SystemError(presentResult, "An unexpected error occurred during presentation");
            break; // an unexpected result is returned!
    }
}

void VkRenderer::ProcessCameraEvent(const InputEvent event) {
    std::visit( Overload{
        [this](MouseMoveEvent e) {
            // yaw_ += static_cast<float>(e.x) / 500.0f;
            // pitch_ -= static_cast<float>(e.y) / 500.0f;

        },
        [](MouseWheelEvent e) {},
        [this, event](const KeyEvent e) {
            if (event.type == InputEvent::Type::kKeyDown) {
                if (e.key == 'W') cameraCoords_.z += 1;
                if (e.key == 'S') cameraCoords_.z -= 1;
                if (e.key == 'A') cameraCoords_.x += 1;
                if (e.key == 'D') cameraCoords_.x -= 1;
                if (e.key == VK_SPACE) cameraCoords_.y -= 1;
                if (e.key == VK_CONTROL) cameraCoords_.y += 1;
            }
            // if (event.type == InputEvent::Type::kKeyUp) {
            //     if (e.key == 'W') cameraCoords_.z = 0;
            //     if (e.key == 'S') cameraCoords_.z = 0;
            //     if (e.key == 'A') cameraCoords_.x = 0;
            //     if (e.key == 'D') cameraCoords_.x = 0;
            //     if (e.key == VK_SPACE) cameraCoords_.y = 0;
            //     if (e.key == VK_CONTROL) cameraCoords_.y = 0;
            // }
        }
    }, event.data);
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

vk::Bool32 VkRenderer::DebugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity, const vk::DebugUtilsMessageTypeFlagsEXT type, const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
    std::cerr << "Validation Error of type " << to_string(type) << ". msg: " << pCallbackData->pMessage << std::endl;

    return vk::False;
}

void VkRenderer::TransitionImageLayout(
    const vk::Image image,
    const vk::ImageLayout oldLayout,
    const vk::ImageLayout newLayout,
    const vk::AccessFlags2 srcAccessMask,
    const vk::AccessFlags2 dstAccessMask,
    const vk::PipelineStageFlags2 srcStageMask,
    const vk::PipelineStageFlags2 dstStageMask,
    const vk::ImageAspectFlags imageAspectFlags,
    const uint32_t currentFrameIndex) const {

    vk::ImageMemoryBarrier2 barrier = {
        .srcStageMask = srcStageMask,
        .srcAccessMask = srcAccessMask,
        .dstStageMask = dstStageMask,
        .dstAccessMask = dstAccessMask,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = image,
        .subresourceRange = {
            .aspectMask = imageAspectFlags,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    const vk::DependencyInfo dependencyInfo = {
        .dependencyFlags = {},
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier
    };

    frames_.at(currentFrameIndex).commandBuffer.pipelineBarrier2(dependencyInfo);
}


/*
 * Find a memory type for the buffer that is both supported and works for our application
 * typeFilter specifies suitable memory types by setting their corresponding bit to 1
 * So to find the indices of suitable types, we just iterate over all and check if their bit is set to 1
 * memoryProperties.MemoryTypes contains vk::MemoryType structs that specify the heap and properties of each memory type
 * We check those against the specified properties we need for the buffer
 * So only a type that's suitable (bit set to 1) and fits our required properties is selected
 */
uint32_t VkRenderer::FindMemoryType(const uint32_t typeFilter, const vk::MemoryPropertyFlags properties) const {
    const vk::PhysicalDeviceMemoryProperties memoryProperties{physicalDevice_.getMemoryProperties()};

    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++) {
        if (typeFilter & (1 << i) && (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("No suitable memory type found");
}

void VkRenderer::CopyBuffer(const vk::raii::Buffer& srcBuffer, const vk::raii::Buffer& dstBuffer, const vk::DeviceSize size) const {
    const vk::CommandBufferAllocateInfo allocateInfo{
        .commandPool = ephemeralCommandPool_,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    };
    const vk::raii::CommandBuffer commandCopyBuffer = std::move(device_.allocateCommandBuffers(allocateInfo).front());

    commandCopyBuffer.begin({
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
    });

    commandCopyBuffer.copyBuffer(*srcBuffer, *dstBuffer, vk::BufferCopy(0, 0, size));

    commandCopyBuffer.end();

    graphicsQueue_.submit(
        vk::SubmitInfo{
            .commandBufferCount = 1,
            .pCommandBuffers = &*commandCopyBuffer
        }, nullptr);
    graphicsQueue_.waitIdle();
}
}
