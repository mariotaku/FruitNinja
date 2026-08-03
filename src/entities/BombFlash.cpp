#include "BombFlash.h"
#include "Bomb.h"
#include <new>

// v1.6.1 BombFlash ctor @0x001d5fa8
// m_Colour0/m_Colour1/m_pTexture are member-default-constructed (opaque black /
// null SmartPtr), mirroring the binary's explicit Colour::Colour/SmartPtr::SmartPtr
// calls. m_Timer/m_SinAngle/m_CosAngle/m_Pos/m_Dir/m_Scale/m_AngleIdx are left
// uninitialized, matching the binary.
BombFlash::BombFlash() {
    m_bActive = 0;
}

// v1.6.1 BombFlash dtor @0x001d5ac0 / 0x001d5b80
// Compiler-generated member destruction releases m_pTexture's SmartPtr.
BombFlash::~BombFlash() {}

// v1.6.1 BombFlash::Init @0x001d4dbc — stub (real init logic not yet ported)
void BombFlash::Init(void*, long, _Vector3<float>*) {}

// ASM-spec v1.6.1 BombFlash::Update @ 0x001d4dd4
// UNVERIFIED: this carried an ASM-verified stamp the instruction counts do not
// support -- the port compiles to 64 instructions against the binary's 86. The
// binary builds a Vector3 temp on the stack where the port assigns m_Scale
// directly, which covers only part of the 26% shortfall. No divergence proven,
// but the stamp is not earned; needs a fresh body-level read.
// Quadratic scale-grow + linear fade-in / quadratic fade-out over a 0.6s lifetime;
// deactivates and returns the slot to the pool once m_Timer exceeds 0.6s.
void BombFlash::Update(float dt) {
    m_Timer += dt;
    float t = m_Timer / 0.6f;
    m_Scale = _Vector3<float>(50.0f * t * t + 150.0f, 200.0f * t * t + 100.0f, 0.0f);

    float maxAlpha = (float)(uint8_t)m_Colour0.a;
    float alpha;
    if (m_Timer < 0.2f) {
        alpha = maxAlpha * (m_Timer / 0.2f);
    } else if (m_Timer < 0.6f) {
        float f = (0.6f - m_Timer) / 0.4f;
        alpha = maxAlpha * f * f;
    } else {
        alpha = 0.0f;
    }
    if (alpha < 0.0f) alpha = 0.0f;
    uint8_t a8 = (alpha >= maxAlpha) ? (uint8_t)maxAlpha : (uint8_t)(int)alpha;
    m_Colour1.a = a8;

    if (m_Timer > 0.6f) {
        m_bActive = false;
        m_Timer = 0.0f;
    }
}

// v1.6.1 BombFlash::Draw @0x001d6910 — stub (draw not yet ported)
void BombFlash::Draw() {}

// v1.6.1 BombFlash::DrawUpdate @0x001d4dc0 — stub
void BombFlash::DrawUpdate(float) {}

// v1.6.1 BombFlash::CreatePool @0x001d4cc0 — DEFUNCT (bx lr).
// Port still needs a pool; allocate contiguous block of n entries.
int BombFlash::CreatePool(int n) {
    if (g_bombData.pool == nullptr && n > 0) {
        void* mem = ::operator new[](static_cast<size_t>(n) * sizeof(BombFlash));
        g_bombData.pool = static_cast<BombFlash*>(mem);
        g_bombData.poolCount = n;
        g_bombData.currentFree = 0;
        for (int i = 0; i < n; ++i) {
            new (&g_bombData.pool[i]) BombFlash();
        }
    }
    return n;
}

// v1.6.1 BombFlash::GetFree @0x001d4cc4 — circular probe from currentFree.
// Returns a free (inactive) slot, or the currentFree slot if none free.
BombFlash* BombFlash::GetFree() {
    if (!g_bombData.pool) return nullptr;

    int idx = g_bombData.currentFree;
    for (int i = 0; ; ++i) {
        BombFlash* entry = &g_bombData.pool[idx];
        if (entry->m_bActive == 0) {
            g_bombData.currentFree = idx;
            return entry;
        }
        if (i >= g_bombData.poolCount) {
            g_bombData.currentFree = idx;
            return entry;
        }
        idx = (idx + 1) % g_bombData.poolCount;
    }
}

// v1.6.1 BombFlash::MakeFlash @0x001d5bf0 — stub (not yet ported)
void BombFlash::MakeFlash(Colour /*col*/, _Vector3<float> /*pos*/, _Vector3<float> /*dir*/,
                           Mortar::SmartPtr<Mortar::Texture> /*tex*/) {}

// v1.6.1 BombFlash::UpdateActiveFlashes @0x001d4dc4 — DEFUNCT (bx lr).
void BombFlash::UpdateActiveFlashes(float /*dt*/) {}

// v1.6.1 BombFlash::DrawActiveFlashes @0x001d4dc8 — DEFUNCT (bx lr).
void BombFlash::DrawActiveFlashes() {}

// v1.6.1 BombFlash::RemoveAllFlashes @0x001d4d64 — deactivates every pool slot.
void BombFlash::RemoveAllFlashes() {
    for (int i = 0; i < g_bombData.poolCount; ++i) {
        g_bombData.pool[i].m_bActive = 0;
    }
}

// v1.6.1 BombFlash::CleanUp @0x001d5afc — destructs entries backward, frees the heap block.
void BombFlash::CleanUp() {
    if (g_bombData.pool != nullptr) {
        BombFlash* it = g_bombData.pool + g_bombData.poolCount;
        while (it != g_bombData.pool) {
            --it;
            it->~BombFlash();
        }
        ::operator delete[](static_cast<void*>(g_bombData.pool));
        g_bombData.pool = nullptr;
    }
    g_bombData.poolCount = 0;
}
