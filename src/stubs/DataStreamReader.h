#ifndef FN_STUBS_DATASTREAMREADER_H
#define FN_STUBS_DATASTREAMREADER_H

// TODO: DataStreamReader -- auto-generated symbol-coverage stub.
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

class DataStreamReader {
public:
    // TODO: DataStreamReader::MakeSubReader -- auto stub
    void MakeSubReader(unsigned int);
    // TODO: DataStreamReader::Read -- auto stub
    void Read(std::basic_string<char, std::char_traits<char>, std::allocator<char> >&);
};

}  // namespace Mortar

#endif  // FN_STUBS_DATASTREAMREADER_H
