#ifndef FN_STUBS_MENUBACKGROUND_H
#define FN_STUBS_MENUBACKGROUND_H

// TODO: MenuBackground -- auto-generated symbol-coverage stub.
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

class MenuBackground {
public:
    // TODO: MenuBackground::Draw -- auto stub
    void Draw();
    // TODO: MenuBackground::Init -- auto stub
    void Init(bool);
    // TODO: MenuBackground::MenuBackground -- auto stub
    MenuBackground();
    // TODO: MenuBackground::Update -- auto stub
    void Update(float);
    // TODO: MenuBackground::~MenuBackground -- auto stub
    ~MenuBackground();
};

}  // namespace Mortar


// Hoist into global scope to match the binary's class location.
using Mortar::MenuBackground;
#endif  // FN_STUBS_MENUBACKGROUND_H
