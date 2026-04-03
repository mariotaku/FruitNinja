add_library(fn_screens STATIC
    MainScreen.cpp
    DojoScreen.cpp
)
target_include_directories(fn_screens PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(fn_screens PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fn_screens PUBLIC fn_hud fn_engine fn_math fn_platform)
target_link_libraries(fruit-ninja PRIVATE fn_screens)
