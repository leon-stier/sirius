#pragma once

#include <array>
#include <limits>
#include <utility>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <vulkan/vulkan_raii.hpp>

#include "AssetManager.h"

class VulkanContext;

namespace sirius {

struct GpuVertex {
    glm::vec3 position;
    glm::vec3 normal;

    static vk::VertexInputBindingDescription GetBindingDescription();
    static std::array<vk::VertexInputAttributeDescription, 2> GetAttributeDescriptions();
};

struct GpuPrimitive {
    uint32_t firstIndex{0};
    uint32_t indexCount{0};
    int32_t vertexOffset{0};
    uint32_t materialIndex{0};
};

struct GpuAsset {
    std::vector<GpuPrimitive> primitives;
    std::vector<AssetMaterial> materials;
};

struct GpuAssetHandle {
    static constexpr uint32_t InvalidValue = (std::numeric_limits<uint32_t>::max)();

    uint32_t value{InvalidValue};

    bool IsValid() const noexcept { return value != InvalidValue; }
    explicit operator bool() const noexcept { return IsValid(); }
    friend bool operator==(GpuAssetHandle, GpuAssetHandle) = default;
};

class GpuAssetUploader {
public:
    void Init(VulkanContext& context);

    // Uploads an asset into the shared device-local vertex and index buffers.
    GpuAssetHandle Upload(const ModelAsset& asset);

    const GpuAsset& Get(GpuAssetHandle handle) const;
    vk::Buffer VertexBuffer() const noexcept { return *vertexBuffer_; }
    vk::Buffer IndexBuffer() const noexcept { return *indexBuffer_; }
    bool HasBuffers() const noexcept { return vertexBuffer_ != nullptr && indexBuffer_ != nullptr; }

private:
    std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> CreateBuffer(
        vk::DeviceSize size,
        vk::BufferUsageFlags usage,
        vk::MemoryPropertyFlags properties
    ) const;
    uint32_t FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const;
    vk::raii::CommandBuffer BeginSingleTimeCommands() const;
    void EndSingleTimeCommands(vk::raii::CommandBuffer&& commandBuffer) const;
    void RebuildBuffers();

    VulkanContext* context_{nullptr};
    vk::raii::CommandPool commandPool_{nullptr};
    vk::raii::Buffer vertexBuffer_{nullptr};
    vk::raii::DeviceMemory vertexMemory_{nullptr};
    vk::raii::Buffer indexBuffer_{nullptr};
    vk::raii::DeviceMemory indexMemory_{nullptr};

    std::vector<GpuVertex> vertices_;
    std::vector<uint32_t> indices_;
    std::vector<GpuAsset> assets_;
};

}
