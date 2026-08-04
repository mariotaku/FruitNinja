#ifndef FN_PLATFORM_WII_INPUT_TRANSLATOR_WII_H
#define FN_PLATFORM_WII_INPUT_TRANSLATOR_WII_H

//
// InputTranslatorWii -- converts WPAD Wiimote IR-pointer + A-button state to
// the same input model the SDL backend's MOUSE produces
// (src/platform/InputTranslatorSDL.{h,cpp} is the reference implementation).
//
// LIVE SDL mouse path (confirmed against the vcpkg-vendored SDL 2.32.10
// source, src/events/SDL_mouse.c): mainSDL.cpp sets
// SDL_HINT_MOUSE_TOUCH_EVENTS=1, which makes SDL_PrivateSendMouseButton /
// SDL_PrivateSendMouseMotion ALSO synthesize SDL_FINGER* with
// fingerId=SDL_TOUCH_MOUSEID (SDL_SendTouch/SDL_SendTouchMotion,
// SDL_MOUSE_TOUCHID) -- but only while a button is/was held
// (track_mouse_down). The RAW SDL_MOUSEBUTTON*/SDL_MOUSEMOTION events are
// POSTED UNCONDITIONALLY alongside the synthesized ones (SDL never
// suppresses its own raw stream; SDL_HINT_TOUCH_MOUSE_EVENTS=0 only discards
// synthetic-mouse-from-touch, not real mouse events) -- both streams fire
// concurrently. InputTranslatorSDL::DrainSDLEvent explicitly suppresses the
// synthesized SDL_TOUCH_MOUSEID finger (FINGERDOWN/MOTION/UP) whenever
// FN::g_MotionMode is ON, so in that mode ONLY the raw mouse path
// (MOUSE_CHANNEL / POINTER_FINGER_CHANNEL 15) is
// live for the mouse: a button press LIFTS the blade, release RE-PRESSES it,
// and hover-drag cuts are gated by SlashEntity's speed threshold. The
// synthesized channel-0-style finger only drives anything when motion mode
// is OFF, where it is the ordinary Mortar::Touch-slot press-to-slice path
// (ungated, same as any real touch finger).
//
// The SDL mouse is therefore DE FACTO two channels depending on mode, and
// each Wiimote mirrors both roles (always active; which one actually drives
// a slice for a given gesture depends on g_MotionMode, mirroring SDL):
//
//  Role 1 -- "press finger" (SDL: the synthesized/real mouse-finger, live
//    when motion mode is OFF). Feeds the Mortar::Touch ring/slots -- the
//    ONLY input path there is, for widgets (free IsTouchDown / TouchInRegion
//    + game_work.m_FingerSpawnPos) and for the blade alike (the binary's
//    Touch::SendIndividualTouchCallbacks poll turns each slot into Touch<n>
//    actions), so this role is what makes menu/widget clicks work in EITHER
//    mode.
//    Wii mirror: remote N presses channel N (0-3) with extId N+1 while A is
//    held AND the IR read is valid; IR movement while held is the
//    FINGERMOTION. Provides, in both modes: menu/widget clicks on the A
//    press-edge (IsTouchDown==2 via the slot) and widget drags
//    (m_FingerSpawnPos refresh, slider tracking). The SLICE itself is
//    ungated when motion mode is OFF (press-to-cut, matching the live SDL
//    mouse) but IS speed-gated when motion mode is ON -- see the "Net
//    behaviour" acceptance spec below: A must be menu-click-only in motion
//    mode, so SlashEntity's motion-mode gate (src/entities/SlashEntity.cpp)
//    covers channels 0-3 as well as 12-15 on FRUIT_PLATFORM_WII
//    (FN::MOTION_GATE_CHANNEL_MIN/MAX, src/debug/DebugFlags.h).
//
//  Role 2 -- "hover pointer blade" (SDL: raw-mouse MOTION MODE path, only
//    while FN::g_MotionMode is ON). The blade tracks the pointer continuously
//    with no button; cuts are gated by SlashEntity's speed threshold
//    (FN::g_MotionSpeedThreshold); the button is INVERTED: pressing lifts the
//    blade, releasing re-presses it. Like Role 1 it feeds the Mortar::Touch
//    ring (extId = channel+1); the channel number is only this class's own
//    bookkeeping, the SLOT the press lands in is what the game acts on.
//    Wii mirror: remote N drives channel 12+N (FN::WII_POINTER_CHANNEL_FIRST
//    + N, see src/debug/DebugFlags.h) -- the analogue of the mouse's
//    POINTER_FINGER_CHANNEL (15):
//      - IR valid + A not held: channel pressed + position tracked
//        (SDL_MOUSEMOTION with no button held),
//      - A down-edge: release = blade lifts (SDL_MOUSEBUTTONDOWN),
//      - A up-edge with IR valid: re-press (SDL_MOUSEBUTTONUP),
//      - IR valid -> invalid: release (SDL_WINDOWEVENT_LEAVE analogue --
//        pointing away from the screen removes the blade).
//    When g_MotionMode is OFF this role is inert (mirrors SDL, where the raw
//    mouse handlers early-out on !g_MotionMode) and any live hover blade is
//    released on the next drain.
//    NO DOUBLE-BLADE: Role 2 is pressed only when `irValid && !aPressed` and
//    is released the instant A goes down, so Role 1 (aPressed && irValid)
//    and Role 2 are mutually exclusive on `aPressed` -- at most one blade
//    per remote is ever live.
//
// Net behaviour (the acceptance spec):
//   MOTION MODE OFF: A held + IR motion slices (press-to-cut, no speed gate,
//     via Role 1); A on a menu button clicks it.
//   MOTION MODE ON: slow motion (Role 1 held-but-slow, or Role 2 hovering)
//     does NOT slice; motion over g_MotionSpeedThreshold slices (either
//     role); A is MENU CLICK ONLY -- holding A never produces an ungated
//     slice (Role 1's slice is speed-gated same as Role 2's; Role 2 itself
//     is lifted while A is held).
//
// 4 Wiimotes = 4 simultaneous player pointers, the Wii analogue of the
// binary's "multiplayer IS multi-finger slicing" model (see CLAUDE.md); the
// press fingers use channels 0-3 and the hover blades channels 12-15. Roles
// are mutually exclusive per remote, so at most 4 of Mortar::Touch's 8 slots
// are claimed at once and every blade gets one.
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
    static const int MAX_REMOTES   = 4;   // WPAD_CHAN_0..3, hard hardware ceiling
    static const int CHANNEL_COUNT = 16;  // same 16-channel space as InputTranslatorSDL

    InputTranslatorWii();

    // No-op. Kept as a call site because mainWii calls it during boot; the
    // action names now come from Input/Input.txt via
    // InputManager::LoadConfigFile @0x002442fc.
    void Init();

    // Drain one WPAD channel's polled state into both roles (see header
    // block). Called once per remote per WPAD_ScanPads() from the main loop
    // (mainWii.cpp).
    //   chan:     WPAD_CHAN_0..WPAD_CHAN_3 (0-3)
    //   irValid:  ir_t.valid -- the remote is pointed at the screen and has a
    //             usable IR position this frame.
    //   aPressed: WPAD_BUTTON_A is held (raw, NOT pre-ANDed with irValid --
    //             the two signals compose differently per role/mode).
    //   x, y:     normalized ([0,1], top-left origin, y-down) IR pointer
    //             position in the WPAD_SetVRes-configured fb space; only
    //             meaningful while irValid.
    void DrainWiimoteIR(int chan, bool irValid, bool aPressed, float x, float y);

    // Drain the whole Mortar::Touch ring for one sim tick and advance the
    // Wii-only hand-pointer speed EMA -- a mirror of
    // InputTranslatorSDL::DispatchForSimTick(). The action events themselves
    // are raised later in the same tick by GameUpdate -> InputManager::Update,
    // so this must run BEFORE Game::stepUpdate().
    void DispatchForSimTick();

    // Queue a Touch release for every held channel (both roles) and drain the
    // ring. Called on suspend (Wii Home-menu return) -- analogous to
    // InputTranslatorSDL::ReleaseAllFingers() on SDL focus-loss.
    void ReleaseAllFingers();

    // Port specific: on-screen hand-pointer state for remote `remote`
    // (0..MAX_REMOTES-1). No binary equivalent -- feeds WiiPointer::Draw
    // (src/platform/wii/WiiPointer.cpp), the Wii-only IR cursor overlay.
    // Returns false (and leaves the outs untouched) when that remote's last
    // DrainWiimoteIR() call reported irValid==false. gx/gy are centred-ortho
    // game coords (same space Layout::TouchToGame produces); speed is the
    // per-sim-tick smoothed pointer speed (see DispatchForSimTick), in the
    // same units as SlashEntity::m_SmoothedSpeed.
    bool GetPointer(int remote, float* gx, float* gy, bool* aHeld, float* speed) const;

