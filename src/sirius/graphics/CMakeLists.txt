find_package(Vulkan REQUIRED)

add_library(VulkanCppModule)
add_library(Vulkan::cppm ALIAS VulkanCppModule)

# Force MSVC to build 'import std;' for this target
set_target_properties(VulkanCppModule PROPERTIES
        CXX_MODULE_STD ON
)

target_compile_definitions(VulkanCppModule
        PUBLIC
        VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
        VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
        VULKAN_HPP_CXX_MODULE_EXPERIMENTAL_WARNING
)

target_include_directories(VulkanCppModule PUBLIC "${Vulkan_INCLUDE_DIR}")
target_link_libraries(VulkanCppModule PUBLIC Vulkan::Vulkan)

target_sources(VulkanCppModule
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS "${Vulkan_INCLUDE_DIR}"
        FILES "${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm"
)

if(MSVC)
    target_compile_options(VulkanCppModule PRIVATE
            /permissive-
            /Zc:__cplusplus
            /EHsc
            /Zc:preprocessor
    )
endif()

# Graphics static library
add_library(graphics STATIC
        renderer.h
        renderer.cpp
        vkRenderer.h
        vkRenderer.cpp
)

set_target_properties(graphics PROPERTIES CXX_MODULE_STD ON)
target_link_libraries(graphics PRIVATE Vulkan::cppm)