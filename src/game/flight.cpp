//
// Created by Leon on 07/10/2026.
//

#include "flight.h"

#include <glm/fwd.hpp>
#include <glm/detail/type_quat.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include "graphics/renderer.h"
#include "input/input_manager.h"

void Flight::Init() {
    levelInstance_ = sirius::Renderer::LoadModelInstance("../../resources/tree.glb", glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, 0.0f, 0.0f)));
    planeInstance_ = sirius::Renderer::LoadModelInstance("../../resources/plane.glb", glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, 0.0f, 0.0f)));

    sirius::InputManager::Subscribe([this](const sirius::InputEvent& e) { ProcessInputEvent(e); });
}

void Flight::StartRun() {
}

void Flight::Update(float deltaTime) {
    glm::quat pitchRotation = glm::angleAxis(planeState_.pitch, glm::vec3 { 1.f, 0.f, 0.f });
    glm::quat rollRotation = glm::angleAxis(planeState_.roll + planeState_.rollSpeed, glm::vec3 { 0.f, 0.f, 1.f });
    auto rotation = glm::toMat4(rollRotation * pitchRotation);
    planeState_.position += glm::vec3(rotation * glm::vec4(planeState_.velocity * -0.05f, 0.0f));
    sirius::Renderer::SetInstanceTransform(planeInstance_, glm::translate(glm::mat4(1.0f), planeState_.position) * rotation);
}

void Flight::EndRun() {
}

namespace {
template<class... Ts> struct Overload : Ts...{using Ts::operator()...; };
}

void Flight::ProcessInputEvent(sirius::InputEvent event) {
    std::visit( Overload{
        [this](sirius::MouseMoveEvent e) {
            // planeState_ -= static_cast<float>(e.x) / 500.0f;
            planeState_.pitch -= static_cast<float>(e.y) / 500.0f;
            planeState_.pitch = std::clamp(planeState_.pitch, -1.55f, 1.55f);
        },
        [](sirius::MouseWheelEvent e){},
       [this, event](sirius::KeyEvent e) {
           if (event.type == sirius::InputEvent::Type::kKeyDown) {
               if (e.key == 'W') planeState_.velocity.z += 1;
               if (e.key == 'S') planeState_.velocity.z += -1;
               if (e.key == 'A') planeState_.rollSpeed += -1;
               if (e.key == 'D') planeState_.rollSpeed += 1;
               // if (e.key == VK_SPACE) velocity_.y = -1;
               // if (e.key == VK_CONTROL) velocity_.y = 1;
           }
           if (event.type == sirius::InputEvent::Type::kKeyUp) {
               // if (e.key == 'W') velocity_.z = 0;
               // if (e.key == 'S') velocity_.z = 0;
               // if (e.key == 'A') velocity_.x = 0;
               // if (e.key == 'D') velocity_.x = 0;
               // if (e.key == VK_SPACE) velocity_.y = 0;
               // if (e.key == VK_CONTROL) velocity_.y = 0;
           }
       }
   }, event.data);
}
