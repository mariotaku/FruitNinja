// Minimal SDL2 stub for cross-build (.o-only) verification.
// We compile the port's host code through the bada toolchain to produce
// per-symbol assembly, then diff against the original ARM binary. Linking
// is never invoked, so empty stubs are sufficient.
#ifndef FN_VERIFY_SDL_STUB_H
#define FN_VERIFY_SDL_STUB_H

#include <cstdint>
#include <cstddef>
#include <cctype>

typedef uint8_t  Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef int32_t  Sint32;
typedef int64_t  Sint64;
typedef uint64_t Uint64;
typedef Uint8    SDL_bool;
typedef Sint64   SDL_FingerID;
typedef Sint64   SDL_TouchID;
typedef Uint32   SDL_AudioDeviceID;

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Surface;
typedef struct SDL_Window   SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;

// SDL event type constants
#define SDL_QUIT             0x100
#define SDL_KEYDOWN          0x300
#define SDL_MOUSEMOTION      0x400
#define SDL_MOUSEBUTTONDOWN  0x401
#define SDL_MOUSEBUTTONUP    0x402
#define SDL_FINGERDOWN       0x700
#define SDL_FINGERUP         0x701
#define SDL_FINGERMOTION     0x702

// SDL mouse button constants
#define SDL_BUTTON_LEFT      1
#define SDL_BUTTON_RIGHT     3

// Event structs (must be declared before SDL_Event union)
typedef struct {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint8  button;
    Uint8  state;
    Uint8  clicks;
    Uint8  padding1;
    Sint32 x;
    Sint32 y;
} SDL_MouseButtonEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint32 which;
    Uint32 state;
    Sint32 x;
    Sint32 y;
    Sint32 xrel;
    Sint32 yrel;
} SDL_MouseMotionEvent;

typedef struct {
    Uint32 type;
    Uint32 scancode;
    Uint32 sym;
    Uint16 mod;
} SDL_Keysym;

typedef struct {
    Uint32    type;
    Uint32    timestamp;
    Uint32    windowID;
    Uint8     state;
    Uint8     repeat;
    Uint8     padding2;
    Uint8     padding3;
    SDL_Keysym keysym;
} SDL_KeyboardEvent;

typedef struct {
    Uint32        type;
    Uint32        timestamp;
    SDL_TouchID   touchId;
    SDL_FingerID  fingerId;
    float x, y, dx, dy, pressure;
} SDL_TouchFingerEvent;

typedef union {
    Uint32               type;
    SDL_MouseButtonEvent button;
    SDL_MouseMotionEvent motion;
    SDL_TouchFingerEvent tfinger;
    SDL_KeyboardEvent    key;
    Uint8                padding[56];
} SDL_Event;

typedef struct { int x, y, w, h; } SDL_Rect;
typedef struct { float x, y; } SDL_FPoint;
typedef void* SDL_GLContext;

inline Uint32 SDL_GetTicks()                { return 0; }
inline Uint64 SDL_GetPerformanceCounter()   { return 0; }
inline Uint64 SDL_GetPerformanceFrequency() { return 1; }
inline void   SDL_Delay(Uint32)             {}
inline int    SDL_PollEvent(SDL_Event*)     { return 0; }

// SDL init flags
#define SDL_INIT_VIDEO  0x00000020U
#define SDL_INIT_AUDIO  0x00000010U
#define SDL_INIT_EVENTS 0x00004000U

// SDL_Window flags
#define SDL_WINDOW_OPENGL  0x00000002U
#define SDL_WINDOW_SHOWN   0x00000004U

// SDL_GL attributes
#define SDL_GL_RED_SIZE                  0
#define SDL_GL_GREEN_SIZE                1
#define SDL_GL_BLUE_SIZE                 2
#define SDL_GL_ALPHA_SIZE                3
#define SDL_GL_DEPTH_SIZE                6
#define SDL_GL_DOUBLEBUFFER              5
#define SDL_GL_CONTEXT_MAJOR_VERSION    17
#define SDL_GL_CONTEXT_MINOR_VERSION    18
#define SDL_GL_CONTEXT_PROFILE_MASK     21
#define SDL_GL_CONTEXT_PROFILE_ES       0x0004
#define SDL_GL_CONTEXT_PROFILE_COMPATIBILITY 0x0002

