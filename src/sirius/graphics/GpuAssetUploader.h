#pragma once

#include <array>
#include <limits>
#include <utility>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <vulkan/vulkan_raii.hpp>

#include "assetManager.h"

class VulkanContext;

namespace sirius {
struct GpuVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;

    static vk::VertexInputBindingDescription GetBindingDescription();

    static std::array<vk::VertexInputAttributeDescription, 3> GetAttributeDescriptions();
};

struct GpuPrimitive {
    uint32_t firstIndex{0};
    uint32_t indexCount{0};
    int32_t vertexOffset{0};
    uint32_t materialIndex{0};
};

struct GpuTexture {
    vk::raii::Image image{nullptr};
    vk::raii::DeviceMemory memory{nullptr};
    vk::raii::ImageView view{nullptr};
};

struct GpuAsset {
    std::vector<GpuPrimitive> primitives;
    std::vector<AssetMaterial> materials;
    std::vector<uint32_t> baseColorTextureIndices;
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
    GpuAssetHandle Upload(const ModelAsset& asset, const AssetManager& assetManager);

    const GpuAsset& Get(GpuAssetHandle handle) const;

    const std::vector<GpuTexture>& Textures() const noexcept { return textures_; }
    vk::Sampler TextureSampler() const noexcept { return *textureSampler_; }
    vk::Buffer VertexBuffer() const noexcept { return *vertexBuffer_; }
    vk::Buffer IndexBuffer() const noexcept { return *indexBuffer_; }
    bool HasBuffers() const noexcept { return vertexBuffer_ != nullptr && indexBuffer_ != nullptr; }

private:
    void UploadTextureData(const AssetTexture& texture, GpuTexture& destination);

    std::pair<vk::raii::Image, vk::raii::DeviceMemory> CreateTextureImage(uint32_t width, uint32_t height) const;

    uint32_t UploadTexture(TextureHandle handle, const AssetManager& assetManager);

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

    vk::raii::Sampler textureSampler_{nullptr};
    std::vector<GpuTexture> textures_;
    std::unordered_map<uint32_t, uint32_t> uploadedTextures_;
};
}
