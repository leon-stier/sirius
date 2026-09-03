# Graphics static library
add_library(graphics STATIC
        renderer.h
        renderer.cpp
        vkRenderer.h
        vkRenderer.cpp
)

target_link_libraries(graphics
        PUBLIC
            Vulkan::cppm
            glm::glm
)

if (ENABLE_CPP20_MODULE)
    target_compile_definitions(graphics PRIVATE USE_CPP20_MODULES=1)
endif ()


target_include_directories(graphics
        PUBLIC  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)