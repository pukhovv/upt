cmake_minimum_required(VERSION 3.16)

add_library(libuvideomem SHARED
    uvideomem.cpp
)

find_package(Qt6 COMPONENTS Widgets REQUIRED)
target_link_libraries(libuvideomem Qt6::Widgets)
target_include_directories(libuvideomem PRIVATE "${PROJECT_SOURCE_DIR}")
