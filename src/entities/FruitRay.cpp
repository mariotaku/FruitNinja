#include "FruitRay.h"

#include "Fruit.h"
#include "asset/Texture.h"
#include "math/Random.h"
#include "math/MathUtil.h"
#include "game/GameWork.h"

// Binary @ 0x001d954c -- base Entity ctor only; no field priming here (Init
// primes every field). entityType is set here to match the CreateEntity(6)
// factory path (mirrors Coin/Jiblet ctors, which set their own entityType).
FruitRay::FruitRay()
    : m_WorldMatrix()
    , m_StartMatrix()
    , m_pSourceFruit(0)
    , m_Phase(0.0f)
    , m_Life(0.0f)
    , m_ColourEnd(0.0f, 0.0f, 0.0f)
    , m_ColourStart(0.0f, 0.0f, 0.0f)
    , m_Expiring(0)
{
    entityType = 6;
}

FruitRay::~FruitRay() {}

// ASM-spec v1.6.1 FruitRay::Init @0x001e4740
void FruitRay::Init(Fruit* src, Quaternion /*rot*/) {
    m_pSourceFruit = src;
    flags &= 0xEE;   // clear ENT_INACTIVE(0x01) + ENT_KILLED(0x10)
    m_Expiring = 0;
    // m_ColourStart = Vec3::One * (rand01()*50 + 70) -- per-spawn random brightness.
    float brightness = Math::g_Random.RandF(1.0f) * 50.0f + 70.0f;
    m_ColourStart = _Vector3<float>::One() * brightness;
    m_Phase = 0.0f;
    m_ColourEnd = _Vector3<float>::One() * 40.0f;
    scale = m_ColourEnd;   // scale (Entity+0x28) doubles as m_ColourCurrent; see FruitRay.h note
    m_Life = 1.0f;
    pos = src->pos;
    // NOTE: the `rot` param is NOT used to build either matrix -- binary
    // leaves both at identity here; orientation comes from
    // m_pSourceFruit->m_Rot1 every Update.
    m_StartMatrix = Matrix44();   // identity
    m_WorldMatrix = Matrix44();   // identity
}

// ASM-spec v1.6.1 FruitRay::Update @0x001e45e0
void FruitRay::Update(float dt) {
    if (!m_Expiring) {
        Fruit* f = m_pSourceFruit;
        m_Phase += game_work.dt;             // fixed dt (NOT the param)
        pos = f->pos;
        m_WorldMatrix = f->m_Rot1.ToMatrix44();
        float t = m_Phase / 0.15f;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        scale.x = Lerp(m_ColourStart.x, m_ColourEnd.x, t);
        scale.y = Lerp(m_ColourStart.y, m_ColourEnd.y, t);
        scale.z = Lerp(m_ColourStart.z, m_ColourEnd.z, t);
    } else {
        m_Life += dt * -1.6f;
        m_pSourceFruit = 0;
        if (m_Life <= 0.0f) {
            flags |= ENT_KILLED;   // request removal
        }
    }
}

Mortar::SmartPtr<Mortar::Texture> FruitRay::RayTexture;
