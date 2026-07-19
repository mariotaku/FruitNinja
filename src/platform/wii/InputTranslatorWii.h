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
// (MOUSE_CHANNEL / POINTER_FINGER_CHANNEL 15, the pending-bool model) is
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
//    ONLY input path MenuButton / CheckBox / SliderControl / etc. consume
//    (free IsTouchDown / TouchInRegion + game_work.m_FingerSpawnPos;
//    InputManager hash events on channels >= 8 never reach them), so this
//    role is what makes menu/widget clicks work in EITHER mode.
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
//    blade, releasing re-presses it. Uses the pending-bool model because
//    channels >= 8 have no Mortar::Touch slot.
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
// press fingers use channels 0-3 and the hover blades channels 12-15, so up
// to 8 SlashEntity blades can be live at once (g_pSlashEntities covers 16).
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

    // Initialize action hashes for all 16 channels ("TouchDown_N",
    // "TouchMove_XN", "TouchMove_YN", "TouchUp_N", "TouchScreen"), mirroring
    // InputTranslatorSDL::Init(). Call once after StringHash is available.
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

    // Drain the Mortar::Touch ring buffer and dispatch InputManager hash
    // events for one sim tick -- a mirror of
    // InputTranslatorSDL::DispatchForSimTick(): HUD input-modal gate,
    // Touch::Update(0.0f) full-ring drain, slot-derived dispatch (+
    // game_work.m_FingerSpawnPos refresh) for channels 0-7, pending-bool
    // dispatch for channels 8-15. Called once per sim tick from the main
    // loop, BEFORE Game::stepUpdate().
    void DispatchForSimTick();

    // Synthesize a release for every held channel (both roles). Called on
    // suspend (Wii Home-menu return) -- analogous to
    // InputTranslatorSDL::ReleaseAllFingers() on SDL focus-loss.
    void ReleaseAllFingers();

private:
    // Pre-computed action hashes, indexed by CHANNEL (0-15) like the SDL
    // translator (Role 1 uses 0-3, Role 2 uses 12-15).
    uint32_t hashTouchDown[CHANNEL_COUNT];
    uint32_t hashTouchMoveX[CHANNEL_COUNT];
    uint32_t hashTouchMoveY[CHANNEL_COUNT];
    uint32_t hashTouchUp[CHANNEL_COUNT];
    uint32_t hashTouchScreen;

    // Per-channel state, same model as InputTranslatorSDL: position, active
    // flag, previous-dispatch active snapshot, pending bools (channels >= 8
    // only), press-vs-motion gate.
    float fingerX[CHANNEL_COUNT];
    float fingerY[CHANNEL_COUNT];
    bool  fingerActive[CHANNEL_COUNT];
    bool  prevActive[CHANNEL_COUNT];
    bool  pendingDown[CHANNEL_COUNT];
    bool  pendingUp[CHANNEL_COUNT];
    bool  pendingEdge[CHANNEL_COUNT];
    // Port specific: press-vs-motion gate, same semantics as
    // InputTranslatorSDL::motionSinceDown -- false on a fresh press, true
    // once the pointer position actually changes while pressed (the WPAD
    // poll-model equivalent of "an SDL_FINGERMOTION / SDL_MOUSEMOTION event
    // arrived": SDL only delivers those when the pointer moved). TouchMove_XN
    // /YN are only dispatched while true, so a press-and-release without
    // movement is a TAP (TouchScreen + TouchDown_N + TouchUp_N, no TouchMove)
    // -- v1.6.1 semantics, matching the SDL translator.
    bool  motionSinceDown[CHANNEL_COUNT];

    // Per-remote previous-frame raw signals for edge detection (Role 2's
    // inverted-button model needs the A edges; IR-loss release needs the IR
    // validity edge).
    bool  prevButtonDown[MAX_REMOTES];
    bool  prevIRValid[MAX_REMOTES];

    // Transform normalized IR-pointer coords (nx, ny in [0,1], top-left
    // origin, y-down -- same convention WPAD_SetVRes/ir.x,ir.y normalize to)
    // into centred game-ortho coords, mirroring
    // InputTranslatorSDL::TransformTouchNormalized. See InputTranslatorWii.cpp.
    void TransformIRNormalized(float nx, float ny, float& gx, float& gy);

    // Role 2 press/release helpers for the hover-blade channels (12-15) --
    // mirrors InputTranslatorSDL::PointerPressMouseChannel /
    // PointerReleaseMouseChannel (the ch >= 8 pending-bool model).
    // PointerPressChannel is a no-op if the channel is already active;
    // PointerReleaseChannel is a no-op if it is not.
    void PointerPressChannel(int ch, float gx, float gy);
    void PointerReleaseChannel(int ch);
};

#endif // FRUIT_PLATFORM_WII

#endif // FN_PLATFORM_WII_INPUT_TRANSLATOR_WII_H
