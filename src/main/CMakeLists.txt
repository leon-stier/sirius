add_executable(main
        main.cpp
        ${CMAKE_SOURCE_DIR}/sirius.rc
)

target_link_libraries(main PRIVATE game)