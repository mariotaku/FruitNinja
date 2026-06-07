#ifndef FN_ENGINE_INPUT_ACCELERATION_H
#define FN_ENGINE_INPUT_ACCELERATION_H

// Port specific: no desktop accelerometer -- returns zero.
// Binary: Acceleration::GetInstance / GetAccelAbs @ 0x001955e0 / GetAccelDelta @ 0x001955fc.
//
// On Bada, Acceleration is a singleton wrapping the OS accelerometer sensor.
// InputDeviceBada::Update polls it once per frame for three XYZ axes (abs + delta).
// The desktop port has no accelerometer; all axes return 0.0f.

namespace Mortar {

class Acceleration {
public:
    // Binary: Acceleration::GetInstance @ (called from InputDeviceBada::Update).
    static Acceleration* GetInstance() {
        static Acceleration s_instance;
        return &s_instance;
    }

    // Binary @ 0x001955e0 — write absolute accelerometer axes.
    // Port specific: no hardware sensor -- writes 0, 0, 0.
    void GetAccelAbs(float* x, float* y, float* z) {
        if (x) *x = 0.0f;
        if (y) *y = 0.0f;
        if (z) *z = 0.0f;
    }

    // Binary @ 0x001955fc — write per-frame accelerometer delta.
    // Port specific: no hardware sensor -- writes 0, 0, 0.
    void GetAccelDelta(float* x, float* y, float* z) {
        if (x) *x = 0.0f;
        if (y) *y = 0.0f;
        if (z) *z = 0.0f;
    }

private:
    Acceleration() {}
};

}  // namespace Mortar

#endif  // FN_ENGINE_INPUT_ACCELERATION_H
