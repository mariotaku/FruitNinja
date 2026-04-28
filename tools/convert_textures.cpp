// Standalone .tex → .png converter
// Build: g++ -o convert_textures convert_textures.cpp -lz
// Usage: convert_textures <input_dir> <output_dir>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>
#include <dirent.h>
#include <sys/stat.h>

// Minimal TGA writer (no dependencies)
static bool write_tga(const char* path, int w, int h, const uint8_t* rgba) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    uint8_t hdr[18] = {};
    hdr[2] = 2; // uncompressed true-color
    hdr[12] = w & 0xFF; hdr[13] = (w >> 8) & 0xFF;
    hdr[14] = h & 0xFF; hdr[15] = (h >> 8) & 0xFF;
    hdr[16] = 32; // bits per pixel
    hdr[17] = 0x28; // top-left origin + 8 alpha bits
    fwrite(hdr, 1, 18, f);
    // TGA stores BGRA
    for (int i = 0; i < w * h; i++) {
        uint8_t bgra[4] = { rgba[i*4+2], rgba[i*4+1], rgba[i*4+0], rgba[i*4+3] };
        fwrite(bgra, 1, 4, f);
    }
    fclose(f);
    return true;
}

static bool convert_tex(const char* inpath, const char* outpath) {
    FILE* f = fopen(inpath, "rb");
    if (!f) { fprintf(stderr, "Cannot open: %s\n", inpath); return false; }

    uint8_t header[12];
    if (fread(header, 1, 12, f) != 12) { fclose(f); return false; }

    int widthLog2  = header[0];
    int heightLog2 = header[1];
    int format     = header[2];
    int w = 1 << widthLog2;
    int h = 1 << heightLog2;

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
                rgba[i*4+0] = raw[i*3+0];
                rgba[i*4+1] = raw[i*3+1];
                rgba[i*4+2] = raw[i*3+2];
                rgba[i*4+3] = 255;
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
            fprintf(stderr, "Unsupported format 0x%02x: %s\n", format, inpath);
            return false;
    }

    return write_tga(outpath, w, h, rgba.data());
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
    if (!d) { fprintf(stderr, "Cannot open dir: %s\n", indir); return 1; }

    int converted = 0, failed = 0, skipped = 0;
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        const char* name = ent->d_name;
        int len = strlen(name);
        if (len < 5 || strcmp(name + len - 4, ".tex") != 0) continue;

        char inpath[512], outpath[512];
        snprintf(inpath, sizeof(inpath), "%s/%s", indir, name);

        // Replace .tex with .tga
        char basename[256];
        strncpy(basename, name, len - 4);
        basename[len - 4] = '\0';
        snprintf(outpath, sizeof(outpath), "%s/%s.tga", outdir, basename);

        if (convert_tex(inpath, outpath)) {
            converted++;
        } else {
            failed++;
        }
    }
    closedir(d);

    printf("Done: %d converted, %d failed\n", converted, failed);
    return 0;
}
