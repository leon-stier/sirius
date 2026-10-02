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

}
