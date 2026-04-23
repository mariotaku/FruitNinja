#!/usr/bin/env python3
"""Parse Fruit Ninja Bada .mmd files and dump JSON for the WebGL gallery.

Format (from src/engine/asset/ResourceLoader.cpp::Initialize):
  u32 skip          // usually "HBR0" ASCII, ignored
  u32 childCount
  [u32 childSize, childSize bytes recursive] * childCount
  u32 typeIdCount
  typeIdCount * u32 typeIds    // skipped
  u32 rawSize
  rawSize bytes rawData
"""
import json
import os
import struct
import sys
from pathlib import Path


HERE      = Path(__file__).resolve().parent
PROJ_ROOT = HERE.parent.parent                        # docs/model_gallery -> project root
MMD_ROOT  = PROJ_ROOT / "FruitNinjaBada" / "Data" / "models" / "Fruit"
OUT_DIR   = HERE                                      # write JSON sibling to this script


class Reader:
    """Byte-level reader scoped to a slice of a buffer."""
    __slots__ = ("data", "pos", "end")

    def __init__(self, data: bytes, start: int = 0, end: int | None = None):
        self.data = data
        self.pos = start
        self.end = end if end is not None else len(data)

    def remaining(self) -> int:
        return self.end - self.pos

    def _advance(self, n: int) -> int:
        p = self.pos
        if p + n > self.end:
            raise ValueError(f"read past end: need {n}, pos {p}, end {self.end}")
        self.pos += n
        return p

    def read(self, n: int) -> bytes:
        p = self._advance(n)
        return self.data[p:p + n]

    def u8(self) -> int:  return self.read(1)[0]
    def u16(self) -> int: return struct.unpack_from("<H", self.data, self._advance(2))[0]
    def u32(self) -> int: return struct.unpack_from("<I", self.data, self._advance(4))[0]
    def f32(self) -> float: return struct.unpack_from("<f", self.data, self._advance(4))[0]

    def string(self) -> str:
        ln = self.u16()
        if ln == 0:
            return ""
        return self.read(ln).decode("ascii", errors="replace")


class Resource:
    """One parsed HBR0 resource: rawData reader-ready + list of children."""
    __slots__ = ("raw", "children")

    def __init__(self, raw: bytes, children: list["Resource"]):
        self.raw = raw
        self.children = children

    def reader(self) -> Reader:
        return Reader(self.raw)


def parse_resource(data: bytes, start: int, end: int) -> Resource:
    """Initialize from a slice of data, matching ResourceLoader::Initialize."""
    r = Reader(data, start, end)
    r.u32()                          # skip u32 (often "HBR0")
    child_count = r.u32()
    if child_count > 1000:
        raise ValueError(f"absurd childCount {child_count}")
    children: list[Resource] = []
    for _ in range(child_count):
        if r.remaining() < 4:
            break
        csize = r.u32()
        if csize == 0 or r.remaining() < csize:
            break
        children.append(parse_resource(data, r.pos, r.pos + csize))
        r.pos += csize
    # typeIds (skipped)
    if r.remaining() >= 4:
        ticount = r.u32()
        if ticount <= 1000 and r.remaining() >= ticount * 4:
            r.pos += ticount * 4
    # raw
    raw = b""
    if r.remaining() >= 4:
        rsize = r.u32()
        if 0 < rsize <= r.remaining():
            raw = data[r.pos:r.pos + rsize]
    return Resource(raw, children)


# --- Mesh-specific decoders (mirror src/engine/asset/MeshManager.cpp) ---


def fmt_size(fmt: int) -> int:
    # Matches C++ FmtSize: 0→0, 1→1, 2→2, default→4.
    if fmt == 0: return 0
    if fmt == 1: return 1
    if fmt == 2: return 2
    return 4


def read_sub_resource_lookup(r: Reader) -> int | None:
    """Binary ReadSubResourceLookup — reads a 1-based u32 (NOT u16), 0 means 'no child'."""
    idx = r.u32()
    return (idx - 1) if idx > 0 else None


