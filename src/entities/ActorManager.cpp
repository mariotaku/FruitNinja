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
// Addresses tagged in method comments reference the original FruitNinja ARM32 binary.
//
// Analysed: 2026-05-04T00:00

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
    // Binary ctor @ 0x00170500: constructs delegates (default/empty state)
    // then clears m_Listeners; m_Listeners was default-constructed by the
    // compiler, clear() matches the binary epilogue.
    m_Listeners.clear();
}

ActorManager::~ActorManager() {
    // Binary dtor calls Destroy() then tears down delegates / listener list.
    Destroy();
    if (s_Instance == this) s_Instance = nullptr;
}

ActorManager* ActorManager::GetInstance() {
    // Binary: Meyers singleton at 0x001705f0 with __cxa_guard_acquire.
    // Port: lazy-init via static pointer (single-thread SDL, no guard needed).
    if (s_Instance == nullptr) {
        s_Instance = new ActorManager();
    }
    return s_Instance;
}

// --- Lifecycle ------------------------------------------------------------

// 0x0017046c. Allocates the per-type list array from the LinkedHeap.
// Port drops LinkedHeap and uses new[]; sets m_pHeap to a sentinel so the
// null-guard in Update/Draw stays false.
// ASM-verified: 2026-04-29T00:00Z v1.6.1 binary @ 0x0017046c (asm-inspector)
void ActorManager::Initialise(int numTypes, int heapSize) {
    if (m_pHeap != nullptr) return;  // already initialised
    m_HeapSize    = heapSize;
    m_NumTypes    = numTypes;
    m_pTypeLists  = new std::list<Entity*>[numTypes];
    m_pHeap       = this;  // non-null sentinel
}

// 0x0017037c.
// ASM-verified: 2026-04-29T00:00Z v1.6.1 binary @ 0x0017037c (asm-inspector)
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
}

// 0x00170064. Delete all entities in type lists + free pool.
// ASM-verified: 2026-04-29T00:00Z v1.6.1 binary @ 0x00170064 (asm-inspector)
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

// 0x0017068c. Binary-faithful recycle-first Add.
// ASM-verified: 2026-04-29T00:00Z v1.6.1 binary @ 0x0017068c (asm-inspector)
Entity* ActorManager::Add(int entityType, bool /*unused — dead param in binary*/) {
    if (m_pHeap == nullptr || m_pTypeLists == nullptr) return nullptr;
    if (entityType < 0 || entityType >= m_NumTypes) return nullptr;

    // --- Recycle path: reverse-scan free pool for matching type. ---
    for (int i = m_FreeCount - 1; i >= 0; i--) {
        Entity* candidate = m_FreePool[i];
        if (candidate && candidate->entityType == entityType) {
            // push_back BEFORE decrementing count (matches binary ordering
            // at 0x001706be → 0x001706ca).
            m_pTypeLists[entityType].push_back(candidate);
            m_FreeCount--;
            // Compact pool: shift [i+1..m_FreeCount] one slot left.
            for (int j = i; j < m_FreeCount; j++)
                m_FreePool[j] = m_FreePool[j + 1];
            m_FreePool[m_FreeCount] = nullptr;
            // Mortar::Entity::Activate: flags &= 0xFE (clear bit 0 only) — only
            // ENT_INACTIVE (bit 0) is cleared; ENT_KILLED is NOT touched.
            // ASM-verified: 2026-04-28T15:55Z v1.6.1 Mortar::Entity::Activate @ 0x001d45f8 (asm-inspector)
            candidate->flags &= ~ENT_INACTIVE;
            return candidate;
        }
    }

    // --- Factory path: pool empty or no matching type. ---
    if (!m_FactoryDelegate) {
        LOG_WARN("ACTOR/Add", "no factory registered (type %d)", entityType);
        return nullptr;
    }
    Entity* entity = m_FactoryDelegate(entityType);
    if (entity == nullptr) return nullptr;
    m_pTypeLists[entityType].push_back(entity);
    entity->entityType    = entityType;   // binary: store at +0x35
    entity->m_RecycleFlag = 0;            // binary: store at +0x34
    // Factory path does NOT call Entity::Activate — the fresh entity comes
    // out of its ctor with flags=0 which already satisfies `(flags & 0x11) == 0`.
    return entity;
}

