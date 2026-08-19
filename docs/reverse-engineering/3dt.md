# Omikron `.3DT` indexed texture format

> **Status:** work-in-progress reverse-engineering documentation for OpenNomad.
>
> This document describes the `.3DT` texture streams used by the Windows release of *Omikron: The Nomad Soul*, with emphasis on behavior observed directly in `Runtime.exe`. The format is unusually dependent on metadata stored outside the `.3DT` file itself, so this document should be read together with [`3do.md`](3do.md).

## Source precedence and confidence

The sources used here are, in descending order of authority:

1. **`Runtime.exe` behavior** — authoritative for how the retail game reads, decompresses, palettes, and uploads the data.
2. **Observed retail assets**, including texture payloads associated with standalone `.3DO` resources and `.3DO` objects embedded in `.SCX` scenario data.
3. **Chevluh's Omikron Blender Importer** — a valuable prior implementation and the original basis for parts of OpenNomad's decoder, but not authoritative where it disagrees with Runtime: <https://github.com/Chevluh/Omikron_Blender_Importer/blob/main/omikronImporter.py>

The `Runtime.exe` currently used for this analysis has:

- PE image base: `0x00400000`
- linker timestamp: `1999-10-04 20:31:50`
- SHA-256: `55f7120bfea7891b048c64e3682f3259cdbf2719a43fa24e42254b753c95d2ef`

Addresses in this document refer to that executable and are not expected to be stable across other builds.

Confidence labels used below:

- **Confirmed — Runtime:** directly demonstrated by code in `Runtime.exe`.
- **Confirmed — data:** strongly established from retail asset layout and section boundaries.
- **Corroborated:** Runtime behavior and asset observations agree, often also agreeing with the Blender importer.
- **Tentative:** plausible semantic interpretation, but not yet fully demonstrated.
- **Unknown:** byte layout or behavior is bounded, but its higher-level purpose is not established.

## Overview

`.3DT` is Omikron's indexed texture-data format. Unlike `.3DO`, it does **not** contain a self-describing header, signature, texture count, dimensions, or per-texture directory.

A `.3DT` file is effectively a sequential payload stream whose interpretation is supplied by the material/texture descriptors in the companion `.3DO` model.

For each `.3DO` material, in material-table order, the stream contains:

```text
+-----------------------------------------------+
| palette: 3 * (1 << bitsPerPixel) bytes       |
+-----------------------------------------------+
| indexed pixel payload: dataSize bytes         |
+-----------------------------------------------+
| palette for next material                     |
+-----------------------------------------------+
| pixel payload for next material               |
+-----------------------------------------------+
| ...                                           |
+-----------------------------------------------+
```

There is no separator between records. The consumer must know, from the `.3DO` material descriptor:

- `bitsPerPixel`;
- `dataSize`;
- `width`;
- `height`;
- material ordering.

This means an isolated `.3DT` cannot in general be decoded correctly without its associated `.3DO` metadata or equivalent externally supplied metadata.

## Relationship to `.3DO`

The relevant serialized `.3DO` record is `0x50` bytes. Runtime turns it into a mutable runtime texture/material descriptor and reuses several fields for texture-page and palette-page allocation.

The fields relevant to `.3DT` are:

