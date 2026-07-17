#ifndef FN_PLATFORM_WII_INPUT_TRANSLATOR_WII_H
#define FN_PLATFORM_WII_INPUT_TRANSLATOR_WII_H

//
// InputTranslatorWii -- SCAFFOLDING ONLY. Converts WPAD Wiimote IR-pointer
// data to Mortar::Touch finger channels, mirroring what InputTranslatorSDL
// does for SDL touch/mouse events (src/platform/InputTranslatorSDL.h).
//
// Mapping: up to 4 Wiimotes (WPAD_CHAN_0..WPAD_CHAN_3) each report one IR
// pointer -- the on-screen dot from pointing the remote at the sensor bar.
// Each remote's IR pointer maps to ONE FIXED finger channel (remote N ->
// channel N, no dynamic allocation needed since the Wii has a hard 4-remote
// ceiling, unlike SDL's dynamic per-finger-ID channel search). This is the
// same "multiplayer IS multi-finger slicing" model the SDL backend uses for
// up to 8 simultaneous touches (see CLAUDE.md "Preserve simultaneous
// multi-finger slicing... this IS the binary's multiplayer") -- 4 Wiimotes
// giving 4 simultaneous blades is the direct Wii analogue, not a same-screen
// split-screen mode (v1.6.1 has none, per the #158 input-path audit).
//
// A Wiimote pointed away from the sensor bar reports IR-invalid (no dot);
// that is treated as a released finger for its channel, same as an SDL
// finger-up.
//
// Only compiled when FRUIT_PLATFORM_WII is set (see
// src/platform/wii/CMakeLists.txt). No wiiuse/gccore headers are included
// here (kept opaque) so this header stays parseable outside a devkitPPC
// toolchain for review purposes; the .cpp does the real includes behind the
// same guard.

#ifdef FRUIT_PLATFORM_WII

#include <cstdint>

class InputTranslatorWii {
public:
    // Fixed channel-per-remote mapping -- no dynamic ID search needed.
    static const int MAX_REMOTES = 4;

    InputTranslatorWii();

    // Initialize action hashes (call once after StringHash is available),
    // mirroring InputTranslatorSDL::Init().
    void Init();

    // TODO(wii): drain one WPAD channel's IR pointer state into the
    // Mortar::Touch ring buffer (OnPressed/OnMoved/OnReleased), same
    // immediate-push model InputTranslatorSDL::DrainSDLEvent uses for
    // channels 0-7. Called once per WPAD_ScanPads() from the main loop
    // (mainWii.cpp), once per remote channel.
    //   chan: WPAD_CHAN_0..WPAD_CHAN_3 (0-3)
    //   irValid: whether the remote is currently pointed at the sensor bar
    //   x, y: normalized screen-space IR pointer position (opaque float pair;
    //         real signature will take a `struct ir_t` or similar from
    //         <wiiuse/wpad.h> once that header is wired in)
    void DrainWiimoteIR(int chan, bool irValid, float x, float y);

    // Drain the Mortar::Touch ring buffer and dispatch InputManager hash
    // events for one sim tick -- same role as
    // InputTranslatorSDL::DispatchForSimTick().
    void DispatchForSimTick();

    // Synthesize a release for every held remote channel. Called on suspend
    // (Wii Home-menu return) -- analogous to
    // InputTranslatorSDL::ReleaseAllFingers() on SDL focus-loss.
    void ReleaseAllFingers();

private:
    // Pre-computed action hashes for the 4 remote channels (subset of the
    // 16-channel space InputManager already knows about -- reuses channels
    // 0-3, the same channels SDL touch fingers 0-3 would occupy).
    uint32_t hashTouchDown[MAX_REMOTES];
    uint32_t hashTouchMoveX[MAX_REMOTES];
    uint32_t hashTouchMoveY[MAX_REMOTES];
    uint32_t hashTouchUp[MAX_REMOTES];
    uint32_t hashTouchScreen;

    bool  channelActive[MAX_REMOTES];
    float channelX[MAX_REMOTES];
    float channelY[MAX_REMOTES];
    bool  prevActive[MAX_REMOTES];

    // TODO(wii): normalized IR-pointer -> game coords (centred ortho),
    // mirroring InputTranslatorSDL::TransformTouchNormalized.
    void TransformIRNormalized(float nx, float ny, float& gx, float& gy);
};

#endif // FRUIT_PLATFORM_WII

#endif // FN_PLATFORM_WII_INPUT_TRANSLATOR_WII_H
