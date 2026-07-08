#!/usr/bin/env python3
"""Shared binary writers + VRAM allocator for the PSX-authentic asset pipeline.

Byte layouts are pinned in docs/file_formats.md ("Engine-native binary formats
v1"). Everything is little-endian and tightly packed; strings are UTF-8,
NUL-padded to their fixed size. The C++ loaders in engine/assets/assets.cpp
read exactly these layouts -- keep both sides in sync with the document.

All output here is deterministic: same inputs -> same bytes.
"""

import struct

# --- magics / versions -------------------------------------------------------

MAGIC_TEX = b"PXTX"
MAGIC_MESH = b"PXMS"
MAGIC_LEVEL = b"PXLV"
MAGIC_MANIFEST = b"PXMF"
FORMAT_VERSION = 1

# --- texture formats (engine/renderer/vram.h TexFormat) ----------------------

TEX_4BIT, TEX_8BIT, TEX_15BIT = 0, 1, 2
FORMAT_IDS = {"indexed_4": TEX_4BIT, "indexed_8": TEX_8BIT, "direct_15": TEX_15BIT}
FORMAT_NAMES = {v: k for k, v in FORMAT_IDS.items()}

# --- mesh prim types (engine/assets/assets.h MeshPrimType) -------------------

MP_F3, MP_G3, MP_FT3, MP_GT3, MP_F4, MP_G4, MP_FT4, MP_GT4 = range(8)

# --- asset types (engine/assets/assets.h AssetType) --------------------------

ASSET_TEXTURE, ASSET_MESH, ASSET_SOUND, ASSET_LEVEL = 0, 1, 2, 3

# --- world conventions --------------------------------------------------------

WORLD_SCALE = 256          # 1.0 source units = 256 engine units
FX12_ONE = 4096            # 4.12 fixed point
PSX_FULL_TURN = 4096       # PS1 angle units per full revolution

NO_CLUT = 0xFFFF


class PackError(Exception):
    """Raised on any contract violation (range, size, budget...)."""


# --- color helpers ------------------------------------------------------------

def rgb888_to_15(r, g, b, stp=False):
    """RGB888 -> PS1 15-bit halfword: c = (b5<<10)|(g5<<5)|r5, bit15 = STP."""
    c = (((b >> 3) & 31) << 10) | (((g >> 3) & 31) << 5) | ((r >> 3) & 31)
    if stp:
        c |= 0x8000
    return c


def expand5(v5):
    """Expand a 5-bit channel to 8 bits ((v<<3)|(v>>2)), matching vram.h."""
    return ((v5 << 3) | (v5 >> 2)) & 0xFF


def rgb15_to_888(c):
    """15-bit halfword -> (r,g,b) 8-bit tuple (STP bit ignored)."""
    return (expand5(c & 31), expand5((c >> 5) & 31), expand5((c >> 10) & 31))


# --- string field packing -----------------------------------------------------

def pack_name(s, size=32):
    """UTF-8 encode + validate; struct 'Ns' pads with NULs. Must leave >=1 NUL."""
    b = s.encode("utf-8")
    if len(b) > size - 1:
        raise PackError("string %r too long for %d-byte field (max %d bytes)"
                        % (s, size, size - 1))
    return b


def pack_path(s, size=64):
    """Path field: forward slashes enforced, NUL-padded to 64 bytes."""
    return pack_name(s.replace("\\", "/"), size)


# --- integer range checks -----------------------------------------------------

def check_i16(v, what="value"):
    if not -32768 <= v <= 32767:
        raise PackError("%s = %d out of i16 range" % (what, v))
    return v


def check_i32(v, what="value"):
    if not -2147483648 <= v <= 2147483647:
        raise PackError("%s = %d out of i32 range" % (what, v))
    return v


def clamp_u8(v):
    return 0 if v < 0 else (255 if v > 255 else int(v))


def storage_width_hw(fmt, width_px):
    """Storage width in VRAM halfwords: 4-bit w/4, 8-bit w/2, 15-bit w."""
    if fmt == TEX_4BIT:
        return width_px // 4
    if fmt == TEX_8BIT:
        return width_px // 2
    return width_px


# --- VRAM allocator -----------------------------------------------------------

VRAM_W = 1024
VRAM_H = 512
VRAM_TEX_X0 = 320          # columns < 320 reserved for display buffers
VRAM_TEX_Y_MAX = 504       # texture region y in [0, 504)
VRAM_CLUT_Y0 = 504         # CLUT strip y in [504, 512)
VRAM_CLUT_Y1 = 512
VRAM_BUDGET_BYTES = 704 * 1024   # hard budget (vram.h VRAM_TEX_BUDGET region)


