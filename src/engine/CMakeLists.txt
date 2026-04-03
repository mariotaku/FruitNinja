add_library(fn_engine STATIC
    gl_funcs.cpp
    tex_loader.cpp
    Renderer.cpp
    Mesh.cpp
)
target_include_directories(fn_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fn_engine PUBLIC SDL2::SDL2)
target_link_libraries(fruit-ninja PRIVATE fn_engine)
