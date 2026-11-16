cmake_minimum_required(VERSION 3.10)

add_executable(upt
    upt.c
    shapes.c
    bvh.c
    surfaces.c
    atelier.c
    util.c
)

target_include_directories(upt PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(upt m)
if(UPT_WITH_GUI)
    target_compile_definitions(upt PRIVATE UPT_WITH_GUI)
    target_link_libraries(upt libuvideomem)
endif()
install(TARGETS upt DESTINATION bin)
