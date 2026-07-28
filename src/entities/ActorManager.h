#ifndef FN_ACTOR_MANAGER_H
#define FN_ACTOR_MANAGER_H

// ActorManager — binary-faithful port.
//
// Binary class is 4204 bytes (0x106C). Gameplay relies on:
//   - per-type std::list<Entity*> so GetNumEntities(type) is O(1) and
//     DeactivateAllEntities(type) / iterator walks work as expected;
//   - a 512-slot free pool used to recycle entities between waves
//     instead of new/delete churn;
//   - a factory Mortar::Delegate1<Entity*, long> at +0x1024 registered once
//     from GameInit; callers of Add never construct entities directly.
//
// Field names / offsets match binary ctor + RE of Initialise, Add,
// Deactivate, Update, Draw, Find, SendMessage.
// sizeof(ActorManager) == 4204 (0x106C).
//
// Not modelled (stubbed or omitted):
//   - LinkedHeap allocator at +0x000 — port uses new[] for the type-list
//     array and leaves m_pHeap as an opaque non-null sentinel so the
//     binary's `if (m_pHeap != nullptr)` gate in Update/Draw still behaves.
//   - LoadEntity / PostLoad — tied to serialisation we don't implement. The
//     hash-converter delegate at +0x1048 IS wired (RegisterHashConverter is
//     real), but its only binary caller is LoadEntity, itself unreachable.
//
// All addresses below are v1.6.1; the whole TU is 0x001d2e80-0x001d4228.
// LIVENESS METHOD NOTE: the binary is PIC, so direct xrefs are useless -- every
// intra-image call routes through `.plt`. "Does this symbol have a .plt thunk"
// is the liveness test; a `.got` self-slot is just the PIC prologue and proves
// nothing. Several "zero in-binary callers" notes in earlier revisions of this
// file were wrong for exactly that reason (DrawDebug, ClearAllListeners).

#include "Entity.h"
#include "Message.h"
#include "engine/util/Delegate.h"
#include <cstdint>
#include <list>
#include <cstddef>

class ColAABB;
struct Renderer;

namespace Mortar {

struct EntityChunk;   // opaque; only used by LoadEntity which is stubbed

class ActorManager {
public:
    // Binary: Entity*[0x200] flat array at +0x008.
    static const int FREE_POOL_CAP = 512;

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
    // after the full type sweep. v1.6.1 ActorManager::Update @0x001d38f0:
    // Entity*[512] (2048 bytes).
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

    // +0x1024: Mortar::Delegate1<Entity*, long> (36 bytes in binary).
    Mortar::Delegate1<Entity*, long> m_FactoryDelegate;

    // +0x1048: Mortar::Delegate2<long, unsigned long, bool&> (36 bytes in binary).
    // Registered from GameInit with HashTypeConvert; never invoked at runtime
    // (its only binary caller is LoadEntity @0x001d408c, itself unreachable).
    Mortar::Delegate2<long, unsigned long, bool&> m_HashDelegate;

    // Singleton — binary uses Meyers static local `em` inside
    // GetInstance @0x001d3ec0. Port exposes the same access pattern.
    static ActorManager* s_Instance;

    ActorManager();
    ~ActorManager();

    static ActorManager* GetInstance();

    // --- Lifecycle (v1.6.1: Initialise @0x001d3ca4 / Destroy @0x001d3b44 /
    //     Clear @0x001d3690) --------------------------------------------

    // Initialise is NOT re-entrant in the binary -- it allocates a fresh
    // LinkedHeap + list array on every call. The port adds an early-out guard
    // (see the DIFFERS block in ActorManager.cpp) because the port has two
    // call sites (GameInitialise + GameInit) where the binary has one; the
    // first call wins, so both must pass the same numTypes.
    void Initialise(int numTypes, int heapSize = 0x2000);
    void Destroy();
    void Clear();

    // --- Factory / listener registration --------------------------------