class VramAllocator:
    """Packs texture rects + CLUT rows into simulated VRAM (halfword coords).

    Texture region: x in [320, 1024), y in [0, 504). Shelf packer: callers
    SHOULD allocate in height-descending order (import_textures does); each
    rect is placed at the running cursor, a new shelf starts when the current
    row is full, and allocation fails once y + h > 504.

    CLUT strip: y in [504, 512), x advancing from 320, starts aligned to 16
    halfwords (entries are 16 or 256, so alignment is preserved).

    Total accounted bytes (pixels + CLUTs) may never exceed 704 KiB.
    """

    def __init__(self):
        self.shelf_y = 0
        self.shelf_h = 0
        self.cursor_x = VRAM_TEX_X0
        self.clut_x = VRAM_TEX_X0
        self.clut_y = VRAM_CLUT_Y0
        self.used_bytes = 0
        self.allocs = []   # (kind, x, y, w_hw, h) for debugging / reports

    # -- internal ---------------------------------------------------------
    def _track(self, kind, x, y, w_hw, h):
        bytes_ = w_hw * h * 2
        self.used_bytes += bytes_
        if self.used_bytes > VRAM_BUDGET_BYTES:
            raise PackError(
                "VRAM budget exceeded: %d bytes used > %d byte budget (704 KiB)"
                % (self.used_bytes, VRAM_BUDGET_BYTES))
        self.allocs.append((kind, x, y, w_hw, h))
        return bytes_

    # -- textures ----------------------------------------------------------
    def alloc_texture(self, w_hw, h):
        """Allocate a w_hw x h halfword rect; returns (vram_x, vram_y)."""
        if w_hw <= 0 or h <= 0:
            raise PackError("invalid texture rect %dx%d" % (w_hw, h))
        if w_hw > VRAM_W - VRAM_TEX_X0:
            raise PackError("texture storage width %d halfwords exceeds region "
                            "width %d" % (w_hw, VRAM_W - VRAM_TEX_X0))
        if self.cursor_x + w_hw > VRAM_W:        # row full -> new shelf
            self.shelf_y += self.shelf_h
            self.shelf_h = 0
            self.cursor_x = VRAM_TEX_X0
        if self.shelf_y + h > VRAM_TEX_Y_MAX:
            raise PackError(
                "VRAM texture region overflow: shelf y=%d + h=%d > %d"
                % (self.shelf_y, h, VRAM_TEX_Y_MAX))
        x, y = self.cursor_x, self.shelf_y
        self.cursor_x += w_hw
        if h > self.shelf_h:
            self.shelf_h = h
        self._track("tex", x, y, w_hw, h)
        return x, y

    # -- CLUTs ---------------------------------------------------------------
    def alloc_clut(self, nentries):
        """Allocate a CLUT row of 16 or 256 halfwords; returns (clut_x, clut_y)."""
        if nentries not in (16, 256):
            raise PackError("CLUT length must be 16 or 256, got %d" % nentries)
        x = (self.clut_x + 15) & ~15             # keep 16-halfword alignment
        y = self.clut_y
        if x + nentries > VRAM_W:                # row full -> next strip row
            y += 1
            x = VRAM_TEX_X0
        if y >= VRAM_CLUT_Y1:
            raise PackError("VRAM CLUT strip full (rows %d..%d exhausted)"
                            % (VRAM_CLUT_Y0, VRAM_CLUT_Y1 - 1))
        self.clut_x = x + nentries
        self.clut_y = y
        self._track("clut", x, y, nentries, 1)
        return x, y

    # -- reporting -------------------------------------------------------------
    @property
    def budget_bytes(self):
        return VRAM_BUDGET_BYTES

    def usage_string(self):
        pct = 100.0 * self.used_bytes / VRAM_BUDGET_BYTES
        return "%d / %d bytes (%.1f%% of 704 KiB)" % (
            self.used_bytes, VRAM_BUDGET_BYTES, pct)


# --- .texbin writer ('PXTX') ---------------------------------------------------

_TEX_HDR = "<4sI32sBB7HII"          # 64 bytes
assert struct.calcsize(_TEX_HDR) == 64


def write_texbin(path, *, name, fmt, width, height, vram_x, vram_y,
                 clut_x, clut_y, clut_len, vram_cost_bytes, clut, payload):
    """Write a .texbin. `clut` = iterable of halfwords, `payload` = bytes."""
    if fmt not in (TEX_4BIT, TEX_8BIT, TEX_15BIT):
        raise PackError("bad texture format id %r" % (fmt,))
    if clut_len not in (0, 16, 256):
        raise PackError("clut_len must be 0/16/256, got %d" % clut_len)
    clut = list(clut)
    if len(clut) != clut_len:
        raise PackError("clut has %d entries, header says %d" % (len(clut), clut_len))
    hdr = struct.pack(_TEX_HDR, MAGIC_TEX, FORMAT_VERSION, pack_name(name),
                      fmt, 0, width, height, vram_x, vram_y,
                      clut_x, clut_y, clut_len, vram_cost_bytes, len(payload))
    with open(path, "wb") as f:
        f.write(hdr)
        if clut_len:
            f.write(struct.pack("<%dH" % clut_len, *clut))
        f.write(payload)


