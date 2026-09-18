//
// Created by Leon on 29/10/2025.
//

#include "input_manager.h"

#include "Keyboard.h"
#include "mouse.h"

#include "window/wndProc.h"

namespace sirius {
POINT screenCenter = {static_cast<long>(windowWidth / 2), static_cast<long>(windowHeight / 2)};


LRESULT CALLBACK InputWindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) { return InputManager::Get().ProcessMessage(hWnd, msg, wParam, lParam); }

void InputManager::Init() {
    windowProc = InputWindowProc;
    // ShowCursor(false);
}

std::pair<float, float> InputManager::GetMouseCoords() {
    auto mouse = Get().GetActiveMouse();
    return std::make_pair(mouse.GetX(), mouse.GetY());
}

void InputManager::Subscribe(const std::function<void(const InputEvent&)>& callback) {
    Get().callbacks_.push_back(callback);
}

void InputManager::Notify() {
    Get().events_.push_back({.type = InputEvent::Type::kMouseMove, .data = MouseMoveEvent{Get().mousedDeltaX_, Get().mousedDeltaY_}});
    for (int i = std::min(static_cast<int>(Get().events_.size()) - 1, 10); i >= 0; --i) {
        for (auto& callback : Get().callbacks_) {
            callback(Get().events_.at(i));
        }
    }
    Get().events_.clear();
}

LRESULT CALLBACK InputManager::ProcessMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        /*********** KEYBOARD MESSAGES ***********/
        case WM_SYSKEYDOWN:
        case WM_KEYDOWN: {
            if (wParam == VK_ESCAPE) {
                captureMouse_ = false;
                while (ShowCursor(true) < 0) {};
                events_.clear();
                mousedDeltaX_ = 0;
                mousedDeltaY_ = 0;
                break;
            }
            InputEvent event;
            event.type = InputEvent::Type::kKeyDown;
            event.data = KeyEvent{static_cast<unsigned char>(wParam), true};
            events_.push_back(event);
            break;
        }
        case WM_SYSKEYUP:
        case WM_KEYUP: {
            InputEvent event;
            event.type = InputEvent::Type::kKeyUp;
            event.data = KeyEvent{static_cast<unsigned char>(wParam), false};
            events_.push_back(event);
            break;
        }
        case WM_CHAR:

            break;
        /*********** END KEYBOARD MESSAGES ***********/
        /************** MOUSE MESSAGES ***************/
        case WM_MOUSEMOVE: {
            if (captureMouse_) {
                const auto [x, y] = MAKEPOINTS(lParam);
                mousedDeltaX_ = x - screenCenter.x;
                mousedDeltaY_ = y - screenCenter.y;
                POINT p = screenCenter;
                ClientToScreen(hWnd, &p);
                SetCursorPos(p.x, p.y);
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            break;
        }
        case WM_LBUTTONUP: {
            const auto [x, y]{MAKEPOINTS(lParam)};
            GetActiveMouse().SetLeftIsPressed(false);
            break;
        }
        case WM_RBUTTONDOWN: {
            if (const auto [x, y] = MAKEPOINTS(lParam); x >= 0 && x < windowWidth && y >= 0 && y < windowHeight) {
                // ShowCursor(false);
                captureMouse_ = true;
            }
            break;
        }
        case WM_RBUTTONUP: {
            const auto [x, y]{MAKEPOINTS(lParam)};
            GetActiveMouse().SetRightIsPressed(false);
            break;
        }
        case WM_MOUSEWHEEL: {
            const auto [x, y] = MAKEPOINTS(lParam);
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            GetActiveMouse().SetWheelDelta(delta);
            break;
        }
        /************ END MOUSE MESSAGES *************/
        default:
            return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

InputManager::InputManager() {
    mice_.emplace_back();
    keyboards_.emplace_back();
}

Mouse InputManager::GetActiveMouse() {
    return mice_.at(activeMouse_);
}
}
