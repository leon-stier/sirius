#include "gpuAssetUploader.h"

#include <stdexcept>

#include "vulkanContext.h"
#include "vulkanUtils.h"

namespace sirius {
vk::VertexInputBindingDescription GpuVertex::GetBindingDescription() {
    return {
        .binding = 0,
        .stride = sizeof(GpuVertex),
        .inputRate = vk::VertexInputRate::eVertex
    };
}

std::array<vk::VertexInputAttributeDescription, 3> GpuVertex::GetAttributeDescriptions() {
    return {
        {
            {
                .location = 0,
                .binding = 0,
                .format = vk::Format::eR32G32B32Sfloat,
                .offset = offsetof(GpuVertex, position)
            },
            {
                .location = 1,
                .binding = 0,
                .format = vk::Format::eR32G32B32Sfloat,
                .offset = offsetof(GpuVertex, normal)
            },
            {
                .location = 2,
                .binding = 0,
                .format = vk::Format::eR32G32Sfloat,
                .offset = offsetof(GpuVertex, uv)
            }
        }
    };
}

void GpuAssetUploader::Init(VulkanContext& context) {
    context_ = &context;

    const vk::CommandPoolCreateInfo poolInfo{
        .flags = vk::CommandPoolCreateFlagBits::eTransient,
        .queueFamilyIndex = context.GraphicsQueueIndex()
    };
    commandPool_ = vk::raii::CommandPool(context.Device(), poolInfo);

    const auto properties = context.PhysicalDevice().getProperties2().properties;
    const float maxAnisotropy =
            properties.limits.maxSamplerAnisotropy < 16.0f
                ? properties.limits.maxSamplerAnisotropy
                : 16.0f;
    const vk::SamplerCreateInfo samplerCreateInfo{
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eLinear,
        .addressModeU = vk::SamplerAddressMode::eRepeat,
        .addressModeV = vk::SamplerAddressMode::eRepeat,
        .addressModeW = vk::SamplerAddressMode::eRepeat,
        .anisotropyEnable = maxAnisotropy > 1.0f ? vk::True : vk::False,
        .maxAnisotropy = maxAnisotropy,
        .minLod = 0.0f,
        .maxLod = 0.0f
    };

    textureSampler_ = vk::raii::Sampler(context.Device(), samplerCreateInfo);

    const AssetTexture defaultTexture{
        .width = 1,
        .height = 1,
        .pixels = {255, 0, 0, 255}
    };

    textures_.push_back({});
    UploadTextureData(defaultTexture, textures_.front());
}

GpuAssetHandle GpuAssetUploader::Upload(const ModelAsset& asset, const AssetManager& assetManager) {
    if (context_ == nullptr) {
        throw std::runtime_error("GPU asset uploader has not been initialized");
    }
    if (asset.vertices.empty() || asset.indices.empty() || asset.primitives.empty()) {
        throw std::runtime_error("Cannot upload an empty model asset");
    }

    const uint32_t vertexOffset = static_cast<uint32_t>(vertices_.size());
    const uint32_t indexOffset = static_cast<uint32_t>(indices_.size());

    vertices_.reserve(vertices_.size() + asset.vertices.size());
    for (const AssetVertex& vertex : asset.vertices) {
        vertices_.push_back({.position = vertex.position, .normal = vertex.normal, .uv = vertex.uv});
    }
    indices_.insert(indices_.end(), asset.indices.begin(), asset.indices.end());

    GpuAsset gpuAsset;
    gpuAsset.materials = asset.materials;
    if (gpuAsset.materials.empty()) {
        gpuAsset.materials.push_back({.baseColorFactor = glm::vec4(1.0f)});
    }

    gpuAsset.baseColorTextureIndices.resize(gpuAsset.materials.size(), 0);

    for (size_t materialIndex = 0; materialIndex < gpuAsset.materials.size(); ++materialIndex) {
        const AssetMaterial& material = gpuAsset.materials.at(materialIndex);

        if (!material.baseColorTexture.has_value()) {
            continue;
        }

        gpuAsset.baseColorTextureIndices.at(materialIndex) = UploadTexture(material.baseColorTexture.value(), assetManager);
    }


    gpuAsset.primitives.reserve(asset.primitives.size());
    for (const auto& [firstIndex, indexCount, primitiveVertexOffset, materialIndex] : asset.primitives) {
        gpuAsset.primitives.push_back({
            .firstIndex = indexOffset + firstIndex,
            .indexCount = indexCount,
            .vertexOffset = static_cast<int32_t>(vertexOffset) + primitiveVertexOffset,
            .materialIndex = materialIndex < gpuAsset.materials.size() ? materialIndex : 0
        });
    }

    assets_.push_back(std::move(gpuAsset));
    RebuildBuffers();
    return {.value = static_cast<uint32_t>(assets_.size() - 1)};
}

const GpuAsset& GpuAssetUploader::Get(const GpuAssetHandle handle) const {
    if (!handle.IsValid() || handle.value >= assets_.size()) {
        throw std::runtime_error("GPU asset handle is invalid");
    }
    return assets_.at(handle.value);
}

void GpuAssetUploader::UploadTextureData(const AssetTexture& texture, GpuTexture& destination) {
    if (texture.width == 0 || texture.height == 0) {
        throw std::runtime_error("Cannot upload a texture with zero dimensions");
    }

    const vk::DeviceSize imageSize = static_cast<vk::DeviceSize>(texture.width) * texture.height * 4;
    if (texture.pixels.size() != imageSize) {
        throw std::runtime_error("Texture pixel data is not RGBA8");
    }

    auto [image, memory] = CreateTextureImage(texture.width, texture.height);
    auto [stagingBuffer, stagingMemory] = VulkanUtils::CreateBuffer(*context_, imageSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void* mapped = stagingMemory.mapMemory(0, imageSize);
    std::memcpy(mapped, texture.pixels.data(), imageSize);
    stagingMemory.unmapMemory();

    auto commandBuffer = VulkanUtils::BeginSingleTimeCommands(*context_, commandPool_);
    VulkanUtils::TransitionImageLayout(
        *image,
        commandBuffer,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eTransferDstOptimal,
        {},
        vk::AccessFlagBits2::eTransferWrite,
        vk::PipelineStageFlagBits2::eTopOfPipe,
        vk::PipelineStageFlagBits2::eTransfer,
        vk::ImageAspectFlagBits::eColor
    );

    commandBuffer.copyBufferToImage(
        *stagingBuffer,
        *image,
        vk::ImageLayout::eTransferDstOptimal,
        vk::BufferImageCopy{
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1
            },
            .imageOffset = {0, 0, 0},
            .imageExtent = {texture.width, texture.height, 1}
        }
    );

    VulkanUtils::TransitionImageLayout(
        *image,
        commandBuffer,
        vk::ImageLayout::eTransferDstOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::AccessFlagBits2::eTransferWrite,
        vk::AccessFlagBits2::eShaderSampledRead,
        vk::PipelineStageFlagBits2::eTransfer,
        vk::PipelineStageFlagBits2::eFragmentShader,
        vk::ImageAspectFlagBits::eColor
    );
    VulkanUtils::EndSingleTimeCommands(*context_, std::move(commandBuffer));

    destination.image = std::move(image);
    destination.memory = std::move(memory);
    destination.view = VulkanUtils::CreateImageView(*context_, *destination.image, vk::Format::eR8G8B8A8Srgb, vk::ImageAspectFlagBits::eColor);
}

std::pair<vk::raii::Image, vk::raii::DeviceMemory> GpuAssetUploader::CreateTextureImage(
    const uint32_t width,
    const uint32_t height
) const {
    return VulkanUtils::CreateImage(
        *context_,
        width,
        height,
        vk::Format::eR8G8B8A8Srgb,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
        vk::MemoryPropertyFlagBits::eDeviceLocal
    );
}

uint32_t GpuAssetUploader::UploadTexture(
    const TextureHandle handle,
    const AssetManager& assetManager
) {
    if (const auto it = uploadedTextures_.find(handle.value); it != uploadedTextures_.end()) {
        return it->second;
    }

    GpuTexture texture;
    UploadTextureData(assetManager.Get(handle), texture);
    textures_.push_back(std::move(texture));
    const uint32_t index = static_cast<uint32_t>(textures_.size() - 1);
    uploadedTextures_.emplace(handle.value, index);
    return index;
}


void GpuAssetUploader::RebuildBuffers() {
    context_->Device().waitIdle();

    auto [newVertexBuffer, newVertexMemory] = VulkanUtils::CreateBuffer(*context_, sizeof(GpuVertex) * vertices_.size(), vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);
    auto [vertexStaging, vertexStagingMemory] = VulkanUtils::CreateBuffer(*context_, sizeof(GpuVertex) * vertices_.size(), vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    void* vertexData = vertexStagingMemory.mapMemory(0, sizeof(GpuVertex) * vertices_.size());
    std::memcpy(vertexData, vertices_.data(), sizeof(GpuVertex) * vertices_.size());
    vertexStagingMemory.unmapMemory();

    auto [newIndexBuffer, newIndexMemory] = VulkanUtils::CreateBuffer(*context_, sizeof(uint32_t) * indices_.size(), vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);
    auto [indexStaging, indexStagingMemory] = VulkanUtils::CreateBuffer(*context_, sizeof(uint32_t) * indices_.size(), vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    void* indexData = indexStagingMemory.mapMemory(0, sizeof(uint32_t) * indices_.size());
    std::memcpy(indexData, indices_.data(), sizeof(uint32_t) * indices_.size());
    indexStagingMemory.unmapMemory();

    auto commandBuffer = VulkanUtils::BeginSingleTimeCommands(*context_, commandPool_);
    commandBuffer.copyBuffer(*vertexStaging, *newVertexBuffer, vk::BufferCopy{.size = sizeof(GpuVertex) * vertices_.size()});
    commandBuffer.copyBuffer(*indexStaging, *newIndexBuffer, vk::BufferCopy{.size = sizeof(uint32_t) * indices_.size()});
    VulkanUtils::EndSingleTimeCommands(*context_, std::move(commandBuffer));

    vertexBuffer_ = std::move(newVertexBuffer);
    vertexMemory_ = std::move(newVertexMemory);
    indexBuffer_ = std::move(newIndexBuffer);
    indexMemory_ = std::move(newIndexMemory);
}
}
