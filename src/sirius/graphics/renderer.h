#pragma once


namespace sirius {
class VkRenderer;

class Renderer {
public:
    static void Init();

    static void Draw();
private:
    static VkRenderer vkRenderer_;
};
}