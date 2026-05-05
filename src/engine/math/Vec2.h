#ifndef MORTAR_VEC2_H
#define MORTAR_VEC2_H

// `Vec2` is the port-facing alias for the binary's `_Vector2<float>` --
// same 8-byte layout, same methods. Templated form is in `_Vector2.h`.

#include "_Vector2.h"

typedef _Vector2<float> Vec2;

#endif
