#include "util/Immutable.h"

// Function-local static (Meyers Singleton): lazy-initialised on first call.
// Phase 4 originally tried a file-scope static to dodge __cxa_guard, but that
// hit a static-initialization order fiasco -- file-scope kMeshName_* globals
// in Mesh.cpp's anonymous namespace ran their dynamic initialisers before
// this TU's s_Pool was constructed, crashing inside std::map::find. With the
// function-local pattern, GetPool() is the single point that owns the
// initialisation guard, and because Phase 4 moved GetPool() out-of-line, the
// __cxa_guard_acquire emits ONCE in this TU (not per call site like the
// pre-Phase-4 inline version). All Intern/Release call sites still pay only
// a single BL to the out-of-line function. Single-threaded; no mutex needed.
std::map<std::string, Immutable::Node*>& Immutable::GetPool() {
    static std::map<std::string, Immutable::Node*> s_Pool;
    return s_Pool;
}

Immutable::Node* Immutable::Intern(const std::string& str) {
    std::map<std::string, Immutable::Node*>& pool = GetPool();
    std::map<std::string, Immutable::Node*>::iterator it = pool.find(str);
    if (it != pool.end()) {
        return it->second;
    }
    Immutable::Node* node = new Immutable::Node(str);
    pool[str] = node;
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
            GetPool().erase(m_Node->s);
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
