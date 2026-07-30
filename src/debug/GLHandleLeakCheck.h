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
// MeshManager::Destroy() and reports them with LOG_ERROR -- deliberately NOT an
// assert, because an abort there would kill the test suite before the rest of
// the teardown backlog is drained. Once the counts reach zero the LOG_ERROR is
// meant to be promoted to a hard assert; treat a non-zero count as a bug.

#ifndef __bada__

namespace FN {

// Live Mortar::Bada::Texture2D_Bada instances (each may own a GL texture name).
int GLLiveCount_Texture2D();

// Logs the identities of every live Texture2D_Bada -- grouped by name (its
// port-side m_Path, or "<unnamed>" when none was ever set) with a live count
// and a capped sample of m_TexId's per group, sorted by count descending.
// `maxLines` caps how many distinct-name groups get printed; any remainder is
// summarised as a single truncation count so a pathological run can't flood
// the log. LOG_ERROR only -- never an assert (see the invariant note above).
void GLLiveLog_Texture2D(int maxLines);

// Live Mortar::Geometry instances (each may own a VBO and an IBO).
int GLLiveCount_Geometry();

// Live Mortar::FontInterface instances (each owns its atlas pages' GL textures).
int GLLiveCount_FontInterface();

}  // namespace FN

#endif  // !__bada__

#endif  // FN_DEBUG_GLHANDLELEAKCHECK_H
