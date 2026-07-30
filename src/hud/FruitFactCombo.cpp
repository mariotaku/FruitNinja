// FruitFactCombo -- combo helper functions extracted from the old v1.5.1
// FruitFactControl.cpp. These are free functions shared across FruitFactControl,
// FruitFactZenPage, and GameOverScreen.
//
// Binary symbols (v1.6.1):
//   FruitFact::CheckCombo       @ 0x00110cb0
//   GetComboStarTexture         @ 0x00132a94
//   GetComboStarText            @ 0x001325f8
//   GetComboName                @ 0x00110c94  (definition in GameOverScreen.cpp)

#include "hud/FruitFactCombo.h"
#include "entities/Fruit.h"
#include "engine/asset/TextureManager.h"
#include "engine/math/Random.h"
#include <cstdint>

using Mortar::TextureManager;

// ---------------------------------------------------------------------------
// v1.6.1 CheckCombo @0x001320b4 -- pattern matcher for the BestCombo slice
// array. Input is a 1..11-slot array of fruit type indices (m_ComboSliceArr
// values stored as int). Returns 0..24 = combo-name index (see GetComboName
// at GameOverScreen.cpp:944), or 0xFF for "no combo".
//
// Algorithm (ASM-verified 2026-05-22, re-analyst):
//   - Build unique-fruit work table tracking (type, count) per slot.
//   - Track `alternating` = true while consecutive same-fruit hits never
//     match the *newest* entry (ABAB... checkerboard pattern).
//   - Compute `unique` count + dominant fruit.
//   - Branch on (unique, count):
//       unique==1   -> ALL_<FRUIT> lookup by type-index
//       unique==2 && alternating -> CHECKERS (combo 24)
//       unique==2 && count==5 && (n0==2 || n1==2) -> FULLHOUSE (20)
//       unique==3 && count==5 && (n0==2 || n1==2) -> 2_PAIR (21)
//       unique==count && unique>4 -> ALL_DIFFERENT (4)
//       X-OF-A-KIND scan: any n==3 -> 22, any n==4 -> 23 (4OAK overrides 3OAK)
//       fallback by count: 3->0, 4->1, 5->2, 6->3, 7+->5; <3 -> 0xFF
// ---------------------------------------------------------------------------

// Fruit name -> ALL_<FRUIT> combo byte. Resolved to type indices lazily on
// first CheckCombo call (Fruit::FruitType doesn't depend on game state).
// Binary's pair table at DAT_00110f88 is hash-keyed; port uses type indices
// since m_ComboSliceArr stores indices, not hashes.
struct AllFruitEntry { const char* name; uint8_t combo; int typeIdx; };
static AllFruitEntry s_AllFruitTable[13] = {
    { "apple_red",     6, -1 }, { "orange",       7, -1 },
    { "pineapple",     8, -1 }, { "watermelon",   9, -1 },
    { "kiwifruit",    10, -1 }, { "mango",       11, -1 },
    { "strawberry",   12, -1 }, { "pear",        13, -1 },
    { "banana",       14, -1 }, { "lime",        15, -1 },
    { "lemon",        16, -1 }, { "coconut",     17, -1 },
    { "passionfruit", 18, -1 },
};
static bool s_AllFruitResolved = false;

static void ResolveAllFruitIndices() {
    if (s_AllFruitResolved) return;
    s_AllFruitResolved = true;
    for (int i = 0; i < 13; ++i) {
        s_AllFruitTable[i].typeIdx = Fruit::FruitType(s_AllFruitTable[i].name, false);
    }
}

