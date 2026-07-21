// StringTable smoke test: loads translations_*.str from the asset
// directory and prints resolved strings for a handful of known keys.
//
// First-run mode: prints the resolved values so the user can verify them
// visually. Once verified, the same keys are re-asserted against pinned
// expected strings (see kExpect[] below).
//
// Run via:
//   cd build/host && ctest --output-on-failure -R localisation

#include "util/StringTable.h"
#include "asset/FileManager.h"
#include "asset/FileSystem_Direct.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// FN_BADA_DATA_DIR is injected by tests/CMakeLists.txt as
// "${CMAKE_SOURCE_DIR}/FruitNinjaBada/Data" -- portable across machines/checkouts.
static const char* DATA_DIR_DEFAULT = FN_BADA_DATA_DIR;

struct TestKey {
    const char* key;
    const char* expectedEnglishUS;  // empty = print only, don't assert
};

// Expected strings, verified manually from a first run against
// translations_english_us.str. All assertions enabled.
// Note: miss fallback is "STRING NOT FOUND" (not the key itself) per binary.
static const TestKey kKeys[] = {
    { "GEN_OK",        "OK"                                    },
    { "GEN_CANCEL",    "Cancel"                                },
    { "DOJO_TEXT_00",  "ORIGINAL BLADE"                        },
    { "DOJO_TEXT_03",  "SHINY RED BLADE"                       },
    { "DOJO_TEXT_05",  "DISCO BLADE"                           },
    { "DOJO_TEXT_06",  "SLICE 50 BANANAS TO UNLOCK. %i TO GO!" },
    { "DOJO_TEXT_22",  "ORIGINAL WOOD"                         },
    // Miss: keys not in the table return "STRING NOT FOUND" per binary.
    { "MENU_PLAY",     "STRING NOT FOUND"                      },
    { "FRONTEND_DOJO", "STRING NOT FOUND"                      },
    { "NOT_A_REAL_KEY","STRING NOT FOUND"                      },
};

int main(int argc, char** argv) {
    const char* dataDir = (argc > 1) ? argv[1] : DATA_DIR_DEFAULT;
    const int   lang    = (argc > 2) ? std::atoi(argv[2]) : 0;  // english_us

    printf("test_localisation: dataDir='%s' languageFlag=%d\n", dataDir, lang);

    // Set up FileSystem for Mortar::File (matches GameInitialise's CreateFileSystems step).
    {
        Mortar::FileSystem_Direct* fs = new Mortar::FileSystem_Direct();
        fs->Initialise(dataDir, /*writable=*/false);
        FileManager::GetInstance().AddSystem(fs, /*id=*/0, /*priority=*/0);
    }

    Mortar::StringTable::Load(dataDir, lang);

    if (!Mortar::StringTable::IsLoaded()) {
        fprintf(stderr, "FAIL: StringTable::Load did not complete\n");
        return 1;
    }
    printf("loaded\n");

    int failures = 0;
    for (int i = 0; i < (int)(sizeof(kKeys)/sizeof(kKeys[0])); i++) {
        const TestKey& tk = kKeys[i];
        const char* got = Mortar::StringTable::GetStringS(tk.key);
        printf("  %-22s -> '%s'\n", tk.key, got ? got : "(null)");

        if (tk.expectedEnglishUS && tk.expectedEnglishUS[0] != '\0') {
            if (!got || std::strcmp(got, tk.expectedEnglishUS) != 0) {
                fprintf(stderr,
                        "    FAIL: expected '%s', got '%s'\n",
                        tk.expectedEnglishUS,
                        got ? got : "(null)");
                ++failures;
            }
        }
    }

    // Regression asserts for v1.6.1 integer-ID anchors (re-derived via tmp/remap_lstr.py).
    // These pin the LSTR_* enum values against the shipped translations_header.str.
    struct IntIdAnchor {
        LocalizedString id;
        const char* label;
        const char* expected;
    };
    static const IntIdAnchor kIntAnchors[] = {
        { LSTR_BEST,              "LSTR_BEST (0xc8)",             "BEST:"                   },
        { LSTR_BEST_COMBO,        "LSTR_BEST_COMBO (0xab)",       "BEST COMBO: %i FRUIT!"   },
        { LSTR_FRUIT_FACT_TITLE,  "LSTR_FRUIT_FACT_TITLE (0xae)", "SENSEI'S FRUIT FACT"     },
        { LSTR_SHOP_BACKGROUND,   "LSTR_SHOP_BACKGROUND (0xc9)",  "BACKGROUND"              },
        { LSTR_SHOP_BLADE,        "LSTR_SHOP_BLADE (0xca)",       "BLADE"                   },
        { LSTR_SHOP_FULL_VERSION, "LSTR_SHOP_FULL_VERSION (0xcb)","FULL VERSION"            },
        // MainScreen top-left instruction parchment. v1.6.1 removed slice_fruit.tex and draws
        // this as dynamic text from GETSTRING_CAST_0(0x39d) (MENU_TEXTURE_13). Raw-int cast so
        // the assert is independent of the LSTR_* enum name.
        { (LocalizedString)0x39d, "MENU_TEXTURE_13 (0x39d)",      "SLICE FRUIT TO BEGIN"    },
    };
    for (int i = 0; i < (int)(sizeof(kIntAnchors)/sizeof(kIntAnchors[0])); i++) {
        const IntIdAnchor& a = kIntAnchors[i];
        const char* got = GETSTRING_CAST_0(a.id);
        printf("  %-36s -> '%s'\n", a.label, got ? got : "(null)");
        if (!got || std::strcmp(got, a.expected) != 0) {
            fprintf(stderr, "    FAIL: %s expected '%s', got '%s'\n",
                    a.label, a.expected, got ? got : "(null)");
            ++failures;
        }
    }

    Mortar::StringTable::Unload();

    if (failures != 0) {
        fprintf(stderr, "test_localisation: %d failures\n", failures);
        return 1;
    }
    printf("test_localisation: PASS\n");
    return 0;
}