// Audio
typedef Uint16 SDL_AudioFormat;
#define AUDIO_S16SYS  0x8010
#define AUDIO_S16LSB  0x8010
typedef void (*SDL_AudioCallback)(void*, Uint8*, int);
typedef struct {
    int freq; SDL_AudioFormat format; Uint8 channels; Uint8 silence;
    Uint16 samples; Uint32 size; SDL_AudioCallback callback; void* userdata;
} SDL_AudioSpec;
inline SDL_AudioDeviceID SDL_OpenAudioDevice(const char*, int, const SDL_AudioSpec*, SDL_AudioSpec*, int) { return 0; }
inline void SDL_CloseAudioDevice(SDL_AudioDeviceID)    {}
inline void SDL_PauseAudioDevice(SDL_AudioDeviceID, int) {}
inline void SDL_LockAudioDevice(SDL_AudioDeviceID)     {}
inline void SDL_UnlockAudioDevice(SDL_AudioDeviceID)   {}

// GL/window
inline void*         SDL_GL_GetProcAddress(const char*)              { return 0; }
inline int           SDL_GL_LoadLibrary(const char*)                 { return 0; }
inline void          SDL_GL_UnloadLibrary()                          {}
inline SDL_Window*   SDL_CreateWindow(const char*, int, int, int, int, Uint32) { return 0; }
inline void          SDL_DestroyWindow(SDL_Window*)                  {}
inline SDL_GLContext SDL_GL_CreateContext(SDL_Window*)               { return 0; }
inline void          SDL_GL_DeleteContext(SDL_GLContext)             {}
inline void          SDL_GL_SwapWindow(SDL_Window*)                  {}
inline int           SDL_GL_SetAttribute(int, int)                   { return 0; }
inline int           SDL_GL_GetAttribute(int, int*)                  { return 0; }
inline int           SDL_GL_SetSwapInterval(int)                     { return 0; }
inline int           SDL_Init(Uint32)                                { return 0; }
inline void          SDL_Quit()                                      {}
inline const char*   SDL_GetError()                                  { return ""; }
inline void          SDL_GetWindowSize(SDL_Window*, int* w, int* h)  { if (w) *w = 960; if (h) *h = 640; }

// Window position constants
#define SDL_WINDOWPOS_CENTERED_MASK 0x2FFF0000u
#define SDL_WINDOWPOS_CENTERED      (SDL_WINDOWPOS_CENTERED_MASK | 0)

// Touch/finger
typedef struct { SDL_TouchID touchId; SDL_FingerID fingerId; float x, y, pressure; } SDL_Finger;
inline int         SDL_GetNumTouchFingers(SDL_TouchID)     { return 0; }
inline SDL_Finger* SDL_GetTouchFinger(SDL_TouchID, int)    { return 0; }
inline int         SDL_GetNumTouchDevices(void)            { return 0; }
inline SDL_TouchID SDL_GetTouchDevice(int)                 { return 0; }

// Misc
inline Uint32  SDL_WasInit(Uint32)      { return 0; }
inline int     SDL_InitSubSystem(Uint32){ return 0; }
inline void    SDL_QuitSubSystem(Uint32){}
inline int     SDL_SetMainReady(void)   { return 0; }
inline char*   SDL_GetBasePath(void)    { return 0; }
inline void    SDL_free(void*)          {}
inline char*   SDL_strdup(const char*)  { return 0; }

// SDL_stdinc.h helpers (SDL_strcasecmp, SDL_memset)
inline int SDL_strcasecmp(const char* a, const char* b) {
    while (*a && *b) {
        int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (d) return d;
        ++a; ++b;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}
inline void* SDL_memset(void* dst, int c, size_t n) {
    return __builtin_memset(dst, c, n);
}

// SDL_RWops (minimal — object-only compile)
struct SDL_RWops { int dummy; };
inline SDL_RWops* SDL_RWFromFile(const char*, const char*)  { return 0; }
inline size_t     SDL_RWread(SDL_RWops*, void*, size_t, size_t) { return 0; }
inline int        SDL_RWclose(SDL_RWops*)                   { return 0; }

#endif
