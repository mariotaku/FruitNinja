#include "entity/Entity.h"

namespace Mortar {

Entity::Entity()
    : pos(0, 0, 0)
    , vel(0, 0, 0)
    , angle(0)
    , scale(1.0f)
    , flags(0)
    , m_EntityType(0)
{
}

Entity::~Entity() {
}

} // namespace Mortar
