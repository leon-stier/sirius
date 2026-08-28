cmake_minimum_required(VERSION 4.0)
project(Sirius LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(sirius)
add_subdirectory(game)
add_subdirectory(main)