// 0x00170654. Push an already-built entity into its type list.
// Defunct: zero live in-binary callers (only LoadEntity, itself dead); v1.6.1 binary @ 0x00170654.
// ASM-verified: 2026-04-29T00:00Z v1.6.1 binary @ 0x00170654 (asm-inspector)
Entity* ActorManager::Add(Entity* entity, long typeIdx) {
    if (!entity) return nullptr;
    if (typeIdx < 0 || typeIdx >= (long)m_NumTypes || m_pTypeLists == nullptr) return nullptr;
    m_pTypeLists[typeIdx].push_back(entity);
    entity->entityType    = (int)typeIdx;
    entity->m_RecycleFlag = 0;
    return entity;
}

// 0x00170184. Binary walks type list, erases, pushes to free pool. No flag
// manipulation. Binary @ 0x00170184.
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

// 0x001702d8. Binary calls vtable+0xc (Release) then operator delete.
// Defunct: zero in-binary callers; v1.6.1 binary @ 0x001702d8.
// ASM-verified: 2026-04-29T00:00Z v1.6.1 binary @ 0x001702d8 (asm-inspector)
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

// 0x0016fb44.
// Defunct: zero in-binary callers; v1.6.1 binary @ 0x0016fb44.
// ASM-verified: 2026-04-29T00:00Z v1.6.1 binary @ 0x0016fb44 (asm-inspector)
void ActorManager::DeactivateAllEntities(int typeIdx) {
    if (m_pTypeLists == nullptr) return;
    if (typeIdx < 0 || typeIdx >= m_NumTypes) return;
    for (std::list<Entity*>::iterator it = m_pTypeLists[typeIdx].begin();
         it != m_pTypeLists[typeIdx].end(); ++it) {
        if (*it) (*it)->flags |= ENT_KILLED;
    }
}

// --- Per-frame update / draw ---------------------------------------------

// 0x001701f4. Binary queues kills into m_PendingDeact[] during the type
// sweep and drains after, so std::list iteration is never invalidated
// by a mid-loop erase. Binary @ 0x001701f4.
// Binary disasm @ 0x0017024e: `orr r2, r2, #0xc; strb r2, [r6, #0xc]`
// sets ENT_TICK_DISPATCHED (bits 2+3, 0xc) BEFORE Update dispatch.
// Binary leaves the bits set permanently after PostUpdate -- sticky advisory, never cleared.
void ActorManager::Update(float dt) {
    if (m_pHeap == nullptr || m_pTypeLists == nullptr) return;

    m_PendingDeactCount = 0;

    for (int t = 0; t < m_NumTypes; t++) {
        std::list<Entity*>& list = m_pTypeLists[t];
        for (std::list<Entity*>::iterator it = list.begin(); it != list.end(); ++it) {
            Entity* e = *it;
            if (!e) continue;
            if ((e->flags & ENT_SKIP_MASK) == 0) {
                e->flags |= ENT_TICK_DISPATCHED;  // 0xc -- both halves set atomically per binary
                e->Update(dt);
                e->PostUpdate(dt);  // vtable +0x18 -- Bomb uses this to track fuse emitter
                // Binary leaves ENT_TICK_DISPATCHED set after PostUpdate (sticky, never cleared).
            }
            if ((e->flags & ENT_KILLED) && m_PendingDeactCount < 512) {
                m_PendingDeact[m_PendingDeactCount++] = e;
            }
        }
    }

    for (int i = 0; i < m_PendingDeactCount; i++) {
        Deactivate(m_PendingDeact[i]);
    }
    m_PendingDeactCount = 0;
}

// 0x0016fe7c.
// ASM-verified: 2026-04-29T00:00Z v1.6.1 binary @ 0x0016fe7c (asm-inspector)
void ActorManager::Draw(Renderer& r) {
    if (m_pHeap == nullptr || m_pTypeLists == nullptr) return;
    for (int t = 0; t < m_NumTypes; t++) {
        std::list<Entity*>& list = m_pTypeLists[t];
        for (std::list<Entity*>::iterator it = list.begin(); it != list.end(); ++it) {
            Entity* e = *it;
            if (e && (e->flags & ENT_SKIP_MASK) == 0) {
                e->Draw(r);
            }
        }
    }
    if (m_DebugDraw) DrawDebug();
}

// 0x0016fe1c. ColSphere debug drawing.
// Defunct: zero in-binary callers; v1.6.1 binary @ 0x0016fe1c.
void ActorManager::DrawDebug() {}

// Binary @ 0x0016fdc8 -- iterates m_pTypeLists and calls entity->vtable[+0x1c]
// (slot 7, PostLoad). Defunct: zero in-binary callers; LoadEntity is itself dead.
// Port keeps stub body as no-op for safety.
void ActorManager::PostLoad() {
    // Defunct: zero in-binary callers; v1.6.1 binary @ 0x0016fdc8.
}

