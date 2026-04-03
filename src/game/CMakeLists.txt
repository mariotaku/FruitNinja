add_library(fn_game STATIC
    GameTaskState.cpp
    GameInitialise.cpp
    GameInit.cpp
    SplashTask.cpp
    FrontendTask.cpp
)
target_include_directories(fn_game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(fn_game PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/screens
)
target_link_libraries(fn_game PUBLIC fn_engine fn_math fn_hud fn_entities fn_platform fn_screens)
target_link_libraries(fruit-ninja PRIVATE fn_game)