# --- .meshbin writer ('PXMS') ---------------------------------------------------

_MESH_HDR = "<4sI32sIIIIIi"         # 64 bytes
_MESH_PRIM = "<BBH4H8B12BhH"        # 36 bytes
assert struct.calcsize(_MESH_HDR) == 64
assert struct.calcsize(_MESH_PRIM) == 36


def write_meshbin(path, *, name, verts, norms, tex_names, prims,
                  tri_count, radius):
    """verts/norms: [(x,y,z) i16]; prims: dicts with type/flags/tex_index/
    vi[4]/uv[4][2]/rgb[4][3]/sort_bias."""
    nverts, nnorms = len(verts), len(norms)
    if nnorms not in (0, nverts):
        raise PackError("nnorms (%d) must be 0 or == nverts (%d)" % (nnorms, nverts))
    if nverts > 0xFFFF:
        raise PackError("too many vertices (%d) for u16 indices" % nverts)
    with open(path, "wb") as f:
        f.write(struct.pack(_MESH_HDR, MAGIC_MESH, FORMAT_VERSION,
                            pack_name(name), nverts, nnorms, len(prims),
                            len(tex_names), tri_count, check_i32(int(radius), "radius")))
        for v in verts:
            f.write(struct.pack("<3h", *v))
        for n in norms:
            f.write(struct.pack("<3h", *n))
        for t in tex_names:
            f.write(struct.pack("<32s", pack_name(t)))
        for p in prims:
            uv = [c for pair in p["uv"] for c in pair]
            rgb = [c for trip in p["rgb"] for c in trip]
            f.write(struct.pack(_MESH_PRIM, p["type"], p.get("flags", 0),
                                p["tex_index"], *(list(p["vi"]) + uv + rgb
                                + [p.get("sort_bias", 0), 0])))


# --- .lvlbin writer ('PXLV') -----------------------------------------------------

_LVL_HDR = "<4sI32sI3i3hHB3BiiB3B3BB3hH3BB"   # 96 bytes
_LVL_OBJ = "<32s3i3hH3i"                       # 64 bytes
assert struct.calcsize(_LVL_HDR) == 96
assert struct.calcsize(_LVL_OBJ) == 64


def write_lvlbin(path, *, name, cam_pos, cam_rot, fog, light, clear_color,
                 objects):
    """fog: {enabled,r,g,b,start,end}; light: {enabled,ambient[3],diffuse[3],
    dir[3]}; objects: [{mesh,pos i32[3],rot i16[3],scale fx12 i32[3]}]."""
    hdr = struct.pack(
        _LVL_HDR, MAGIC_LEVEL, FORMAT_VERSION, pack_name(name), len(objects),
        cam_pos[0], cam_pos[1], cam_pos[2],
        cam_rot[0], cam_rot[1], cam_rot[2], 0,
        fog["enabled"], fog["r"], fog["g"], fog["b"], fog["start"], fog["end"],
        light["enabled"],
        light["ambient"][0], light["ambient"][1], light["ambient"][2],
        light["diffuse"][0], light["diffuse"][1], light["diffuse"][2], 0,
        light["dir"][0], light["dir"][1], light["dir"][2], 0,
        clear_color[0], clear_color[1], clear_color[2], 0)
    with open(path, "wb") as f:
        f.write(hdr)
        for o in objects:
            f.write(struct.pack(_LVL_OBJ, pack_name(o["mesh"]),
                                o["pos"][0], o["pos"][1], o["pos"][2],
                                o["rot"][0], o["rot"][1], o["rot"][2], 0,
                                o["scale"][0], o["scale"][1], o["scale"][2]))


# --- manifest.bin writer ('PXMF') --------------------------------------------------

_MF_HDR = "<4sII"                   # 12 bytes
_MF_REC = "<BBBB32s64sII"           # 108 bytes
assert struct.calcsize(_MF_HDR) == 12
assert struct.calcsize(_MF_REC) == 108


def write_manifest(path, records):
    """records: [{type,is_music,loop_whole,name,path,file_bytes}] in final order."""
    with open(path, "wb") as f:
        f.write(struct.pack(_MF_HDR, MAGIC_MANIFEST, FORMAT_VERSION, len(records)))
        for r in records:
            f.write(struct.pack(_MF_REC, r["type"], r.get("is_music", 0),
                                r.get("loop_whole", 0), 0,
                                pack_name(r["name"]), pack_path(r["path"]),
                                r["file_bytes"], 0))