// --- Query API ------------------------------------------------------------

// 0x0016ff98. Binary returns list size with NO active filtering.
// ASM-verified: 2026-04-29T00:00Z v1.6.1 binary @ 0x0016ff98 (asm-inspector)
int ActorManager::GetNumEntities(int typeIdx) {
    if (!m_pTypeLists || typeIdx < 0 || typeIdx >= m_NumTypes) return 0;
    return (int)m_pTypeLists[typeIdx].size();
}

// 0x0016ffac.
// ASM-verified: 2026-04-29T00:00Z v1.6.1 binary @ 0x0016ffac (asm-inspector)
int ActorManager::GetNumEntities() {
    if (!m_pTypeLists) return 0;
    int total = 0;
    for (int t = 0; t < m_NumTypes; t++) total += (int)m_pTypeLists[t].size();
    return total;
}

// 0x0016ff30. Sentinel is -1L, not 0 (type 0 == Bomb would be skipped wrongly).
// Binary @ 0x0016ff30.
// ASM-verified: 2026-04-29T00:00Z v1.6.1 binary @ 0x0016ff30 (asm-inspector)
int ActorManager::GetNumEntities(const long* typeIdxNullTerminated) {
    if (!m_pTypeLists || !typeIdxNullTerminated) return 0;
    int total = 0;
    for (const long* p = typeIdxNullTerminated; *p != -1L; ++p) {
        const long t = *p;
        if (t >= 0 && t < (long)m_NumTypes) total += (int)m_pTypeLists[t].size();
    }
    return total;
}

// 0x0016ff5c.
// ASM-verified: 2026-04-29T00:00Z v1.6.1 binary @ 0x0016ff5c (asm-inspector)
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

// 0x0016ff00.
// ASM-verified: 2026-04-29T00:00Z v1.6.1 binary @ 0x0016ff00 (asm-inspector)
int ActorManager::GetNumTypes() {
    if (!m_pTypeLists) return 0;
    int n = 0;
    for (int t = 0; t < m_NumTypes; t++) if (!m_pTypeLists[t].empty()) n++;
    return n;
}

// Matches ActorManager::GetEntityFirst (0x0016fbb8). Seeds `it` with the
// type list's begin() iterator and returns the Entity* it points at, or
// nullptr if the list is empty. Binary also returns the ActorManager*
// via an outer CONCAT that callers ignore.
// ASM-verified: 2026-04-29T00:00Z v1.6.1 binary @ 0x0016fbb8 (asm-inspector)
Entity* ActorManager::GetEntityFirst(int typeIdx, std::list<Entity*>::iterator& it) {
    if (!m_pTypeLists || typeIdx < 0 || typeIdx >= m_NumTypes) {
        it = std::list<Entity*>::iterator();
        return nullptr;
    }
    std::list<Entity*>& list = m_pTypeLists[typeIdx];
    it = list.begin();
    if (it == list.end()) return nullptr;
    return *it;
}

// Matches ActorManager::GetEntityNext (0x0016fb88). Advances `it` and
// returns the next Entity*, or nullptr when past end.
// ASM-verified: 2026-04-29T00:00Z v1.6.1 binary @ 0x0016fb88 (asm-inspector)
Entity* ActorManager::GetEntityNext(int typeIdx, std::list<Entity*>::iterator& it) {
    if (!m_pTypeLists || typeIdx < 0 || typeIdx >= m_NumTypes) return nullptr;
    std::list<Entity*>& list = m_pTypeLists[typeIdx];
    ++it;
    if (it == list.end()) return nullptr;
    return *it;
}

// 0x0016fcc4.
// ASM-verified: 2026-04-29T00:00Z v1.6.1 binary @ 0x0016fcc4 (asm-inspector)
Entity* ActorManager::GetEntity(int typeIdx, size_t slot) const {
    if (!m_pTypeLists || typeIdx < 0 || typeIdx >= m_NumTypes) return nullptr;
    std::list<Entity*>& list = m_pTypeLists[typeIdx];
    if (slot >= list.size()) return nullptr;
    std::list<Entity*>::iterator it = list.begin();
    std::advance(it, slot);
    return *it;
}

