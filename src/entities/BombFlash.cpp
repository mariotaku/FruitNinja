// Analysed: 2026-04-30T00:00

#include "BombFlash.h"
#include <cstring>

// Static pool array. BombFlash::CreatePool(0x20) allocates 32 entries.
static BombFlash* s_Pool[BombFlash::POOL_SIZE] = { nullptr };
static bool s_PoolCreated = false;

// ctor @ 0x00171a14
BombFlash::BombFlash() {
    m_bActive = 0;
    std::memset(m_pad, 0, sizeof(m_pad));
}

// dtor @ 0x00171f38 / 0x00171fb8
BombFlash::~BombFlash() {}

// @ 0x00171038 — stub: quadratic scale + alpha anim (TODO: real impl pending)
void BombFlash::Update(float /*dt*/) {}

// @ 0x00170f84 — stub in binary (returns param); port mirrors this behavior.
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

// @ 0x001723f4 — activate a pooled flash slot (TODO: real impl pending)
void BombFlash::MakeFlash(Colour /*col*/, Vec3* /*pos*/, Vec3* /*dir*/,
                           Mortar::SmartPtr<Mortar::Texture>* /*tex*/) {}

// @ 0x00171028 — iterate pool calling Update on active slots
void BombFlash::UpdateActiveFlashes(float dt) {
    if (!s_PoolCreated) return;
    for (int i = 0; i < POOL_SIZE; ++i) {
        if (s_Pool[i] && s_Pool[i]->m_bActive) {
            s_Pool[i]->Update(dt);
        }
    }
}

// @ 0x0017102c — iterate pool calling Draw on active slots (TODO: real draw)
void BombFlash::DrawActiveFlashes() {}

// @ 0x00170fe4 — deactivate every pool slot
void BombFlash::RemoveAllFlashes() {
    if (!s_PoolCreated) return;
    for (int i = 0; i < POOL_SIZE; ++i) {
        if (s_Pool[i]) s_Pool[i]->m_bActive = 0;
    }
}

// @ 0x00171f64 — destructs each entry, frees backing memory
void BombFlash::CleanUp() {
    if (!s_PoolCreated) return;
    for (int i = POOL_SIZE - 1; i >= 0; --i) {
        delete s_Pool[i];
        s_Pool[i] = nullptr;
    }
    s_PoolCreated = false;
}

// ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----
// STUB: BombFlash::Draw -- auto stub
void BombFlash::Draw() {}
// STUB: BombFlash::DrawUpdate -- auto stub
void BombFlash::DrawUpdate(float) {}
// STUB: BombFlash::GetFree -- auto stub
void BombFlash::GetFree() {}
// STUB: BombFlash::Init -- auto stub
void BombFlash::Init(void*, int, Vec3*) {}
// ---- end AUTO-STUB MERGE ----
