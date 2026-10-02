#pragma once


class VulkanContext;

namespace sirius {
class VkRenderer;

class Renderer {
public:
    static void Init();

    static void Draw();
private:
    static VulkanContext vulkanContext_;
    static VkRenderer vkRenderer_;
};
}
