#include "render/FontTTFRegistry.h"
#include "render/FontCacheObjectTTF.h"
#include "debug/Logger.h"

namespace Mortar {

FontTTFRegistry::FontTTFRegistry() {
}

FontTTFRegistry::~FontTTFRegistry() {
    // Destroy all cached TTF faces.
    for (std::map<const Font*, FontCacheObjectTTF*>::iterator it = m_Map.begin();
         it != m_Map.end(); ++it) {
        delete it->second;
    }
    m_Map.clear();
}

FontTTFRegistry& FontTTFRegistry::GetInstance() {
    // Port specific: construct-on-first-use, NEVER destroyed. The registry must
    // outlive every Font destructor -- Font::~Font calls Unregister() (FontTTFRegistry.cpp:56).
    // A plain function-local value-static is destroyed at atexit in reverse
    // construction order; since the registry is built lazily at runtime (after
    // pre-main globals like FN::s_DebugFont), it would be torn down FIRST, and
    // s_DebugFont's ~Font would then Unregister() into a freed std::map -> crash.
    // Leaking the singleton is the standard static-deinit-order-fiasco fix; the
    // OS reclaims the memory at process exit (the dtor's TtfFace deletes are a
    // clean-exit-only nicety we forgo to stay crash-safe).
    static FontTTFRegistry* instance = new FontTTFRegistry();
    return *instance;
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
