#include "ActorManager.h"
#include "../engine/util/Delegate.h"
#include "Fruit.h"
#include "Bomb.h"
#include "BombBlast.h"
#include "collision/ColAABB.h"
#include "render/Renderer.h"
#include "debug/Logger.h"
#include <cstdio>
#include <cstring>

// Binary-faithful ActorManager port.
// Addresses tagged in method comments reference v1.6.1 FruitNinja.exe. The whole
// TU lives at 0x001d2e80-0x001d4228; the older 0x0016fb..-0x00170.. addresses that
// used to be cited here were the v1.5.1 layout of the same TU (identical ordinal
// order) and have been remapped.
//
// LIVENESS METHOD NOTE: this binary is PIC, so Ghidra's direct xrefs are useless --
// every intra-image call routes through `.plt`. The liveness test for "is this
// method ever called" is therefore "does the symbol have a .plt thunk". A `.got`
// self-slot is just the PIC prologue and proves nothing either way.

namespace Mortar {

ActorManager* ActorManager::s_Instance = nullptr;

// --- Construction / singleton --------------------------------------------

ActorManager::ActorManager()
    : m_pHeap(nullptr)
    , m_HeapSize(0)
    , m_FreeCount(0)
    , m_PendingDeactCount(0)
    , m_pTypeLists(nullptr)
    , m_NumTypes(0)
    , m_DebugDraw(false)
    , m_FactoryDelegate(nullptr)
    , m_HashDelegate(nullptr)
{
    // Binary ctors @0x001d3d68 / @0x001d3e14 (base/complete): construct the
    // delegates (default/empty state) then clear m_Listeners; m_Listeners was
    // default-constructed by the compiler, clear() matches the binary epilogue.
    // (dtors @0x001d3b9c / @0x001d3c20.)
    m_Listeners.clear();
}

ActorManager::~ActorManager() {
    // Binary dtor calls Destroy() then tears down delegates / listener list.
    Destroy();
    if (s_Instance == this) s_Instance = nullptr;
}

ActorManager* ActorManager::GetInstance() {
    // Binary: Meyers singleton, v1.6.1 ActorManager::GetInstance @0x001d3ec0, with
    // __cxa_guard_acquire.
    // Port: lazy-init via static pointer (single-thread SDL, no guard needed).
    if (s_Instance == nullptr) {
        s_Instance = new ActorManager();
    }
    return s_Instance;
}

// --- Lifecycle ------------------------------------------------------------

// v1.6.1 ActorManager::Initialise @0x001d3ca4. Allocates a LinkedHeap
// (`operator new(0x24)`) sized `heapSize`, then carves the per-type list array
// out of it via `LinkedHeap::Allocate((numTypes + 1) * 8, 0x2841bd)` -- note the
// binary reserves N+1 slots but only placement-constructs N std::lists (8 bytes
// each on Sourcery 2010q1), i.e. one slot of slack is allocated and never used.
// The body ENDS with a discarded `Entity::HeapExist()` call.
// Port drops LinkedHeap and uses new[] (exactly N lists -- the binary's spare
// slot is never indexed, so N vs N+1 is not observable); m_pHeap becomes a
// non-null sentinel so the null-guard in Update/Draw behaves as in the binary.
// ASM-verified: 2026-07-28T00:00Z v1.6.1 ActorManager::Initialise @ 0x001d3ca4 (re-analyst)
void ActorManager::Initialise(int numTypes, int heapSize) {
    // DIFFERS: original = no re-entry guard (v1.6.1 ActorManager::Initialise
    // @0x001d3ca4 unconditionally allocates a fresh LinkedHeap + list array on
    // every call), using an early-out because the PORT has two Initialise call
    // sites where the binary has one: GameInitialise.cpp:218 and GameInit.cpp:182
    // (binary GameInit @0x001ce1c0) both call Initialise(7, 0x2000), the former
    // first. Without the guard the second call would leak the first m_pTypeLists
    // array and orphan anything already registered. Remove this guard only
    // together with the duplicate GameInitialise.cpp call site.
    if (m_pHeap != nullptr) return;
    m_HeapSize    = heapSize;
    m_NumTypes    = numTypes;
    m_pTypeLists  = new std::list<Entity*>[numTypes];
    m_pHeap       = this;  // non-null sentinel
    Entity::HeapExist();   // binary tail call; return value discarded
}

// v1.6.1 ActorManager::Destroy @0x001d3b44.
// ASM-verified: 2026-07-28T00:00Z v1.6.1 ActorManager::Destroy @ 0x001d3b44 (re-analyst)
void ActorManager::Destroy() {
    m_DebugDraw = false;
    Clear();
    if (m_pTypeLists) {
        delete[] m_pTypeLists;
        m_pTypeLists = nullptr;
    }
    m_pHeap    = nullptr;
    m_HeapSize = 0;
    m_FreeCount = 0;
    m_PendingDeactCount = 0;
}

// v1.6.1 ActorManager::Clear @0x001d3690. Delete all entities in type lists + free pool.
// ASM-verified: 2026-04-29T00:00Z v1.6.1 ActorManager::Clear @ 0x001d3690 (asm-inspector)
void ActorManager::Clear() {
    if (m_pTypeLists) {
        for (int t = 0; t < m_NumTypes; t++) {
            std::list<Entity*>& list = m_pTypeLists[t];
            for (std::list<Entity*>::iterator it = list.begin(); it != list.end(); ++it) {
                Entity* e = *it;
                if (e && !(e->flags & ENT_NO_DESTRUCT)) {
                    e->Release();
                    delete e;
                }
            }
            list.clear();
        }
    }
    for (int i = 0; i < m_FreeCount; i++) {
        Entity* e = m_FreePool[i];
        if (e && !(e->flags & ENT_NO_DESTRUCT)) {
            e->Release();
            delete e;
        }
    }
    std::memset(m_FreePool, 0, sizeof(m_FreePool));
    m_FreeCount = 0;
}

// --- Entity API -----------------------------------------------------------

// v1.6.1 ActorManager::Add(long, bool) @0x001d3fac. Binary-faithful recycle-first Add.
// This is the overload LoadEntity @0x001d408c calls (PLT thunk 0x001168e4).
// ASM-spec v1.6.1 ActorManager::Add(long,bool) @ 0x001d3fac: 36 insns. No heap/typelist null check, no entityType range check, no candidate null check -- reverse-scans m_FreePool[m_FreeCount-1..0] on entityType (+0x35, byte), push_back, m_FreeCount--, compact, Entity::Activate. Factory path calls Delegate1::operator() unconditionally; the unbound guard lives in Delegate1<Entity*,long>::Call @0x001d4798 (subs r3,r0,#0; cpyeq r0,r3; ret).
Entity* ActorManager::Add(int entityType, bool /*unused — dead param in binary*/) {
    // --- Recycle path: reverse-scan free pool for matching type. ---
    for (int i = m_FreeCount - 1; i >= 0; i--) {
        Entity* candidate = m_FreePool[i];
        if (candidate->entityType == entityType) {
            // push_back BEFORE decrementing count (matches binary ordering
            // inside ActorManager::Add(long,bool) @0x001d3fac).
            m_pTypeLists[entityType].push_back(candidate);
            m_FreeCount--;
            // Compact pool: shift [i+1..m_FreeCount] one slot left. Binary
            // stops here -- the vacated top slot keeps its stale duplicate,
            // it is never re-nulled.
            for (int j = i; j < m_FreeCount; j++)
                m_FreePool[j] = m_FreePool[j + 1];
            // Mortar::Entity::Activate: flags &= 0xFE (clear bit 0 only) — only
            // ENT_INACTIVE (bit 0) is cleared; ENT_KILLED is NOT touched.
            // ASM-verified: 2026-04-28T15:55Z v1.6.1 Mortar::Entity::Activate @ 0x001d45f8 (asm-inspector)
            candidate->flags &= ~ENT_INACTIVE;
            return candidate;
        }
    }

    // --- Factory path: pool empty or no matching type. ---
    // Genuine guard: binary calls Delegate1<Entity*,long>::operator() unconditionally
    // @0x001d4050, but the unbound-impl check lives one level down in
    // Delegate1<Entity*,long>::Call @0x001d4798 (subs r3,r0,#0; cpyeq r0,r3; ret).
    // This is a faithful relocation of that check. LOG_WARN is port-only and
    // compiles out under __bada__.
    if (!m_FactoryDelegate) {
        LOG_WARN("ACTOR/Add", "no factory registered (type %d)", entityType);
        return nullptr;
    }
    Entity* entity = m_FactoryDelegate(entityType);
    // Genuine guard: `cmp r0,#0 ; str r0,[sp,#4] ; beq 0x001d4084` @0x001d4054.
    if (entity == nullptr) return nullptr;
    m_pTypeLists[entityType].push_back(entity);
    entity->entityType    = entityType;   // binary: store at +0x35
    entity->m_RecycleFlag = 0;            // binary: store at +0x34
    // Factory path does NOT call Entity::Activate — the fresh entity comes
    // out of its ctor with flags=0 which already satisfies `(flags & 0x11) == 0`.
    return entity;
}

// v1.6.1 ActorManager::Add(Entity*, long) @0x001d3f54. Push an already-built
// entity into its type list.
// Defunct: zero in-binary callers -- this symbol has NO .plt thunk at all, so
//   nothing in the image can reach it (LoadEntity @0x001d408c calls the OTHER
//   overload, Add(long,bool) @0x001d3fac via PLT 0x001168e4 -- not this one);
//   no-op-equivalent stub; v1.6.1 ActorManager::Add(Entity*,long) @ 0x001d3f54.
// ASM-verified: 2026-04-29T00:00Z v1.6.1 ActorManager::Add(Entity*,long) @ 0x001d3f54 (asm-inspector)
Entity* ActorManager::Add(Entity* entity, long typeIdx) {
    if (!entity) return nullptr;
    if (typeIdx < 0 || typeIdx >= (long)m_NumTypes || m_pTypeLists == nullptr) return nullptr;
    m_pTypeLists[typeIdx].push_back(entity);
    entity->entityType    = (int)typeIdx;
    entity->m_RecycleFlag = 0;
    return entity;
}

// v1.6.1 ActorManager::Deactivate @0x001d3854. Binary walks the type list,
// erases, pushes to free pool. No flag manipulation.
// DIFFERS: m_FreeCount < FREE_POOL_CAP guard kept defensively; binary lacks bound check.
void ActorManager::Deactivate(Entity* entity) {
    if (!entity || m_pTypeLists == nullptr) return;
    const int type = entity->entityType;
    if (type < 0 || type >= m_NumTypes) return;
    std::list<Entity*>& list = m_pTypeLists[type];
    list.remove(entity);
    if (m_FreeCount < FREE_POOL_CAP) {
        m_FreePool[m_FreeCount++] = entity;
    } else {
        delete entity;
    }
}

// v1.6.1 ActorManager::Remove @0x001d3a44. Binary calls vtable+0xc (Release)
// then operator delete.
// Defunct: zero in-binary callers (no .plt thunk); v1.6.1 ActorManager::Remove @ 0x001d3a44.
// ASM-verified: 2026-04-29T00:00Z v1.6.1 ActorManager::Remove @ 0x001d3a44 (asm-inspector)
void ActorManager::Remove(Entity* entity) {
    if (!entity || m_pTypeLists == nullptr) return;
    const int type = entity->entityType;
    if (type < 0 || type >= m_NumTypes) return;
    std::list<Entity*>& list = m_pTypeLists[type];
    for (std::list<Entity*>::iterator it = list.begin(); it != list.end(); ++it) {
        if (*it == entity) {
            if (!(entity->flags & ENT_NO_DESTRUCT)) {
                entity->Release();
                delete entity;
            }
            list.erase(it);
            return;
        }
    }
}

// v1.6.1 ActorManager::DeactivateAllEntities @0x001d2e94.
// Defunct: zero in-binary callers (no .plt thunk); v1.6.1 ActorManager::DeactivateAllEntities @ 0x001d2e94.
// ASM-spec v1.6.1 ActorManager::DeactivateAllEntities @ 0x001d2e94: 27 insns. Indexes m_pTypeLists[typeIdx] with no null check, no range check, and ORs 0x10 into (*it)->flags (+0xc) with no null check on the entity.
void ActorManager::DeactivateAllEntities(int typeIdx) {
    for (std::list<Entity*>::iterator it = m_pTypeLists[typeIdx].begin();
         it != m_pTypeLists[typeIdx].end(); ++it) {
        (*it)->flags |= ENT_KILLED;
    }
}

// --- Per-frame update / draw ---------------------------------------------

// v1.6.1 ActorManager::Update @0x001d38f0. Binary queues kills into
// m_PendingDeact[] during the type sweep and drains after, so std::list
// iteration is never invalidated by a mid-loop erase.
// Binary disasm inside @0x001d38f0: `orr r2, r2, #0xc; strb r2, [r6, #0xc]`
// sets ENT_TICK_DISPATCHED (bits 2+3, 0xc) BEFORE Update dispatch.
// Binary leaves the bits set permanently after PostUpdate -- sticky advisory, never cleared.
// ASM-spec v1.6.1 ActorManager::Update @ 0x001d38f0: the per-entity load is
// 'ldr r6,[r3,#0x8]; ldrb r3,[r6,#0xc]' -- the Entity* is dereferenced with no
// null test. The pending-deact append @0x001d39b4-0x001d39c8 is unconditional
// after 'tst r3,#0x10', so there is no m_PendingDeactCount bound check either.
// The two top-of-body gates (m_pHeap @0x001d3914, m_pTypeLists @0x001d3924) ARE
// genuine.
void ActorManager::Update(float dt) {
    if (m_pHeap == nullptr || m_pTypeLists == nullptr) return;

    m_PendingDeactCount = 0;

    for (int t = 0; t < m_NumTypes; t++) {
        std::list<Entity*>& list = m_pTypeLists[t];
        for (std::list<Entity*>::iterator it = list.begin(); it != list.end(); ++it) {
            Entity* e = *it;
            if ((e->flags & ENT_SKIP_MASK) == 0) {
                e->flags |= ENT_TICK_DISPATCHED;  // 0xc -- both halves set atomically per binary
                e->Update(dt);
                e->PostUpdate(dt);  // vtable +0x18 -- Bomb uses this to track fuse emitter
                // Binary leaves ENT_TICK_DISPATCHED set after PostUpdate (sticky, never cleared).
            }
            if (e->flags & ENT_KILLED) {
                m_PendingDeact[m_PendingDeactCount++] = e;
            }
        }
    }

    for (int i = 0; i < m_PendingDeactCount; i++) {
        Deactivate(m_PendingDeact[i]);
    }
    m_PendingDeactCount = 0;
}

// v1.6.1 ActorManager::Draw @0x001d3380. Calls DrawDebug (PLT 0x0010c884) via a
// plain bl, not a tail call.
// ASM-spec v1.6.1 ActorManager::Draw @ 0x001d3380: 52 insns. Two genuine top-of-body gates -- m_pHeap (+0x00) and m_pTypeLists (+0x1010) -- then per entity 'tst flags,#0x11; bne' with NO null check, then 'ldrb m_DebugDraw (+0x1020)' gating a bl to DrawDebug @0x0010c884.
void ActorManager::Draw(Renderer& r) {
    if (m_pHeap == nullptr || m_pTypeLists == nullptr) return;
    for (int t = 0; t < m_NumTypes; t++) {
        std::list<Entity*>& list = m_pTypeLists[t];
        for (std::list<Entity*>::iterator it = list.begin(); it != list.end(); ++it) {
            Entity* e = *it;
            if ((e->flags & ENT_SKIP_MASK) == 0) {
                e->Draw(r);
            }
        }
    }
    if (m_DebugDraw) DrawDebug();
}

// v1.6.1 ActorManager::DrawDebug @0x001d32e0. NOT defunct -- it has a .plt thunk
// (0x0010c884) and its caller is ActorManager::Draw @0x001d3380. It is
// debug-GATED (m_DebugDraw), not dead code; the earlier "zero in-binary callers"
// note was wrong (it read the PIC .got self-slot as the only reference).
//
// Binary body: `if (m_DebugDraw) for each type list, for each entity:
//               Col* c = e->m_Col (+0x38); if (c) c->vtable[+0x10]();`
// Col vtable +0x10 is slot 4 == Col::DrawDebug, which the port implements on
// ColAABB / ColLine / ColSphere -- so this is ported 1:1. Draw already applies
// the m_DebugDraw gate at its call site, matching the binary's observable
// behaviour either way; nothing in the port calls SetCollisionVisible, so this
// loop never runs in a normal session.
// ASM-spec v1.6.1 ActorManager::DrawDebug @ 0x001d32e0: the only top-of-body test
// is 'ldrb r3,[r0,#0x1020]; cmp r3,#0; bne' (m_DebugDraw). 'ldr r0,[r5,#0x1010]'
// has no cmp, and the per-entity sequence 'ldr r3,[r3,#0x8]; ldr r0,[r3,#0x38];
// cmp r0,#0' tests only m_Col -- the Entity* itself is never null-checked.
void ActorManager::DrawDebug() {
    if (!m_DebugDraw) return;
    for (int t = 0; t < m_NumTypes; t++) {
        std::list<Entity*>& list = m_pTypeLists[t];
        for (std::list<Entity*>::iterator it = list.begin(); it != list.end(); ++it) {
            Entity* e = *it;
            if (e->m_Col) e->m_Col->DrawDebug();
        }
    }
}

// v1.6.1 ActorManager::PostLoad @0x001d3258 -- iterates m_pTypeLists and calls
// entity->vtable[+0x1c] (slot 7, PostLoad).
// Defunct: zero in-binary callers (no .plt thunk); LoadEntity @0x001d408c, which
//   would be its caller, is itself unreachable; no-op stub;
//   v1.6.1 ActorManager::PostLoad @ 0x001d3258.
void ActorManager::PostLoad() {
}

// --- Query API ------------------------------------------------------------

// v1.6.1 ActorManager::GetNumEntities(long) @0x001d3544. Binary returns list
// size with NO active filtering.
// ASM-verified: 2026-04-29T00:00Z v1.6.1 ActorManager::GetNumEntities(long) @ 0x001d3544 (asm-inspector)
int ActorManager::GetNumEntities(int typeIdx) {
    if (!m_pTypeLists || typeIdx < 0 || typeIdx >= m_NumTypes) return 0;
    return (int)m_pTypeLists[typeIdx].size();
}

// v1.6.1 ActorManager::GetNumEntities() @0x001d3554.
// ASM-spec v1.6.1 ActorManager::GetNumEntities() @ 0x001d3554: the body walks all
// m_NumTypes lists summing size(); there is no m_pTypeLists null test. (The old
// ASM-verified stamp predates that read and covered a port-added guard.)
int ActorManager::GetNumEntities() {
    int total = 0;
    for (int t = 0; t < m_NumTypes; t++) total += (int)m_pTypeLists[t].size();
    return total;
}

// v1.6.1 ActorManager::GetNumEntities(long*) @0x001d349c. Sentinel is -1L, not 0
// (type 0 == Bomb would be skipped wrongly) -- confirmed against v1.6.1.
// ASM-spec v1.6.1 ActorManager::GetNumEntities(long*) @ 0x001d349c: 17 insns. Walks the array summing m_pTypeLists[*p].size() until 'cmn r3,#0x1' hits the -1L sentinel. No m_pTypeLists null check, no pointer null check, no per-entry range check.
int ActorManager::GetNumEntities(const long* typeIdxNullTerminated) {
    int total = 0;
    for (const long* p = typeIdxNullTerminated; *p != -1L; ++p) {
        total += (int)m_pTypeLists[*p].size();
    }
    return total;
}

// v1.6.1 ActorManager::GetNumEntities(long,long) @0x001d34e0.
// ASM-verified: 2026-04-29T00:00Z v1.6.1 ActorManager::GetNumEntities(long,long) @ 0x001d34e0 (asm-inspector)
int ActorManager::GetNumEntities(long typeA, long typeB) {
    if (!m_pTypeLists) return 0;
    long lo = typeA < typeB ? typeA : typeB;
    long hi = typeA < typeB ? typeB : typeA;
    if (lo < 0) lo = 0;
    if (hi > m_NumTypes) hi = m_NumTypes;
    int total = 0;
    for (long t = lo; t < hi; t++) total += (int)m_pTypeLists[t].size();
    return total;
}

// v1.6.1 ActorManager::GetNumTypes @0x001d3454.
// ASM-spec v1.6.1 ActorManager::GetNumTypes @ 0x001d3454: counts the non-empty
// lists with no m_pTypeLists null test. (The old ASM-verified stamp predates that
// read and covered a port-added guard.)
int ActorManager::GetNumTypes() {
    int n = 0;
    for (int t = 0; t < m_NumTypes; t++) if (!m_pTypeLists[t].empty()) n++;
    return n;
}

// Matches v1.6.1 ActorManager::GetEntityFirst @0x001d2f48. Seeds `it` with the
// type list's begin() iterator and returns the Entity* it points at, or
// nullptr if the list is empty. Binary also returns the ActorManager*
// via an outer CONCAT that callers ignore.
// ASM-spec v1.6.1 ActorManager::GetEntityFirst @ 0x001d2f48: 14 insns -- begin(), end(), operator!=, and an NE-predicated load of *it. No m_pTypeLists null test and no typeIdx range test.
Entity* ActorManager::GetEntityFirst(int typeIdx, std::list<Entity*>::iterator& it) {
    std::list<Entity*>& list = m_pTypeLists[typeIdx];
    it = list.begin();
    if (it == list.end()) return nullptr;
    return *it;
}

// Matches v1.6.1 ActorManager::GetEntityNext @0x001d2f00. Advances `it` and
// returns the next Entity*, or nullptr when past end.
// ASM-spec v1.6.1 ActorManager::GetEntityNext @ 0x001d2f00: 18 insns -- begin(), end(), operator!=, and an NE-predicated load of *it. No m_pTypeLists null test and no typeIdx range test.
Entity* ActorManager::GetEntityNext(int typeIdx, std::list<Entity*>::iterator& it) {
    std::list<Entity*>& list = m_pTypeLists[typeIdx];
    ++it;
    if (it == list.end()) return nullptr;
    return *it;
}

// ASM-spec v1.6.1 ActorManager::GetEntity @0x001d30d4 (29 instructions):
//   r5 = typeIdx << 3 (std::list is 8 bytes); the body indexes
//   m_pTypeLists[typeIdx] (ldr [this,#0x1010] + r5) twice -- once for begin(),
//   once for end() -- then walks the list counting up to `slot` and returns *it.
//   Falling off the end returns 0 (the loop exits on operator!= == false and
//   that same 0 is the return value, @0x001d3140/@0x001d3144).
// There is NO typeIdx range check, NO m_pTypeLists null check and NO
// `slot >= size()` pre-test in the binary -- an earlier port pass added all
// three, which cannot fit in the 29-instruction body.
Entity* ActorManager::GetEntity(int typeIdx, size_t slot) const {
    std::list<Entity*>& list = m_pTypeLists[typeIdx];
    std::list<Entity*>::iterator it  = list.begin();
    std::list<Entity*>::iterator end = list.end();
    size_t i = 0;
    for (; it != end; ++it, ++i) {
        if (i == slot) return *it;
    }
    return nullptr;
}

// v1.6.1 ActorManager::GetEntityIdx @0x001d3044.
// Defunct: zero in-binary callers (no .plt thunk); v1.6.1 ActorManager::GetEntityIdx @ 0x001d3044.
// ASM-spec v1.6.1 ActorManager::GetEntityIdx @ 0x001d3044: opens with 'ldr r3,[r0,#0x101c]; ldrb r6,[r1,#0x35]; cmp r3,r6; ble -> -1'. The entity is dereferenced before any test and m_pTypeLists is never null-checked.
int ActorManager::GetEntityIdx(Entity* entity) {
    const int type = entity->entityType;
    if (type >= m_NumTypes) return -1;
    int idx = 0;
    for (std::list<Entity*>::const_iterator it = m_pTypeLists[type].begin();
         it != m_pTypeLists[type].end(); ++it, ++idx) {
        if (*it == entity) return idx;
    }
    return -1;
}

// --- Factory / hash converter registration --------------------------------

// ASM-spec v1.6.1 Mortar::ActorManager::RegisterHashConverter @0x001d0460:
// `m_HashDelegate = param;` (by-value Delegate2 copy-assign).
// Called from GameInit step 16c @ 0x0016cb9e..0x0016cc04 with HashTypeConvert.
void ActorManager::RegisterHashConverter(Mortar::Delegate2<long, unsigned long, bool&> converter) {
    m_HashDelegate = converter;
}

// --- Find variants --------------------------------------------------------

// v1.6.1 ActorManager::Find(long, ulong) @0x001d3148. Linear scan of type list
// `type`; returns first entity whose m_RuntimeID (Entity+0x04) equals `trackerKey`.
// ASM-spec v1.6.1 ActorManager::Find(long,ulong) @ 0x001d3148: no m_pTypeLists null
// test and no `type` range test -- the body indexes the type list directly.
Entity* ActorManager::Find(long type, unsigned long trackerKey) {
    std::list<Entity*>& lst = m_pTypeLists[type];
    for (std::list<Entity*>::iterator it = lst.begin(); it != lst.end(); ++it) {
        if ((*it)->m_RuntimeID == (uint32_t)trackerKey) return *it;
    }
    return nullptr;
}

// v1.6.1 ActorManager::Find(ulong) @0x001d31b8. Type-agnostic scan; returns first
// entity matching trackerKey across all type lists. This is the overload
// SendMessage @0x001d3598 calls.
// ASM-spec v1.6.1 ActorManager::Find(ulong) @ 0x001d31b8: the loop head @0x001d31dc
// reads +0x1010 with no cmp -- m_pTypeLists is never null-checked.
Entity* ActorManager::Find(unsigned long trackerKey) {
    for (int t = 0; t < m_NumTypes; t++) {
        std::list<Entity*>& lst = m_pTypeLists[t];
        for (std::list<Entity*>::iterator it = lst.begin(); it != lst.end(); ++it) {
            if ((*it)->m_RuntimeID == (uint32_t)trackerKey) return *it;
        }
    }
    return nullptr;
}

// v1.6.1 ActorManager::GetNumInAABB @0x001d2f98. Count entities whose vtable+0x20
// (InRect(ColAABB*)) collision test passes against aabb.
// Defunct: zero in-binary callers (no .plt thunk); no-op stub;
//   v1.6.1 ActorManager::GetNumInAABB @ 0x001d2f98.
int ActorManager::GetNumInAABB(ColAABB* aabb) {
    (void)aabb;
    return 0;
}

// --- Level deserialiser ---------------------------------------------------

// v1.6.1 ActorManager::LoadEntity @0x001d408c. EntityChunk deserialise; LOD scale
// + AABB->pos/size + Init. Its own Add call goes to Add(long,bool) @0x001d3fac
// (PLT 0x001168e4), NOT to Add(Entity*,long).
// Defunct: zero in-binary callers (no .plt thunk); no-op stub;
//   v1.6.1 ActorManager::LoadEntity @ 0x001d408c.
bool ActorManager::LoadEntity(EntityChunk* /*chunk*/, void* /*hdr*/,
                              long /*hdrLen*/, long /*lod*/) {
    return false;
}

// --- Heap diagnostics (LinkedHeap stubs) ----------------------------------

// v1.6.1 ActorManager::GetHeapFree @0x001d3b34. Binary forwards to
// LinkedHeap::GetTotalFreeMemory; port has no heap pressure so report m_HeapSize
// as "all free".
int ActorManager::GetHeapFree() const {
    return m_HeapSize;
}

void ActorManager::HeapDisplay(bool /*verbose*/) {
    // Defunct: LinkedHeap diagnostics -- no-op stub; v1.6.1 ActorManager::HeapDisplay @ 0x001d3b24
}

void ActorManager::DisplayUsage(bool /*dumpAll*/) {
    // Defunct: LinkedHeap diagnostics -- no-op stub; v1.6.1 ActorManager::DisplayUsage @ 0x001d3b08
}

// --- Messaging ------------------------------------------------------------

// v1.6.1 ActorManager::AddMessageListener @0x001d4208. m_Listeners.push_back(L).
// Defunct: zero in-binary callers -- this symbol genuinely has NO .plt thunk, so
//   nothing ever registers a listener and m_Listeners is empty for the whole
//   process lifetime; v1.6.1 ActorManager::AddMessageListener @ 0x001d4208.
// ASM-spec v1.6.1 ActorManager::AddMessageListener @ 0x001d4208: 7 insns straight
// into push_back -- no null test on the listener.
void ActorManager::AddMessageListener(Mortar::MessageListener* listener) {
    m_Listeners.push_back(listener);
}

// v1.6.1 ActorManager::RemoveMessageListener @0x001d37c4. m_Listeners.remove(L).
// Defunct: zero in-binary callers (no .plt thunk); v1.6.1
//   ActorManager::RemoveMessageListener @ 0x001d37c4.
void ActorManager::RemoveMessageListener(Mortar::MessageListener* listener) {
    m_Listeners.remove(listener);
}

// v1.6.1 ActorManager::ClearAllListeners @0x001d37e4. Delete each listener
// (operator delete), then clear list.
// NOT dead code: it has a .plt thunk (0x00112dac) and GameExit calls it. It is a
// no-op in practice only because AddMessageListener @0x001d4208 has no .plt thunk,
// so m_Listeners is always empty by the time GameExit runs -- the CALL SITE exists.
// The earlier "zero in-binary callers" note here was wrong.
void ActorManager::ClearAllListeners() {
    for (std::list<Mortar::MessageListener*>::iterator it = m_Listeners.begin();
         it != m_Listeners.end(); ++it) {
        delete *it;
    }
    m_Listeners.clear();
}

// v1.6.1 ActorManager::SendMessage @0x001d3598. Filter listeners, fire
// callback->vtable[+0x30], one-shot clear, then dispatch
// target->ReceiveMessage(sender, msg). Resolves its target via Find(ulong)
// @0x001d31b8.
// Defunct: zero in-binary callers (no .plt thunk); v1.6.1
//   ActorManager::SendMessage @ 0x001d3598.
//
// Filter semantics (v1.6.1 ActorManager::SendMessage @0x001d3598):
//   L->type == msg->type         (exact match; no wildcard on type)
//   L->msgKind == 0 || L->msgKind == typeHash   (0 = any)
//   L->senderId == 0 || (sender && L->senderId == sender->id)  (0 = any)
//
// The unconditional m_Listeners.clear() after the loop IS binary-faithful
// one-shot semantic — each SendMessage fires matching listeners exactly once.
bool ActorManager::SendMessage(unsigned long typeHash, Entity* sender,
                               Mortar::Message* msg) {
    if (!msg) return false;

    // Binary resolves target entity by typeHash before the listener loop.
    Entity* target = Find(typeHash);

    for (std::list<Mortar::MessageListener*>::iterator it = m_Listeners.begin();
         it != m_Listeners.end(); ++it) {
        Mortar::MessageListener* L = *it;
        if (!L) continue;
        if (L->type != (unsigned int)msg->type) continue;
        if (L->msgKind != 0 && L->msgKind != (unsigned int)typeHash) continue;
        // Binary @ 0x001d3598: senderId filter is
        //   piVar3[1] == 0 || (param_2 != 0 && piVar3[1] == param_2->m_RuntimeID)
        // i.e. 0 = wildcard, else the sender's RuntimeID (Entity+0x04) must match.
        if (L->senderId != 0 &&
            !(sender != nullptr && (unsigned int)L->senderId == sender->m_RuntimeID))
            continue;
        if (L->callback) {
            // ASM-verified: 2026-05-20 v1.6.1 ActorManager::SendMessage @ 0x001d3598 (asm-inspector)
            // Binary: cb->vtable[+0x30](cb, sender, target, msg) — Delegate3 invoke.
            Mortar::Delegate3<void, Entity*, Entity*, Message*>* cb =
                static_cast<Mortar::Delegate3<void, Entity*, Entity*, Message*>*>(L->callback);
            (*cb)(sender, target, msg);
        }
    }

    // One-shot clear -- v1.6.1 ActorManager::SendMessage @0x001d3598.
    m_Listeners.clear();

    if (target) target->ReceiveMessage(sender, msg);
    return target != nullptr;
}

// --- Out-of-line formerly-inline methods (required for symbol emission) ----

// v1.6.1 ActorManager::GetHeapSize @0x001d2e80.
int ActorManager::GetHeapSize() const { return m_HeapSize; }

// TODO: v1.6.1 <addr unresolved> (ActorManager::SetCollisionVisible) -- the v1.6.1
// address was not resolved in the batch-B remap; it sits between GetHeapSize
// @0x001d2e80 and DeactivateAllEntities @0x001d2e94 by ordinal, but the exact
// entry point is unconfirmed.
void ActorManager::SetCollisionVisible(unsigned char v) { m_DebugDraw = (v != 0); }

// --- Integer-width convenience overloads -----------------------------------
// Each binary address below resolves to a SINGLE mangled symbol; Ghidra plate
// comments show the binary uses `long` for the type-index param in most of
// these. ARM32 long == int == 4 bytes, so the int-typed primary body (already
// implemented + ASM-verified at the cited address) and these forwarders are the
// same code. The forwarders are a port-only convenience so call sites of either
// integer width link; they have no separate binary counterpart.

// Port specific: int-width forwarder; binary symbol @ 0x001d3fac is Add(long,bool).
Entity* ActorManager::Add(long entityType, bool unused) {
    return Add((int)entityType, unused);
}

// Port specific: int-width forwarder; binary symbol @ 0x001d2e94 is DeactivateAllEntities(long).
void ActorManager::DeactivateAllEntities(long typeIdx) {
    DeactivateAllEntities((int)typeIdx);
}

// Port specific: no-arg Draw() IS the binary symbol @ 0x001d3380 (thiscall, no
// Renderer arg). The binary draws via an implicit global renderer; the port
// resolves it through Renderer::GetInstance() and forwards to the port-only
// Draw(Renderer&) that carries the actual draw loop.
void ActorManager::Draw() {
    Renderer* r = Renderer::GetInstance();
    if (r) Draw(*r);
}

// Port specific: int-width forwarder; binary symbol @ 0x001d30d4 is GetEntity(long,ulong).
Entity* ActorManager::GetEntity(long typeIdx, unsigned long slot) const {
    return GetEntity((int)typeIdx, (size_t)slot);
}

// Port specific: int-width forwarder; binary symbol @ 0x001d2f48 is GetEntityFirst(long,&).
Entity* ActorManager::GetEntityFirst(long typeIdx, std::list<Entity*>::iterator& it) {
    return GetEntityFirst((int)typeIdx, it);
}

// Port specific: int-width forwarder; binary symbol @ 0x001d2f00 is GetEntityNext(long,&).
Entity* ActorManager::GetEntityNext(long typeIdx, std::list<Entity*>::iterator& it) {
    return GetEntityNext((int)typeIdx, it);
}

// Port specific: int-width forwarder; binary symbol @ 0x001d3544 is GetNumEntities(long).
int ActorManager::GetNumEntities(long typeIdx) {
    return GetNumEntities((int)typeIdx);
}

// Port specific: non-const forwarder; binary symbol @ 0x001d349c is GetNumEntities(long*).
int ActorManager::GetNumEntities(long* typeIdxNullTerminated) {
    return GetNumEntities((const long*)typeIdxNullTerminated);
}

// Port specific: int-width forwarder; binary symbol @ 0x001d3ca4 is Initialise(int,int).
void ActorManager::Initialise(long numTypes, long heapSize) {
    Initialise((int)numTypes, (int)heapSize);
}

// Bounds-taking Update(float,ColAABB*,ColAABB*) IS the binary symbol @
// 0x001d38f0. Decompile confirms the two ColAABB* bounds are dead in the binary
// body, so forwarding to the dt-only port body is behaviourally faithful.
void ActorManager::Update(float dt, ::ColAABB* /*boundsA*/, ::ColAABB* /*boundsB*/) {
    Update(dt);
}

}  // namespace Mortar
