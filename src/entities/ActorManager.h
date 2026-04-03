#ifndef FN_ACTOR_MANAGER_H
#define FN_ACTOR_MANAGER_H

#include "Entity.h"
#include <vector>
#include <list>

struct Renderer;

// Matches Mortar::ActorManager — pool-based entity lifecycle
// Original struct ~0x106C bytes with fixed pools per type
class ActorManager {
public:
    // Entity pools per type
    std::list<Entity*> entities;       // all active entities
    std::vector<Entity*> deactivateQueue;  // pending deactivation

    static ActorManager* s_instance;

    ActorManager() { s_instance = this; }
    ~ActorManager() { Clear(); s_instance = NULL; }

    static ActorManager* GetInstance() { return s_instance; }

    // Matches ActorManager::Add (0x17068c)
    // entityType: 0=Fruit, 1=Bomb, 4=BombBlast
    Entity* Add(int entityType, bool hidden = false);

    // Matches ActorManager::Update (0x1701f4)
    void Update(float dt);

    // Matches ActorManager::Draw (0x16fe7c)
    void Draw(Renderer& r);

    // Matches ActorManager::Deactivate (0x170184)
    void Deactivate(Entity* e);

    // Count active entities of a given type (-1 = all types)
    int GetNumEntities(int type = -1);

    // Clear all entities
    void Clear();
};

#endif
