#ifndef FN_RESOURCE_IRESOURCESOURCE_H
#define FN_RESOURCE_IRESOURCESOURCE_H

// Task #36 Stage 1 -- file-source seam (tmp/wii/loader-blueprint.md section 3).
//
// A single abstract read-whole-file operation that the block-preload loader
// (task #36 Stage 2+) routes through instead of calling
// FileManager::GetFileData directly. The point is to keep exactly ONE place
// in the port that knows "how to get bytes for a logical resource path" --
// today that's loose files under the mounted root via FileManager's
// IFileSystem chain (ResourceSourceLooseSD below); a future DVD-bundle
// reader (Wii task #57) becomes a second IResourceSource implementation
// behind this same interface, with no caller-side changes.
//
// Stage 1 does NOT wire this into any caller yet -- File::Load /
// FileManager::GetFileData continue to be called directly everywhere (see
// TextureManager::Load, SoundManager::LoadSound, BakedFontWii::LoadSizeIndex/
// EnsurePageTexture, MeshManager::LoadMeshInternal). This header exists so
// the Stage 2/3 BlockLoader has a stable seam to read through from the start,
// per the blueprint's "do NOT thread File/FileManager directly" instruction.
//
// Originally Wii-only; relocated out of src/platform/wii so any target can
// build+enable it via FN_BLOCK_PRELOAD (see root CMakeLists.txt). Only
// compiled when FN_BLOCK_PRELOAD is set.
#ifdef FN_BLOCK_PRELOAD

#include <cstddef>

namespace fn {
namespace wii {

// Abstract "read a whole logical resource into a heap buffer" operation.
// Implementations own the returned buffer's allocator; ReleaseWhole() must be
// used to free it (not raw free()/delete[]) so a future DVD-bundle source
// (which may hand back a pointer into a memory-mapped/cached region rather
// than a malloc'd copy) can implement a matching no-op release.
class IResourceSource {
public:
    virtual ~IResourceSource() {}

    // Reads the full contents of `logicalPath` (a FileManager-relative path,
    // e.g. "textures/fruit_atlas.tex" -- same strings File::Load already
    // takes) into a newly allocated buffer. On success returns true and sets
    // *outBuf/*outSize; caller must pass *outBuf to ReleaseWhole() when done.
    // On failure returns false and leaves *outBuf/*outSize untouched.
    virtual bool ReadWhole(const char* logicalPath, void** outBuf, unsigned long* outSize) = 0;

    // Frees a buffer previously returned by ReadWhole(). Must be called
    // through the same IResourceSource instance that produced the buffer.
    virtual void ReleaseWhole(void* buf) = 0;
};

// Loose-file implementation: wraps Mortar::FileManager::GetFileData, i.e. the
// exact same IFileSystem chain (FileManager -> FileSystem_Direct ->
// IFile_Direct::Read == fread) every resource load already goes through
// today (see tmp/wii/loader-blueprint.md section 3). No new file-IO code
// path -- this class only adapts the existing FileManager call into the
// IResourceSource shape.
class ResourceSourceLooseSD : public IResourceSource {
public:
    virtual bool ReadWhole(const char* logicalPath, void** outBuf, unsigned long* outSize);
    virtual void ReleaseWhole(void* buf);
};

} // namespace wii
} // namespace fn

#endif // FN_BLOCK_PRELOAD

#endif // FN_RESOURCE_IRESOURCESOURCE_H
