#ifndef FN_ENGINE_RENDER_FONTTTFREGISTRY_H
#define FN_ENGINE_RENDER_FONTTTFREGISTRY_H

// FontTTFRegistry — side table mapping Font* -> FontCacheObjectTTF*.
//
// Port specific: Font has a fixed binary layout (0x438 bytes, verified by
// static_assert under __bada__). Adding new fields to Font would break the
// layout check in the cross-build. This registry provides the TTF state
// lookup as a separate singleton so Font's layout remains unchanged.
//
// The rasterizer backend (FreeType or stb_truetype, see TtfBackend.h) owns
// its own process-wide state internally now; this registry only tracks the
// Font*->FontCacheObjectTTF* map.

#include <map>

namespace Mortar {

class Font;
class FontCacheObjectTTF;

class FontTTFRegistry {
public:
    static FontTTFRegistry& GetInstance();

    // Register a TTF face for the given Font instance. Ownership of the
    // FontCacheObjectTTF is transferred to the registry.
    void Register(Font* font, FontCacheObjectTTF* ttf);

    // Remove the registration for a Font (called from Font::~Font).
    void Unregister(Font* font);

    // Look up the TTF face for a Font. Returns nullptr for .fnt fonts.
    FontCacheObjectTTF* Lookup(const Font* font) const;

private:
    FontTTFRegistry();
    ~FontTTFRegistry();

    // Non-copyable
    FontTTFRegistry(const FontTTFRegistry&);
    FontTTFRegistry& operator=(const FontTTFRegistry&);

    std::map<const Font*, FontCacheObjectTTF*> m_Map;
};

} // namespace Mortar

#endif // FN_ENGINE_RENDER_FONTTTFREGISTRY_H
