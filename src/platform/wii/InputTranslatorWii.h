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

    // Drains one WPAD channel's IR pointer state into the Mortar::Touch ring
    // buffer (OnPressed/OnMoved/OnReleased), same immediate-push model
    // InputTranslatorSDL::DrainSDLEvent uses for channels 0-7. Called once
    // per WPAD_ScanPads() from the main loop (mainWii.cpp), once per remote
    // channel.
    //   chan: WPAD_CHAN_0..WPAD_CHAN_3 (0-3)
    //   irValid: whether the remote is currently pointed at the sensor bar
    //   x, y: normalized ([0,1], top-left origin, y-down) IR pointer position
    //         in the WPAD_SetVRes-configured fb space -- caller (mainWii.cpp)
    //         normalizes the raw `ir_t` x/y before calling; kept as opaque
    //         floats here so this header stays parseable without
    //         <wiiuse/wpad.h>.
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

    // Port specific: per-channel previous IR-valid edge state, mirroring
    // InputTranslatorSDL's fingerSuspended/fingerActive edge tracking but
    // simpler -- there's no out-of-window concept, just a valid/invalid IR
    // read per remote each WPAD_ScanPads(). prevIRValid[chan] is what
    // DrainWiimoteIR compares against to detect the false->true (press) and
    // true->false (release) edges.
    bool  prevIRValid[MAX_REMOTES];

    // Port specific: press-vs-motion gate, same semantics as
    // InputTranslatorSDL::motionSinceDown -- false on a fresh IR press, set
    // true once a subsequent DrainWiimoteIR call for this channel reports a
    // changed position while still valid. A pointer that presses and is
    // released without moving emits TouchScreen + TouchDown_N + TouchUp_N
    // and NO TouchMove (v1.6.1 tap semantics), matching the SDL translator.
    bool  motionSinceDown[MAX_REMOTES];

    // Port specific: DOWN_EDGE latch for the InputManager hash dispatch,
    // mirroring InputTranslatorSDL::pendingEdge. Set on the press edge,
    // consumed (and cleared) the next DispatchForSimTick.
    bool  pendingEdge[MAX_REMOTES];

    // Transform normalized IR-pointer coords (nx, ny in [0,1], top-left
    // origin, y-down -- same convention WPAD_SetVRes/ir.x,ir.y normalize to)
    // into centred game-ortho coords, mirroring
    // InputTranslatorSDL::TransformTouchNormalized. See InputTranslatorWii.cpp.
    void TransformIRNormalized(float nx, float ny, float& gx, float& gy);
};

#endif // FRUIT_PLATFORM_WII

#endif // FN_PLATFORM_WII_INPUT_TRANSLATOR_WII_H
