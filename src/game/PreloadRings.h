#ifndef FN_GAME_PRELOADRINGS_H
#define FN_GAME_PRELOADRINGS_H

// PreloadRings -- binary @ 0x11c644
// Loads m_RingTex[0..16], m_RingColours[0..12], m_Colour69C, m_TitleColour into game_work.
// Called from GameInitialise (binary @ 0x11d22c).
void PreloadRings();

#endif // FN_GAME_PRELOADRINGS_H
