#include "ActorManager.h"
#include "Fruit.h"
#include "Bomb.h"
#include "BombBlast.h"
#include "render/Renderer.h"
#include <cstdio>
#include <cstring>

// Binary-faithful ActorManager port.
// Addresses tagged in method comments reference the original FruitNinja
// ARM32 binary; see docs/engine/actor-manager.md for the decompile.
//
// Analysed: 2026-04-23T01:00

ActorManager* ActorManager::s_Instance = nullptr;

// --- Construction / singleton --------------------------------------------

ActorManager::ActorManager()
    : m_pHeap(nullptr)
    , m_HeapSize(0)
    , m_FreeCount(0)
    , m_pTypeLists(nullptr)
    , m_NumTypes(0)
    , m_DebugDraw(false)
    , m_FactoryDelegate(nullptr)
{
    std::memset(m_FreePool, 0, sizeof(m_FreePool));
    s_Instance = this;
}

ActorManager::~ActorManager() {
    // Binary dtor calls Destroy() then tears down delegates / listener list.
    Destroy();
    if (s_Instance == this) s_Instance = nullptr;
}

ActorManager* ActorManager::GetInstance() {
    // Binary: Meyers static local at 0x0022ee80-like slot. Port uses a raw
    // pointer set by the ctor — GameInitialise `new ActorManager()` is the
    // only constructor call, so s_Instance is stable after startup.
    return s_Instance;
}

// --- Lifecycle ------------------------------------------------------------

// 0x0017046c. Allocates the per-type list array from the LinkedHeap.
// Port drops LinkedHeap and uses new[]; sets m_pHeap to a sentinel so the
// null-guard in Update/Draw stays false.
void ActorManager::Initialise(int numTypes, int heapSize) {
    if (m_pHeap != nullptr) return;  // already initialised
    m_HeapSize    = heapSize;
    m_NumTypes    = numTypes;
    m_pTypeLists  = new std::list<Entity*>[numTypes];
    m_pHeap       = this;  // non-null sentinel
}

// 0x0017037c.
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
void ActorManager::Clear() {
    if (m_pTypeLists) {
        for (int t = 0; t < m_NumTypes; t++) {
            std::list<Entity*>& list = m_pTypeLists[t];
            for (std::list<Entity*>::iterator it = list.begin(); it != list.end(); ++it) {
                Entity* e = *it;
                if (e && !(e->flags & ENT_NO_DESTRUCT)) {
                    e->Deactivate();   // virtual cleanup (emitters etc.)
                    delete e;
                }
            }
            list.clear();
        }
    }
    for (int i = 0; i < m_FreeCount; i++) {
        Entity* e = m_FreePool[i];
        if (e && !(e->flags & ENT_NO_DESTRUCT)) {
            delete e;
        }
    }
    std::memset(m_FreePool, 0, sizeof(m_FreePool));
    m_FreeCount = 0;
}

// --- Entity API -----------------------------------------------------------

