#include "asset/AlternativeTextureLoader.h"

namespace Mortar {

// Static globals matching _GLOBAL__I_AlternativeTextureLoader.cpp @ 0x002255f0.
// Both constructed as empty AsciiStrings (the shipped default).
Mortar::AsciiString AlternativeTextureLoader::Prefix;
Mortar::AsciiString AlternativeTextureLoader::Postfix;

AlternativeTextureLoaderObj::AlternativeTextureLoaderObj() {
}

AlternativeTextureLoaderObj::~AlternativeTextureLoaderObj() {
}

// Binary @ 0x002252f8 -- static factory.
// When Prefix and Postfix are both empty (shipped default) the binary short-circuits
// and returns a loader whose path equals the input unchanged.
// Port: Defunct/optional: AlternativeTextureLoader path-rewrite; binary @ 0x002252f8
// -- no-op pass-through; Prefix/Postfix empty in shipped data.
Mortar::SmartPtr<AlternativeTextureLoaderObj> AlternativeTextureLoader::CreateLoader(
    const Mortar::AsciiString& path)
{
    AlternativeTextureLoaderObj* obj = new AlternativeTextureLoaderObj();
    if (Prefix.Empty() && Postfix.Empty()) {
        // Short-circuit: original path unchanged (matches binary fast path).
        obj->m_ResolvedPath = path;
    } else {
        // TODO: 0x002252f8 -- full path-rewrite: find last '/' or '\\', rebuild
        // Prefix + dirpart + Postfix + filename. Not needed while Prefix/Postfix are empty.
        obj->m_ResolvedPath = path;
    }
    return Mortar::WrapPtr(obj);
}

} // namespace Mortar
