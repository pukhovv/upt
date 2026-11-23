cmake_minimum_required(VERSION 3.10)

add_executable(upt
    upt.c
    render.c
    shapes.c
    bvh.c
    surfaces.c
    scenes/atelier.c
    scenes/original.c
    util.c
    mesh.c
    scene.c
    transform.c
    texture.c
    light.c
    environment.c
    volume.c
)

target_include_directories(upt PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(upt m)
if(UPT_LTO AND CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(upt PRIVATE -flto=full)
    set_property(TARGET upt APPEND_STRING PROPERTY LINK_FLAGS " -flto=full")
endif()
if(UPT_WITH_GUI)
    target_compile_definitions(upt PRIVATE UPT_WITH_GUI)
    target_link_libraries(upt libuvideomem)
endif()
install(TARGETS upt DESTINATION bin)

# Shared-edge signs must use symmetric products, including on FMA targets.
set_source_files_properties(mesh.c PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
