#include "AssetManager.h"

#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>
#include <glm/fwd.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace sirius {
namespace {
    [[nodiscard]]
    glm::mat4 GetNodeTransform(const tinygltf::Node& node) {
        if (node.matrix.size() == 16) {
            glm::mat4 transform{1.0f};
            std::memcpy(
                glm::value_ptr(transform),
                node.matrix.data(),
                sizeof(float) * 16
            );
            return transform;
        }

        glm::mat4 transform{1.0f};

        if (node.translation.size() == 3) {
            transform = glm::translate(
                transform,
                glm::vec3(
                    static_cast<float>(node.translation.at(0)),
                    static_cast<float>(node.translation.at(1)),
                    static_cast<float>(node.translation.at(2))
                )
            );
        }

        if (node.rotation.size() == 4) {
            const glm::quat rotation{
                static_cast<float>(node.rotation.at(3)),
                static_cast<float>(node.rotation.at(0)),
                static_cast<float>(node.rotation.at(1)),
                static_cast<float>(node.rotation.at(2))
            };

            transform *= glm::mat4_cast(rotation);
        }

        if (node.scale.size() == 3) {
            transform = glm::scale(
                transform,
                glm::vec3(
                    static_cast<float>(node.scale.at(0)),
                    static_cast<float>(node.scale.at(1)),
                    static_cast<float>(node.scale.at(2))
                )
            );
        }

        return transform;
    }

    [[nodiscard]]
    const tinygltf::Accessor& GetAccessor(const tinygltf::Model& model, const int accessorIndex) {
        if (accessorIndex < 0 || accessorIndex >= static_cast<int>(model.accessors.size())) {
            throw std::runtime_error(std::format("Invalid glTF accessor index: {}", accessorIndex));
        }
        return model.accessors[accessorIndex];
    }

