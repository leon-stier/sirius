#define NOMINMAX

#include "vkRenderer.h"
#include "window/wndProc.h"
#include <iostream>
#include <ranges>
#include <glm/gtc/matrix_transform.hpp>

#include "vulkanUtils.h"

namespace sirius {
void VkRenderer::Init(VulkanContext& context) {
    context_ = &context;
    assetUploader_.Init(context);

    CreateSwapChain();
    CreateImageViews();

    CreateDescriptorSetLayout();
    CreateGraphicsPipeline();

    InitCommandBuffers();
    CreateDepthResources();

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

RenderInstanceHandle VkRenderer::LoadModelInstance(const std::filesystem::path& path, const glm::mat4& transform) {
    const AssetHandle cpuHandle = assetManager_.LoadModel(path);
    GpuAssetHandle gpuHandle;
    if (const auto it = uploadedAssets_.find(cpuHandle.value); it != uploadedAssets_.end()) {
        gpuHandle = it->second;
    } else {
        gpuHandle = assetUploader_.Upload(assetManager_.Get(cpuHandle), assetManager_);
        uploadedAssets_.emplace(cpuHandle.value, gpuHandle);
        CreateMaterialDescriptorSets(gpuHandle);
    }
    return renderWorld_.CreateInstance(gpuHandle, transform);
}

void VkRenderer::DestroyInstance(const RenderInstanceHandle handle) {
    renderWorld_.DestroyInstance(handle);
}

void VkRenderer::SetInstanceTransform(const RenderInstanceHandle handle, const glm::mat4& transform) {
    renderWorld_.SetTransform(handle, transform);
    // defaultCamera_.SetLockedMatrix(transform);
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
        swapChainImageViews_.emplace_back(VulkanUtils::CreateImageView(*context_, image, swapChainSurfaceFormat_.surfaceFormat.format, vk::ImageAspectFlagBits::eColor));
    }
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

    constexpr vk::DescriptorSetLayoutBinding textureBinding{
        .binding = 0,
        .descriptorType = vk::DescriptorType::eSampledImage,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };
    constexpr vk::DescriptorSetLayoutBinding samplerBinding{
        .binding = 1,
        .descriptorType = vk::DescriptorType::eSampler,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };
    constexpr std::array materialBindings{textureBinding, samplerBinding};
    const vk::DescriptorSetLayoutCreateInfo materialLayoutInfo{
        .bindingCount = static_cast<uint32_t>(materialBindings.size()),
        .pBindings = materialBindings.data()
    };
    materialDescriptorSetLayout_ = vk::raii::DescriptorSetLayout(context_->Device(), materialLayoutInfo);
}

void VkRenderer::CreateMaterialDescriptorSets(const GpuAssetHandle handle) {
    const GpuAsset& asset = assetUploader_.Get(handle);
    std::vector layouts(asset.materials.size(), *materialDescriptorSetLayout_);
    const vk::DescriptorSetAllocateInfo allocInfo{
        .descriptorPool = descriptorPool_,
        .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
        .pSetLayouts = layouts.data()
    };

    auto descriptorSets = context_->Device().allocateDescriptorSets(allocInfo);
    std::vector<vk::raii::DescriptorSet> materialSets;
    materialSets.reserve(descriptorSets.size());

    for (size_t materialIndex = 0; materialIndex < descriptorSets.size(); ++materialIndex) {
        materialSets.push_back(std::move(descriptorSets.at(materialIndex)));
        const uint32_t textureIndex = asset.baseColorTextureIndices.at(materialIndex);
        const vk::DescriptorImageInfo imageInfo{
            .sampler = nullptr,
            .imageView = assetUploader_.Textures().at(textureIndex).view,
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
        };
        const vk::DescriptorImageInfo samplerInfo{
            .sampler = assetUploader_.TextureSampler(),
            .imageView = nullptr,
            .imageLayout = vk::ImageLayout::eUndefined
        };
        const std::array writes{
            vk::WriteDescriptorSet{
                .dstSet = *materialSets.back(),
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eSampledImage,
                .pImageInfo = &imageInfo
            },
            vk::WriteDescriptorSet{
                .dstSet = *materialSets.back(),
                .dstBinding = 1,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eSampler,
                .pImageInfo = &samplerInfo
            }
        };
        context_->Device().updateDescriptorSets(writes, {});
    }

    materialDescriptorSets_.push_back(std::move(materialSets));
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
        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        .offset = 0,
        .size = sizeof(DrawConstants)
    };

