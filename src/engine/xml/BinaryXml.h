#ifndef FN_ENGINE_XML_BINARYXML_H
#define FN_ENGINE_XML_BINARYXML_H

// Binary XML (bxml) reader/writer stubs.
// v1.6.1 ParseBinary @0x00267c64 / WriteBinary @0x00268268
//
// These are content-pipeline utilities exported by the binary but never called
// by game code in v1.6.1 (no in-binary callers; ELF .dynsym only).
// A full port is not attempted: the port's TiXml shim wraps tinyxml2 via void*
// handles and is not layout-compatible with the binary's TiXml v1.x node structs
// (binary allocates 0x50-byte TiXmlElement, 0x2c-byte TiXmlComment, etc.).
//
// bxml format (from ParseBinary RE):
//   [+0x00] 4B  magic = 'b','x','m','l' (0x62,0x78,0x6d,0x6c)
//   [+0x04] 2B  uint16 version = 1
//   [+0x06] 2B  uint16 max_stack_depth
//   [+0x08] 4B  uint32 str_blob_size (little-endian)
//   [+0x0c] str_blob_size bytes: packed null-terminated UTF-8 strings
//   [+0x0c+str_blob_size] node_stream (depth-first, stack-managed):
//     byte type_tag:
//       0x01 Element:     name_offset(u32), attr_count(u32),
//                         [attr_name_offset(u32), attr_value_offset(u32)]*attr_count,
//                         child_count(u32)
//       0x02 Comment:     text_offset(u32)
//       0x03 Unknown:     text_offset(u32)
//       0x04 Text:        text_offset(u32)
//       0x05 Declaration: version_offset(u32), encoding_offset(u32), standalone_offset(u32)
//     All u32 fields are 4-byte little-endian (ReadRaw<uint> @0x0026778c).

#include "TiXml.h"

// v1.6.1 ParseBinary @0x00267c64
// Parses a bxml blob into doc. Returns pointer past the consumed data on success,
// nullptr on format error (version != 1).
// Stub: returns nullptr (full port deferred; no v1.6.1 game callers).
const char* ParseBinary(TiXmlDocument& doc, const char* blob);

// v1.6.1 WriteBinary @0x00268268
// Serialises doc to bxml via write_cb(data, len, user).
// Stub: returns 1 (full port deferred; no v1.6.1 game callers).
int WriteBinary(const TiXmlDocument& doc,
                void (*write_cb)(const void* data, unsigned int len, void* user),
                void* user);

#endif // FN_ENGINE_XML_BINARYXML_H
