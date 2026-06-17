#include "BombFlash.h"
#include "math/MathUtil.h"
#include "math/Matrix44.h"
#include "asset/Mesh.h"
#include "render/MatrixManager.h"
#include <cstring>

// Binary flat-pool globals (v1.6.1):
//   s_pPool      @ 0x00317898  -- base pointer to BombFlash array
//   s_PoolCount  @ 0x0031789c  -- element count (set by CreatePool)
//   s_CurentFree @ 0x003178a0  -- rotating next-free index (note: binary spells it CurentFree)
//
// CreatePool is a bx-lr stub in v1.6.1 so s_pPool is never set. All pool-dependent
// functions guard on s_pPool == 0 and are effectively no-ops at runtime.
static BombFlash* s_pPool     = 0;
static int        s_PoolCount = 0;
static int        s_CurentFree = 0;

// ---------------------------------------------------------------------------
// Update animation constants (binary literal pool @ v1.6.1 BombFlash::Update @0x001d4dd4).
// ---------------------------------------------------------------------------
static const float FLASH_LIFE        = 0.6f;
static const float SCALE_X_GROW      = 50.0f;
static const float SCALE_Y_GROW      = 200.0f;
static const float TIMER_RESET       = 0.0f;
static const float SCALE_Y_BASE      = 100.0f;
static const float SCALE_X_BASE      = 150.0f;
static const float FADE_PEAK_TIME    = 0.2f;
static const float FADE_OUT_SPAN     = 0.4f;

// ---------------------------------------------------------------------------
// MakeFlash constants (binary literal pool @ v1.6.1 BombFlash::MakeFlash @0x001d5bf0).
// ---------------------------------------------------------------------------
static const float MF_TIMER_INIT     = 0.0f;
static const float MF_POS_Z          = -5400.0f;
static const float MF_X_NEG          = -240.0f;
static const float MF_SCALE          = 128.0f;
static const float MF_X_POS          = 240.0f;
static const float MF_DIR_MUL        = 5.0f;

// v1.6.1 BombFlash::BombFlash @0x001d5fa8
BombFlash::BombFlash()
    : m_Timer(0.0f)
    , m_Colour0()
    , m_Colour1()
    , m_SinAngle(0.0f)
    , m_CosAngle(0.0f)
    , m_pTexture()
    , m_Pos(0.0f, 0.0f, 0.0f)
    , m_Dir(0.0f, 0.0f, 0.0f)
    , m_Scale_x(0.0f)
    , m_Scale_y(0.0f)
    , m_Scale_z(0.0f)
    , m_bActive(false)
    , m_Pad41(0)
    , m_AngleIdx(0)
{
}

// v1.6.1 BombFlash::~BombFlash @0x001d5b80 / @0x001d5ac0
BombFlash::~BombFlash() {}

// v1.6.1 BombFlash::Update @0x001d4dd4
//
// Scale grows quadratically with normalised lifetime f = m_Timer / 0.6:
//   scale.x = 150 + 50*f^2,  scale.y = 100 + 200*f^2,  scale.z = 0
//
// Alpha (m_Colour1.a) ramps up linearly over the first 0.2s to m_Colour0.a,
// then fades out quadratically over the remaining 0.4s:
//   t <  0.2 : a = maxA * (t / 0.2)
//   t == 0.2 : a = maxA
//   t >  0.2 : a = maxA * ((0.6 - t) / 0.4)^2
// The result is clamped to [0, maxA] and truncated to an integer byte.
//
// When m_Timer exceeds 0.6 the flash deactivates and the timer resets to 0.
void BombFlash::Update(float dt) {
    const float maxA = (float)m_Colour0.a;

    m_Timer += dt;
    const float f = m_Timer / FLASH_LIFE;

    m_Scale_x = SCALE_X_BASE + f * SCALE_X_GROW * f;
    m_Scale_y = SCALE_Y_BASE + f * SCALE_Y_GROW * f;
    m_Scale_z = TIMER_RESET;

    uint8_t curAlpha;
    if (FADE_PEAK_TIME <= m_Timer) {
        if (m_Timer == FADE_PEAK_TIME) {
            curAlpha = m_Colour0.a;
        } else {
            float k = (FLASH_LIFE - m_Timer) / FADE_OUT_SPAN;
            float a = maxA * k * k;
            if (a <= 0.0f) {
                curAlpha = 0;
            } else {
                if (maxA <= a) {
                    curAlpha = (maxA > 0.0f) ? (uint8_t)(int)maxA : 0;
                } else {
                    curAlpha = (a > 0.0f) ? (uint8_t)(int)a : 0;
                }
            }
        }
    } else {
        float a = maxA * (m_Timer / FADE_PEAK_TIME);
        if (a <= 0.0f) {
            curAlpha = 0;
        } else {
            if (maxA <= a) {
                curAlpha = (maxA > 0.0f) ? (uint8_t)(int)maxA : 0;
            } else {
                curAlpha = (a > 0.0f) ? (uint8_t)(int)a : 0;
            }
        }
    }

    m_Colour1.a = curAlpha;

    if (FLASH_LIFE < m_Timer) {
        m_bActive = false;
        m_Timer = TIMER_RESET;
    }
}

// v1.6.1 BombFlash::CreatePool @0x001d4cc0
// Defunct: BombFlash pool disabled in v1.6.1 -- no-op stub; v1.6.1 BombFlash::CreatePool @0x001d4cc0
int BombFlash::CreatePool(int n) {
    (void)n;
    return n;
}