    const std::array setLayouts{*descriptorSetLayout_, *materialDescriptorSetLayout_};
    vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo{
        .setLayoutCount = 2,
        .pSetLayouts = setLayouts.data(),
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

    std::tie(depthImage_, depthImageMemory_) = VulkanUtils::CreateImage(*context_, swapChainExtent_.width, swapChainExtent_.height, depthFormat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::MemoryPropertyFlagBits::eDeviceLocal);
    depthImageView_ = VulkanUtils::CreateImageView(*context_, depthImage_, depthFormat, vk::ImageAspectFlagBits::eDepth);
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

void VkRenderer::CreateUniformBuffers() {
    for (auto& frame : frames_) {
        constexpr vk::DeviceSize bufferSize = sizeof(FrameUniforms);
        auto [buffer, bufferMem] = VulkanUtils::CreateBuffer(*context_, bufferSize, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        frame.uniformBuffer = std::move(buffer);
        frame.uniformBufferMemory = std::move(bufferMem);
        frame.uniformBufferMapped = frame.uniformBufferMemory.mapMemory(0, bufferSize);
    }
}

void VkRenderer::CreateDescriptorPool() {
    constexpr uint32_t maxMaterialDescriptorSets = 4096;
    constexpr std::array poolSizes{
        vk::DescriptorPoolSize{
            .type = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = kMaxFramesInFlight
        },
        vk::DescriptorPoolSize{
            .type = vk::DescriptorType::eSampledImage,
            .descriptorCount = maxMaterialDescriptorSets
        },
        vk::DescriptorPoolSize{
            .type = vk::DescriptorType::eSampler,
            .descriptorCount = maxMaterialDescriptorSets
        }
    };
    const vk::DescriptorPoolCreateInfo poolCreateInfo{
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = kMaxFramesInFlight + maxMaterialDescriptorSets,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data()
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

        context_->Device().updateDescriptorSets(write, {});
    }
}

void VkRenderer::RecordCommandBuffer(const uint32_t imageIndex, const uint32_t currentFrameIndex) const {
    auto& buffer = frames_.at(currentFrameIndex).commandBuffer;
    buffer.begin({});

    // Transition the image layout for rendering
    VulkanUtils::TransitionImageLayout(
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
    VulkanUtils::TransitionImageLayout(
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
    buffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipelineLayout_,
        0,
        *frames_.at(currentFrameIndex).descriptorSet,
        nullptr
    );

    if (assetUploader_.HasBuffers()) {
        buffer.bindVertexBuffers(0, assetUploader_.VertexBuffer(), {0});
        buffer.bindIndexBuffer(assetUploader_.IndexBuffer(), 0, vk::IndexType::eUint32);
    }

    for (const RenderItem& item : renderWorld_.Items()) {
        if (!item.asset.IsValid() || !assetUploader_.HasBuffers()) continue;
        const GpuAsset& asset = assetUploader_.Get(item.asset);
        for (const GpuPrimitive& primitive : asset.primitives) {
            const AssetMaterial& material = asset.materials.at(primitive.materialIndex);
            buffer.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                pipelineLayout_,
                1,
                *materialDescriptorSets_.at(item.asset.value).at(primitive.materialIndex),
                nullptr
            );

            const DrawConstants constants{
                .model = item.transform,
                .baseColor = material.baseColorFactor
            };

            buffer.pushConstants(
                *pipelineLayout_,
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                0,
                sizeof(constants),
                &constants
            );

            buffer.drawIndexed(primitive.indexCount, 1, primitive.firstIndex, primitive.vertexOffset, 0);
        }
    }

    // End rendering
    buffer.endRendering();

    // Transition the image layout for presentation
    VulkanUtils::TransitionImageLayout(
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
    // uniforms.view = glm::inverse(defaultCamera_.GetLockedMatrix());

    uniforms.projection =
            glm::perspective(
                glm::radians(70.0f),
                static_cast<float>(swapChainExtent_.width) / static_cast<float>(swapChainExtent_.height),
                10000.0f,
                0.1f
            );

    std::memcpy(frames_[currentFrameIndex].uniformBufferMapped, &uniforms, sizeof(uniforms));
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
    const vk::Result waitResult{context_->Device().waitSemaphores(waitInfo, (std::numeric_limits<uint64_t>::max)())};
    if (waitResult != vk::Result::eSuccess) {
        // Handle unexpected results (e.g., eTimeout or device loss)
        throw std::runtime_error("Failed or timed out waiting for timeline semaphore!");
    }

    auto [acquireResult, imageIndex] = swapChain_.acquireNextImage((std::numeric_limits<uint64_t>::max)(), *frames_[currentFrameIndex].imageAcquiredSemaphore, nullptr);
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


void VkRenderer::CopyBuffer(const vk::raii::Buffer& srcBuffer, const vk::raii::Buffer& dstBuffer, const vk::DeviceSize size) const {
    vk::raii::CommandBuffer commandCopyBuffer = VulkanUtils::BeginSingleTimeCommands(*context_, ephemeralCommandPool_);
    commandCopyBuffer.copyBuffer(*srcBuffer, *dstBuffer, vk::BufferCopy{.size = size});
    VulkanUtils::EndSingleTimeCommands(*context_, std::move(commandCopyBuffer));
}
}