| Offset | Size | Type | Runtime-oriented name | Status / notes |
|---:|---:|---|---|---|
| `0x00` | 20 | char[20] | material name | Serialized material identifier. |
| `0x14` | 20 | char[20] | texture resource name | **Confirmed behavior.** Runtime uses this string to identify/share texture-page resources. The Blender importer calls it `BMPfile`. |
| `0x28` | 20 | char[20] | palette resource name | **Confirmed behavior.** Runtime uses this string to identify/share palette resources. The Blender importer calls it `TGAfile`. |
| `0x3C` | 4 | u32 | `dataSize` | Number of bytes occupied by the stored indexed pixel payload for this material. |
| `0x40` | 2 | u16 | `texturePageIndex` | **Confirmed — Runtime mutation.** Index of a 256×256 runtime indexed texture page. Serialized pre-load meaning, if any, remains unknown. |
| `0x42` | 2 | u16 | `textureSlotIndex` | **Confirmed — Runtime mutation.** Slot within a shared texture page. |
| `0x44` | 2 | u16 | `palettePageIndex` | **Confirmed — Runtime mutation.** Selects a shared 256-entry runtime palette page. |
| `0x46` | 2 | u16 | `paletteSlotIndex` | **Confirmed — Runtime mutation.** Selects the material's subpalette within that page. |
| `0x48` | 1 | u8 | `bitsPerPixel` | Indexed texture bit depth used by Runtime as `1 << bitsPerPixel`. Only the low byte is needed for this calculation. |
| `0x49` | 1 | byte | unknown / runtime scratch | Not established as serialized `.3DT` metadata. |
| `0x4A` | 1 | u8 | runtime atlas X offset | **Confirmed behavior.** Used as the low-byte/X component of the destination location inside a 256×256 texture page. |
| `0x4B` | 1 | u8 | runtime atlas Y offset | **Confirmed behavior.** Used as the high-byte/Y component of the destination location inside a 256×256 texture page. |
| `0x4C` | 2 | u16 | `width` | Texture width in pixels. |
| `0x4E` | 2 | u16 | `height` | Texture height in pixels. |

The names `BMPfile` and `TGAfile` from the Blender importer may reflect authoring-pipeline history, but Runtime's retail behavior gives the two strings a more useful operational interpretation: `+0x14` is used as a texture-resource identity and `+0x28` as a palette-resource identity.

### Important consequence

The `.3DT` stream contains **only palette and indexed-pixel bytes**. Dimensions, compression size, bit depth, names, and material association all live in the `.3DO` descriptor.

## Logical `.3DT` record

For one material, define:

```text
colorCount  = 1 << bitsPerPixel
paletteSize = colorCount * 3
pixelCount  = width * height
```

The corresponding `.3DT` record is:

```text
struct Logical3DTRecord {
    uint8_t palette[colorCount][3]; // paletteSize bytes
    uint8_t pixelPayload[dataSize]; // raw indices or compressed stream
};
```

This is a **logical** description, not a literal C structure, because both arrays are variable-sized and no record header exists on disk.

The start of record `n+1` is therefore:

```text
next = current + 3 * (1 << bitsPerPixel) + dataSize
```

using the metadata of record `n`.

## Palette data

### Size

**Confirmed — Runtime.** Palette size is calculated exactly as:

```text
colorCount  = 1 << bitsPerPixel
paletteSize = 3 * colorCount
```

Examples:

| Indexed depth | Entries | Palette bytes |
|---:|---:|---:|
| 4 bpp | 16 | 48 (`0x30`) |
| 8 bpp | 256 | 768 (`0x300`) |

Runtime's palette-loading path at `0x004A77E0` calculates this size directly from byte `material+0x48`.

### Entry layout

Each palette entry occupies exactly three consecutive bytes:

```text
byte 0
byte 1
byte 2
```

The Blender importer interprets these as `R`, `G`, `B`, and Runtime subsequently consumes them as three independent color channels before converting them into the active display pixel format.

**Corroborated:** treating the serialized order as RGB is consistent with the importer and the Runtime palette-processing pipeline. The exact historical naming of the three internal channel lookup tables has not been independently recovered, so code should avoid deriving additional format semantics from those table addresses alone.

There is **no serialized alpha byte** in a `.3DT` palette entry.

### Palette allocation in Runtime

Runtime does not keep every material palette as an isolated object. It packs palettes into shared pages containing 256 logical palette entries.

After allocation, the material contains:

```text
palettePageIndex = material[0x44]
paletteSlotIndex = material[0x46]
```

For a texture with `bitsPerPixel = b`, its serialized palette is placed at the logical palette-page index:

```text
firstEntry = palettePageIndex * 256
           + paletteSlotIndex * (1 << b)
```

The byte address in Runtime's shared RGB palette storage is therefore equivalent to:

```text
paletteRgbBase + 3 * firstEntry
```

This relationship is directly visible in `0x004A77E0` and the palette callback at `0x004A7900`.

For 8-bpp textures, a single material palette consumes all 256 entries of a page. Lower-bit-depth palettes can share a page.

### Palette names and sharing

Runtime uses the 20-byte string at material offset `0x28` when registering or finding palette resources. This is stronger evidence than the Blender importer's historical `TGAfile` label that the field functions as a **palette resource key** in the retail engine.

