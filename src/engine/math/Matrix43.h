#ifndef MORTAR_MATRIX43_H
#define MORTAR_MATRIX43_H

// `Matrix43` is the port-facing alias for the binary's `_Matrix43<float>` --
// 48-byte 4-row x 3-col matrix (data[row][col]). Templated form lives in
// `_Matrix43.h`; method addresses are documented there.

#include "_Matrix43.h"

typedef _Matrix43<float> Matrix43;

#endif