    [[nodiscard]]
    const tinygltf::BufferView& GetBufferView(const tinygltf::Model& model, const tinygltf::Accessor& accessor) {
        if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size())) {
            throw std::runtime_error("glTF accessor has no valid buffer view");
        }
        return model.bufferViews[accessor.bufferView];
    }

    [[nodiscard]]
    const tinygltf::Buffer& GetBuffer(const tinygltf::Model& model, const tinygltf::BufferView& bufferView) {
        if (bufferView.buffer < 0 || bufferView.buffer >= static_cast<int>(model.buffers.size())) {
            throw std::runtime_error("glTF buffer view has no valid buffer");
        }
        return model.buffers[bufferView.buffer];
    }

    [[nodiscard]]
    size_t GetElementSize(const tinygltf::Accessor& accessor) {
        const int componentCount = tinygltf::GetNumComponentsInType(accessor.type);
        const size_t componentSize = tinygltf::GetComponentSizeInBytes(accessor.componentType);

        if (componentCount <= 0 || componentSize == 0) {
            throw std::runtime_error("Invalid glTF accessor element type");
        }
        return static_cast<size_t>(componentCount) * componentSize;
    }

    [[nodiscard]]
    const uint8_t* GetAccessorElement(const tinygltf::Model& model, const tinygltf::Accessor& accessor, const size_t elementIndex) {
        const tinygltf::BufferView& bufferView = GetBufferView(model, accessor);
        const tinygltf::Buffer& buffer = GetBuffer(model, bufferView);

        const size_t elementSize = GetElementSize(accessor);
        const size_t stride = accessor.ByteStride(bufferView) != 0 ? accessor.ByteStride(bufferView) : elementSize;

        const size_t offset = bufferView.byteOffset + accessor.byteOffset + elementIndex * stride;

        if (offset > buffer.data.size() || elementSize > buffer.data.size() - offset) {
            throw std::runtime_error("glTF accessor references data outside its buffer");
        }
        return buffer.data.data() + offset;
    }

    [[nodiscard]]
    glm::vec3 ReadPosition(const tinygltf::Model& model, const tinygltf::Accessor& accessor, const size_t elementIndex) {
        if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || accessor.type != TINYGLTF_TYPE_VEC3) {
            throw std::runtime_error("Only FLOAT VEC3 position accessors are supported");
        }

        glm::vec3 position;
        std::memcpy(
            glm::value_ptr(position),
            GetAccessorElement(model, accessor, elementIndex),
            sizeof(position)
        );
        return position;
    }

    [[nodiscard]]
    glm::vec3 ReadNormal(const tinygltf::Model& model, const tinygltf::Accessor& accessor, const size_t elementIndex) {
        if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || accessor.type != TINYGLTF_TYPE_VEC3) {
            throw std::runtime_error("Only FLOAT VEC3 normal accessors are supported");
        }

        glm::vec3 normal;
        std::memcpy(
            glm::value_ptr(normal),
            GetAccessorElement(model, accessor, elementIndex),
            sizeof(normal)
        );
        return normal;
    }

    [[nodiscard]]
    uint32_t ReadIndex(const tinygltf::Model& model, const tinygltf::Accessor& accessor, const size_t elementIndex) {
        const uint8_t* data = GetAccessorElement(model, accessor, elementIndex);

        switch (accessor.componentType) {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                return *data;

            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
                uint16_t value;
                std::memcpy(&value, data, sizeof(value));
                return value;
            }

            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
                uint32_t value;
                std::memcpy(&value, data, sizeof(value));
                return value;
            }

            default:
                throw std::runtime_error(
                    "Only unsigned byte, unsigned short, and unsigned int glTF index types are supported"
                );
        }
    }

    [[nodiscard]]
    uint32_t GetMaterialIndex(const tinygltf::Primitive& primitive, const size_t materialCount) {
        if (primitive.material < 0) return 0;

        const auto materialIndex = static_cast<size_t>(primitive.material);

        if (materialIndex >= materialCount) {
            throw std::runtime_error(std::format("Invalid glTF material index: {}", primitive.material));
        }
        return static_cast<uint32_t>(materialIndex + 1);
    }

    void ImportPrimitive(const tinygltf::Model& model, const tinygltf::Primitive& primitive, const glm::mat4& worldTransform, ModelAsset& asset) {
        if (primitive.mode != -1 && primitive.mode != TINYGLTF_MODE_TRIANGLES) {
            throw std::runtime_error("Only triangle-list glTF primitives are supported");
        }

        const auto positionAttribute = primitive.attributes.find("POSITION");
        if (positionAttribute == primitive.attributes.end()) return;

        const tinygltf::Accessor& positionAccessor = GetAccessor(model, positionAttribute->second);
        if (positionAccessor.count == 0) return;

        const auto normalAttribute = primitive.attributes.find("NORMAL");
        const tinygltf::Accessor* normalAccessor = nullptr;
        if (normalAttribute != primitive.attributes.end()) {
            normalAccessor = &GetAccessor(model, normalAttribute->second);
        }

        const uint32_t vertexOffset = static_cast<uint32_t>(asset.vertices.size());

        const glm::mat3 normalTransform = glm::transpose(glm::inverse(glm::mat3(worldTransform)));

        asset.vertices.reserve(asset.vertices.size() + positionAccessor.count);

        for (size_t vertexIndex = 0; vertexIndex < positionAccessor.count; ++vertexIndex) {
            AssetVertex vertex{};

            const glm::vec3 position =
                    ReadPosition(
                        model,
                        positionAccessor,
                        vertexIndex
                    );

            vertex.position = glm::vec3(worldTransform * glm::vec4(position, 1.0f));

            if (normalAccessor != nullptr) {
                vertex.normal = glm::normalize(
                    normalTransform *
                    ReadNormal(
                        model,
                        *normalAccessor,
                        vertexIndex
                    )
                );
            } else {
                vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            }

            asset.vertices.push_back(vertex);
        }

        const uint32_t firstIndex = static_cast<uint32_t>(asset.indices.size());

        if (primitive.indices >= 0) {
            const tinygltf::Accessor& indexAccessor = GetAccessor(model, primitive.indices);

            asset.indices.reserve(asset.indices.size() + indexAccessor.count);

            for (size_t index = 0; index < indexAccessor.count; ++index) {
                const uint32_t localIndex = ReadIndex(model, indexAccessor, index);

                if (localIndex >= positionAccessor.count) {
                    throw std::runtime_error("glTF primitive index references a vertex outside its position accessor"
                    );
                }

                asset.indices.push_back(localIndex);
            }
        } else {
            asset.indices.reserve(asset.indices.size() + positionAccessor.count);

            for (uint32_t index = 0; index < positionAccessor.count; ++index) {
                asset.indices.push_back(index);
            }
        }

        const uint32_t materialIndex = GetMaterialIndex(primitive,asset.materials.size());

        asset.primitives.push_back({
            .firstIndex = firstIndex,
            .indexCount = static_cast<uint32_t>(asset.indices.size()) - firstIndex,
            .vertexOffset = static_cast<int32_t>(vertexOffset),
            .materialIndex = materialIndex
        });
    }

    void VisitNode(const tinygltf::Model& model, const int nodeIndex, const glm::mat4& parentTransform, ModelAsset& asset) {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(model.nodes.size())) {
            throw std::runtime_error(std::format("Invalid glTF node index: {}", nodeIndex));
        }

        const tinygltf::Node& node = model.nodes[static_cast<size_t>(nodeIndex)];

        const glm::mat4 worldTransform = parentTransform * GetNodeTransform(node);

        if (node.mesh >= 0) {
            if (node.mesh >= static_cast<int>(model.meshes.size())) {
                throw std::runtime_error(
                    std::format("Invalid glTF mesh index: {}", node.mesh)
                );
            }

            const tinygltf::Mesh& mesh = model.meshes[static_cast<size_t>(node.mesh)];

            for (const tinygltf::Primitive& primitive : mesh.primitives) {
                ImportPrimitive(
                    model,
                    primitive,
                    worldTransform,
                    asset
                );
            }
        }

        for (const int childIndex : node.children) {
            VisitNode(
                model,
                childIndex,
                worldTransform,
                asset
            );
        }
    }
}