Whether two materials with identical palette bytes but different palette-resource names are intentionally considered distinct has not yet been exhaustively characterized.

## Transparency and alpha

`.3DT` does not contain explicit alpha values.

The Blender importer applies this rule when expanding a palette to RGBA:

```text
RGB == (0, 0, 0) -> alpha 0
otherwise        -> alpha 1
```

That is **not confirmed as part of the Omikron file format**.

Runtime reads only three bytes per palette entry. Original transparency behavior is controlled elsewhere, including mesh/material rendering flags such as alpha test, alpha blending, additive blending, and subtractive blending, plus renderer state.

OpenNomad should therefore not document or implement “black means transparent” as an intrinsic `.3DT` rule unless later Runtime analysis demonstrates such a rule in the rendering path.

## Pixel payload

The palette is followed immediately by `dataSize` bytes belonging to the indexed pixel payload.

The decompressed representation is one byte per pixel regardless of the material's indexed bit depth:

```text
uint8_t indices[width * height]
```

A 4-bpp texture is therefore **not nibble-packed** after decompression. `bitsPerPixel` controls the palette size/range, not the number of bits physically used per decoded pixel.

For a `b`-bpp texture, serialized/decompressed local indices are expected to address a palette of:

```text
0 .. (1 << b) - 1
```

The retail engine later rebases those local indices when packing several small palettes into one 256-entry runtime palette page.

## Raw versus compressed payloads

**Confirmed — Runtime.** The canonical test is:

```text
if dataSize == width * height:
    payload is raw indexed pixels
else:
    payload is compressed
```

This is important because the Blender importer contains a special check for `compressedSize == 65536`. Runtime shows that `65536` is not a general magic value meaning “uncompressed”; it is simply the raw pixel count of a 256×256 texture.

### Optimized 256×256 path

`0x004A75E0` has a dedicated branch for:

```text
width  == 256
height == 256
```

If `dataSize == 0x10000`, Runtime copies exactly 65536 raw index bytes directly into the selected 256×256 texture page.

If the dimensions are 256×256 but `dataSize != 0x10000`, Runtime calls the decompressor directly with the texture page as its destination.

For other dimensions, Runtime applies the same general `dataSize == width * height` raw test before deciding whether to decompress.

## Runtime texture pages

The original renderer stores indexed texture pixels in shared 256×256 pages.

A page therefore occupies:

```text
256 * 256 = 65536 = 0x10000 bytes
```

The material's runtime `texturePageIndex` at `+0x40` selects a page. Runtime computes the page base using an effective `pageIndex << 16`, which is exactly a 65536-byte stride.

Smaller textures are packed into these pages. Material bytes `+0x4A` and `+0x4B` are populated with the pixel offset of the material inside the page, and Runtime addresses the resulting location as:

```text
pageBase + (atlasY << 8) + atlasX
```

where:

```text
atlasX = material[0x4A]
atlasY = material[0x4B]
```

The exact allocation policy for all non-square texture dimensions still deserves additional asset-wide verification. The page addressing itself is confirmed.

## Local indices versus runtime palette-page indices

This distinction is easy to miss and is important when comparing `.3DT` bytes with Runtime memory.

Serialized/decompressed `.3DT` pixels are **local indices into the material's own palette**.

When lower-bit-depth palettes share a 256-entry runtime palette page, `0x004A75E0` transforms each decoded pixel before copying it into the shared indexed texture page:

```text
paletteBaseIndex = (1 << bitsPerPixel) * paletteSlotIndex
runtimeIndex     = localIndex + paletteBaseIndex
```

For example, if a 4-bpp texture occupies palette slot 3:

```text
colorCount       = 16
paletteBaseIndex = 16 * 3 = 48

local index 0  -> runtime page index 48
local index 15 -> runtime page index 63
```

The `.3DT` file itself still contains the local values `0..15`.

A modern renderer does not have to reproduce this packing internally. OpenNomad can expand each texture to an independent RGBA image, but the decoder must apply the palette using the **local** indices from the file rather than assuming they are already page-global indices.

## Indexed texture compression

Runtime's decompressor begins at `0x004B4B40`.

