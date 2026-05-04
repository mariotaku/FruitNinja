// Analysed: 2026-05-04T00:00
#include "asset/IFile.h"
#include "asset/IFileSystem.h"

namespace Mortar {

// Binary @ 0x0019baa8 / 0x0019bb1c
IFile::IFile(IFileSystem* sys)
    : m_pSystem(sys)
{
    if (m_pSystem) {
        m_pSystem->RegisterIFile(this);
    }
}

// Binary @ 0x0019baa8 (D2 — destroys object but NOT memory; called by placement-style dtors)
IFile::~IFile() {
    if (m_pSystem) {
        m_pSystem->DeregisterIFile(this);
    }
}

} // namespace Mortar
