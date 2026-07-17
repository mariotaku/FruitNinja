#ifndef FN_PLATFORM_WII_FILESYSTEM_WII_H
#define FN_PLATFORM_WII_FILESYSTEM_WII_H

// libfat-backed filesystem -- SCAFFOLDING ONLY.
//
// Subclasses Mortar::FileSystem_Direct (src/engine/asset/FileSystem_Direct.h)
// rather than reimplementing Mortar::IFileSystem from scratch: libfat
// exposes a POSIX-compatible fopen/fread/fseek surface once fatInitDefault()
// has run, so the existing stdio-based FileSystem_Direct/IFile_Direct body
// (src/engine/asset/FileSystem_Direct.cpp, IFile_Direct.cpp -- both portable,
// no #ifdef _WIN32/POSIX split needed there) is reusable as-is. This class
// only needs to override construction/mount-prefix behaviour, not the
// FILE*-based read/write/seek logic.
//
// Only compiled when FRUIT_PLATFORM_WII is set (see
// src/platform/wii/CMakeLists.txt).
#ifdef FRUIT_PLATFORM_WII

#include "asset/FileSystem_Direct.h"

namespace Mortar {

class FileSystemWii : public FileSystem_Direct {
public:
    FileSystemWii();
    virtual ~FileSystemWii();

    // TODO(wii): mount-time entry point -- calls fatInitDefault() (once,
    // process-wide) then FileSystem_Direct::Initialise(root, writable) with
    // root set to "sd:/<data-dir>" (or "usb:/<data-dir>" as a fallback if no
    // SD card is detected -- libfat mounts both simultaneously via
    // fatInitDefault so the choice is just which root path string to use).
    void InitialiseWii(const char* subDir, bool writable);
};

} // namespace Mortar

#endif // FRUIT_PLATFORM_WII

#endif // FN_PLATFORM_WII_FILESYSTEM_WII_H