It is a compact LZ/RLE-style byte-stream decoder. It operates entirely on bytes and has no knowledge of image width, height, palette, rows, or texture boundaries.

### Runtime function contract

The function has the effective signature:

```c
size_t Decompress3DT(
    uint8_t *destination,
    const uint8_t *source,
    size_t compressedSize);
```

It receives **no expected decompressed size**.

Runtime computes:

```text
sourceEnd = source + compressedSize
```

and stops based on compressed-input consumption. The return value is:

```text
number of destination bytes produced/advanced
```

The texture upload path does not appear to rely on that return value for validating `width * height`.

### Initial bytes

For a compressed stream, Runtime assumes at least two bytes are available:

```text
source[0] = first literal output byte
source[1] = first control byte
```

The first byte is copied directly to the destination before any control bit is processed.

The first control byte then governs the next eight coding decisions.

### Control-bit order

Control bits are consumed **most-significant bit first**:

```text
bit 7, bit 6, bit 5, ... bit 0
```

For each bit:

- `0` -> copy one literal byte from the source;
- `1` -> read a sequence descriptor and emit a run/back-reference.

After eight decisions, a new control byte is read and processing continues.

Conceptually:

```text
firstLiteral
controlByte
  token 0
  token 1
  ...
  token 7
controlByte
  token 8
  ...
```

The variable-length bytes belonging to tokens are interleaved in the stream between control bytes.

## Sequence descriptor

When a control bit is `1`, Runtime reads one descriptor byte `d`:

```text
type = d & 0x03
base = d >> 2
```

The upper six bits encode the run length. The lower two bits select one of four forms.

| Type | Form | Output length | Source/back-reference distance | Extra bytes |
|---:|---|---:|---:|---:|
| 0 | repeat previous byte | `base + 2` | 1 | 0 |
| 1 | short LZ copy | `base + 3` | `1 + next_u8` | 1 |
| 2 | long LZ copy | `base + 3` | `1 + big_endian_u16` | 2 |
| 3 | 256-granularity LZ/self copy | `base + 3` | `next_u8 << 8` | 1 |

The maximum descriptor-derived lengths are therefore:

```text
type 0: 65 bytes  (63 + 2)
type 1: 66 bytes  (63 + 3)
type 2: 66 bytes
type 3: 66 bytes
```

### Type 0 — repeated previous byte

Type 0 emits:

```text
length = (d >> 2) + 2
value  = destination[-1]
```

Runtime implements this as an optimized fill rather than a generic byte-by-byte LZ copy.

Semantically it is equivalent to a distance-1 back-reference.

### Type 1 — short back-reference

Runtime reads one extra byte `n`:

```text
length   = (d >> 2) + 3
distance = 1 + n
```

Range:

```text
1 .. 256 bytes back
```

The copy is performed forward, one byte at a time, so overlapping references are supported.

### Type 2 — 16-bit back-reference

Runtime reads two bytes in **big-endian order** for the distance component:

```text
n = (next0 << 8) | next1

length   = (d >> 2) + 3
distance = 1 + n
```

Range:

```text
1 .. 65536 bytes back
```

The use of big-endian order here is local to the compression token. It does not imply that other Omikron structures are big-endian; `.3DO` scalar structures are little-endian.

### Type 3 — 256-byte-granularity back-reference

Runtime reads one byte `n` and computes:

```text
length   = (d >> 2) + 3
distance = n << 8
```

Possible encoded distances are:

```text
0, 256, 512, ... 65280
```

This is a direct observation from `0x004B4B40`.

#### Type 3 with `n == 0`

This is an important edge case.

Runtime does **not** reinterpret zero as 65536. There is no `+65536`, wraparound fix-up, or special branch.

Instead:

```text
distance = 0
sourcePtr = destinationPtr
```

The byte-copy loop then reads each byte from the destination position that it is about to write back to the same position, while advancing the destination pointer.

Operationally, this is a **self-copy/no-op run that advances the output cursor while preserving the destination's previous contents**.

If the destination region is already zeroed, the result looks like a zero run. If it contains previous data, that data is preserved.

This behavior may be intentional for some texture-page cases, but its higher-level purpose is not yet confirmed. A decoder that blindly converts type-3 zero to a 65536-byte back-reference does **not** match Runtime.

