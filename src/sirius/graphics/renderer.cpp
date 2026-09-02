#include "renderer.h"

#include "vkRenderer.h"

namespace sirius {

VkRenderer Renderer::vkRenderer_;

void Renderer::Init() {
    vkRenderer_.Init();
}

void Renderer::Draw() {
    vkRenderer_.Draw();
}

}