#ifndef FN_PLATFORM_WII_FILE_WII_H
#define FN_PLATFORM_WII_FILE_WII_H

// libfat file handle -- SCAFFOLDING ONLY / placeholder.
//
// Unlike FileSystemWii (which needs a mount-prefix override), the per-file
// IFile side needs NO Wii-specific subclass: Mortar::IFile_Direct
// (src/engine/asset/IFile_Direct.h) is already a plain stdio FILE* wrapper
// (fopen/fread/fwrite/fseek/ftell), and libfat's FILE* IS a real stdio
// FILE* once fatInitDefault() has run -- there is no libfat-specific file
// handle type to wrap. FileSystemWii::OpenFile (inherited from
// FileSystem_Direct, unmodified) constructs an IFile_Direct directly.
//
// This header exists to document that decision at the seam a reader would
// otherwise expect a FileWii.cpp override to exist. No FRUIT_PLATFORM_WII
// class is declared here; nothing to compile.
//
// TODO(wii): if a future pass finds a real need for Wii-specific file
// behaviour (e.g. DVD-based reads with different alignment/sector-size
// constraints for a disc-based distribution, as opposed to SD/USB via
// libfat), add an IFileWii subclass here then -- not before it's needed.

#endif // FN_PLATFORM_WII_FILE_WII_H
