// .tex → .png converter + HTML gallery generator
// Uses raw deflate via zlib (available in MSYS2)
// Build: g++ -O2 -o tex2png.exe tex2png.cpp -lz

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <zlib.h>

// CRC32 for PNG chunks
static uint32_t png_crc32(const uint8_t* data, size_t len) {
    return (uint32_t)crc32(0L, data, (uInt)len);
}

static void write_be32(uint8_t* p, uint32_t v) {
    p[0] = (v >> 24) & 0xFF; p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF; p[3] = v & 0xFF;
}

static void write_png_chunk(FILE* f, const char* type, const uint8_t* data, size_t len) {
    uint8_t hdr[4]; write_be32(hdr, (uint32_t)len);
    fwrite(hdr, 1, 4, f);
    fwrite(type, 1, 4, f);
    if (len > 0) fwrite(data, 1, len, f);
    // CRC over type + data
    std::vector<uint8_t> crcbuf(4 + len);
    memcpy(crcbuf.data(), type, 4);
    if (len > 0) memcpy(crcbuf.data() + 4, data, len);
    uint32_t crc = png_crc32(crcbuf.data(), crcbuf.size());
    uint8_t crcb[4]; write_be32(crcb, crc);
    fwrite(crcb, 1, 4, f);
}

static bool write_png(const char* path, int w, int h, const uint8_t* rgba) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;

    // PNG signature
    const uint8_t sig[8] = {137,80,78,71,13,10,26,10};
    fwrite(sig, 1, 8, f);

    // IHDR
    uint8_t ihdr[13];
    write_be32(ihdr, w);
    write_be32(ihdr + 4, h);
    ihdr[8] = 8;  // bit depth
    ihdr[9] = 6;  // RGBA
    ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    write_png_chunk(f, "IHDR", ihdr, 13);

    // IDAT: filter byte (0) + row data, then deflate
    size_t rawSize = (size_t)h * (1 + w * 4);
    std::vector<uint8_t> raw(rawSize);
    for (int y = 0; y < h; y++) {
        raw[y * (1 + w * 4)] = 0; // filter: none
        memcpy(&raw[y * (1 + w * 4) + 1], &rgba[y * w * 4], w * 4);
    }

    uLongf compSize = compressBound((uLong)rawSize);
    std::vector<uint8_t> comp(compSize);
    compress2(comp.data(), &compSize, raw.data(), (uLong)rawSize, 6);
    write_png_chunk(f, "IDAT", comp.data(), compSize);

    // IEND
    write_png_chunk(f, "IEND", NULL, 0);
    fclose(f);
    return true;
}

struct TexInfo {
    std::string name;
    int w, h, format;
};

static bool convert_tex(const char* inpath, const char* outpath, int* outW, int* outH, int* outFmt) {
    FILE* f = fopen(inpath, "rb");
    if (!f) return false;

    uint8_t header[12];
    if (fread(header, 1, 12, f) != 12) { fclose(f); return false; }

    int w = 1 << header[0];
    int h = 1 << header[1];
    int format = header[2];
    *outW = w; *outH = h; *outFmt = format;

    fseek(f, 0, SEEK_END);
    long dataSize = ftell(f) - 12;
    fseek(f, 12, SEEK_SET);

    std::vector<uint8_t> raw(dataSize);
    fread(raw.data(), 1, dataSize, f);
    fclose(f);

    std::vector<uint8_t> rgba(w * h * 4);

    switch (format) {
        case 0x00: // RGB888
            for (int i = 0; i < w * h; i++) {
                rgba[i*4+0] = raw[i*3+0]; rgba[i*4+1] = raw[i*3+1];
                rgba[i*4+2] = raw[i*3+2]; rgba[i*4+3] = 255;
            }
            break;
        case 0x01: // RGBA8888
            memcpy(rgba.data(), raw.data(), w * h * 4);
            break;
        case 0x0f: // RGBA5551
            for (int i = 0; i < w * h; i++) {
                uint16_t p = (uint16_t)(raw[i*2] | (raw[i*2+1] << 8));
                rgba[i*4+0] = (uint8_t)(((p >> 11) & 0x1F) * 255 / 31);
                rgba[i*4+1] = (uint8_t)(((p >>  6) & 0x1F) * 255 / 31);
                rgba[i*4+2] = (uint8_t)(((p >>  1) & 0x1F) * 255 / 31);
                rgba[i*4+3] = (p & 1) ? 255 : 0;
            }
            break;
        case 0x10: // RGBA4444
            for (int i = 0; i < w * h; i++) {
                uint16_t p = (uint16_t)(raw[i*2] | (raw[i*2+1] << 8));
                rgba[i*4+0] = (uint8_t)(((p >> 12) & 0xF) * 17);
                rgba[i*4+1] = (uint8_t)(((p >>  8) & 0xF) * 17);
                rgba[i*4+2] = (uint8_t)(((p >>  4) & 0xF) * 17);
                rgba[i*4+3] = (uint8_t)(((p >>  0) & 0xF) * 17);
            }
            break;
        case 0x11: // RGB565
            for (int i = 0; i < w * h; i++) {
                uint16_t p = (uint16_t)(raw[i*2] | (raw[i*2+1] << 8));
                rgba[i*4+0] = (uint8_t)(((p >> 11) & 0x1F) * 255 / 31);
                rgba[i*4+1] = (uint8_t)(((p >>  5) & 0x3F) * 255 / 63);
                rgba[i*4+2] = (uint8_t)(((p >>  0) & 0x1F) * 255 / 31);
                rgba[i*4+3] = 255;
            }
            break;
        default:
            return false;
    }

    return write_png(outpath, w, h, rgba.data());
}