uint8_t FruitFact::CheckCombo(int* hashes, int count, int* outFruitIdx) {
    if (!hashes || count <= 0) return 0xFF;
    ResolveAllFruitIndices();

    struct Entry { int type; int n; };
    Entry work[16];
    int unique = 0;
    int domType = -1;
    int domCount = 0;
    bool alternating = true;

    for (int i = 0; i < count; ++i) {
        bool found = false;
        for (int j = 0; j < unique; ++j) {
            if (work[j].type == hashes[i]) {
                found = true;
                ++work[j].n;
                if (work[j].n > domCount) { domCount = work[j].n; domType = work[j].type; }
                // alternating gate: hit on NEWEST entry breaks the alternating
                // pattern (means same-fruit-back-to-back somewhere).
                if (j == unique - 1) alternating = false;
                break;
            }
        }
        if (!found) {
            if (unique < 16) { work[unique].type = hashes[i]; work[unique].n = 1; ++unique; }
            if (domCount == 0) { domType = hashes[i]; domCount = 1; }
        }
    }
    if (outFruitIdx) *outFruitIdx = domType;

    if (unique == 1) {
        // ALL_<FRUIT> -- match by type-index.
        for (int i = 0; i < 13; ++i) {
            if (s_AllFruitTable[i].typeIdx >= 0 && hashes[0] == s_AllFruitTable[i].typeIdx) {
                return s_AllFruitTable[i].combo;
            }
        }
        // Fruit not in the all-fruit table (e.g. moose, special) -> fall
        // through to count-based fallback below.
    } else if (unique == 2) {
        if (alternating) return 24; // 5_OF_A_KIND / CHECKERS pattern
        if (count == 5 && (work[0].n == 2 || work[1].n == 2)) return 20; // FULLHOUSE
    } else if (unique == 3 && count == 5 &&
               (work[0].n == 2 || work[1].n == 2)) {
        return 21; // 2_PAIR
    } else if (unique == count && unique > 4) {
        return 4; // ALL_DIFFERENT (5+ slices, all distinct)
    }

    // X-OF-A-KIND scan: priority 4OAK > 3OAK.
    if (unique > 1) {
        int8_t r = -1;
        for (int i = 0; i < unique; ++i) {
            if (work[i].n == 3 && r == -1) r = 22;
            else if (work[i].n == 4 && r < 23) r = 23;
        }
        if (r != -1) return (uint8_t)r;
    }

    // Count-based fallback (binary table at DAT_00110f60).
    if (count < 3) return 0xFF;
    if (count == 3) return 0;
    if (count == 4) return 1;
    if (count == 5) return 2;
    if (count == 6) return 3;
    return 5; // 7_FRUIT_PLUS
}

// ---------------------------------------------------------------------------
// GetComboStarTexture -- binary: _Z19GetComboStarTexture10COMBO_TYPE @0x00132a94 (v1.6.1)
// Returns the star-burst texture for the given combo type. Some combos have
// multiple tier-textures; binary picks one uniformly at random per call
// using Math::g_Random. Port mirrors via Math::g_Random.Rand32(count).
// ---------------------------------------------------------------------------
struct ComboStarEntry { uint8_t count; const char* tex[3]; };
static const ComboStarEntry kComboStars[25] = {
    { 2, { "star_fruity.tex",          "star_juicy.tex",          NULL } },             // 0  3_FRUIT
    { 2, { "star_yummy.tex",           "star_tasty.tex",          NULL } },             // 1  4_FRUIT
    { 2, { "star_lush.tex",            "star_delicious.tex",      NULL } },             // 2  5_FRUIT
    { 2, { "star_succulent.tex",       "star_succulent.tex",      NULL } },             // 3  6_FRUIT (binary stores dup)
    { 3, { "star_fruit_salad.tex",     "star_fruits_basket.tex",  "star_megamix.tex" } }, // 4  ALL_DIFFERENT
    { 2, { "star_amazing.tex",         "star_exquisite.tex",      NULL } },             // 5  7_FRUIT_PLUS
    { 1, { "star_its_apples.tex",      NULL,                      NULL } },             // 6  ALL_APPLES
    { 1, { "star_vitamin_c.tex",       NULL,                      NULL } },             // 7  ALL_ORANGES
    { 1, { "star_got_the_sweats.tex",  NULL,                      NULL } },             // 8  ALL_PINEAPPLES
    { 1, { "star_melon_mania.tex",     NULL,                      NULL } },             // 9  ALL_WATERMELONS
    { 1, { "star_flightless_bird.tex", NULL,                      NULL } },             // 10 ALL_KIWIS
    { 1, { "star_mango_smoothie.tex",  NULL,                      NULL } },             // 11 ALL_MANGOES
    { 1, { "star_full_punnet.tex",     NULL,                      NULL } },             // 12 ALL_STRAWBERRIES
    { 1, { "star_pear_tree.tex",       NULL,                      NULL } },             // 13 ALL_PEARS
    { 1, { "star_banana_cake.tex",     NULL,                      NULL } },             // 14 ALL_BANANAS
    { 1, { "star_scurvy_cure.tex",     NULL,                      NULL } },             // 15 ALL_LIMES
    { 1, { "star_lemon_line_up.tex",   NULL,                      NULL } },             // 16 ALL_LEMONS
    { 1, { "star_lovely_bunch.tex",    NULL,                      NULL } },             // 17 ALL_COCONUTS
    { 1, { "star_passion_punch.tex",   NULL,                      NULL } },             // 18 ALL_PASSIONFRUITS
    { 1, { "star_alphabetic.tex",      NULL,                      NULL } },             // 19 ALPHABETICAL
    { 1, { "star_full_house.tex",      NULL,                      NULL } },             // 20 FULLHOUSE
    { 1, { "star_two_pairs.tex",       NULL,                      NULL } },             // 21 2_PAIR
    { 1, { "star_three_of_a_kind.tex", NULL,                      NULL } },             // 22 3_OF_A_KIND
    { 1, { "star_four_of_a_kind.tex",  NULL,                      NULL } },             // 23 4_OF_A_KIND
    { 1, { "star_checkers.tex",        NULL,                      NULL } },             // 24 5_OF_A_KIND (CHECKERS)
};

