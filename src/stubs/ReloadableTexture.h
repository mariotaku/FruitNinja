#ifndef FN_STUBS_RELOADABLETEXTURE_H
#define FN_STUBS_RELOADABLETEXTURE_H

// TODO: ReloadableTexture -- auto-generated symbol-coverage stub.
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

class ReloadableTexture {
public:
    // TODO: ReloadableTexture::Load -- auto stub
    void Load() const;
    // TODO: ReloadableTexture::ReloadableTexture -- auto stub
    ReloadableTexture();
    // TODO: ReloadableTexture::ReloadableTexture -- auto stub
    ReloadableTexture(ReloadableTexture const&);
    // TODO: ReloadableTexture::ReloadableTexture -- auto stub
    ReloadableTexture(char const*);
    // TODO: ReloadableTexture::Unload -- auto stub
    void Unload() const;
};

}  // namespace Mortar

#endif  // FN_STUBS_RELOADABLETEXTURE_H
