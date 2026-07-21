# Block-preload resource loader (task #36) -- see FN_BLOCK_PRELOAD in the
# root CMakeLists.txt. Originally Wii-only; these 3 TUs are ~95%
# platform-agnostic (load via TextureManager/MeshManager/SoundManager), so
# they're compiled for any target that opts in via FN_BLOCK_PRELOAD instead
# of being tied to FRUIT_PLATFORM_WII. Each source is itself internally
# guarded `#ifdef FN_BLOCK_PRELOAD` too, so this block is belt-and-suspenders
# with the option check -- consistent with how src/platform/wii/CMakeLists.txt
# guards its own FRUIT_PLATFORM_WII-only sources.
if (FN_BLOCK_PRELOAD)
    target_sources(fruit-ninja-game PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ResBlock.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/IResourceSource.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/BlockLoader.cpp
    )
endif()
