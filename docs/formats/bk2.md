# BK2 – Bink 2 videos

The History Edition ships the game's videos as Bink 2 files (RAD Game Tools) where the
Königsedition has Smacker files ([smk.md](smk.md)): `VideoSmk/<number>.bk2` in English and
`Loca/<language>/VideoSmk/<number>.bk2` in the other languages, with the same numbers. Open1602
plays them with its own decoder, `video::Bink` (`modules/video/src/bink.cpp` of annokit), without a library:

- the picture is a port of Librempeg's Bink 2 decoder (`libavcodec/bink2.c`, `bink2g.h`;
  GPL-3.0-or-later, Konstantin Shishkov and Paul B Mahol), only its half for the revisions `g` on
  (`modules/video/src/bink2_video*.cpp` of annokit);
- the sound is a port of FFmpeg's Bink Audio decoder (`binkaudio.c`, `dct.c`, `rdft.c`;
  LGPL-2.1-or-later) (`modules/video/src/bink_audio.cpp` of annokit).

The video player takes a `.smk` before a `.bk2` of the same number.

## The History Edition's files

All are `KB2j` with the video flags `0x10` (no alpha plane, two slices). The logos and the intro are
the Königsedition's videos enlarged five times (1760 × 1200, 15 frames/s); 703 and 12345 are new
(1920 × 1080, 30 frames/s); the credits (10) are 436 × 356 where the Königsedition has 432 × 352.
8 and 9 are missing, as are 60, 2061 and 989, which in the Königsedition are copies of 58, 2060 and
988. Each video has the audio tracks of its Smacker file, as Bink Audio (DCT).

## Structure

All numbers little-endian.

| Offset | Field |
|---|---|
| 0x00 | `KB2` and the revision letter (`a` … `n`) |
| 0x04 | file size − 8 |
| 0x08 | frames |
| 0x0C | largest frame in bytes |
| 0x10 | (not used) |
| 0x14 | width |
| 0x18 | height |
| 0x1C | frame rate: numerator |
| 0x20 | frame rate: denominator |
| 0x24 | video flags (bit 20: alpha plane; revisions `h` on: bits 0..1 the slices) |
| 0x28 | audio tracks |
| 0x2C | revisions `i`, `j`, `k`, `n`: 4 more bytes |

Then per track 4 bytes (largest decoded size), per track `u16` sample rate and `u16` flags
(`0x2000` stereo, `0x1000` DCT, else RDFT), per track a 4-byte id. Then the frame index: one `u32`
offset per frame, the last frame ends at the end of the file; bit 0 of an offset marks a key frame
(frame 0 is always one), so frames start at even offsets.

A frame holds one packet per audio track (`u32` size, then the data) and then the
picture.

### Picture

YUV 4:2:0 in macroblocks of 32 × 32 pixels (16 × 16 for chroma), each split into 8 × 8 blocks with an
integer DCT. The picture starts with a `u32` of frame flags and, from the second slice on, the
offset of each slice in the packet; a slice is a band of whole macroblock rows. Bits are read from the
lowest bit of each byte up; the variable length codes are stored the same way.

Each macroblock of a key frame is intra coded; in the other frames a type comes first (intra, skip,
motion, residue), coded as an index into a list of the types used last. The DC coefficients are
predicted from the neighbours, the AC coefficients come as runs and levels; motion vectors are
half-pel for luma and quarter-pel for chroma. The edges between the blocks are smoothed after
decoding, unless the frame flags turn that off.

Sizes that are not a multiple of 32: the column block flags count the width rounded up to 32
(432: 55 flags), the row flags the height rounded up to 16 (240: 29); motion compensation clamps at
the rounded width and the real height. Librempeg counts the columns from the real width and
fails on the German credits from frame 48.

The frames are turned into RGBA with the BT.601 matrix in video range.

### Audio

As in Bink 1: per block a frame of 512 samples below 22 050 Hz, 1024 below 44 100 Hz, else 2048
(RDFT: times the channels, which it interleaves), coded as quantised coefficients in Bark bands and turned back into samples with an
inverse DCT-III or real DFT; consecutive blocks are crossfaded over 1/16 of a frame. Revision `n` is
Bink Audio 2 with other coefficient scaling.

## Verification

- `tests/unit/test_bink.cpp`: built files (a flat key frame, a skipped frame, a silent audio track,
  damaged files) and the transforms against their definitions.
- Against Librempeg on all 94 videos of the History Edition: 93 bit for bit in every frame (Y, U,
  V); the English credits (436 × 356) differ by at most 3 levels at the right edge, the German
  credits (432 × 352) match the Königsedition's frames where Librempeg fails. The sound matches to
  1 LSB.
- `o1602-tool bk2 <file> [--png DIR] [--every N] [--wav TRACK OUT.wav]` decodes a whole file,
  says how long each frame takes and writes frames as PNG and a track as WAV, to compare with the
  Königsedition's Smacker files.
