#pragma once

class IGame {
public:
    virtual ~IGame() = default;

    virtual void Init() = 0;
    virtual void Update() = 0;
};