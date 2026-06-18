#include "BombFlash.h"
#include <new>

// Contiguous pool allocation (matches v1.6.1 binary model).
// Managed by CreatePool / CleanUp. Each entry is 0x44 bytes.
static BombFlash* pool = nullptr;
static int poolCount = 0;
static int currentFree = 0;

// v1.6.1 BombFlash ctor @0x001d5fa8
BombFlash::BombFlash() {
    // ctor initializes vtable, Colour fields, SmartPtr, sets m_bActive = 0.
    // m_padBefore and m_padAfter are uninitialized junk at construction time.
    m_bActive = 0;
}

// v1.6.1 BombFlash dtor @0x001d5ac0 / 0x001d5b80
BombFlash::~BombFlash() {}

// v1.6.1 BombFlash::Init @0x001d4dbc — stub (real init logic not yet ported)
void BombFlash::Init(void*, long, Vec3*) {}

// v1.6.1 BombFlash::Update @0x001d4dd4 — stub (quadratic scale + alpha anim not yet ported)
void BombFlash::Update(float /*dt*/) {}

// v1.6.1 BombFlash::Draw @0x001d6910 — stub (draw not yet ported)
void BombFlash::Draw() {}

// v1.6.1 BombFlash::DrawUpdate @0x001d4dc0 — stub
void BombFlash::DrawUpdate(float) {}

// v1.6.1 BombFlash::CreatePool @0x001d4cc0 — DEFUNCT (bx lr).
// Port still needs a pool; allocate contiguous block of n entries.
int BombFlash::CreatePool(int n) {
    if (pool == nullptr && n > 0) {
        void* mem = ::operator new[](static_cast<size_t>(n) * sizeof(BombFlash));
        pool = static_cast<BombFlash*>(mem);
        poolCount = n;
        currentFree = 0;
        for (int i = 0; i < n; ++i) {
            new (&pool[i]) BombFlash();
        }
    }
    return n;
}

// v1.6.1 BombFlash::GetFree @0x001d4cc4 — circular probe from currentFree.
// Returns a free (inactive) slot, or the currentFree slot if none free.
BombFlash* BombFlash::GetFree() {
    if (!pool) return nullptr;

    int idx = currentFree;
    for (int i = 0; ; ++i) {
        BombFlash* entry = &pool[idx];
        if (entry->m_bActive == 0) {
            currentFree = idx;
            return entry;
        }
        if (i >= poolCount) {
            currentFree = idx;
            return entry;
        }
        idx = (idx + 1) % poolCount;
    }
}

// v1.6.1 BombFlash::MakeFlash @0x001d5bf0 — stub (not yet ported)
void BombFlash::MakeFlash(Colour /*col*/, Vec3 /*pos*/, Vec3 /*dir*/,
                           Mortar::SmartPtr<Mortar::Texture> /*tex*/) {}

// v1.6.1 BombFlash::UpdateActiveFlashes @0x001d4dc4 — DEFUNCT (bx lr).
void BombFlash::UpdateActiveFlashes(float /*dt*/) {}

// v1.6.1 BombFlash::DrawActiveFlashes @0x001d4dc8 — DEFUNCT (bx lr).
void BombFlash::DrawActiveFlashes() {}

// v1.6.1 BombFlash::RemoveAllFlashes @0x001d4d64 — deactivates every pool slot.
void BombFlash::RemoveAllFlashes() {
    for (int i = 0; i < poolCount; ++i) {
        pool[i].m_bActive = 0;
    }
}

// v1.6.1 BombFlash::CleanUp @0x001d5afc — destructs entries backward, frees the heap block.
void BombFlash::CleanUp() {
    if (pool != nullptr) {
        BombFlash* it = pool + poolCount;
        while (it != pool) {
            --it;
            it->~BombFlash();
        }
        ::operator delete[](static_cast<void*>(pool));
        pool = nullptr;
    }
    poolCount = 0;
}
