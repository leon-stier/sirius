add_library(core STATIC
        fsm.cpp
        fsm.h
        entrypoint_engine.cpp
        entrypoint_engine.h
)

target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)