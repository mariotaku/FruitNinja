#ifndef FN_STUBS_DIALOG_H
#define FN_STUBS_DIALOG_H

// TODO: Dialog -- auto-generated symbol-coverage stub.
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

class Dialog {
public:
    // TODO: Dialog::AddButton -- auto stub
    void AddButton(char const*, Mortar::Delegate1<void, int>);
    // TODO: Dialog::Call -- auto stub
    void Call(int);
    // TODO: Dialog::Dialog -- auto stub
    Dialog();
    // TODO: Dialog::Release -- auto stub
    void Release();
    // TODO: Dialog::SetText -- auto stub
    void SetText(char const*, char const*);
};

}  // namespace Mortar

#endif  // FN_STUBS_DIALOG_H
