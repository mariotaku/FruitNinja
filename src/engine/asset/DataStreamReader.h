#ifndef FN_ENGINE_ASSET_DATASTREAMREADER_H
#define FN_ENGINE_ASSET_DATASTREAMREADER_H

// Mortar::DataStreamReader -- low-level stream reader over a flat memory buffer.
//
// Wraps a caller-owned byte buffer. The caller is responsible for keeping the
// buffer alive for the lifetime of the reader.
//
// Key behaviors:
//   - ReadRaw<T>: copies sizeof(T) raw bytes from cursor (no endian swap).
//     On underflow sets m_Error=true, advances cursor to end, zeroes out.
//   - ReadBasicType<T>: ReadRaw then conditionally byte-swaps if host endian
//     differs from stream endian (Endian::GetEndian() != m_Endian).
//     On the LE-only port the swap path is compiled but never executes.
//   - Read(std::string&): reads a uint32 length prefix then assigns that many
//     bytes as the string value; advances cursor.
//   - MakeSubReader(source): initialises this reader starting at source's
//     current cursor with its remaining byte count, same endianness. The source
//     cursor is NOT advanced (caller is responsible for advancing if needed).
//   - m_Error is sticky: once set it is never cleared within the session.
//
// Struct layout (0x14 = 20 bytes, ARM32):
//   +0x00  void*     m_pStart   -- buffer start (set by SetSource; never changes)
//   +0x04  void*     m_pCursor  -- current read position
//   +0x08  uint32_t  m_Size     -- total buffer size in bytes
//   +0x0c  uint32_t  m_Endian   -- stream byte order (Endian::Endianness value)
//   +0x10  bool      m_Error    -- error flag; set on underflow
//
// Binary: v1.6.1 DataStreamReader::SetSource @0x00250bdc,
//         DataStreamReader::DataStreamReader(void const*, unsigned long, Mortar::Endian::Endianness) @0x00250bf4,
//         DataStreamReader::MakeSubReader @0x00250c08,
//         DataStreamReader::Read(std::string&) @0x00250c28.
//         ReadRaw<unsigned long> @0x0022bfc4, ReadBasicType<unsigned long> @0x0022c058.

#include <cstdint>
#include <cstring>
#include <string>
#include "util/Endian.h"

// Forward declaration: Immutable lives in global namespace.
class Immutable;

namespace Mortar {

class DataStreamReader {
public:
    // ASM-spec v1.6.1 DataStreamReader::SetSource @0x00250bdc:
    //   m_pStart = m_pCursor = data; m_Size = size; m_Endian = e; m_Error = false.
    // Param explicitly ::-qualified: binary mangles this as the GLOBAL Endianness
    // (N6Endian10EndiannessE), not the Mortar-nested one the ctor uses -- see
    // util/Endian.h and the ctor decl below. Since Mortar::Endian now also exists
    // as a real nested namespace, unqualified "Endian::" here would resolve to the
    // nearer Mortar::Endian and silently flip this symbol's mangling; the leading
    // "::" pins it to the global one.
    void SetSource(const void* data, unsigned long size, ::Endian::Endianness e);

    // Default ctor: uninitialized state. Used as target for MakeSubReader.
    DataStreamReader();

    // ASM-spec v1.6.1 DataStreamReader(void const*, unsigned long, Mortar::Endian::Endianness) @0x00250bf4:
    //   delegates to SetSource. Binary mangles this ctor's 3rd param as the
    //   Mortar-nested Endianness (NS_6Endian10EndiannessE), unlike SetSource which
    //   mangles it as the global one (N6Endian10EndiannessE) -- see util/Endian.h.
    DataStreamReader(const void* data, unsigned long size, Mortar::Endian::Endianness e);

    // ASM-spec v1.6.1 DataStreamReader::MakeSubReader @0x00250c08:
    //   Initialises *this from source.m_pCursor, remaining bytes, source.m_Endian.
    //   Source cursor is NOT modified.
    // Binary mangled: _ZN6Mortar16DataStreamReader13MakeSubReaderEm -- takes the source
    // reader's address encoded as unsigned long (not a reference). Callers pass
    // (unsigned long)&source.
    void MakeSubReader(unsigned long sourcePtr);

    // ASM-spec v1.6.1 DataStreamReader::Read(std::string&) @0x00250c28:
    //   ReadBasicType<unsigned long>(len); out.assign((char*)m_pCursor, len); m_pCursor += len.
    void Read(std::string& out);

    // ReadRaw<T> -- raw memcpy sizeof(T) bytes from cursor; no endian swap.
    // On underflow: m_Error=true, cursor advanced to end, out zeroed.
    // ASM-spec v1.6.1 DataStreamReader::ReadRaw<unsigned long> @0x0022bfc4
    template<typename T>
    void ReadRaw(T& out) {
        size_t remaining = (size_t)((const uint8_t*)m_pStart + m_Size - (const uint8_t*)m_pCursor);
        if (remaining < sizeof(T)) {
            m_Error = true;
            m_pCursor = (uint8_t*)m_pStart + m_Size;
            memset(&out, 0, sizeof(T));
            return;
        }
        memcpy(&out, m_pCursor, sizeof(T));
        m_pCursor = (uint8_t*)m_pCursor + sizeof(T);
    }

    // ReadBasicType<T> -- ReadRaw then conditionally byte-swap for endianness.
    // Swap fires only when host endian != stream endian; never fires on LE hosts.
    // ASM-spec v1.6.1 DataStreamReader::ReadBasicType<unsigned long> @0x0022c058,
    //   ReadBasicType<unsigned short> @0x0022bf98, ReadBasicType<float> @0x0022c118.
    template<typename T>
    void ReadBasicType(T& out) {
        ReadRaw(out);
        if (::Endian::GetEndian() != (::Endian::Endianness)m_Endian) {
            uint8_t* b = reinterpret_cast<uint8_t*>(&out);
            size_t i = 0;
            size_t j = sizeof(T) - 1;
            while (i < j) {
                uint8_t tmp = b[i];
                b[i] = b[j];
                b[j] = tmp;
                ++i;
                --j;
            }
        }
    }

    void*    m_pStart;   // +0x00
    void*    m_pCursor;  // +0x04
    uint32_t m_Size;     // +0x08
    uint32_t m_Endian;   // +0x0c (::Endian::Endianness value stored as uint32_t)
    bool     m_Error;    // +0x10
};

#if defined(__bada__)
static_assert(sizeof(DataStreamReader) == 0x14,
              "DataStreamReader: sizeof mismatch (expected 0x14 on ARM32)");
static_assert(offsetof(DataStreamReader, m_pStart)  == 0x00,
              "DataStreamReader::m_pStart offset mismatch");
static_assert(offsetof(DataStreamReader, m_pCursor) == 0x04,
              "DataStreamReader::m_pCursor offset mismatch");
static_assert(offsetof(DataStreamReader, m_Size)    == 0x08,
              "DataStreamReader::m_Size offset mismatch");
static_assert(offsetof(DataStreamReader, m_Endian)  == 0x0c,
              "DataStreamReader::m_Endian offset mismatch");
static_assert(offsetof(DataStreamReader, m_Error)   == 0x10,
              "DataStreamReader::m_Error offset mismatch");
#endif

// ASM-spec v1.6.1 Mortar::operator>>(DataStreamReader&, Immutable&) @0x0025fa40:
//   Read(string&) then imm = string.
DataStreamReader& operator>>(DataStreamReader& reader, ::Immutable& imm);

} // namespace Mortar

#endif  // FN_ENGINE_ASSET_DATASTREAMREADER_H
