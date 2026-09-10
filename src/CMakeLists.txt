cmake_minimum_required(VERSION 4.0)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
        "a9e1cf81-9932-4810-974b-6eccaf14e457"
)
set(CMAKE_CXX_MODULE_STD ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(Sirius
        VERSION 0.1
        LANGUAGES CXX
)

find_package(Vulkan 1.4.357 REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(tinyobjloader CONFIG REQUIRED)

# ------------------------------------------------------------------------------
# Vulkan-Hpp C++ module
# ------------------------------------------------------------------------------

add_library(SiriusVulkanModule)

add_library(Vulkan::cppm ALIAS SiriusVulkanModule)

target_compile_features(SiriusVulkanModule
        PUBLIC
        cxx_std_23
)

target_compile_definitions(SiriusVulkanModule
        PUBLIC
        VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
        VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
        VK_USE_PLATFORM_WIN32_KHR
        VULKAN_HPP_CXX_MODULE_EXPERIMENTAL_WARNING
)

target_link_libraries(SiriusVulkanModule
        PUBLIC
        Vulkan::Vulkan
)

target_include_directories(SiriusVulkanModule
        PUBLIC
        "${Vulkan_INCLUDE_DIR}"
)

target_sources(SiriusVulkanModule
        PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS "${Vulkan_INCLUDE_DIR}"
        FILES "${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm"
)

set_target_properties(SiriusVulkanModule PROPERTIES
        CXX_SCAN_FOR_MODULES ON
        CXX_MODULE_STD ON
)

if(MSVC)
    target_compile_options(SiriusVulkanModule
            PRIVATE
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /translateInclude
    )
endif()

# ------------------------------------------------------------------------------
# Project
# ------------------------------------------------------------------------------

add_subdirectory(sirius)
add_subdirectory(game)
add_subdirectory(main)