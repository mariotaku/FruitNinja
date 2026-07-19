// Port specific: Wii boot-splash. See SplashBootScreen.h.
//
// Reproduces StartupEffects.cpp DrawStartFade()'s FRAME-0 appearance (the
// state at splashFadeTimer's init value 1.5, i.e. t>0.5 branch with
// rgb_factor clamped to 1.0):
//   rgb_factor = 1.0, bright = 1.0, alpha_factor = (1-1)^2 + 1 = 1.0
//   col   = Colour(255,255,255,255)  -- logo tint: opaque white
//   bgCol = Colour(0,0,0,255)        -- backdrop: opaque black
//   logo quad scale = FN_SCREEN_W*alpha_factor x FN_SCREEN_H*alpha_factor
//                    = 480 x 320 (alpha_factor==1 -> no "explode" yet)
//   UV crop = (uMin=0.03125, uMax=0.96875, vMin=0.1875, vMax=0.8125)
// (see src/game/StartupEffects.cpp lines 51-117). Backdrop is the full
// non-widescreen 480x320 box (Layout::HalfWidth() is host/web-only --
// Wii never opts into widescreen, see Layout.cpp's `#ifndef __bada__` /
// "whole TU is inert" note -- so bgHalfWidth is the faithful 240).
//
// Quad geometry mirrors Renderer::DrawQuad's unit quad (-0.5..0.5) scaled by
// the world matrix, with the same vertex/UV correspondence (BL=(uMin,vMax),
// BR=(uMax,vMax), TL=(uMin,vMin), TR=(uMax,vMin)) -- see Renderer.cpp
// DrawQuad. Drawn as a GX_TRIANGLESTRIP in TL,BL,TR,BR order (same
// triangle winding a strip over that vertex layout produces).
//
// --- Compressed-transient asset model ---
// The UV crop above is baked into the embedded asset at BUILD time instead
// of being applied at draw time: tools/wii/make-splash-blob.py crops the
// original 1024x1024 source to the 960x640 visible window, downscales 2x2
// box-average to display-native 480x320, RGB5A3-tiles it, then raw-DEFLATEs
// the tiled payload, emitting the result as a generated C byte array (see
// that script + src/platform/wii/CMakeLists.txt for the full pipeline --
// plain C rather than a .s + .incbin TU because the devkitPPC CMake
// toolchain wrapper doesn't set up CMake's ASM language). Only that
// compressed blob (tens of KB) lives in .rodata for the whole process.
// PrepareSplashBoot() inflates it into a transient buffer, uploads it into a
// retained GXTexObj, and keeps that buffer alive (unlike the old
// single-shot boot-only version) so DrawSplashBootQuad() can redraw it on
// every frame during the Splash->Game state-transition gap; ReleaseSplashBoot()
// frees it once the game's own DrawStartFade() has taken over. Peak
// transient usage is one 480*320*2 = 307,200-byte RGB5A3 texture. Because
// the pre-crop is baked in, the runtime quad samples the FULL texture UV
// range (0,0)-(1,1), not a sub-rect.
#ifdef FRUIT_PLATFORM_WII

#include "SplashBootScreen.h"

#include <gccore.h>
#include <malloc.h>
#include <cstring>

#include "debug/Logger.h"
#include "platform/wii/WiiVideo.h"
#include "puff.h"

// Embedded via the generated hb_logo_splash_blob.cpp (see
// src/platform/wii/CMakeLists.txt / tools/wii/make-splash-blob.py).
// Raw-DEFLATE compressed; no container header -- width/height/format below
// are compile-time constants matching what make-splash-blob.py produced.
extern "C" const unsigned char hb_logo_gxtx_z[];
extern "C" const unsigned int hb_logo_gxtx_z_len;