This edge case was particularly relevant during OpenNomad's texture-decoder work because treating it as an ordinary vector-style back-reference can lead to truncated output or an out-of-range access.

## Overlapping copies

Types 1, 2, and 3 are copied forward byte-by-byte:

```text
repeat length times:
    *dst = *history
    ++dst
    ++history
```

This means the format intentionally supports overlapping LZ references. A short history sequence can expand into repeated patterns as newly emitted bytes become part of the source for later bytes in the same token.

Implementations must **not** use a copy primitive whose behavior is undefined for overlap.

A simple byte loop reproduces Runtime semantics most directly.

## Compression termination behavior

Runtime's decoder is input-size driven.

The main continuation condition is effectively:

```text
source < sourceEnd
```

It does not receive `width * height`, and therefore cannot independently stop when the expected image size has been produced.

Consequences:

1. A valid caller must supply the exact compressed payload size from the `.3DO` descriptor.
2. The caller must know the expected decompressed pixel count separately.
3. A modern safe decoder should verify that the decompressed size is exactly `width * height` after decoding.
4. Runtime itself is permissive/unsafe with malformed streams in ways OpenNomad should not emulate literally.

### Boundary overreads in the original implementation

The retail decoder assumes valid game data. It may fetch a new control byte immediately after the eighth token before checking whether the compressed source has reached the supplied end pointer. Parameter bytes for a token are likewise read before a final boundary check.

That behavior should be treated as a property of the 1999 implementation, **not** as permission for a modern parser to read beyond `dataSize`.

OpenNomad should bounds-check every literal, descriptor, and parameter read.

## Invalid history references

The Blender importer contains a defensive behavior:

```text
if backReferenceDistance > bytesAlreadyProduced:
    output zero
```

Runtime's decompressor does **not** implement that rule.

Except for the legitimate type-3 zero-distance self-copy case, a back-reference that points before the beginning of the destination buffer causes Runtime to read memory before the output region. Retail assets are assumed not to rely on such malformed references.

A safe OpenNomad decoder should reject an invalid history reference rather than silently defining zero-fill as part of the file format.

## Reference decompression pseudocode

The following pseudocode describes the observed grammar while adding explicit safety checks that Runtime lacks.

```text
function decompress3DT(source, compressedSize, expectedSize):
    require compressedSize >= 2

    src = 0
    dst = byte_array(expectedSize)   # initialization policy discussed below
    out = 0

    # Runtime copies this unconditionally.
    require out < expectedSize
    dst[out] = source[src]
    out += 1
    src += 1

    control = source[src]
    src += 1
    bitsRemaining = 8

    while src < compressedSize:
        compressedToken = (control & 0x80) != 0

        if not compressedToken:
            require src < compressedSize
            require out < expectedSize
            dst[out] = source[src]
            out += 1
            src += 1

        else:
            require src < compressedSize
            d = source[src]
            src += 1

            type = d & 3
            base = d >> 2

            if type == 0:
                require out > 0
                length = base + 2
                value = dst[out - 1]
                repeat length times:
                    require out < expectedSize
                    dst[out] = value
                    out += 1

            else if type == 1:
                require src < compressedSize
                n = source[src]
                src += 1
                length = base + 3
                distance = 1 + n
                require distance <= out
                repeat length times:
                    require out < expectedSize
                    dst[out] = dst[out - distance]
                    out += 1

            else if type == 2:
                require src + 1 < compressedSize
                n = (source[src] << 8) | source[src + 1]
                src += 2
                length = base + 3
                distance = 1 + n
                require distance <= out
                repeat length times:
                    require out < expectedSize
                    dst[out] = dst[out - distance]
                    out += 1

            else: # type == 3
                require src < compressedSize
                n = source[src]
                src += 1
                length = base + 3
                distance = n << 8

                if distance == 0:
                    # Runtime advances while preserving existing destination bytes.
                    require out + length <= expectedSize
                    out += length
                else:
                    require distance <= out
                    repeat length times:
                        require out < expectedSize
                        dst[out] = dst[out - distance]
                        out += 1

        bitsRemaining -= 1

        if bitsRemaining == 0:
            if src >= compressedSize:
                break
            control = source[src]
            src += 1
            bitsRemaining = 8
        else:
            control = (control << 1) & 0xFF

    require out == expectedSize
    return dst
```

