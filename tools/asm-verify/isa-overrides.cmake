# Per-TU ISA overrides for the asm-verify cross-build.  SINGLE SOURCE OF TRUTH.
#
# Consumed by BOTH:
#   cross-build/CMakeLists.txt  -- applies the COMPILE_FLAGS
#   detect-isa-mismatch.py      -- knows the port-side ISA per TU, so a TU that
#                                  IS mode-matched is not falsely tagged
#                                  `isa-mismatch`.
# Keep them in sync by editing only this file.
#
# WHY PER-TU. The binary is mixed-ISA (7951 `$a` ARM vs 486 `$t` Thumb mapping
# symbols; `___<sym>_veneer` ARM thunks do the interworking). GCC 4.4.1 has NO
# per-function mode selection -- `__attribute__((target("thumb")))` and
# `#pragma GCC target` both emit "target attribute is not supported on this
# machine" and have zero effect -- so the TU is the finest granularity available.
#
# THE RULE FOR ADDING A FILE HERE. Only list a TU whose paired rows are
# HOMOGENEOUS in the binary. `detect-isa-mismatch.py` computes this and prints
# `FLIP-ELIGIBLE` / `MIXED` per TU; a MIXED TU must NOT be listed, because
# flipping it trades its ARM rows for its Thumb ones and manufactures new
# phantom divergences. Re-check after any change that alters pairing.
#
# Rows in a TU that cannot be flipped stay excluded, tagged `isa-mismatch`.
# A correctly-labelled exclusion is the acceptable outcome; a half-working
# flip is not.

# ---- Compile as ARM (-marm) --------------------------------------------------
# Only meaningful when the cross-build default is not already ARM (it is, so
# these are documentation + a guard for a future default change).
#
# ColAABB.cpp -- ARM in the binary (confirmed 2026-06-18: ColAABB::UpdateVertices
#   @ 0x00275ccc and ColAABBAABB @ 0x00275d60 both end with the ARM `bx lr`
#   encoding e12fff1e).
set(FN_ISA_ARM_SOURCES
    src/engine/collision/ColAABB.cpp
)

# ---- Compile as Thumb-2 (-mthumb) -------------------------------------------
# FontInterface.cpp -- the WHOLE Mortar::FontInterface TU is Thumb-2 in the
#   binary: all 21 real bodies from FontInterface::GetNewFontUID @0x00240020
#   through AddStringRef @0x0024050c carry the Thumb low bit, and the only ARM
#   symbols bearing the name are the 7 `___..._veneer` interworking thunks
#   (not bodies). 5 paired rows, 0 ARM -> homogeneous, flip is free.
set(FN_ISA_THUMB_SOURCES
    src/engine/render/FontInterface.cpp
)

# NOT listed, deliberately:
#   src/engine/core/SystemManager.cpp -- MIXED. Its 7 SystemManager:: bodies are
#     Thumb, but the 12 free version/config helpers the port keeps in the same
#     file (GetVersionMajor, LowResBackgrounds, GetApparentWindowWidth, ...) are
#     ARM in the binary, plus Mortar::Event2<int,int>::~Event2. Flipping would
#     trade 13 good ARM rows for 7 Thumb ones. Those 7 stay tagged
#     `isa-mismatch`. Splitting the free functions into their own TU would make
#     it flip-eligible, but that is a src/ change, not a tools/ one.
