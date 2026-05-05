#ifndef FN_STUBS_WORKGROUP_H
#define FN_STUBS_WORKGROUP_H

// TODO: WorkGroup -- auto-generated symbol-coverage stub.
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
  class Job;
}

namespace Mortar {

class WorkGroup {
public:
    // TODO: WorkGroup::AllocateThread -- auto stub
    void AllocateThread(int);
    // TODO: WorkGroup::CancelAllJobs -- auto stub
    void CancelAllJobs();
    // TODO: WorkGroup::CloseAllThreads -- auto stub
    void CloseAllThreads();
    // TODO: WorkGroup::DestroyPlatformData -- auto stub
    void DestroyPlatformData();
    // TODO: WorkGroup::InitialisePlatformData -- auto stub
    void InitialisePlatformData();
    // TODO: WorkGroup::QueueJob -- auto stub
    void QueueJob(Mortar::SmartPtr<Mortar::Job> const&);
    // TODO: WorkGroup::WakeWorkerThread -- auto stub
    void WakeWorkerThread();
    // TODO: WorkGroup::WorkGroup -- auto stub
    WorkGroup();
    // TODO: WorkGroup::~WorkGroup -- auto stub
    ~WorkGroup();
};

}  // namespace Mortar

#endif  // FN_STUBS_WORKGROUP_H
