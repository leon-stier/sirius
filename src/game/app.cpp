#include "app.h"

#include <iostream>
#include <optional>

#include "window/window.h"
#include "window/wndProc.h"


Fsm::FsmReturn App::UpdateState(signed short state) {
    switch (state) {
        case kInitSystem:
            return Init();
        case kRunGame:
            return RunGame();
        case kShutdownSystem:
            return Shutdown();
        default:
            return kUnhandled;
    }
}

Fsm::FsmReturn App::Init() {
    if (!hwndMain) {
        hwndMain = sirius::SrsWindow::CreateDeviceWindow();
    }
    SetState(kRunGame);
    return kContinue;
}

Fsm::FsmReturn App::Shutdown() {
    return kExit;
}

Fsm::FsmReturn App::RunGame() {
    MSG message;
    std::optional<int> exitCode = {};
    if (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            exitCode = static_cast<int>(message.wParam);
            std::cout << "Exiting" << std::endl;
        }
        TranslateMessage(&message);
        DispatchMessage(&message);
    }
    if (exitCode == 0) {
        SetState(kShutdownSystem);
        return kContinue;
    }
    return kContinue;
}
