// Analysed: 2026-04-30T00:00

#include "BombFlash.h"
#include <cstring>

// Static pool array. BombFlash::CreatePool(0x20) allocates 32 entries.
static BombFlash* s_Pool[BombFlash::POOL_SIZE] = { 0 };
static bool s_PoolCreated = false;

// ctor @ 0x00171a14
BombFlash::BombFlash()
    : m_Timer(0.0f)
    , m_Colour0(0, 0, 0, 0)
    , m_Colour1(0, 0, 0, 0)
    , m_pTexture()
    , m_Scale_x(0.0f)
    , m_Scale_y(0.0f)
    , m_Scale_z(0.0f)
    , m_bActive(false)
{
    std::memset(field_0x10, 0, sizeof(field_0x10));
    std::memset(field_0x1c, 0, sizeof(field_0x1c));
}

// dtor @ 0x00171f38 / 0x00171fb8
BombFlash::~BombFlash() {}

// TODO: 0x00171038 -- quadratic scale + alpha animation over flash lifetime, then deactivate
void BombFlash::Update(float /*dt*/) {}

// @ 0x00170f84 -- stub in binary (returns param); port mirrors this behavior.
// Real pool allocation is handled by the static array sized to POOL_SIZE.
int BombFlash::CreatePool(int n) {
    // Binary stub returns param unchanged. Real pool backed by s_Pool[].
    if (!s_PoolCreated) {
        for (int i = 0; i < POOL_SIZE && i < n; ++i) {
            s_Pool[i] = new BombFlash();
        }
        s_PoolCreated = true;
    }
    return n;
}

// TODO: 0x001723f4 -- activate a pooled flash slot (find free slot, set texture/pos/colour/anim)
void BombFlash::MakeFlash(Colour /*col*/, Vec3* /*pos*/, Vec3* /*dir*/,
                           Mortar::SmartPtr<Mortar::Texture>* /*tex*/) {}

// @ 0x00171028 -- iterate pool calling Update on active slots
void BombFlash::UpdateActiveFlashes(float dt) {
    if (!s_PoolCreated) return;
    for (int i = 0; i < POOL_SIZE; ++i) {
        if (s_Pool[i] && s_Pool[i]->m_bActive) {
            s_Pool[i]->Update(dt);
        }
    }
}

// TODO: 0x0017102c -- iterate pool, call Draw on active slots
void BombFlash::DrawActiveFlashes() {}

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
}

// TODO: BombFlash::Draw -- render one active flash sprite (textured quad, animated alpha)
void BombFlash::Draw() {}
// TODO: BombFlash::DrawUpdate -- per-frame draw-state advance for one flash
void BombFlash::DrawUpdate(float) {}
// TODO: BombFlash::GetFree -- return next free pool slot for MakeFlash
void BombFlash::GetFree() {}
// TODO: BombFlash::Init -- initialise a flash slot (texture, position, anim state)
void BombFlash::Init(void*, int, Vec3*) {}
