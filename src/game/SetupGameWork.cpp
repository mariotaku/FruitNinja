// Analysed: 2026-04-30T00:00
// SetupGameWork -- stub for binary 0x0010b4e8.
// TODO: implement -- 16 field stores + AddToTotal("plays_total", +1) +
//   copy saveData+0x110 into Game+0x30.
//   See docs/engine/initialisation-asm-audit.md Section 4.

#include "SetupGameWork.h"

void SetupGameWork() {
    // TODO: implement SetupGameWork (0x0010b4e8)
    //   Sets gameMode=2 (Classic default), 15 other field stores,
    //   increments "plays_total" save counter, copies saveData[+0x110] -> Game[+0x30].
}
