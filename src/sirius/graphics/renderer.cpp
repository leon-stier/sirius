#include "renderer.h"

#include "vkRenderer.h"

namespace sirius {

VulkanContext Renderer::vulkanContext_;
VkRenderer Renderer::vkRenderer_;

void Renderer::Init() {
    vulkanContext_.Init();
    vkRenderer_.Init(vulkanContext_);
}

void Renderer::Draw() {
    vkRenderer_.Draw();
}

RenderInstanceHandle Renderer::LoadModelInstance(const std::filesystem::path& path, const glm::mat4& transform) {
    return vkRenderer_.LoadModelInstance(path, transform);
}

void Renderer::DestroyInstance(const RenderInstanceHandle handle) {
    vkRenderer_.DestroyInstance(handle);
}

void Renderer::SetInstanceTransform(const RenderInstanceHandle handle, const glm::mat4& transform) {
    vkRenderer_.SetInstanceTransform(handle, transform);
}

}
