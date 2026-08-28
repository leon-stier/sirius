add_library(window STATIC
        window.cpp
        window.h

)

target_include_directories(window PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)