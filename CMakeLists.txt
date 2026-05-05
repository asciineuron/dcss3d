cmake_minimum_required(VERSION 3.30)
project(dcss3d LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find Python for venv creation
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(PYTHON_EXECUTABLE ${Python3_EXECUTABLE})

# Main application sources as OBJECT library
add_library(dcss3d_obj OBJECT)
target_compile_features(dcss3d_obj PUBLIC cxx_std_23)

target_sources(dcss3d_obj PRIVATE
	src/MessageQueue.hpp
	src/MessageQueue.cpp
	src/Turn.hpp
	src/Turn.cpp
	src/GameMap.hpp
	src/GameMap.cpp
	src/PlayerState.hpp
	src/PlayerState.cpp
	src/Renderer.hpp
	src/Renderer.cpp
	src/ShaderUtil.hpp
	src/ShaderUtil.cpp
	src/Skybox.hpp
	src/Skybox.cpp
	src/main.cpp
	src/imguilayouts.hpp
	src/imguilayouts.cpp
	src/imguilayouts.hpp

	src/WindowManager.hpp
	src/WindowManager.cpp

	src/CharacterSelect.hpp
	src/CharacterSelect.cpp

	src/AudioManager.hpp
	src/AudioManager.cpp

	src/InputModeTracker.hpp
	src/InputModeTracker.cpp

	src/MessageLog.hpp
	src/MessageLog.cpp

	src/debug.hpp
	src/debug.cpp

	imgui/imconfig.h
	imgui/imgui.cpp
	imgui/imgui.h
	imgui/imgui_demo.cpp
	imgui/imgui_draw.cpp
	imgui/imgui_internal.h
	imgui/imgui_tables.cpp
	imgui/imgui_widgets.cpp
	imgui/imstb_rectpack.h
	imgui/imstb_textedit.h
	imgui/imstb_truetype.h

	imgui/backends/imgui_impl_sdlgpu3.cpp
	imgui/backends/imgui_impl_sdl3.cpp
	imgui/backends/imgui_impl_sdlgpu3.h
	imgui/backends/imgui_impl_sdl3.h
	imgui/backends/imgui_impl_sdlgpu3_shaders.h

	imgui/misc/cpp/imgui_stdlib.h
	imgui/misc/cpp/imgui_stdlib.cpp)

find_library(MATH_LIB m)
if (MATH_LIB)
	target_link_libraries(dcss3d_obj PRIVATE ${MATH_LIB})
endif()

find_package(SDL3 REQUIRED)
target_link_libraries(dcss3d_obj PRIVATE SDL3::SDL3)

find_package(SDL3_image REQUIRED)
target_link_libraries(dcss3d_obj PRIVATE SDL3_image::SDL3_image)

find_package(nlohmann_json 3.11.0 REQUIRED)
target_link_libraries(dcss3d_obj PRIVATE nlohmann_json::nlohmann_json)

find_package(glm CONFIG REQUIRED)
target_link_libraries(dcss3d_obj PRIVATE glm::glm)

find_package(spdlog REQUIRED)
target_link_libraries(dcss3d_obj PRIVATE spdlog::spdlog)

# Main executable from object library
add_executable(dcss3d)
target_link_libraries(dcss3d PRIVATE dcss3d_obj)

add_compile_options(-Wall -Wextra -Wpedantic)
set(CMAKE_BUILD_TYPE Debug)

add_compile_options(-fstandalone-debug)

target_include_directories(dcss3d_obj PRIVATE ${PROJECT_SOURCE_DIR}/imgui)
target_include_directories(dcss3d_obj PRIVATE ${PROJECT_SOURCE_DIR}/imgui/backends)
target_include_directories(dcss3d_obj PRIVATE ${PROJECT_SOURCE_DIR}/imgui/misc/cpp)
target_include_directories(dcss3d_obj PRIVATE mdspan/include)

file(COPY ${PROJECT_SOURCE_DIR}/resources DESTINATION ${CMAKE_BINARY_DIR})
file(COPY ${PROJECT_SOURCE_DIR}/shaders DESTINATION ${CMAKE_BINARY_DIR})

# --- Shader compilation via slangc ---
# Only .slang sources are tracked in git; compiled outputs (.hlsl .metal .spv .dxil)
# are generated at build time and placed in build/shaders/.
find_program(SLANGC slangc
    HINTS ${PROJECT_SOURCE_DIR}/slang-2026.5.2-macos-aarch64/bin
    PATHS $ENV{PATH}
)
if(NOT SLANGC)
    message(FATAL_ERROR "slangc not found. Expected in ${PROJECT_SOURCE_DIR}/slang-*/bin/ or in PATH.")
endif()
message(STATUS "Found slangc: ${SLANGC}")

# Detect stage ('vertex' -> 'vert', 'fragment' -> 'frag') by regexing the .slang file.
function(detect_slang_stage SLANG_FILE OUT_STAGE OUT_SUFFIX)
    file(STRINGS ${SLANG_FILE} lines)
    foreach(line ${lines})
        if(line MATCHES "\\[shader\\(\"vertex\"\\)\\]")
            set(${OUT_STAGE} "vertex" PARENT_SCOPE)
            set(${OUT_SUFFIX} "vert" PARENT_SCOPE)
            return()
        elseif(line MATCHES "\\[shader\\(\"fragment\"\\)\\]")
            set(${OUT_STAGE} "fragment" PARENT_SCOPE)
            set(${OUT_SUFFIX} "frag" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    message(FATAL_ERROR "Could not detect shader stage in ${SLANG_FILE}")
endfunction()

set(SLANG_SHADERS
    ${PROJECT_SOURCE_DIR}/shaders/position_color_shifted.slang
    ${PROJECT_SOURCE_DIR}/shaders/position_monster.vert.slang
    ${PROJECT_SOURCE_DIR}/shaders/lit.slang
    ${PROJECT_SOURCE_DIR}/shaders/solid.slang
    ${PROJECT_SOURCE_DIR}/shaders/skybox_vertex.slang
    ${PROJECT_SOURCE_DIR}/shaders/skybox_fragment.slang
)

set(SHADER_TARGETS "")
foreach(SLANG_FILE ${SLANG_SHADERS})
    get_filename_component(SLANG_NAME ${SLANG_FILE} NAME_WE)
    detect_slang_stage(${SLANG_FILE} STAGE SUFFIX)

    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/shaders/${SLANG_NAME}.${SUFFIX}.hlsl
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
        COMMAND ${SLANGC} ${SLANG_FILE}
            -entry main -stage ${STAGE}
            -target hlsl
            -o ${CMAKE_BINARY_DIR}/shaders/${SLANG_NAME}.${SUFFIX}.hlsl
            -profile sm_5_0
        DEPENDS ${SLANG_FILE}
        COMMENT "Compiling ${SLANG_NAME}.slang → HLSL"
    )

    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/shaders/${SLANG_NAME}.${SUFFIX}.metal
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
        COMMAND ${SLANGC} ${SLANG_FILE}
            -entry main -stage ${STAGE}
            -target metal
            -o ${CMAKE_BINARY_DIR}/shaders/${SLANG_NAME}.${SUFFIX}.metal
            -profile sm_5_0
        DEPENDS ${SLANG_FILE}
        COMMENT "Compiling ${SLANG_NAME}.slang → Metal"
    )

    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/shaders/${SLANG_NAME}.${SUFFIX}.spv
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
        COMMAND ${SLANGC} ${SLANG_FILE}
            -entry main -stage ${STAGE}
            -target spirv
            -o ${CMAKE_BINARY_DIR}/shaders/${SLANG_NAME}.${SUFFIX}.spv
            -profile glsl_450
        DEPENDS ${SLANG_FILE}
        COMMENT "Compiling ${SLANG_NAME}.slang → SPIR-V"
    )

    list(APPEND SHADER_TARGETS
        ${CMAKE_BINARY_DIR}/shaders/${SLANG_NAME}.${SUFFIX}.hlsl
        ${CMAKE_BINARY_DIR}/shaders/${SLANG_NAME}.${SUFFIX}.metal
        ${CMAKE_BINARY_DIR}/shaders/${SLANG_NAME}.${SUFFIX}.spv)
