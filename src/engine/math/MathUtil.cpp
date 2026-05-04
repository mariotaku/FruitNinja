// Analysed: 2026-05-04T00:00
#include "MathUtil.h"

namespace {
    float g_sqrtAsyncResult = 0.0f;
    float g_divAsyncResult  = 0.0f;
}

namespace Mortar { namespace Math {

// Binary @ 0x0019521c — computes sqrtf eagerly, stores in cache for SqrtAsyncGet
void SqrtAsyncSet(float x) {
    g_sqrtAsyncResult = sqrtf(x);
}

// Binary @ 0x00194cf8 — returns last value computed by SqrtAsyncSet
float SqrtAsyncGet() {
    return g_sqrtAsyncResult;
}

// Binary @ 0x00194d14 — computes a/b eagerly, stores in cache for DivAsyncGet
void DivAsyncSet(float a, float b) {
    g_divAsyncResult = a / b;
}

// Binary @ 0x00194d34 — returns last value computed by DivAsyncSet
float DivAsyncGet() {
    return g_divAsyncResult;
}

}} // namespace Mortar::Math
