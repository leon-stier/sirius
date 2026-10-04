//
// Created by Leon on 29/10/2025.
//

#include "camera.h"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/quaternion.hpp"
#include "glm/gtx/transform.hpp"


namespace sirius {
void Camera::Init() {
    InputManager::Subscribe([this](const InputEvent& e) { ProcessWindowEvent(e); });
}

void Camera::ProcessWindowEvent(InputEvent event) {
    std::visit( Overload{
        [this](MouseMoveEvent e) {
            yaw_ += static_cast<float>(e.x) / 500.0f;
            pitch_ -= static_cast<float>(e.y) / 500.0f;

        },
        [](MouseWheelEvent e) {},
        [this, event](KeyEvent e) {
            if (event.type == InputEvent::Type::kKeyDown) {
                if (e.key == 'W') velocity_.z = 1;
                if (e.key == 'S') velocity_.z = -1;
                if (e.key == 'A') velocity_.x = 1;
                if (e.key == 'D') velocity_.x = -1;
                if (e.key == VK_SPACE) velocity_.y = -1;
                if (e.key == VK_CONTROL) velocity_.y = 1;
            }
            if (event.type == InputEvent::Type::kKeyUp) {
                if (e.key == 'W') velocity_.z = 0;
                if (e.key == 'S') velocity_.z = 0;
                if (e.key == 'A') velocity_.x = 0;
                if (e.key == 'D') velocity_.x = 0;
                if (e.key == VK_SPACE) velocity_.y = 0;
                if (e.key == VK_CONTROL) velocity_.y = 0;
            }
        }
    }, event.data);
}

void Camera::Update() {
    glm::mat4 cameraRotation = GetRotationMatrix();
    position_ += glm::vec3(cameraRotation * glm::vec4(velocity_ * -0.05f, 0.0f));
}

glm::mat4 Camera::GetViewMatrix() {
    glm::mat4 cameraTranslation = glm::translate(glm::mat4(1.0f), position_);
    glm::mat4 cameraRotation = GetRotationMatrix();

    return glm::inverse(cameraTranslation * cameraRotation);
}

glm::mat4 Camera::GetRotationMatrix() {
    glm::quat pitchRotation = glm::angleAxis(pitch_, glm::vec3 { 1.f, 0.f, 0.f });
    glm::quat yawRotation = glm::angleAxis(yaw_, glm::vec3 { 0.f, -1.f, 0.f });

    return glm::toMat4(yawRotation * pitchRotation);
}
}
