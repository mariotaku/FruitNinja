add_library(fn_hud STATIC
    MenuButton.cpp
)
target_include_directories(fn_hud PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(fn_hud PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(fn_hud PUBLIC fn_engine fn_math)
target_link_libraries(fruit-ninja PRIVATE fn_hud)
