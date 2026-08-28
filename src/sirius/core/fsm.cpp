//
// Created by Leon on 16/09/2025.
//

#include "fsm.h"

Fsm::Fsm() {
    currentState_ = 0;
    nextState_ = 0;
}

Fsm::FsmReturn Fsm::Update() {
    FsmReturn ret = kUnhandled;
    while (ret == kUnhandled) {
        // If the next state differs from the current state, there is a request to transition to the next state
        // Update the current state and let the app handle it
        if (currentState_ != nextState_) {
            currentState_ = nextState_;
            ret = UpdateState(currentState_);
            // If the next state is the same as the current, we might be in a scenario where the state hasn't been updated yet, but we also weren't in the current state before. This occurs in the very first run for example.
            // Check if we enter for the first time (we could send an event or different state to allow for some setup of the state)
            // If yes, clear the flag. If not, do a regular update. This probably leads to the main game loop
        } else {
            if (currentStateNotYetEntered_) {
                currentStateNotYetEntered_ = false;
                ret = UpdateState(currentState_);
            } else {
                ret = UpdateState(currentState_);
            }
        }
    }
    return ret;
}

bool Fsm::RunOneIteration() noexcept {
    if (Update() == kExit) {
        return false;
    }
    return true;
}

void Fsm::SetState(signed short state) noexcept {
    nextState_ = state;
}

signed short Fsm::GetState() const noexcept {
    return currentState_;
}
