#ifndef FN_STUBS_JOB_H
#define FN_STUBS_JOB_H

// TODO: Job -- auto-generated symbol-coverage stub.
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

class Job {
public:
    // TODO: Job::Cancel -- auto stub
    void Cancel();
    // TODO: Job::Job -- auto stub
    Job();
    // TODO: Job::Run -- auto stub
    void Run();
    // TODO: Job::WaitUntilDone -- auto stub
    void WaitUntilDone();
    // TODO: Job::~Job -- auto stub
    ~Job();
};

}  // namespace Mortar

#endif  // FN_STUBS_JOB_H
