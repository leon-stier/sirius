//
// Created by Leon on 29/10/2025.
//
#pragma once
#include <functional>
#include "Keyboard.h"
#include "mouse.h"
#define NOMINMAX
#include <variant>

#include "window/window.h"


namespace sirius {
LRESULT CALLBACK InputWindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

struct KeyEvent {
    unsigned char key;
    bool pressed;
};

struct MouseMoveEvent {
    int x;
    int y;
};

struct MouseWheelEvent {
    float delta;
};

struct InputEvent {
    enum class Type {
        kKeyDown,
        kKeyUp,
        kMouseMove,
        kMouseDown,
        kMouseUp,
        kMouseWheel
    };
    Type type;
    std::variant<KeyEvent, MouseMoveEvent, MouseWheelEvent> data;
};

class InputManager {
public:
    static InputManager& Get() {
        static InputManager instance;
        return instance;
    }

    static void Init();

    static void Subscribe(const std::function<void(const InputEvent&)>& callback);

    static void Notify();

    LRESULT CALLBACK ProcessMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    static std::pair<float, float> GetMouseCoords();

private:
    InputManager();

    Mouse GetActiveMouse();

    bool captureMouse_{true};

    int mousedDeltaX_;
    int mousedDeltaY_;

    int activeMouse_{0};
    int activeKeyboard_{0};
    std::vector<Mouse> mice_;
    std::vector<Keyboard> keyboards_;
    LPBYTE buffer_[sizeof(RAWINPUT)];

    std::vector<InputEvent> events_;
    std::vector<std::function<void(const InputEvent&)> > callbacks_;
};
} // sirius