    // RegisterFactory @0x001d0378: `m_FactoryDelegate = param;`.
    // By-value Delegate1 param matches the binary's copy-in mangling.
    void RegisterFactory(Mortar::Delegate1<Entity*, long> factory) { m_FactoryDelegate = factory; }

    // v1.6.1 Mortar::ActorManager::RegisterHashConverter @0x001d0460.
    // Body: `m_HashDelegate = param;` (by-value Delegate2 param).
    // Called from GameInit step 16c @ 0x0016cb9e..0x0016cc04 with HashTypeConvert.
    void RegisterHashConverter(Mortar::Delegate2<long, unsigned long, bool&> converter);

    // --- Entity API -----------------------------------------------------

    // v1.6.1 Add(long,bool) @0x001d3fac. Second bool param is declared but the binary body never
    // reads it — every caller passes `true`. Kept in the signature so
    // call sites don't rename; body ignores it.
    Entity* Add(int entityType, bool /*unused*/ = true);

    // v1.6.1 Add(Entity*,long) @0x001d3f54. Push a pre-constructed entity into
    // a type list without going through the factory.
    // Defunct: zero in-binary callers -- the symbol has NO .plt thunk at all.
    //   (LoadEntity @0x001d408c is NOT its caller: LoadEntity calls the other
    //   overload, Add(long,bool) @0x001d3fac, via PLT 0x001168e4.) No-op-
    //   equivalent stub; v1.6.1 ActorManager::Add(Entity*,long) @ 0x001d3f54.
    Entity* Add(Entity* entity, long typeIdx);

    // v1.6.1 Deactivate @0x001d3854. Erase from its type list, set ENT_INACTIVE, append to
    // free pool. Binary-faithful: no virtual call. Subclass emitter
    // cleanup happens before this in the Kill* helpers (KillBomb,
    // KillFruit, Coin::Arrived).
    void Deactivate(Entity* entity);

    // v1.6.1 Remove @0x001d3a44. Erase + (unless ENT_NO_DESTRUCT) delete.
    // Dtor calls Release() for per-type cleanup.
    // Defunct: zero in-binary callers (no .plt thunk); v1.6.1 ActorManager::Remove @ 0x001d3a44.
    void Remove(Entity* entity);

    // v1.6.1 DeactivateAllEntities @0x001d2e94. Set ENT_KILLED on every entity
    // of the given type so the next Update sweep drains them into the free pool.
    // Defunct: zero in-binary callers (no .plt thunk); v1.6.1 ActorManager::DeactivateAllEntities @ 0x001d2e94.
    void DeactivateAllEntities(int typeIdx);

    // --- Per-frame update / draw ---------------------------------------

    // v1.6.1 Update @0x001d38f0. Binary signature takes two `ColAABB*` bounds that are
    // unused in FruitNinja; port keeps the dt-only form. Iterates all
    // type lists, updates active entities, drains killed entities via
    // Deactivate.
    void Update(float dt);

    // v1.6.1 Draw @0x001d3380. Binary takes no Renderer (entities use an implicit
    // global); port threads Renderer& through for uniformity with the
    // rest of the draw pipeline — documented divergence.
    void Draw(Renderer& r);

    // v1.6.1 DrawDebug @0x001d32e0. Per-entity collider debug draw: for every
    // entity in every type list, dispatch Col::DrawDebug (Col vtable +0x10) on
    // its m_Col (+0x38), if any. Self-gated on m_DebugDraw.
    // NOT defunct: it has .plt thunk 0x0010c884 and ActorManager::Draw
    // @0x001d3380 calls it -- it is debug-GATED, not dead. Nothing in the port
    // currently calls SetCollisionVisible, so the loop is inert in practice.
    void DrawDebug();

    // v1.6.1 PostLoad @0x001d3258 -- iterates m_pTypeLists and calls
    // entity->vtable[+0x1c] (slot 7, PostLoad).
    // Defunct: zero in-binary callers (no .plt thunk); LoadEntity @0x001d408c,
    //   which would be its caller, is itself unreachable; no-op stub;
    //   v1.6.1 ActorManager::PostLoad @ 0x001d3258.
    void PostLoad();

