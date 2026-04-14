cmake_minimum_required(VERSION 3.30)
project(dcss3d LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(dcss3d)
target_compile_features(dcss3d PUBLIC cxx_std_23)

target_sources(dcss3d PRIVATE
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
	target_link_libraries(dcss3d PRIVATE ${MATH_LIB})
endif()

find_package(SDL3 REQUIRED)
target_link_libraries(dcss3d PRIVATE SDL3::SDL3)

find_package(nlohmann_json 3.11.0 REQUIRED)
target_link_libraries(dcss3d PRIVATE nlohmann_json::nlohmann_json)

find_package(glm CONFIG REQUIRED)
target_link_libraries(dcss3d PRIVATE glm::glm)

find_package(spdlog REQUIRED)
target_link_libraries(dcss3d PRIVATE spdlog::spdlog)

add_compile_options(-Wall -Wextra -Wpedantic)
set(CMAKE_BUILD_TYPE Debug)

add_compile_options(-fstandalone-debug)

target_include_directories(dcss3d PRIVATE ${PROJECT_SOURCE_DIR}/imgui)
target_include_directories(dcss3d PRIVATE ${PROJECT_SOURCE_DIR}/imgui/backends)
target_include_directories(dcss3d PRIVATE ${PROJECT_SOURCE_DIR}/imgui/misc/cpp)

target_include_directories(dcss3d PRIVATE mdspan/include)

file(COPY ${PROJECT_SOURCE_DIR}/resources DESTINATION ${CMAKE_BINARY_DIR})
file(COPY ${PROJECT_SOURCE_DIR}/shaders DESTINATION ${CMAKE_BINARY_DIR})

file(COPY ${PROJECT_SOURCE_DIR}/dcss_server.py DESTINATION ${CMAKE_BINARY_DIR})
file(COPY ${PROJECT_SOURCE_DIR}/dcss_server_wrapper.sh DESTINATION ${CMAKE_BINARY_DIR})
