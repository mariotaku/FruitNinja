#include "entity/ActorManager.h"
#include <cstring>

namespace Mortar {

ActorManager::ActorManager()
    : m_FreeCount(0)
    , m_TypeLists(NULL)
    , m_NumTypes(0)
    , m_Initialized(false)
{
    memset(m_FreePool, 0, sizeof(m_FreePool));
}

ActorManager::~ActorManager() {
    if (m_TypeLists) {
        delete[] m_TypeLists;
        m_TypeLists = NULL;
    }
}

void ActorManager::Initialise(int numTypes, int heapSize) {
    (void)heapSize;
    m_NumTypes = numTypes;
    m_TypeLists = new std::list<Entity*>[numTypes];
    m_Initialized = true;
}

// Matches 0x0017068c
Entity* ActorManager::Add(int entityType, bool activate) {
    if (!m_Initialized || entityType < 0 || entityType >= m_NumTypes) return NULL;

    Entity* entity = NULL;

    // Search free pool backwards for matching type
    for (int i = m_FreeCount - 1; i >= 0; i--) {
        if (m_FreePool[i]->m_EntityType == entityType) {
            entity = m_FreePool[i];
            m_FreeCount--;
            for (int j = i; j < m_FreeCount; j++) {
                m_FreePool[j] = m_FreePool[j + 1];
            }
            m_TypeLists[entityType].push_back(entity);
            if (activate) {
                entity->flags &= ~ENTITY_INACTIVE;
                entity->OnActivate();
            }
            return entity;
        }
    }

    // No recycled entity — create via factory
    if (m_Factory) {
        entity = m_Factory(entityType);
        if (entity) {
            entity->m_EntityType = (uint8_t)entityType;
            m_TypeLists[entityType].push_back(entity);
            if (activate) {
                entity->OnActivate();
            }
        }
    }

    return entity;
}

// Matches 0x001701f4
void ActorManager::Update(float dt) {
    if (!m_Initialized || !m_TypeLists) return;

    // Collect entities to deactivate after iteration
    std::vector<Entity*> deactivateQueue;

    for (int type = 0; type < m_NumTypes; type++) {
        for (std::list<Entity*>::iterator it = m_TypeLists[type].begin();
             it != m_TypeLists[type].end(); ++it) {
            Entity* e = *it;
            if ((e->flags & (ENTITY_INACTIVE | ENTITY_PENDING_DEACTIVATE)) == 0) {
                e->flags |= ENTITY_UPDATING;
                e->Update(dt);
                e->flags |= ENTITY_POST_UPDATING;
                e->PostUpdate(dt);
                e->flags &= ~(ENTITY_UPDATING | ENTITY_POST_UPDATING);
            }
            if (e->flags & ENTITY_PENDING_DEACTIVATE) {
                deactivateQueue.push_back(e);
            }
        }
    }

    for (size_t i = 0; i < deactivateQueue.size(); i++) {
        Deactivate(deactivateQueue[i]);
    }
}

// Matches 0x0016fe7c
void ActorManager::Draw() {
    if (!m_Initialized || !m_TypeLists) return;

    for (int type = 0; type < m_NumTypes; type++) {
        for (std::list<Entity*>::iterator it = m_TypeLists[type].begin();
             it != m_TypeLists[type].end(); ++it) {
            Entity* e = *it;
            if ((e->flags & (ENTITY_INACTIVE | ENTITY_PENDING_DEACTIVATE)) == 0) {
                e->Draw();
            }
        }
    }
}

// Matches 0x00170184
void ActorManager::Deactivate(Entity* entity) {
    if (!entity) return;
    int type = entity->m_EntityType;
    if (type < 0 || type >= m_NumTypes) return;

    std::list<Entity*>& list = m_TypeLists[type];
    for (std::list<Entity*>::iterator it = list.begin(); it != list.end(); ++it) {
        if (*it == entity) {
            list.erase(it);
            entity->OnDeactivate();
            entity->flags |= ENTITY_INACTIVE;
            entity->flags &= ~ENTITY_PENDING_DEACTIVATE;
            if (m_FreeCount < MAX_FREE_POOL) {
                m_FreePool[m_FreeCount++] = entity;
            }
            return;
        }
    }
}

// Matches 0x001702d8
void ActorManager::Remove(Entity* entity) {
    if (!entity) return;
    int type = entity->m_EntityType;
    if (type < 0 || type >= m_NumTypes) return;

    std::list<Entity*>& list = m_TypeLists[type];
    for (std::list<Entity*>::iterator it = list.begin(); it != list.end(); ++it) {
        if (*it == entity) {
            if ((entity->flags & ENTITY_NO_DESTRUCT) == 0) {
                entity->OnDeactivate();
                delete entity;
            }
            list.erase(it);
            return;
        }
    }
}

int ActorManager::GetNumEntities() const {
    int total = 0;
    for (int i = 0; i < m_NumTypes; i++) {
        total += (int)m_TypeLists[i].size();
    }
    return total;
}

int ActorManager::GetNumEntities(int type) const {
    if (type < 0 || type >= m_NumTypes) return 0;
    return (int)m_TypeLists[type].size();
}

} // namespace Mortar