// v1.6.1 BombFlash::GetFree @0x001d4cc4
//
// Walks the pool from the rotating index, advancing (index+1) % count
// past any active slot, capped at count iterations. Returns NULL if pool is empty.
BombFlash* BombFlash::GetFree() {
    if (!s_pPool) return 0;

    int index = s_CurentFree;
    int iterations = 0;
    BombFlash* slot = s_pPool + index;
    while (slot->m_bActive) {
        if (s_PoolCount <= iterations) break;
        index = (index + 1) % s_PoolCount;
        slot = s_pPool + index;
        ++iterations;
    }
    s_CurentFree = index;
    return slot;
}

// v1.6.1 BombFlash::MakeFlash @0x001d5bf0
//
// NOTE: the `col` parameter is dead in the binary (MakeFlash never writes the
// flash colour; it stays at the ctor default). It is kept in the signature for
// call-shape parity.
//
// Binary body:
//   slot->m_pTexture = *tex
//   slot->m_Pos = *pos;  slot->m_Pos.z = -5400
//   slot->m_Dir = *dir
//   slot->m_Pos += dir * 5.0
//   slot->m_Pos.x = (pos.x < 0) ? -240 : 240
//   slot->m_Scale = (128, 128, 128)
//   idx = Atan2Idx(dir.x, -dir.y)
//   slot->m_Timer = 0;  slot->m_bActive = 1;  slot->m_AngleIdx = idx
//   slot->m_SinAngle = SinIdx(idx);  slot->m_CosAngle = CosIdx(idx)
//   slot->Update(0)
void BombFlash::MakeFlash(Colour /*col*/, Vec3* pos, Vec3* dir,
                           Mortar::SmartPtr<Mortar::Texture>* tex) {
    BombFlash* slot = GetFree();
    if (!slot) return;

    slot->m_pTexture = *tex;

    slot->m_Pos = *pos;
    slot->m_Pos.z = MF_POS_Z;
    slot->m_Dir = *dir;

    slot->m_Pos.x += dir->x * MF_DIR_MUL;
    slot->m_Pos.y += dir->y * MF_DIR_MUL;
    slot->m_Pos.z += dir->z * MF_DIR_MUL;

    slot->m_Pos.x = (slot->m_Pos.x < MF_TIMER_INIT) ? MF_X_NEG : MF_X_POS;

    slot->m_Scale_x = MF_SCALE;
    slot->m_Scale_y = MF_SCALE;
    slot->m_Scale_z = MF_SCALE;

    uint16_t idx = (uint16_t)Math::Atan2Idx(slot->m_Dir.x, -slot->m_Dir.y);

    slot->m_Timer    = MF_TIMER_INIT;
    slot->m_bActive  = true;
    slot->m_AngleIdx = idx;

    slot->m_SinAngle = SinIdx(idx);
    slot->m_CosAngle = CosIdx(idx);

    slot->Update(MF_TIMER_INIT);
}

// v1.6.1 BombFlash::UpdateActiveFlashes @0x001d4dc4
// Defunct: BombFlash pool disabled in v1.6.1 -- no-op stub; v1.6.1 BombFlash::UpdateActiveFlashes @0x001d4dc4
void BombFlash::UpdateActiveFlashes(float dt) {
    (void)dt;
}

// v1.6.1 BombFlash::DrawActiveFlashes @0x001d4dc8
// Defunct: BombFlash pool disabled in v1.6.1 -- no-op stub; v1.6.1 BombFlash::DrawActiveFlashes @0x001d4dc8
void BombFlash::DrawActiveFlashes() {
}

// v1.6.1 BombFlash::RemoveAllFlashes @0x001d4d64
void BombFlash::RemoveAllFlashes() {
    if (!s_pPool) return;
    for (int i = 0; i < s_PoolCount; ++i) {
        s_pPool[i].m_bActive = false;
    }
}

// v1.6.1 BombFlash::CleanUp @0x001d5afc
void BombFlash::CleanUp() {
    if (!s_pPool) return;
    for (int i = s_PoolCount - 1; i >= 0; --i) {
        s_pPool[i].~BombFlash();
    }
    delete[] s_pPool;
    s_pPool      = 0;
    s_PoolCount  = 0;
    s_CurentFree = 0;
}

// v1.6.1 BombFlash::Draw @0x001d6910
//
// Binary body:
//   if (!m_pTexture) return;
//   mat = Scale44(m_Scale) * RotZ44(m_SinAngle, m_CosAngle) * GlobalTranslate44(m_Pos)
//   MatrixManager.m_World.SetCurrentMatrix(mat)
//   UploadModelViewOnly()
//   m_pTexture->Set()
//   DrawQuadUnCached(m_Colour1, NULL)
//   m_pTexture->UnSet()
void BombFlash::Draw() {
    if (!m_pTexture.IsValid()) return;

    Matrix44 mat = Matrix44::Scale44(m_Scale_x, m_Scale_y, m_Scale_z);
    mat.RotZ44(m_SinAngle, m_CosAngle);
    mat.GlobalTranslate44(m_Pos.x, m_Pos.y, m_Pos.z);

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    m_pTexture->Set();
    Mortar::Mesh::DrawQuadUnCached(m_Colour1, NULL);
    m_pTexture->UnSet();
}

// v1.6.1 BombFlash::DrawUpdate @0x001d4dc0 -- binary body is empty.
void BombFlash::DrawUpdate(float) {}

// v1.6.1 BombFlash::Init @0x001d4dbc -- binary body is empty (no-op).
void BombFlash::Init(void*, int, Vec3*) {}
