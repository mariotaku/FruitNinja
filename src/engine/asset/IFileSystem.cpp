// Analysed: 2026-05-04T00:00
#include "asset/IFileSystem.h"
#include "asset/IFile.h"

namespace Mortar {

// Binary @ IFileSystem base ctor: zero m_systemId / m_priority
IFileSystem::IFileSystem()
    : m_systemId(0)
    , m_priority(0)
{
}

IFileSystem::~IFileSystem() {
}

// Binary @ non-virtual helper — no per-system IFile list in port; no-op stub.
void IFileSystem::RegisterIFile(IFile* /*f*/) {
}

// Binary @ non-virtual helper — no-op stub.
void IFileSystem::DeregisterIFile(IFile* /*f*/) {
}

} // namespace Mortar
