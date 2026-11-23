cmake_minimum_required(VERSION 3.10)

project(upt C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu11 -O3 -march=native")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -O2")

option(UPT_WITH_GUI "Build the Qt display mode" ON)
option(UPT_INLINE_SPHERES "Inline built-in sphere hits while retaining generic shapes" ON)
option(UPT_LTO "Enable full link-time optimization with Clang" ON)
if(UPT_INLINE_SPHERES)
    add_definitions(-DUPT_INLINE_SPHERES)
endif()
if(UPT_WITH_GUI)
    add_subdirectory(uvideomem)
endif()
add_subdirectory(upt)
