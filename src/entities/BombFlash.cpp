// Analysed: 2026-04-30T00:00

#include "BombFlash.h"
#include "math/MathUtil.h"
#include "math/Matrix44.h"
#include "asset/Mesh.h"
#include "render/MatrixManager.h"
#include <cstring>

// Static pool array. BombFlash::CreatePool(0x20) allocates 32 entries.
//
// DIFFERS: the binary keeps the pool as a single heap array of BombFlash[count]
// (a base pointer + count + rotating "next free" index in three globals) and
// indexes it by `base + i*0x44`. The port keeps an array of owning pointers so
// the per-element ctor/dtor stay ordinary C++ object lifetimes. The rotating
// "next free" index (s_NextFree) and count (s_PoolCreated/POOL_SIZE) are
// preserved so GetFree/MakeFlash behave identically.
static BombFlash* s_Pool[BombFlash::POOL_SIZE] = { 0 };
static bool s_PoolCreated = false;

// Binary GetFree (0x170f88) holds the rotating slot index in a global; mirror it.
static int s_NextFree = 0;

// ---------------------------------------------------------------------------
// Update animation constants (binary literal pool @ 0x0017114c..0x0017116c).
// ---------------------------------------------------------------------------
static const float FLASH_LIFE        = 0.6f;   // DAT_0017114c -- total lifetime / divisor
static const float SCALE_X_GROW       = 50.0f;  // DAT_00171150
static const float SCALE_Y_GROW       = 200.0f; // DAT_00171154
static const float TIMER_RESET        = 0.0f;   // DAT_00171158
static const float SCALE_Y_BASE       = 100.0f; // DAT_0017115c
static const float SCALE_X_BASE       = 150.0f; // DAT_00171160
static const float FADE_PEAK_TIME     = 0.2f;   // DAT_00171164 / DAT_00171168 -- fade-in -> fade-out boundary
static const float FADE_OUT_SPAN      = 0.4f;   // DAT_0017116c -- (FLASH_LIFE - FADE_PEAK_TIME)

// ---------------------------------------------------------------------------
// MakeFlash constants (binary literal pool @ 0x001724f0..0x00172500).
// ---------------------------------------------------------------------------
static const float MF_TIMER_INIT      = 0.0f;     // DAT_001724f0 -- initial m_Timer, also x-clamp pivot
static const float MF_POS_Z           = -5400.0f; // DAT_001724f4 -- written to m_Pos.z
static const float MF_X_NEG           = -240.0f;  // DAT_001724f8 -- m_Pos.x when pos.x < 0
static const float MF_SCALE           = 128.0f;   // DAT_001724fc -- initial uniform scale
static const float MF_X_POS           = 240.0f;   // DAT_00172500 -- m_Pos.x when pos.x >= 0
static const float MF_DIR_MUL         = 5.0f;     // 0x40f00000 -- dir multiplier added to pos

// ctor @ 0x00171a14
BombFlash::BombFlash()
    : m_Timer(0.0f)
    , m_Colour0()          // binary uses default Colour::Colour() (owned by Colour.h)
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

// dtor @ 0x00171f38 / 0x00171fb8
BombFlash::~BombFlash() {}

// Binary @ 0x00171038 -- quadratic scale + alpha animation over the flash
// lifetime, then deactivate.
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
    m_Scale_z = TIMER_RESET;   // DAT_00171158 == 0.0

    uint8_t curAlpha;
    if (FADE_PEAK_TIME <= m_Timer) {
        if (m_Timer == FADE_PEAK_TIME) {
            // Exactly at the peak: full max alpha.
            curAlpha = m_Colour0.a;
        } else {
            // Fade-out: quadratic falloff.
            float k = (FLASH_LIFE - m_Timer) / FADE_OUT_SPAN;
            float a = maxA * k * k;
            if (a <= 0.0f) {
                curAlpha = 0;
            } else {
                // Clamp a to [0, maxA].
                if (maxA <= a) {
                    curAlpha = (maxA > 0.0f) ? (uint8_t)(int)maxA : 0;
                } else {
                    curAlpha = (a > 0.0f) ? (uint8_t)(int)a : 0;
                }
            }
        }
    } else {
        // Fade-in: linear ramp.
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
        m_Timer = TIMER_RESET;   // DAT_00171158 == 0.0
    }
}

// @ 0x00170f84 -- stub in binary (returns param); port mirrors this behavior.
// Real pool allocation is handled by the static array sized to POOL_SIZE.
int BombFlash::CreatePool(int n) {
    // Binary stub returns param unchanged. Real pool backed by s_Pool[].
    if (!s_PoolCreated) {
        for (int i = 0; i < POOL_SIZE && i < n; ++i) {
            s_Pool[i] = new BombFlash();
        }
        s_PoolCreated = true;
        s_NextFree = 0;
    }
    return n;
}

// Binary @ 0x00170f88 -- return the next free pool slot.
//
// Binary walks the pool from the rotating index, advancing (index+1) % count
// past any active slot, capped at `count` iterations. On the cap it returns the
// slot it last landed on (even if active); the rotating index is stored back so
// the next call resumes from there. Returns NULL if the pool is empty.
BombFlash* BombFlash::GetFree() {
    if (!s_PoolCreated) return 0;

    int index = s_NextFree;
    int iterations = 0;
    BombFlash* slot = s_Pool[index];
    while (slot && slot->m_bActive) {
        if (POOL_SIZE <= iterations) break;
        index = (index + 1) % POOL_SIZE;
        slot = s_Pool[index];
        ++iterations;
    }
    s_NextFree = index;
    return slot;
}

