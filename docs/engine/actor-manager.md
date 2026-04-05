# ActorManager Functions

## Overview

`Mortar::ActorManager` is the engine's entity pool manager. It maintains per-type linked lists of active entities and a free pool for recycling.

**Singleton**: `GetInstance()` at 0x1705f0, lazy-init with `__cxa_guard`.

## Struct Layout (~0x106C bytes)

```
+0x000: LinkedHeap*           m_pHeap         // memory allocator
+0x004: int                   m_HeapSize      // heap capacity
+0x008: Entity*[0x200]        m_FreePool      // free entity array (max 512)
+0x808: int                   m_FreeCount     // number of free entities in pool
+0x80c: ...                   (padding/unknown)
+0x100c: int                  field_0x100c
+0x1010: std::list<Entity*>*  m_TypeLists     // array of per-type entity lists
+0x1014: std::list<MessageListener*>  m_Listeners  // message listeners
+0x101c: int                  m_NumTypes      // number of entity types (5 for FruitNinja)
+0x1020: char                 m_DebugDraw     // debug visualization flag
+0x1024: Delegate1<Entity*,long>  m_FactoryDelegate  // entity factory callback
+0x1048: Delegate2<long,ulong,bool&>  m_HashConverter  // hash→type converter
```

Entity types in FruitNinja (from GameInit):
```
0 = Fruit
1 = Bomb
2 = (unused?)
3 = (unused?)
4 = BombBlast
```

Initialized with: `Initialise(5 types, 0x2000 heap size)`

---

## GetInstance (0x001705f0, 68 bytes)

Lazy singleton with `__cxa_guard`:

```cpp
ActorManager& ActorManager::GetInstance() {
    static ActorManager em;   // constructed on first call
    return em;
}
```

---

## Initialise (0x0017046c, 110 bytes)

```cpp
void ActorManager::Initialise(int numTypes, int heapSize) {
    m_HeapSize = heapSize;
    m_pHeap = new LinkedHeap(heapSize);
    m_NumTypes = numTypes;
    
    // Allocate array of (numTypes+1) std::list<Entity*>
    m_TypeLists = m_pHeap->Allocate((numTypes + 1) * 8);
    for (int i = numTypes - 1; i >= 0; i--)
        std::list<Entity*>::list();   // init each list
    
    Entity::HeapExist();  // set global heap-exists flag
}
```

---

## Add (0x0017068c, 154 bytes)

Allocates an entity of the given type. First searches the free pool for a recycled entity of the same type; if none found, calls the factory delegate to create a new one.

```cpp
Entity* ActorManager::Add(int entityType, bool activate) {
    Entity* entity = NULL;
    
    // Search free pool backwards for matching type
    for (int i = m_FreeCount - 1; i >= 0; i--) {
        if (m_FreePool[i]->m_EntityType == entityType) {
            entity = m_FreePool[i];
            // Remove from free pool (shift array down)
            m_FreeCount--;
            for (int j = i; j < m_FreeCount; j++)
                m_FreePool[j] = m_FreePool[j + 1];
            // Add to active list
            m_TypeLists[entityType].push_back(entity);
            Entity::Activate(entity);
            return entity;
        }
    }
    
    // No recycled entity found → create via factory
    entity = m_FactoryDelegate(entityType);
    if (entity == NULL) return NULL;
    
    m_TypeLists[entityType].push_back(entity);
    entity->m_EntityType = entityType;  // field_0x35
    entity->field_0x34 = 0;
    return entity;
}
```

**Key**: `m_EntityType` is at Entity+0x35 (byte). The free pool is a flat array at +0x08, searched in reverse.

---

## Update (0x001701f4, 228 bytes)

Per-frame entity update. Iterates all entity types, calls Update + PostUpdate on active entities, then deactivates flagged ones.

```cpp
void ActorManager::Update(float dt, ColAABB* bounds1, ColAABB* bounds2) {
    // bounds1 appears to contain a deactivation queue at specific offsets
    
    if (m_pHeap == NULL || m_TypeLists == NULL) return;
    
    for (int type = 0; type < m_NumTypes; type++) {
        for (Entity* e : m_TypeLists[type]) {
            byte flags = e->flags;  // at entity+0x0c (byte 3)
            if ((flags & 0x11) == 0) {    // not deactivated, not paused
                flags |= 0x0c;            // mark as "updating"
                e->vtable->Update(dt);    // vtable +0x10
                e->vtable->PostUpdate(dt); // vtable +0x18
            }
            if ((e->flags & 0x10) != 0) {  // marked for deactivation
                // Add to deactivation queue
                deactivationQueue[deactivationCount++] = e;
            }
        }
    }
    
    // Process deactivation queue
    for (int i = 0; i < deactivationCount; i++) {
        Deactivate(deactivationQueue[i]);
    }
    deactivationCount = 0;
}
```

