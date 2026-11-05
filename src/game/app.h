#pragma once

#pragma once

#include "flight.h"
#include "core/fsm.h"

class App final : public Fsm {
public:
    enum {
        kInitSystem = 0,
        kRunGame,
        kShutdownSystem
    };



    // Implement state machine here later to handle Initialization, Game loop and teardown
    // App can inherit from state machine class, RunOneIteration is part of state machine class
    FsmReturn Init();

    static FsmReturn Shutdown();

    FsmReturn RunGame();

protected:
    FsmReturn UpdateState(signed short state) override;
private:
    std::unique_ptr<IGame> game_;
    bool isSystemInitialized_ = false;
};
