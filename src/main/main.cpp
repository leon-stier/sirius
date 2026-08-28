#include <cassert>
#include <iostream>
#include "app.h"
#include "core/entrypoint_engine.h"

static App myApp;

static bool MainPrologue() {
    std::cout << "Hello World!" << std::endl;
    return true;
}

static bool MainOneLoopIteration() {
    return myApp.RunOneIteration();
}

SET_APP_ENTRY_POINTS(MainPrologue, MainOneLoopIteration)

int Main() {
    assert(false && "Should never reach here, Sirius is using App style entry points");
    return 0;
}
