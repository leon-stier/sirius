//
// Created by Leon on 05/11/2026.
//

#include "TestGame.h"

#include <random>
#include <glm/ext/matrix_transform.hpp>

#include "graphics/renderer.h"


static float random_float(const float min = 0.0f, const float max = 1.0f) {
    thread_local static std::mt19937 gen{std::random_device{}()};
    std::uniform_real_distribution dist(min, max);
    return dist(gen);
}


void TestGame::Init() {
    for (int i = 0; i < 20; i++) {
        for (int j = 0; j < 20; j++) {
            auto pos = glm::vec3(random_float(-20.0, 20.0f), random_float(-20.0, 20.0f), random_float(-20.0, 20.0f));
            float r = std::sqrt(pos.x * pos.x + pos.z * pos.z); // Distance from origin in XZ plane
            radii.push_back(r > 0.001f ? r : 0.1f);
            angles.push_back(std::atan2(pos.z, pos.x));          // Starting angle
            speeds.push_back(10.0f / radii.back());
            positions.push_back(pos);
            cubes_.push_back(sirius::Renderer::LoadModelInstance("../../resources/cubeTextured.glb", glm::translate(glm::mat4(1.0f), pos)));
        }
    }
}



void TestGame::Update() {
    static auto startTime = std::chrono::high_resolution_clock::now();
    auto now = std::chrono::high_resolution_clock::now();
    float currentTime = std::chrono::duration<float>(now - startTime).count();
    for (size_t i = 0; i < cubes_.size(); ++i) {
        // Advance angle over time
        float currentAngle = angles[i] + (currentTime * speeds[i]);

        // Recalculate X and Z on the circle
        positions[i].x = radii[i] * std::cos(currentAngle);
        positions[i].z = radii[i] * std::sin(currentAngle);

        // Optional: add a slight sine wave bob to Y for a swirling debris effect
        positions[i].y += std::sin(currentTime * 2.0f + i) * 0.01f;

        // Send it to your engine
        sirius::Renderer::SetInstanceTransform(cubes_[i], glm::translate(glm::mat4(1.0f), positions[i]));
    }

}