def parse_vertex_stream(data: bytes, start: int) -> dict:
    r = Reader(data, start)
    skip_count = r.u8()
    r.pos += skip_count * 4
    vert_decl = r.u32()
    vert_count = r.u32()

    tex_fmt    = (vert_decl >> 0)  & 0x3
    weight_fmt = (vert_decl >> 2)  & 0x7
    color_fmt  = (vert_decl >> 5)  & 0x3
    normal_fmt = (vert_decl >> 7)  & 0x3
    pos_fmt    = (vert_decl >> 9)  & 0x3
    morph      = (vert_decl >> 13) & 0x7

    offset = 0
    tex_off = offset
    tex_bytes = fmt_size(tex_fmt) * 2
    offset += tex_bytes
    weight_bytes = fmt_size(weight_fmt)
    color_off = offset if (weight_fmt == 7 and color_fmt == 3) else -1
    color_bytes = 4 if color_off >= 0 else 0
    offset += weight_bytes
    offset += fmt_size(color_fmt) * 3  # "color" slot reserved, actually normal
    normal_off = offset
    normal_bytes = fmt_size(normal_fmt) * 3
    offset += normal_bytes
    pos_bytes_dedicated = fmt_size(pos_fmt) * (morph + 1)
    pos_off = offset
    offset += pos_bytes_dedicated

    if pos_bytes_dedicated == 0 and normal_bytes >= 12:
        pos_off = normal_off
        pos_bytes = normal_bytes
        normal_bytes = 0
    else:
        pos_bytes = pos_bytes_dedicated

    stride = offset
    if stride == 0:
        raise ValueError("zero stride")

    vbase = r.pos
    if vbase + vert_count * stride > len(data):
        raise ValueError("vertex data short")

    positions = []
    uvs = []
    colors = []
    for v in range(vert_count):
        vp = vbase + v * stride
        if tex_bytes >= 8:
            u, t = struct.unpack_from("<ff", data, vp + tex_off)
            uvs.extend([u, t])
        else:
            uvs.extend([0.0, 0.0])
        if pos_bytes >= 12:
            px, py, pz = struct.unpack_from("<fff", data, vp + pos_off)
            positions.extend([px, py, pz])
        else:
            positions.extend([0.0, 0.0, 0.0])
        if color_bytes >= 4:
            colors.extend([
                data[vp + color_off + 0],
                data[vp + color_off + 1],
                data[vp + color_off + 2],
                data[vp + color_off + 3],
            ])
        else:
            colors.extend([255, 255, 255, 255])

    return {
        "decl":      f"0x{vert_decl:08x}",
        "stride":    stride,
        "vertCount": vert_count,
        "positions": positions,
        "uvs":       uvs,
        "colors":    colors,
    }


def parse_index_stream(data: bytes, start: int) -> tuple[dict, int]:
    r = Reader(data, start)
    r.pos += 2  # padding
    flags = r.u8()
    prim_hi = flags & 0xF0
    idx_nib = flags & 0x0F
    prim_name = {0x20: "TSTRIP", 0x30: "FAN", 0x40: "TRIS",
                 0x50: "LINES", 0x60: "POINTS"}.get(prim_hi, "TSTRIP")
    idx_count = r.u32()
    if idx_nib == 1:
        indices = list(struct.unpack_from(f"<{idx_count}H", data, r.pos))
        r.pos += idx_count * 2
    elif idx_nib == 2:
        indices = list(struct.unpack_from(f"<{idx_count}I", data, r.pos))
        r.pos += idx_count * 4
    else:
        indices = []
    return {
        "primType":   prim_name,
        "indexCount": idx_count,
        "indices":    indices,
    }, r.pos


def parse_material(res: Resource) -> dict:
    r = res.reader()
    mat_name = r.string()
    tex_idx = read_sub_resource_lookup(r)
    tex_path = None
    if tex_idx is not None and 0 <= tex_idx < len(res.children):
        tr = res.children[tex_idx].reader()
        _map_name = tr.string()
        tex_path = tr.string()
    return {"name": mat_name, "texPath": tex_path}