    // --- Query API (binary: four GetNumEntities overloads, +Find/Get) --

    // v1.6.1 GetNumEntities(long) @0x001d3544. Raw list size — NO active-flag filtering (matches
    // binary; the old port's IsActive() filter diverged and caused
    // MainScreen state transitions to fire on wrong counts).
    int  GetNumEntities(int typeIdx);

    // v1.6.1 GetNumEntities() @0x001d3554. Sum of all type-list sizes.
    int  GetNumEntities();

    // v1.6.1 GetNumEntities(long*) @0x001d349c. Type-index array variant,
    // terminated by a -1 sentinel (NOT 0).
    int  GetNumEntities(const long* typeIdxNullTerminated);

    // v1.6.1 GetNumEntities(long,long) @0x001d34e0. Range variant: sums [min(a,b) .. max(a,b)).
    int  GetNumEntities(long typeA, long typeB);

    // v1.6.1 GetNumTypes @0x001d3454. Count of type lists that contain at least one entity.
    int  GetNumTypes();

    // Accessor for per-type iteration. Two APIs:
    //
    //   GetEntityFirst / GetEntityNext -- binary-faithful iterator pair
    //   (GetEntityFirst @0x001d2f48 / GetEntityNext @0x001d2f00). Caller owns
    //   the iterator; functions
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

    // v1.6.1 GetEntity @0x001d30d4. Entity at ordinal `slot` in type list `typeIdx`.
    Entity* GetEntity(int typeIdx, size_t slot) const;

    // v1.6.1 GetEntityIdx @0x001d3044. Ordinal index of `entity` in its type list, or -1.
    // Defunct: zero in-binary callers (no .plt thunk); v1.6.1 ActorManager::GetEntityIdx @ 0x001d3044.
    int GetEntityIdx(Entity* entity);

    // v1.6.1 Find(long,ulong) @0x001d3148. Linear scan of the type list `type`; returns the first
    // entity whose m_TrackerID (Entity+0x04) equals `trackerKey`.
    Entity* Find(long type, unsigned long trackerKey);

    // v1.6.1 Find(ulong) @0x001d31b8. Type-agnostic linear scan; returns first
    // entity whose m_TrackerID matches `trackerKey` across all type lists.
    // This is the overload SendMessage @0x001d3598 uses.
    Entity* Find(unsigned long trackerKey);

    // v1.6.1 GetNumInAABB @0x001d2f98. Count entities whose vtable+0x20
    // (InRect(ColAABB*)) collision test passes against `aabb`. Port stub returns 0.
    // Defunct: zero in-binary callers (no .plt thunk); v1.6.1 ActorManager::GetNumInAABB @ 0x001d2f98.
    int GetNumInAABB(ColAABB* aabb);

    // --- Level deserialiser (stubbed — .lvl loading not used by FN) -----

    // v1.6.1 LoadEntity @0x001d408c. EntityChunk deserialise; LOD scale +
    // AABB->pos/size + Init. Port stub returns false -- not used at runtime.
    // Defunct: zero in-binary callers (no .plt thunk); v1.6.1 ActorManager::LoadEntity @ 0x001d408c.
    //
    // Spec for the (unimplemented) invoke shape, now that m_HashDelegate is a
    // real Delegate2 object:
    //   bool found = false;
    //   long t = m_HashDelegate(hash, found);
    //   if (t == -1 || !Add(t, found)) return false;
    bool LoadEntity(EntityChunk* chunk, void* hdr, long hdrLen, long lod);

    // --- Heap diagnostics -----------------------------------------------

    // v1.6.1 GetHeapSize @0x001d2e80. Binary returns m_HeapSize from LinkedHeap;
    // port returns m_HeapSize directly (no heap pressure in port).
    int  GetHeapSize() const;

    // v1.6.1 GetHeapFree @0x001d3b34. Binary forwards to
    // LinkedHeap::GetTotalFreeMemory; port returns m_HeapSize (no tracking).
    // Defunct: zero in-binary callers (no .plt thunk); v1.6.1 ActorManager::GetHeapFree @ 0x001d3b34.
    int  GetHeapFree() const;

