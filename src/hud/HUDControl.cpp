#include "HUDControl.h"

#ifndef __bada__
#include "debug/Logger.h"
#include <list>
static std::list<HUDControl*> s_ActiveControls;

const std::list<HUDControl*>& HUDControl::GetActiveControls() {
    return s_ActiveControls;
}
#endif

HUDControl::HUDControl()
    : m_bPreserveOnMP(0),
      m_Timer(0.0f),
      m_bActive(1),
      field_0x31(0),
      m_bNoDestructor(0),
      m_bPendingRemoval(0),
      m_LayerFlags(1),
      m_DrawColour(255, 255, 255, 255),
      m_bUseHUDScales(1),
      m_UVLeft(0.0f), m_UVTop(0.0f), m_UVRight(1.0f), m_UVBottom(1.0f)
{
#ifndef __bada__
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

// Binary @ 0x00143fac
bool HUDControl::SetToMultiplayerState()
{
    if (m_bPreserveOnMP == 0) {
        m_bNoDestructor = 0;
        m_bPendingRemoval = 1;
    }
    return m_bPreserveOnMP == 0;
}
