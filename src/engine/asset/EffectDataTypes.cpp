#include "asset/EffectDataTypes.h"

namespace Mortar {
namespace EffectDataTypes {

// Element byte size for each type bucket.
// Indexed by Type enum value; used by EffectPropertyValues ctor to compute
// the allocation size for each bucket: (count * s_TypeSize[t] + 3) & ~3.
const unsigned long s_TypeSize[kNumTypes] = {
    4,   // Type_Int
    4,   // Type_Float
    1,   // Type_Bool  (bucket prefix rounded to 4 in EffectPropertyValues ctor)
    64,  // Type_Matrix44
    8,   // Type_Vec2
    12,  // Type_Vec3
    16,  // Type_Vec4
    4,   // Type_Texture2D
    4,   // Type_Texture3D
    4,   // Type_TextureCube
};

}  // namespace EffectDataTypes
}  // namespace Mortar