// 0x0016fc64.
// Defunct: zero in-binary callers; v1.6.1 binary @ 0x0016fc64.
// ASM-verified: 2026-04-29T00:00Z v1.6.1 binary @ 0x0016fc64 (asm-inspector)
int ActorManager::GetEntityIdx(Entity* entity) {
    if (!entity || !m_pTypeLists) return -1;
    const int type = entity->entityType;
    if (type < 0 || type >= m_NumTypes) return -1;
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

// 0x0016fd10. Linear scan of type list `type`; returns first entity whose
// m_RuntimeID (Entity+0x04) equals `trackerKey`. Binary @ 0x0016fd10.
Entity* ActorManager::Find(long type, unsigned long trackerKey) {
    if (!m_pTypeLists || type < 0 || type >= (long)m_NumTypes) return nullptr;
    std::list<Entity*>& lst = m_pTypeLists[type];
    for (std::list<Entity*>::iterator it = lst.begin(); it != lst.end(); ++it) {
        if ((*it)->m_RuntimeID == (uint32_t)trackerKey) return *it;
    }
    return nullptr;
}

// 0x0016fd58. Type-agnostic scan; returns first entity matching trackerKey
// across all type lists. Binary @ 0x0016fd58.
Entity* ActorManager::Find(unsigned long trackerKey) {
    if (!m_pTypeLists) return nullptr;
    for (int t = 0; t < m_NumTypes; t++) {
        std::list<Entity*>& lst = m_pTypeLists[t];
        for (std::list<Entity*>::iterator it = lst.begin(); it != lst.end(); ++it) {
            if ((*it)->m_RuntimeID == (uint32_t)trackerKey) return *it;
        }
    }
    return nullptr;
}

// 0x0016fbec. Count entities whose vtable+0x20 (InRect(ColAABB*)) collision
// test passes against aabb.
// Defunct: zero in-binary callers; v1.6.1 binary @ 0x0016fbec.
int ActorManager::GetNumInAABB(ColAABB* aabb) {
    (void)aabb;
    return 0;
}

// --- Level deserialiser ---------------------------------------------------

// 0x00170728. EntityChunk deserialise; LOD scale + AABB->pos/size + Init.
// Defunct: zero in-binary callers; v1.6.1 binary @ 0x00170728.
bool ActorManager::LoadEntity(EntityChunk* /*chunk*/, void* /*hdr*/,
                              long /*hdrLen*/, long /*lod*/) {
    return false;
}

// --- Heap diagnostics (LinkedHeap stubs) ----------------------------------

// 0x00170370. Binary forwards to LinkedHeap::GetTotalFreeMemory; port has
// no heap pressure so report m_HeapSize as "all free".
int ActorManager::GetHeapFree() const {
    return m_HeapSize;
}

// 0x00170364.
void ActorManager::HeapDisplay(bool /*verbose*/) {
    // Defunct: LinkedHeap diagnostics -- no-op stub; v1.6.1 binary @ 0x00170364
}

// 0x00170354.
void ActorManager::DisplayUsage(bool /*dumpAll*/) {
    // Defunct: LinkedHeap diagnostics -- no-op stub; v1.6.1 binary @ 0x00170354
}

// --- Messaging ------------------------------------------------------------

// 0x0017085c. m_Listeners.push_back(L).
// Defunct: zero in-binary callers; v1.6.1 binary @ 0x0017085c.
void ActorManager::AddMessageListener(Mortar::MessageListener* listener) {
    if (!listener) return;
    m_Listeners.push_back(listener);
}

// 0x00170124. m_Listeners.remove(L).
// Defunct: zero in-binary callers; v1.6.1 binary @ 0x00170124.
void ActorManager::RemoveMessageListener(Mortar::MessageListener* listener) {
    m_Listeners.remove(listener);
}

// 0x0017013c. Delete each listener (operator delete), then clear list.
// Defunct: zero in-binary callers; v1.6.1 binary @ 0x0017013c.
void ActorManager::ClearAllListeners() {
    for (std::list<Mortar::MessageListener*>::iterator it = m_Listeners.begin();
         it != m_Listeners.end(); ++it) {
        delete *it;
    }
    m_Listeners.clear();
}

// 0x0016ffd8. Filter listeners, fire callback->vtable[+0x30], one-shot clear,
// then dispatch target->ReceiveMessage(sender, msg).
// Defunct: zero in-binary callers; v1.6.1 binary @ 0x0016ffd8.
//
// Filter semantics (binary @ 0x0016ffd8):
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
        // Binary @ 0x0016ffd8: senderId filter is
        //   piVar3[1] == 0 || (param_2 != 0 && piVar3[1] == param_2->m_RuntimeID)
        // i.e. 0 = wildcard, else the sender's RuntimeID (Entity+0x04) must match.
        if (L->senderId != 0 &&
            !(sender != nullptr && (unsigned int)L->senderId == sender->m_RuntimeID))
            continue;
        if (L->callback) {
            // ASM-verified: 2026-05-20 v1.6.1 binary @ 0x0016ffd8 (asm-inspector)
            // Binary: cb->vtable[+0x30](cb, sender, target, msg) — Delegate3 invoke.
            Mortar::Delegate3<void, Entity*, Entity*, Message*>* cb =
                static_cast<Mortar::Delegate3<void, Entity*, Entity*, Message*>*>(L->callback);
            (*cb)(sender, target, msg);
        }
    }

    // One-shot clear — binary @ 0x0016ffd8.
    m_Listeners.clear();

    if (target) target->ReceiveMessage(sender, msg);
    return target != nullptr;
}

