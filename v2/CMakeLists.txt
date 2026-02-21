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

	imgui-master/imconfig.h
	imgui-master/imgui.cpp
	imgui-master/imgui.h
	imgui-master/imgui_demo.cpp
	imgui-master/imgui_draw.cpp
	imgui-master/imgui_internal.h
	imgui-master/imgui_tables.cpp
	imgui-master/imgui_widgets.cpp
	imgui-master/imstb_rectpack.h
	imgui-master/imstb_textedit.h
	imgui-master/imstb_truetype.h

	imgui-master/backends/imgui_impl_sdlgpu3.cpp
	imgui-master/backends/imgui_impl_sdl3.cpp
	imgui-master/backends/imgui_impl_sdlgpu3.h
	imgui-master/backends/imgui_impl_sdl3.h
	imgui-master/backends/imgui_impl_sdlgpu3_shaders.h

	imgui-master/misc/cpp/imgui_stdlib.h
	imgui-master/misc/cpp/imgui_stdlib.cpp)

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

target_include_directories(dcss3d PRIVATE imgui-master)
target_include_directories(dcss3d PRIVATE imgui-master/backends)

file(COPY ${PROJECT_SOURCE_DIR}/resources DESTINATION ${CMAKE_BINARY_DIR})
file(COPY ${PROJECT_SOURCE_DIR}/shaders DESTINATION ${CMAKE_BINARY_DIR})

file(COPY ${PROJECT_SOURCE_DIR}/dcss_server.py DESTINATION ${CMAKE_BINARY_DIR})
