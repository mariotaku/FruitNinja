#include "render/FontTTFRegistry.h"
#include "render/FontCacheObjectTTF.h"
#include "debug/Logger.h"

#include <ft2build.h>
#include FT_FREETYPE_H

namespace Mortar {

FontTTFRegistry::FontTTFRegistry()
    : m_FTLib(nullptr)
{
}

FontTTFRegistry::~FontTTFRegistry() {
    // Destroy all cached TTF faces.
    for (std::map<const Font*, FontCacheObjectTTF*>::iterator it = m_Map.begin();
         it != m_Map.end(); ++it) {
        delete it->second;
    }
    m_Map.clear();

    if (m_FTLib) {
        FT_Done_FreeType(m_FTLib);
        m_FTLib = nullptr;
    }
}

FontTTFRegistry& FontTTFRegistry::GetInstance() {
    static FontTTFRegistry instance;
    return instance;
}

FT_Library FontTTFRegistry::GetFTLibrary() {
    if (!m_FTLib) {
        FT_Error err = FT_Init_FreeType(&m_FTLib);
        if (err) {
            LOG_ERROR("FontTTFRegistry", "FT_Init_FreeType failed (err %d)", err);
            m_FTLib = nullptr;
        }
    }
    return m_FTLib;
}

void FontTTFRegistry::Register(Font* font, FontCacheObjectTTF* ttf) {
    // If an old entry exists (re-load), clean it up.
    std::map<const Font*, FontCacheObjectTTF*>::iterator it = m_Map.find(font);
    if (it != m_Map.end()) {
        delete it->second;
        it->second = ttf;
    } else {
        m_Map[font] = ttf;
    }
}

void FontTTFRegistry::Unregister(Font* font) {
    std::map<const Font*, FontCacheObjectTTF*>::iterator it = m_Map.find(font);
    if (it != m_Map.end()) {
        delete it->second;
        m_Map.erase(it);
    }
}

FontCacheObjectTTF* FontTTFRegistry::Lookup(const Font* font) const {
    std::map<const Font*, FontCacheObjectTTF*>::const_iterator it = m_Map.find(font);
    if (it != m_Map.end()) return it->second;
    return nullptr;
}

} // namespace Mortar
