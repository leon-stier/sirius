#pragma once
#include <glm/vec3.hpp>

struct PlaneState {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float pitch{0.0f};
    float roll{0.0f};
    float rollSpeed{0.0f};
    float speed{0.0f};
    bool airborne{false};
};

struct UpgradeState {
    int engineLevel{0};
    int fuelLevel{0};
    int controlLevel{0};
};

struct gameState {
    PlaneState plane;
    UpgradeState upgrades;
    int coins{0};
    float distance{0.0f};
    bool running{false};
};
