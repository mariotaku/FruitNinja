#ifndef FN_ENGINE_UTIL_IMMUTABLE_H
#define FN_ENGINE_UTIL_IMMUTABLE_H

// Immutable<std::string> — thin interned-string handle.
// Layout: single 4-byte Node* pointer (m_Node).
// Two equal strings always produce the same Node*, so operator== is pointer comparison (O(1)).
//
// Fruit Ninja is single-threaded — no mutex is needed for the intern pool.
// If threading is ever added, the pool map must be protected.

#include <string>
#include <map>

template<typename T>
class Immutable;

template<>
class Immutable<std::string> {
public:
    struct Node {
        int         m_RefCount;
        std::string s;

        explicit Node(const std::string& str) : m_RefCount(0), s(str) {}
    };

private:
    Node* m_Node;

    static std::map<std::string, Node*>& GetPool() {
        static std::map<std::string, Node*> s_Pool;
        return s_Pool;
    }

    static Node* Intern(const std::string& str) {
        std::map<std::string, Node*>& pool = GetPool();
        std::map<std::string, Node*>::iterator it = pool.find(str);
        if (it != pool.end()) {
            return it->second;
        }
        Node* node = new Node(str);
        pool[str] = node;
        return node;
    }

    void AddRef() {
        if (m_Node) {
            m_Node->m_RefCount++;
        }
    }

    void Release() {
        if (m_Node) {
            m_Node->m_RefCount--;
            if (m_Node->m_RefCount <= 0) {
                std::map<std::string, Node*>& pool = GetPool();
                pool.erase(m_Node->s);
                delete m_Node;
            }
            m_Node = NULL;
        }
    }

public:
    Immutable() : m_Node(NULL) {}

    explicit Immutable(const std::string& str) : m_Node(Intern(str)) {
        AddRef();
    }

    explicit Immutable(const char* str) : m_Node(Intern(std::string(str))) {
        AddRef();
    }

    Immutable(const Immutable<std::string>& other) : m_Node(other.m_Node) {
        AddRef();
    }

    ~Immutable() {
        Release();
    }

    Immutable<std::string>& operator=(const Immutable<std::string>& other) {
        if (this != &other) {
            if (other.m_Node) other.m_Node->m_RefCount++;
            Release();
            m_Node = other.m_Node;
        }
        return *this;
    }

    bool operator==(const Immutable<std::string>& other) const {
        return m_Node == other.m_Node;
    }

    bool operator!=(const Immutable<std::string>& other) const {
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
static_assert(sizeof(Immutable<std::string>) == 4,
              "Immutable<std::string> must be 4 bytes (single Node* pointer)");
#endif

#endif  // FN_ENGINE_UTIL_IMMUTABLE_H
