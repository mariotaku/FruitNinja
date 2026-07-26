// Analysed: 2026-04-29T00:00
#include "render/Font.h"
#include "render/FontTTFRegistry.h"
#include "render/FontCacheObjectTTF.h"
#include "render/FontInterface.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/MatrixStack.h"
#include "render/gl_funcs.h"
#include "asset/File.h"
#include "asset/TextureManager.h"
#include "math/_Vector3.h"
#include "math/Matrix44.h"
#include "debug/Logger.h"
#if !defined(__bada__) && !defined(FN_GL_STUB)
#  include "debug/DebugFlags.h"
#endif
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cmath>
#include <new>

#ifdef _MSC_VER
#  define FN_STRNICMP _strnicmp
#else
#  define FN_STRNICMP strncasecmp
#endif

namespace Mortar {

// ---------------------------------------------------------------------------
// Font ctor / dtor
// ---------------------------------------------------------------------------

Font::Font()
    : m_Glyphs(nullptr)
    , m_GlyphCount(0)
    , m_Pages(nullptr)
    , m_PageCount(0)
    , m_Kernings(nullptr)
    , m_KerningCount(0)
    , m_RefCount(0)
    , m_ScaleW(256)
    , m_ScaleH(256)
    , m_LineHeight(1.0f)
    , m_BaseNorm(0.0f)
{
    memset(m_GlyphLookup, 0, sizeof(m_GlyphLookup));
}

Font::~Font() {
#ifndef __bada__
    // Unregister any TTF face from the side-table (port specific: TTF state
    // lives outside Font's binary-layout struct to keep sizeof == 0x438).
    FontTTFRegistry::GetInstance().Unregister(this);
#endif

    // DIFFERS: v1.6.1 binary @ 0x0024d818 free order is (1) delete[] m_Kernings@+0x410,
    // (2) delete[] m_Glyphs@+0x000, (3) ~Page each + operator delete[] on Page
    // array@+0x408, (4) ~vector m_PageVerts@+0x42c. Port order differs but all
    // four frees are independent non-aliasing heap blocks; semantically identical.
    delete[] m_Glyphs;
    m_Glyphs = nullptr;

    if (m_Pages) {
        for (int i = 0; i < m_PageCount; i++) {
            m_Pages[i].~Page();
        }
        // Pages were allocated as raw bytes; free via operator delete[]
        operator delete[](reinterpret_cast<void*>(m_Pages));
        m_Pages = nullptr;
    }

    delete[] m_Kernings;
    m_Kernings = nullptr;

    m_PageVerts.clear();
}

// ---------------------------------------------------------------------------
// Helper: parse an integer value from a key=N token in the .fnt line
// ---------------------------------------------------------------------------

static bool ParseFntInt(const char* p, const char* key, int* out) {
    const char* k = strstr(p, key);
    if (!k) return false;
    k += strlen(key);
    if (*k != '=') return false;
    k++;
    *out = (int)strtol(k, nullptr, 10);
    return true;
}

static bool ParseFntString(const char* p, const char* key, char** outAlloc) {
    const char* k = strstr(p, key);
    if (!k) return false;
    k += strlen(key);
    if (*k != '=') return false;
    k++;
    bool quoted = (*k == '"');
    if (quoted) k++;
    const char* end = k;
    while (*end && (quoted ? *end != '"' : (*end != ' ' && *end != '\t' && *end != '\r' && *end != '\n'))) {
        end++;
    }
    size_t len = (size_t)(end - k);
    char* buf = new char[len + 1];
    memcpy(buf, k, len);
    buf[len] = '\0';
    *outAlloc = buf;
    return true;
}

// ---------------------------------------------------------------------------
// .fnt binary-stream parser free functions (for asm-verify coverage).
// These are distinct from the static ParseFntInt/ParseFntString helpers above;
// they match the binary's global-linkage symbols used internally by the engine.
// Font::Load continues to use ParseFntInt/ParseFntString directly.
// ---------------------------------------------------------------------------

// ASM-spec v1.6.1 Mortar::Next_Word_Is @0x0024bdf4
bool Next_Word_Is(char* key, const char* word) {
    int n = (int)strlen(word);
    int i = 0;
    while ((unsigned char)key[i] == (unsigned char)word[i] && (unsigned char)key[i] != 0) {
        if ((unsigned char)key[i] == 0x20) break;  // space in key: stop
        if (i >= n) break;
        i++;
    }
    return n <= i;
}

// ASM-spec v1.6.1 Mortar::Get_Next_Value @0x0024bb54
// Sentinel value -0xaabe (-43710) used by callers (Parse_Char, Parse_Kerning) to
// detect whether intOut was populated. Callers initialise *intOut = -0xaabe and
// check != -0xaabe after the call before writing to the struct field.
int Get_Next_Value(char* line, char* keyBuf, int* intOut, char** strHeapOut) {
    // 1. Scan forward to find '=' (0x3d). Stop on CR/LF/NUL.
    int i = 0;
    while (line[i] != '=' && line[i] != '\r' && line[i] != '\n' && line[i] != '\0')
        i++;
    if (line[i] != '=') return -(i + 1);  // no '=' found: EOL

    // 2. Copy key (strip leading+trailing spaces) into keyBuf[0..31].
    //    Note: the spec shows trailing-space strip; leading-space strip is required
    //    for subsequent calls where the cursor is positioned at whitespace between pairs.
    int keyStart = 0;
    while (keyStart < i && (unsigned char)line[keyStart] == 0x20) keyStart++;
    int keyEnd = i;
    while (keyEnd > keyStart && (unsigned char)line[keyEnd - 1] == 0x20) keyEnd--;
    snprintf(keyBuf, 32, "%.*s", keyEnd - keyStart, line + keyStart);

    // 3. Consume value after '='.
    int j = i + 1;  // skip '='
    if (line[j] == '\r' || line[j] == '\n' || line[j] == '\0') return -(j);

    bool negative = false;
    if (line[j] == '-') { negative = true; j++; }
    if (line[j] == '\r' || line[j] == '\n' || line[j] == '\0') return -(j);

    if (line[j] == '"') {
        // Quoted string value (e.g. file="foo.tga").
        // Binary stops at '"' or '.' so the extension is excluded from the result.
        // ASM-spec: scan uses _OS_snprintf to copy into heap buffer (operator new[]).
        j++;  // skip opening '"'
        int valStart = j;
        while (line[j] && line[j] != '"' && line[j] != '.') j++;
        int strLen = j - valStart;
        char* buf = new char[strLen + 1];
        snprintf(buf, strLen + 1, "%.*s", strLen, line + valStart);
        *strHeapOut = buf;
        // j now points to '"' or '.' (not consumed; next call will scan past it)
    } else {
        // Integer value: scan digit run, build value via power-of-ten.
        int valStart = j;
        int valEnd = j;
        while (line[valEnd] && line[valEnd] != '\r' && line[valEnd] != '\n'
               && line[valEnd] != ' ' && line[valEnd] != '\0') valEnd++;
        // Validate each char is a decimal digit; return negative on non-digit.
        int acc = 0, mul = 1;
        for (int k = valEnd - 1; k >= valStart; k--) {
            unsigned char d = (unsigned char)line[k] - '0';
            if (d > 9) return -(valEnd);  // non-digit: error
            acc += (int)d * mul;
            mul *= 10;
        }
        *intOut = negative ? -acc : acc;
        j = valEnd;
        // Skip trailing whitespace so the next call's cursor is at the next key.
        while (line[j] == ' ') j++;
    }

    // 4. Return bytes consumed (to advance line cursor).
    if (line[j] == '\r' || line[j] == '\n' || line[j] == '\0') return -(j + 1);
    return j;  // positive: more pairs remain on this line
}

// ASM-spec v1.6.1 Mortar::Parse_Char @0x0024be44
// Priority order for key matching (longer names before shorter prefixes):
//   id -> xadvance -> xoffset -> x -> yoffset -> y -> width -> height -> page
// Stores RAW pixel values as floats; caller normalises by scaleW/H and lineHeight.
int Parse_Char(char* line, CharTemplate* out, int lineLen) {
    int pos = 0;
    while (pos < lineLen) {
        char keyBuf[32];
        int intVal = -0xaabe;     // sentinel: "not set"
        char* strHeapPtr = NULL;
        int consumed = Get_Next_Value(line + pos, keyBuf, &intVal, &strHeapPtr);

        if (Next_Word_Is(keyBuf, "id") && intVal != -0xaabe) {
            *(short*)((char*)out + 0x00) = (short)intVal;
        } else if (Next_Word_Is(keyBuf, "xadvance") && intVal != -0xaabe) {
            *(float*)((char*)out + 0x1c) = (float)intVal;
        } else if (Next_Word_Is(keyBuf, "xoffset") && intVal != -0xaabe) {
            *(float*)((char*)out + 0x14) = (float)intVal;
        } else if (Next_Word_Is(keyBuf, "x") && intVal != -0xaabe) {
            *(float*)((char*)out + 0x04) = (float)intVal;
        } else if (Next_Word_Is(keyBuf, "yoffset") && intVal != -0xaabe) {
            *(float*)((char*)out + 0x18) = (float)intVal;
        } else if (Next_Word_Is(keyBuf, "y") && intVal != -0xaabe) {
            *(float*)((char*)out + 0x08) = (float)intVal;
        } else if (Next_Word_Is(keyBuf, "width") && intVal != -0xaabe) {
            *(float*)((char*)out + 0x0c) = (float)intVal;
        } else if (Next_Word_Is(keyBuf, "height") && intVal != -0xaabe) {
            *(float*)((char*)out + 0x10) = (float)intVal;
        } else if (Next_Word_Is(keyBuf, "page") && intVal != -0xaabe) {
            *(unsigned char*)((char*)out + 0x20) = (unsigned char)intVal;
        }
        delete[] strHeapPtr;   // quoted-string path not expected here; free if set

        if (consumed < 0) break;
        pos += consumed;
    }
    return pos;
}

// ASM-spec v1.6.1 Mortar::Parse_Page @0x0024d744
// Zero-inits filename (nullptr) and texture (default SmartPtr) before parsing.
// Only the "file" key is handled; its value is heap-allocated (caller owns).
int Parse_Page(char* line, Page* out, int lineLen) {
    // Binary zero-inits: *(int*)out = 0 (filename=nullptr) and
    // SmartPtr<Texture2D>::SetPtrCast(nullptr) (texture slot = 0).
    out->filename = NULL;
    out->texture = Mortar::SmartPtr<Mortar::Texture>();  // zero the SmartPtr slot

    int pos = 0;
    while (pos < lineLen) {
        char keyBuf[32];
        int intVal = 0;
        char* strHeapPtr = NULL;
        int consumed = Get_Next_Value(line + pos, keyBuf, &intVal, &strHeapPtr);

        if (Next_Word_Is(keyBuf, "file") && strHeapPtr != NULL) {
            // Assign heap-allocated filename (truncated at '.', no extension).
            // Binary: *(char**)out = strHeapPtr. Port: out->filename = strHeapPtr.
            // Page dtor calls delete[] filename, so the caller owns this allocation.
            out->filename = strHeapPtr;
            strHeapPtr = NULL;  // transfer ownership
        }
        delete[] strHeapPtr;   // free if not consumed (other keys)

        if (consumed < 0) break;
        pos += consumed;
    }
    return pos;
}

// ASM-spec v1.6.1 Mortar::Parse_Kerning @0x0024c0b0
// Zeroes all 12 bytes of *out before parsing.
// first/second: stored as int; amount: stored as (float)intVal.
int Parse_Kerning(char* line, Kerning* out, int lineLen) {
    memset(out, 0, sizeof(Kerning));  // binary: zeroes all 12 bytes

    int pos = 0;
    while (pos < lineLen) {
        char keyBuf[32];
        int intVal = -0xaabe;   // sentinel: "not set"
        char* strHeapPtr = NULL;
        int consumed = Get_Next_Value(line + pos, keyBuf, &intVal, &strHeapPtr);

        if (Next_Word_Is(keyBuf, "first") && intVal != -0xaabe) {
            *(int*)((char*)out + 0x00) = intVal;
        } else if (Next_Word_Is(keyBuf, "second") && intVal != -0xaabe) {
            *(int*)((char*)out + 0x04) = intVal;
        } else if (Next_Word_Is(keyBuf, "amount") && intVal != -0xaabe) {
            *(float*)((char*)out + 0x08) = (float)intVal;
        }
        delete[] strHeapPtr;   // not expected on kerning lines; free if set

        if (consumed < 0) break;
        pos += consumed;
    }
    return pos;
}

// ---------------------------------------------------------------------------
// Font::Load -- binary @ 0x00199e9c (instance method, returns int 1/0)
//
// Slurps the entire .fnt file via Mortar::File (IFile-backed), walks
// byte-by-byte comparing tags. Matches the binary's IFile-based slurp
// (binary uses a stack-allocated File; port matches that pattern).
// Port-specific additions: texture-loading path (baseDir stripping,
// extension swap .tga->.tex, TextureManager::GetDataDir). These are
// beyond what the binary's Font::Load does (binary defers texture loading
// to a separate step). TODO: re-verify when the texture-loading path is
// fully RE'd to restore ASM-verify score.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// TTF path: Font::Load routes here when the path ends in .ttf
// Port specific: FreeType-backed dynamic glyph rendering. The binary used the
// Samsung Bada IFont/IGlyphCache API; this port replaces it with FreeType.
// ---------------------------------------------------------------------------

static bool HasTTFExtension(const char* path) {
    if (!path) return false;
    const char* dot = strrchr(path, '.');
    if (!dot) return false;
    return (FN_STRNICMP(dot, ".ttf", 4) == 0 ||
            FN_STRNICMP(dot, ".otf", 4) == 0);
}

int Font::LoadTTF(const char* path) {
    // Port specific: resolve the absolute path via the same FileSystem layer
    // the .fnt path uses (data_dir prefix). We need the real filesystem path
    // for TtfFace::Open (backend's font-file loader), which takes an OS path.
    // Use TextureManager::GetDataDir() to construct the full path.
    const char* dataDir = TextureManager::GetDataDir();
    char fullPath[512] = "";
    if (dataDir && dataDir[0]) {
        size_t ddLen = strlen(dataDir);
        bool needSlash = ddLen > 0 &&
            dataDir[ddLen - 1] != '/' && dataDir[ddLen - 1] != '\\';
        snprintf(fullPath, sizeof(fullPath), "%s%s%s",
                 dataDir, needSlash ? "/" : "", path);
    } else {
        snprintf(fullPath, sizeof(fullPath), "%s", path);
    }

    // Default pixel size for TTF rendering (world-unit scale = pixelSize in
    // the original game's ortho space). The caller's DrawString scale arg is
    // in world units; 32px is a typical menu font size. The TTF render path
    // will re-render at the exact pixel size requested by the caller.
    const int defaultPixelSize = 32;

    FontCacheObjectTTF* ttf = new FontCacheObjectTTF(fullPath, defaultPixelSize);
    if (!ttf->IsValid()) {
        delete ttf;
        LOG_ERROR("FONT/LoadTTF", "failed to load TTF face from '%s'", fullPath);
        return 0;
    }

    // Store a nominal line height so GetLineHeight() works.
    // For TTF, m_LineHeight is the pixel size in world units.
    m_LineHeight = (float)defaultPixelSize;
    m_ScaleW     = 1;
    m_ScaleH     = 1;

    FontTTFRegistry::GetInstance().Register(this, ttf);
    return 1;
}

int Font::Load(const char* path) {
    // Port specific: route .ttf / .otf to the dynamic-TTF path (backend
    // chosen at compile time via FN_TTF_BACKEND -- see TtfBackend.h).
    if (HasTTFExtension(path)) {
        return LoadTTF(path);
    }

    // Binary @ 0x0024d8bc (v1.6.1; stale 0x00199e9c v1.5.x): open via Mortar::File (IFile -> FileSystem_Direct).
    // The path is forwarded straight through; FileSystem_Direct's prefix
    // logic (data_dir prepend or strict) is owned by the FileSystem layer.
    Mortar::File f(path, 0, 0);
    if (!f.Open()) {
        LOG_ERROR("FONT/Load", "failed to open '%s'", path);
        return 0;
    }
    if (!f.Load(nullptr, 0)) {
        LOG_ERROR("FONT/Load", "failed to slurp '%s'", path);
        return 0;
    }
    const unsigned long fsize = f.Size();
    if (fsize == 0) return 0;

    // Copy into a NUL-terminated heap buffer (parser walks `*p`).
    char* buf = new char[(size_t)fsize + 1];
    memcpy(buf, f.Data(), (size_t)fsize);
    buf[fsize] = '\0';

    // Extract base directory from path for fallback texture loads
    const char* lastSlash = nullptr;
    for (const char* s = path; *s; s++) {
        if (*s == '/' || *s == '\\') lastSlash = s;
    }
    char baseDir[512] = "";
    if (lastSlash) {
        size_t dirLen = (size_t)(lastSlash - path) + 1;
        if (dirLen < sizeof(baseDir)) {
            memcpy(baseDir, path, dirLen);
            baseDir[dirLen] = '\0';
        }
    }

    // Temp storage for raw parse values before we know counts
    int lineHeight = 0, base = 0, scaleW = 256, scaleH = 256;
    int pageCountParsed = 0;
    int glyphCountParsed = 0;
    int kerningCountParsed = 0;

    // --- First pass: gather common/counts ---
    const char* p = buf;
    while (*p) {
        if (strncmp(p, "common ", 7) == 0) {
            ParseFntInt(p, "lineHeight", &lineHeight);
            ParseFntInt(p, "base",       &base);
            ParseFntInt(p, "scaleW",     &scaleW);
            ParseFntInt(p, "scaleH",     &scaleH);
            ParseFntInt(p, "pages",      &pageCountParsed);
        } else if (strncmp(p, "chars ", 6) == 0) {
            ParseFntInt(p, "count", &glyphCountParsed);
        } else if (strncmp(p, "kernings ", 9) == 0) {
            ParseFntInt(p, "count", &kerningCountParsed);
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }

    m_ScaleW     = scaleW;
    m_ScaleH     = scaleH;
    m_LineHeight = (float)lineHeight;
    m_BaseNorm   = (float)base;  // will be divided at the end
    m_GlyphCount = glyphCountParsed;
    m_PageCount  = pageCountParsed;
    m_KerningCount = kerningCountParsed;

    // Allocate arrays matching binary alloc pattern
    if (m_GlyphCount > 0) {
        m_Glyphs = new CharTemplate[m_GlyphCount]();
    }
    if (m_PageCount > 0) {
        // Binary: operator_new((N+1)*8) — allocates raw bytes, then in-place-constructs each Page
        void* pagesRaw = operator new[]((size_t)(m_PageCount + 1) * sizeof(Page));
        m_Pages = reinterpret_cast<Page*>(pagesRaw);
        for (int i = 0; i < m_PageCount; i++) {
            new (&m_Pages[i]) Page();
        }
    }
    if (m_KerningCount > 0) {
        m_Kernings = new Kerning[m_KerningCount]();
    }

    const float invLH = (lineHeight > 0) ? (1.0f / (float)lineHeight) : 1.0f;
    const float invW  = (scaleW > 0)     ? (1.0f / (float)scaleW)     : 1.0f;
    const float invH  = (scaleH > 0)     ? (1.0f / (float)scaleH)     : 1.0f;

    int glyphIdx   = 0;
    int kerningIdx = 0;

    // --- Second pass: fill arrays ---
    p = buf;
    while (*p) {
        if (strncmp(p, "page ", 5) == 0) {
            int pageId = -1;
            ParseFntInt(p, "id", &pageId);
            if (pageId >= 0 && pageId < m_PageCount) {
                char* fname = nullptr;
                if (ParseFntString(p, "file", &fname)) {
                    m_Pages[pageId].filename = fname;
                }
            }
        } else if (strncmp(p, "char ", 5) == 0 && glyphIdx < m_GlyphCount) {
            int rawId   = 0, rawX = 0, rawY = 0, rawW = 0, rawH = 0;
            int rawXoff = 0, rawYoff = 0, rawXadv = 0, rawPage = 0;
            ParseFntInt(p, "id",       &rawId);
            ParseFntInt(p, "x",        &rawX);
            ParseFntInt(p, "y",        &rawY);
            ParseFntInt(p, "width",    &rawW);
            ParseFntInt(p, "height",   &rawH);
            ParseFntInt(p, "xoffset",  &rawXoff);
            ParseFntInt(p, "yoffset",  &rawYoff);
            ParseFntInt(p, "xadvance", &rawXadv);
            ParseFntInt(p, "page",     &rawPage);

            CharTemplate* g = &m_Glyphs[glyphIdx++];
            g->id   = (uint16_t)rawId;
            g->_pad = 0;
            // Normalize in-place (matches binary ARM at 0x0019a128)
            g->u0   = (float)rawX    * invW;
            g->v0   = (float)rawY    * invH;
            g->w    = (float)rawW    * invLH;
            g->h    = (float)rawH    * invLH;
            g->xoff = (float)rawXoff * invLH;
            g->yoff = (float)rawYoff * invLH;
            g->xadv = (float)rawXadv * invLH;
            g->page = (uint8_t)rawPage;
            g->_pad2[0] = g->_pad2[1] = g->_pad2[2] = 0;

            if (rawId >= 0 && rawId < 256) {
                m_GlyphLookup[rawId] = g;
            }
        } else if (strncmp(p, "kerning ", 8) == 0 && kerningIdx < m_KerningCount) {
            int first = 0, second = 0;
            float amount = 0.0f;
            int iamt = 0;
            ParseFntInt(p, "first",  &first);
            ParseFntInt(p, "second", &second);
            ParseFntInt(p, "amount", &iamt);
            amount = (float)iamt;
            m_Kernings[kerningIdx].first  = (uint32_t)first;
            m_Kernings[kerningIdx].second = (uint32_t)second;
            m_Kernings[kerningIdx].amount = amount;
            kerningIdx++;
        }

        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }

    delete[] buf;

    // Final fix-up: normalize base
    if (m_LineHeight > 0.0f) {
        m_BaseNorm /= m_LineHeight;
    }

    // Load page textures.
    // TextureManager::Load routes through FileSystem_Direct::TranslateFileName which
    // prepends data_dir; pass a logical (relative) path, not an absolute one.
    // Port specific: derive logical subdir by stripping data_dir prefix from baseDir.
    const char* dataDir = TextureManager::GetDataDir();
    const char* logicalSubDir = "";
    char logicalSubDirBuf[512] = "";
    if (dataDir && dataDir[0] && baseDir[0]) {
        size_t ddLen = strlen(dataDir);
        // baseDir may end with '/' or '\\'; dataDir typically does not
        if (strncmp(baseDir, dataDir, ddLen) == 0) {
            const char* rel = baseDir + ddLen;
            while (*rel == '/' || *rel == '\\') rel++;
            // rel now points to e.g. "fonts/" or "fonts"
            strncpy(logicalSubDirBuf, rel, sizeof(logicalSubDirBuf) - 1);
            logicalSubDirBuf[sizeof(logicalSubDirBuf) - 1] = '\0';
            logicalSubDir = logicalSubDirBuf;
        }
    }

    for (int i = 0; i < m_PageCount; i++) {
        if (!m_Pages[i].filename) continue;

        // Port specific: swap extension to .tex (original atlas is .tga, not present)
        const char* origName = m_Pages[i].filename;
        char texName[512];
        strncpy(texName, origName, sizeof(texName) - 1);
        texName[sizeof(texName) - 1] = '\0';
        char* dot = strrchr(texName, '.');
        if (dot) strcpy(dot, ".tex");

        // Build logical path: subdir + texName (FileSystem_Direct prepends data_dir)
        char logicalPath[512];
        if (logicalSubDir[0]) {
            size_t sdLen = strlen(logicalSubDir);
            bool needSlash = sdLen > 0 &&
                logicalSubDir[sdLen - 1] != '/' &&
                logicalSubDir[sdLen - 1] != '\\';
            snprintf(logicalPath, sizeof(logicalPath), "%s%s%s",
                     logicalSubDir, needSlash ? "/" : "", texName);
        } else {
            snprintf(logicalPath, sizeof(logicalPath), "%s", texName);
        }
        m_Pages[i].texture = TextureManager::GetInstance().Load(logicalPath);
    }

    // Pre-allocate per-page vertex scratch: PAGE_VERT_CAPACITY (0x600) verts per page.
    // Binary stores std::vector<std::vector<QUADCUSTOMVERTEX>> at +0x42c.
    m_PageVerts.clear();
    m_PageVerts.resize((size_t)m_PageCount);
    for (int i = 0; i < m_PageCount; i++) {
        m_PageVerts[i].resize(PAGE_VERT_CAPACITY);
    }

    return 1;
}

Mortar::SmartPtr<Font> Font::Create(const char* path) {
    Font* font = new Font();
    if (!font->Load(path)) {
        delete font;
        return Mortar::SmartPtr<Font>();
    }
    return Mortar::SmartPtr<Font>(font);
}

// ---------------------------------------------------------------------------
// Accessor helpers
// ---------------------------------------------------------------------------

Font::CharTemplate* Font::GetCharTemplate(uint32_t cp) const {
    if (cp < 256) return m_GlyphLookup[cp];
    // For codepoints >= 256, linear search (rare in this game)
    for (int i = 0; i < m_GlyphCount; i++) {
        if (m_Glyphs[i].id == (uint16_t)cp) return &m_Glyphs[i];
    }
    return nullptr;
}

Font::Page* Font::GetPage(unsigned long idx) const {
    if (idx < static_cast<unsigned long>(m_PageCount)) return &m_Pages[idx];
    return nullptr;
}

// ---------------------------------------------------------------------------
// GetLineLength — used for horizontal alignment per-line offset
// Returns the line width in lineHeight-normalized units up to a wrap point.
// outSlack is unused (binary computes it but callers ignore it).
// ---------------------------------------------------------------------------

float Font::GetLineLength(Mortar::Utf8StringIterator iter, float wrapWidth, float* outSlack) {
    float runX = 0.0f;
    while (!iter.IsEmpty()) {
        uint32_t cp = iter.m_CurrentCodepoint;
        if (cp == '\n') break;

        // Skip <font color=...> and </font tags
        if (cp == '<') {
            Mortar::Utf8StringIterator tmp = iter + 1;
            if (!tmp.IsEmpty()) {
                uint32_t next = tmp.m_CurrentCodepoint;
                if (next == '/') {
                    // </font ...> — skip to '>'
                    iter++;
                    while (!iter.IsEmpty() && iter.m_CurrentCodepoint != '>') iter++;
                    if (!iter.IsEmpty()) iter++; // consume '>'
                    continue;
                } else if (next == 'f' || next == 'F') {
                    // <font color=...> — skip to '>'
                    while (!iter.IsEmpty() && iter.m_CurrentCodepoint != '>') iter++;
                    if (!iter.IsEmpty()) iter++;
                    continue;
                }
            }
        }

        CharTemplate* g = GetCharTemplate(cp);
        if (!g) { iter++; continue; }

        // Word-wrap check at space
        if (cp == ' ' && wrapWidth > 0.0f) {
            // Measure the next word
            float wordW = 0.0f;
            Mortar::Utf8StringIterator wi = iter + 1;
            while (!wi.IsEmpty() && wi.m_CurrentCodepoint != ' ' && wi.m_CurrentCodepoint != '\n') {
                CharTemplate* wg = GetCharTemplate(wi.m_CurrentCodepoint);
                if (wg) wordW += wg->xadv;
                wi++;
            }
            if (runX + wordW > wrapWidth) break;
        }

        runX += g->xadv;
        iter++;
    }

    if (outSlack) *outSlack = wrapWidth - runX;
    return runX;
}

// ---------------------------------------------------------------------------
// MeasureWidth (char* overload — compat for existing callers)
// ---------------------------------------------------------------------------

float Font::MeasureWidth(float scale, const char* text) const {
    Mortar::Utf8StringIterator iter(text);
    return MeasureWidth(scale, iter);
}

float Font::MeasureWidth(float scale, Mortar::Utf8StringIterator iter) const {
    // Port specific: TTF dispatch.
    {
        FontCacheObjectTTF* ttf = FontTTFRegistry::GetInstance().Lookup(this);
        if (ttf) {
            int pixelSize = (int)(scale + 0.5f);
            if (pixelSize < 1)   pixelSize = 1;
            if (pixelSize > 256) pixelSize = 256;
            // Matches DrawStringTTF's normalization so measured width equals
            // drawn width. GetGlyph metrics render at FT_Set_Char_Size's
            // vert_res = atlas->m_CacheSize (100, not 72dpi) -- see
            // FontCacheObjectTTF::SetCharSize -- so the pixelSize-relative
            // scale needs the extra 72/m_CacheSize factor, not a bare
            // 1/pixelSize.
            FontInterface* atlas = ttf->GetAtlas();
            const float invPS = atlas
                ? 1.0f / (float)pixelSize * (72.0f / (float)atlas->m_CacheSize)
                : 1.0f / (float)pixelSize;
            float width = 0.0f;
            float maxW  = 0.0f;
            while (!iter.IsEmpty()) {
                uint32_t cp = iter.m_CurrentCodepoint;
                if (cp == '\n') {
                    if (width > maxW) maxW = width;
                    width = 0.0f;
                    iter++;
                    continue;
                }
                if (cp == '<') {
                    while (!iter.IsEmpty() && iter.m_CurrentCodepoint != '>') iter++;
                    if (!iter.IsEmpty()) iter++;
                    continue;
                }
                const GlyphAtlasEntry* g = ttf->GetGlyph(cp, pixelSize);
                if (g) width += (float)g->advanceX * invPS;
                iter++;
            }
            if (width > maxW) maxW = width;
            return maxW;
        }
    }

    float width = 0.0f;
    float maxW  = 0.0f;
    while (!iter.IsEmpty()) {
        uint32_t cp = iter.m_CurrentCodepoint;
        if (cp == '\n') {
            if (width > maxW) maxW = width;
            width = 0.0f;
            iter++;
            continue;
        }
        // Skip color tags
        if (cp == '<') {
            while (!iter.IsEmpty() && iter.m_CurrentCodepoint != '>') iter++;
            if (!iter.IsEmpty()) iter++;
            continue;
        }
        CharTemplate* g = GetCharTemplate(cp);
        if (g) width += g->xadv;
        iter++;
    }
    if (width > maxW) maxW = width;
    return maxW;
}

// ---------------------------------------------------------------------------
// MeasureString (binary @ 0x001988a8)
// Single-line only: stops at newline or end of string.
// Returns total advance in lineHeight-normalized units.
// ---------------------------------------------------------------------------

// ASM-verified: 2026-05-09 v1.6.1 binary @ 0x001988a8 (asm-inspector)
// Binary's MeasureString is a thunk that calls GetLineLength(iter, 0.0f, NULL).
// Forward through to match exactly.
float Font::MeasureString(const Mortar::Utf8StringIterator& iterIn) const {
    // Port specific: TTF dispatch -- measure using glyph advances at default size.
    {
        FontCacheObjectTTF* ttf = FontTTFRegistry::GetInstance().Lookup(this);
        if (ttf) {
            const int pixelSize = ttf->GetDefaultPixelSize();
            // Same 72/m_CacheSize correction as MeasureWidth/DrawStringTTF --
            // GetGlyph metrics render at vert_res=m_CacheSize, not 72dpi.
            FontInterface* atlas = ttf->GetAtlas();
            const float invPS = atlas
                ? 1.0f / (float)pixelSize * (72.0f / (float)atlas->m_CacheSize)
                : 1.0f / (float)pixelSize;
            float width = 0.0f;
            Mortar::Utf8StringIterator scan = iterIn;
            while (!scan.IsEmpty() && scan.m_CurrentCodepoint != '\n') {
                uint32_t cp = scan.m_CurrentCodepoint;
                const GlyphAtlasEntry* g = ttf->GetGlyph(cp, pixelSize);
                if (g) width += (float)g->advanceX * invPS;
                scan++;
            }
            return width;
        }
    }
    // GetLineLength has no binary const-qualifier (matches its mangled ABI) and never
    // mutates Font state; const_cast bridges this const method to that non-const call.
    return const_cast<Font*>(this)->GetLineLength(iterIn, 0.0f, nullptr);
}

float Font::MeasureString(const char* str) const {
    Mortar::Utf8StringIterator iter(str);
    return MeasureString(iter);
}

// ---------------------------------------------------------------------------
// TTF DrawString helper (port specific)
//
// Renders a string using the FreeType glyph atlas. Follows the same
// quad-emit geometry as the .fnt path so call sites need no changes.
// Pixel size = (int)scale (world units == pixels in original ortho space).
// GlyphAtlasEntry metrics (bearingX/Y, advanceX, width, height) come out of
// FontCacheObjectTTF::GetGlyph rendered at FT_Set_Char_Size's vert_res =
// atlas->m_CacheSize (100, NOT 72dpi -- see FontCacheObjectTTF::SetCharSize),
// so they are pixelSize*(m_CacheSize/72) px, not 1px-per-pixelSize. This
// path folds in the extra 72/m_CacheSize factor locally (see invPS below) so
// the caller-requested `scale` still lands correctly in world units.
// ---------------------------------------------------------------------------

static void DrawStringTTF(FontCacheObjectTTF* ttf, float scale,
                           Mortar::Utf8StringIterator iter,
                           const _Vector3<float>& pos, const Colour& colour,
                           int alignment,
                           const Mortar::MortarRectangleT<float>* clipRect)
{
    FontInterface* atlas = ttf->GetAtlas();
    if (!atlas) return;

    // Pixel size used for glyph rendering: clamp to [1,256].
    int pixelSize = (int)(scale + 0.5f);
    if (pixelSize < 1)  pixelSize = 1;
    if (pixelSize > 256) pixelSize = 256;

    // GlyphAtlasEntry metrics (advanceX/bearingX/Y/width/height) come out of
    // FontCacheObjectTTF::GetGlyph scaled by FT_Set_Char_Size's vert_res =
    // m_CacheSize (100, not 72dpi) -- see FontCacheObjectTTF::SetCharSize.
    // So a glyph requested at `pixelSize` actually rasterises pixelSize *
    // (m_CacheSize/72) device px. BakedStringTTF (the binary-calibrated TTF
    // consumer) uses these metrics RAW as its world-unit baseline, so this is
    // not a bug -- but THIS path normalizes by (1/pixelSize) assuming metrics
    // are 1px-per-pixelSize, which is only true at 72dpi. Fold in the extra
    // 72/m_CacheSize factor at quad-build time (below) so this path still
    // lands at the caller-requested `scale` in world units. Keep lineWidth /
    // cursorX accumulation in RAW glyph units here (matches the un-normalized
    // g->advanceX they are summed from) and normalize once at quad-build.
    const float invPS = 1.0f / (float)pixelSize * (72.0f / (float)atlas->m_CacheSize);

    // Pass 1: collect glyphs, ensure all are in atlas, measure line width.
    // We need the line width for alignment before emitting vertices.
    float lineWidth = 0.0f;
    {
        Mortar::Utf8StringIterator scan = iter;
        while (!scan.IsEmpty()) {
            uint32_t cp = scan.m_CurrentCodepoint;
            if (cp == '\n') break;
            if (cp != '<') {
                const GlyphAtlasEntry* g = ttf->GetGlyph(cp, pixelSize);
                if (g) lineWidth += (float)g->advanceX;
            } else {
                // Skip color tags in measurement.
                while (!scan.IsEmpty() && scan.m_CurrentCodepoint != '>') scan++;
                if (!scan.IsEmpty()) scan++;
                continue;
            }
            scan++;
        }
    }

    // Upload any newly packed glyphs to the GL texture.
    atlas->BuildPendingTextures();

    // Determine alignment offset in pixels.
    float lineOffset = 0.0f;
    const int horizAlign = alignment & 0x3;
    if (horizAlign == 2) {
        lineOffset = -lineWidth;
    } else if (horizAlign == 3) {
        lineOffset = -lineWidth * 0.5f;
    }
    // CENTER (0x01) is inert per binary spec (matches .fnt DrawString behaviour).

    // Pass 2: emit quads. We render in pixel coordinates then apply the
    // world-space transform at flush time (same as .fnt path).
    // Max glyphs per draw call -- reuse a local stack buffer.
    const int MAX_GLYPHS = 512;
    QUADCUSTOMVERTEX verts[MAX_GLYPHS * 6];
    GLuint pageTexIDs[MAX_GLYPHS]; // one per glyph (parallel to every 6-vert group)
    int vertCount = 0;

    const uint32_t packedColour = colour.PlatformColour();
    float cursorX = lineOffset;

    // Port specific: TOP-origin pen shift. GlyphAtlasEntry::bearingY is
    // baseline-relative (FreeType convention: ink top is `bearingY` px ABOVE
    // the baseline), so anchoring quads at raw bearingY makes `pos.y` the
    // BASELINE and all ink renders above it. The bitmap-.fnt path's Y build
    // (Font.cpp DrawString, "cy = cursorY - g->yoff - g->h*0.5f" with
    // cursorY starting at 0) instead anchors `pos.y` at the TOP of the line:
    // g->yoff is the .fnt "top of line -> top of glyph ink" offset, so at
    // cursorY=0 the glyph top sits at -yoff, i.e. AT OR BELOW pos.y, never
    // above it. To match that convention here, shift the pen down by the
    // face ascent so local Y=0 (before the scale+translate transform below)
    // represents the line's TOP, same as the bitmap path's cursorY=0.
    const float ascentPx = ttf->GetAscender((float)pixelSize);

    Mortar::Utf8StringIterator it = iter;
    while (!it.IsEmpty() && vertCount < MAX_GLYPHS) {
        uint32_t cp = it.m_CurrentCodepoint;
        if (cp == '\n') { it++; break; }

        // Skip color tags (TTF path: tags are not supported yet, just skip them).
        if (cp == '<') {
            while (!it.IsEmpty() && it.m_CurrentCodepoint != '>') it++;
            if (!it.IsEmpty()) it++;
            continue;
        }

        const GlyphAtlasEntry* g = ttf->GetGlyph(cp, pixelSize);
        if (!g) { it++; continue; }

        if (g->width > 0 && g->height > 0) {
            // Glyph quad in pixel space. bearingY is pixels above baseline;
            // subtract the face ascent to re-anchor to the line TOP (see
            // ascentPx comment above) so this matches the .fnt TOP-origin
            // convention. Local space feeds textM (pure scale+translate, no
            // flip) straight into the +Y-up world ortho -- same convention
            // as the .fnt path's "cy+hh -> top / cy-hh -> bottom"
            // (Font.cpp ~1237). So y0 (top, paired with v0) must be the
            // LARGER local Y, not more negative.
            const float x0 = cursorX + (float)g->bearingX;
            const float y0 = (float)g->bearingY - ascentPx;    // top edge (+Y-up), line-top-relative
            const float x1 = x0 + (float)g->width;
            const float y1 = y0 - (float)g->height;           // bottom edge (+Y-up)

            // Normalize to lineHeight-normalized space (like .fnt): divide
            // raw glyph-space coords by pixelSize*(m_CacheSize/72) (see the
            // invPS comment above -- GetGlyph metrics render at m_CacheSize
            // dpi, not 1px-per-pixelSize).
            float nx0 = x0 * invPS, nx1 = x1 * invPS;
            float ny0 = y0 * invPS, ny1 = y1 * invPS;   // ny0=top, ny1=bottom (+Y-up)

            float u0 = g->u0, v0 = g->v0;
            float u1 = g->u1, v1 = g->v1;
            const float kZ = 0.0f;

            // Port specific: per-glyph clip clamp + UV lerp, in this local
            // (pre-textM) space -- mirrors the .fnt path's clip block
            // (Font.cpp ~1317-1347), which the TTF path bypassed entirely
            // before this fix (DrawStringTTF took no clipRect at all, so
            // UiDropdown's row text -- drawn via a TTF font, see m_LangFont
            // -- never clamped to the panel viewport regardless of the
            // caller passing a rect). clipRect is caller-transformed into
            // this same local space by Font::DrawString before the TTF
            // dispatch (see the clipRect->Scale(1/scale) entry transform).
            bool clipSkip = false;
            if (clipRect != nullptr) {
                if (nx1 < clipRect->left || nx0 > clipRect->right ||
                    ny0 < clipRect->bottom || ny1 > clipRect->top) {
                    clipSkip = true;
                } else {
                    if (nx0 < clipRect->left) {
                        float fullW = nx1 - nx0;
                        nx0 = clipRect->left;
                        float ratio = fullW != 0.0f ? (nx1 - nx0) / fullW : 0.0f;
                        u0 = u1 - (u1 - u0) * ratio;
                    }
                    if (nx1 > clipRect->right) {
                        float fullW = nx1 - nx0;
                        nx1 = clipRect->right;
                        float ratio = fullW != 0.0f ? (nx1 - nx0) / fullW : 0.0f;
                        u1 = u0 + (u1 - u0) * ratio;
                    }
                    if (ny0 > clipRect->top) {
                        float fullH = ny0 - ny1;
                        ny0 = clipRect->top;
                        float ratio = fullH != 0.0f ? (ny0 - ny1) / fullH : 0.0f;
                        v0 = v1 - (v1 - v0) * ratio;
                    }
                    if (ny1 < clipRect->bottom) {
                        float fullH = ny0 - ny1;
                        ny1 = clipRect->bottom;
                        float ratio = fullH != 0.0f ? (ny0 - ny1) / fullH : 0.0f;
                        v1 = v0 + (v1 - v0) * ratio;
                    }
                }
            }

            if (!clipSkip) {
                QUADCUSTOMVERTEX* v = &verts[vertCount * 6];
                v[0] = { nx0, ny1, kZ, 0,0,1, packedColour, u0, v1 }; // LB
                v[1] = { nx0, ny0, kZ, 0,0,1, packedColour, u0, v0 }; // LT
                v[2] = { nx1, ny1, kZ, 0,0,1, packedColour, u1, v1 }; // RB
                v[3] = { nx1, ny0, kZ, 0,0,1, packedColour, u1, v0 }; // RT
                v[4] = v[3]; // degenerate
                v[5] = v[3]; // degenerate

                // Inter-glyph connector (matches .fnt path).
                if (vertCount > 0) {
                    v[-1] = v[0];
                }
                pageTexIDs[vertCount] = g->pageTextureID;
                vertCount++;
            }
        }

        cursorX += (float)g->advanceX;
        it++;
    }

    if (vertCount == 0) return;

    // Apply world transform (same as .fnt path).
    Matrix44 textM;
    textM.Identity();
    textM.ApplyScale(scale, scale, 1.0f);
    textM.GlobalTranslate44(pos);

    const float a00 = textM.m[0], a01 = textM.m[4], ax = textM.m[12];
    const float a10 = textM.m[1], a11 = textM.m[5], ay = textM.m[13];

    const int totalVerts = vertCount * 6;
    for (int i = 0; i < totalVerts; i++) {
        const float lx = verts[i].x;
        const float ly = verts[i].y;
        verts[i].x = a00 * lx + a01 * ly + ax;
        verts[i].y = a10 * lx + a11 * ly + ay;
    }

    // Flush: identity world matrix + per-page atlas textures.
    MatrixStack& world = MatrixManager::GetInstance().GetWorldStack();
    world.Push();
    world.m_Current.Identity();
    world.m_Version++;
    MatrixManager::GetInstance().UploadModelViewOnly();

    // Draw per-page runs: bind each atlas page and draw its consecutive glyph run.
    // In the common single-page case this is exactly one bind + one DrawTriStrip.
    Renderer* renderer = Renderer::GetInstance();
    if (renderer) {
        int gIdx = 0;
        while (gIdx < vertCount) {
            GLuint curTex = pageTexIDs[gIdx];
            int runStart = gIdx;
            while (gIdx < vertCount && pageTexIDs[gIdx] == curTex) gIdx++;
            renderer->BindTexture2D((uint32_t)curTex);
            renderer->DrawTriStrip(verts + runStart * 6, (gIdx - runStart) * 6);
        }
    }

    if (renderer) renderer->BindTexture2D(0);

    world.Pop();

    // Port specific: text-bounds debug overlay (F1). Ink bounds from transformed verts.
#if !defined(__bada__) && !defined(FN_GL_STUB)
    if (FN::g_DebugHitboxes >= 3 && !FN::g_SuppressTextOverlay) {
        float inkX0 = verts[0].x, inkX1 = verts[0].x;
        float inkY0 = verts[0].y, inkY1 = verts[0].y;
        for (int i = 1; i < totalVerts; i++) {
            if (verts[i].x < inkX0) inkX0 = verts[i].x;
            if (verts[i].x > inkX1) inkX1 = verts[i].x;
            if (verts[i].y < inkY0) inkY0 = verts[i].y;
            if (verts[i].y > inkY1) inkY1 = verts[i].y;
        }
        FN::DebugText_Overlay(pos.x, pos.y,
                              false, 0.0f, 0.0f, 0.0f, 0.0f,
                              true, inkX0, inkY0, inkX1, inkY1);
    }
#endif
}

// ---------------------------------------------------------------------------
// DrawString (matches Font_DrawString 0x00198e44)
// ---------------------------------------------------------------------------

// ASM-verified: 2026-05-09 v1.6.1 binary @ 0x00198e44 (asm-inspector)
void Font::DrawString(float scale, float yLineFactor, float rotZ,
                      Mortar::Utf8StringIterator iter, const _Vector3<float>& pos, const Colour& colour,
                      _Vector2<float> maxWH, int alignment, float z,
                      Mortar::MortarRectangleT<float>* clipRect)
{
    (void)z;

    // Binary @ 0x0024c824: empty-iterator check happens before the clipRect
    // transform below, so an empty string never touches the caller's rect.
    if (iter.IsEmpty()) return;

    // ASM-spec v1.6.1 Font::DrawString @0x0024c7f0: clipRect per-glyph clamp+UV-lerp.
    // Entry transforms the caller's world-space clipRect into the same
    // lineHeight-normalized local space the glyph loop computes cx/cy in:
    //   clipRect -= pos; clipRect.Scale(1/scale);
    // (restored via clipRect.Scale(scale); clipRect += pos; on every exit path,
    // since the binary mutates the caller's rect by pointer in place.)
    if (clipRect != nullptr) {
        clipRect->left   -= pos.x;
        clipRect->right  -= pos.x;
        clipRect->top    -= pos.y;
        clipRect->bottom -= pos.y;
        clipRect->Scale(1.0f / scale);
    }

    // Port specific: TTF dispatch. The TTF path uses pixel-size glyphs from
    // FreeType rather than pre-baked atlas quads from a .fnt page. Not part of
    // the binary's clip-rect flow; restore the caller's rect before bailing.
    {
        FontCacheObjectTTF* ttf = FontTTFRegistry::GetInstance().Lookup(this);
        if (ttf) {
            // clipRect (if non-NULL) is already in this function's local
            // (pre-scale/translate) space via the entry transform above --
            // DrawStringTTF's nx0/ny0/nx1/ny1 quad coords are built in that
            // SAME normalized local space (pre textM transform), so the rect
            // can be applied directly, no further conversion needed.
            DrawStringTTF(ttf, scale, iter, pos, colour, alignment, clipRect);
            if (clipRect != nullptr) {
                clipRect->Scale(scale);
                clipRect->left   += pos.x;
                clipRect->right  += pos.x;
                clipRect->top    += pos.y;
                clipRect->bottom += pos.y;
            }
            return;
        }
    }

    // Binary modifies maxWH in-place on a stack copy:
    //   maxWH.x /= scale
    //   maxWH.y /= (yLineFactor * scale)
    // yLineFactor is a vertical line-pitch divisor (NOT a wrap-width limit
    // as earlier port comments claimed). The binary's wrapper @ 0x00199aa0
    // hardcodes yLineFactor = 1.0; ScoreControl's "BEST:" label passes 0.9,
    // its digit call passes 0.0 directly to the full overload.
    //
    // Binary preamble @ 0x00198eb0..0x00198eee only runs when maxWH != null
    // (re-analyst 2026-05-10). Port models this by gating on maxWH being
    // non-zero -- prevents NaN when yLineFactor=0 with maxWH=(0,0).
    if (maxWH.x != 0.0f || maxWH.y != 0.0f) {
        maxWH.x /= scale;
        maxWH.y /= (yLineFactor * scale);
    }

    // Word-wrap threshold in lineHeight-normalized units (maxWH.x after /= scale).
    // Wrap is active whenever maxWH.x > 0; the binary's caller signals "no wrap"
    // by passing maxWH = (0, 0). The 0x10 alignment bit is reserved for other
    // semantics (still being RE'd) -- using maxWH.x as the wrap gate matches
    // the description-text path's binary call shape.
    const float wrapLimit = maxWH.x;

    // Per-page glyph vertex counts.
    int* perPageCount = new int[m_PageCount]();

    // m_PageVerts is a vector<vector<QUADCUSTOMVERTEX>> populated by Font::Load.
    // Lazy-resize for unit-test paths that never called Load.
    if ((int)m_PageVerts.size() < m_PageCount) {
        m_PageVerts.resize((size_t)m_PageCount);
        for (int pg = 0; pg < m_PageCount; pg++) {
            if ((int)m_PageVerts[pg].size() < PAGE_VERT_CAPACITY) {
                m_PageVerts[pg].resize(PAGE_VERT_CAPACITY);
            }
        }
    }
    for (int pg = 0; pg < m_PageCount; pg++) {
        perPageCount[pg] = 0;
    }

    // Initial colour
    uint32_t curColour = colour.PlatformColour();
    uint32_t origColour = curColour;

    // Horizontal alignment: compute offset for first line.
    // ASM-verified: 2026-05-10T00:00Z v1.6.1 binary @ 0x00198e44 (re-analyst hand-
    // disassembly @ 0x00198ef0..0x00198f80). Definitive alignment decode:
    //   alignment & 3 == 0 (LEFT):       lineOffset = 0
    //   alignment & 3 == 1 (CENTER):     lineOffset = 0  -- INERT for X
    //                                    (the binary loads 0.0f from the
    //                                    constant pool unconditionally for
    //                                    this branch; bit 4 is tested but
    //                                    both sides produce 0)
    //   alignment & 3 == 2 (RIGHT):
    //     measureCap = (alignment & 0x10) ? wrapLimit : 0.0f
    //     lineOffset = wrapLimit - GetLineLength(iter, measureCap)
    //   alignment & 3 == 3 (RIGHT|CENTER, "centre-within-box"):
    //     same as RIGHT, then lineOffset *= 0.5
    // Implications when callers pass maxWH=(0,0) (most callers):
    //   - LEFT  (0x00): text starts at pos.x
    //   - CENTER(0x01): text starts at pos.x   (NOT centred -- caller must
    //                                           pre-offset its anchor)
    //   - RIGHT (0x02): right edge at pos.x    (lineOffset = -lineWidth)
    //   - 0x03         : centred on pos.x      (lineOffset = -lineWidth/2)
    // Earlier port unconditionally applied -lineWidth*0.5 for CENTER, which
    // moved score digits leftward over the watermelon icon. The binary's
    // CENTER mode is intentionally inert; ScoreControl's 0x0d achieves
    // LEFT-anchor layout via this inert path, not via a real centring op.
    // ASM-verified: 2026-05-11 v1.6.1 binary @ 0x00198eee..0x00198f7c (asm-inspector)
    const int horizAlign = alignment & 0x3;
    float lineOffset = 0.0f;
    if (horizAlign >= 2) {
        // First-line measure. The asm-verified compute uses
        //   measureCap = (alignment & 0x10) ? wrapLimit : 0.0f
        // (full-string measure when 0x10 bit not set). But when wrap is
        // active (wrapLimit > 0) and the bit isn't set, the full-string
        // measure produces a negative offset on line 1 because the full
        // remaining string is much wider than wrapLimit. Force wrap-aware
        // measure whenever wrapLimit > 0 so line 1 is also centered per
        // its own wrap-bounded width. Matches the wrap-induced per-line
        // recompute below.
        const float measureCap = (wrapLimit > 0.0f) ? wrapLimit
                               : ((alignment & 0x10) ? wrapLimit : 0.0f);
        float len = GetLineLength(iter, measureCap, nullptr);
        lineOffset = wrapLimit - len;
        if (horizAlign == 3) lineOffset *= 0.5f;
    }
    float cursorX = 0.0f;
    float cursorY = 0.0f;  // starts at 0; vertical alignment applied via TranslateLocal

    // Spacing from maxWH (binary reads local_64[0] which is a spacing scalar;
    // in shipping callers maxWH is always (0,0) so spacing = 0)
    const float spacing = 0.0f;

    // --- Glyph emit loop ---
    while (!iter.IsEmpty()) {
        uint32_t cp = iter.m_CurrentCodepoint;

        if (cp == '\n') {
            iter++;
            // Per-line recompute (binary @ 0x00198fc0..0x0019906a).
            // Same alignment decode as the first-line block above.
            lineOffset = 0.0f;
            if (horizAlign >= 2) {
                const float measureCap = (alignment & 0x10) ? wrapLimit : 0.0f;
                float _len = GetLineLength(iter, measureCap, nullptr);
                lineOffset = wrapLimit - _len;
                if (horizAlign == 3) lineOffset *= 0.5f;
            }
            cursorX = 0.0f;
            cursorY -= yLineFactor;
            continue;
        }

        // Word-wrap at space
        if (cp == ' ' && wrapLimit > 0.0f) {
            Mortar::Utf8StringIterator wi = iter + 1;
            float wordW = 0.0f;
            while (!wi.IsEmpty() && wi.m_CurrentCodepoint != ' ' && wi.m_CurrentCodepoint != '\n') {
                CharTemplate* wg = GetCharTemplate(wi.m_CurrentCodepoint);
                if (wg) wordW += wg->xadv;
                wi++;
            }
            if (cursorX + wordW > wrapLimit) {
                iter++;
                lineOffset = 0.0f;
                if (horizAlign >= 2) {
                    // Wrap-induced break -- measure ONE wrap-line worth of
                    // text so the offset reflects the current line's width,
                    // not the entire remaining string. Without `wrapLimit`
                    // here, GetLineLength(..., 0.0f) returns the full
                    // remaining string length, producing a "staircase" effect
                    // where each line's offset grows based on what's still
                    // to be rendered, not what's actually on this line.
                    // Differs from the asm-verified first-line measure (which
                    // does honor alignment & 0x10) -- the wrap path knows it
                    // just broke on a wrap point, so wrap-aware measurement
                    // is always correct here.
                    float _len = GetLineLength(iter, wrapLimit, nullptr);
                    lineOffset = wrapLimit - _len;
                    if (horizAlign == 3) lineOffset *= 0.5f;
                }
                cursorX = 0.0f;
                cursorY -= yLineFactor;
                continue;
            }
        }

        // Color tags: <font color=RRGGBB[AA]> and </font>
        if (cp == '<') {
            Mortar::Utf8StringIterator tagIter = iter + 1;
            if (!tagIter.IsEmpty() && (tagIter.m_CurrentCodepoint == '/' ||
                tagIter.m_CurrentCodepoint == 'f' || tagIter.m_CurrentCodepoint == 'F')) {

                if (tagIter.m_CurrentCodepoint == '/') {
                    // </font -- reset colour, advance to '>'
                    curColour = origColour;
                    iter++;
                    while (!iter.IsEmpty() && iter.m_CurrentCodepoint != '>') iter++;
                    if (!iter.IsEmpty()) iter++; // consume '>'
                    continue;
                } else {
                    // <font color=RRGGBB[AA]>
                    // Skip to 'color=' or 'colour=' tag
                    // Advance past '<font' to find 'color='
                    Mortar::Utf8StringIterator scan = iter + 1;
                    // Skip "font"
                    while (!scan.IsEmpty() && scan.m_CurrentCodepoint != '>' &&
                           scan.m_CurrentCodepoint != '=') scan++;
                    // Now scan should be at '='
                    if (!scan.IsEmpty() && scan.m_CurrentCodepoint == '=') {
                        scan++; // skip '='
                        // Read up to 8 hex digits
                        char hexbuf[9];
                        int hexlen = 0;
                        while (!scan.IsEmpty() && hexlen < 8) {
                            uint32_t hc = scan.m_CurrentCodepoint;
                            if ((hc >= '0' && hc <= '9') || (hc >= 'a' && hc <= 'f') ||
                                (hc >= 'A' && hc <= 'F')) {
                                hexbuf[hexlen++] = (char)hc;
                                scan++;
                            } else {
                                break;
                            }
                        }
                        hexbuf[hexlen] = '\0';
                        if (hexlen >= 6) {
                            uint32_t rgb = (uint32_t)strtoul(hexbuf, nullptr, 16);
                            uint8_t cr, cg, cb, ca;
                            if (hexlen >= 8) {
                                // RRGGBBAA
                                cr = (uint8_t)((rgb >> 24) & 0xFF);
                                cg = (uint8_t)((rgb >> 16) & 0xFF);
                                cb = (uint8_t)((rgb >>  8) & 0xFF);
                                ca = (uint8_t)(rgb & 0xFF);
                            } else {
                                // RRGGBB — preserve original alpha
                                cr = (uint8_t)((rgb >> 16) & 0xFF);
                                cg = (uint8_t)((rgb >>  8) & 0xFF);
                                cb = (uint8_t)(rgb & 0xFF);
                                ca = colour.a;
                            }
                            Colour tagCol(cr, cg, cb, ca);
                            curColour = tagCol.PlatformColour();
                        }
                    }
                    // Advance iter to past '>'
                    while (!iter.IsEmpty() && iter.m_CurrentCodepoint != '>') iter++;
                    if (!iter.IsEmpty()) iter++;
                    continue;
                }
            }
        }

        CharTemplate* g = GetCharTemplate(cp);
        if (!g) {
            iter++;
            continue;
        }

        int pageIdx = (int)g->page;
        if (pageIdx >= 0 && pageIdx < m_PageCount) {
            // Vertex geometry in lineHeight-normalized space (ARM-confirmed at 0x0019919e)
            // cx = cursor_x + xoff + w*0.5
            // cy = cursor_y - yoff - h*0.5
            const float cx = cursorX + lineOffset + g->xoff + g->w * 0.5f;
            const float cy = cursorY - g->yoff - g->h * 0.5f;
            const float hw = g->w * 0.5f;
            const float hh = g->h * 0.5f;

            // UVs already normalized in CharTemplate
            float u0 = g->u0;
            float v0 = g->v0;
            // u1/v1 computed from w/h * (lineHeight / scaleW/H) = raw_w/scaleW
            const float lhDivW = (m_ScaleW > 0) ? m_LineHeight / (float)m_ScaleW : 1.0f;
            const float lhDivH = (m_ScaleH > 0) ? m_LineHeight / (float)m_ScaleH : 1.0f;
            float u1 = u0 + g->w * lhDivW;
            float v1 = v0 + g->h * lhDivH;

            // Quad extents (local space, pre-clamp). top/bottom in the
            // binary's Y sense: top = cy+hh (screen-top), bottom = cy-hh.
            float x0 = cx - hw;
            float x1 = cx + hw;
            float top = cy + hh;
            float bottom = cy - hh;
            bool clipSkip = false;

            // ASM-spec v1.6.1 Font::DrawString @0x0024c7f0..0x0024cfd8: per-glyph
            // clip clamp + UV lerp. clipRect is already in this function's local
            // (pre-scale/translate) space via the entry transform above. A glyph
            // fully outside any one edge is skipped entirely (no vertex emit,
            // cursor still advances). Otherwise each edge that the quad crosses
            // is clamped to the rect and its UV is lerped proportionally so the
            // visible sub-quad samples the correct texels (partial glyph, not a
            // squashed one). Edges are clamped in order left, right, top, bottom;
            // each later edge's ratio uses the already-clamped opposite coordinate
            // (matches the binary's sequential dependency chain).
            if (clipRect != nullptr) {
                if (x1 < clipRect->left || x0 > clipRect->right ||
                    top < clipRect->bottom || bottom > clipRect->top) {
                    clipSkip = true;
                } else {
                    if (x0 < clipRect->left) {
                        x0 = clipRect->left;
                        float clampedW = x1 - x0;
                        float ratio = fabsf(clampedW) / g->w;
                        u0 = u1 - (u1 - u0) * ratio;
                    }
                    if (x1 > clipRect->right) {
                        x1 = clipRect->right;
                        float clampedW = x1 - x0;
                        float ratio = fabsf(clampedW) / g->w;
                        u1 = u0 + (u1 - u0) * ratio;
                    }
                    if (top > clipRect->top) {
                        top = clipRect->top;
                        float clampedH = bottom - top;
                        float ratio = fabsf(clampedH) / g->h;
                        v0 = v1 - (v1 - v0) * ratio;
                    }
                    if (bottom < clipRect->bottom) {
                        bottom = clipRect->bottom;
                        float clampedH = bottom - top;
                        float ratio = fabsf(clampedH) / g->h;
                        v1 = v0 + (v1 - v0) * ratio;
                    }
                }
            }

            // ASM-verified: 2026-05-09 v1.6.1 binary @ 0x00199576..0x00199836
            // (asm-inspector). Per-glyph emit is 6 verts in GL_TRIANGLE_STRIP
            // order: LB, LT, RB, RT + 2 degenerate copies of RT.
            //   strip(LB,LT,RB,RT) = tris (LB,LT,RB) + (LT,RB,RT) = one quad
            // Inter-glyph connector: when starting a new glyph at base > 0,
            // the binary OVERWRITES the previous glyph's last slot
            // verts[base-1] with the current LB before emitting the new
            // 6 verts. This collapses the strip transition
            //   (RT_prev, RT_prev, LB_cur)  -> RT_prev=LB_cur degen
            //   (RT_prev=LB_cur, LB_cur, LT_cur) -> degen (first two equal)
            // Without the overwrite, the second tri is NON-degenerate and
            // draws a thin connector triangle between glyphs. (Binary
            // writes verts[base-1] @ 0x001995ee..0x00199648.)
            // V-axis pairing matches binary: cy+hh (screen-top) -> v0,
            // cy-hh (screen-bottom) -> v1.
            if (!clipSkip) {
                const float kZ = 0.0f;  // DAT_00199a94 = 0.0f
                QUADCUSTOMVERTEX v[6];
                v[0] = { x0, bottom, kZ, 0,0,1, curColour, u0, v1 };  // LB
                v[1] = { x0, top,    kZ, 0,0,1, curColour, u0, v0 };  // LT
                v[2] = { x1, bottom, kZ, 0,0,1, curColour, u1, v1 };  // RB
                v[3] = { x1, top,    kZ, 0,0,1, curColour, u1, v0 };  // RT
                v[4] = v[3];                                          // degenerate
                v[5] = v[3];                                          // degenerate

                const int base = perPageCount[pageIdx] * 6;
                if (base + 6 <= PAGE_VERT_CAPACITY && pageIdx < (int)m_PageVerts.size()) {
                    QUADCUSTOMVERTEX* dst = &m_PageVerts[pageIdx][base];
                    // Inter-glyph connector overwrite: prev glyph's last slot
                    // becomes this glyph's LB so the strip transition is fully
                    // degenerate. Binary @ 0x001995ee-0x00199648.
                    if (base > 0) {
                        dst[-1] = v[0];
                    }
                    for (int vi = 0; vi < 6; vi++) {
                        dst[vi] = v[vi];
                    }
                    perPageCount[pageIdx]++;
                }
            }
        }

        // Advance cursor: xadv + kerning + spacing
        // spacing * (cp == 0x20 ? 3.0 : 1.0) matches binary (spacing=0 in all shipping callers)
        float spacingMul = (cp == 0x20) ? 3.0f : 1.0f;
        cursorX += g->xadv + GetKerning((uint32_t)cp, (uint32_t)0) + spacing * spacingMul;
        iter++;
    }

#if defined(FN_FONT_DEBUG)
    {
        int totalVerts = 0;
        for (int pg = 0; pg < m_PageCount; pg++) totalVerts += perPageCount[pg];
        int lineCount = 0;
        if (maxWH.y > 0.0f) {
            lineCount = (int)(-cursorY / maxWH.y) + 1;
        } else {
            lineCount = (cursorY < 0.0f) ? (int)(-cursorY + 0.5f) + 1 : 1;
        }
        LOG_DEBUG("Font", "DrawString: wrapLimit=%.3f maxWH=(%.3f,%.3f)"
                  " cursorY=%.3f lineCount=%d totalGlyphs=%d alignment=0x%02X"
                  " pos=(%.1f,%.1f) scale=%.1f yLineFactor=%.2f",
                  wrapLimit, maxWH.x, maxWH.y, cursorY, lineCount,
                  totalVerts / 6, alignment,
                  pos.x, pos.y, scale, yLineFactor);
    }
#endif

    // --- Bake text transform into vertex coords (binary-faithful, matches
    // 0x00199216..0x00199254 architecture) ---
    //
    // ASM-verified: 2026-06-24 v1.6.1 Mortar::Font::DrawString @ 0x0024c7f0 (re-analyst)
    //   (10-param Vector3-pos overload; per-glyph scalar-bake vertex stores
    //    @ 0x0024d01c..0x0024d27c; matrix-stack flush transform @ 0x0024d36c..0x0024d384.
    //    v1.5.x 0x00101c58/0x00101964 were the stale equivalents; 0x0010671c is a thunk
    //    for the wrong 12-param overload.)
    // "Helper bypasses the matrix stack entirely. World coords
    // are computed from scalars". The binary's Font_DrawString writes
    // screen-space scalar math directly into the vertex buffer; it never
    // reads or writes m_Current during per-glyph submission. Per-glyph corner
    // emit is `Vec2 ctor` + `Vec2 operator+` + direct vstr.32 into the batch
    // vertex slots.
    //
    // Port replicates this: build the combined text transform matrix once,
    // apply it as a 2D affine to every batched vertex's (x, y), then flush
    // with an identity world matrix. The port is now insensitive to the
    // caller's dirty m_Current, matching the binary's behaviour without the
    // previous Push+Identity workaround.
    //
    // The transform is M = T_world(pos) * RotZ(rotZ) * T_local(0, alignY) * S(scale)
    // -- build via the existing MatrixStack helpers, then snapshot the 2x3
    // affine submatrix (m[0], m[4], m[12]; m[1], m[5], m[13]) into locals.
    Matrix44 textM;
    textM.Identity();
    textM.ApplyScale(scale, scale, 1.0f);
    if (rotZ != 0.0f) {
        textM.RotZ44(sinf(rotZ), cosf(rotZ));
    }

    // ASM-verified: 2026-05-11 v1.6.1 binary @ 0x00199920..0x00199964 (asm-inspector)
    // Vertical alignment: LocalTranslate(0, alignY, 0). The Y-shift depends
    // on the final cursorY (== -(numLines - 1)) so it's known only after
    // the glyph loop above.
    if (alignment & 0xC) {
        float s19 = cursorY - yLineFactor;
        float s14 = -maxWH.y - s19;
        float factor = (alignment & 0x4) ? 0.5f : 1.0f;
        float translateY = s14 * factor;
        textM.LocalTranslate44(0.0f, translateY, 0.0f);
    }
    textM.GlobalTranslate44(pos);

    // 2x3 affine columns (Matrix44 is column-major).
    const float a00 = textM.m[0],  a01 = textM.m[4],  ax = textM.m[12];
    const float a10 = textM.m[1],  a11 = textM.m[5],  ay = textM.m[13];

    // Apply the affine to every emitted vertex's (x, y). Z stays 0 since
    // glyphs are flat and the binary never writes Z.
    for (int pg = 0; pg < m_PageCount; pg++) {
        if (perPageCount[pg] == 0 || pg >= (int)m_PageVerts.size()) continue;
        QUADCUSTOMVERTEX* page_verts = &m_PageVerts[pg][0];
        const int n = perPageCount[pg] * 6;
        for (int i = 0; i < n; i++) {
            const float lx = page_verts[i].x;
            const float ly = page_verts[i].y;
            page_verts[i].x = a00 * lx + a01 * ly + ax;
            page_verts[i].y = a10 * lx + a11 * ly + ay;
        }
    }

    // --- Per-page flush with identity world matrix ---
    // Verts are in world space; the matrix stack must NOT re-transform them.
    // Push/Pop bracket to preserve caller's m_Current.
    MatrixStack& world = MatrixManager::GetInstance().GetWorldStack();
    world.Push();
    world.m_Current.Identity();
    world.m_Version++;
    MatrixManager::GetInstance().UploadModelViewOnly();

    Renderer* renderer = Renderer::GetInstance();
    if (renderer) {
        for (int pg = 0; pg < m_PageCount; pg++) {
            if (perPageCount[pg] == 0) continue;
            Page* page = GetPage(pg);
            if (!page || !page->texture.IsValid()) continue;
            page->texture->Set();
            renderer->DrawTriStrip(
                &m_PageVerts[pg][0],
                perPageCount[pg] * 6);
            page->texture->UnSet();
        }
    }

    world.Pop();

    // Port specific: text-bounds debug overlay (F1). Ink bounds accumulated from
    // all transformed page verts.
#if !defined(__bada__) && !defined(FN_GL_STUB)
    if (FN::g_DebugHitboxes >= 3 && !FN::g_SuppressTextOverlay) {
        float inkX0 = 0.0f, inkX1 = 0.0f, inkY0 = 0.0f, inkY1 = 0.0f;
        bool hasInk = false;
        for (int pg = 0; pg < m_PageCount; pg++) {
            if (perPageCount[pg] == 0 || pg >= (int)m_PageVerts.size()) continue;
            const QUADCUSTOMVERTEX* pv = &m_PageVerts[pg][0];
            const int n = perPageCount[pg] * 6;
            for (int i = 0; i < n; i++) {
                if (!hasInk) {
                    inkX0 = inkX1 = pv[i].x;
                    inkY0 = inkY1 = pv[i].y;
                    hasInk = true;
                } else {
                    if (pv[i].x < inkX0) inkX0 = pv[i].x;
                    if (pv[i].x > inkX1) inkX1 = pv[i].x;
                    if (pv[i].y < inkY0) inkY0 = pv[i].y;
                    if (pv[i].y > inkY1) inkY1 = pv[i].y;
                }
            }
        }
        if (hasInk) {
            FN::DebugText_Overlay(pos.x, pos.y,
                                  false, 0.0f, 0.0f, 0.0f, 0.0f,
                                  true, inkX0, inkY0, inkX1, inkY1);
        }
    }
#endif

    delete[] perPageCount;

    // Exit transform: restore the caller's clipRect (binary mutates it by
    // pointer, in/out) -- Scale(scale) then translate(+pos), the inverse of
    // the entry transform above.
    if (clipRect != nullptr) {
        clipRect->Scale(scale);
        clipRect->left   += pos.x;
        clipRect->right  += pos.x;
        clipRect->top    += pos.y;
        clipRect->bottom += pos.y;
    }
}

// ---------------------------------------------------------------------------
// DrawString — thin char* wrapper (matches 0x00199aa0)
// Packs x/y/z into Vec3, calls the full overload with maxWidth=1.0.
// ---------------------------------------------------------------------------

// Binary-shape wrapper. Matches Font::DrawString @ 0x00199aa0 byte-for-byte:
//   - 7 individual float args mapped to s0..s6 by hard-float ABI.
//   - by-reference iter / colour copied into local stack copies (binary
//     does this for both — Utf8StringIterator copy ctor + Colour copy ctor).
//   - hardcodes yLineFactor = 1.0 when forwarding to Font_DrawString.
void Font::DrawString(Mortar::Utf8StringIterator& iter,
                      const Colour& colour, int alignment,
                      float posX, float posY, float posZ,
                      float scale, float maxWHx, float maxWHy, float rotZ,
                      Mortar::MortarRectangleT<float>* clip)
{
    if (iter.IsEmpty()) return;
    _Vector3<float> pos(posX, posY, posZ);
    _Vector2<float> maxWH(maxWHx, maxWHy);
    // Binary @ 0x00199b1c: vmov.f32 s1, 0x3f800000 -- yLineFactor pinned to 1.0.
    DrawString(scale, /*yLineFactor=*/1.0f, rotZ,
               iter, pos, colour, maxWH, alignment, /*z=*/0.0f, clip);
}

// Port-side convenience wrapper. Forwards to the binary-shape overload
// above (which hardcodes yLineFactor = 1.0 like binary @ 0x00199b1c).
// The 2nd arg `yLineFactor` is preserved in the signature for backward
// source-compat with existing port callers (almost all pass 1.0), but
// is IGNORED -- to pass a non-1.0 value (e.g. ScoreControl's BEST label
// passing 0.9), call the full Font_DrawString directly.
void Font::DrawString(float scale, float /*yLineFactor (ignored)*/, float z,
                      const char* text, const _Vector3<float>& pos,
                      const Colour& colour, int alignment)
{
    if (!text || !*text) return;
    Mortar::Utf8StringIterator iter(text);
    // Forward through the binary-shape wrapper so the call shape lines up
    // with binary's wrapper @ 0x00199aa0 (yLineFactor pinned to 1.0).
    DrawString(iter, colour, alignment,
               pos.x, pos.y, pos.z, scale,
               /*maxWHx=*/0.0f, /*maxWHy=*/0.0f, /*rotZ=*/0.0f, nullptr);
    (void)z;  // binary's wrapper ignores its z slot too
}

// ---------------------------------------------------------------------------
// Unported overloads (binary signatures present; bodies forward/TBD)
// ---------------------------------------------------------------------------

// Binary @ 0x0024c7f0 (v1.6.1; stale 0x00198e44 v1.5.x) -- packed Vec3/Vec2 ABI shape of the full Font_DrawString
// (Ghidra's alternate arg-decode of the same symbol the canonical overload
// implements). Forward to the implemented
// DrawString(scale,yLineFactor,rotZ,iter,pos,colour,maxWH,alignment,z,clipRect).
// The binary's wrapper pins yLineFactor = 1.0 (vmov.f32 s1, 0x3f800000 @
// 0x00199b1c); this shape originates from that wrapper, so yLineFactor = 1.0.
void Font::DrawString(Utf8StringIterator iter, _Vector3<float> pos, Colour colour, float scale,
                      _Vector2<float> maxWH, int alignment, float rotZ, Mortar::MortarRectangleT<float>* clipRect,
                      float z)
{
    DrawString(scale, /*yLineFactor=*/1.0f, rotZ, iter, pos, colour, maxWH, alignment, z, clipRect);
}

// Binary @ 0x0024d6b8 (v1.6.1; stale 0x00199aa0 v1.5.x) -- by-value-arg ABI shape of the binary DrawString wrapper
// (alternate Ghidra decode of the same symbol the canonical wrapper implements).
// Forward to the implemented
// DrawString(iter&,colour&,alignment,posX,posY,posZ,scale,maxWHx,maxWHy,rotZ,clip).
void Font::DrawString(Utf8StringIterator iter, float posX, float posY, float posZ,
                      Colour colour, float scale, float maxWHx, float maxWHy,
                      int alignment, Mortar::MortarRectangleT<float>* clip, float rotZ)
{
    DrawString(iter, colour, alignment, posX, posY, posZ, scale, maxWHx, maxWHy, rotZ, clip);
}

// TODO: v1.6.1 Font::FindAdvanceOfNextWord @0x0024c2a0 -- word-advance helper for word-wrap: walk chars until
// WordWrap::CanBreakLineAt, tag-skip <font>/</font> via strncasecmp, accumulate
// xadv per glyph; return startIter-if-fits else NULL (iterator/char*, NOT float).
// BLOCKED: faithful port requires the Mortar::WordWrap subsystem
// (WordWrap::CanBreakLineAt + East-Asian line-break table + locale flags).
// Note: binary return type is Utf8StringIterator/char* (start iter when next word
// fits, NULL when line break needed) -- the port's `float` return signature is a
// Ghidra mis-decode; correct the signature when WordWrap lands.
// DIFFERS: stub returns 0.0f (wrong type/value); blocked on unported WordWrap.
float Font::FindAdvanceOfNextWord(Utf8StringIterator, float, float, float, float) { return 0.0f; }

// Binary @ 0x0024c228 -- the engine's canonical single-codepoint glyph lookup.
// Takes (long codepoint, int unused_always_zero). The `long` IS the codepoint
// (ARM32 long==int); the second param is ignored (binary callers always pass 0).
//   codepoint < 0   : codepoint += 0x100  (signed-byte wraps into 0x80..0xFF)
//   codepoint <=0xFF: try m_GlyphLookup[codepoint]; if null, linear search
//   codepoint > 0xFF: skip lookup table, linear-search m_Glyphs by id
// Linear search walks m_Glyphs (stride sizeof(CharTemplate)=0x24), comparing the
// uint16 id at offset 0, returning the first match or nullptr after m_GlyphCount.
// Single shared linear search loop at the end for both paths (binary has one
// loop body entered from two points).
Font::CharTemplate* Font::GetCharTemplate(long codepoint, int /*unused*/) {
    if (codepoint < 0) {
        codepoint += 0x100;
    }
    if (codepoint <= 0xFF) {
        CharTemplate* g = m_GlyphLookup[codepoint];
        if (g) return g;
    }
    // codepoint out of lookup-table range OR null lookup slot:
    // shared linear search (matches binary single-loop-body structure)
    for (int i = 0; i < m_GlyphCount; i++) {
        if ((int)m_Glyphs[i].id == codepoint) return &m_Glyphs[i];
    }
    return nullptr;
}

// Binary @ 0x0024c45c (v1.6.1; stale 0x001988f0 was an older build).
//   maxWidth <= 0: walks string counting '\n', returns lineH * (n+1).
//     Binary inits s18=lineH (vmovls s18,s0), adds lineH per '\n' (vaddeq).
//     Port matches exactly; binary also calls GetCharTemplate+GetKerning per char
//     (dead calls whose results are discarded); port omits those -- cosmetic only.
//   maxWidth >  0: DIFFERS: binary calls FindAdvanceOfNextWord(v1.6.1 @0x0024c2a0) per
//     word (East-Asian/tag-aware WordWrap); port substitutes a space-tokenised
//     accumulation. Blocked on Mortar::WordWrap::CanBreakLineAt (unported).
//     TODO: v1.6.1 Font::GetStringHeight @0x0024c45c wrap path -- port FindAdvanceOfNextWord(v1.6.1 @0x0024c2a0) +
//     WordWrap::CanBreakLineAt; do NOT empirically tune the space-split heuristic.
float Font::GetStringHeight(Utf8StringIterator iter, float lineH, float maxWidth) {
    if (maxWidth <= 0.0f) {
        int newlines = 0;
        while (!iter.IsEmpty()) {
            if (iter.m_CurrentCodepoint == '\n') ++newlines;
            iter++;
        }
        return lineH * (float)(newlines + 1);
    }

    // DIFFERS: v1.6.1 binary @ 0x0024c45c wrap path calls FindAdvanceOfNextWord per word
    // (Mortar::WordWrap-aware); space-tokenised approximation used until WordWrap lands.
    int lines = 1;
    float curWidth = 0.0f;
    while (!iter.IsEmpty()) {
        uint32_t cp = iter.m_CurrentCodepoint;
        if (cp == '\n') {
            ++lines;
            curWidth = 0.0f;
            iter++;
            continue;
        }
        float wordW = 0.0f;
        Mortar::Utf8StringIterator wi = iter;
        while (!wi.IsEmpty() && wi.m_CurrentCodepoint != ' ' && wi.m_CurrentCodepoint != '\n') {
            CharTemplate* g = GetCharTemplate(wi.m_CurrentCodepoint);
            if (g) wordW += g->xadv;
            wi++;
        }
        if (curWidth > 0.0f && curWidth + wordW > maxWidth) {
            ++lines;
            curWidth = 0.0f;
        }
        curWidth += wordW;
        iter = wi;
        if (!iter.IsEmpty() && iter.m_CurrentCodepoint == ' ') {
            CharTemplate* g = GetCharTemplate(' ');
            if (g) curWidth += g->xadv;
            iter++;
        }
    }
    return lineH * (float)lines;
}

// Binary @ 0x0024c794 (v1.6.1; stale 0x001988a8 v1.5.x) -- by-value-iter ABI shape of MeasureString (same binary
// symbol as the const-ref overload above). Forwards to GetLineLength(iter, 0, NULL)
// exactly like the implemented overload.
float Font::MeasureString(Utf8StringIterator iter) { return GetLineLength(iter, 0.0f, nullptr); }

} // namespace Mortar