// 0x0017068c. Binary-faithful recycle-first Add.
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
            // Entity::Activate at 0x00170b18: flags &= 0xFE. The port
            // also clears ENT_KILLED so a resurrected entity doesn't
            // immediately trip the deactivation sweep again.
            candidate->flags &= ~(ENT_INACTIVE | ENT_KILLED);
            return candidate;
        }
    }

    // --- Factory path: pool empty or no matching type. ---
    if (!m_FactoryDelegate) {
        fprintf(stderr, "ActorManager::Add: no factory registered (type %d)\n", entityType);
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
Entity* ActorManager::Add(Entity* entity, long typeIdx) {
    if (!entity) return nullptr;
    if (typeIdx < 0 || typeIdx >= (long)m_NumTypes || m_pTypeLists == nullptr) return nullptr;
    m_pTypeLists[typeIdx].push_back(entity);
    entity->entityType    = (int)typeIdx;
    entity->m_RecycleFlag = 0;
    return entity;
}

// 0x00170184.
void ActorManager::Deactivate(Entity* entity) {
    if (!entity || m_pTypeLists == nullptr) return;
    const int type = entity->entityType;
    if (type < 0 || type >= m_NumTypes) return;
    std::list<Entity*>& list = m_pTypeLists[type];
    for (std::list<Entity*>::iterator it = list.begin(); it != list.end(); ++it) {
        if (*it == entity) {
            list.erase(it);
            // Virtual cleanup — subclasses drop emitters etc. Clears
            // ENT_KILLED so a recycled entity starts clean.
            entity->Deactivate();
            entity->flags |=  ENT_INACTIVE;
            entity->flags &= ~ENT_KILLED;
            if (m_FreeCount < FREE_POOL_CAP) {
                m_FreePool[m_FreeCount++] = entity;
            } else {
                // Pool full — binary would leak; port deletes to prevent
                // unbounded growth. Should never fire in practice (pool
                // size 512 is huge for 3 entity types with ~30 live each).
                delete entity;
            }
            return;
        }
    }
}

// 0x001702d8.
void ActorManager::Remove(Entity* entity) {
    if (!entity || m_pTypeLists == nullptr) return;
    const int type = entity->entityType;
    if (type < 0 || type >= m_NumTypes) return;
    std::list<Entity*>& list = m_pTypeLists[type];
    for (std::list<Entity*>::iterator it = list.begin(); it != list.end(); ++it) {
        if (*it == entity) {
            if (!(entity->flags & ENT_NO_DESTRUCT)) {
                entity->Deactivate();
                delete entity;
            }
            list.erase(it);
            return;
        }
    }
}

// 0x0016fb44.
void ActorManager::DeactivateAllEntities(int typeIdx) {
    if (m_pTypeLists == nullptr) return;
    if (typeIdx < 0 || typeIdx >= m_NumTypes) return;
    for (std::list<Entity*>::iterator it = m_pTypeLists[typeIdx].begin();
         it != m_pTypeLists[typeIdx].end(); ++it) {
        if (*it) (*it)->flags |= ENT_KILLED;
    }
}

// --- Per-frame update / draw ---------------------------------------------

// 0x001701f4.
void ActorManager::Update(float dt) {
    if (m_pHeap == nullptr || m_pTypeLists == nullptr) return;

    // Two-pass update: iterate type lists first, collect kill list after.
    // Matches the binary which stages the deactivation queue at +0x80c.
    Entity* killList[FREE_POOL_CAP];
    int killCount = 0;

    for (int t = 0; t < m_NumTypes; t++) {
        std::list<Entity*>& list = m_pTypeLists[t];
        for (std::list<Entity*>::iterator it = list.begin(); it != list.end(); ++it) {
            Entity* e = *it;
            if (!e) continue;
            if ((e->flags & ENT_SKIP_MASK) == 0) {
                e->flags |=  ENT_UPDATING;
                e->Update(dt);
                e->flags |=  ENT_POST_UPDATING;
                e->PostUpdate(dt);  // vtable +0x18 — Bomb uses this to track fuse emitter
                e->flags &= ~(ENT_UPDATING | ENT_POST_UPDATING);
            }
            if ((e->flags & ENT_KILLED) && killCount < FREE_POOL_CAP) {
                killList[killCount++] = e;
            }
        }
    }

    for (int i = 0; i < killCount; i++) {
        Deactivate(killList[i]);
    }
}

// 0x0016fe7c.
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

// 0x0016fe1c. Stubbed — ColSphere debug drawing not ported.
void ActorManager::DrawDebug() {}

// 0x0016fdc8.
void ActorManager::PostLoad() {}

// --- Query API ------------------------------------------------------------

// 0x0016ff98. Binary returns list size with NO active filtering.
int ActorManager::GetNumEntities(int typeIdx) const {
    if (!m_pTypeLists || typeIdx < 0 || typeIdx >= m_NumTypes) return 0;
    return (int)m_pTypeLists[typeIdx].size();
}

// 0x0016ffac.
int ActorManager::GetNumEntities() const {
    if (!m_pTypeLists) return 0;
    int total = 0;
    for (int t = 0; t < m_NumTypes; t++) total += (int)m_pTypeLists[t].size();
    return total;
}

// 0x0016ff30.
int ActorManager::GetNumEntities(const long* typeIdxNullTerminated) const {
    if (!m_pTypeLists || !typeIdxNullTerminated) return 0;
    int total = 0;
    for (const long* p = typeIdxNullTerminated; *p != 0; ++p) {
        const long t = *p;
        if (t >= 0 && t < (long)m_NumTypes) total += (int)m_pTypeLists[t].size();
    }
    return total;
}

// 0x0016ff5c.
int ActorManager::GetNumEntities(long typeA, long typeB) const {
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
int ActorManager::GetNumTypes() const {
    if (!m_pTypeLists) return 0;
    int n = 0;
    for (int t = 0; t < m_NumTypes; t++) if (!m_pTypeLists[t].empty()) n++;
    return n;
}

// Matches ActorManager::GetEntityFirst (0x0016fbb8). Seeds `it` with the
// type list's begin() iterator and returns the Entity* it points at, or
// nullptr if the list is empty. Binary also returns the ActorManager*
// via an outer CONCAT that callers ignore.
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
Entity* ActorManager::GetEntityNext(int typeIdx, std::list<Entity*>::iterator& it) {
    if (!m_pTypeLists || typeIdx < 0 || typeIdx >= m_NumTypes) return nullptr;
    std::list<Entity*>& list = m_pTypeLists[typeIdx];
    ++it;
    if (it == list.end()) return nullptr;
    return *it;
}

// 0x0016fcc4.
Entity* ActorManager::GetEntity(int typeIdx, size_t slot) const {
    if (!m_pTypeLists || typeIdx < 0 || typeIdx >= m_NumTypes) return nullptr;
    std::list<Entity*>& list = m_pTypeLists[typeIdx];
    if (slot >= list.size()) return nullptr;
    std::list<Entity*>::iterator it = list.begin();
    std::advance(it, slot);
    return *it;
}

// 0x0016fc64.
int ActorManager::GetEntityIdx(Entity* entity) const {
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

// --- Messaging (stubs) ----------------------------------------------------

void ActorManager::SendMessage(uint32_t, Entity*, Mortar::Message*) {}
void ActorManager::AddMessageListener(Mortar::MessageListener*) {}
void ActorManager::RemoveMessageListener(Mortar::MessageListener*) {}
void ActorManager::ClearAllListeners() {}
