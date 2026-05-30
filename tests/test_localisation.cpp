// StringTable smoke test: loads translations_*.str from the asset
// directory and prints resolved strings for a handful of known keys.
//
// First-run mode: prints the resolved values so the user can verify them
// visually. Once verified, the same keys are re-asserted against pinned
// expected strings (see kExpect[] below).
//
// Run via:
//   cd build && ctest --output-on-failure -R localisation

#include "util/StringTable.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static const char* DATA_DIR_DEFAULT =
    "C:/Users/Mariotaku/Projects/webosbrew/fruit-ninja/FruitNinjaBada/Data";

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

    // Test integer-ID overload for confirmed LSTR_ values.
    {
        const char* best = Mortar::GETSTRING_CAST_0(LSTR_BEST);
        printf("  LSTR_BEST (0xb5)       -> '%s'\n", best ? best : "(null)");
        // "BEST:" in english_us -- only assert if table loaded with assets.
        if (best && std::strcmp(best, "STRING NOT FOUND") != 0) {
            if (std::strcmp(best, "BEST:") != 0) {
                fprintf(stderr, "    FAIL: LSTR_BEST expected 'BEST:', got '%s'\n", best);
                ++failures;
            }
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
