#ifndef FN_STUBS_STRINGTABLE_H
#define FN_STUBS_STRINGTABLE_H

// TODO: StringTable -- auto-generated symbol-coverage stub.
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
  class File;
}

namespace Mortar {

class StringTable {
public:
    // TODO: StringTable::Clear -- auto stub
    void Clear();
    // TODO: StringTable::GetInfo -- auto stub
    void GetInfo(char const*) const;
    // TODO: StringTable::LoadHeader -- auto stub
    void LoadHeader(Mortar::File&);
    // TODO: StringTable::LoadHeader -- auto stub
    void LoadHeader(char const*);
    // TODO: StringTable::LoadLanguage -- auto stub
    void LoadLanguage(Mortar::File&);
    // TODO: StringTable::LoadLanguage -- auto stub
    void LoadLanguage(char const*);
    // TODO: StringTable::StringTable -- auto stub
    StringTable();
    // TODO: StringTable::~StringTable -- auto stub
    ~StringTable();
};

}  // namespace Mortar

#endif  // FN_STUBS_STRINGTABLE_H
