// Mortar texture-conversion utility free functions.
// Binary: v1.6.1; all canonical (non-PLT) addresses cited below.

#include "asset/TextureConverter.h"
#include "asset/TextureSource.h"  // TextureInfo::ChannelDescription

namespace Mortar {

// ASM-spec v1.6.1 Mortar::COUNT_BITS @0x0026ba14:
// Parallel popcount (Hamming weight). Five-step parallel bit accumulation.
// Step 4 uses mask 0xFFFF00FF on the shifted half per binary decompile (not the
// textbook 0x00FF00FF); the result is equivalent for 32-bit input because no
// byte exceeds 8 after step 3, so the upper 16 bits of (v>>8) cannot overflow.
unsigned int COUNT_BITS(unsigned int x) {
    unsigned int v = x;
    v = ((v >> 1) & 0x55555555u) + (v & 0x55555555u);
    v = ((v >> 2) & 0x33333333u) + (v & 0x33333333u);
    v = ((v >> 4) & 0x0F0F0F0Fu) + (v & 0x0F0F0F0Fu);
    v = ((v >> 8) & 0xFFFF00FFu) + (v & 0x00FF00FFu);
    return (v >> 16) + (v & 0x0000FFFFu);
}

// ASM-spec v1.6.1 Mortar::CorrectHalfTexel @0x0025805c:
// VFP params: s0=halfTexel, r0=&u, r1=&v.
// Shrinks or expands the [u,v] UV range by halfTexel on each end.
void CorrectHalfTexel(float halfTexel, float& u, float& v) {
    if (u < v) {
        u += halfTexel;
        v -= halfTexel;
    } else {
        u -= halfTexel;
        v += halfTexel;
    }
}

// ASM-spec v1.6.1 Mortar::MultiplyColorComponent @0x0024b0b0:
// PLT thunk to this canonical copy: @0x0010a73c.
// ARM VFP path: converts both bytes to float, divides each by 255, multiplies,
// multiplies by 255, converts back via vcvt.u32.f32 (saturates negatives to 0),
// then masks to 8 bits.
// Integer (a*b+127)/255 must NOT be substituted -- float precision path required.
unsigned char MultiplyColorComponent(unsigned char a, unsigned char b) {
    float result = ((float)a / 255.0f) * ((float)b / 255.0f) * 255.0f;
    return (unsigned char)((unsigned int)result & 0xFFu);
}

// ASM-spec v1.6.1 Mortar::GetConversionChannelRank @0x0026b75c:
// Computes a conversion cost for mapping src channel type/depth to dst.
// typeRank encodes type compatibility (0 = same type or wildcard dst, large = incompatible).
// Bit-depth penalty: 1 << (3 + |src.m_Bits - dst.m_Bits| + upscale_penalty).
// upscale_penalty = 1 when dst has more bits than src (upscaling costs extra).
int GetConversionChannelRank(const TextureInfo::ChannelDescription& src,
                             const TextureInfo::ChannelDescription& dst) {
    uint8_t srcType = src.m_TypeFlag & 0x7fu;
    uint8_t dstType = dst.m_TypeFlag & 0x7fu;
    unsigned int typeRank;

    if (srcType == dstType) {
        typeRank = 0;
    } else {
        switch (dstType) {
        case 0:
            return 0;
        case 1:
            typeRank = 0x8000000u;
            break;
        case 2:
        case 3:
        case 4:
            typeRank = (srcType == 5) ? 0x20000u : 0x8000000u;
            break;
        case 5:
            // srcType in {2,3,4}: (srcType - 2) < 3 as unsigned
            typeRank = ((unsigned int)(srcType - 2) < 3u) ? 0x20000u : 0x8000000u;
            break;
        default:
            typeRank = 0;
            break;
        }
    }

    int bitDiff = (int)src.m_Bits - (int)dst.m_Bits;
    if (bitDiff < 0) bitDiff = -bitDiff;
    int upscale = (src.m_Bits < dst.m_Bits) ? 1 : 0;
    return (int)(typeRank + (1u << (3 + bitDiff + upscale)));
}

}  // namespace Mortar
