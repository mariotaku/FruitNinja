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

// v1.6.1 DefaultDeleteCallback @0x0018b0fc — no-op free function.
// The ctor's Global-delegate factory (v1.6.1 T_865 @0x0018b310, called from
// 0x0018b3ec) builds a "Global" (free-function) delegate variant bound to this
// target and assigns it into m_RemoveCallback (+0x38).
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

// v1.6.1 HUDControl::HUDControl C2 @0x0018b354 / C1 @0x0018b440.
// C1 and C2 are instruction-identical (47 body instructions + a 12-instruction
// EH landing pad = the 59 the asm-verify pass counts).
//
// Binary field writes, in binary order:
//   +0x00 vptr                     (vtable+8)
//   +0x04 m_Singular      = 0      (strb @0x0018b36c)
//   +0x14 m_HudScale      = _Vector3<float>::Zero  (bl T_864 @0x0018b2dc)
//   +0x31 m_reserved31    = 0
//   +0x32 m_bNoDestructor = 0
//   +0x33 m_bPendingRemoval = 0
//   +0x30 m_Active        = 1
//   +0x34 m_LayerFlags    = 1
//   +0x38 m_RemoveCallback default-ctor            (bl 0x001138fc)
//   +0x5c m_DrawColour    = Colour::White          (bl 0x00110c48, copy ctor)
//   +0x60 m_bUseHUDScales = 1      (strb r7 @0x0018b3bc)
//   +0x64 m_UVLeft/m_UVTop    = _Vector2<float>::Zero (ldm/stm pair)
//   +0x6c m_UVRight/m_UVBottom = _Vector2<float>::One  (ldm/stm pair)
//   then Global-delegate build (bl T_865 @0x0018b310), operator= into +0x38
//   (bl 0x00112880), and ~Global on the stack temporary (bl 0x0010e334).
//
// DIFFERS: the binary leaves pos (+0x08), size (+0x20) and m_Timer (+0x2c)
// UNINITIALISED — C1/C2 contain no store to any of those offsets. The port
// zero-inits all three (pos/size through the _Vector3<float> default ctor,
// m_Timer explicitly) because reading an uninitialised float is UB on the host
// toolchains and the F1 hitbox overlay walks every live control. Every
// subclass assigns all three before first use, so no observable behaviour
// changes; the cost is ~7 extra inline vstr in the cross-build.
//
// The rest of the asm-verify delta on this symbol is inlining, not behaviour:
// Mortar::Delegate1 is a header-only template here, so the four out-of-line
// binary calls (Delegate1 ctor / Global ctor / operator= / ~Global) expand
// inline over two 36-byte stack temporaries; and m_DrawColour is built from
// literals rather than copy-constructed from the Colour::White global (same
// four bytes, one fewer call). Together those account for the whole 143p-vs-59b
// instruction gap.
//
// ASM-verified: 2026-07-31T00:00Z v1.6.1 HUDControl::HUDControl C2 @ 0x0018b354 (re-analyst)
HUDControl::HUDControl()
    : m_Singular(0),                              // +0x04
      m_Timer(0.0f),                              // DIFFERS: see the ctor note above
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
    // m_RemoveCallback: a free-function "Global" delegate bound to the no-op
    // DefaultDeleteCallback. The binary builds it on the stack via the factory
    // T_865 @0x0018b310 (called from 0x0018b3ec) then assigns into +0x38
    // (Delegate1<void,HUDControl*>::operator= @0x00112880, call at 0x0018b3f8).
    m_RemoveCallback = Mortar::Delegate1<void, HUDControl*>::MakeFree(&DefaultDeleteCallback);
#ifndef __bada__
    // Port specific: debug-only registry. Binary has no global HUDControl
    // list; ctor/dtor xrefs confirm. Used by F1 hitbox overlay only.
    s_ActiveControls.push_back(this);
    LOG_DEBUG("HUDCONTROL", "ctor this=%p", static_cast<void*>(this));
#endif
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
