//
// Created by Leon on 17/09/2025.
//

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <string>
#include <Windows.h>

namespace sirius {
class SrsWindow {
public:
    static HWND CreateDeviceWindow();

    static void SetWindowTitle(const std::string& title);

    static void SetBackgroundColor(float r, float g, float b, float a);

private:
    static LRESULT HandleMessages(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept;
};
}
