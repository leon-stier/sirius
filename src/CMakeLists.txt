cmake_minimum_required(VERSION 4.0)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "a9e1cf81-9932-4810-974b-6eccaf14e457") # UUID required by CMake for experimental features
set(CMAKE_CXX_MODULE_STD ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(Sirius LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/CMake")

find_package(Vulkan 1.4.357 REQUIRED)
find_package(glm REQUIRED)
find_package(tinyobjloader REQUIRED)
find_package(tinygltf REQUIRED)
#find_package(KTX REQUIRED)
find_package(stb REQUIRED)

option(ENABLE_CPP20_MODULE "Enable C++ 20 module support for Vulkan" ON)

if(ENABLE_CPP20_MODULE)
    set(CMAKE_CXX_SCAN_FOR_MODULES ON)
    add_library(VulkanCppModule)
    add_library(Vulkan::cppm ALIAS VulkanCppModule)


    target_compile_definitions(VulkanCppModule
            PUBLIC
            VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
            VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
            VK_USE_PLATFORM_WIN32_KHR
            VULKAN_HPP_CXX_MODULE_EXPERIMENTAL_WARNING
    )
    target_include_directories(VulkanCppModule PUBLIC "${Vulkan_INCLUDE_DIR}")
    target_link_libraries(VulkanCppModule PUBLIC Vulkan::Vulkan)
    set_target_properties(VulkanCppModule PROPERTIES
            CXX_MODULE_STD ON

    )

    if(MSVC)
        target_compile_options(VulkanCppModule PRIVATE
                /std:c++latest
                /permissive-
                /Zc:__cplusplus
                /EHsc
                /Zc:preprocessor
                /translateInclude
        )
    endif()

    target_sources(VulkanCppModule
            PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES
            BASE_DIRS "${Vulkan_INCLUDE_DIR}"
            FILES "${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm"
    )
else()
    # Interface library fallback when C++20 modules are disabled
    add_library(VulkanCppModule INTERFACE)
    add_library(Vulkan::cppm ALIAS VulkanCppModule)
    target_link_libraries(VulkanCppModule INTERFACE Vulkan::Vulkan)
    target_compile_definitions(VulkanCppModule
            INTERFACE
            VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
            VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
    )
endif()

add_subdirectory(sirius)
add_subdirectory(game)
add_subdirectory(main)
