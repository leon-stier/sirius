//
// Created by Leon on 17/09/2025.
//

#pragma once


#define WIN32_LEAN_AND_MEAN
#include <string>
#include <Windows.h>


inline LRESULT (__stdcall * windowProc)(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) = DefWindowProc;

inline HWND hwndMain = nullptr;
inline HINSTANCE hInstance = nullptr;
inline unsigned winClass;

inline std::string windowTitle;
inline uint32_t windowWidth = 800;
inline uint32_t windowHeight = 600;
inline bool closing = false;