private:
    // Per-channel state, same model as InputTranslatorSDL: latest position
    // plus an active flag. Bookkeeping only -- the position and phase the game
    // acts on live in the Mortar::Touch slot this channel pushed into.
    float fingerX[CHANNEL_COUNT];
    float fingerY[CHANNEL_COUNT];
    bool  fingerActive[CHANNEL_COUNT];

    // Per-remote previous-frame raw signals for edge detection (Role 2's
    // inverted-button model needs the A edges; IR-loss release needs the IR
    // validity edge).
    bool  prevButtonDown[MAX_REMOTES];
    bool  prevIRValid[MAX_REMOTES];

    // Port specific: on-screen hand-pointer state, per remote. No binary
    // equivalent -- feeds GetPointer()/WiiPointer::Draw. Independent of the
    // press/hover channel roles above (this is "where to draw the hand",
    // not an input-dispatch signal).
    float m_PtrGX[MAX_REMOTES];
    float m_PtrGY[MAX_REMOTES];
    bool  m_PtrValid[MAX_REMOTES];
    bool  m_PtrAHeld[MAX_REMOTES];
    float m_PtrSmoothedSpeed[MAX_REMOTES];
    float m_PtrPrevGX[MAX_REMOTES];
    float m_PtrPrevGY[MAX_REMOTES];

    // Transform normalized IR-pointer coords (nx, ny in [0,1], top-left
    // origin, y-down -- same convention WPAD_SetVRes/ir.x,ir.y normalize to)
    // into centred game-ortho coords, mirroring
    // InputTranslatorSDL::TransformTouchNormalized. See InputTranslatorWii.cpp.
    void TransformIRNormalized(float nx, float ny, float& gx, float& gy);

    // Role 2 press/release helpers for the hover-blade channels (12-15) --
    // mirrors InputTranslatorSDL::PointerPressMouseChannel /
    // PointerReleaseMouseChannel.
    // PointerPressChannel is a no-op if the channel is already active;
    // PointerReleaseChannel is a no-op if it is not.
    void PointerPressChannel(int ch, float gx, float gy);
    void PointerReleaseChannel(int ch);
};

#endif // FRUIT_PLATFORM_WII

#endif // FN_PLATFORM_WII_INPUT_TRANSLATOR_WII_H
