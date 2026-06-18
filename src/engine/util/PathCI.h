#ifndef MORTAR_PATH_CI_H
#define MORTAR_PATH_CI_H

#include <string>

namespace Mortar {

// Case-insensitive path resolution for POSIX. Walks `absPath` component
// by component; if any component doesn't exist verbatim, scans its
// parent directory case-insensitively (strcasecmp) and substitutes the
// first match. Returns the resolved path with original case, or an
// empty string if any component has no CI match.
//
// On Windows the function is a no-op pass-through (NTFS is already
// case-insensitive). Use this as a fallback when a raw fopen / SDL
// SDL_RWFromFile / tinyxml2::XMLDocument::LoadFile call returns ENOENT.
//
// FileSystem_Direct also uses this helper (via Mortar::ResolvePathCI
// calls in FileSystemPosix.cpp and FileSystemWin32.cpp).  External
// callers include file open paths that bypass the IFile chain
// (tinyxml2 in *Manager.cpp loaders, Localisation, raw fopen).
std::string ResolvePathCI(const char* absPath);

} // namespace Mortar

#endif
