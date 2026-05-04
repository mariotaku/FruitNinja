# Object-only cross build for ARM Thumb-2 verification.
#
# This CMakeLists pulls in selected .cpp files from the port and compiles
# each into a .o whose per-symbol assembly is then diffed against the
# original binary via ../asm-verify.py.
#
# Driven by ../run.sh; not intended for direct invocation. If you need to,
# from the project root:
#   cmake -S tools/asm-verify/cross-build -B ~/fn-build -G "Unix Makefiles" \
#         -DCMAKE_TOOLCHAIN_FILE=$(pwd)/tools/asm-verify/toolchain.cmake
#   cmake --build ~/fn-build -j$(nproc)

cmake_minimum_required(VERSION 3.20)
project(fnverify LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_EXTENSIONS OFF)

# This file lives at tools/asm-verify/cross-build/CMakeLists.txt; project
# root is three parents up.
set(_PROJECT_ROOT "${CMAKE_SOURCE_DIR}/../../..")
set(_ASM_VERIFY   "${CMAKE_SOURCE_DIR}/..")

# Cross-buildable TUs. Expanded R4 W5 to cover the full portable-cpp set
# (everything that compiles clean under symbol-diff -- 119 TUs as of
# commit 4102d26). Platform-glue (*SDL.cpp / *Posix.cpp / *Win32.cpp /
# src/platform/sdl/ / src/main.cpp) is excluded by convention.
#
# When a TU fails to compile under arm-none-eabi, comment it out with a
# // FIXME marker citing the blocker, NOT delete it -- the goal is to
# track every portable TU's verification state explicitly.
set(VERIFY_SOURCES
    "${_ASM_VERIFY}/cross-build/demos/gameover_demo.cpp"

    # Top-level
    "${_PROJECT_ROOT}/src/Game.cpp"

    # Engine: asset / audio / collision / compression / core / input /
    #         math / network / particle / render / util
    "${_PROJECT_ROOT}/src/engine/MenuBackground.cpp"
    "${_PROJECT_ROOT}/src/engine/Renderer.cpp"
    "${_PROJECT_ROOT}/src/engine/asset/AnimationState.cpp"
    "${_PROJECT_ROOT}/src/engine/asset/File.cpp"
    "${_PROJECT_ROOT}/src/engine/asset/FileManager.cpp"
    "${_PROJECT_ROOT}/src/engine/asset/IFile.cpp"
    "${_PROJECT_ROOT}/src/engine/asset/IFileSystem.cpp"
    "${_PROJECT_ROOT}/src/engine/asset/IFile_Direct.cpp"
    "${_PROJECT_ROOT}/src/engine/asset/Mesh.cpp"
    "${_PROJECT_ROOT}/src/engine/asset/MeshDraw.cpp"
    "${_PROJECT_ROOT}/src/engine/asset/MeshManager.cpp"
    "${_PROJECT_ROOT}/src/engine/asset/ResourceLoader.cpp"
    "${_PROJECT_ROOT}/src/engine/asset/Skeleton.cpp"
    "${_PROJECT_ROOT}/src/engine/asset/Texture.cpp"
    "${_PROJECT_ROOT}/src/engine/asset/TextureManager.cpp"
    "${_PROJECT_ROOT}/src/engine/audio/GameSound.cpp"
    "${_PROJECT_ROOT}/src/engine/audio/MortarSound.cpp"
    "${_PROJECT_ROOT}/src/engine/collision/Col.cpp"
    "${_PROJECT_ROOT}/src/engine/collision/ColAABB.cpp"
    "${_PROJECT_ROOT}/src/engine/collision/ColLine.cpp"
    "${_PROJECT_ROOT}/src/engine/collision/ColSphere.cpp"
    "${_PROJECT_ROOT}/src/engine/compression/LZ8.cpp"
    "${_PROJECT_ROOT}/src/engine/core/MortarGame.cpp"
    "${_PROJECT_ROOT}/src/engine/core/SystemManager.cpp"
    "${_PROJECT_ROOT}/src/engine/input/InputDeviceBada.cpp"
    "${_PROJECT_ROOT}/src/engine/input/InputManager.cpp"
    "${_PROJECT_ROOT}/src/engine/input/Touch.cpp"
    "${_PROJECT_ROOT}/src/engine/math/Math.cpp"
    "${_PROJECT_ROOT}/src/engine/math/MathUtil.cpp"
    "${_PROJECT_ROOT}/src/engine/math/Random.cpp"
    "${_PROJECT_ROOT}/src/engine/network/NetworkManager.cpp"
    "${_PROJECT_ROOT}/src/engine/particle/PSPParticleManager.cpp"
    "${_PROJECT_ROOT}/src/engine/render/BakedString.cpp"
    "${_PROJECT_ROOT}/src/engine/render/DisplayManager.cpp"
    "${_PROJECT_ROOT}/src/engine/render/Font.cpp"
    "${_PROJECT_ROOT}/src/engine/render/MatrixManager.cpp"
    "${_PROJECT_ROOT}/src/engine/render/MortarCamera.cpp"
    "${_PROJECT_ROOT}/src/engine/render/Renderer.cpp"
    "${_PROJECT_ROOT}/src/engine/render/Utf8StringIterator.cpp"
    "${_PROJECT_ROOT}/src/engine/util/AsciiString.cpp"
    "${_PROJECT_ROOT}/src/engine/util/Localisation.cpp"
    "${_PROJECT_ROOT}/src/engine/util/StringHash.cpp"

    # Entities
    "${_PROJECT_ROOT}/src/entities/ActorManager.cpp"
    "${_PROJECT_ROOT}/src/entities/Bomb.cpp"
    "${_PROJECT_ROOT}/src/entities/BombBlast.cpp"
    "${_PROJECT_ROOT}/src/entities/BombFlash.cpp"
    "${_PROJECT_ROOT}/src/entities/Coin.cpp"
    "${_PROJECT_ROOT}/src/entities/Entity.cpp"
    "${_PROJECT_ROOT}/src/entities/EntityFactory.cpp"
    "${_PROJECT_ROOT}/src/entities/Fruit.cpp"
    "${_PROJECT_ROOT}/src/entities/FruitInfo.cpp"
    "${_PROJECT_ROOT}/src/entities/SlashEntity.cpp"
    "${_PROJECT_ROOT}/src/entities/SplatEntity.cpp"

    # Game
    "${_PROJECT_ROOT}/src/game/AchievementManager.cpp"
    "${_PROJECT_ROOT}/src/game/BombHit.cpp"
    "${_PROJECT_ROOT}/src/game/Bonus.cpp"
    "${_PROJECT_ROOT}/src/game/BonusManager.cpp"
    "${_PROJECT_ROOT}/src/game/FrontendTask.cpp"
    "${_PROJECT_ROOT}/src/game/FruitCamera.cpp"
    "${_PROJECT_ROOT}/src/game/FruitSaveData.cpp"
    "${_PROJECT_ROOT}/src/game/GameInit.cpp"
    "${_PROJECT_ROOT}/src/game/GameInitialise.cpp"
    "${_PROJECT_ROOT}/src/game/GameOver.cpp"
    "${_PROJECT_ROOT}/src/game/GameTaskInput.cpp"
    "${_PROJECT_ROOT}/src/game/GameTaskState.cpp"
    "${_PROJECT_ROOT}/src/game/ItemInfo.cpp"
    "${_PROJECT_ROOT}/src/game/ItemManager.cpp"
    "${_PROJECT_ROOT}/src/game/PowerUp.cpp"
    "${_PROJECT_ROOT}/src/game/PowerUpManager.cpp"
    "${_PROJECT_ROOT}/src/game/PreloadSounds.cpp"
    "${_PROJECT_ROOT}/src/game/ScoreDelegate.cpp"
    "${_PROJECT_ROOT}/src/game/ScoreModifier.cpp"
    "${_PROJECT_ROOT}/src/game/ScoreState.cpp"
    "${_PROJECT_ROOT}/src/game/ScreenEffect.cpp"
    "${_PROJECT_ROOT}/src/game/SetupGameWork.cpp"
    "${_PROJECT_ROOT}/src/game/SlashModifier.cpp"
    "${_PROJECT_ROOT}/src/game/SpecificOrder.cpp"
    "${_PROJECT_ROOT}/src/game/SplashTask.cpp"
    "${_PROJECT_ROOT}/src/game/StartupEffects.cpp"
    "${_PROJECT_ROOT}/src/game/TimeModifier.cpp"
    "${_PROJECT_ROOT}/src/game/UpdateMusic.cpp"
    "${_PROJECT_ROOT}/src/game/WaveManager.cpp"
    "${_PROJECT_ROOT}/src/game/WaveModifier.cpp"
    "${_PROJECT_ROOT}/src/game/WaveSyncPacket.cpp"

    # HUD
    "${_PROJECT_ROOT}/src/hud/CheckBox.cpp"
    "${_PROJECT_ROOT}/src/hud/CoinCounter.cpp"
    "${_PROJECT_ROOT}/src/hud/ComboControl.cpp"
    "${_PROJECT_ROOT}/src/hud/FruitFactControl.cpp"
    "${_PROJECT_ROOT}/src/hud/HUD.cpp"
    "${_PROJECT_ROOT}/src/hud/HUDControl3d.cpp"
    "${_PROJECT_ROOT}/src/hud/MenuButton.cpp"
    "${_PROJECT_ROOT}/src/hud/MissControl.cpp"
    "${_PROJECT_ROOT}/src/hud/NotificationControl.cpp"
    "${_PROJECT_ROOT}/src/hud/ProgressionTimerControl.cpp"
    "${_PROJECT_ROOT}/src/hud/ScoreControl.cpp"
    "${_PROJECT_ROOT}/src/hud/ScreenFadeControl.cpp"
    "${_PROJECT_ROOT}/src/hud/ScrollingMenu.cpp"
    "${_PROJECT_ROOT}/src/hud/ScrollingMenuItem.cpp"
    "${_PROJECT_ROOT}/src/hud/ShopListItem.cpp"
    "${_PROJECT_ROOT}/src/hud/SliceEffect.cpp"
    "${_PROJECT_ROOT}/src/hud/SliderControl.cpp"
    "${_PROJECT_ROOT}/src/hud/SpeedControl.cpp"
    "${_PROJECT_ROOT}/src/hud/TimeControl.cpp"
    "${_PROJECT_ROOT}/src/hud/TutorialControl.cpp"
    "${_PROJECT_ROOT}/src/hud/VerticalScroller.cpp"

    # Screens
    # FIXME: AboutScreen / BaseScreen / DojoScreen / GameModeScreen /
    #        MainScreen / ShopScreen use std::bind for Mortar::Delegate1
    #        wiring; GCC 4.4 doesn't accept the resulting std::_Bind
    #        expression for the implicit Delegate conversion. Restore
    #        when the bind sites are converted to QCallee or named
    #        member-fn pointers.
    # "${_PROJECT_ROOT}/src/screens/AboutScreen.cpp"
    # "${_PROJECT_ROOT}/src/screens/BaseScreen.cpp"
    "${_PROJECT_ROOT}/src/screens/BonusScreen.cpp"
    # "${_PROJECT_ROOT}/src/screens/DojoScreen.cpp"
    # "${_PROJECT_ROOT}/src/screens/GameModeScreen.cpp"
    "${_PROJECT_ROOT}/src/screens/GameOverScreen.cpp"
    # "${_PROJECT_ROOT}/src/screens/MainScreen.cpp"
    "${_PROJECT_ROOT}/src/screens/PauseScreen.cpp"
    "${_PROJECT_ROOT}/src/screens/PowerUpShop.cpp"
    "${_PROJECT_ROOT}/src/screens/ScreenButton.cpp"
    # "${_PROJECT_ROOT}/src/screens/ShopScreen.cpp"

    # Debug + platform-portable
    "${_PROJECT_ROOT}/src/debug/CrashHandler.cpp"
    "${_PROJECT_ROOT}/src/debug/DebugFlags.cpp"
    "${_PROJECT_ROOT}/src/platform/InputManager.cpp"
)