endforeach()

add_custom_target(compile_shaders DEPENDS ${SHADER_TARGETS})
add_dependencies(dcss3d compile_shaders)

# Create virtual environment and install dependencies in build directory
set(VENV_DIR "${CMAKE_BINARY_DIR}/venv")
execute_process(
    COMMAND ${PYTHON_EXECUTABLE} -m venv ${VENV_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    OUTPUT_QUIET
    ERROR_QUIET
)

# Install required Python packages
execute_process(
    COMMAND ${VENV_DIR}/bin/pip install --quiet websockets
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    OUTPUT_QUIET
    ERROR_QUIET
)

file(COPY ${PROJECT_SOURCE_DIR}/dcss_server.py DESTINATION ${CMAKE_BINARY_DIR})
file(COPY ${PROJECT_SOURCE_DIR}/dcss_server_wrapper.sh DESTINATION ${CMAKE_BINARY_DIR})

# Catch2 testing framework
find_package(Catch2 3 REQUIRED)

# Test executable - standalone unit tests
add_executable(dcss3d_tests)
target_compile_features(dcss3d_tests PUBLIC cxx_std_23)

target_sources(dcss3d_tests PRIVATE
    tests/main.cpp
    tests/test_game_map.cpp
    tests/test_turn.cpp
    tests/test_input_mode.cpp
    tests/test_player_state.cpp
    tests/test_audio_manager.cpp
    tests/test_window_manager.cpp
    tests/test_character_select.cpp
    src/InputModeTracker.cpp
    src/MessageLog.cpp
    src/WindowManager.cpp
    src/CharacterSelect.cpp
    src/GameMap.cpp
    src/Turn.cpp
    src/MessageQueue.cpp
    src/PlayerState.cpp
    src/AudioManager.cpp
    src/debug.cpp
)

target_link_libraries(dcss3d_tests PRIVATE
    Catch2::Catch2WithMain
    SDL3::SDL3
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    glm::glm
)

target_include_directories(dcss3d_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_include_directories(dcss3d_tests PRIVATE mdspan/include)

# Enable testing
include(CTest)
add_test(NAME dcss3d_tests COMMAND dcss3d_tests)

# Add a convenience target for running tests
add_custom_target(run_tests
    COMMAND ${CMAKE_BINARY_DIR}/dcss3d_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running dcss3d tests"
)
