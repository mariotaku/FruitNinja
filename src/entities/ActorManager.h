#ifndef FN_ACTOR_MANAGER_H
#define FN_ACTOR_MANAGER_H

// ActorManager — binary-faithful port.
//
// Binary class is 4204 bytes. Gameplay relies on:
//   - per-type std::list<Entity*> so GetNumEntities(type) is O(1) and
//     DeactivateAllEntities(type) / iterator walks work as expected;
//   - a 512-slot free pool used to recycle entities between waves
//     instead of new/delete churn;
//   - a factory Delegate1<Entity*, long> at +0x1024 registered once from
//     GameInitialise; callers of Add never construct entities directly.
//
// Field names / offsets match binary ctor + RE of Initialise, Add,
// Deactivate, Update, Draw, Find, SendMessage (2026-05-04).
// sizeof(ActorManager) == 4204 (0x106C).
//
// Not modelled (stubbed or omitted):
//   - LinkedHeap allocator at +0x000 — port uses new[] for the type-list
//     array and leaves m_pHeap as an opaque non-null sentinel so the
//     binary's `if (m_pHeap != nullptr)` gate in Update/Draw still behaves.
//   - Delegate2<long, ulong, bool&> hash converter at +0x1048 — not
//     called from any live FruitNinja code path.
//   - LoadEntity / PostLoad — tied to serialisation we don't implement.
//
// Analysed: 2026-05-04T00:00

#include "Entity.h"
#include "Message.h"
#include <cstdint>
#include <list>
#include <cstddef>

class ColAABB;
struct EntityChunk;   // opaque; only used by LoadEntity which is stubbed
struct Renderer;

namespace Mortar {

class ActorManager {
public:
    // Binary: Entity*[0x200] flat array at +0x008.
    static const int FREE_POOL_CAP = 512;

    // Factory delegate signature — matches Delegate1<Entity*, long>.
    typedef Entity* (*FactoryFn)(int entityType);

    // --- Fields mirrored from binary layout (sizes/offsets in comments) -

    // +0x000: LinkedHeap*. Port stores a sentinel (self ptr) once
    // Initialise runs, nullptr otherwise. Update/Draw short-circuit on nullptr
    // to match the binary's guard.
    void* m_pHeap;

    // +0x004
    int   m_HeapSize;

    // +0x008: 2048-byte free pool (Entity*[512]).
    Entity* m_FreePool[FREE_POOL_CAP];

    // +0x808: free-pool count, grows on Deactivate, shrinks on Add recycle.
    int m_FreeCount;

    // +0x80C: deferred-deactivation scratch used during Update. Entities
    // that set ENT_KILLED during their own Update are queued here so the
    // type-list iterator isn't invalidated by a mid-loop erase. Drained
    // after the full type sweep. Binary @ 0x001701f4.
    // Binary @ 0x001701f4: Entity*[512] (2048 bytes). W4 TODO closed.
    Entity* m_PendingDeact[512];

    // +0x100C: count of pending-deact entries.
    int m_PendingDeactCount;

    // +0x1010: pointer to heap-allocated std::list<Entity*> array of
    // m_NumTypes slots. Each list is the active entities of that type.
    std::list<Entity*>* m_pTypeLists;

    // +0x1014: message-listener list (8 bytes, Sourcery 2010q1 pre-C++11).
    std::list<Mortar::MessageListener*> m_Listeners;

    // +0x101C
    int  m_NumTypes;

    // +0x1020
    bool m_DebugDraw;

    // +0x1024 (binary): factory function is Delegate1<Entity*, long> object (36 bytes).
    // DIFFERS: binary uses Delegate1<Entity*, long> object (36 bytes); port stores
    //          raw fnptr (4 bytes) at the same logical offset. Functionally equivalent
    //          for the singular call site. Layout deviates by 32 bytes from binary.
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

    // Hash converter delegate signature --
    //   Delegate2<long entityType, unsigned long& outHash, bool& outOk>.
    // Binary: ActorManager::RegisterHashConverter @ 0x001069f8 (PLT thunk).
    // Called from GameInit step 16c @ 0x0016cb9e..0x0016cc04.
    // RE-gap: exact function body behind GOT slots [0x0016ccbc..0x0016ccc0].
    // Binary signature: entityType is the return value; takes (StringHash key, bool& outFound).
    typedef long (*HashFn)(unsigned long key, bool& outFound);

    // Stores the hash-converter function into m_HashDelegate.
    // Binary: ActorManager::RegisterHashConverter @ 0x001069f8 (PLT thunk).
    // TODO: implement -- see docs/systems/gameinit-todos.md step 16.
    void RegisterHashConverter(HashFn fn);