AssetHandle AssetManager::LoadModel(const std::filesystem::path& path) {
    // Normalize file path
    std::filesystem::path normalizedPath = std::filesystem::weakly_canonical(path);
    const std::string key = normalizedPath.generic_string();

    // Check if asset is already loaded
    if (const auto it = loadedAssets_.find(key); it != loadedAssets_.end()) {
        return it->second;
    }

    const AssetHandle handle = ImportModel(normalizedPath);

    loadedAssets_.emplace(key, handle);
    return handle;
}

AssetHandle AssetManager::ImportModel(const std::filesystem::path& path) {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string warning, error;

    const std::string extension = path.extension().string();
    bool loaded = false;

    if (extension == ".glb" || extension == ".GLB") {
        loaded = loader.LoadBinaryFromFile(
            &model,
            &error,
            &warning,
            path.string()
        );
    } else if (extension == ".gltf" || extension == ".GLTF") {
        loaded = loader.LoadASCIIFromFile(
            &model,
            &error,
            &warning,
            path.string()
        );
    } else {
        throw std::runtime_error("Unsupported model format: " + path.string());
    }

    if (!warning.empty()) {
        std::cerr << "glTF warning for "
                << path.string()
                << ": "
                << warning
                << '\n';
    }

    if (!loaded) {
        throw std::runtime_error(
            "Failed to load glTF model '" +
            path.string() +
            "': " +
            error
        );
    }

    ModelAsset asset;
    asset.sourcePath = path;

    asset.materials.push_back({
        .baseColorFactor = glm::vec4(1.0f)
    });

    asset.materials.reserve(
        asset.materials.size() + model.materials.size()
    );

    for (const tinygltf::Material& sourceMaterial : model.materials) {
        AssetMaterial material{
            .baseColorFactor = glm::vec4(1.0f)
        };

        const auto& factor = sourceMaterial.pbrMetallicRoughness.baseColorFactor;

        if (factor.size() >= 4) {
            material.baseColorFactor = glm::vec4(
                static_cast<float>(factor.at(0)),
                static_cast<float>(factor.at(1)),
                static_cast<float>(factor.at(2)),
                static_cast<float>(factor.at(3))
            );
        }

        asset.materials.push_back(material);
    }

    int sceneIndex = model.defaultScene;

    if (sceneIndex < 0 && !model.scenes.empty()) {
        sceneIndex = 0;
    }

    if (sceneIndex < 0 || sceneIndex >= static_cast<int>(model.scenes.size())) {
        throw std::runtime_error("glTF model contains no usable scene: " + path.string());
    }

    const tinygltf::Scene& scene = model.scenes.at(static_cast<size_t>(sceneIndex));
    for (const int rootNode : scene.nodes) {
        VisitNode(
            model,
            rootNode,
            glm::mat4(1.0f),
            asset
        );
    }

    if (asset.primitives.empty()) {
        throw std::runtime_error("glTF model contains no renderable primitives: " + path.string());
    }

    const AssetHandle handle{
        .value = static_cast<uint32_t>(assets_.size())
    };

    assets_.push_back(std::move(asset));
    return handle;
}

const ModelAsset& AssetManager::Get(const AssetHandle handle) const {
    if (!handle.IsValid() || handle.value >= assets_.size()) {
        throw std::runtime_error("Asset handle is invalid");
    }
    return assets_.at(handle.value);
}
}
