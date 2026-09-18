//
// Created by Leon on 17/01/2025.
//

#ifndef MOUSE_H
#define MOUSE_H
#include <iostream>
#include <ostream>

namespace sirius {
class Mouse {
public:
    Mouse();

    int GetX() noexcept;

    int GetY() noexcept;

    bool IsInWindow() noexcept;

    bool LeftIsPressed() noexcept;

    bool RightIsPressed() noexcept;

    void SetPos(int x, int y) noexcept;

    void SetIsInWindow(bool isInWindow) noexcept;

    void SetLeftIsPressed(bool leftIsPressed) noexcept;

    void SetRightIsPressed(bool rightIsPressed) noexcept;

    void SetWheelDelta(int wheelDelta) noexcept;

    void SetRawEnabled(bool rawEnabled) noexcept;

private:
    int x_{}, y_{};
    bool leftIsPressed_{}, rightIsPressed_{};
    bool isInWindow_{};
    int wheelDeltaCarry_{};
    bool rawEnabled_{};
};
}

#endif //MOUSE_H
