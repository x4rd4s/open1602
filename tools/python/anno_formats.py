# SPDX-License-Identifier: GPL-3.0-or-later
"""Reference readers for Anno 1602 data formats (prototyping / research).

Unmaintained prototype: the C++ readers in engine/assets (and o1602-tool) are authoritative and
tested; this module is not tested and may lag behind them and docs/formats/. Where they disagree,
the C++ is right. Use it for quick experiments while reverse engineering.
Only the Python standard library is required (Pillow optional for PNG export).
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field
from pathlib import Path

CHUNK_HEADER = struct.Struct("<16sI")


@dataclass
class Chunk:
    name: str
    offset: int  # payload offset in file
    data: bytes


def read_chunks(data: bytes) -> list[Chunk]:
    """Parses the generic chunk container: {char name[16]; u32 size; u8 data[size]}*."""
    chunks, off = [], 0
    while off < len(data):
        if off + CHUNK_HEADER.size > len(data):
            raise ValueError(f"truncated chunk header at {off}")
        raw_name, size = CHUNK_HEADER.unpack_from(data, off)
        name = raw_name.split(b"\0", 1)[0].decode("ascii", "replace")
        start = off + CHUNK_HEADER.size
        if start + size > len(data):
            raise ValueError(f"chunk {name!r} at {off} exceeds file size")
        chunks.append(Chunk(name, start, data[start:start + size]))
        off = start + size
    return chunks


def decode_cod(data: bytes) -> str:
    """.cod files: every byte negated; text is Windows-1252."""
    return bytes((-b) & 0xFF for b in data).decode("cp1252", "replace")


def encode_cod(text: str) -> bytes:
    return bytes((-b) & 0xFF for b in text.encode("cp1252"))


def read_palette(data: bytes) -> list[tuple[int, int, int]]:
    col = next(c for c in read_chunks(data) if c.name == "COL").data
    return [tuple(col[i * 4:i * 4 + 3]) for i in range(256)]


@dataclass
class Sprite:
    width: int
    height: int
    type: int
    length: int
    pixels: dict[tuple[int, int], int] = field(default_factory=dict)  # (x, y) -> palette index


def read_sprites(data: bytes) -> tuple[bytes, list[int]]:
    """Returns (payload, offsets) of a BSH/ZEI file."""
    chunk = next(c for c in read_chunks(data) if c.name in ("BSH", "ZEI"))
    payload = chunk.data
    count = struct.unpack_from("<I", payload, 0)[0] // 4
    offsets = []
    i = 0
    while i < count:
        o = struct.unpack_from("<I", payload, i * 4)[0]
        offsets.append(o)
        count = min(count, o // 4)
        i += 1
    return payload, offsets[:count]


def decode_sprite(payload: bytes, offset: int) -> Sprite:
    w, h, t, length = struct.unpack_from("<4I", payload, offset)
    s = Sprite(w, h, t, length)
    p, x, y = offset + 16, 0, 0
    while True:
        b = payload[p]
        p += 1
        if b == 0xFF:
            break
        if b == 0xFE:
            x, y = 0, y + 1
            continue
        x += b
        n = payload[p]
        p += 1
        for k in range(n):
            s.pixels[(x + k, y)] = payload[p + k]
        p += n
        x += n
    return s


def sprite_to_image(sprite: Sprite, palette):
    from PIL import Image  # optional dependency

    img = Image.new("RGBA", (max(sprite.width, 1), max(sprite.height, 1)), (0, 0, 0, 0))
    for (x, y), c in sprite.pixels.items():
        if x < sprite.width and y < sprite.height:
            img.putpixel((x, y), palette[c] + (255,))
    return img


def read_file(path: str | Path) -> bytes:
    return Path(path).read_bytes()