### Initialization and type-3 zero

The safe pseudocode above models a type-3 zero-distance command as “advance without changing destination.” That is what Runtime's memory operations do.

For a standalone software decoder, however, this creates a question Runtime answers implicitly through the pre-existing contents of its destination buffer. If an asset uses type-3 zero before those bytes have otherwise been initialized, faithfully reproducing the result requires knowing how the relevant Runtime texture page or scratch buffer was initialized.

OpenNomad should therefore keep this case visible in diagnostics rather than silently rewriting it to another back-reference distance.

## Runtime upload path

The high-level Runtime sequence for each material is approximately:

```text
1. allocate/reuse a palette page and palette slot
2. read paletteSize bytes from the texture stream
3. register/upload/rebuild palette state
4. allocate/reuse a 256×256 indexed texture page and texture slot
5. read dataSize bytes from the texture stream
6. if raw, use the bytes directly
7. otherwise decompress them
8. for shared low-bpp palette pages, rebase local indices
9. copy/pack the pixels into the 256×256 runtime texture page
10. invoke the renderer upload callback
```

Runtime has synchronous and callback/asynchronous variants of parts of this path, but the serialized byte order is the same.

## Palette processing after load

After palette bytes are loaded, Runtime calls a renderer-facing palette upload callback and then `0x00483C70`, which dispatches to `0x00483C80` with a default bias/overbright parameter of zero.

`0x00483C80` reads the three serialized palette channels and builds a larger 16-level table of packed 16-bit colors for the active renderer/pixel format.

Conceptually, each of the 256 palette-page entries receives multiple intensity variants. This is **runtime rendering state**, not extra `.3DT` data.

A modern OpenNomad renderer that expands indexed textures directly to RGB/RGBA does not need to preserve this exact internal table representation unless it is reproducing Omikron's original palette-lighting behavior.

The important file-format facts are:

- palette entries are three bytes;
- palette pages contain 256 entries in Runtime;
- lower-bpp material palettes may occupy subranges of one page;
- decoded local pixel indices are rebased before entering a shared runtime page.

## Standalone `.3DT` versus `.SCX`-embedded texture payloads

Standalone models conventionally use:

```text
MODEL.3DO  -> structural/model data
MODEL.3DT  -> palette and indexed texture payload stream
```

The same texture payload representation also appears when a `.3DO` core is embedded inside scenario containers such as `aventure.SCX` and `Grid.SCX`.

In that case there may be no separate filesystem `.3DT` sibling. The scenario/resource stream supplies the palette and pixel bytes associated with the embedded model.

The useful distinction is therefore:

```text
serialized 3DO core metadata
        +
3DT-style palette/pixel payload source
```

The payload source can be a standalone `.3DT` file or an enclosing resource/container stream.

OpenNomad should model these as two independent concepts rather than making the `.3DO` parser itself assume a sibling `.3DT` path.

## Differences and cautions relative to the Blender importer

Chevluh's importer is an excellent reference and correctly discovered most of the high-level payload grammar, but several details should now be documented differently based on Runtime.

### 1. `.3DT` has no self-contained metadata header

The importer already relies on `.3DO` materials, and Runtime confirms that this dependency is fundamental. A `.3DT` should not be documented as a sequence of autonomous texture structures with their own headers.

### 2. Raw data is identified by pixel count, not by a magic 65536 value

Importer code contains:

```python
if compressedSize == 65536:
    return readUBytes(file_object, compressedSize)
```

Runtime's general rule is:

```text
dataSize == width * height
```

`65536` is simply the optimized 256×256 case.

### 3. Type-3 distance is exactly `next_u8 << 8`

The Runtime implementation does not add one and does not reinterpret zero as 65536.

### 4. Type-3 zero is a self-copy/no-op run

A decoder based on an append-only vector cannot reproduce Runtime by evaluating `result[currentByte - 0]`, because that refers to the next not-yet-appended element. Runtime instead operates on preallocated memory, where source and destination can be the same address.

### 5. Invalid backward references do not define zero-fill semantics

The importer's zero-fill guard is a robustness choice. It is not present in Runtime and should not be documented as part of the compression format.

