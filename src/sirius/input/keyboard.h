#pragma once
#include <queue>
#include <bitset>

class Keyboard {
public:
    [[nodiscard]] bool IsKeyPressed(unsigned char keycode) noexcept;

    // char event stuff
    char ReadChar() noexcept;

    [[nodiscard]] bool CharIsEmpty() noexcept;

    void ClearChar() noexcept;

    void Clear() noexcept;

    void OnKeyPressed(unsigned char keycode) noexcept;

    void OnKeyReleased(unsigned char keycode) noexcept;

    void OnChar(char character) noexcept;

    void ClearState() noexcept;

private:
    unsigned int nKeys_ = 256u;
    unsigned int bufferSize_ = 16u;
    std::bitset<256> keyStates_;
    std::queue<char> charBuffer_;
};
