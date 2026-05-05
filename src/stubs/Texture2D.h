#ifndef FN_STUBS_TEXTURE2D_H
#define FN_STUBS_TEXTURE2D_H

// TODO: Texture2D -- auto-generated symbol-coverage stub.
//   Empty bodies; real binary implementations live at the
//   addresses listed in tmp/symbol-diff/missing_full_demangled.txt.
//   Replace each method with a real port over time.

#include "math/Vec3.h"
#include "math/Vec2.h"
#include "math/Matrix44.h"
#include "math/Colour.h"
#include "util/Delegate.h"
#include "util/SmartPtr.h"
#include "util/AsciiString.h"
#include <cstdint>

namespace Mortar {

class Texture2D {
public:
    // TODO: Texture2D::Load -- auto stub
    void Load(char const*);
    // TODO: Texture2D::Load -- auto stub
    void Load(char const*, unsigned int);
    // TODO: Texture2D::LoadFromMemory -- auto stub
    void LoadFromMemory(void const*, int);
};

}  // namespace Mortar

#endif  // FN_STUBS_TEXTURE2D_H