def skip_skeleton(r: Reader) -> None:
    """Matches ResourceLoader::ReadSkeleton — u32 boneCount, skip per-bone
    payload (name + parent(4) + mat44(64) + vec3(12) + quat(16) + mat3(36)
    = 132 + name). If boneCount==0 or >1024 the loader returns without
    consuming per-bone data — just the u32 count."""
    if r.remaining() < 4:
        return
    bone_count = r.u32()
    if bone_count == 0 or bone_count > 1024:
        return
    for _ in range(bone_count):
        if r.remaining() < 2:
            return
        name_len = r.u16()
        r.pos += name_len
        if r.remaining() < 132:
            return
        r.pos += 132


def parse_model(res: Resource) -> dict:
    r = res.reader()
    model_name = r.string()
    skip_skeleton(r)
    mesh_count = r.u32()
    meshes = []
    for _ in range(mesh_count):
        meshes.append(parse_mesh(r, res.children))
    return {
        "modelName": model_name,
        "meshes":    meshes,
    }


def parse_mesh(r: Reader, children: list[Resource]) -> dict:
    name = r.string()
    bone_count = r.u32()
    bones = []
    # Binary gate: `if (boneCount > 0 && boneCount < 256)`
    if 0 < bone_count < 256:
        for _ in range(bone_count):
            if r.remaining() < 2:
                break
            bname = r.string()
            if r.remaining() < 24:
                break
            bmin = struct.unpack_from("<fff", r.data, r._advance(12))
            bmax = struct.unpack_from("<fff", r.data, r._advance(12))
            bones.append({"name": bname, "min": list(bmin), "max": list(bmax)})

    mat_count = r.u32()
    materials = []
    for _ in range(mat_count):
        idx = read_sub_resource_lookup(r)
        if idx is not None and 0 <= idx < len(children):
            materials.append(parse_material(children[idx]))
        else:
            materials.append({"name": "?", "texPath": None})

    geom_count = r.u32()
    geometries = []
    for _ in range(geom_count):
        if r.remaining() < 4:
            break
        gidx = read_sub_resource_lookup(r)
        # matIndex is optional — binary guards `if (m_ReadPos + 2 <= DataSize())`
        mat_index = r.u16() if r.remaining() >= 2 else 0
        if gidx is None or not (0 <= gidx < len(children)):
            continue
        g = children[gidx].raw
        idx_block, vert_start = parse_index_stream(g, 0)
        vert_block = None
        if vert_start < len(g):
            try:
                vert_block = parse_vertex_stream(g, vert_start)
            except Exception as e:
                vert_block = {"error": str(e)}
        geometries.append({
            "materialIndex": mat_index,
            "index":         idx_block,
            "vertex":        vert_block,
        })

    return {
        "name":       name,
        "bones":      bones,
        "materials":  materials,
        "geometries": geometries,
    }


def parse_mmd(path: Path) -> dict:
    buf = path.read_bytes()
    root = parse_resource(buf, 0, len(buf))
    model = parse_model(root)
    model["file"] = path.name
    return model


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    files = sorted(MMD_ROOT.glob("*.mmd"))
    results = []
    failed = []
    for p in files:
        try:
            results.append(parse_mmd(p))
        except Exception as e:
            failed.append((p.name, str(e)))

    combined = {"count": len(results), "models": results}
    (OUT_DIR / "models.json").write_text(json.dumps(combined))

    print(f"[dump] parsed {len(results)} / {len(files)} models  |  {(OUT_DIR / 'models.json').stat().st_size:,} bytes")
    if failed:
        print(f"[dump] failed: {len(failed)}")
        for n, e in failed[:5]:
            print(f"  - {n}: {e}")

    # Sanity check
    for m in results:
        if m.get("file") == "bomb.mmd":
            print(f"[dump] bomb.mmd: meshes={len(m['meshes'])}")
            for i, mesh in enumerate(m["meshes"]):
                print(f"  mesh[{i}] '{mesh['name']}' geoms={len(mesh['geometries'])}")
                for j, g in enumerate(mesh["geometries"]):
                    v = g["vertex"] or {}
                    idx = g["index"]
                    print(f"    g[{j}] mat={g['materialIndex']} vCount={v.get('vertCount','?')} "
                          f"iCount={idx.get('indexCount','?')} prim={idx.get('primType','?')}")


if __name__ == "__main__":
    main()
