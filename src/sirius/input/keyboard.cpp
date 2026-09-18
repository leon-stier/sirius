#include "Keyboard.h"


bool Keyboard::IsKeyPressed(unsigned char keycode) noexcept {
    return keyStates_[keycode];
}

char Keyboard::ReadChar() noexcept {
    if (charBuffer_.empty()) {
        return {};
    }
    const char charcode = charBuffer_.front();
    charBuffer_.pop();
    return charcode;
}

bool Keyboard::CharIsEmpty() noexcept {
    return charBuffer_.empty();
}

void Keyboard::ClearChar() noexcept {
    charBuffer_ = std::queue<char>();
}

void Keyboard::Clear() noexcept {
    ClearChar();
}

void Keyboard::OnKeyPressed(unsigned char keycode) noexcept {
    keyStates_[keycode] = true;
}

void Keyboard::OnKeyReleased(unsigned char keycode) noexcept {
    keyStates_[keycode] = false;
}

void Keyboard::ClearState() noexcept {
    keyStates_.reset();
}

void Keyboard::OnChar(char character) noexcept {
    charBuffer_.emplace(character);
}