add_library(game app.h app.cpp)

target_include_directories(game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(game PUBLIC core graphics window)