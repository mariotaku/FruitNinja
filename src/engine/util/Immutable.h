#ifndef FN_ENGINE_UTIL_IMMUTABLE_H
#define FN_ENGINE_UTIL_IMMUTABLE_H

// Immutable -- thin interned-string handle (std::string only; de-templated).
// Layout: single 4-byte Node* pointer (m_Node).
// Two equal strings always produce the same Node*, so operator== is pointer comparison (O(1)).
//
// Fruit Ninja is single-threaded -- no mutex is needed for the intern pool.
// If threading is ever added, the pool map must be protected.
//
// Heavy members (GetPool, Intern, AddRef, Release, and the two value ctors) are
// defined out-of-line in Immutable.cpp. The pool is a file-scope static (not a
// function-local static), so no __cxa_guard / __aeabi_atexit is emitted per call
// site. Previously these were inline-in-header template bodies, which caused the
// Mesh ctor to expand the full map-walk 4 times (one per Immutable<string> call).

#include <string>
#include <map>

class Immutable {
public:
    struct Node {
        int         m_RefCount;
        std::string s;

        explicit Node(const std::string& str) : m_RefCount(0), s(str) {}
    };

private:
    Node* m_Node;

    static std::map<std::string, Node*>& GetPool();
    static Node* Intern(const std::string& str);
    void AddRef();
    void Release();

public:
    Immutable() : m_Node(NULL) {}

    explicit Immutable(const std::string& str);
    explicit Immutable(const char* str);

    Immutable(const Immutable& other) : m_Node(other.m_Node) {
        AddRef();
    }

    ~Immutable() {
        Release();
    }

    Immutable& operator=(const Immutable& other) {
        if (this != &other) {
            if (other.m_Node) other.m_Node->m_RefCount++;
            Release();
            m_Node = other.m_Node;
        }
        return *this;
    }

    // Binary: called by operator>>(DataStreamReader&, Immutable&) @0x0025fa40
    // which does `imm = tmp` where tmp is std::string.
    Immutable& operator=(const std::string& s) {
        Immutable tmp(s);
        *this = tmp;
        return *this;
    }

    bool operator==(const Immutable& other) const {
        return m_Node == other.m_Node;
    }

    bool operator!=(const Immutable& other) const {
        return m_Node != other.m_Node;
    }

    const char* c_str() const {
        return m_Node ? m_Node->s.c_str() : "";
    }

    bool empty() const {
        return m_Node == NULL;
    }

    Node* GetNode() const { return m_Node; }
};

#ifdef __bada__
static_assert(sizeof(Immutable) == 4,
              "Immutable must be 4 bytes (single Node* pointer)");
#endif

#endif  // FN_ENGINE_UTIL_IMMUTABLE_H
