#ifndef FN_STUBS_DIALOGMANAGER_H
#define FN_STUBS_DIALOGMANAGER_H

// TODO: DialogManager -- auto-generated symbol-coverage stub.
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

// Forward decls for binary-shape arg types not yet ported here.
namespace Mortar {
  class Dialog;
}

namespace Mortar {

class DialogManager {
public:
    // TODO: DialogManager::AddDialog -- auto stub
    void AddDialog(Mortar::Dialog*);
    // TODO: DialogManager::GetInstance -- auto stub
    void GetInstance();
    // TODO: DialogManager::NewDialog -- auto stub
    void NewDialog();
    // TODO: DialogManager::NewDialogWithText -- auto stub
    void NewDialogWithText(char const*, char const*, bool);
    // TODO: DialogManager::NewDialogWithText -- auto stub
    void NewDialogWithText(char const*, char const*, char const*, Mortar::Delegate1<void, int>, bool);
    // TODO: DialogManager::NewDialogWithText -- auto stub
    void NewDialogWithText(char const*, char const*, char const*, Mortar::Delegate1<void, int>, char const*, Mortar::Delegate1<void, int>, bool);
    // TODO: DialogManager::NewDialogWithText -- auto stub
    void NewDialogWithText(char const*, char const*, char const*, Mortar::Delegate1<void, int>, char const*, Mortar::Delegate1<void, int>, char const*, Mortar::Delegate1<void, int>, bool);
    // TODO: DialogManager::RemoveDialog -- auto stub
    void RemoveDialog(Mortar::Dialog*);
};

}  // namespace Mortar

#endif  // FN_STUBS_DIALOGMANAGER_H