namespace fn {
namespace wii {

namespace {

const u16 kSplashW = 480;
const u16 kSplashH = 320;
const u8 kSplashGxFmt = GX_TF_RGB5A3;
// RGB5A3 = 2 bytes/texel; 480 and 320 are both multiples of 4 so this equals
// GX_GetTexBufferSize(kSplashW, kSplashH, kSplashGxFmt, GX_FALSE, 0) with no
// tile padding.
const unsigned long kSplashTiledSize = (unsigned long)kSplashW * kSplashH * 2;

// Retained across PrepareSplashBoot()/DrawSplashBootQuad()/ReleaseSplashBoot()
// calls -- see file header "Compressed-transient asset model".
void*    s_texBuf   = 0;
GXTexObj s_tex;
bool     s_prepared = false;
bool     s_released = false;

} // namespace

bool PrepareSplashBoot() {
    const unsigned char* blobZ = hb_logo_gxtx_z;
    const unsigned long blobZSize = (unsigned long)hb_logo_gxtx_z_len;
    if (blobZSize == 0) {
        LOG_WARN("SplashBootScreen", "hb_logo_gxtx_z: empty embedded blob -- skipping boot splash");
        return false;
    }

    s_texBuf = memalign(32, kSplashTiledSize);
    if (!s_texBuf) {
        LOG_WARN("SplashBootScreen", "hb_logo_gxtx_z: memalign(%lu) failed -- skipping boot splash",
                 kSplashTiledSize);
        return false;
    }

    unsigned long destLen = kSplashTiledSize;
    unsigned long srcLen = blobZSize;
    int puffErr = puff((unsigned char*)s_texBuf, &destLen, blobZ, &srcLen);
    if (puffErr != 0 || destLen != kSplashTiledSize) {
        LOG_WARN("SplashBootScreen", "hb_logo_gxtx_z: puff() failed (err=%d, got %lu/%lu bytes) -- skipping boot splash",
                 puffErr, destLen, kSplashTiledSize);
        free(s_texBuf);
        s_texBuf = 0;
        return false;
    }

    // CPU wrote the inflated bytes; GX's texture fetch reads from RAM, so
    // flush the CPU cache before the GP can see them.
    DCFlushRange(s_texBuf, kSplashTiledSize);
    GX_InvalidateTexAll();

    GX_InitTexObj(&s_tex, s_texBuf, kSplashW, kSplashH, kSplashGxFmt, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&s_tex, GX_LINEAR, GX_LINEAR);

    s_prepared = true;
    s_released = false;
    return true;
}

void DrawSplashBootQuad() {
    if (!s_prepared || s_released) {
        return;
    }

    // Backdrop for the caller's GX_CopyDisp: opaque black, matching
    // DrawStartFade's frame-0 bgCol (see file header). This function's own
    // backdrop quad below covers the full EFB anyway, but set the copy-clear
    // explicitly rather than relying on leftover state.
    GXColor blackClear = { 0, 0, 0, 255 };
    GX_SetCopyClear(blackClear, GX_MAX_Z24);

    // --- GX draw state: this function is fully self-contained -- it may run
    // before RendererGX::init()/InitGL() ever executes (at boot, called
    // right after WiiGxInit()) OR after it (bridging frames from
    // DisplayManagerWii::SwapBuffers), so it must not assume any state
    // beyond what WiiGxInit() itself sets (FIFO, EFB dimensions/copy-src-dst,
    // pixel format, one bootstrap GX_CopyDisp). Re-derive viewport/scissor
    // from the real video mode here rather than trusting any leftover state
    // (caller's or WiiGxInit's) to still be current by the time this runs.
    GXRModeObj* rmode = (GXRModeObj*)fn::wii::VideoMode();
    if (rmode) {
        GX_SetViewport(0.0f, 0.0f, (f32)rmode->fbWidth, (f32)rmode->efbHeight, 0.0f, 1.0f);
        GX_SetScissor(0, 0, (u32)rmode->fbWidth, (u32)rmode->efbHeight);
    }

    // identity ortho projection over a [-240,240]x[-160,160]
    // world (matches FN_SCREEN_W=480 / FN_SCREEN_H=320 centered convention),
    // one textured TEV stage (GL_MODULATE-equivalent -- tint * texel), opaque
    // (col.a==255 for both quads at frame-0, so blend can stay off, matching
    // DrawQuad's tint.a==255 fast path), no depth test (this is a single
    // presented frame with nothing behind it), no culling.
    Mtx44 proj;
    memset(proj, 0, sizeof(proj));
    // Orthographic: x' = x/240, y' = y/160 (clip space), z passthrough scaled
    // into GX's [-1,0] clip range like SetupGxDrawState's proj below.
    proj[0][0] = 1.0f / 240.0f;
    proj[1][1] = 1.0f / 160.0f;
    proj[2][2] = 0.5f; proj[2][3] = -0.5f;
    proj[3][3] = 1.0f;
    GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);

