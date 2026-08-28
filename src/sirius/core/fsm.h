//
// Created by Leon on 16/09/2025.
//

#pragma once


class Fsm {
public:
    enum FsmReturn {
        kUnhandled = 0,
        kContinue,
        kExit,
    };

    Fsm();

    virtual ~Fsm() = default;

    FsmReturn Update();

    bool RunOneIteration() noexcept;

    void SetState(signed short state) noexcept;

    [[nodiscard]] signed short GetState() const noexcept;

protected:
    // App implements this to define where to go from which state.
    virtual FsmReturn UpdateState(signed short state) = 0;

    bool currentStateNotYetEntered_ = true;

private:
    signed short currentState_;
    signed short nextState_;
};
