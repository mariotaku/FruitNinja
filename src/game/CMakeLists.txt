target_sources(fruit-ninja-game PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/EntityTypes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GameWork.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AchievementManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GameTaskState.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GameInitialise.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GameInit.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GameTaskInput.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SplashTask.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FrontendTask.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FruitCamera.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/BombHit.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GlobalProbabilityOveride.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/WaveManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FruitSaveData.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SettingsSave.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GameModifier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SlashModifier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ScoreDelegate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ScoreModifier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TimeModifier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/WaveModifier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ComboModifier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SpawnModifier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TimeSinkModifier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ExplodyFruitModifier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ScreenEffect.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FNHighscore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PowerUp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PowerUpManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ItemManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ItemInfo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UpdateMusic.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GameOver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/StartupEffects.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SetupGameWork.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Bonus.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/BonusManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PreloadSounds.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PreloadRings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PreloadFontsTTF.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ScoreState.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SpecificOrder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/WaveSyncPacket.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FruitSlicedPacket.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CoinChanceinator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DRMManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ProfanityFilter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PointsPacket.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/StartGamePacket.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PlayerDisconnectGamePacket.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PacketFactory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FruitNinjaNewsControl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GameMode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ItemParseUtil.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Leaderboard.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Licensing.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Social.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DeviceQuery.cpp
)

# Platform twin for Licensing.h's OpenBrowser: LicensingWii.cpp on Wii (no
# browser app-control to hand off to), LicensingSDL.cpp everywhere else
# (SDL_OpenURL) -- mirrors the GameSDL.cpp/GameWii.cpp swap in the root
# CMakeLists.txt.
fn_add_platform_source(_fn_licensing_platform_src
    ${CMAKE_CURRENT_SOURCE_DIR}/LicensingWii.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LicensingSDL.cpp)
target_sources(fruit-ninja-game PRIVATE ${_fn_licensing_platform_src})
