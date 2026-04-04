#ifndef MORTAR_ASCII_STRING_H
#define MORTAR_ASCII_STRING_H

#include <string>

// Simple string wrapper using std::string internally
// Original: 40 bytes (0x28) with SSO-like layout
class AsciiString {
    std::string m_str;
public:
    AsciiString() {}
    AsciiString(const char* s) : m_str(s ? s : "") {}
    AsciiString(const std::string& s) : m_str(s) {}

    const char* CStr() const { return m_str.c_str(); }
    int Length() const { return (int)m_str.length(); }
    bool IsEmpty() const { return m_str.empty(); }

    bool operator==(const AsciiString& o) const { return m_str == o.m_str; }
    bool operator!=(const AsciiString& o) const { return m_str != o.m_str; }
    bool operator<(const AsciiString& o) const { return m_str < o.m_str; }
};

#endif // MORTAR_ASCII_STRING_H
