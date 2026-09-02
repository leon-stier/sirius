add_library(graphics STATIC
        renderer.h
        renderer.cpp
        vkRenderer.h
        vkRenderer.cpp
)

find_package(Vulkan REQUIRED)

# Vulkan C++ Module support
add_library(VulkanCppModule)
add_library(Vulkan::cppm ALIAS VulkanCppModule)

target_compile_definitions(VulkanCppModule
        PUBLIC VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1 VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
)
target_include_directories(VulkanCppModule
        PUBLIC
        "${Vulkan_INCLUDE_DIR}"
)
target_link_libraries(VulkanCppModule
        PUBLIC
        Vulkan::Vulkan
)

set_target_properties(VulkanCppModule PROPERTIES CXX_STANDARD 20)

# Add MSVC-specific compiler options for proper C++ module support
if(MSVC)
    target_compile_options(VulkanCppModule PRIVATE
            /std:c++latest      # Use latest C++ standard for better module support
            /permissive-        # Standards conformance mode
            /Zc:__cplusplus     # Enable correct __cplusplus macro
            /EHsc               # Enable C++ exception handling
            /Zc:preprocessor    # Use conforming preprocessor
            /translateInclude   # Automatically translate #include to import for standard library
    )
endif()

target_sources(VulkanCppModule
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS
        "${Vulkan_INCLUDE_DIR}"
        FILES
        "${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm"
)


# Add the vulkan.cppm file directly as a source file
target_sources(VulkanCppModule
        PRIVATE
        "${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm"
)




#add_dependencies(graphics shaders)

target_link_libraries(graphics
        PRIVATE
        Vulkan::Vulkan
        Vulkan::Headers
)

target_include_directories(graphics PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_definitions(graphics PRIVATE
        VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
)