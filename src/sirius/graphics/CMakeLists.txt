# Graphics static library
add_library(graphics STATIC
        camera.h
        camera.cpp
        renderer.h
        renderer.cpp
        vkRenderer.h
        vkRenderer.cpp
        vulkanContext.cpp
        vulkanContext.h
        AssetManager.cpp
        AssetManager.h
)

target_link_libraries(graphics
        PUBLIC
            Vulkan::Vulkan
            KTX::ktx
)

target_include_directories(graphics PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>)

add_dependencies(graphics shaders)