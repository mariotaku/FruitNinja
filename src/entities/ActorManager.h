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

namespace Mortar { class ColAABB; }
struct EntityChunk;   // opaque; only used by LoadEntity which is stubbed
struct Renderer;

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
    Entity* m_PendingDeact[256];

    // +0x100C: count of pending-deact entries.
    int m_PendingDeactCount;

    // +0x1010: pointer to heap-allocated std::list<Entity*> array of
    // m_NumTypes slots. Each list is the active entities of that type.
    std::list<Entity*>* m_pTypeLists;

    // +0x1014: message-listener list. Binary: std::list<MessageListener*>
    // (12 bytes on Bada libstdc++). Populated by AddMessageListener /
    // cleared by ClearAllListeners / iterated by SendMessage.
    std::list<Mortar::MessageListener*> m_Listeners;

    // +0x1020: count of types passed to Initialise.
    int  m_NumTypes;

    // +0x1024: debug flag — Draw calls DrawDebug() when non-zero.
    bool m_DebugDraw;

    // +0x1028: factory function used when the free pool has no matching
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

    // Hash converter delegate signature --
    //   Delegate2<long entityType, unsigned long& outHash, bool& outOk>.
    // Binary: Mortar::ActorManager::RegisterHashConverter @ 0x001069f8 (PLT thunk).
    // Called from GameInit step 16c @ 0x0016cb9e..0x0016cc04.
    // RE-gap: exact function body behind GOT slots [0x0016ccbc..0x0016ccc0].
    // Binary signature: entityType is the return value; takes (StringHash key, bool& outFound).
    typedef long (*HashFn)(unsigned long key, bool& outFound);

    // Stores the hash-converter function into m_HashDelegate.
    // Binary: Mortar::ActorManager::RegisterHashConverter @ 0x001069f8 (PLT thunk).
    // TODO: implement -- see docs/systems/gameinit-todos.md step 16.
    void RegisterHashConverter(HashFn fn);

    // +0x104C: hash converter (Delegate2 slot, see HashFn typedef above).
    // DIFFERS: original binary offset +0x1048 from original field layout; offset
    // shifts by 4 after m_DebugDraw bool padding correction.
    HashFn m_HashDelegate;

    // --- Entity API -----------------------------------------------------

    // 0x0017068c. Second bool param is declared but the binary body never
    // reads it — every caller passes `true`. Kept in the signature so
    // call sites don't rename; body ignores it.
    Entity* Add(int entityType, bool /*unused*/ = true);

    // 0x00170654. Push a pre-constructed entity into a type list without
    // going through the factory. Used by LoadEntity; exposed here for
    // parity but currently only stubbed callers use it.
    Entity* Add(Entity* entity, long typeIdx);

    // 0x00170184. Erase from its type list, set ENT_INACTIVE, append to
    // free pool. Binary-faithful: no virtual call. Subclass emitter
    // cleanup happens before this in the Kill* helpers (KillBomb,
    // KillFruit, Coin::Arrived).
    void Deactivate(Entity* entity);

    // 0x001702d8. Erase + (unless ENT_NO_DESTRUCT) delete.
    // Dtor calls Release() for per-type cleanup.
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
    int GetEntityIdx(Entity* entity) const;

    // 0x0016fd10. Linear scan of the type list `type`; returns the first
    // entity whose m_TrackerID (Entity+0x04) equals `trackerKey`.
    Entity* Find(long type, unsigned long trackerKey);

    // 0x0016fd58. Type-agnostic linear scan; returns first entity whose
    // m_TrackerID matches `trackerKey` across all type lists.
    Entity* Find(unsigned long trackerKey);

    // 0x0016fbec. Count entities whose vtable+0x20 (InRect) collision test
    // passes against `aabb`. Port gates on Entity::InRect being wired.
    int GetNumInAABB(Mortar::ColAABB* aabb);

    // --- Level deserialiser (stubbed — .lvl loading not used by FN) -----

    // 0x00170728. EntityChunk deserialise; LOD scale + AABB->pos/size + Init.
    // Port stub returns false — not used by FruitNinja runtime.
    bool LoadEntity(EntityChunk* chunk, void* hdr, long hdrLen, long lod);

    // --- Heap diagnostics -----------------------------------------------

    // 0x0016fb38. Binary returns m_HeapSize from LinkedHeap; port returns
    // m_HeapSize directly (no heap pressure in port).
    int  GetHeapSize() const { return m_HeapSize; }

    // 0x00170370. Binary forwards to LinkedHeap::GetTotalFreeMemory; port
    // returns m_HeapSize (no allocation tracking).
    int  GetHeapFree() const;

    // 0x00170364. Binary forwards to LinkedHeap::DisplayUsage(verbose).
    void HeapDisplay(bool verbose);

    // 0x00170354. Binary forwards to LinkedHeap::DisplayUsage(true) gated.
    void DisplayUsage(bool dumpAll);

    // 0x0016fb3c. Setter for m_DebugDraw.
    void SetCollisionVisible(unsigned char v) { m_DebugDraw = (v != 0); }

    // --- Messaging ------------------------------------------------------
    // Defunct: Mortar messaging — no-op stub; binary @ 0x0016ffd8 (Send),
    //   0x0017085c (Add), 0x00170124 (Remove). Listener subsystem wired but
    //   never instantiated in shipped retail.

    // 0x0016ffd8. Filter listeners, fire callback->vtable[+0x30], one-shot
    // clear, then dispatch target->ReceiveMessage(sender, msg).
    bool SendMessage(unsigned long typeHash, Entity* sender, Mortar::Message* msg);

    // 0x0017085c. m_Listeners.push_back(L).
    void AddMessageListener(Mortar::MessageListener* listener);

    // 0x00170124. m_Listeners.remove(L).
    void RemoveMessageListener(Mortar::MessageListener* listener);

    // 0x0017013c. Delete each listener, then clear list.
    void ClearAllListeners();
};

// Confirmed-correct offsets (toolchain patch makes these assertable):
#ifdef __bada__
static_assert(offsetof(ActorManager, m_FreePool)  == 0x008, "m_FreePool offset");
static_assert(offsetof(ActorManager, m_FreeCount) == 0x808, "m_FreeCount offset");
static_assert(offsetof(ActorManager, m_PendingDeact) == 0x80C, "m_PendingDeact offset");
#endif

// TODO: 0x00170500 -- ActorManager m_PendingDeact array size mismatch: port has
// Entity*[256] (1024B) but binary's m_PendingDeactCount lives at +0x100C, which
// implies Entity*[512] (2048B). Cross-build measures m_PendingDeactCount at
// +0xC0C, m_pTypeLists at +0xC10, m_Listeners at +0xC14, m_NumTypes at +0xC20,
// sizeof = 0xC30. Widening the array to [512] makes all later offsets match
// binary. Re-validate via asm-inspector before flipping.

#endif  // FN_ACTOR_MANAGER_H
