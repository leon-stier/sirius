#include "GpuAssetUploader.h"

#include <stdexcept>

#include "vulkanContext.h"

namespace sirius {

vk::VertexInputBindingDescription GpuVertex::GetBindingDescription() {
    return {
        .binding = 0,
        .stride = sizeof(GpuVertex),
        .inputRate = vk::VertexInputRate::eVertex
    };
}

std::array<vk::VertexInputAttributeDescription, 2> GpuVertex::GetAttributeDescriptions() {
    return {{
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
        }
    }};
}

void GpuAssetUploader::Init(VulkanContext& context) {
    context_ = &context;

    const vk::CommandPoolCreateInfo poolInfo{
        .flags = vk::CommandPoolCreateFlagBits::eTransient,
        .queueFamilyIndex = context.GraphicsQueueIndex()
    };
    commandPool_ = vk::raii::CommandPool(context.Device(), poolInfo);
}

GpuAssetHandle GpuAssetUploader::Upload(const ModelAsset& asset) {
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
        vertices_.push_back({.position = vertex.position, .normal = vertex.normal});
    }
    indices_.insert(indices_.end(), asset.indices.begin(), asset.indices.end());

    GpuAsset gpuAsset;
    gpuAsset.materials = asset.materials;
    if (gpuAsset.materials.empty()) {
        gpuAsset.materials.push_back({.baseColorFactor = glm::vec4(1.0f)});
    }
    gpuAsset.primitives.reserve(asset.primitives.size());
    for (const AssetPrimitive& primitive : asset.primitives) {
        gpuAsset.primitives.push_back({
            .firstIndex = indexOffset + primitive.firstIndex,
            .indexCount = primitive.indexCount,
            .vertexOffset = static_cast<int32_t>(vertexOffset) + primitive.vertexOffset,
            .materialIndex = primitive.materialIndex < gpuAsset.materials.size() ? primitive.materialIndex : 0
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

std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> GpuAssetUploader::CreateBuffer(
    const vk::DeviceSize size,
    const vk::BufferUsageFlags usage,
    const vk::MemoryPropertyFlags properties
) const {
    const vk::BufferCreateInfo bufferInfo{
        .size = size,
        .usage = usage,
        .sharingMode = vk::SharingMode::eExclusive
    };
    auto buffer = vk::raii::Buffer(context_->Device(), bufferInfo);
    const vk::MemoryRequirements requirements = buffer.getMemoryRequirements();
    const vk::MemoryAllocateInfo allocateInfo{
        .allocationSize = requirements.size,
        .memoryTypeIndex = FindMemoryType(requirements.memoryTypeBits, properties)
    };
    auto memory = vk::raii::DeviceMemory(context_->Device(), allocateInfo);
    buffer.bindMemory(memory, 0);
    return {std::move(buffer), std::move(memory)};
}

uint32_t GpuAssetUploader::FindMemoryType(
    const uint32_t typeFilter,
    const vk::MemoryPropertyFlags properties
) const {
    const vk::PhysicalDeviceMemoryProperties2 memoryProperties = context_->PhysicalDevice().getMemoryProperties2();
    for (uint32_t i = 0; i < memoryProperties.memoryProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) != 0 &&
            (memoryProperties.memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("No suitable memory type found for GPU asset");
}

vk::raii::CommandBuffer GpuAssetUploader::BeginSingleTimeCommands() const {
    const vk::CommandBufferAllocateInfo allocateInfo{
        .commandPool = commandPool_,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1
    };
    auto commandBuffer = std::move(context_->Device().allocateCommandBuffers(allocateInfo).front());
    commandBuffer.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    return commandBuffer;
}

void GpuAssetUploader::EndSingleTimeCommands(vk::raii::CommandBuffer&& commandBuffer) const {
    commandBuffer.end();
    const vk::CommandBufferSubmitInfo commandInfo{.commandBuffer = *commandBuffer};
    const vk::SubmitInfo2 submitInfo{
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandInfo
    };
    context_->GraphicsQueue().submit2(submitInfo, nullptr);
    context_->GraphicsQueue().waitIdle();
}

void GpuAssetUploader::RebuildBuffers() {
    context_->Device().waitIdle();

    auto [newVertexBuffer, newVertexMemory] = CreateBuffer(
        sizeof(GpuVertex) * vertices_.size(),
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
        vk::MemoryPropertyFlagBits::eDeviceLocal
    );
    auto [vertexStaging, vertexStagingMemory] = CreateBuffer(
        sizeof(GpuVertex) * vertices_.size(),
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
    );
    void* vertexData = vertexStagingMemory.mapMemory(0, sizeof(GpuVertex) * vertices_.size());
    std::memcpy(vertexData, vertices_.data(), sizeof(GpuVertex) * vertices_.size());
    vertexStagingMemory.unmapMemory();

    auto [newIndexBuffer, newIndexMemory] = CreateBuffer(
        sizeof(uint32_t) * indices_.size(),
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
        vk::MemoryPropertyFlagBits::eDeviceLocal
    );
    auto [indexStaging, indexStagingMemory] = CreateBuffer(
        sizeof(uint32_t) * indices_.size(),
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
    );
    void* indexData = indexStagingMemory.mapMemory(0, sizeof(uint32_t) * indices_.size());
    std::memcpy(indexData, indices_.data(), sizeof(uint32_t) * indices_.size());
    indexStagingMemory.unmapMemory();

    auto commandBuffer = BeginSingleTimeCommands();
    commandBuffer.copyBuffer(
        *vertexStaging,
        *newVertexBuffer,
        vk::BufferCopy{.size = sizeof(GpuVertex) * vertices_.size()}
    );
    commandBuffer.copyBuffer(
        *indexStaging,
        *newIndexBuffer,
        vk::BufferCopy{.size = sizeof(uint32_t) * indices_.size()}
    );
    EndSingleTimeCommands(std::move(commandBuffer));

    vertexBuffer_ = std::move(newVertexBuffer);
    vertexMemory_ = std::move(newVertexMemory);
    indexBuffer_ = std::move(newIndexBuffer);
    indexMemory_ = std::move(newIndexMemory);
}

}
