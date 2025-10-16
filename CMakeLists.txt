cmake_minimum_required(VERSION 3.30)
project(dcss3d C)

add_executable(dcss3d MACOSX_BUNDLE)

target_sources(dcss3d PRIVATE turn.c render.c net_data.c log.c main.c)

find_package(SDL3 REQUIRED)
target_link_libraries(dcss3d PRIVATE SDL3::SDL3)

target_include_directories(dcss3d PRIVATE {CMAKE_CURRENT_SOURCE_DIR}/cglm)
