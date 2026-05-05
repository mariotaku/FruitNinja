#include "HUDControl.h"

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
{}

HUDControl::~HUDControl()
{}

void HUDControl::Init()
{}

void HUDControl::Release()
{}

void HUDControl::Reset()
{}

// STUB: HUDControl::Draw(float*) -- binary @ 0x???? (TODO RE)
void HUDControl::Draw(float* viewVec)
{
    (void)viewVec;
}

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
