#ifndef FN_ENGINE_ASSET_ALTERNATIVE_TEXTURE_LOADER_H
#define FN_ENGINE_ASSET_ALTERNATIVE_TEXTURE_LOADER_H

// Mortar::AlternativeTextureLoader -- v1.6.1 addition.
// Binary: CreateLoader @ 0x002252f8 (static factory, returns SmartPtr<Loader> via 0x28-byte
// object); Prefix/Postfix globals set at static-init by _GLOBAL__I_AlternativeTextureLoader.cpp
// @ 0x002255f0. UseAlternativeTextureLoader flag @ data segment.
//
// Behaviour: given a base texture path, finds the last '/' or '\\' to split dir/file, then
// rebuilds: Prefix + dirpart + Postfix + filename. If Prefix and Postfix are BOTH empty
// (shipped default), CreateLoader short-circuits and returns a loader over the original path.
//
// For the port: entire class is a compile-clean stub (pass-through). Prefix/Postfix remain
// empty (matching shipped binary defaults); UseAlternativeTextureLoader defaults false.
// The public method + globals are declared so any call sites compile.

#include "util/AsciiString.h"
#include "util/SmartPtr.h"
#include "util/ReferenceCounter.h"

namespace Mortar {

// Internal loader object returned by CreateLoader (0x28 bytes in binary).
// Port: minimal shell with vtable + refcount base to satisfy SmartPtr<Loader>.
class AlternativeTextureLoaderObj : public ReferenceCounter {
public:
    AlternativeTextureLoaderObj();
    virtual ~AlternativeTextureLoaderObj();

    // The resolved (possibly rewritten) path.
    AsciiString m_ResolvedPath;
};

class AlternativeTextureLoader {
public:
    // Binary @ 0x002252f8 -- static factory returning SmartPtr<AlternativeTextureLoaderObj>.
    // When Prefix/Postfix are both empty (the shipped default) it returns a loader over
    // the ORIGINAL path unchanged.
    // Defunct/optional: AlternativeTextureLoader path-rewrite; binary @ 0x002252f8
    // -- no-op pass-through; Prefix/Postfix empty in shipped data.
    static Mortar::SmartPtr<AlternativeTextureLoaderObj> CreateLoader(
        const Mortar::AsciiString& path);

    // Static globals set by _GLOBAL__I_AlternativeTextureLoader.cpp @ 0x002255f0.
    // Both empty in the shipped binary.
    static Mortar::AsciiString Prefix;
    static Mortar::AsciiString Postfix;
};

} // namespace Mortar

#endif // FN_ENGINE_ASSET_ALTERNATIVE_TEXTURE_LOADER_H
