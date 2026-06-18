#ifndef FN_SCREENS_PURCHASE_INFO_H
#define FN_SCREENS_PURCHASE_INFO_H

// PurchaseInfo — per-purchasable power-up metadata record.
//
// Binary ctor @ 0x0011bdd8, dtor @ 0x00118664, sizeof = 196 / 0xC4.
//
// Ctor sequence: ReloadableTexture::ReloadableTexture() on m_Texture, m_InUseTexture,
// m_GreyTexture (three inline calls), then zero-inits m_MaxUses, m_Cost, m_CurrentUses.
// m_Description and m_DisplayName are NOT cleared by ctor (set externally via Parse).
//
// Layout verified against binary field access patterns (re-analyst pass 2026-05-20).
//
// m_TextureFilenames[3] are port-only fields (beyond 0xC4) used to stash the
// filenames that Parse reads from XML so LoadTextures() can call Load() later.
// The binary stores filenames by a different mechanism not yet fully RE'd.

#include "asset/ReloadableTexture.h"
#include <cstdint>
#include "engine/xml/TiXmlElement.h"

class PurchaseInfo {
public:
    int  m_MaxUses;                                  // +0x00
    int  m_Cost;                                     // +0x04
    char m_Description[128];                         // +0x08 (RE: stores <description> child text)
    char m_DisplayName[32];                          // +0x88
    Mortar::ReloadableTexture m_Texture;             // +0xA8 (8 bytes)
    Mortar::ReloadableTexture m_InUseTexture;        // +0xB0 (8 bytes)
    Mortar::ReloadableTexture m_GreyTexture;         // +0xB8 (8 bytes)
    int  m_CurrentUses;                              // +0xC0
    // Port-specific: filename buffers beyond binary sizeof (0xC4).
    // The binary stores texture filenames via a mechanism not yet fully RE'd.
    char m_TextureFilenames[3][64];                  // port-only, not in binary struct

    // Binary ctor @ 0x0011bdd8
    PurchaseInfo();

    // Binary dtor @ 0x00118664
    ~PurchaseInfo();

    // Binary @ 0x001576ac — returns reference to icon texture slot
    Mortar::ReloadableTexture& GetTexture()      { return m_Texture;      }

    // Binary @ 0x001576c4 — returns reference to in-use (active) texture slot
    Mortar::ReloadableTexture& GetInUseTexture() { return m_InUseTexture; }

    // Binary @ 0x001576b8 — returns reference to greyed-out (unavailable) texture slot
    Mortar::ReloadableTexture& GetGreyTexture()  { return m_GreyTexture;  }

    // Binary @ 0x00118474 — parse <purchase_info> XML element.
    void Parse(TiXmlElement* xml);

    // Binary @ 0x001183d4 — load m_Texture / m_InUseTexture / m_GreyTexture.
    void LoadTextures();

    // @ 0x00118334 — call Unload() on all three ReloadableTexture slots.
    // ASM-verified: 2026-05-18 binary @ 0x00118334 (re-analyst)
    void UnloadTextures();
};

#ifdef __bada__
#include <cstddef>
// m_TextureFilenames[] is a port-only addition beyond the binary struct; sizeof != 0xC4.
static_assert(offsetof(PurchaseInfo, m_MaxUses)      == 0x00, "PurchaseInfo m_MaxUses offset");
static_assert(offsetof(PurchaseInfo, m_Cost)         == 0x04, "PurchaseInfo m_Cost offset");
static_assert(offsetof(PurchaseInfo, m_Description)  == 0x08, "PurchaseInfo m_Description offset");
static_assert(offsetof(PurchaseInfo, m_DisplayName)  == 0x88, "PurchaseInfo m_DisplayName offset");
static_assert(offsetof(PurchaseInfo, m_Texture)      == 0xA8, "PurchaseInfo m_Texture offset");
static_assert(offsetof(PurchaseInfo, m_InUseTexture) == 0xB0, "PurchaseInfo m_InUseTexture offset");
static_assert(offsetof(PurchaseInfo, m_GreyTexture)  == 0xB8, "PurchaseInfo m_GreyTexture offset");
static_assert(offsetof(PurchaseInfo, m_CurrentUses)  == 0xC0, "PurchaseInfo m_CurrentUses offset");
#endif

#endif // FN_SCREENS_PURCHASE_INFO_H