    Mtx mv;
    guMtxIdentity(mv);
    GX_LoadPosMtxImm(mv, GX_PNMTX0);

    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    GX_SetAlphaUpdate(GX_TRUE);
    GX_SetColorUpdate(GX_TRUE);

    // Descriptor declaration order AND per-vertex emission order below must
    // both follow GX's fixed hardware attribute order (POS, CLR0, TEX0) --
    // see RendererGX.cpp's SetupGxVertexAndTev/EmitInterleavedVertex, the
    // proven-working reference for this convention. GX_SetVtxDesc doesn't
    // reorder the FIFO parse; emitting attributes out of that order
    // misaligns the GP FIFO and the draw rasterizes nothing (or corrupts
    // state), regardless of which order they were declared in.
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);

    // --- Backdrop quad: opaque black, full 480x320 box, untextured (PASSCLR).
    GX_SetNumTexGens(0);
    GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);

    GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, 4);
        GX_Position3f32(-240.0f,  160.0f, 0.0f); GX_Color4u8(0, 0, 0, 255); GX_TexCoord2f32(0.0f, 0.0f); // TL
        GX_Position3f32(-240.0f, -160.0f, 0.0f); GX_Color4u8(0, 0, 0, 255); GX_TexCoord2f32(0.0f, 0.0f); // BL
        GX_Position3f32( 240.0f,  160.0f, 0.0f); GX_Color4u8(0, 0, 0, 255); GX_TexCoord2f32(0.0f, 0.0f); // TR
        GX_Position3f32( 240.0f, -160.0f, 0.0f); GX_Color4u8(0, 0, 0, 255); GX_TexCoord2f32(0.0f, 0.0f); // BR
    GX_End();

    // --- Logo quad: opaque white tint, FULL texture UV (crop already baked
    // into the embedded asset at build time -- see file header), textured
    // (GL_MODULATE-equiv).
    GX_LoadTexObj(&s_tex, GX_TEXMAP0);
    GX_SetNumTexGens(1);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);

    GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, 4);
        GX_Position3f32(-240.0f,  160.0f, 0.0f); GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(0.0f, 0.0f); // TL
        GX_Position3f32(-240.0f, -160.0f, 0.0f); GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(0.0f, 1.0f); // BL
        GX_Position3f32( 240.0f,  160.0f, 0.0f); GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(1.0f, 0.0f); // TR
        GX_Position3f32( 240.0f, -160.0f, 0.0f); GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(1.0f, 1.0f); // BR
    GX_End();

    // Force Z-write ON before the caller's GX_CopyDisp: matches
    // DisplayManagerWii::SwapBuffers' same fix -- GX_CopyDisp's implicit EFB
    // clear-to-GX_SetCopyClear-colour is gated by the last GX_SetZMode's
    // write-enable bit, and this function leaves it OFF above (for its own
    // untested/no-depth quad draws). Leaving write-off going into the copy
    // risks the same silent no-op clear that caused the tumbling-menu-fruit
    // black-center bug. GX_ALWAYS/test-irrelevant, only the write-enable bit
    // matters for the copy-clear.
    GX_SetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
}

void ReleaseSplashBoot() {
    if (s_released) {
        return;
    }
    s_released = true;
    s_prepared = false;
    if (s_texBuf) {
        // GX_DrawDone() blocks until the GP has finished draining the FIFO,
        // which guarantees any texture read of s_texBuf that a preceding
        // DrawSplashBootQuad() issued has already happened, so it is safe to
        // free the buffer right after.
        GX_DrawDone();
        free(s_texBuf);
        s_texBuf = 0;
    }
}

} // namespace wii
} // namespace fn

#endif // FRUIT_PLATFORM_WII
