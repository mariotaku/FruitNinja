#include "mesh.h"
#include <cstdio>
#include <cstring>
#include <vector>

// Scan for "HBR0" magic in buffer, return offset or -1
static long find_hbr0(const uint8_t* data, long start, long end) {
    for (long pos = start; pos <= end - 4; pos++) {
        if (memcmp(data + pos, "HBR0", 4) == 0) return pos;
    }
    return -1;
}

static uint16_t buf_u16(const uint8_t* p) { return p[0] | (p[1] << 8); }
static uint32_t buf_u32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); }
static float buf_float(const uint8_t* p) { float v; memcpy(&v, p, 4); return v; }

static int fmt_size(int fmt) {
    switch (fmt) {
        case 0: return 0;
        case 1: return 1;
        case 2: return 2;
        default: return 4;
    }
}

Mesh::Mesh() : vbo(0), ibo(0), index_count(0), prim_type(GL_TRIANGLES) {}

bool Mesh::load(const std::string& mmd_path) {
    FILE* f = fopen(mmd_path.c_str(), "rb");
    if (!f) {
        fprintf(stderr, "Failed to open: %s\n", mmd_path.c_str());
        return false;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Read entire file into memory
    std::vector<uint8_t> data(file_size);
    if (fread(data.data(), 1, file_size, f) != (size_t)file_size) {
        fclose(f);
        fprintf(stderr, "Failed to read file: %s\n", mmd_path.c_str());
        return false;
    }
    fclose(f);

    // Find the LAST HBR0 block (geometry data with 16-byte header)
    long geom_hbr0 = -1;
    {
        long pos = 0;
        while (true) {
            long found = find_hbr0(data.data(), pos, file_size);
            if (found < 0) break;
            geom_hbr0 = found;
            pos = found + 4;
        }
    }

    if (geom_hbr0 < 0) {
        fprintf(stderr, "No HBR0 blocks found in: %s\n", mmd_path.c_str());
        return false;
    }

    // 16-byte header: magic(4) + type(4) + flags(4) + size(4)
    uint32_t type = buf_u32(data.data() + geom_hbr0 + 4);
    uint32_t flags = buf_u32(data.data() + geom_hbr0 + 8);
    uint32_t size = buf_u32(data.data() + geom_hbr0 + 12);
    long payload = geom_hbr0 + 16;

    printf("Geometry block at 0x%04lx: type=%u flags=%u size=%u\n",
           geom_hbr0, type, flags, size);

    if (size < 100 || payload + (long)size > file_size) {
        fprintf(stderr, "Invalid geometry block in: %s\n", mmd_path.c_str());
        return false;
    }

    const uint8_t* d = data.data();
    long p = payload;

    // Skip 2 unknown bytes
    p += 2;

    // Index flags byte
    uint8_t idx_flags = d[p++];
    uint8_t prim_bits = idx_flags & 0xF0;

    switch (prim_bits) {
        case 0x20: prim_type = GL_TRIANGLE_STRIP; break;
        case 0x30: prim_type = GL_TRIANGLE_STRIP; break;
        case 0x40: prim_type = GL_TRIANGLES; break;
        default:   prim_type = GL_TRIANGLE_STRIP; break;
    }

    // Index count (uint32)
    uint32_t idx_count = buf_u32(d + p); p += 4;
    if (idx_count == 0 || idx_count > 100000) {
        fprintf(stderr, "Bad index count %u in: %s\n", idx_count, mmd_path.c_str());
        return false;
    }

    // Read uint16 indices
    std::vector<uint16_t> indices(idx_count);
    for (uint32_t i = 0; i < idx_count; i++) {
        indices[i] = buf_u16(d + p); p += 2;
    }

    // Vertex data: skip_count byte, vert_decl uint32, vert_count uint32
    uint8_t skip_count = d[p++];
    p += skip_count * 4;

    uint32_t vert_decl = buf_u32(d + p); p += 4;
    uint32_t vert_count = buf_u32(d + p); p += 4;

    printf("  idx_flags=0x%02x idx_count=%u vert_decl=0x%08x vert_count=%u\n",
           idx_flags, idx_count, vert_decl, vert_count);

    int tex_fmt    = (vert_decl >> 0) & 0x3;
    int color_fmt  = (vert_decl >> 5) & 0x3;
    int normal_fmt = (vert_decl >> 7) & 0x3;
    int pos_fmt    = (vert_decl >> 9) & 0x3;

    if (pos_fmt == 0) pos_fmt = 3;

    // Color format: 0=none(0), 1=565(2), 2=5551(2), 3=8888(4)
    int color_bytes = 0;
    if (color_fmt == 1 || color_fmt == 2) color_bytes = 2;
    else if (color_fmt == 3) color_bytes = 4;

    int vert_stride = fmt_size(tex_fmt) * 2 +
                      color_bytes +
                      fmt_size(normal_fmt) * 3 +
                      fmt_size(pos_fmt) * 3;

    printf("  stride=%d (tex=%d*2 + color=%d + normal=%d*3 + pos=%d*3)\n",
           vert_stride, fmt_size(tex_fmt), color_bytes, fmt_size(normal_fmt), fmt_size(pos_fmt));

    if (vert_stride == 0 || vert_count == 0 || vert_count > 100000) {
        fprintf(stderr, "Bad vertex data in: %s\n", mmd_path.c_str());
        return false;
    }

    long vert_data_size = (long)vert_count * vert_stride;
    if (p + vert_data_size > file_size) {
        fprintf(stderr, "Vertex data exceeds file in: %s (need %ld at 0x%lx, file=%ld)\n",
                mmd_path.c_str(), vert_data_size, p, file_size);
        return false;
    }

    // Convert to MeshVertex (layout: texcoord, color, normal, position)
    std::vector<MeshVertex> vertices(vert_count);
    for (uint32_t i = 0; i < vert_count; i++) {
        const uint8_t* vp = d + p + i * vert_stride;
        int off = 0;
        MeshVertex& v = vertices[i];

        // Texcoord
        if (tex_fmt == 3) {
            memcpy(&v.u, vp + off, 4); off += 4;
            memcpy(&v.v, vp + off, 4); off += 4;
        } else if (tex_fmt == 2) {
            v.u = (float)*(const uint16_t*)(vp + off) / 32767.0f; off += 2;
            v.v = (float)*(const uint16_t*)(vp + off) / 32767.0f; off += 2;
        } else {
            v.u = v.v = 0.0f;
        }

        // Color
        if (color_fmt == 3) { // 8888 RGBA
            v.r = vp[off]; v.g = vp[off+1]; v.b = vp[off+2]; v.a = vp[off+3];
            off += 4;
        } else if (color_fmt == 1 || color_fmt == 2) { // 565/5551/4444
            v.r = v.g = v.b = v.a = 255;
            off += 2;
        } else {
            v.r = v.g = v.b = v.a = 255;
        }

        // Normal
        if (normal_fmt == 3) {
            memcpy(&v.nx, vp + off, 4); off += 4;
            memcpy(&v.ny, vp + off, 4); off += 4;
            memcpy(&v.nz, vp + off, 4); off += 4;
        } else if (normal_fmt == 2) {
            v.nx = (float)*(const int16_t*)(vp + off) / 32767.0f; off += 2;
            v.ny = (float)*(const int16_t*)(vp + off) / 32767.0f; off += 2;
            v.nz = (float)*(const int16_t*)(vp + off) / 32767.0f; off += 2;
        } else {
            v.nx = 0; v.ny = 1; v.nz = 0;
        }

        // Position
        if (pos_fmt == 3) {
            memcpy(&v.px, vp + off, 4); off += 4;
            memcpy(&v.py, vp + off, 4); off += 4;
            memcpy(&v.pz, vp + off, 4); off += 4;
        } else if (pos_fmt == 2) {
            v.px = (float)*(const int16_t*)(vp + off); off += 2;
            v.py = (float)*(const int16_t*)(vp + off); off += 2;
            v.pz = (float)*(const int16_t*)(vp + off); off += 2;
        } else {
            v.px = v.py = v.pz = 0;
        }
    }

    index_count = (int)indices.size();

    printf("Loaded mesh: %u verts (stride=%d), %u indices, prim=0x%02x\n",
           vert_count, vert_stride, idx_count, prim_bits);

    // Upload to GPU
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(vertices.size() * sizeof(MeshVertex)),
                 vertices.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 (GLsizeiptr)(indices.size() * sizeof(uint16_t)),
                 indices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    return true;
}

void Mesh::destroy() {
    if (vbo) { glDeleteBuffers(1, &vbo); vbo = 0; }
    if (ibo) { glDeleteBuffers(1, &ibo); ibo = 0; }
    index_count = 0;
}

void Mesh::draw() {
    if (!vbo || !ibo || index_count == 0) return;

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);

    int stride = sizeof(MeshVertex);

    // a_pos (location 0): offset 0
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);

    // a_normal (location 1): offset 12
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)12);

    // a_color (location 2): offset 24
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)24);

    // a_uv (location 3): offset 28
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, (void*)28);

    glDrawElements(prim_type, index_count, GL_UNSIGNED_SHORT, (void*)0);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(3);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}
