# Retail FNT bitmap fonts

Omikron retail `.FNT` files are headerless, byte-indexed bitmap fonts. The
first `0x800` bytes are a table of 256 eight-byte little-endian descriptors:

```text
+0x00  uint16 data_block
+0x02  int16  vertical_offset
+0x04  uint16 width
+0x06  uint16 height
```

For a non-empty descriptor, the bitmap begins at `data_block * 8`. It contains
`width * height` bytes in top-row-first row-major order. Each byte is a
five-bit coverage value in `0..31`; it is not a palette index. The next bitmap
begins on an eight-byte boundary. Empty descriptors carry no bitmap payload.

OpenNomad validates the complete descriptor table, checked dimension and
offset arithmetic, bitmap ranges, descriptor-table overlap, and every coverage
value before building a GPU resource. The atlas stores white RGB and
`coverage / 31` alpha, preserves the top-down source orientation in glyph UVs,
uses one transparent pixel between glyphs, and uses nearest filtering. This
keeps the five-bit samples exact and prevents inter-glyph bleed.

## Runtime font registry

The recovered registry associates each key with a resource and three logical
metrics:

| Key | Resource | Letter spacing | Blank width | Line height |
| --- | --- | ---: | ---: | ---: |
| `I` | `MENUINTR` | +2 | 15 | 36 |
| `M` | `MENUSAVE` | +1 | 8 | 17 |
| `D` | `DIALOGUE` | +1 | 6 | 17 |
| `R` | `DIALSELE` | -1 | 6 | 17 |
| `P` | `PARCHEMI` | +1 | 6 | 17 |
| `C` | `COMPUTER` | 0 | 6 | 14 |
| `S` | `SNEAK` | +1 | 6 | 20 |
| `J` | `JOURNAL` | +1 | 6 | 17 |
| `V` | `VOIXOFF` | +1 | 6 | 23 |
| `1` | `GENERIC1` | +2 | 6 | 12 |
| `2` | `GENERIC2` | +3 | 6 | 18 |
| `3` | `GENERIC3` | +3 | 6 | 24 |
| `L` | `SMALL` | 0 | 6 | 12 |

A non-empty glyph advances by `width + letter_spacing`. An empty glyph,
including space, advances by `blank_width + letter_spacing`. For a logical
line beginning at `line_y`:

```text
baseline     = line_y + line_height
glyph_top    = baseline + vertical_offset - height
glyph_bottom = glyph_top + height
```

Retail strings select `glyph[unsigned_byte]` directly. In particular, a byte
such as `0xE9` is one glyph index and must not be decoded as UTF-8. The optional
TTF fallback retains its own UTF-8 decoder because it is a different backend.

The dialog keys resolve through the normal case-insensitive game-data path as
`FONTS/DIALOGUE.FNT` and `FONTS/DIALSELE.FNT`. Valid retail data is preferred;
a missing or rejected file produces a warning before the `OMIKRON.TTF`
fallback is attempted.
