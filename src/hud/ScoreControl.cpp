// Analysed: 2026-04-30T00:00

#include "ScoreControl.h"
#include <cstring>

// ScoreControl ctor @ 0x00158c7c
// Stub: zero-fills subclass fields, defers all real content loading.
ScoreControl::ScoreControl() {
    std::memset(m_fields, 0, sizeof(m_fields));
}

// dtor @ 0x00158394 / 0x00158418 / 0x00158494
ScoreControl::~ScoreControl() {}
