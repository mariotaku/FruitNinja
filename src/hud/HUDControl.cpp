#include "HUDControl.h"
#include "engine/audio/MortarSound.h"

#ifndef __bada__
#include "debug/Logger.h"
#include <list>
static std::list<HUDControl*> s_ActiveControls;

const std::list<HUDControl*>& HUDControl::GetActiveControls() {
    return s_ActiveControls;
}
#endif

// Binary @ 0x00143f94 — DefaultDeleteCallback(HUDControl*): no-op free
// function. The ctor's MakeDelegate_PauseScreen_HUD (binary @ 0x001440d8)
// builds a "Global" (free-function) delegate variant bound to this target
// and assigns it into m_RemoveCallback (+0x38).
void DefaultDeleteCallback(HUDControl* control)
{
    (void)control;
}

// v1.6.1 DefaultSoundRemovedCallback @0x00151a74 -- default sound-remove no-op callback.
// Used at 30+ call sites as the default when no specific cleanup is needed on sound removal.
int DefaultSoundRemovedCallback(Mortar::MortarSound* /*snd*/)
{
    return 0;
}

// v1.6.1 HUDControl::HUDControl C2 @0x0018b354 / C1 @0x0018b440
HUDControl::HUDControl()
    : m_Singular(0),                              // +0x04
      m_Timer(0.0f),                              // DIFFERS: binary leaves uninitialised; port zero-inits for determinism
      m_Active(1),                                // +0x30
      m_reserved31(0),
      m_bNoDestructor(0),
      m_bPendingRemoval(0),
      m_LayerFlags(1),
      m_DrawColour(255, 255, 255, 255),           // +0x5c (g_WhiteColour)
      m_bUseHUDScales(1),                         // +0x60 (strb r7,[r4,#0x60] @0x0018b3bc)
      m_UVLeft(0.0f), m_UVTop(0.0f),             // +0x64, GOT[0x78c0] = (0,0)
      m_UVRight(1.0f), m_UVBottom(1.0f)          // +0x6c, GOT[0x7170] = (1,1)
{
    // m_RemoveCallback = MakeDelegate_PauseScreen_HUD(): a free-function "Global"
    // delegate bound to the no-op DefaultDeleteCallback. The ctor builds it on the
    // stack via the factory at 0x0018b310 (called from 0x0018b3ec) then move-assigns
    // into +0x38 (call at 0x0018b3f8).
    m_RemoveCallback = Mortar::Delegate1<void, HUDControl*>::MakeFree(&DefaultDeleteCallback);
#ifndef __bada__
    // Port specific: debug-only registry. Binary has no global HUDControl
    // list; ctor/dtor xrefs confirm. Used by F1 hitbox overlay only.
    s_ActiveControls.push_back(this);
    LOG_DEBUG("HUDCONTROL", "ctor this=%p", static_cast<void*>(this));
#endif
    // ASM-verified: 2026-05-24 v1.6.1 HUDControl::HUDControl @ 0x0018b354 (re-analyst)
}

HUDControl::~HUDControl()
{
#ifndef __bada__
    LOG_DEBUG("HUDCONTROL", "dtor this=%p", static_cast<void*>(this));
    s_ActiveControls.remove(this);
#endif
}

// ASM-verified: 2026-05-24 v1.6.1 HUDControl::Init @ 0x0018b100 (re-analyst)
// Single `bx lr`. Sibling no-op slots: Release @0x0018b104, Reset @0x0018b108,
// Draw @0x0018b10c, Update @0x0018b110.
void HUDControl::Init()
{}

void HUDControl::Release()
{}

void HUDControl::Reset()
{}

void HUDControl::Update(float dt)
{
    (void)dt;
}

bool HUDControl::SetToMultiplayerState()
{
    if (m_Singular == 0) {
        m_bNoDestructor = 0;
        m_bPendingRemoval = 1;
        return true;
    }
    return false;
    // ASM-verified: 2026-05-24 v1.6.1 HUDControl::SetToMultiplayerState @ 0x0018b114 (re-analyst)
}

// v1.6.1 HUDControl::GetAdjustedPos @0x00136c2c — DAT_00136c88={480.0f, 320.0f, 0.0f}
// Returns pos + Vec3(480,320,0) * m_HudScale.
// Used by MenuButton::Update to re-anchor the held fruit/bomb entity every frame.
_Vector3<float> HUDControl::GetAdjustedPos()
{
    return _Vector3<float>(pos.x + 480.0f * m_HudScale.x,
                           pos.y + 320.0f * m_HudScale.y,
                           pos.z + 0.0f   * m_HudScale.z);
}
