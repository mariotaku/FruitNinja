add_library(fn_platform STATIC
    InputManager.cpp
    SDLInputTranslator.cpp
)
target_include_directories(fn_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fn_platform PUBLIC SDL2::SDL2 fn_engine fn_math)
target_link_libraries(fruit-ninja PRIVATE fn_platform)
