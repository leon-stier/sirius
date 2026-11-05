#pragma once
#include "IGame.h"
#include "graphics/renderWorld.h"


class TestGame : public IGame {
public:
    void Init() override;
    void Update() override;
private:
    std::vector<sirius::RenderInstanceHandle> cubes_{};
    std::vector<float> radii;
    std::vector<float> angles;
    std::vector<float> speeds;
    std::vector<glm::vec3> positions;
};
