target_sources(fruit-ninja-game PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/GameSpyScreen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UpsellScreen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/BaseScreen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PauseScreen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ScreenButton.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MainScreen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DojoScreen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SettingsScreen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AboutScreen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GameModeScreen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ShopScreen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GameOverScreen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/BonusScreen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PowerUpShop.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PurchaseInfo.cpp
    # v1.6.1 new-screens subsystem
    ${CMAKE_CURRENT_SOURCE_DIR}/FruitFactPage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FruitFactZenPage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FruitFactLitePage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FruitFactClassicFactPage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FruitFactBigClassicFactPage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FruitFactBonusFactPage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FruitFactRewardsPage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FruitFactLeaderboard.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AchievementsScreen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ChallengeMenuScreen.cpp
)
