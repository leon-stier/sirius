# Graphics static library
add_library(graphics STATIC
        renderer.h
        renderer.cpp
        vkRenderer.h
        vkRenderer.cpp
        vulkanContext.cpp
        vulkanContext.h
)

target_link_libraries(graphics
        PUBLIC
            Vulkan::Vulkan
            glm::glm
)

target_include_directories(graphics PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>)

add_dependencies(graphics shaders)