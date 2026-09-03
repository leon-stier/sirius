#include "vkRenderer.h"

#include <iostream>
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#	include <vulkan/vulkan_raii.hpp>
#else
import vulkan;
#endif


namespace sirius {

void VkRenderer::Init() {
    std::cout << "TEST" << std::endl;
}

void VkRenderer::Draw() {
}
}
