#pragma once
#ifndef FN_DEBUG_GLHANDLELEAKCHECK_H
#define FN_DEBUG_GLHANDLELEAKCHECK_H

// Port specific: teardown leak instrumentation. No binary counterpart.
//
// Invariant enforced: at the END of GameDestroy, no GL-handle-owning object may
// still be alive. Exactly three classes own a GL handle directly; every other
// GL-owning object in the port owns one of these transitively:
//
//   Mortar::Bada::Texture2D_Bada  -- m_TexId              (asset/Texture.cpp)
//   Mortar::Geometry              -- m_Vbo / m_Ibo        (asset/Geometry.cpp)
//   Mortar::FontInterface         -- atlas page textures  (render/FontInterface.cpp)
//
// Each of the three .cpp files defines file-static live-instance tracking,
// updated in every ctor and dropped in the dtor, and exposes it through the
// matching accessor(s) below. STATIC storage only -- no instance member is added,
// because Texture2D_Bada's size is pinned by `operator new(100)` (Texture.h) and
// must not grow. Texture2D_Bada additionally keeps a static std::set<Texture2D_Bada*>
// registry (not a member) so the leak report can name identities, not just a count --
// see GLLiveLog_Texture2D below.
//
// The whole facility is `#ifndef __bada__`-gated -- both this header and the
// counter/hook code in the three .cpp files. Under __bada__ the hooks would add
// an ldr/add/str against a GOT global to the ctor+dtor of three hot,
// already-asm-verified classes, so gating keeps the cross-build byte-identical
// and leaves asm-verify untouched. Callers must gate their use the same way.
//
// GameDestroy (game/GameInitialise.cpp) reads all three after
// MeshManager::Destroy(). Task #144 promoted this from LOG_ERROR to a hard
// failure (LOG_ERROR identity dump, then abort()) now that the teardown
// backlog is drained: Geometry and FontInterface must be exactly 0 in every
// scenario, and every live Texture2D_Bada must resolve to a name on the
// drained-backlog allow-list (kExpectedLeakedTextureNames in asset/Texture.cpp) -- see
// FN::GLLiveTexture2D_AllExpected below. The check is on the IDENTITY SET, not
// a count: a bare `count <= 9` would pass if one expected texture stopped
// leaking while an unexpected one started, exactly the failure this guard
// exists to catch.

#ifndef __bada__

#include <string>
#include <vector>

namespace FN {

// Live Mortar::Bada::Texture2D_Bada instances (each may own a GL texture name).
int GLLiveCount_Texture2D();

// Logs the identities of every live Texture2D_Bada -- grouped by name (its
// port-side m_Path, or "<unnamed>" when none was ever set) with a live count
// and a capped sample of m_TexId's per group, sorted by count descending.
// `maxLines` caps how many distinct-name groups get printed; any remainder is
// summarised as a single truncation count so a pathological run can't flood
// the log. This function only logs -- it does not decide pass/fail; GameDestroy
// calls it for the dump, then hard-fails separately via
// GLLiveTexture2D_AllExpected (see below).
void GLLiveLog_Texture2D(int maxLines);

// Live Mortar::Geometry instances (each may own a VBO and an IBO).
int GLLiveCount_Geometry();

// Live Mortar::FontInterface instances (each owns its atlas pages' GL textures).
int GLLiveCount_FontInterface();

// Task #144 hard-fail gate. Checks every live Texture2D_Bada's identity
// (m_Path) against the drained-backlog allow-list -- not just the live count
// -- so a new leak can't hide behind an old one shrinking. Returns true iff
// every live name is allow-listed; false and (if outUnexpected is non-NULL)
// the distinct unexpected names otherwise. Call this instead of
// GLLiveCount_Texture2D() for the pass/fail decision; GLLiveCount_Texture2D
// and GLLiveLog_Texture2D remain for the raw count and the full identity dump.
bool GLLiveTexture2D_AllExpected(std::vector<std::string>* outUnexpected);

}  // namespace FN

#endif  // !__bada__

#endif  // FN_DEBUG_GLHANDLELEAKCHECK_H
