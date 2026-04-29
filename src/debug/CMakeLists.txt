target_sources(fruit-ninja-game PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/DebugFlags.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CrashHandler.cpp
)

# CrashHandler links dbghelp.lib (MSVC: via #pragma comment; MinGW: via target).
# Only matters when both _WIN32 and _DEBUG are defined; otherwise the .cpp is
# a no-op stub and the import is harmless dead weight.
if (WIN32 AND NOT MSVC)
    target_link_libraries(fruit-ninja-game PRIVATE dbghelp)
endif()
