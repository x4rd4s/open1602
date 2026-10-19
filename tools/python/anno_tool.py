#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Python counterpart of o1602-tool for quick experiments (unmaintained prototype; o1602-tool and
the C++ readers in engine/assets are authoritative, see anno_formats.py).

  anno_tool.py chunks <file>
  anno_tool.py cod <file.cod> [out.txt]
  anno_tool.py bsh2png <file.bsh> <outdir> --palette STADTFLD.COL [--first N] [--count N]
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import anno_formats as af  # noqa: E402


def cmd_chunks(args):
    for c in af.read_chunks(af.read_file(args.file)):
        print(f"{c.name:<18} {c.offset:>10} {len(c.data):>10}")


def cmd_cod(args):
    text = af.decode_cod(af.read_file(args.file))
    if args.out:
        Path(args.out).write_text(text, encoding="utf-8", newline="")
    else:
        sys.stdout.write(text)


def cmd_bsh2png(args):
    palette = af.read_palette(af.read_file(args.palette))
    payload, offsets = af.read_sprites(af.read_file(args.file))
    out = Path(args.outdir)
    out.mkdir(parents=True, exist_ok=True)
    stem = Path(args.file).stem
    last = len(offsets) if args.count is None else min(len(offsets), args.first + args.count)
    for i in range(args.first, last):
        af.sprite_to_image(af.decode_sprite(payload, offsets[i]), palette).save(out / f"{stem}_{i:05}.png")
    print(f"wrote {last - args.first} sprites to {out}")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("chunks"); s.add_argument("file"); s.set_defaults(fn=cmd_chunks)
    s = sub.add_parser("cod"); s.add_argument("file"); s.add_argument("out", nargs="?"); s.set_defaults(fn=cmd_cod)
    s = sub.add_parser("bsh2png"); s.add_argument("file"); s.add_argument("outdir")
    s.add_argument("--palette", required=True); s.add_argument("--first", type=int, default=0)
    s.add_argument("--count", type=int); s.set_defaults(fn=cmd_bsh2png)
    args = p.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