// Binary @ 0x001723f4 -- activate a pooled flash slot.
//
// NOTE: the `col` parameter is dead in the binary (MakeFlash never writes the
// flash colour; it stays at the ctor default). It is kept in the signature for
// call-shape parity. The slot's colours are whatever the ctor set.
//
// Binary body:
//   slot->m_pTexture = *tex
//   slot->m_Pos = *pos;  slot->m_Pos.z = -5400         (DAT_001724f4)
//   slot->m_Dir = *dir
//   slot->m_Pos += dir * 5.0
//   slot->m_Pos.x = (pos.x < 0) ? -240 : 240           (clamp to screen half)
//   slot->m_Scale = (128, 128, 128)
//   idx = Atan2Idx(dir.x, -dir.y)
//   slot->m_Timer = 0;  slot->m_bActive = 1;  slot->m_AngleIdx = idx
//   slot->m_SinAngle = SinIdx(idx);  slot->m_CosAngle = CosIdx(idx)
//   slot->Update(0)   (vtable slot 1; primes scale/alpha for frame 0)
void BombFlash::MakeFlash(Colour /*col*/, Vec3* pos, Vec3* dir,
                           Mortar::SmartPtr<Mortar::Texture>* tex) {
    BombFlash* slot = GetFree();
    if (!slot) return;

    slot->m_pTexture = *tex;

    slot->m_Pos = *pos;
    slot->m_Pos.z = MF_POS_Z;   // DAT_001724f4 == -5400
    slot->m_Dir = *dir;

    // m_Pos += dir * 5
    slot->m_Pos.x += dir->x * MF_DIR_MUL;
    slot->m_Pos.y += dir->y * MF_DIR_MUL;
    slot->m_Pos.z += dir->z * MF_DIR_MUL;

    // Clamp x to a fixed screen-edge position based on its sign.
    slot->m_Pos.x = (slot->m_Pos.x < MF_TIMER_INIT) ? MF_X_NEG : MF_X_POS;

    slot->m_Scale_x = MF_SCALE;
    slot->m_Scale_y = MF_SCALE;
    slot->m_Scale_z = MF_SCALE;

    uint16_t idx = (uint16_t)Math::Atan2Idx(slot->m_Dir.x, -slot->m_Dir.y);

    slot->m_Timer    = MF_TIMER_INIT;   // DAT_001724f0 == 0.0
    slot->m_bActive  = true;
    slot->m_AngleIdx = idx;

    slot->m_SinAngle = SinIdx(idx);
    slot->m_CosAngle = CosIdx(idx);

    // Binary calls vtable slot 1 (Update) with dt=0 to prime frame-0 state.
    slot->Update(MF_TIMER_INIT);
}

// @ 0x00171028 -- iterate pool calling Update on active slots.
//
// DIFFERS: the binary BombFlash::UpdateActiveFlashes thunk (0x171028) is an
// empty no-op; the live per-frame iteration sits in the Game update path. The
// port consolidates that iteration here so GameInit can call one entry point.
void BombFlash::UpdateActiveFlashes(float dt) {
    if (!s_PoolCreated) return;
    for (int i = 0; i < POOL_SIZE; ++i) {
        if (s_Pool[i] && s_Pool[i]->m_bActive) {
            s_Pool[i]->Update(dt);
        }
    }
}

// @ 0x0017102c -- iterate pool calling Draw on active slots.
//
// DIFFERS: the binary BombFlash::DrawActiveFlashes thunk (0x17102c) is an empty
// no-op; the live per-frame iteration sits in GameDraw (0x16baf0). The port
// consolidates that iteration here so GameDraw can call one entry point.
void BombFlash::DrawActiveFlashes() {
    if (!s_PoolCreated) return;
    for (int i = 0; i < POOL_SIZE; ++i) {
        if (s_Pool[i] && s_Pool[i]->m_bActive) {
            s_Pool[i]->Draw();
        }
    }
}

// @ 0x00170fe4 -- deactivate every pool slot
void BombFlash::RemoveAllFlashes() {
    if (!s_PoolCreated) return;
    for (int i = 0; i < POOL_SIZE; ++i) {
        if (s_Pool[i]) s_Pool[i]->m_bActive = false;
    }
}

// @ 0x00171f64 -- destructs each entry, frees backing memory
void BombFlash::CleanUp() {
    if (!s_PoolCreated) return;
    for (int i = POOL_SIZE - 1; i >= 0; --i) {
        delete s_Pool[i];
        s_Pool[i] = 0;
    }
    s_PoolCreated = false;
    s_NextFree = 0;
}

// Binary @ 0x00171B54 -- render one active flash sprite.
//
// Binary body:
//   if (!m_pTexture) return;
//   mat = Scale44(m_Scale) * RotZ44(m_SinAngle, m_CosAngle) * GlobalTranslate44(m_Pos)
//   MatrixManager.m_World.SetCurrentMatrix(mat)
//   UploadModelViewOnly()                         (UploadMatrices_Coin @ 0x1719f0)
//   m_pTexture->Set()
//   DrawQuadUnCached(m_Colour1, NULL)             (animated-alpha tint)
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

// Binary @ 0x00171024 -- per-frame draw-state advance. Binary body is empty.
void BombFlash::DrawUpdate(float) {}

// Binary @ 0x00171020 -- initialise a flash slot. Binary body is empty (no-op).
void BombFlash::Init(void*, int, Vec3*) {}
