// test_slash_combo -- classification pins for the free function CheckCombo
// (v1.6.1 CheckCombo @ 0x001320b4, ported in src/entities/SlashEntity.cpp).
//
// Guards the two fidelity fixes:
//   1. Alternating gate polarity: the binary enters the ABAB element-wise
//      verify when the alternating flag is FALSE (a repeat matched a non-last
//      scratch slot -- exactly what a genuine ABAB stream does). The pre-fix
//      port had the gate inverted, so a real ABAB never scored 0x18 and any
//      2-fruit two-distinct-type slice wrongly returned 0x18.
//   2. 0x14 / 0x15 test the slot COUNT field (slot[k].n == 2), not the fruit
//      type (the pre-fix port compared .type == 2, "pomegranate").
//
// CheckCombo feeds the game-over fact board and persisted save data --
// high-fan-in scoring leaf, hence deterministic pins here.
//
// Pure logic: no SDL init, no GL, no audio. Fruit::FruitType is called once
// inside CheckCombo to seed its rare-fruit table; with no fruitlist.xml
// loaded g_FruitInfoCount==0 so every rare type resolves to -1, which
// never collides with the non-negative type ids used below (all cases here
// use uniq >= 2 and never reach the rare path anyway).
//
// Cross-build safe: no lambdas, no auto, no range-for, no enum class.

#include <cstdio>
#include <cstdlib>

// Free function with external linkage in src/entities/SlashEntity.cpp
// (binary free symbol CheckCombo @ 0x001320b4; not declared in a header).
int CheckCombo(int* fruitTypes, int count, int* outDominantType);

#define CHECK_EQ(expr, expected) \
    do { \
        int _v = (expr); \
        int _e = (expected); \
        if (_v != _e) { \
            std::printf("FAIL (%s:%d): %s = 0x%02x (%d), expected 0x%02x (%d)\n", \
                        __FILE__, __LINE__, #expr, _v & 0xff, _v, _e & 0xff, _e); \
            ::exit(1); \
        } \
    } while (0)

int main() {
    std::printf("test_slash_combo: start\n");

    // --- 0x18: genuine strict ABAB alternation, any length ---
    {
        int abab4[4] = { 1, 2, 1, 2 };
        CHECK_EQ(CheckCombo(abab4, 4, 0), 0x18);

        int ababa5[5] = { 1, 2, 1, 2, 1 };
        CHECK_EQ(CheckCombo(ababa5, 5, 0), 0x18);

        int abab6[6] = { 7, 3, 7, 3, 7, 3 };
        CHECK_EQ(CheckCombo(abab6, 6, 0), 0x18);
    }

    // --- NOT 0x18: 2 fruit of two distinct types (pre-fix wrongly 0x18).
    // Falls all the way through to the fallback table: fallback[2] = -1. ---
    {
        int ab2[2] = { 1, 2 };
        CHECK_EQ(CheckCombo(ab2, 2, 0), -1);
    }

    // --- NOT 0x18: 2 types but broken alternation (ABBA). Flag goes false
    // (the trailing A matches slot 0 while slot 1 is last) but the
    // element-wise verify rejects; fallback[4] = 1. ---
    {
        int abba[4] = { 1, 2, 2, 1 };
        CHECK_EQ(CheckCombo(abba, 4, 0), 1);
    }

    // --- 0x14: 5 fruit, 2 types, 3+2 split (slot count field == 2) ---
    {
        int aabbb[5] = { 4, 4, 9, 9, 9 };   // alternating flag stays true -> skips ABAB gate
        CHECK_EQ(CheckCombo(aabbb, 5, 0), 0x14);

        int aabab[5] = { 4, 4, 9, 4, 9 };   // flag false, ABAB verify fails -> 0x14
        CHECK_EQ(CheckCombo(aabab, 5, 0), 0x14);

        int aaabb[5] = { 4, 4, 4, 9, 9 };   // pair lands in slot 1
        CHECK_EQ(CheckCombo(aaabb, 5, 0), 0x14);
    }

    // --- NOT 0x14: 5 fruit, 2 types, 4+1 split -- no slot has n==2;
    // the maxCount==4 branch yields 0x17 instead. ---
    {
        int aaaab[5] = { 4, 4, 4, 4, 9 };
        CHECK_EQ(CheckCombo(aaaab, 5, 0), 0x17);
    }

    // --- 0x15: 5 fruit, 3 types, one pair (2+2+1 split; slots 0/1 suffice) ---
    {
        int aabcc[5] = { 1, 1, 2, 3, 3 };   // pairs in slots 0 and 2
        CHECK_EQ(CheckCombo(aabcc, 5, 0), 0x15);

        int abcbc[5] = { 1, 2, 3, 2, 3 };   // singleton in slot 0, pairs in 1/2
        CHECK_EQ(CheckCombo(abcbc, 5, 0), 0x15);

        int aabbc[5] = { 5, 5, 6, 6, 7 };   // singleton in slot 2
        CHECK_EQ(CheckCombo(aabbc, 5, 0), 0x15);
    }

    // --- NOT 0x15: 5 fruit, 3 types, 3+1+1 split -- no pair;
    // maxCount==3 branch yields 0x16. ---
    {
        int aaabc[5] = { 1, 1, 1, 2, 3 };
        CHECK_EQ(CheckCombo(aaabc, 5, 0), 0x16);
    }

    // --- 0x16: a slot reaches count 3 (uniq>1, not a 5-fruit special) ---
    {
        int aaab[4] = { 1, 1, 1, 2 };
        CHECK_EQ(CheckCombo(aaab, 4, 0), 0x16);
    }

    // --- 0x04: all unique, count >= 5 ---
    {
        int uniq5[5] = { 1, 2, 3, 4, 5 };
        CHECK_EQ(CheckCombo(uniq5, 5, 0), 0x04);
    }

    // --- dominant-type out param: most frequent type wins ---
    {
        int abb[3] = { 1, 2, 2 };
        int dom = -1;
        (void)CheckCombo(abb, 3, &dom);
        CHECK_EQ(dom, 2);
    }

    std::printf("test_slash_combo: PASS\n");
    return 0;
}