// GetComboStarTexture  binary: _Z19GetComboStarTexture10COMBO_TYPE @0x00132a94 (v1.6.1)
Mortar::SmartPtr<Mortar::Texture> GetComboStarTexture(COMBO_TYPE comboType) {
    uint8_t ct = (uint8_t)comboType;
    if (ct >= 25) return Mortar::SmartPtr<Mortar::Texture>();
    const ComboStarEntry& e = kComboStars[ct];
    uint32_t tier = (e.count > 1) ? Math::g_Random.Rand32(e.count) : 0;
#if defined(FRUIT_PLATFORM_WII)
    // Port specific: the returned texture is stored in a write-only member and
    // NEVER drawn (see FruitFactZenPage -- the star visual is the "* {name}" font
    // text). On Wii the LoadLocalisedTexture would be a wasted per-combo lazy disk
    // read of one of ~40 random combo-star icons the #36 block-preload can't cover
    // (combo-type + random-tier dependent) that never renders. Skip the load; the
    // Rand32 above still runs so the shared RNG sequence stays byte-identical to
    // the binary/other platforms. Returns a null ref (the member goes unused).
    (void)tier;
    return Mortar::SmartPtr<Mortar::Texture>();
#else
    return Mortar::TextureManager::LoadLocalisedTexture(e.tex[tier]);
#endif
}

// GetComboStarText  binary: _Z16GetComboStarText10COMBO_TYPE @0x001325f8 (v1.6.1)
// ASM-spec v1.6.1 GetComboStarText @0x001325f8: per-COMBO_TYPE LSTR-id LUT
// (stickerNames @0x00280480, 28-byte entries: +0 count, then up to 3 u16 ids),
// random pick via Math::g_Random.Rand32(count). 25 valid types; ct>0x18 -> 0.
// Ids index translations_header.str (GAME_TEXTURE_55..85); caller GETSTRING-casts.
struct ComboStarTextEntry { uint8_t count; uint16_t ids[3]; };
static const ComboStarTextEntry kComboStarText[25] = {
    {2,{814,819,0}},{2,{834,830,0}},{2,{822,808,0}},{2,{829,829,0}},
    {3,{812,813,824}},{2,{804,809,0}},{1,{818,0,0}},{1,{833,0,0}},
    {1,{817,0,0}},{1,{825,0,0}},{1,{810,0,0}},{1,{823,0,0}},{1,{816,0,0}},
    {1,{827,0,0}},{1,{806,0,0}},{1,{828,0,0}},{1,{820,0,0}},{1,{821,0,0}},
    {1,{826,0,0}},{1,{805,0,0}},{1,{815,0,0}},{1,{832,0,0}},{1,{831,0,0}},
    {1,{811,0,0}},{1,{807,0,0}},
};
unsigned int GetComboStarText(COMBO_TYPE comboType) {
    uint8_t ct = (uint8_t)comboType;
    if (ct > 0x18) return 0;
    const ComboStarTextEntry& e = kComboStarText[ct];
    uint32_t idx = (e.count > 1) ? Math::g_Random.Rand32(e.count) : 0;
    return e.ids[idx];
}
