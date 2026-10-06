#pragma once

#include <filesystem>

#include <glm/mat4x4.hpp>

#include "RenderWorld.h"

class VulkanContext;

namespace sirius {
class VkRenderer;

class Renderer {
public:
    static void Init();

    static void Draw();

    static RenderInstanceHandle LoadModelInstance(const std::filesystem::path& path, const glm::mat4& transform = glm::mat4(1.0f));
    static void DestroyInstance(RenderInstanceHandle handle);
    static void SetInstanceTransform(RenderInstanceHandle handle, const glm::mat4& transform);
private:
    static VulkanContext vulkanContext_;
    static VkRenderer vkRenderer_;
};
}
