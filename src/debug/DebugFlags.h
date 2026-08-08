#ifndef FN_DEBUG_FLAGS_H
#define FN_DEBUG_FLAGS_H

#ifndef __bada__

namespace Mortar { class FontCacheObjectTTF; }

//
// Port specific: debug-only overlay flags. No binary equivalent.
// g_DebugHitboxes:  4-level debug overlay cycle. Toggle F1 (cycles 0->1->2->3->0).
//                     0 = off
//                     1 = entity collision spheres + blade trails
//                     2 = level 1 + HUDControl bounding boxes
//                     3 = level 2 + font/text-box debug overlays
// g_DebugWireframe: glPolygonMode(GL_LINE) around 3D entity pass. Toggle F2.
//                   Desktop GL only (no-op under GLES).
// g_ShowFps:        "FPS NNN" counter in top-left corner. Toggle F3,
//                   or launch with --fps (desktop) / web ?fps=1. Also
//                   user-settable via SettingsScreen and persisted
//                   (SettingsSave); the CLI/URL override runs after
//                   LoadSettings so it wins for that session.
// g_FpsCap60:       Port specific: caps render/present rate to 60 fps
//                   (sim stays a fixed 60 Hz accumulator either way -- see
//                   FixedStepDriver). false (default) = native/display-refresh
//                   present rate; true = "Limit to 60 FPS" (SettingsScreen
//                   checkbox). Binary is fixed 60 Hz Bada; this is a
//                   render-only QoL knob for high-refresh desktop/web panels.
// g_DebugTimeScale: multiplies fixed dt=1/60. 1.0 = normal, 0.1 = slow-mo.
//                   Toggle F7.
// g_bOsdSfx:        posts an OSD toast per SFX played, carrying why it might
//                   be silent (SoundManager::SFXPlay + SFXSetVolume, which
//                   appends the volume byte one call later).
//                   SDL   : "[tick] <name> a=<busy>/16 m=<sfxMuted> v=<byte>"
//                   Web   : "[tick] <name> d=<decode> g=<initial gain>
//                            c=<ctx state> m=<sfxMuted> v=<byte>"
//                   v is the raw 0-255 volume byte and the voice's linear gain
//                   is v/255 (255 full, 0 silent, 20 about 8%); no v= means
//                   SetVolume never arrived for that handle. Web d=: 1
//                   decoded, 0 undecoded (deferred), P decoding, F failed
//                   (play dropped); c=: r running, s suspended (no audio
//                   gesture), c closed, ? no context. Field meanings are
//                   spelled out at each backend's readout block.
//                   Display-only -- never gates actual audio. Toggle F4,
//                   or launch with --osd-sfx (desktop) / web ?osdsfx=1.
// g_MotionMode:     host-only velocity-gated pointer slash -- the pointer
//                   blade (channel FN::MOTION_BLADE_CHANNEL) tracks the
//                   raw cursor position continuously (same plumbing as the
//                   retired "relax" mode), but a fruit/bomb cut only
//                   registers when the blade's smoothed speed clears
//                   g_MotionSpeedThreshold: slow movement aims, a fast
//                   flick cuts. The mouse BUTTON keeps its ordinary
//                   press/release meaning on FN::POINTER_FINGER_CHANNEL in
//                   both modes, so menus and widgets behave the same way
//                   with motion mode on or off: point to aim, click to
//                   press. While motion mode is ON that click is UI-only --
//                   it never feeds a blade, see MOTION_CLICK_ONLY_CHANNEL
//                   below. See src/platform/InputTranslatorSDL.cpp
//                   (cursor tracking) and src/entities/SlashEntity.cpp
//                   (the speed gate). Toggle F5, or launch
//                   with --motion / web ?motion=1.
// g_MotionSpeedThreshold: px-per-sim-tick cut threshold for g_MotionMode.
//                   Tune live with F6 (down) / F8 (up), --motion-threshold=<f>
//                   (desktop) or web ?motionthreshold=<f>.
//

