add_library(fn_hud STATIC
    HUDControl3d.cpp
    MenuButton.cpp
)
target_include_directories(fn_hud PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(fn_hud PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fn_hud PUBLIC fn_engine fn_math fn_entities)
target_link_libraries(fruit-ninja PRIVATE fn_hud)