add_library(fnverify OBJECT ${VERIFY_SOURCES})

target_include_directories(fnverify PRIVATE
    # Stubbed dependencies that aren't compatible with arm-none-eabi.
    "${_ASM_VERIFY}/cross-headers"
    # Real port headers, in include order matching the host build.
    "${_PROJECT_ROOT}/src"
    "${_PROJECT_ROOT}/src/engine"
    # Real third-party headers reachable from the host build dir.
    "${_PROJECT_ROOT}/build/_deps/tinyxml2-src"
)

set_target_properties(fnverify PROPERTIES LINKER_LANGUAGE CXX)

# Force recompilation of key TUs on every cmake --build run.
# rsync inside verify.sh uses --archive (preserves source mtime) which can
# cause stale .o files to appear newer than their source headers on Docker
# named volumes. Adding a custom_command that always runs and touches each
# .cpp to update its mtime in the staging tree ensures cmake recompiles them.
#
# Implementation: custom target with ALL that runs before the fnverify build.
add_custom_target(force_struct_recompile ALL
    COMMAND "${CMAKE_COMMAND}" -E touch
        "${_PROJECT_ROOT}/src/game/PowerUp.cpp"
        "${_PROJECT_ROOT}/src/game/WaveManager.cpp"
        "${_PROJECT_ROOT}/src/game/ScoreModifier.cpp"
    COMMENT "Touching struct-dep TUs to ensure recompile after header edits"
)
add_dependencies(fnverify force_struct_recompile)

message(STATUS "fnverify OBJECTs will be at: ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/fnverify.dir/...")
message(STATUS "CMakeLists.txt re-read: source timestamp tracking active")
