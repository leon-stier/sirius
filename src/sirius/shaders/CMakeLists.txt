# ------------------------------------------------------------------------------
# Shader compiler
# ------------------------------------------------------------------------------

find_program(SLANGC_EXECUTABLE
        NAMES slangc
        REQUIRED
)

message(STATUS "Using slangc: ${SLANGC_EXECUTABLE}")


# ------------------------------------------------------------------------------
# Configuration
# ------------------------------------------------------------------------------

set(SHADER_OUTPUT_DIR
        "${CMAKE_BINARY_DIR}/main/shaders"
)

set(SHADER_PROFILE
        "spirv_1_4"
)


# ------------------------------------------------------------------------------
# Shader compilation helper
# ------------------------------------------------------------------------------

function(sirius_compile_shader OUTPUT_NAME SOURCE_FILE)
    set(SPIRV_FILE
            "${SHADER_OUTPUT_DIR}/${OUTPUT_NAME}.spv"
    )

    set(DEPFILE
            "${SHADER_OUTPUT_DIR}/${OUTPUT_NAME}.d"
    )

    add_custom_command(
            OUTPUT
            "${SPIRV_FILE}"

            COMMAND
            "${CMAKE_COMMAND}" -E make_directory
            "${SHADER_OUTPUT_DIR}"

            COMMAND
            "${SLANGC_EXECUTABLE}"
            "${SOURCE_FILE}"

            -target spirv
            -profile "${SHADER_PROFILE}"
            -emit-spirv-directly
            -fvk-use-entrypoint-name

            -entry vertMain
            -entry fragMain

            -depfile "${DEPFILE}"

            -o "${SPIRV_FILE}"

            DEPENDS
            "${SOURCE_FILE}"

            DEPFILE
            "${DEPFILE}"

            COMMENT
            "Compiling Slang shader ${SOURCE_FILE}"

            VERBATIM
    )

    set(SHADER_OUTPUTS
            ${SHADER_OUTPUTS}
            "${SPIRV_FILE}"
            PARENT_SCOPE
    )
endfunction()


# ------------------------------------------------------------------------------
# Shaders
# ------------------------------------------------------------------------------

file(GLOB_RECURSE SHADER_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.slang"
)

foreach (SHADER_SOURCE IN LISTS SHADER_SOURCES)
    get_filename_component(SHADER_NAME
            "${SHADER_SOURCE}"
            NAME_WE
    )

    sirius_compile_shader(
            "${SHADER_NAME}"
            "${SHADER_SOURCE}"
    )
endforeach ()


# ------------------------------------------------------------------------------
# Target
# ------------------------------------------------------------------------------

add_custom_target(shaders
        DEPENDS
        ${SHADER_OUTPUTS}
)

set_target_properties(shaders PROPERTIES
        FOLDER "Sirius"
)
