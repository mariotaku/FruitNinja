#ifndef FN_GAME_SPECIFIC_ORDER_H
#define FN_GAME_SPECIFIC_ORDER_H

// Analysed: 2026-05-03T00:00
// SpecificOrder — pattern matcher for SPECIFIC_ORDER achievements.
// Spec format: comma-separated tokens; a "(...)" group holds comma-separated
// alternatives for that one slot (the SAME separator as top level, only
// disambiguated by paren nesting -- binary has no '|' alternation delimiter).
//   "apple,orange"        -> slot[0]={apple}, slot[1]={orange}
//   "apple,(orange,lime)" -> slot[0]={apple}, slot[1]={orange,lime}
// Parses into up to 10 sequence slots, each accepting up to 10 alternative
// fruit hashes. Binary @ v1.6.1 SpecificOrder::SpecificOrder @0x00116efc
// (C2 base-object ctor; identical body at C1 @0x001171c8). sizeof=0x1C0 = 448 bytes.

#include <cstdint>

// Binary @ v1.6.1 SpecificOrder::SpecificOrder @0x00116efc. sizeof=0x1C0.
// Pattern matcher for SPECIFIC_ORDER achievements (consume fruits in a
// declared order). Up to 10 sequence slots; each slot can match any of up
// to 10 alternative fruit hashes.
class SpecificOrder {
public:
    explicit SpecificOrder(const char* spec);   // v1.6.1 @0x00116efc
    ~SpecificOrder() {}

    // Binary @ 0x0010846c
    // Returns 1 if the entire sequence has been completed, 0 otherwise.
    // On match of current slot, advances to next slot (or returns 1 on last).
    // On mismatch, retries slot 0; resets to 0 if still no match.
    int Check(uint32_t newFruitHash);

    // v1.6.1 SpecificOrder::GetFirstFruitTypeHash @0x00116d18: ldr r0,[r0,#8]; bx lr -- unconditional return of m_Slots[0].hashes[0].
    uint32_t GetFirstFruitTypeHash() const;

private:
    // Binary layout: sizeof = 0x1C0 = 448.
    // +0x000: m_CurrentSlot (4 bytes) — note: decompiler shows this overlapping slot[0],
    //         but standalone it fits as: 4 + (44*10) + 4 = 448.
    int  m_CurrentSlot;    // +0x000: index into m_Slots for current expected position

    struct Slot {
        int      count;        // number of valid hashes in this slot
        uint32_t hashes[10];   // alternative fruit hashes
    } m_Slots[10];             // +0x004..+0x1BB (44 bytes * 10 = 440)

    int  m_SlotCount;          // +0x1BC: total number of slots parsed from spec
};

#ifdef __bada__
#include <cstddef>
static_assert(sizeof(SpecificOrder) == 0x1c0, "SpecificOrder size mismatch"); // v1.6.1 AchievementManager::LoadAchievementInfo @0x00118728 -- operator new(0x1c0) sizes SpecificOrder
#endif

#endif // FN_GAME_SPECIFIC_ORDER_H
