#include "util/Immutable.h"

// File-scope pool -- NOT function-local static, so no __cxa_guard /
// __aeabi_atexit is emitted. All Immutable instances share this one map for
// the lifetime of the process.
static std::map<std::string, Immutable::Node*> s_ImmutablePool;

std::map<std::string, Immutable::Node*>& Immutable::GetPool() {
    return s_ImmutablePool;
}

Immutable::Node* Immutable::Intern(const std::string& str) {
    std::map<std::string, Immutable::Node*>::iterator it = s_ImmutablePool.find(str);
    if (it != s_ImmutablePool.end()) {
        return it->second;
    }
    Immutable::Node* node = new Immutable::Node(str);
    s_ImmutablePool[str] = node;
    return node;
}

void Immutable::AddRef() {
    if (m_Node) {
        m_Node->m_RefCount++;
    }
}

void Immutable::Release() {
    if (m_Node) {
        m_Node->m_RefCount--;
        if (m_Node->m_RefCount <= 0) {
            s_ImmutablePool.erase(m_Node->s);
            delete m_Node;
        }
        m_Node = NULL;
    }
}

Immutable::Immutable(const std::string& str) : m_Node(Intern(str)) {
    AddRef();
}

Immutable::Immutable(const char* str) : m_Node(Intern(std::string(str))) {
    AddRef();
}
