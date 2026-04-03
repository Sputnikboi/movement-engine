cmake_minimum_required(VERSION 3.24)
project(movement_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Dependencies ---

# Vulkan (must have Vulkan SDK installed)
find_package(Vulkan REQUIRED)

# SDL3 via FetchContent
include(FetchContent)
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.2.8
    GIT_SHALLOW    TRUE
)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL3)

# --- Shader compilation ---

find_program(GLSLC glslc HINTS
    $ENV{VULKAN_SDK}/bin
    $ENV{VULKAN_SDK}/Bin
    /usr/bin
)

if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found. Install the Vulkan SDK.")
endif()

set(SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders)
set(SHADER_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_BINARY_DIR})

set(SHADER_SOURCES
    ${SHADER_SOURCE_DIR}/mesh.vert
    ${SHADER_SOURCE_DIR}/mesh.frag
)

set(SHADER_SPIRV "")
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPIRV_OUTPUT ${SHADER_BINARY_DIR}/${SHADER_NAME}.spv)
    add_custom_command(
        OUTPUT ${SPIRV_OUTPUT}
        COMMAND ${GLSLC} ${SHADER} -o ${SPIRV_OUTPUT}
        DEPENDS ${SHADER}
        COMMENT "Compiling ${SHADER_NAME} -> SPIR-V"
    )
    list(APPEND SHADER_SPIRV ${SPIRV_OUTPUT})
endforeach()

add_custom_target(shaders ALL DEPENDS ${SHADER_SPIRV})

# --- Engine executable ---

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/renderer.cpp
    src/mesh.cpp
    src/collision.cpp
    src/player.cpp
)

add_dependencies(${PROJECT_NAME} shaders)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    SDL3::SDL3-static
)

# Validation layers only in debug
target_compile_definitions(${PROJECT_NAME} PRIVATE
    SHADER_DIR="${SHADER_BINARY_DIR}/"
    $<$<CONFIG:Debug>:ENABLE_VALIDATION_LAYERS>
)
