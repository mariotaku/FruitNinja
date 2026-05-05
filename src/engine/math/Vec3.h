#ifndef MORTAR_VEC3_H
#define MORTAR_VEC3_H

// `Vec3` is the port-facing alias for the binary's `_Vector3<float>` --
// same 12-byte layout, same methods. The templated form lives in
// `_Vector3.h` so signatures involving `_Vector3<float>` mangle identically
// to the binary's `8_Vector3IfE` symbols.

#include "_Vector3.h"

typedef _Vector3<float> Vec3;

#endif
