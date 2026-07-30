#include "asset/DataStreamReader.h"
#include "util/Immutable.h"
#include <cstdint>
#include <cstring>

namespace Mortar {

// ASM-spec v1.6.1 DataStreamReader::SetSource @0x00250bdc
// ARM disassembly:
//   str r3, [r0, #0x0c]   ; m_Endian = e
//   str r1, [r0, #0x00]   ; m_pStart = data
//   stmib r0, {r1, r2}    ; m_pCursor = data, m_Size = size
//   strb r3(0), [r0, #0x10] ; m_Error = false
void DataStreamReader::SetSource(const void* data, unsigned long size, ::Endian::Endianness e) {
    m_pStart  = const_cast<void*>(data);
    m_pCursor = const_cast<void*>(data);
    m_Size    = (uint32_t)size;
    m_Endian  = (uint32_t)e;
    m_Error   = false;
}

DataStreamReader::DataStreamReader()
    : m_pStart(0), m_pCursor(0), m_Size(0), m_Endian(0), m_Error(false) {}

// ASM-spec v1.6.1 DataStreamReader::DataStreamReader(void const*, unsigned long, Mortar::Endian::Endianness) @0x00250bf4
// Body: calls SetSource. Param type is the Mortar-nested Endianness (see util/Endian.h);
// cast down to the global one that SetSource takes -- both share the same LITTLE/BIG values.
DataStreamReader::DataStreamReader(const void* data, unsigned long size, Mortar::Endian::Endianness e) {
    SetSource(data, size, (::Endian::Endianness)e);
}

// ASM-spec v1.6.1 DataStreamReader::MakeSubReader @0x00250c08 -- see header.
DataStreamReader DataStreamReader::MakeSubReader(unsigned long count) const {
    return DataStreamReader(m_pCursor, count, (Mortar::Endian::Endianness)m_Endian);
}

// ASM-spec v1.6.1 DataStreamReader::Read(std::string&) @0x00250c28
// Reads a uint32 length prefix via ReadBasicType, then string::assign(cursor, len),
// then advances cursor by len.
void DataStreamReader::Read(std::string& out) {
    unsigned long len = 0;
    ReadBasicType<unsigned long>(len);
    out.assign((const char*)m_pCursor, len);
    m_pCursor = (uint8_t*)m_pCursor + len;
}

// ASM-spec v1.6.1 Mortar::operator>>(DataStreamReader&, Immutable&) @0x0025fa40
// Reads a length-prefixed string from reader, assigns it to imm.
DataStreamReader& operator>>(DataStreamReader& reader, Immutable& imm) {
    std::string tmp;
    reader.Read(tmp);
    imm = tmp;
    return reader;
}

} // namespace Mortar
