cmake_minimum_required(VERSION 3.30)
project(dcss3d LANGUAGES CXX)

add_executable(dcss3d)
target_compile_features(dcss3d PUBLIC cxx_std_23)
target_sources(dcss3d PRIVATE 
	src/MessageQueue.hpp
	src/MessageQueue.cpp
	src/Turn.hpp 
	src/GameMap.hpp
	src/GameMap.cpp
	src/PlayerState.hpp
	src/PlayerState.cpp
	src/Renderer.hpp
	src/Renderer.cpp
	src/main.cpp)

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

file(COPY ${PROJECT_SOURCE_DIR}/resources DESTINATION ${CMAKE_BINARY_DIR})
file(COPY ${PROJECT_SOURCE_DIR}/shaders DESTINATION ${CMAKE_BINARY_DIR})

add_compile_options(-Wall -Wextra -Wpedantic)
set(CMAKE_BUILD_TYPE Debug)
