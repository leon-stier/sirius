//
// Created by Leon on 07/10/2026.
//

#pragma once
#include "gameState.h"
#include "IGame.h"
#include "graphics/renderWorld.h"


namespace sirius {
struct InputEvent;
}

class Flight : public IGame {
public:
    void Init() override;
    void StartRun();
    void Update() override;
    void EndRun();

private:
    void ProcessInputEvent(sirius::InputEvent event);

    PlaneState planeState_;

    sirius::RenderInstanceHandle levelInstance_;
    sirius::RenderInstanceHandle planeInstance_;
    std::vector<sirius::RenderInstanceHandle> cubes_;
};
