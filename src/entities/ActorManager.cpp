#include "ActorManager.h"
#include "Fruit.h"
#include "Bomb.h"
#include "render/Renderer.h"
#include <cstdio>

ActorManager* ActorManager::s_instance = NULL;

Entity* ActorManager::Add(int entityType, bool hidden) {
    Entity* e = NULL;

    switch (entityType) {
    case 0: e = new Fruit(); break;
    case 1: e = new Bomb(); break;
    default:
        printf("ActorManager::Add: unknown type %d\n", entityType);
        return NULL;
    }

    e->entityType = entityType;
    e->active = true;
    if (hidden) e->flags |= 0x11;  // pre-allocated + hidden

    entities.push_back(e);
    return e;
}

void ActorManager::Update(float dt) {
    // Process deactivation queue first
    for (size_t i = 0; i < deactivateQueue.size(); i++) {
        Entity* e = deactivateQueue[i];
        e->Deactivate();
    }
    deactivateQueue.clear();

    // Update all active entities
    for (auto it = entities.begin(); it != entities.end(); ++it) {
        Entity* e = *it;
        if (e->IsActive()) {
            e->Update(dt);
        }
    }
}

void ActorManager::Draw(Renderer& r) {
    for (auto it = entities.begin(); it != entities.end(); ++it) {
        Entity* e = *it;
        if (e->IsActive()) {
            e->Draw(r);
        }
    }
}

void ActorManager::Deactivate(Entity* e) {
    deactivateQueue.push_back(e);
}

int ActorManager::GetNumEntities(int type) {
    int count = 0;
    for (auto it = entities.begin(); it != entities.end(); ++it) {
        Entity* e = *it;
        if (e->IsActive() && (type < 0 || e->entityType == type))
            count++;
    }
    return count;
}

void ActorManager::Clear() {
    for (auto it = entities.begin(); it != entities.end(); ++it) {
        delete *it;
    }
    entities.clear();
    deactivateQueue.clear();
}
