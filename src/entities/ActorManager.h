#ifndef FN_ACTOR_MANAGER_H
#define FN_ACTOR_MANAGER_H

// Mortar::ActorManager — binary-faithful port.
//
// Binary class is 4204 bytes. Gameplay relies on:
//   - per-type std::list<Entity*> so GetNumEntities(type) is O(1) and
//     DeactivateAllEntities(type) / iterator walks work as expected;
//   - a 512-slot free pool used to recycle entities between waves
//     instead of new/delete churn;
//   - a factory Delegate1<Entity*, long> at +0x1024 registered once from
//     GameInitialise; callers of Add never construct entities directly.
//
// Field names / offsets match docs/engine/actor-manager.md (refreshed
// 2026-04-23 from Ghidra decompile of the ctor, Initialise, Add,
// Deactivate, Update, Draw).
//
// Not modelled (stubbed or omitted):
//   - LinkedHeap allocator at +0x000 — port uses new[] for the type-list
//     array and leaves m_pHeap as an opaque non-null sentinel so the
//     binary's `if (m_pHeap != NULL)` gate in Update/Draw still behaves.
//   - Delegate2<long, ulong, bool&> hash converter at +0x1048 — not
//     called from any live FruitNinja code path.
//   - MessageListener list at +0x1014 — SendMessage/AddMessageListener
//     stubbed; no port code wires them yet.
//   - LoadEntity / PostLoad — tied to serialisation we don't implement.
//
// Analysed: 2026-04-23T01:00

#include "Entity.h"
#include <cstdint>
#include <list>
#include <cstddef>

namespace Mortar { class ColAABB; }
namespace Mortar { class MessageListener; struct Message; }
struct Renderer;

class ActorManager {
public:
    // Binary: Entity*[0x200] flat array at +0x008.
    static const int FREE_POOL_CAP = 512;

    // Factory delegate signature — matches Delegate1<Entity*, long>.
    typedef Entity* (*FactoryFn)(int entityType);

    // --- Fields mirrored from binary layout (sizes/offsets in comments) -

    // +0x000: LinkedHeap*. Port stores a sentinel (self ptr) once
    // Initialise runs, NULL otherwise. Update/Draw short-circuit on NULL
    // to match the binary's guard.
    void* m_pHeap;

    // +0x004
    int   m_HeapSize;

    // +0x008: 2048-byte free pool (Entity*[512]).
    Entity* m_FreePool[FREE_POOL_CAP];

    // +0x808: free-pool count, grows on Deactivate, shrinks on Add recycle.
    int m_FreeCount;

    // +0x1010: pointer to heap-allocated std::list<Entity*> array of
    // m_NumTypes slots. Each list is the active entities of that type.
    std::list<Entity*>* m_pTypeLists;

    // +0x101c
    int  m_NumTypes;

    // +0x1020: debug flag — Draw calls DrawDebug() when non-zero.
    bool m_DebugDraw;

    // +0x1024: factory function used when the free pool has no matching
    // entity. Registered via RegisterFactory from GameInitialise.
    FactoryFn m_FactoryDelegate;

    // Singleton — binary uses Meyers static local `em` inside
    // GetInstance (0x001705f0). Port exposes the same access pattern.
    static ActorManager* s_Instance;

    ActorManager();
    ~ActorManager();

    static ActorManager* GetInstance();

    // --- Lifecycle (binary: 0x0017046c / 0x0017037c / 0x00170064) ------

    void Initialise(int numTypes, int heapSize = 0x2000);
    void Destroy();
    void Clear();

    // --- Factory / listener registration --------------------------------

    // 0x0016d870.
    void RegisterFactory(FactoryFn factory) { m_FactoryDelegate = factory; }

    // --- Entity API -----------------------------------------------------

    // 0x0017068c. Second bool param is declared but the binary body never
    // reads it — every caller passes `true`. Kept in the signature so
    // call sites don't rename; body ignores it.
    Entity* Add(int entityType, bool /*unused*/ = true);

    // 0x00170654. Push a pre-constructed entity into a type list without
    // going through the factory. Used by LoadEntity; exposed here for
    // parity but currently only stubbed callers use it.
    Entity* Add(Entity* entity, long typeIdx);

    // 0x00170184. Erase from its type list, append to free pool. Calls
    // the entity's virtual Deactivate() first so subclasses can clean up
    // emitters / external references before the slot is reused. (The
    // binary does NOT call a virtual here — its entity cleanup lives in
    // the KillX helpers. We invoke the virtual because the port's
    // Bomb/Fruit Deactivate overrides own that cleanup.)
    void Deactivate(Entity* entity);

    // 0x001702d8. Erase + (unless ENT_NO_DESTRUCT) call virtual
    // Deactivate then delete. Does not return the entity to the pool.
    void Remove(Entity* entity);

    // 0x0016fb44. Set ENT_KILLED on every entity of the given type so
    // the next Update sweep drains them into the free pool.
    void DeactivateAllEntities(int typeIdx);

    // --- Per-frame update / draw ---------------------------------------

    // 0x001701f4. Binary signature takes two `ColAABB*` bounds that are
    // unused in FruitNinja; port keeps the dt-only form. Iterates all
    // type lists, updates active entities, drains killed entities via
    // Deactivate.
    void Update(float dt);

    // 0x0016fe7c. Binary takes no Renderer (entities use an implicit
    // global); port threads Renderer& through for uniformity with the
    // rest of the draw pipeline — documented divergence.
    void Draw(Renderer& r);

    // 0x0016fe1c. Per-entity collision-sphere debug draw. Gated on
    // m_DebugDraw.
    void DrawDebug();

    // 0x0016fdc8. Stubbed — no deserialisation in port.
    void PostLoad();

    // --- Query API (binary: four GetNumEntities overloads, +Find/Get) --

    // 0x0016ff98. Raw list size — NO active-flag filtering (matches
    // binary; the old port's IsActive() filter diverged and caused
    // MainScreen state transitions to fire on wrong counts).
    int  GetNumEntities(int typeIdx) const;

    // 0x0016ffac. Sum of all type-list sizes.
    int  GetNumEntities() const;

    // 0x0016ff30. Null-terminated type-index array variant.
    int  GetNumEntities(const long* typeIdxNullTerminated) const;

    // 0x0016ff5c. Range variant: sums [min(a,b) .. max(a,b)).
    int  GetNumEntities(long typeA, long typeB) const;

    // 0x0016ff00. Count of type lists that contain at least one entity.
    int  GetNumTypes() const;

    // Accessor for per-type iteration. Binary callers use GetEntityFirst /
    // GetEntityNext; port exposes the std::list directly which is idiomatic
    // C++ and lets call sites iterate with range-for.
    const std::list<Entity*>& GetTypeList(int typeIdx) const {
        static const std::list<Entity*> s_empty;
        if (!m_pTypeLists || typeIdx < 0 || typeIdx >= m_NumTypes) return s_empty;
        return m_pTypeLists[typeIdx];
    }

    // 0x0016fcc4. Entity at ordinal `slot` in type list `typeIdx`.
    Entity* GetEntity(int typeIdx, size_t slot) const;

    // 0x0016fc64. Ordinal index of `entity` in its type list, or -1.
    int GetEntityIdx(Entity* entity) const;

    // --- Messaging (stubbed — no callers in current port code) ---------

    void SendMessage(uint32_t typeHash, Entity* sender, Mortar::Message* msg);
    void AddMessageListener(Mortar::MessageListener* listener);
    void RemoveMessageListener(Mortar::MessageListener* listener);
    void ClearAllListeners();
};

#endif  // FN_ACTOR_MANAGER_H