static const char* fmt_name(int fmt) {
    switch (fmt) {
        case 0x00: return "RGB888";
        case 0x01: return "RGBA8888";
        case 0x0f: return "RGBA5551";
        case 0x10: return "RGBA4444";
        case 0x11: return "RGB565";
        default: return "unknown";
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input_dir> <output_dir>\n", argv[0]);
        return 1;
    }
    const char* indir = argv[1];
    const char* outdir = argv[2];
    mkdir(outdir);

    DIR* d = opendir(indir);
    if (!d) { fprintf(stderr, "Cannot open: %s\n", indir); return 1; }

    std::vector<TexInfo> textures;
    int converted = 0, failed = 0;

    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        const char* name = ent->d_name;
        int len = strlen(name);
        if (len < 5 || strcmp(name + len - 4, ".tex") != 0) continue;

        char inpath[512], outpath[512], basename[256];
        snprintf(inpath, sizeof(inpath), "%s/%s", indir, name);
        strncpy(basename, name, len - 4);
        basename[len - 4] = '\0';
        snprintf(outpath, sizeof(outpath), "%s/%s.png", outdir, basename);

        int w, h, fmt;
        if (convert_tex(inpath, outpath, &w, &h, &fmt)) {
            TexInfo ti; ti.name = basename; ti.w = w; ti.h = h; ti.format = fmt;
            textures.push_back(ti);
            converted++;
        } else {
            failed++;
        }
    }
    closedir(d);

    // Sort by name
    std::sort(textures.begin(), textures.end(),
        [](const TexInfo& a, const TexInfo& b) { return a.name < b.name; });

    // Generate HTML gallery
    char htmlpath[512];
    snprintf(htmlpath, sizeof(htmlpath), "%s/gallery.html", outdir);
    FILE* html = fopen(htmlpath, "w");
    fprintf(html, "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"UTF-8\">\n");
    fprintf(html, "<title>Fruit Ninja Textures (%d)</title>\n", (int)textures.size());
    fprintf(html, "<style>\n");
    fprintf(html, "body { background: #1a1a2e; color: #eee; font-family: monospace; margin: 20px; }\n");
    fprintf(html, "h1 { color: #e94560; }\n");
    fprintf(html, ".grid { display: flex; flex-wrap: wrap; gap: 12px; }\n");
    fprintf(html, ".card { background: #16213e; border: 1px solid #333; border-radius: 6px; padding: 8px; text-align: center; width: 180px; }\n");
    fprintf(html, ".card img { max-width: 170px; max-height: 170px; background: repeating-conic-gradient(#333 0%% 25%%, #222 0%% 50%%) 50%%/16px 16px; image-rendering: pixelated; }\n");
    fprintf(html, ".card .name { font-size: 11px; color: #8cf; margin-top: 4px; word-break: break-all; }\n");
    fprintf(html, ".card .info { font-size: 10px; color: #666; }\n");
    fprintf(html, "input { background: #16213e; color: #eee; border: 1px solid #444; padding: 6px 12px; margin-bottom: 12px; width: 300px; font-size: 14px; }\n");
    fprintf(html, "</style>\n</head>\n<body>\n");
    fprintf(html, "<h1>Fruit Ninja Textures (%d files)</h1>\n", (int)textures.size());
    fprintf(html, "<input type=\"text\" id=\"filter\" placeholder=\"Filter by name...\" oninput=\"filterCards()\">\n");
    fprintf(html, "<div class=\"grid\" id=\"grid\">\n");

    for (size_t i = 0; i < textures.size(); i++) {
        const TexInfo& t = textures[i];
        fprintf(html, "<div class=\"card\" data-name=\"%s\">\n", t.name.c_str());
        fprintf(html, "  <img src=\"%s.png\" loading=\"lazy\">\n", t.name.c_str());
        fprintf(html, "  <div class=\"name\">%s</div>\n", t.name.c_str());
        fprintf(html, "  <div class=\"info\">%dx%d %s (0x%02x)</div>\n", t.w, t.h, fmt_name(t.format), t.format);
        fprintf(html, "</div>\n");
    }

    fprintf(html, "</div>\n");
    fprintf(html, "<script>\n");
    fprintf(html, "function filterCards() {\n");
    fprintf(html, "  var q = document.getElementById('filter').value.toLowerCase();\n");
    fprintf(html, "  var cards = document.querySelectorAll('.card');\n");
    fprintf(html, "  cards.forEach(function(c) {\n");
    fprintf(html, "    c.style.display = c.dataset.name.toLowerCase().indexOf(q) >= 0 ? '' : 'none';\n");
    fprintf(html, "  });\n");
    fprintf(html, "}\n");
    fprintf(html, "</script>\n");
    fprintf(html, "</body>\n</html>\n");
    fclose(html);

    printf("Done: %d converted, %d failed\nGallery: %s\n", converted, failed, htmlpath);
    return 0;
}
