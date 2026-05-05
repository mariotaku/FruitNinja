#ifndef FN_STUBS_FREELIST_H
#define FN_STUBS_FREELIST_H

// TODO: FreeList -- auto-generated symbol-coverage stub.
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

namespace Mortar { class StackHeap; }

class FreeList {
public:
    // TODO: FreeList::Allocate -- auto stub
    void Allocate(char const*);
    // TODO: FreeList::Clear -- auto stub
    void Clear();
    // TODO: FreeList::FreeList -- auto stub
    FreeList(Mortar::StackHeap*, unsigned long);
    // TODO: FreeList::GetNumFreeBlocks -- auto stub
    void GetNumFreeBlocks();
    // TODO: FreeList::Release -- auto stub
    void Release(void*);
    // TODO: FreeList::UpdateFreeList -- auto stub
    void UpdateFreeList();
    // TODO: FreeList::VerifyFreeList -- auto stub
    void VerifyFreeList();
    // TODO: FreeList::VerifyFreeList2 -- auto stub
    void VerifyFreeList2();
    // TODO: FreeList::~FreeList -- auto stub
    ~FreeList();
};

#endif  // FN_STUBS_FREELIST_H
