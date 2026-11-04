//
// Created by Leon on 05/10/2026.
//

#pragma once
#include <filesystem>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>


namespace sirius {

struct AssetVertex {
    glm::vec3 position;
    glm::vec4 color;
    glm::vec3 normal;
    glm::vec2 uv;
};

struct AssetPrimitive {
    uint32_t firstIndex;
    uint32_t indexCount;
    int32_t vertexOffset;
    uint32_t materialIndex;
};

struct TextureHandle {
    static constexpr uint32_t invalidValue = (std::numeric_limits<uint32_t>::max)();

    uint32_t value{invalidValue};

    [[nodiscard]] bool IsValid() const noexcept {
        return value != invalidValue;
    }

    explicit operator bool() const noexcept { return IsValid(); }
    friend bool operator==(TextureHandle, TextureHandle) = default;
};

struct AssetTexture {
    uint32_t width{0};
    uint32_t height{0};
    std::vector<uint8_t> pixels;
};

struct AssetMaterial {
    glm::vec4 baseColorFactor;
    std::optional<TextureHandle> baseColorTexture;
};


struct ModelAsset {
    std::filesystem::path sourcePath;
    std::vector<AssetVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<AssetPrimitive> primitives;
    std::vector<AssetMaterial> materials;
};

struct AssetHandle {
    static constexpr uint32_t invalidValue = (std::numeric_limits<uint32_t>::max)();

    uint32_t value{invalidValue};

    bool IsValid() const noexcept {
        return value != invalidValue;
    }

    explicit operator bool() const noexcept { return IsValid(); }
    friend bool operator==(AssetHandle, AssetHandle) = default;
};

class AssetManager {
public:
    AssetHandle LoadModel(const std::filesystem::path& path);

    const ModelAsset& Get(AssetHandle handle) const;
    const AssetTexture& Get(TextureHandle handle) const;
private:
    AssetHandle ImportModel(const std::filesystem::path& path);

    std::vector<ModelAsset> assets_;
    std::unordered_map<std::string, AssetHandle> loadedAssets_;

    std::vector<AssetTexture> textures_;
    std::unordered_map<std::string, TextureHandle> loadedTextures_;
};

}
