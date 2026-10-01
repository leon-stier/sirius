#include "renderer.h"

#include "vkRenderer.h"

namespace sirius {

VkRenderer Renderer::vkRenderer_;
VulkanContext Renderer::vulkanContext_;

void Renderer::Init() {
    vulkanContext_.Init();
    vkRenderer_.Init(vulkanContext_);
}

void Renderer::Draw() {
    vkRenderer_.Draw();
}

}