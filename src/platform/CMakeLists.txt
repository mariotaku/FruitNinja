target_sources(fruit-ninja-game PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/InputManager.cpp
)
if(NOT FN_ASM_VERIFY)
    target_sources(fruit-ninja-game PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/InputTranslatorSDL.cpp
    )
endif()
