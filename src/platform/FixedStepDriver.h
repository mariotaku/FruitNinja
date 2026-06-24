#ifndef FN_PLATFORM_FIXEDSTEPDRIVER_H
#define FN_PLATFORM_FIXEDSTEPDRIVER_H

// fixed-step accumulator shared between the SDL desktop build
// and the Emscripten web build.  No SDL / GL / emscripten dependencies -- pure
// arithmetic so it compiles in any translation unit that includes it.
//
// The simulation design rate is 60 ticks/s (SystemManager::Update writes a
// hardcoded dt = 1/60 s per tick; binary 0x0018ade0).  The accumulator
// converts real elapsed time (ms) into a discrete tick count, guaranteeing
// the simulation always advances at the correct wall-clock rate regardless of
// display refresh (60 / 120 / 144 Hz, or 30 fps when the GPU is under load).
//
// maxSteps=5 is the spiral-of-death guard: on a badly lagging frame the sim
// drains at most 5 ticks so it can never fall further behind.
//
// alpha() returns the fractional residual in [0,1) -- reserved for Phase 2
// render interpolation; Phase 1 does not use it.

namespace fn {

struct FixedStepDriver {
    double frameMs;
    int    maxSteps;
    double accumulator;

    explicit FixedStepDriver(double f = 1000.0 / 60.0, int m = 5)
        : frameMs(f), maxSteps(m), accumulator(0.0) {}

    // Advance the accumulator by `ms` real milliseconds.
    // Returns the number of simulation steps that should be executed.
    // Clamps `ms` to frameMs*maxSteps before accumulating (spiral guard).
    int advance(double ms) {
        if (ms < 0.0) ms = 0.0;
        double cap = frameMs * maxSteps;
        if (ms > cap) ms = cap;
        accumulator += ms;
        int s = 0;
        while (accumulator >= frameMs && s < maxSteps) {
            accumulator -= frameMs;
            ++s;
        }
        return s;
    }

    // Fractional residual in [0, 1) for Phase 2 render interpolation.
    double alpha() const { return accumulator / frameMs; }
};

} // namespace fn

#endif // FN_PLATFORM_FIXEDSTEPDRIVER_H
