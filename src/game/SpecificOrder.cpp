// Analysed: 2026-05-03T00:00
// SpecificOrder — SPECIFIC_ORDER achievement sequence matcher.
// Binary @ v1.6.1 SpecificOrder::SpecificOrder @0x00116efc (ctor; C1 twin
// @0x001171c8, identical body) / 0x0010846c (Check) / 0x00108468 (GetFirstFruitTypeHash).

#include "SpecificOrder.h"
#include "engine/util/StringHash.h"
#include <cstring>
#include <cstdio>

// v1.6.1 SpecificOrder::SpecificOrder @0x00116efc
// Parses spec string into up to 10 slots x up to 10 hashes/slot.
// Format: comma-separated tokens; a "(...)" group holds comma-separated
// alternatives for that one slot -- same separator as top level, only
// disambiguated by paren nesting (binary has NO '|' alternation delimiter).
//   "apple,orange"        -> slot[0]={apple}, slot[1]={orange}
//   "apple,(orange,lime)" -> slot[0]={apple}, slot[1]={orange,lime}
// Each token is fed through StringHash.
SpecificOrder::SpecificOrder(const char* spec)
    : m_CurrentSlot(0)
    , m_SlotCount(0)
{
    memset(m_Slots, 0, sizeof(m_Slots));

    if (!spec || spec[0] == '\0') return;

    const char* p = spec;
    int slotIdx = 0;

    while (*p != '\0' && slotIdx < 10) {
        // Skip whitespace
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\0') break;

        if (*p == '(') {
            // Parenthesised alternation group: (tokenA,tokenB,...) -- SAME
            // comma separator as top level, disambiguated only by paren nesting.
            ++p;  // skip '('
            int hashIdx = 0;
            while (*p != ')' && *p != '\0' && hashIdx < 10) {
                // Read until ',' or ')'
                const char* start = p;
                while (*p != ',' && *p != ')' && *p != '\0') ++p;
                // Extract token
                char token[64];
                int len = (int)(p - start);
                if (len > 63) len = 63;
                memcpy(token, start, (size_t)len);
                token[len] = '\0';
                if (len > 0) {
                    m_Slots[slotIdx].hashes[hashIdx] = StringHash(token);
                    ++hashIdx;
                }
                if (*p == ',') ++p;  // skip ','
            }
            if (*p == ')') ++p;  // skip ')'
            m_Slots[slotIdx].count = hashIdx;
            ++slotIdx;
        } else {
            // Simple token (no alternation)
            const char* start = p;
            while (*p != ',' && *p != '\0') ++p;
            char token[64];
            int len = (int)(p - start);
            if (len > 63) len = 63;
            memcpy(token, start, (size_t)len);
            token[len] = '\0';
            if (len > 0) {
                m_Slots[slotIdx].hashes[0] = StringHash(token);
                m_Slots[slotIdx].count = 1;
                ++slotIdx;
            }
        }

        // Skip comma separator
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == ',') ++p;
    }

    m_SlotCount = slotIdx;
}

// v1.6.1 SpecificOrder::Check @0x00116d20
// Returns 1 if the entire sequence has been completed after this call.
// Returns 0 otherwise (including on mismatch/reset).
// Logic:
//   1. Try matching newFruitHash against current slot's hashes.
//   2. On match: advance slot; if last slot was matched, reset and return 1.
//   3. On mismatch: if we had advanced past slot 0, retry slot 0 with newFruitHash.
//   4. If slot 0 also doesn't match (or we were already at slot 0), reset to slot 0.
int SpecificOrder::Check(uint32_t newFruitHash) {
    if (m_SlotCount <= 0) return 0;

    // Try current slot
    const Slot& cur = m_Slots[m_CurrentSlot];
    bool matched = false;
    for (int i = 0; i < cur.count; ++i) {
        if (cur.hashes[i] == newFruitHash) {
            matched = true;
            break;
        }
    }

    if (matched) {
        int next = m_CurrentSlot + 1;
        if (next >= m_SlotCount) {
            // Completed the sequence
            m_CurrentSlot = 0;
            return 1;
        }
        m_CurrentSlot = next;
        return 0;
    }

    // Mismatch: if not at slot 0, retry slot 0 with this hash
    if (m_CurrentSlot != 0) {
        const Slot& slot0 = m_Slots[0];
        bool matchedSlot0 = false;
        for (int i = 0; i < slot0.count; ++i) {
            if (slot0.hashes[i] == newFruitHash) {
                matchedSlot0 = true;
                break;
            }
        }
        if (matchedSlot0) {
            // Start sequence from slot 1 (slot 0 was just matched)
            m_CurrentSlot = (m_SlotCount > 1) ? 1 : 0;
            return 0;
        }
    }

    // Reset
    m_CurrentSlot = 0;
    return 0;
}

// v1.6.1 SpecificOrder::GetFirstFruitTypeHash @0x00116d18: ldr r0,[r0,#8]; bx lr -- unconditional return of m_Slots[0].hashes[0].
uint32_t SpecificOrder::GetFirstFruitTypeHash() const {
    return m_Slots[0].hashes[0];
}
