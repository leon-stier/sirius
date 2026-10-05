#include "vkRenderer.h"
#include "window/wndProc.h"

#include <ranges>
#include <glm/gtc/matrix_transform.hpp>

#include <ktx.h>

namespace sirius {
void VkRenderer::Init(VulkanContext& context) {
    context_ = &context;

    CreateSwapChain();
    CreateImageViews();

    CreateDescriptorSetLayout();
    CreateGraphicsPipeline();

    InitCommandBuffers();
    CreateDepthResources();

    // CreateTextureSampler();
    LoadModel();
    CreateVertexBuffer();
    CreateIndexBuffer();

    CreateUniformBuffers();
    CreateDescriptorPool();
    CreateDescriptorSets();
    CreateSyncObjects();

    defaultCamera_.Init();
    defaultCamera_.velocity_ = glm::vec3(0.0f);
    defaultCamera_.position_ = glm::vec3(0.0f, 0.0f, 20.0f);
    defaultCamera_.pitch_ = 0.0f;
    defaultCamera_.yaw_ = 0.0f;

    // InputManager::Subscribe([this](const InputEvent& e) { ProcessCameraEvent(e); });
}

void VkRenderer::Draw() {
    if (pauseRendering) {
        requireSwapChainRecreate_ = true;
        WaitMessage();
        return;
    }
    try {
        defaultCamera_.Update();
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

void VkRenderer::CreateSwapChain() {
    vk::PhysicalDeviceSurfaceInfo2KHR surfaceInfo{.surface = context_->Surface()};
    const auto surfaceCapabilities = context_->PhysicalDevice().getSurfaceCapabilities2KHR(surfaceInfo).surfaceCapabilities;
    swapChainExtent_ = ChooseSwapExtent(surfaceCapabilities);
    const uint32_t minImageCount = ChooseSwapMinImageCount(surfaceCapabilities);

    const std::vector availableFormats = context_->PhysicalDevice().getSurfaceFormats2KHR(surfaceInfo);
    swapChainSurfaceFormat_ = ChooseSwapSurfaceFormat(availableFormats);

    const std::vector availablePresentModes = context_->PhysicalDevice().getSurfacePresentModesKHR(*context_->Surface());
    const vk::PresentModeKHR presentMode = ChooseSwapPresentMode(availablePresentModes);

    const vk::SwapchainCreateInfoKHR swapChainCreateInfo{
        .surface = *context_->Surface(),
        .minImageCount = minImageCount,
        .imageFormat = swapChainSurfaceFormat_.surfaceFormat.format,
        .imageColorSpace = swapChainSurfaceFormat_.surfaceFormat.colorSpace,
        .imageExtent = swapChainExtent_,
        .imageArrayLayers = 1,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode = vk::SharingMode::eExclusive,
        .preTransform = surfaceCapabilities.currentTransform,
        .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode = presentMode,
        .clipped = true
    };

    swapChain_ = vk::raii::SwapchainKHR(context_->Device(), swapChainCreateInfo);
    swapChainImages_ = swapChain_.getImages();
}

void VkRenderer::RecreateSwapChain() {
    context_->Device().waitIdle();
    swapChainImageViews_.clear();
    swapChain_ = nullptr;
    CreateSwapChain();
    CreateImageViews();
    CreateDepthResources();
    requireSwapChainRecreate_ = false;
    std::cout << "Recreated SwapChain" << std::endl;
}

vk::SurfaceFormat2KHR VkRenderer::ChooseSwapSurfaceFormat(std::vector<vk::SurfaceFormat2KHR> const& availableFormats) {
    const auto formatIt = std::ranges::find_if(availableFormats, [](const auto& format) {
        return format.surfaceFormat.format == vk::Format::eB8G8R8A8Srgb && format.surfaceFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
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
        swapChainImageViews_.emplace_back(CreateImageView(image, swapChainSurfaceFormat_.surfaceFormat.format, vk::ImageAspectFlagBits::eColor));
    }
}

vk::raii::ImageView VkRenderer::CreateImageView(vk::Image const& image, const vk::Format format, const vk::ImageAspectFlags aspectFlags) const {
    const vk::ImageViewCreateInfo viewCreateInfo{
        .image = image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange = {
            .aspectMask = aspectFlags,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    return vk::raii::ImageView(context_->Device(), viewCreateInfo);
}

void VkRenderer::CreateDescriptorSetLayout() {
    constexpr vk::DescriptorSetLayoutBinding cameraBinding{
        .binding = 0,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eVertex
    };

    vk::DescriptorSetLayoutCreateInfo layoutCreateInfo{
        .bindingCount = 1,
        .pBindings = &cameraBinding
    };

    descriptorSetLayout_ = vk::raii::DescriptorSetLayout(context_->Device(), layoutCreateInfo);
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
    auto bindingDescription = GpuVertex::GetBindingDescription();
    auto attributeDescriptions = GpuVertex::GetAttributeDescriptions();
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
        .depthCompareOp = vk::CompareOp::eGreater,
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

    constexpr vk::PushConstantRange drawConstantsRange{
        .stageFlags = vk::ShaderStageFlagBits::eFragment,
        .offset = 0,
        .size = sizeof(DrawConstants)
    };

    vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo{
        .setLayoutCount = 1,
        .pSetLayouts = &*descriptorSetLayout_,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &drawConstantsRange
    };
    pipelineLayout_ = vk::raii::PipelineLayout(context_->Device(), pipelineLayoutCreateInfo);

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
            .pColorAttachmentFormats = &swapChainSurfaceFormat_.surfaceFormat.format,
            .depthAttachmentFormat = depthFormat
        }
    };

    graphicsPipeline_ = vk::raii::Pipeline(context_->Device(), nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
}

void VkRenderer::InitCommandBuffers() {
    for (FrameContext& frame : frames_) {
        const vk::CommandPoolCreateInfo poolCreateInfo{
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = context_->GraphicsQueueIndex()
        };
        frame.commandPool = vk::raii::CommandPool(context_->Device(), poolCreateInfo);

        const vk::CommandBufferAllocateInfo allocInfo{
            .commandPool = frame.commandPool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = 1
        };

        frame.commandBuffer = std::move(vk::raii::CommandBuffers(context_->Device(), allocInfo).front());
    }

    const vk::CommandPoolCreateInfo poolCreateInfo{
        .flags = vk::CommandPoolCreateFlagBits::eTransient,
        .queueFamilyIndex = context_->GraphicsQueueIndex()
    };

    ephemeralCommandPool_ = vk::raii::CommandPool(context_->Device(), poolCreateInfo);
}

std::pair<vk::raii::Image, vk::raii::DeviceMemory> VkRenderer::CreateImage(const uint32_t width, const uint32_t height, const vk::Format format, const vk::ImageTiling tiling, const vk::ImageUsageFlags usage, const vk::MemoryPropertyFlags properties) const {
    const vk::ImageCreateInfo imageInfo{
        .imageType = vk::ImageType::e2D,
        .format = format,
        .extent = {.width = width, .height = height, .depth = 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = tiling,
        .usage = usage,
        .sharingMode = vk::SharingMode::eExclusive
    };

    auto image = vk::raii::Image(context_->Device(), imageInfo);

    const vk::MemoryRequirements memRequirements = image.getMemoryRequirements();
    const vk::MemoryAllocateInfo allocInfo{
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties)
    };
    auto imageMemory = vk::raii::DeviceMemory(context_->Device(), allocInfo);
    image.bindMemory(imageMemory, 0);

    return {std::move(image), std::move(imageMemory)};
}

vk::Format VkRenderer::FindSupportedFormat(const std::vector<vk::Format>& candidates, const vk::ImageTiling tiling, const vk::FormatFeatureFlags features) const {
    for (const auto format : candidates) {
        vk::FormatProperties2 props = context_->PhysicalDevice().getFormatProperties2(format);

        if ((
                tiling == vk::ImageTiling::eLinear && (props.formatProperties.linearTilingFeatures & features) == features) ||
            (tiling == vk::ImageTiling::eOptimal && (props.formatProperties.optimalTilingFeatures & features) == features)) {
            return format;
        }
    }

    throw std::runtime_error("Failed to find supported Format!");
}

vk::Format VkRenderer::FindDepthFormat() const {
    return FindSupportedFormat({vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint}, vk::ImageTiling::eOptimal, vk::FormatFeatureFlagBits::eDepthStencilAttachment);
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
    timelineSemaphore_ = vk::raii::Semaphore(context_->Device(), timelineSemaphoreCreateInfo);

    for (FrameContext& frame : frames_) {
        frame.imageAcquiredSemaphore = vk::raii::Semaphore(context_->Device(), vk::SemaphoreCreateInfo());
    }

    for (size_t i = 0; i < swapChainImages_.size(); i++) {
        renderCompleteSemaphores_.emplace_back(context_->Device(), vk::SemaphoreCreateInfo());
    }
}

void VkRenderer::CreateTextureImage(const uint8_t* pixelData, uint32_t texWidth, uint32_t texHeight, vk::Format textureFormat) {
    vk::DeviceSize imageSize = texWidth * texHeight * 4;

    auto [stagingBuffer, stagingBufferMemory] = CreateBuffer(
            imageSize,
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );

    void* data = stagingBufferMemory.mapMemory(0, imageSize);
    memcpy(data, pixelData, imageSize);
    stagingBufferMemory.unmapMemory();
    std::tie(textureImage_, textureImageMemory_) = CreateImage(
            texWidth,
            texHeight,
            textureFormat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );

    auto commandBuffer = BeginSingleTimeCommands();

    TransitionImageLayout(
        textureImage_, commandBuffer,
        vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
        {}, vk::AccessFlagBits2::eTransferWrite,
        vk::PipelineStageFlagBits2::eTopOfPipe, vk::PipelineStageFlagBits2::eTransfer,
        vk::ImageAspectFlagBits::eColor
    );

    CopyBufferToImage(commandBuffer, stagingBuffer, textureImage_, texWidth, texHeight);

    TransitionImageLayout(
        textureImage_, commandBuffer,
        vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::AccessFlagBits2::eTransferWrite, vk::AccessFlagBits2::eShaderRead,
        vk::PipelineStageFlagBits2::eTransfer, vk::PipelineStageFlagBits2::eFragmentShader,
        vk::ImageAspectFlagBits::eColor
    );
    EndSingleTimeCommands(std::move(commandBuffer));

    textureImageView_ = CreateImageView(*textureImage_, vk::Format::eR8G8B8A8Srgb, vk::ImageAspectFlagBits::eColor);

    if  (textureImageView_ == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to create texture image view!");
    }
}

// Unused
void VkRenderer::KtxTextureLoader() {
    // Load KTX2 texture instead of using stb_image
    ktxTexture* texture;
    KTX_error_code result = ktxTexture_CreateFromNamedFile("../../resources/CesiumLogoFlat.ktx2", KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture);

    if (result != KTX_SUCCESS) {
        throw std::runtime_error("failed to load ktx texture image!");
    }

    // Get texture dimensions and data
    uint32_t texWidth = texture->baseWidth;
    uint32_t texHeight = texture->baseHeight;
    ktx_size_t imageSize = ktxTexture_GetImageSize(texture, 0);
    ktx_uint8_t* ktxTextureData = ktxTexture_GetData(texture);

    // Create staging buffer
    auto [stagingBuffer, stagingBufferMemory] = CreateBuffer(imageSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    // Copy texture data to staging buffer
    void* data = stagingBufferMemory.mapMemory(0, imageSize);
    memcpy(data, ktxTextureData, imageSize);
    stagingBufferMemory.unmapMemory();

    // Determine the Vulkan format from KTX format
    vk::Format textureFormat = vk::Format::eR8G8B8A8Srgb; // Default format, should be determined from KTX metadata

    // Create the texture image
    std::tie(textureImage_, textureImageMemory_) = CreateImage(texWidth, texHeight, textureFormat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);

    // Copy data from staging buffer to texture image
    auto commandBuffer = BeginSingleTimeCommands();
    TransitionImageLayout(textureImage_, commandBuffer, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, {}, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eTopOfPipe, vk::PipelineStageFlagBits2::eTransfer, vk::ImageAspectFlagBits::eColor);
    CopyBufferToImage(commandBuffer, stagingBuffer, textureImage_, texWidth, texHeight);
    TransitionImageLayout(textureImage_, commandBuffer, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits2::eTransferWrite, vk::AccessFlagBits2::eShaderRead, vk::PipelineStageFlagBits2::eTransfer, vk::PipelineStageFlagBits2::eFragmentShader, vk::ImageAspectFlagBits::eColor);

    // Cleanup KTX resources
    ktxTexture_Destroy(texture);
}

void VkRenderer::CreateTextureSampler() {
    vk::PhysicalDeviceProperties2 properties = context_->PhysicalDevice().getProperties2();
    vk::SamplerCreateInfo samplerCreateInfo{
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eLinear,
        .addressModeU = vk::SamplerAddressMode::eRepeat,
        .addressModeV = vk::SamplerAddressMode::eRepeat,
        .addressModeW = vk::SamplerAddressMode::eRepeat,
        .mipLodBias = 0.0f,
        .anisotropyEnable = vk::True,
        .maxAnisotropy = properties.properties.limits.maxSamplerAnisotropy,
        .compareEnable = vk::False,
        .compareOp = vk::CompareOp::eAlways
    };

    textureSampler_ = vk::raii::Sampler(context_->Device(), samplerCreateInfo);
}

void VkRenderer::CreateVertexBuffer() {
    std::vector<GpuVertex> vertices;
    vertices.reserve(model_->vertices.size());

    for (const AssetVertex& source : model_->vertices) {
        vertices.emplace_back(source.position, source.normal);
    }

    const vk::DeviceSize bufferSize = sizeof(GpuVertex) * vertices.size();

    auto [stagingBuffer, stagingBufferMemory] = CreateBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void* dataStaging = stagingBufferMemory.mapMemory(0, bufferSize);
    memcpy(dataStaging, vertices.data(), bufferSize);
    stagingBufferMemory.unmapMemory();

    std::tie(vertexBuffer_, vertexBufferMemory_) = CreateBuffer(bufferSize, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);

    CopyBuffer(stagingBuffer, vertexBuffer_, bufferSize);
}

void VkRenderer::CreateIndexBuffer() {
    const vk::DeviceSize bufferSize = sizeof(uint32_t) * model_->indices.size();

    auto [stagingBuffer, stagingBufferMemory] = CreateBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void* dataStaging = stagingBufferMemory.mapMemory(0, bufferSize);
    memcpy(dataStaging, model_->indices.data(), bufferSize);
    stagingBufferMemory.unmapMemory();

    std::tie(indexBuffer_, indexBufferMemory_) = CreateBuffer(bufferSize, vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);

    CopyBuffer(stagingBuffer, indexBuffer_, bufferSize);
}

void VkRenderer::CreateUniformBuffers() {
    for (auto& frame : frames_) {
        constexpr vk::DeviceSize bufferSize = sizeof(FrameUniforms);
        auto [buffer, bufferMem] = CreateBuffer(bufferSize, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        frame.uniformBuffer = std::move(buffer);
        frame.uniformBufferMemory = std::move(bufferMem);
        frame.uniformBufferMapped = frame.uniformBufferMemory.mapMemory(0, bufferSize);
    }
}

void VkRenderer::CreateDescriptorPool() {
    constexpr vk::DescriptorPoolSize poolSize{
        .type = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = kMaxFramesInFlight
    };
    const vk::DescriptorPoolCreateInfo poolCreateInfo{
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = kMaxFramesInFlight,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize
    };
    descriptorPool_ = vk::raii::DescriptorPool(context_->Device(), poolCreateInfo);
}

void VkRenderer::CreateDescriptorSets() {
    std::vector layouts(kMaxFramesInFlight, *descriptorSetLayout_);
    vk::DescriptorSetAllocateInfo allocInfo{
        .descriptorPool = descriptorPool_,
        .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
        .pSetLayouts = layouts.data()
    };

    for (auto&& [frame, set] : std::views::zip(frames_, context_->Device().allocateDescriptorSets(allocInfo))) {
        frame.descriptorSet = std::move(set);

        const vk::DescriptorBufferInfo bufferInfo{
            .buffer = frame.uniformBuffer,
            .offset = 0,
            .range = sizeof(FrameUniforms)
        };

        const vk::WriteDescriptorSet write{
            .dstSet = frame.descriptorSet,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .pBufferInfo = &bufferInfo
        };

        context_->Device().updateDescriptorSets(write,{});
        context_->Device().updateDescriptorSets(write, {});
    }
}

std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> VkRenderer::CreateBuffer(const vk::DeviceSize size, const vk::BufferUsageFlags bufferUsage, const vk::MemoryPropertyFlags memoryProperties) const {
    const vk::BufferCreateInfo bufferInfo{
        .size = size,
        .usage = bufferUsage,
        .sharingMode = vk::SharingMode::eExclusive
    };
    auto buffer = vk::raii::Buffer(context_->Device(), bufferInfo);

    const vk::MemoryRequirements memRequirements = buffer.getMemoryRequirements();
    const vk::MemoryAllocateInfo memoryAllocateInfo{
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, memoryProperties)
    };
    auto bufferMemory{vk::raii::DeviceMemory(context_->Device(), memoryAllocateInfo)};
    buffer.bindMemory(bufferMemory, 0);

    return {std::move(buffer), std::move(bufferMemory)};
}

void VkRenderer::LoadModel() {
    modelHandle_ = assetManager_.LoadModel("../../resources/tree.glb");
    model_ = &assetManager_.Get(modelHandle_);

    if (model_->vertices.empty()) {
        throw std::runtime_error("Loaded model contains no vertices");
    }

    if (model_->indices.empty()) {
        throw std::runtime_error("Loaded model contains no indices");
    }

    if (model_->primitives.empty()) {
        throw std::runtime_error("Loaded model contains no primitives");
    }
}

void VkRenderer::RecordCommandBuffer(const uint32_t imageIndex, const uint32_t currentFrameIndex) const {
    auto& buffer = frames_.at(currentFrameIndex).commandBuffer;
    buffer.begin({});

    // Transition the image layout for rendering
    TransitionImageLayout(
        swapChainImages_[imageIndex],
        frames_.at(currentFrameIndex).commandBuffer,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eColorAttachmentOptimal,
        {},
        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::ImageAspectFlagBits::eColor
    );

    // Transition the depth image to depth attachment optimal layout
    TransitionImageLayout(
        *depthImage_,
        frames_.at(currentFrameIndex).commandBuffer,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eDepthAttachmentOptimal,
        vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
        vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
        vk::ImageAspectFlagBits::eDepth
    );

    // Set up the color attachment
    constexpr vk::ClearValue clearColor = vk::ClearColorValue(1.0f, 1.0f, 1.0f, 1.0f);
    constexpr vk::ClearValue clearDepth = vk::ClearDepthStencilValue(0.0f, 0);

    vk::RenderingAttachmentInfo attachmentInfo = {
        .imageView = swapChainImageViews_.at(imageIndex),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = clearColor
    };

    vk::RenderingAttachmentInfo depthAttachmentInfo{
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
    buffer.bindIndexBuffer(*indexBuffer_, 0, vk::IndexType::eUint32);
    buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout_, 0, *frames_.at(currentFrameIndex).descriptorSet, nullptr);

    for (const AssetPrimitive& primitive : model_->primitives) {
        const AssetMaterial& material = model_->materials.at(primitive.materialIndex);

        const DrawConstants constants{
            .baseColor = material.baseColorFactor
        };

        buffer.pushConstants(*pipelineLayout_, vk::ShaderStageFlagBits::eFragment, 0, sizeof(constants), &constants);

        buffer.drawIndexed(primitive.indexCount, 1, primitive.firstIndex, primitive.vertexOffset, 0);
    }

    // End rendering
    buffer.endRendering();

    // Transition the image layout for presentation
    TransitionImageLayout(
        swapChainImages_[imageIndex],
        frames_.at(currentFrameIndex).commandBuffer,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::ePresentSrcKHR,
        vk::AccessFlagBits2::eColorAttachmentWrite,
        {},
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::PipelineStageFlagBits2::eBottomOfPipe,
        vk::ImageAspectFlagBits::eColor
    );

    buffer.end();
}

void VkRenderer::UpdateUniformBuffer(const uint32_t currentFrameIndex) {
    FrameUniforms uniforms{};

    uniforms.view = defaultCamera_.GetViewMatrix();

    uniforms.projection =
        glm::perspective(
            glm::radians(70.0f),
            static_cast<float>(swapChainExtent_.width) / static_cast<float>(swapChainExtent_.height),
            10000.0f,
            0.1f
        );

    std::memcpy(frames_[currentFrameIndex].uniformBufferMapped, &uniforms,sizeof(uniforms));
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
    const vk::Result waitResult{context_->Device().waitSemaphores(waitInfo, std::numeric_limits<uint64_t>::max())};
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

    UpdateUniformBuffer(currentFrameIndex);

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

    const vk::SubmitInfo2 submitInfo{
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &imageAcquireWaitInfo,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cmdSubmitInfo,
        .signalSemaphoreInfoCount = static_cast<uint32_t>(semaphoreSignals.size()),
        .pSignalSemaphoreInfos = semaphoreSignals.data()
    };

    context_->GraphicsQueue().submit2(submitInfo, nullptr);

    const vk::PresentInfoKHR presentInfoKHR{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &*renderCompleteSemaphores_[imageIndex],
        .swapchainCount = 1,
        .pSwapchains = &*swapChain_,
        .pImageIndices = &imageIndex
    };

    const vk::Result presentResult = context_->GraphicsQueue().presentKHR(presentInfoKHR);

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
    std::visit(Overload{
                   [this](MouseMoveEvent e) {
                       // yaw_ += static_cast<float>(e.x) / 500.0f;
                       // pitch_ -= static_cast<float>(e.y) / 500.0f;
                   },
                   [](MouseWheelEvent e) {
                   },
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


void VkRenderer::TransitionImageLayout(
    const vk::Image image,
    const vk::raii::CommandBuffer& commandBuffer,
    const vk::ImageLayout oldLayout,
    const vk::ImageLayout newLayout,
    const vk::AccessFlags2 srcAccessMask,
    const vk::AccessFlags2 dstAccessMask,
    const vk::PipelineStageFlags2 srcStageMask,
    const vk::PipelineStageFlags2 dstStageMask,
    const vk::ImageAspectFlags imageAspectFlags) const {
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

    commandBuffer.pipelineBarrier2(dependencyInfo);
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
    const vk::PhysicalDeviceMemoryProperties2 memoryProperties{context_->PhysicalDevice().getMemoryProperties2()};

    for (uint32_t i = 0; i < memoryProperties.memoryProperties.memoryTypeCount; i++) {
        if (typeFilter & (1 << i) && (memoryProperties.memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("No suitable memory type found");
}

void VkRenderer::CopyBuffer(const vk::raii::Buffer& srcBuffer, const vk::raii::Buffer& dstBuffer, const vk::DeviceSize size) const {
    vk::raii::CommandBuffer commandCopyBuffer = BeginSingleTimeCommands();
    commandCopyBuffer.copyBuffer(*srcBuffer, *dstBuffer, vk::BufferCopy{.size = size});
    EndSingleTimeCommands(std::move(commandCopyBuffer));
}

void VkRenderer::CopyBufferToImage(const vk::raii::CommandBuffer& commandBuffer, const vk::raii::Buffer& buffer, const vk::raii::Image& image, const uint32_t width, const uint32_t height) {
    const vk::BufferImageCopy region{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1
        },
        .imageOffset = {.x = 0, .y = 0, .z = 0},
        .imageExtent = {.width = width, .height = height, .depth = 1}
    };
    commandBuffer.copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, region);
}

vk::raii::CommandBuffer VkRenderer::BeginSingleTimeCommands() const {
    const vk::CommandBufferAllocateInfo allocateInfo{
        .commandPool = ephemeralCommandPool_,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    };
    vk::raii::CommandBuffer commandBuffer = std::move(context_->Device().allocateCommandBuffers(allocateInfo).front());

    commandBuffer.begin({
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
    });

    return std::move(commandBuffer);
}

void VkRenderer::EndSingleTimeCommands(vk::raii::CommandBuffer&& commandBuffer) const {
    commandBuffer.end();

    const vk::CommandBufferSubmitInfo commandBufferSubmitInfo{
        .commandBuffer = *commandBuffer
    };

    const vk::SubmitInfo2 submitInfo{
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandBufferSubmitInfo
    };

    context_->GraphicsQueue().submit2(submitInfo, nullptr);
    context_->GraphicsQueue().waitIdle();
}

bool VkRenderer::EqualsExt(const std::filesystem::path& p, std::string_view expected_ext) {
    auto ext = p.extension().string();
    return std::ranges::equal(ext, expected_ext, [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) ==
               std::tolower(static_cast<unsigned char>(b));
    });
}
}
