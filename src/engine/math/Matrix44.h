#ifndef MORTAR_MATRIX44_H
#define MORTAR_MATRIX44_H

// `Matrix44` is the port-facing alias for the binary's `_Matrix44<float>` --
// 64-byte column-major matrix (m[col*4 + row]). Templated form lives in
// `_Matrix44.h`; method addresses are documented there.

#include "_Matrix44.h"

typedef _Matrix44<float> Matrix44;

#endif