### Entity Flags (at Entity + 0x0c, byte 3)

| Bit | Meaning |
|-----|---------|
| 0x01 | Deactivated / inactive |
| 0x04 | Updating (set during Update) |
| 0x08 | Post-updating (set during Update) |
| 0x10 | Pending deactivation (processed at end of frame) |
| 0x20 | Don't call destructor on Remove |

### Entity Vtable Offsets

| Offset | Method |
|--------|--------|
| +0x04 | Destructor |
| +0x0c | OnDeactivate |
| +0x10 | Update(float dt) |
| +0x14 | Draw() |
| +0x18 | PostUpdate(float dt) |

---

## Draw (0x0016fe7c, 132 bytes)

Renders all active entities by calling their virtual Draw method.

```cpp
void ActorManager::Draw() {
    if (m_pHeap == NULL || m_TypeLists == NULL) return;
    
    for (int type = 0; type < m_NumTypes; type++) {
        for (Entity* e : m_TypeLists[type]) {
            if ((e->flags & 0x11) == 0) {   // active and not paused
                e->vtable->Draw();           // vtable +0x14
            }
        }
    }
    
    if (m_DebugDraw)
        DrawDebug();
}
```

---

## Deactivate (0x00170184, ~50 bytes)

Moves an entity from its active type list back to the free pool.

```cpp
void ActorManager::Deactivate(Entity* entity) {
    // Find in type list
    auto& list = m_TypeLists[entity->m_EntityType];
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (*it == entity) {
            list.erase(it);
            // Add to free pool
            m_FreePool[m_FreeCount] = entity;
            m_FreeCount++;
            return;
        }
    }
}
```

---

## Remove (0x001702d8, ~60 bytes)

Destroys an entity (calls destructor) and removes from the type list.

```cpp
void ActorManager::Remove(Entity* entity) {
    auto& list = m_TypeLists[entity->m_EntityType];
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (*it == entity) {
            if ((entity->flags & 0x20) == 0) {  // unless "no destruct" flag
                entity->vtable->OnDeactivate();  // vtable +0x0c
                entity->vtable->Destructor();    // vtable +0x04, then delete
            }
            list.erase(it);
            return;
        }
    }
}
```

Unlike `Deactivate`, `Remove` does NOT return the entity to the free pool — it destroys it.

---

## GetNumEntities (0x0016ffac, 18 bytes / 0x0016ff98, 18 bytes)

```cpp
// All types
int ActorManager::GetNumEntities() {
    int total = 0;
    for (int i = 0; i < m_NumTypes; i++)
        total += m_TypeLists[i].size();
    return total;
}

// Single type
int ActorManager::GetNumEntities(int type) {
    return m_TypeLists[type].size();
}
```

---

## Entity Lifecycle

```
1. Add(type, true)
   → search free pool for matching type
   → if found: recycle (remove from free, push to type list, Activate)
   → if not: factory creates new entity, push to type list
   
2. Update(dt)
   → for each active entity: vtable->Update(dt), vtable->PostUpdate(dt)
   → collect entities with deactivation flag (0x10)
   → Deactivate all flagged → move from type list to free pool
   
3. Draw()
   → for each active entity: vtable->Draw()

4. Deactivate(entity)
   → remove from type list, add to free pool (recycled for future Add)
   
5. Remove(entity)
   → call OnDeactivate + Destructor, erase from type list (no recycling)
```

---

## Key Functions

| Function | Address | Bytes | Purpose |
|----------|---------|-------|---------|
| GetInstance | 0x1705f0 | 68 | Lazy singleton |
| Initialise | 0x1704ac | 110 | Allocate heap + type lists |
| Add | 0x17068c | 154 | Allocate/recycle entity |
| Update | 0x1701f4 | 228 | Tick all entities + deactivate |
| Draw | 0x16fe7c | 132 | Render all entities |
| Deactivate | 0x170184 | ~50 | Move to free pool |
| Remove | 0x1702d8 | ~60 | Destroy + erase |
| GetNumEntities() | 0x16ffac | 18 | Count all active |
| GetNumEntities(type) | 0x16ff98 | 18 | Count per type |
| ActorManager ctor | 0x170578 | 94 | Init fields + delegates |

---

## See Also

- [Entity base](../entities/entity-base.md) -- Mortar::Entity base class
- [Fruit entity](../entities/fruit.md) -- Fruit struct layout
- [Bomb entity](../entities/bomb.md) -- Bomb struct layout
- [SlashEntity](../entities/slash-entity.md) -- SlashEntity struct layout
- [Game loop](game-loop.md) -- GameUpdate calls ActorManager::Update
- [Wave functions](wave.md) -- SpawnFruit/SpawnBomb call ActorManager::Add
