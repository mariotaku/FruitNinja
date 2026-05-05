#ifndef FN_STUBS_SLASHMODINFO_H
#define FN_STUBS_SLASHMODINFO_H

// TODO: SlashModInfo -- auto-generated symbol-coverage stub.
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
  class TiXmlElement;
}

namespace Mortar {

class SlashModInfo {
public:
    // TODO: SlashModInfo::Parse -- auto stub
    void Parse(TiXmlElement*);
    // TODO: SlashModInfo::ParseSlashModInfo -- auto stub
    void ParseSlashModInfo(TiXmlElement*);
    // TODO: SlashModInfo::UpdateSounds -- auto stub
    void UpdateSounds(float);
};

}  // namespace Mortar


// Hoist into global scope to match the binary's class location.
using Mortar::SlashModInfo;
#endif  // FN_STUBS_SLASHMODINFO_H
