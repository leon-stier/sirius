add_library(input STATIC
        input_manager.cpp
        input_manager.h
        keyboard.cpp
        keyboard.h
        mouse.cpp
        mouse.h
)

target_include_directories(input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)