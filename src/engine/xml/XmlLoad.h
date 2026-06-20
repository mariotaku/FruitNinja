#ifndef FN_XML_LOAD_H
#define FN_XML_LOAD_H

#include <string>
#include <tinyxml2.h>

namespace FN {

// Port specific: CI fallback mirrors Mortar::TiXml loading through the engine
// FileSystem (FileSystemPosix CI), for case-sensitive web/webOS asset access.
//
// (a) Tries doc.LoadFile(path.c_str()) first (exact-case, happy path).
// (b) On failure, resolves the case-insensitive on-disk name via
//     Mortar::ResolvePathCI and retries.
// (c) On hard failure emits LOG_ERROR naming the path.
// Returns the XMLError from the final attempt.
tinyxml2::XMLError LoadXmlCI(tinyxml2::XMLDocument& doc, const std::string& path);

} // namespace FN

#endif // FN_XML_LOAD_H
