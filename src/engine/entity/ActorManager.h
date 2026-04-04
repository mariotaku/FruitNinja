#ifndef MORTAR_ACTOR_MANAGER_H
#define MORTAR_ACTOR_MANAGER_H

#include "entity/Entity.h"
#include "core/Singleton.h"
#include "util/Delegate.h"
#include <vector>
#include <list>
#include <functional>

namespace Mortar {

// Matches original ActorManager (~0x106C bytes)
// Per-type entity pool with free list recycling
class ActorManager : public Singleton<ActorManager> {
    friend class Singleton<ActorManager>;

public:
    static const int MAX_FREE_POOL = 512;
    static const int MAX_ENTITY_TYPES = 8;

    // Entity factory: given type index, returns new Entity*
    typedef std::function<Entity*(int)> FactoryDelegate;

    // Matches Initialise (0x001704ac)
    void Initialise(int numTypes, int heapSize = 0x2000);

    // Matches Add (0x0017068c)
    // Recycles from free pool or creates via factory
    Entity* Add(int entityType, bool activate = true);

    // Matches Update (0x001701f4)
    void Update(float dt);

    // Matches Draw (0x0016fe7c)
    void Draw();

    // Matches Deactivate (0x00170184)
    void Deactivate(Entity* entity);

    // Matches Remove (0x001702d8)
    void Remove(Entity* entity);

    // Set the factory delegate for creating new entities
    void SetFactoryDelegate(FactoryDelegate factory) { m_Factory = factory; }

    // Entity counts
    int GetNumEntities() const;
    int GetNumEntities(int type) const;

    // Access type lists (for game-specific iteration)
    const std::list<Entity*>& GetTypeList(int type) const { return m_TypeLists[type]; }

private:
    ActorManager();
    ~ActorManager();

    Entity* m_FreePool[MAX_FREE_POOL]; // +0x008
    int m_FreeCount;                   // +0x808
    std::list<Entity*>* m_TypeLists;   // +0x1010: array of per-type lists
    int m_NumTypes;                    // +0x101C
    FactoryDelegate m_Factory;         // +0x1024
    bool m_Initialized;
};

} // namespace Mortar

#endif