namespace FN {

extern int   g_DebugHitboxes;          // Port specific: 0=off 1=entity 2=+HUD 3=+font, see comment above
extern bool  g_DebugWireframe;         // Port specific: desktop GL only
extern float g_DebugTimeScale;         // Port specific: debug-only, no binary equivalent
extern bool  g_ShowFps;                // Port specific: FPS counter overlay (toggle F3, --fps, ?fps=1)
extern bool  g_FpsCap60;               // Port specific: cap render/present rate to 60fps (SettingsScreen "Limit to 60 FPS"), default OFF (native/display-refresh)
extern bool  g_SuppressTextOverlay;    // Port specific: suppresses DebugText_Overlay for debug-drawn text
extern bool  g_bOsdSfx;                // Port specific: OSD toast per SFX played (toggle F4, --osd-sfx, ?osdsfx=1)
extern bool  g_MotionMode;             // Port specific: velocity-gated pointer slash (toggle F5, --motion), default ON
extern float g_MotionSpeedThreshold;   // Port specific: g_MotionMode cut speed threshold, px/sim-tick (tune F6/F8, --motion-threshold=<f>)

// Port specific: shared pointer/mouse finger channel constant -- single
// source of truth for InputTranslatorSDL::MOUSE_CHANNEL (channel 15, the
// mouse's dedicated non-touch channel; see InputTranslatorSDL.h). This is the
// UI channel: it carries ORDINARY press/release semantics in BOTH modes, so
// menus, widgets and scrollers see a real click edge. Lives here (not
// InputTranslatorSDL.h) so other TUs can reference it without including the
// SDL translator header.
static const int POINTER_FINGER_CHANNEL = 15;

// Port specific: SDL/host MOTION MODE hover-blade channel. Deliberately NOT
// POINTER_FINGER_CHANNEL: motion mode holds its channel pressed continuously
// so the blade can track the cursor, and a permanently-held press on the UI
// channel latches every widget and scroller the cursor passes over (the
// press-edge would also arrive on button-UP instead of button-DOWN). Hover
// channels are hidden from the Tier A UI helpers -- see TouchInRegion /
// IsTouchDown / Touch::GetTouchInRegion in src/engine/input/Touch.cpp -- so
// only the blade ever acts on them.
static const int MOTION_BLADE_CHANNEL = 14;

// Port specific: Wii motion-mode hover-blade channel range -- Wiimote N
// (0-3) drives a pointer blade on channel (WII_POINTER_CHANNEL_FIRST + N),
// the 4-remote analogue of the SDL mouse's MOTION_BLADE_CHANNEL. See
// src/platform/wii/InputTranslatorWii.h for the full two-role input model.
static const int WII_POINTER_CHANNEL_FIRST = 12;
static const int WII_POINTER_CHANNEL_LAST  = 15;

// Port specific: inclusive channel range whose Mortar::Touch slots carry a
// continuously-held HOVER BLADE rather than a real finger press. The Tier A
// UI helpers (TouchInRegion / IsTouchDown / Touch::GetTouchInRegion) skip
// these slots outright, which is what stops a motion-mode pointer from
// hijacking menus, scrollers and widgets. SlashEntity is unaffected: blades
// are addressed by their Mortar::Touch SLOT through the per-finger action
// callbacks, never through these helpers.
#if defined(FRUIT_PLATFORM_WII)
static const int HOVER_BLADE_CHANNEL_FIRST = WII_POINTER_CHANNEL_FIRST;
static const int HOVER_BLADE_CHANNEL_LAST  = WII_POINTER_CHANNEL_LAST;
#else
static const int HOVER_BLADE_CHANNEL_FIRST = MOTION_BLADE_CHANNEL;
static const int HOVER_BLADE_CHANNEL_LAST  = MOTION_BLADE_CHANNEL;
#endif

// Port specific: the channel whose touches are CLICK-ONLY while g_MotionMode
// is ON. A Mortar::Touch slot that came from this channel (extId == channel+1)
// is never fed to a SlashEntity, so the click drives UI and nothing else --
// no trail, no cut. In motion mode the blade is the HOVER channel's job alone;
// without this gate a fast button-drag is an ordinary touch and produces a
// second, speed-gated, CUTTING blade on the UI channel.
// -1 means "no such channel" -- the gate never matches.
// The gate itself lives in src/game/GameTaskInput.cpp, at the seam where a
// slot index becomes a blade (TouchDownCallback / PointerMoveCallback).
// Wii is deliberately -1. Its press channels are 0-3 (the A button) and its
// own acceptance spec keeps A's slice SPEED-GATED rather than suppressed (see
// src/platform/wii/InputTranslatorWii.h, "Net behaviour"). The number would be
// wrong there too: channel 15 is a Wii HOVER channel, not a click channel.
#if defined(FRUIT_PLATFORM_WII)
static const int MOTION_CLICK_ONLY_CHANNEL = -1;
#else
static const int MOTION_CLICK_ONLY_CHANNEL = POINTER_FINGER_CHANNEL;
#endif

// Port specific: SlashEntity's motion-mode speed-gate range (see
// SlashEntity::Update). SlashEntity::m_FingerId is the Mortar::Touch SLOT the
// blade belongs to (0..7) -- the translator's channel number never reaches it,
// because the binary's Touch::SendIndividualTouchCallbacks @0x00242bc4 derives
// the action channel from the states1 slot it is standing on. The pointer
// cursor claims whatever slot is free, so the whole slot range has to be gated.
// That is the same conclusion the Wii already reached for its own reasons (A is
// menu-click-only in motion mode, so its press channels are gated too).
// Motion mode is OFF by default and, when ON, the cursor is normally the only
// active pointer, so gating the whole range costs nothing.
#if defined(FRUIT_PLATFORM_WII)
static const int MOTION_GATE_CHANNEL_MIN = 0;
static const int MOTION_GATE_CHANNEL_MAX = WII_POINTER_CHANNEL_LAST;
#else
static const int MOTION_GATE_CHANNEL_MIN = 0;
static const int MOTION_GATE_CHANNEL_MAX = 7;
#endif

// Render every active Fruit / Bomb / SplatEntity collision sphere as
// a translucent circle. Call from GameDraw after the entity pass.
// No-op when g_DebugHitboxes < 1.
void DebugHitbox_Draw();

// Render every active HUDControl bounding box as a magenta AABB outline.
// Covers all HUDControl subclasses (MenuButton, BonusScreen controls, etc.).
// Call from GameDraw right after DebugHitbox_Draw().
// No-op when g_DebugHitboxes < 2.
void DebugHUDBounds_Draw();

// Render "FPS NNN" in the top-left corner of the screen.
// Call from renderFrame() after GameTaskDraw so the overlay is additive.
// No-op when g_ShowFps is false or fps <= 0.
void DebugFps_Draw(float fps);

// Draw a thick line from m_TailPos to m_HeadPos for every active blade
// (IsBladeActive() == true). Yellow line + small end-cap markers.
// Call from GameDraw after DebugHitbox_Draw / DebugHUDBounds_Draw.
// No-op when g_DebugHitboxes < 1.
void DebugBladeTrails_Draw();

// Draw a text-anchor crosshair (MAGENTA), an optional box rect (GREEN),
// and an optional ink-bounds rect (YELLOW) for one text draw call.
// All coordinates are in centred-ortho world space.
// hasBox: if true, draws [boxX0,boxY0]-[boxX1,boxY1] as the declared text box.
// hasInk: if true, draws [inkX0,inkY0]-[inkX1,inkY1] as the measured ink bounds.
//         Pass false when the caller has no real per-vertex ink measurement
//         (e.g. BakedStringBox only has a declared box, no ink) -- passing
//         ink==box there would draw the same rect twice.
// No-op when g_DebugHitboxes < 3.
void DebugText_Overlay(float anchorX, float anchorY,
                       bool hasBox,
                       float boxX0, float boxY0, float boxX1, float boxY1,
                       bool hasInk,
                       float inkX0, float inkY0, float inkX1, float inkY1);

// Port specific: shared accessor for the debug TTF font cache used by
// DebugFps_Draw (gangofchinese.ttf, lazily created). OSD_Draw
// (src/debug/OSD.cpp) reuses it so toasts render through the exact same
// text path as the FPS counter. Returns null until the font loads.
Mortar::FontCacheObjectTTF* DebugFontTTF_Get();

// Port specific: release the lazily-created fonts this TU owns -- the bitmap
// "fonts/verdana.fnt" used by DebugHUDBounds_Draw's labels, and the
// gangofchinese.ttf face plus its baked FPS string. Call it from GameDestroy's
// port-only release block, i.e. while the GL context is still alive: these
// statics would otherwise be destroyed at atexit, after SDL_GL_DeleteContext,
// leaking their GL textures and FontInterface objects. Idempotent, and every
// slot is rebuilt lazily on the next draw, so calling it mid-session only costs
// one reload. Does not touch the debug flags themselves.
void DebugFlags_ReleaseResources();

} // namespace FN

#else // __bada__

// On the Bada / cross-build target there is no debug time scaling.
// Provide g_DebugTimeScale as a compile-time constant so call sites
// that multiply by it compile and reduce to no-op arithmetic.
namespace FN {
static const float g_DebugTimeScale      = 1.0f;
static const int   g_DebugHitboxes       = 0;
static const bool  g_DebugWireframe      = false;
static const bool  g_ShowFps             = false;
static const bool  g_FpsCap60            = false;
static const bool  g_SuppressTextOverlay = false;
static const bool  g_bOsdSfx             = false;
static const bool  g_MotionMode          = false;
inline void DebugHitbox_Draw()  {}
inline void DebugHUDBounds_Draw() {}
inline void DebugFps_Draw(float) {}
inline void DebugBladeTrails_Draw() {}
inline void DebugText_Overlay(float, float,
                               bool,
                               float, float, float, float,
                               bool,
                               float, float, float, float) {}
inline void DebugFlags_ReleaseResources() {}
} // namespace FN

#endif // !__bada__

#endif
