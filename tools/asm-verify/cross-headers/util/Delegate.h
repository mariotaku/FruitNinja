// Cross-build forwarder for util/Delegate.h.
//
// The real src/engine/util/Delegate.h IS GCC 4.4-compatible (it was written
// against Sourcery 2010q1: no template aliases, no explicit conversion
// operators; noexcept/override/nullptr come from the force-included
// fn-cxx11-shims.h). Sweep TUs that include it via "engine/util/Delegate.h"
// have always compiled it directly.
//
// This file exists only because the sweep's include order puts cross-headers/
// first, so TUs spelling "util/Delegate.h" from non-engine dirs would
// otherwise miss the real header. It used to be an opaque do-nothing stub
// (operator bool() { return false; }), which let GCC constant-fold every
// delegate guard and delete the guarded code — asm-verify then compared the
// binary against gutted port bodies (BaseScreen::UpdateButtons: 6 ported
// instructions vs 263 in the binary). It also created per-TU ODR drift,
// since both headers share the MORTAR_DELEGATE_H guard and mixed TUs got
// whichever definition was included first.
//
// Forwarding gives every TU the one genuine implementation: operator bool is
// a real load of m_bEmpty, operator() a real virtual dispatch, layout is the
// same 0x24 bytes. No stub to keep in sync.
//
// No local include guard: the real header's MORTAR_DELEGATE_H makes repeat
// inclusion a no-op. Resolved via -I<...>/src (present in the cross-build
// CMake, compile-one.sh and check-tu.sh include paths); cross-headers/ has
// no engine/ subdir, so this cannot self-include.
#include <engine/util/Delegate.h>
