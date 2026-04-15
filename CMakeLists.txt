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
	src/main.cpp
	src/imguilayouts.hpp
	src/imguilayouts.cpp

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
    tests/test_player_state.cpp
)

target_link_libraries(dcss3d_tests PRIVATE
    Catch2::Catch2WithMain
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
