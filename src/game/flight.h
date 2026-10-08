//
// Created by Leon on 07/10/2026.
//

#pragma once
#include "gameState.h"
#include "graphics/RenderWorld.h"


namespace sirius {
struct InputEvent;
}

class Flight {
public:
    void Init();
    void StartRun();
    void Update(float deltaTime);
    void EndRun();

private:
    void ProcessInputEvent(sirius::InputEvent event);

    PlaneState planeState_;

    sirius::RenderInstanceHandle levelInstance_;
    sirius::RenderInstanceHandle planeInstance_;
};
