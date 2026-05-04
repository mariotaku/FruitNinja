// SDL backend for DisplayManager — only the bits that touch SDL_GL_*.
// The portable part lives in DisplayManager.cpp.

#include "render/DisplayManager.h"
#include <SDL.h>

namespace Mortar {

void DisplayManager::SwapBuffers(void* window) {
    SDL_GL_SwapWindow(static_cast<SDL_Window*>(window));
    m_bSwapPending = m_bSwapPending ? 0 : 1;
}

} // namespace Mortar