    // v1.6.1 HeapDisplay @0x001d3b24. Binary forwards to LinkedHeap::DisplayUsage(verbose).
    // Defunct: zero in-binary callers (no .plt thunk); v1.6.1 ActorManager::HeapDisplay @ 0x001d3b24.
    void HeapDisplay(bool verbose);

    // v1.6.1 DisplayUsage @0x001d3b08. Binary forwards to LinkedHeap::DisplayUsage(true) gated.
    // Defunct: zero in-binary callers (no .plt thunk); v1.6.1 ActorManager::DisplayUsage @ 0x001d3b08.
    void DisplayUsage(bool dumpAll);

    // Setter for m_DebugDraw. Enabling it makes ActorManager::Draw's DrawDebug
    // pass render every entity's collider.
    // TODO: v1.6.1 <addr unresolved> (ActorManager::SetCollisionVisible) -- v1.6.1
    // entry point not resolved; by ordinal it lies between GetHeapSize @0x001d2e80
    // and DeactivateAllEntities @0x001d2e94.
    void SetCollisionVisible(unsigned char v);

    // --- Messaging ------------------------------------------------------
    // Defunct: Mortar messaging subsystem. The precise state: AddMessageListener
    // @0x001d4208 has NO .plt thunk, so no listener is ever registered and
    // m_Listeners is empty for the whole process lifetime -- which is what makes
    // the subsystem inert. That is NOT the same as "no call sites":
    // ClearAllListeners @0x001d37e4 DOES have a .plt thunk (0x00112dac) and
    // GameExit calls it.

    // v1.6.1 SendMessage @0x001d3598. Filter listeners, fire
    // callback->vtable[+0x30], one-shot clear, then dispatch
    // target->ReceiveMessage(sender, msg). Target resolved via Find(ulong)
    // @0x001d31b8.
    // Defunct: zero in-binary callers (no .plt thunk); v1.6.1 ActorManager::SendMessage @ 0x001d3598.
    bool SendMessage(unsigned long typeHash, Entity* sender, Mortar::Message* msg);

    // v1.6.1 AddMessageListener @0x001d4208. m_Listeners.push_back(L).
    // Defunct: zero in-binary callers (no .plt thunk) -- this is the reason the
    //   listener list is always empty; v1.6.1 ActorManager::AddMessageListener @ 0x001d4208.
    void AddMessageListener(Mortar::MessageListener* listener);

    // v1.6.1 RemoveMessageListener @0x001d37c4. m_Listeners.remove(L).
    // Defunct: zero in-binary callers (no .plt thunk); v1.6.1 ActorManager::RemoveMessageListener @ 0x001d37c4.
    void RemoveMessageListener(Mortar::MessageListener* listener);

    // v1.6.1 ClearAllListeners @0x001d37e4. Delete each listener, then clear list.
    // NOT dead code: .plt thunk 0x00112dac, called by GameExit. It is a no-op in
    // practice only because m_Listeners is always empty (see AddMessageListener).
    void ClearAllListeners();

    // ---- Integer-width convenience overloads ----
    // Each binary address below resolves to a SINGLE mangled symbol. Ghidra's
    // plate comments show the binary uses `long` for the type-index param in
    // most of these (e.g. Add(long,bool) @0x001d3fac, GetEntityFirst(long,&)
    // @0x001d2f48, GetEntity(long,ulong) @0x001d30d4,
    // DeactivateAllEntities(long) @0x001d2e94). ARM32 has long == int == 4 bytes, so
    // the int-typed primary body and this long-typed overload compile to
    // identical code; the port keeps both so call sites of either width link.
    // The faithful binary symbol is the already-implemented + ASM-verified body
    // at the cited address; these forwarders are a port-only convenience with
    // no separate binary counterpart.

    // Port specific: int-width convenience forwarder; binary symbol @ 0x001d3fac is Add(long,bool).
    Entity* Add(long entityType, bool unused = true);

