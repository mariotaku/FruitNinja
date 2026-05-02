// Minimal SDL2 stub for cross-build (.o-only) verification.
// We compile the port's host code through the bada toolchain to produce
// per-symbol assembly, then diff against the original ARM binary. Linking
// is never invoked, so empty stubs are sufficient.
#ifndef FN_VERIFY_SDL_STUB_H
#define FN_VERIFY_SDL_STUB_H

#include <cstdint>
#include <cstddef>

typedef uint8_t Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef int32_t Sint32;
typedef int64_t Sint64;
typedef uint64_t Uint64;
typedef Uint8 SDL_bool;
typedef Sint64 SDL_FingerID;
typedef Sint64 SDL_TouchID;
typedef Uint32 SDL_AudioDeviceID;

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Surface;
typedef struct SDL_Window  SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;

typedef union {
    int dummy;
} SDL_Event;

typedef struct { int x, y, w, h; } SDL_Rect;
typedef struct { float x, y; } SDL_FPoint;
typedef void* SDL_GLContext;

inline Uint32 SDL_GetTicks() { return 0; }
inline Uint64 SDL_GetPerformanceCounter() { return 0; }
inline Uint64 SDL_GetPerformanceFrequency() { return 1; }
inline void   SDL_Delay(Uint32) {}
inline int    SDL_PollEvent(SDL_Event*) { return 0; }

#endif
