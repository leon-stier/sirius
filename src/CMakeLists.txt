cmake_minimum_required(VERSION 4.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(Sirius
        VERSION 0.1
        LANGUAGES CXX
)

find_package(Vulkan 1.4.357 REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Ktx CONFIG REQUIRED)

add_compile_definitions(
        VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
        VK_USE_PLATFORM_WIN32_KHR
)
# ------------------------------------------------------------------------------
# Project
# ------------------------------------------------------------------------------

add_subdirectory(sirius)
add_subdirectory(game)
add_subdirectory(main)