#pragma once

#pragma once

#include "core/fsm.h"

class App final : public Fsm {
public:
    enum {
        kInitSystem = 0,
        kRunGame,
        kShutdownSystem
    };

    FsmReturn UpdateState(signed short state) override;

    // Implement state machine here later to handle Initialization, Game loop and teardown
    // App can inherit from state machine class, RunOneIteration is part of state machine class
    FsmReturn Init();

    static FsmReturn Shutdown();

    FsmReturn RunGame();

private:
    bool isSystemInitialized_ = false;
};
