// Minimal unit test for Mortar texture-conversion utility functions.
// Covers COUNT_BITS, MultiplyColorComponent, CorrectHalfTexel, and
// GetConversionChannelRank with a small set of known-good values.
//
// Pure CPU: no GPU, no SDL, no audio, no file I/O.
// Cross-build safe: no lambdas, no auto, no range-for, no enum class.

#include "asset/TextureConverter.h"
#include "asset/TextureSource.h"   // TextureInfo::ChannelDescription

#include <cstdio>
#include <cassert>
#include <cmath>

// ---- COUNT_BITS (popcount) --------------------------------------------------

static void test_count_bits() {
    assert(Mortar::COUNT_BITS(0u)           ==  0);
    assert(Mortar::COUNT_BITS(1u)           ==  1);
    assert(Mortar::COUNT_BITS(3u)           ==  2);
    assert(Mortar::COUNT_BITS(0xFu)         ==  4);
    assert(Mortar::COUNT_BITS(0xFFu)        ==  8);
    assert(Mortar::COUNT_BITS(0xFFFFu)      == 16);
    assert(Mortar::COUNT_BITS(0xFFFFFFFFu)  == 32);
    assert(Mortar::COUNT_BITS(0xAAAAAAAAu)  == 16);
    assert(Mortar::COUNT_BITS(0x55555555u)  == 16);
    assert(Mortar::COUNT_BITS(0x80000001u)  ==  2);
    printf("COUNT_BITS: OK\n");
}

// ---- MultiplyColorComponent -------------------------------------------------

static void test_multiply_color_component() {
    // Zero operand always gives 0
    unsigned int i;
    for (i = 0; i <= 255; ++i) {
        assert(Mortar::MultiplyColorComponent(0, (unsigned char)i) == 0);
        assert(Mortar::MultiplyColorComponent((unsigned char)i, 0) == 0);
    }
    // Identity: full-scale preserves other operand
    assert(Mortar::MultiplyColorComponent(255, 255) == 255);
    assert(Mortar::MultiplyColorComponent(255, 128) == 128);
    assert(Mortar::MultiplyColorComponent(128, 255) == 128);
    // Half-scale: 128/255 * 128/255 * 255 ~= 64
    // Float result: (128/255.0) * (128/255.0) * 255 = 64.251... -> 64
    assert(Mortar::MultiplyColorComponent(128, 128) == 64);
    printf("MultiplyColorComponent: OK\n");
}

// ---- CorrectHalfTexel -------------------------------------------------------

static void test_correct_half_texel() {
    float u, v;
    const float half = 0.5f;

    // u < v: inset (shrink range)
    u = 0.0f; v = 1.0f;
    Mortar::CorrectHalfTexel(half, u, v);
    assert(u ==  0.5f && v ==  0.5f);

    // u > v: outset (expand reversed range)
    u = 1.0f; v = 0.0f;
    Mortar::CorrectHalfTexel(half, u, v);
    assert(u ==  0.5f && v ==  0.5f);

    // u == v: else branch (u >= v)
    u = 0.5f; v = 0.5f;
    Mortar::CorrectHalfTexel(0.1f, u, v);
    // u -= 0.1, v += 0.1
    assert(fabsf(u - 0.4f) < 1e-6f && fabsf(v - 0.6f) < 1e-6f);

    printf("CorrectHalfTexel: OK\n");
}

// ---- GetConversionChannelRank -----------------------------------------------

static void test_get_conversion_channel_rank() {
    TextureInfo::ChannelDescription src, dst;

    // Same type, same bits -> typeRank=0, bitDiff=0, upscale=0 -> 1<<3 = 8
    src.m_TypeFlag = 1; src.m_Bits = 8;
    dst.m_TypeFlag = 1; dst.m_Bits = 8;
    assert(Mortar::GetConversionChannelRank(src, dst) == 8);

    // Same type, dst has 1 more bit (upscale by 1) -> bitDiff=1, upscale=1 -> 1<<(3+1+1)=32
    src.m_TypeFlag = 2; src.m_Bits = 4;
    dst.m_TypeFlag = 2; dst.m_Bits = 5;
    assert(Mortar::GetConversionChannelRank(src, dst) == 32);

    // Same type, src has 1 more bit (downscale by 1) -> bitDiff=1, upscale=0 -> 1<<(3+1+0)=16
    src.m_TypeFlag = 2; src.m_Bits = 5;
    dst.m_TypeFlag = 2; dst.m_Bits = 4;
    assert(Mortar::GetConversionChannelRank(src, dst) == 16);

    // dstType=0 (wildcard) -> early return 0
    src.m_TypeFlag = 3; src.m_Bits = 8;
    dst.m_TypeFlag = 0; dst.m_Bits = 8;
    assert(Mortar::GetConversionChannelRank(src, dst) == 0);

    // dstType=1 (incompatible) -> typeRank=0x8000000, bitDiff=0, upscale=0 -> 0x8000000+8
    src.m_TypeFlag = 2; src.m_Bits = 8;
    dst.m_TypeFlag = 1; dst.m_Bits = 8;
    assert(Mortar::GetConversionChannelRank(src, dst) == (int)(0x8000000u + 8u));

    printf("GetConversionChannelRank: OK\n");
}

int main() {
    test_count_bits();
    test_multiply_color_component();
    test_correct_half_texel();
    test_get_conversion_channel_rank();
    printf("All texture_converter tests passed.\n");
    return 0;
}
