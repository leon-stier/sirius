#pragma once

#include <limits>
#include <vector>

#include <glm/mat4x4.hpp>

#include "GpuAssetUploader.h"

namespace sirius {

struct RenderInstanceHandle {
    static constexpr uint32_t invalidValue = (std::numeric_limits<uint32_t>::max)();

    uint32_t value{invalidValue};
    bool IsValid() const noexcept { return value != invalidValue; }
    explicit operator bool() const noexcept { return IsValid(); }
    friend bool operator==(RenderInstanceHandle, RenderInstanceHandle) = default;
};

struct RenderItem {
    GpuAssetHandle asset;
    glm::mat4 transform{1.0f};
};

class RenderWorld {
public:
    RenderInstanceHandle CreateInstance(GpuAssetHandle asset, const glm::mat4& transform = glm::mat4(1.0f));
    void DestroyInstance(RenderInstanceHandle handle);
    void SetTransform(RenderInstanceHandle handle, const glm::mat4& transform);

    const std::vector<RenderItem>& Items() const noexcept { return items_; }

private:
    std::vector<RenderItem> items_;
};

}
