// Localisation smoke test: loads translations_*.str from the asset
// directory and prints resolved strings for a handful of known keys.
//
// First-run mode: prints the resolved values so the user can verify them
// visually. Once verified, the same keys are re-asserted against pinned
// expected strings (see kExpect[] below).
//
// Run via:
//   cd build && ctest --output-on-failure -R localisation

#include "util/Localisation.h"
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
static const TestKey kKeys[] = {
    { "GEN_OK",        "OK"                                    },
    { "GEN_CANCEL",    "Cancel"                                },
    { "DOJO_TEXT_00",  "ORIGINAL BLADE"                        },
    { "DOJO_TEXT_03",  "SHINY RED BLADE"                       },
    { "DOJO_TEXT_05",  "DISCO BLADE"                           },
    { "DOJO_TEXT_06",  "SLICE 50 BANANAS TO UNLOCK. %i TO GO!" },
    { "DOJO_TEXT_22",  "ORIGINAL WOOD"                         },
    // Pass-through: keys that don't exist in the table return the key itself.
    { "MENU_PLAY",     "MENU_PLAY"                             },
    { "FRONTEND_DOJO", "FRONTEND_DOJO"                         },
    { "NOT_A_REAL_KEY","NOT_A_REAL_KEY"                        },
};

int main(int argc, char** argv) {
    const char* dataDir = (argc > 1) ? argv[1] : DATA_DIR_DEFAULT;
    const int   lang    = (argc > 2) ? std::atoi(argv[2]) : 0;  // english_us

    printf("test_localisation: dataDir='%s' languageFlag=%d\n", dataDir, lang);

    Localisation::Load(dataDir, lang);

    if (!Localisation::IsLoaded()) {
        fprintf(stderr, "FAIL: Localisation::Load did not complete\n");
        return 1;
    }
    printf("loaded: count=%u\n", Localisation::s_count);

    int failures = 0;
    for (const TestKey& tk : kKeys) {
        const char* got = Localisation::Get(tk.key);
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

    Localisation::Unload();

    if (failures != 0) {
        fprintf(stderr, "test_localisation: %d failures\n", failures);
        return 1;
    }
    printf("test_localisation: PASS\n");
    return 0;
}
