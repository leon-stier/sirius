//
// Created by Leon on 29/10/2025.
//

#pragma once

#include "input/input_manager.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include  <glm/glm.hpp>

namespace sirius {
template<class... Ts> struct Overload : Ts...{using Ts::operator()...; };

class Camera {

public:
    void Init();

    void ProcessWindowEvent(sirius::InputEvent event);

    void Update();

    glm::mat4 GetViewMatrix();

    glm::mat4 GetRotationMatrix();

    glm::mat4 GetLockedMatrix();

    void SetLockedMatrix(const glm::mat4& newTransform);

    glm::mat4 transform_;

    glm::vec3 velocity_;
    glm::vec3 position_;
    float pitch_{0.0f};
    float yaw_{0.0f};
};
}
