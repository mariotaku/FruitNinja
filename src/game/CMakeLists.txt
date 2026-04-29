target_sources(fruit-ninja-game PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/GameTaskState.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GameInitialise.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GameInit.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SplashTask.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FrontendTask.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FruitCamera.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/BombHit.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/WaveManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FruitSaveData.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SlashModifier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ItemManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ItemInfo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UpdateMusic.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GameOver.cpp
)