### 6. Black palette entries are not serialized alpha

The importer derives transparency from `(0,0,0)`. `.3DT` stores only RGB-like triplets; Runtime's actual transparency comes from rendering behavior outside the palette bytes.

### 7. Local pixel indices are not necessarily final runtime page indices

For shared low-bpp palette pages, Runtime adds the material's palette-slot base before copying pixels into its 256×256 indexed page. An RGBA decoder should apply the material's own palette directly and need not emulate this rebase.

### 8. Vertical image orientation is a consumer concern

The serialized payload is a linear pixel stream. Coordinate-system and image-origin conversions belong to the rendering/import layer. They should not be described as byte rearrangements intrinsic to `.3DT` unless Runtime's upload path demonstrates such a transformation.

The current understanding is that rows should not be unconditionally flipped merely to match a graphics API convention.

## Suggested OpenNomad representation

A modern decoder should keep immutable serialized metadata separate from runtime GPU state.

For example:

```cpp
struct Texture3DTDescriptor {
    std::string materialName;
    std::string textureResourceName;
    std::string paletteResourceName;

    std::uint32_t dataSize{};
    std::uint8_t bitsPerPixel{};
    std::uint16_t width{};
    std::uint16_t height{};
};

struct Texture3DTDecoded {
    Texture3DTDescriptor descriptor;

    std::vector<std::array<std::uint8_t, 3>> palette;
    std::vector<std::uint8_t> indices;
};
```

Renderer-facing RGBA conversion, GPU texture handles, sampler state, and atlas packing should live outside the immutable serialized representation.

## Recommended decoder invariants

A robust OpenNomad implementation should enforce at least the following:

1. `bitsPerPixel` must be small enough that `1 << bitsPerPixel` is safe and plausible.
2. The palette byte count must fit inside the supplied payload source.
3. `width * height` must be checked for integer overflow before allocation.
4. Exactly `dataSize` bytes belong to the pixel payload.
5. If `dataSize == width * height`, read exactly that many raw indices.
6. Otherwise require a compressed payload large enough for the initial literal/control pair.
7. Every literal/descriptor/parameter read must remain inside `dataSize`.
8. Type-1 and type-2 history distances must not point before the beginning of produced output.
9. Non-zero type-3 history distances must not point before the beginning of produced output.
10. Type-3 zero must preserve the destination region rather than being rewritten to an invented 65536-byte distance.
11. Output must never advance past `width * height` in a safe decoder.
12. At successful completion, decompressed output advancement should equal exactly `width * height`.
13. Every local pixel index should be less than `1 << bitsPerPixel` before palette lookup, unless later evidence demonstrates intentionally out-of-range data.
14. Do not infer alpha solely from RGB black.
15. Do not make standalone `.3DT` path resolution part of the decompressor itself; accept a payload stream/source so embedded SCX resources use the same decoder.

## Useful diagnostics

When a texture fails to decode, logging the following values usually makes reverse-engineering problems much easier to localize:

```text
material name
texture resource name
palette resource name
bitsPerPixel
colorCount
paletteSize
width
height
expectedPixelCount
dataSize
raw/compressed decision
compressed source offset
output count at failure
control byte
control-bit index
descriptor byte
descriptor type
descriptor length
back-reference distance
```

For type 3, always log the raw extra byte as well as `distance = byte << 8`. A zero value is semantically significant in Runtime.

## Known / unresolved questions

The following areas should remain explicitly marked as incomplete until additional Runtime work or asset-wide validation resolves them.

### Exact palette channel naming

Three serialized bytes per entry are confirmed, and RGB interpretation is strongly corroborated. The exact naming/mapping of Runtime's internal channel lookup tables relative to every supported display pixel format has not yet been fully documented.

### Intended semantic purpose of type-3 zero

The machine-level behavior is confirmed: it advances the destination while preserving existing bytes.

What remains uncertain is **why assets use it** and which destination-initialization invariant the original asset pipeline expected. Possibilities such as sparse/no-op runs should be treated as hypotheses until verified across retail textures and caller initialization paths.

### Destination initialization

The 256×256 compressed path can decode directly into a runtime texture page, whereas other compressed textures can decode through a shared scratch buffer before being packed into a page.

