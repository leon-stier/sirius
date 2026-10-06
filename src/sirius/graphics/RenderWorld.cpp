#include "RenderWorld.h"

#include <stdexcept>

namespace sirius {

RenderInstanceHandle RenderWorld::CreateInstance(const GpuAssetHandle asset, const glm::mat4& transform) {
    if (!asset.IsValid()) {
        throw std::runtime_error("Cannot create a render instance for an invalid GPU asset");
    }
    items_.push_back({.asset = asset, .transform = transform});
    return {.value = static_cast<uint32_t>(items_.size() - 1)};
}

void RenderWorld::DestroyInstance(const RenderInstanceHandle handle) {
    if (!handle.IsValid() || handle.value >= items_.size()) {
        throw std::runtime_error("Render instance handle is invalid");
    }
    items_[handle.value].asset = {};
}

void RenderWorld::SetTransform(const RenderInstanceHandle handle, const glm::mat4& transform) {
    if (!handle.IsValid() || handle.value >= items_.size() || !items_[handle.value].asset.IsValid()) {
        throw std::runtime_error("Render instance handle is invalid");
    }
    items_[handle.value].transform = transform;
}

}
