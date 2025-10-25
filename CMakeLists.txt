cmake_minimum_required(VERSION 3.30)
project(dcss3d C)

add_executable(dcss3d)

target_sources(dcss3d PRIVATE turn.c render.c net_data.c log.c game.c cJSON.c main.c)

set(CMAKE_BUILD_TYPE Debug)

find_package(SDL3 REQUIRED)
target_link_libraries(dcss3d PRIVATE SDL3::SDL3)

target_include_directories(dcss3d PRIVATE ${PROJECT_SOURCE_DIR}/cglm)

find_library(MATH_LIB m)
if (MATH_LIB)
	target_link_libraries(dcss3d PRIVATE ${MATH_LIB})
endif()

file(COPY ${PROJECT_SOURCE_DIR}/resources DESTINATION ${CMAKE_BINARY_DIR})
# also for compiled shaders
file(COPY ${PROJECT_SOURCE_DIR}/shaders DESTINATION ${CMAKE_BINARY_DIR})