    // +0x1048 (binary): hash converter is Delegate2<long, ulong, bool&> object (36 bytes).
    // DIFFERS: binary uses Delegate2 object (36 bytes); port stores raw fnptr (4 bytes).
    //          Binary offset +0x1048; port offset drifts further due to Delegate1 size diff.
    HashFn m_HashDelegate;

    // --- Entity API -----------------------------------------------------

    // 0x0017068c. Second bool param is declared but the binary body never
    // reads it — every caller passes `true`. Kept in the signature so
    // call sites don't rename; body ignores it.
    Entity* Add(int entityType, bool /*unused*/ = true);

    // 0x00170654. Push a pre-constructed entity into a type list without
    // going through the factory. Used by LoadEntity; exposed here for
    // parity but currently only stubbed callers use it.
    // Defunct: zero live in-binary callers (only LoadEntity, itself dead); binary @ 0x00170654.
    Entity* Add(Entity* entity, long typeIdx);

    // 0x00170184. Erase from its type list, set ENT_INACTIVE, append to
    // free pool. Binary-faithful: no virtual call. Subclass emitter
    // cleanup happens before this in the Kill* helpers (KillBomb,
    // KillFruit, Coin::Arrived).
    void Deactivate(Entity* entity);

    // 0x001702d8. Erase + (unless ENT_NO_DESTRUCT) delete.
    // Dtor calls Release() for per-type cleanup.
    // Defunct: zero in-binary callers; binary @ 0x001702d8.
    void Remove(Entity* entity);

    // 0x0016fb44. Set ENT_KILLED on every entity of the given type so
    // the next Update sweep drains them into the free pool.
    // Defunct: zero in-binary callers; binary @ 0x0016fb44.
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
    // Defunct: zero in-binary callers; binary @ 0x0016fe1c.
    void DrawDebug();

    // Binary @ 0x0016fdc8 -- iterates m_pTypeLists and calls entity->vtable[+0x1c]
    // (slot 7, PostLoad). Defunct: zero in-binary callers; LoadEntity is itself dead.
    // Port keeps stub body as no-op for safety.
    void PostLoad();

    // --- Query API (binary: four GetNumEntities overloads, +Find/Get) --

    // 0x0016ff98. Raw list size — NO active-flag filtering (matches
    // binary; the old port's IsActive() filter diverged and caused
    // MainScreen state transitions to fire on wrong counts).
    int  GetNumEntities(int typeIdx);

    // 0x0016ffac. Sum of all type-list sizes.
    int  GetNumEntities();

    // 0x0016ff30. Null-terminated type-index array variant.
    int  GetNumEntities(const long* typeIdxNullTerminated);

    // 0x0016ff5c. Range variant: sums [min(a,b) .. max(a,b)).
    int  GetNumEntities(long typeA, long typeB);

    // 0x0016ff00. Count of type lists that contain at least one entity.
    int  GetNumTypes();

    // Accessor for per-type iteration. Two APIs:
    //
    //   GetEntityFirst / GetEntityNext -- binary-faithful iterator pair
    //   (0x0016fbb8 / 0x0016fb88). Caller owns the iterator; functions
    //   return the Entity* at the current node, or nullptr when past
    //   end. Prefer these for call sites that mirror binary loops so
    //   the port reads 1:1 with the disassembly.
    //
    //   GetTypeList -- port-only convenience returning the underlying
    //   std::list<Entity*> by const ref so range-for works. Useful for
    //   read-only counts; don't use where the binary explicitly uses
    //   the iterator API.
    Entity* GetEntityFirst(int typeIdx, std::list<Entity*>::iterator& it);
    Entity* GetEntityNext(int typeIdx, std::list<Entity*>::iterator& it);

    const std::list<Entity*>& GetTypeList(int typeIdx) const {
        static const std::list<Entity*> s_empty;
        if (!m_pTypeLists || typeIdx < 0 || typeIdx >= m_NumTypes) return s_empty;
        return m_pTypeLists[typeIdx];
    }

    // 0x0016fcc4. Entity at ordinal `slot` in type list `typeIdx`.
    Entity* GetEntity(int typeIdx, size_t slot) const;

    // 0x0016fc64. Ordinal index of `entity` in its type list, or -1.
    // Defunct: zero in-binary callers; binary @ 0x0016fc64.
    int GetEntityIdx(Entity* entity);

    // 0x0016fd10. Linear scan of the type list `type`; returns the first
    // entity whose m_TrackerID (Entity+0x04) equals `trackerKey`.
    Entity* Find(long type, unsigned long trackerKey);

