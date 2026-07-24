#ifndef FN_ENGINE_NETWORK_BYTE_BUFFER_H
#define FN_ENGINE_NETWORK_BYTE_BUFFER_H

// ByteWriter / ByteReader -- little-endian byte-cursor helpers for MP packet
// wire (de)serialisation. Port-only infra: the binary's real P2P wire layer
// is unrecoverable (dead Bada network stack), so this cursor is a from-scratch
// helper, not a ported binary class. No struct-layout claims are made here --
// only the packet header/field bytes it reads/writes must match the RE'd
// wire spec documented in each packet's header.
//
// Both classes are bounds-tolerant: writes/reads past the end of the backing
// buffer are silently dropped/zero-filled rather than asserting or throwing,
// so a truncated or malformed packet degrades instead of crashing.

#include <cstdint>
#include <cstring>

namespace Mortar {

class ByteWriter {
public:
    ByteWriter(uint8_t* buf, int capacity) : m_Buf(buf), m_Capacity(capacity), m_Pos(0) {}

    void U8(uint8_t v) { WriteRaw(&v, 1); }
    void U16(uint16_t v) { WriteRaw(&v, 2); }
    void U32(uint32_t v) { WriteRaw(&v, 4); }
    void I32(int32_t v) { WriteRaw(&v, 4); }
    void F32(float v) { WriteRaw(&v, 4); }
    void Bytes(const void* src, int count) { WriteRaw(src, count); }

    int Written() const { return m_Pos; }

private:
    void WriteRaw(const void* src, int count) {
        if (m_Pos + count > m_Capacity) {
            // Truncate silently -- bounds-tolerant per contract.
            count = m_Capacity - m_Pos;
            if (count <= 0) return;
        }
        std::memcpy(m_Buf + m_Pos, src, count);
        m_Pos += count;
    }

    uint8_t* m_Buf;
    int m_Capacity;
    int m_Pos;
};

class ByteReader {
public:
    ByteReader(const uint8_t* buf, int size) : m_Buf(buf), m_Size(size), m_Pos(0) {}

    uint8_t U8() { uint8_t v = 0; ReadRaw(&v, 1); return v; }
    uint16_t U16() { uint16_t v = 0; ReadRaw(&v, 2); return v; }
    uint32_t U32() { uint32_t v = 0; ReadRaw(&v, 4); return v; }
    int32_t I32() { int32_t v = 0; ReadRaw(&v, 4); return v; }
    float F32() { float v = 0.0f; ReadRaw(&v, 4); return v; }
    void Bytes(void* dst, int count) { ReadRaw(dst, count); }

    int Pos() const { return m_Pos; }

private:
    void ReadRaw(void* dst, int count) {
        int avail = m_Size - m_Pos;
        int copy = count;
        if (copy > avail) copy = (avail > 0) ? avail : 0;
        if (copy > 0) {
            std::memcpy(dst, m_Buf + m_Pos, copy);
            m_Pos += copy;
        }
        if (copy < count) {
            // Zero-fill the unread tail -- bounds-tolerant per contract.
            std::memset(static_cast<uint8_t*>(dst) + copy, 0, count - copy);
        }
    }

    const uint8_t* m_Buf;
    int m_Size;
    int m_Pos;
};

} // namespace Mortar

#endif // FN_ENGINE_NETWORK_BYTE_BUFFER_H
