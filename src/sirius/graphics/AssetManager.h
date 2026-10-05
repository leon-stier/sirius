//
// Created by Leon on 05/10/2026.
//

#pragma once
#include <filesystem>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>


namespace sirius {

struct AssetVertex {
    glm::vec3 position;
    glm::vec4 color;
    glm::vec3 normal;
};

struct AssetPrimitive {
    uint32_t firstIndex;
    uint32_t indexCount;
    int32_t vertexOffset;
    uint32_t materialIndex;
};

struct AssetMaterial {
    glm::vec4 baseColorFactor;
};

struct ModelAsset {
    std::filesystem::path sourcePath;
    std::vector<AssetVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<AssetPrimitive> primitives;
    std::vector<AssetMaterial> materials;
};

struct AssetHandle {
    uint32_t value{std::numeric_limits<uint32_t>::max()};

    bool IsValid() const noexcept {
        return value != std::numeric_limits<uint32_t>::max();
    }
};

class AssetManager {
public:
    AssetHandle LoadModel(const std::filesystem::path& path);

    const ModelAsset& Get(AssetHandle handle) const;
private:
    AssetHandle ImportModel(const std::filesystem::path& path);
    std::vector<ModelAsset> assets_;
    std::unordered_map<std::string, AssetHandle> loadedAssets_;
};

}
