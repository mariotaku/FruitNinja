#ifndef FN_STUBS_SOUNDMANAGERMAM_H
#define FN_STUBS_SOUNDMANAGERMAM_H

// TODO: SoundManagerMAM -- auto-generated symbol-coverage stub.
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

class SoundManagerMAM {
public:
    // TODO: SoundManagerMAM::BeginInterruption -- auto stub
    void BeginInterruption();
    // TODO: SoundManagerMAM::EndInterruption -- auto stub
    void EndInterruption();
    // TODO: SoundManagerMAM::PreLoadSound -- auto stub
    void PreLoadSound(char const*);
    // TODO: SoundManagerMAM::SFXPauseAll -- auto stub
    void SFXPauseAll();
    // TODO: SoundManagerMAM::SFXUnpauseAll -- auto stub
    void SFXUnpauseAll();
    // TODO: SoundManagerMAM::SoundManagerMAM -- auto stub
    SoundManagerMAM();
    // TODO: SoundManagerMAM::~SoundManagerMAM -- auto stub
    ~SoundManagerMAM();
};

}  // namespace Mortar

#endif  // FN_STUBS_SOUNDMANAGERMAM_H