    // Port specific: int-width convenience forwarder; binary symbol @ 0x001d2e94 is DeactivateAllEntities(long).
    void DeactivateAllEntities(long typeIdx);

    // Port specific: no-arg Draw() IS the binary symbol @ 0x001d3380 (no Renderer
    //   arg); it resolves the renderer via Renderer::GetInstance() and forwards
    //   to the port-only Draw(Renderer&). The threaded-Renderer form is the
    //   port convenience, the no-arg form is binary-faithful.
    void Draw();

    // Port specific: int-width convenience forwarder; binary symbol @ 0x001d30d4 is GetEntity(long,ulong).
    Entity* GetEntity(long typeIdx, unsigned long slot) const;

    // Port specific: int-width convenience forwarder; binary symbol @ 0x001d2f48 is GetEntityFirst(long,&).
    Entity* GetEntityFirst(long typeIdx, std::list<Entity*>::iterator& it);

    // Port specific: int-width convenience forwarder; binary symbol @ 0x001d2f00 is GetEntityNext(long,&).
    Entity* GetEntityNext(long typeIdx, std::list<Entity*>::iterator& it);

    // Port specific: int-width convenience forwarder; binary symbol @ 0x001d3544 is GetNumEntities(long).
    int GetNumEntities(long typeIdx);

    // Port specific: non-const convenience forwarder; binary symbol @ 0x001d349c is GetNumEntities(long*).
    int GetNumEntities(long* typeIdxNullTerminated);

    // Port specific: int-width convenience forwarder; binary symbol @ 0x001d3ca4 is Initialise(int,int).
    void Initialise(long numTypes, long heapSize);

    // Bounds-taking Update(float,ColAABB*,ColAABB*) IS the binary symbol @
    // 0x001d38f0 -- the two ColAABB* bounds are genuinely dead in the binary
    // body, so forwarding to the dt-only port body is behaviourally faithful.
    void Update(float dt, ::ColAABB* boundsA, ::ColAABB* boundsB);
    // ---- end integer-width convenience overloads ----

#ifdef __bada__
    friend struct ActorManagerLayoutAssert;
#endif
};

// Layout asserts -- 32-bit (__bada__) only. Mortar::Delegate<Sig> is 36 bytes
// on any 32-bit ABI (40 bytes on x64, see Delegate.h), so m_FactoryDelegate /
// m_HashDelegate offsets and the final sizeof only line up with the binary
// under the bada cross-build; they are not checked on the x64 host build.
#ifdef __bada__
struct ActorManagerLayoutAssert {
    static_assert(__builtin_offsetof(ActorManager, m_FreePool)          == 0x008,  "m_FreePool offset");
    static_assert(__builtin_offsetof(ActorManager, m_FreeCount)         == 0x808,  "m_FreeCount offset");
    static_assert(__builtin_offsetof(ActorManager, m_PendingDeact)      == 0x80C,  "m_PendingDeact offset");
    static_assert(__builtin_offsetof(ActorManager, m_PendingDeactCount) == 0x100C, "m_PendingDeactCount offset");
    static_assert(__builtin_offsetof(ActorManager, m_pTypeLists)        == 0x1010, "m_pTypeLists offset");
    static_assert(__builtin_offsetof(ActorManager, m_Listeners)         == 0x1014, "m_Listeners offset");
    static_assert(__builtin_offsetof(ActorManager, m_NumTypes)          == 0x101C, "m_NumTypes offset");
    static_assert(__builtin_offsetof(ActorManager, m_DebugDraw)         == 0x1020, "m_DebugDraw offset");
    static_assert(__builtin_offsetof(ActorManager, m_FactoryDelegate)   == 0x1024, "m_FactoryDelegate offset");
    static_assert(__builtin_offsetof(ActorManager, m_HashDelegate)      == 0x1048, "m_HashDelegate offset");
    static_assert(sizeof(ActorManager)                                  == 0x106C, "sizeof(ActorManager)");
};
#endif

}  // namespace Mortar

#endif  // FN_ACTOR_MANAGER_H