The exact initialization/reuse lifecycle of those buffers matters only for type-3 zero-distance runs and malformed streams, but should be characterized before claiming byte-for-byte reproduction in every case.

### Supported indexed bit depths

Runtime computes palette size generically as `1 << bitsPerPixel` and has dedicated behavior for 8-bpp palettes. 4-bpp and 8-bpp are established useful cases. A complete inventory of all bit depths present in retail assets has not yet been recorded here.

### Non-square texture packing

Width and height fields are confirmed, as is the 256×256 page representation. The precise texture-slot allocator math for every rectangular size has not yet been generalized or validated against a comprehensive asset set.

This affects original-runtime atlas placement, not the basic `.3DT` decoding grammar.

### Transparency details

The file has no alpha channel, but the exact interactions among palette index, alpha-test state, blend flags, texture-page state, and original Direct3D rendering still require renderer-path analysis.

### Animated/sprite texture relationships

Some `.3DO` resources act as sprites or animated effects. The relationship among sprite frames, quad UVs, material selection, animated texture resources, and texture-page placement is being reverse engineered separately. Nothing in the base `.3DT` compression grammar itself identifies animation frames.

## Runtime function map

Names below are descriptive research/OpenNomad names rather than recovered original symbols.

| Address | Role |
|---:|---|
| `0x00483C70` | Wrapper that rebuilds the material's palette-lighting table with the default bias/overbright parameter. |
| `0x00483C80` | Converts RGB palette-page entries into 16 levels of packed renderer colors. |
| `0x004A75E0` | Indexed texture unpack/atlas-upload path. Confirms raw-vs-compressed test, 256×256 page size, decompressor use, local-index rebasing, and runtime texture-page fields. |
| `0x004A77E0` | Palette read/allocation path. Confirms `3 * (1 << bitsPerPixel)` palette size, palette page/slot fields, and use of material `+0x28` as a palette resource name. |
| `0x004A7900` | Palette upload/completion callback path; reconstructs palette address from page, slot, and bit depth. |
| `0x004B4B40` | `.3DT`/3DO indexed texture decompressor. Confirms stream grammar and all four descriptor types. |

Renderer upload function pointers used by this code are assigned elsewhere during graphics initialization. Their exact names and hardware-backend semantics are outside the serialized `.3DT` format.

## Secondary reference: Omikron Blender Importer

Chevluh's Blender importer remains the most useful public prior implementation of the format and correctly establishes the core sequential model:

```text
for material in materials:
    read 2**BPP palette entries, 3 bytes each
    read/decompress material.dataSize bytes
```

It also provided the initial community description of the four compression forms.

Reference:

<https://github.com/Chevluh/Omikron_Blender_Importer/blob/main/omikronImporter.py>

When it conflicts with observed `Runtime.exe` behavior, OpenNomad should follow Runtime and document the discrepancy, as done above.

## Summary

The essential format can be reduced to a few rules:

```text
.3DT has no header.

For every material in the companion .3DO, in order:
    colorCount = 1 << bitsPerPixel
    read colorCount * 3 palette bytes
    read dataSize pixel-payload bytes

    if dataSize == width * height:
        payload is raw one-byte palette indices
    else:
        payload uses Omikron's control-byte LZ/RLE codec
```

The compression codec is:

```text
first output byte = literal
control bits = MSB first

0 -> one literal byte
1 -> descriptor:
    type 0: len=(d>>2)+2, repeat previous byte
    type 1: len=(d>>2)+3, distance=1+u8
    type 2: len=(d>>2)+3, distance=1+BE_u16
    type 3: len=(d>>2)+3, distance=u8<<8
```

The most important Runtime-specific cautions are:

- `65536` is not a generic raw-data marker; raw means `dataSize == width * height`;
- type-3 zero means distance zero and behaves as an advancing self-copy/no-op, **not** a 65536-byte back-reference;
- Runtime does not define the Blender importer's invalid-history zero-fill rule;
- black palette entries do not carry serialized alpha;
- low-bpp local indices are rebased only when Runtime packs them into a shared 256-entry palette page;
- `.3DT` metadata lives in `.3DO`, and the same payload grammar can be supplied by an `.SCX` container instead of a sibling file.
