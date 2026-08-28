//
// Created by Leon on 16/09/2025.
//

#include "entrypoint_engine.h"

static int mainReturnValue = 0;

static bool CallMainOnce() {
    mainReturnValue = Main();
    return false;
}

// Call Main if game does not assign Entry point
bool (* projectMainPrologue)() = nullptr;

bool (* projectMainOrDoOneLoop)() = CallMainOnce;

int main() {
    if (projectMainPrologue()) {
        bool carryOn = true;
        while (carryOn) {
            carryOn = projectMainOrDoOneLoop();
        }
    }
    return 0;
}