    // 0x0016fd58. Type-agnostic linear scan; returns first entity whose
    // m_TrackerID matches `trackerKey` across all type lists.
    Entity* Find(unsigned long trackerKey);

    // 0x0016fbec. Count entities whose vtable+0x20 (InRect(ColAABB*)) collision
    // test passes against `aabb`. Port stub returns 0.
    // Defunct: zero in-binary callers; binary @ 0x0016fbec.
    int GetNumInAABB(ColAABB* aabb);

    // --- Level deserialiser (stubbed — .lvl loading not used by FN) -----

    // 0x00170728. EntityChunk deserialise; LOD scale + AABB->pos/size + Init.
    // Port stub returns false -- not used by FruitNinja runtime.
    // Defunct: zero in-binary callers; binary @ 0x00170728.
    bool LoadEntity(EntityChunk* chunk, void* hdr, long hdrLen, long lod);

    // --- Heap diagnostics -----------------------------------------------

    // 0x0016fb38. Binary returns m_HeapSize from LinkedHeap; port returns
    // m_HeapSize directly (no heap pressure in port).
    int  GetHeapSize() const { return m_HeapSize; }

    // 0x00170370. Binary forwards to LinkedHeap::GetTotalFreeMemory; port
    // returns m_HeapSize (no allocation tracking).
    // Defunct: zero in-binary callers; binary @ 0x00170370.
    int  GetHeapFree() const;

    // 0x00170364. Binary forwards to LinkedHeap::DisplayUsage(verbose).
    // Defunct: zero in-binary callers; binary @ 0x00170364.
    void HeapDisplay(bool verbose);

    // 0x00170354. Binary forwards to LinkedHeap::DisplayUsage(true) gated.
    // Defunct: zero in-binary callers; binary @ 0x00170354.
    void DisplayUsage(bool dumpAll);

    // 0x0016fb3c. Setter for m_DebugDraw.
    void SetCollisionVisible(unsigned char v) { m_DebugDraw = (v != 0); }

    // --- Messaging ------------------------------------------------------
    // Defunct: Mortar messaging subsystem -- zero in-binary callers for all
    // four methods. Preserved for call-graph fidelity only.

    // 0x0016ffd8. Filter listeners, fire callback->vtable[+0x30], one-shot
    // clear, then dispatch target->ReceiveMessage(sender, msg).
    // Defunct: zero in-binary callers; binary @ 0x0016ffd8.
    bool SendMessage(unsigned long typeHash, Entity* sender, Mortar::Message* msg);

    // 0x0017085c. m_Listeners.push_back(L).
    // Defunct: zero in-binary callers; binary @ 0x0017085c.
    void AddMessageListener(Mortar::MessageListener* listener);

    // 0x00170124. m_Listeners.remove(L).
    // Defunct: zero in-binary callers; binary @ 0x00170124.
    void RemoveMessageListener(Mortar::MessageListener* listener);

    // 0x0017013c. Delete each listener, then clear list.
    // Defunct: zero in-binary callers; binary @ 0x0017013c.
    void ClearAllListeners();

#ifdef __bada__
    friend struct ActorManagerLayoutAssert;
#endif
};

// Layout asserts. m_FactoryDelegate / m_HashDelegate offsets deviate from binary because:
//   m_FactoryDelegate is raw fnptr (4B) vs Delegate1 object (36B) (+32B drift)
// Those two offsets are excluded. All list-containing fields use 8B (Sourcery 2010q1).
#ifdef __bada__
struct ActorManagerLayoutAssert {
    static_assert(offsetof(ActorManager, m_FreePool)          == 0x008,  "m_FreePool offset");
    static_assert(offsetof(ActorManager, m_FreeCount)         == 0x808,  "m_FreeCount offset");
    static_assert(offsetof(ActorManager, m_PendingDeact)      == 0x80C,  "m_PendingDeact offset");
    static_assert(offsetof(ActorManager, m_PendingDeactCount) == 0x100C, "m_PendingDeactCount offset");
    static_assert(offsetof(ActorManager, m_pTypeLists)        == 0x1010, "m_pTypeLists offset");
    static_assert(offsetof(ActorManager, m_Listeners)         == 0x1014, "m_Listeners offset");
    static_assert(offsetof(ActorManager, m_NumTypes)          == 0x101C, "m_NumTypes offset");
    static_assert(offsetof(ActorManager, m_DebugDraw)         == 0x1020, "m_DebugDraw offset");
};
#endif

}  // namespace Mortar

#endif  // FN_ACTOR_MANAGER_H