// --- Out-of-line formerly-inline methods (required for symbol emission) ----

// 0x0016fb38.
int ActorManager::GetHeapSize() const { return m_HeapSize; }

// 0x0016fb3c.
void ActorManager::SetCollisionVisible(unsigned char v) { m_DebugDraw = (v != 0); }

// --- Integer-width convenience overloads -----------------------------------
// Each binary address below resolves to a SINGLE mangled symbol; Ghidra plate
// comments show the binary uses `long` for the type-index param in most of
// these. ARM32 long == int == 4 bytes, so the int-typed primary body (already
// implemented + ASM-verified at the cited address) and these forwarders are the
// same code. The forwarders are a port-only convenience so call sites of either
// integer width link; they have no separate binary counterpart.

// Port specific: int-width forwarder; binary symbol @ 0x0017068c is Add(long,bool).
Entity* ActorManager::Add(long entityType, bool unused) {
    return Add((int)entityType, unused);
}

// Port specific: int-width forwarder; binary symbol @ 0x0016fb44 is DeactivateAllEntities(long).
void ActorManager::DeactivateAllEntities(long typeIdx) {
    DeactivateAllEntities((int)typeIdx);
}

// Port specific: no-arg Draw() IS the binary symbol @ 0x0016fe7c (thiscall, no
// Renderer arg). The binary draws via an implicit global renderer; the port
// resolves it through Renderer::GetInstance() and forwards to the port-only
// Draw(Renderer&) that carries the actual draw loop.
void ActorManager::Draw() {
    Renderer* r = Renderer::GetInstance();
    if (r) Draw(*r);
}

// Port specific: int-width forwarder; binary symbol @ 0x0016fcc4 is GetEntity(long,ulong).
Entity* ActorManager::GetEntity(long typeIdx, unsigned long slot) const {
    return GetEntity((int)typeIdx, (size_t)slot);
}

// Port specific: int-width forwarder; binary symbol @ 0x0016fbb8 is GetEntityFirst(long,&).
Entity* ActorManager::GetEntityFirst(long typeIdx, std::list<Entity*>::iterator& it) {
    return GetEntityFirst((int)typeIdx, it);
}

// Port specific: int-width forwarder; binary symbol @ 0x0016fb88 is GetEntityNext(long,&).
Entity* ActorManager::GetEntityNext(long typeIdx, std::list<Entity*>::iterator& it) {
    return GetEntityNext((int)typeIdx, it);
}

// Port specific: int-width forwarder; binary symbol @ 0x0016ff98 is GetNumEntities(int/long).
int ActorManager::GetNumEntities(long typeIdx) {
    return GetNumEntities((int)typeIdx);
}

// Port specific: non-const forwarder; binary symbol @ 0x0016ff30 is GetNumEntities(long*).
int ActorManager::GetNumEntities(long* typeIdxNullTerminated) {
    return GetNumEntities((const long*)typeIdxNullTerminated);
}

// Port specific: int-width forwarder; binary symbol @ 0x0017046c is Initialise(int,int).
void ActorManager::Initialise(long numTypes, long heapSize) {
    Initialise((int)numTypes, (int)heapSize);
}

// Bounds-taking Update(float,ColAABB*,ColAABB*) IS the binary symbol @
// 0x001701f4. Decompile confirms the two ColAABB* bounds are dead in the binary
// body, so forwarding to the dt-only port body is behaviourally faithful.
void ActorManager::Update(float dt, ::ColAABB* /*boundsA*/, ::ColAABB* /*boundsB*/) {
    Update(dt);
}

}  // namespace Mortar
