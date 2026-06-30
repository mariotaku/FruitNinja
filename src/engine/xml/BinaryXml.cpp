// Binary XML (bxml) reader/writer stubs.
// v1.6.1 ParseBinary @0x00267c64 / WriteBinary @0x00268268
// No v1.6.1 game callers; present as exported ELF .dynsym symbols only.

#include "BinaryXml.h"

// Defunct: content-pipeline -- no v1.6.1 game callers; bxml reader not ported.
// Full port: walk node_stream per type_tag 0x01-0x05, build TiXml tree via shim API.
// v1.6.1 ParseBinary @0x00267c64
const char* ParseBinary(TiXmlDocument& /*doc*/, const char* /*blob*/) {
    return 0;
}

// Defunct: content-pipeline -- no v1.6.1 game callers; bxml writer not ported.
// Full port: WriteVisitor + string dedup map + packed header + write_cb emission.
// v1.6.1 WriteBinary @0x00268268
int WriteBinary(const TiXmlDocument& /*doc*/,
                void (*/*write_cb*/)(const void*, unsigned int, void*),
                void* /*user*/) {
    return 1;
}
