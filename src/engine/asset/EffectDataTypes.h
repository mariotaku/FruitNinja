#ifndef FN_ENGINE_ASSET_EFFECT_DATA_TYPES_H
#define FN_ENGINE_ASSET_EFFECT_DATA_TYPES_H

// EffectDataTypes — type-bucket index enum and per-bucket element sizes.
// Used by EffectPropertyValues to compute buffer offsets and validate
// TrySetValue / TryGetValue calls.

namespace Mortar {
namespace EffectDataTypes {

    // Plain enum (not enum class) for GCC 4.4.1 / cross-build compat.
    // With -fshort-enums this enum is 1 byte; store as uint32_t in structs
    // where 4-byte alignment is needed (see EffectPropertyDefinition::m_Type).
    enum Type {
        Type_Int         = 0,
        Type_Float       = 1,
        Type_Bool        = 2,
        Type_Matrix44    = 3,
        Type_Vec2        = 4,
        Type_Vec3        = 5,
        Type_Vec4        = 6,
        Type_Texture2D   = 7,
        Type_Texture3D   = 8,
        Type_TextureCube = 9,
        kNumTypes        = 10,
    };

    // Element byte size indexed by Type.
    // Defined in EffectDataTypes.cpp to avoid ODR violations.
    extern const unsigned long s_TypeSize[kNumTypes];

}  // namespace EffectDataTypes
}  // namespace Mortar

#endif  // FN_ENGINE_ASSET_EFFECT_DATA_TYPES_H
