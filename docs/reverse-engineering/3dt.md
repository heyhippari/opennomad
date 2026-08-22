# Omikron `.3DT` indexed texture payload format

> **Status:** work-in-progress reverse-engineering documentation for OpenNomad  
> **Intended repository path:** `docs/reverse-engineering/3dt.md`  
> **Last updated:** 2026-08-22
>
> This document describes the indexed palette/texture payload convention used by
> the Windows retail release of *Omikron: The Nomad Soul*.
>
> A `.3DT` is **not a self-describing image container**. Its texture boundaries,
> dimensions, palette depth and compressed byte counts come from the material
> table of the corresponding `.3DO` resource.
>
> This document distinguishes:
>
> 1. serialized `.3DT` bytes;
> 2. the original Runtime's palette/page/decompression behavior;
> 3. modern OpenNomad decoding/rendering policy.

Read together with:

- [`3do.md`](3do.md) — authoritative material metadata and model-side UV use;
- [`runtime-coordinate-math.md`](runtime-coordinate-math.md) — presentation
  transforms, not part of texture serialization;
- [`script-opcodes.md`](script-opcodes.md) — SCX resource/container context.

---

# 1. Source precedence and confidence

Sources are used in this order:

1. **`Runtime.exe`** — authoritative for payload boundaries, raw/compressed
   selection, palette allocation, decompression and indexed page packing.
2. **Retail assets** — standalone `.3DT` files and equivalent payloads embedded
   with 3DO resources inside retail SCX data.
3. **OpenNomad tests/traces** — validation and safe modern implementation
   behavior.
4. **Chevluh's Omikron Blender Importer** — valuable prior public
   implementation, but not authoritative where it differs from Runtime.

Reference importer:

<https://github.com/Chevluh/Omikron_Blender_Importer/blob/main/omikronImporter.py>

Runtime baseline:

```text
File:             Runtime.exe
Architecture:     PE32 / i386
Image base:       0x00400000
Linker timestamp: 1999-10-04 20:31:50
SHA-256:          55f7120bfea7891b048c64e3682f3259cdbf2719a43fa24e42254b753c95d2ef
```

Confidence labels:

- **Confirmed — Runtime:** direct executable behavior.
- **Confirmed — data:** direct retail data observation.
- **Corroborated:** Runtime and data agree.
- **Strongly reconstructed:** multiple observations agree but original names are
  unavailable.
- **Tentative:** useful hypothesis.
- **Unknown:** behavior/bytes are bounded but semantics are not.

---

# 2. Authoring-pipeline context

Quantic Dream's original tooling included GEM and other internal editors, but no
current evidence proves that `.3DT` was the native project format of one of
those tools.

Contemporary development notes describe platform-specific asset processing.

Treat `.3DT` as a **retail/build/runtime payload representation**, not as a
recovered GEM source file.

---

# 3. Core concept: `.3DT` has no header

There is no known:

- magic/signature;
- version field;
- texture count;
- width/height table;
- palette-count table;
- record directory;
- per-record compressed-size header.

Instead, `.3DT` is interpreted by walking the corresponding `.3DO` material
descriptors in material-table order.

For each material:

```text
+----------------------------------------------+
| palette: 3 * (1 << bitsPerPixel) bytes      |
+----------------------------------------------+
| indexed pixel payload: dataSize bytes        |
+----------------------------------------------+
| next material palette                        |
+----------------------------------------------+
| next material pixel payload                  |
+----------------------------------------------+
| ...                                          |
+----------------------------------------------+
```

The start of the next material is:

```text
nextOffset =
    currentOffset
    + 3 * (1 << bitsPerPixel)
    + dataSize
```

No delimiter is stored between records.

Therefore an isolated `.3DT` generally cannot be decoded correctly without:

```text
the companion .3DO material table
```

or equivalent externally supplied material metadata.

---

# 4. Material metadata supplied by `.3DO`

The relevant 3DO material descriptor is `0x50` bytes.

Fields affecting 3DT interpretation:

| 3DO offset | Size | Type | Meaning |
|---:|---:|---|---|
| `0x00` | 20 | `char[20]` | material name |
| `0x14` | 20 | `char[20]` | Runtime texture/cache resource name |
| `0x28` | 20 | `char[20]` | Runtime palette resource name |
| `0x3C` | 4 | `u32` | `dataSize` |
| `0x40` | 2 | `u16` | runtime texture-page field |
| `0x42` | 2 | `u16` | runtime texture-slot field |
| `0x44` | 2 | `u16` | runtime palette-page field |
| `0x46` | 2 | `u16` | runtime palette-slot field |
| `0x48` | 2 | `u16` | bit-depth field; Runtime uses low byte for palette-size shift |
| `0x4A` | 1 | `u8` | runtime texture-page X/U offset |
| `0x4B` | 1 | `u8` | runtime texture-page Y/V offset |
| `0x4C` | 2 | `u16` | width |
| `0x4E` | 2 | `u16` | height |

Important:

```text
dataSize
bitsPerPixel
width
height
material ordering
```

are **not stored in `.3DT` itself**.

## 4.1 Bit-depth field width

For serialization/documentation it is safest to preserve the full:

```text
u16 at material +0x48
```

because the record layout physically allocates two bytes there.

The recovered Runtime palette-size calculation consumes the low byte when
computing:

```text
1 << bitsPerPixel
```

Do not repurpose `+0x49` as a separate mandatory `.3DT` header field.

## 4.2 Runtime allocation fields

The page/slot/atlas fields are part of the mutable material record used by the
original Runtime.

A modern OpenNomad renderer does not need to reproduce the same allocator
internally.

They remain important for understanding:

- page-space UVs;
- palette rebasing;
- original sprite rendering;
- why a material's serialized local indices are not always the final index
  stored in Runtime's shared page.

---

# 5. Logical per-material record

Define:

```text
bpp          = low byte of material.bitsPerPixel
colorCount   = 1 << bpp
paletteSize  = 3 * colorCount
pixelCount   = width * height
```

Then the logical material payload is:

```text
Logical3DTPayload
{
    palette[paletteSize]
    pixels[dataSize]
}
```

where:

```text
palette = colorCount RGB triplets
pixels  = raw indices or compressed byte stream
```

This is not a literal C struct because both arrays are variable-sized and no
record header exists.

---

# 6. Palette representation

## 6.1 Palette size

**Confirmed — Runtime.**

Runtime calculates:

```text
colorCount  = 1 << bpp
paletteSize = colorCount * 3
```

Examples:

| Indexed depth | Palette entries | Palette bytes |
|---:|---:|---:|
| 1 | 2 | 6 |
| 2 | 4 | 12 |
| 4 | 16 | 48 / `0x30` |
| 8 | 256 | 768 / `0x300` |

4-bpp and 8-bpp assets are established practical cases. Runtime's calculation
is generic; a full retail inventory of every used depth is still desirable.

## 6.2 Palette entry layout

Each entry contains exactly:

```text
3 bytes
```

The interpretation as:

```text
R, G, B
```

is strongly corroborated by:

- the reference importer;
- visual decoding;
- Runtime's subsequent colour conversion/palette-lighting path.

There is no fourth serialized alpha byte.

## 6.3 No serialized alpha channel

The file contains:

```text
RGB-like triplet
```

not:

```text
RGBA
```

Therefore any alpha used by a modern renderer is **derived runtime state**.

That distinction matters particularly for black-key transparency.

---

# 7. Transparency and the black colour key

OpenNomad currently expands a palette entry:

```text
(0, 0, 0)
```

to:

```text
alpha = 0
```

and all other palette colours to:

```text
alpha = 255
```

The reference Blender importer uses the same convention.

This is a useful and apparently correct rendering convention for Omikron
assets, but it must be described precisely:

```text
black-key transparency is not a fourth byte in .3DT
```

The serialized palette still stores only RGB.

The exact original Direct3D mechanism by which black became transparent in each
alpha-test/blend path still belongs to renderer-state reverse engineering.

Therefore:

- **format fact:** no serialized alpha exists;
- **engine/rendering convention:** pure black functions as the transparency key
  for assets that use keyed/transparent rendering;
- **material state still matters:** alpha-test, alpha-blend, additive and other
  mesh flags determine how transparent content is ultimately combined.

Do not infer that every black texel in every opaque material must be discarded
solely because its RGB value is zero.

---

# 8. Pixel representation after decoding

The logical decompressed image is:

```text
uint8_t indices[width * height]
```

That remains true for lower indexed depths such as 4 bpp.

In other words:

```text
4 bpp controls palette size
```

but the decompressed pixels are not packed two-per-byte.

For a `b`-bit material, local decoded values are expected to fall in:

```text
0 .. (1 << b) - 1
```

before Runtime's shared-palette-page rebasing.

A modern decoder should validate this range before palette lookup.

---

# 9. Raw versus compressed payload

**Confirmed — Runtime.**

The canonical decision is:

```text
if dataSize == width * height:
    payload is raw indices
else:
    payload is compressed
```

This rule applies generally.

## 9.1 `65536` is not a magic raw marker

Older importer code special-cased:

```text
compressedSize == 65536
```

as uncompressed.

Runtime shows the reason that often worked:

```text
256 * 256 = 65536 = 0x10000
```

A full-size texture with:

```text
width     = 256
height    = 256
dataSize  = 65536
```

is simply the normal:

```text
dataSize == pixelCount
```

raw case.

Smaller raw textures are legal under the same rule.

## 9.2 256x256 fast path

Runtime code around:

```text
0x004A75E0
```

has a dedicated 256x256 upload path.

If:

```text
width == 256
height == 256
dataSize == 0x10000
```

it copies the 65536 raw indices directly into a texture page.

If dimensions are 256x256 but `dataSize != 0x10000`, Runtime invokes the
decompressor directly against the destination page.

---

# 10. Original Runtime texture pages

The retail renderer stores indexed pixels in shared:

```text
256 x 256
```

pages.

One page is:

```text
256 * 256 = 65536 = 0x10000 bytes
```

The material runtime texture-page field at `+0x40` selects the page.

A page base therefore advances by:

```text
pageIndex << 16
```

bytes.

Smaller material textures can be packed within one page.

Runtime stores page-space placement in:

```text
material +0x4A  atlas U/X
material +0x4B  atlas V/Y
```

and addresses a pixel as conceptually:

```text
pageBase + (atlasV << 8) + atlasU
```

The exact allocator policy for every rectangular size remains incompletely
documented, but page dimensions and addressing are established.

---

# 11. Original Runtime palette pages

Runtime also packs palettes into shared pages of:

```text
256 logical colour entries
```

Material runtime state:

```text
palettePageIndex = material +0x44
paletteSlotIndex = material +0x46
```

For a material with:

```text
colorCount = 1 << bpp
```

the first logical palette entry is:

```text
firstEntry =
    palettePageIndex * 256
    + paletteSlotIndex * colorCount
```

The corresponding RGB byte offset is:

```text
paletteRgbBase + firstEntry * 3
```

Consequences:

- 8-bpp palette: consumes all 256 entries of one page;
- 4-bpp palette: consumes 16 entries and can share a page with other palettes.

This organization is runtime memory management, not additional serialized
`.3DT` metadata.

---

# 12. Local indices versus runtime page indices

Serialized/decompressed 3DT indices are **local to the material palette**.

When Runtime packs a lower-bpp texture into a shared palette page, it rebases
each pixel:

```text
paletteBaseIndex =
    (1 << bpp) * paletteSlotIndex

runtimePageIndex =
    localIndex + paletteBaseIndex
```

Example:

```text
bpp = 4
colorCount = 16
paletteSlotIndex = 3

paletteBaseIndex = 48

local 0  -> page index 48
local 15 -> page index 63
```

The `.3DT` payload still contains:

```text
0 .. 15
```

not:

```text
48 .. 63
```

A modern RGBA decoder should apply the material's local palette directly and
does not need to emulate page-index rebasing.

---

# 13. UV implications of the 256x256 page architecture

UV bytes are serialized in 3DO polygon/frame records, not in `.3DT`.

However, original page packing explains why different consumers normalize them
differently.

## 13.1 Ordinary mesh geometry

OpenNomad's ordinary per-material mesh path currently converts:

```text
u = uByte / material.width
v = vByte / material.height
```

because each decoded material becomes an independent modern GPU texture.

## 13.2 Sprite frames

The sprite path uses original page-space semantics:

```text
u = uByte / 256.0 + textureOffsetU
v = vByte / 256.0 + textureOffsetV
```

This matches the 256x256 Runtime page model.

See the dual rectangle/sprite-frame interpretation in [`3do.md`](3do.md).

## 13.3 Do not hard-code a vertical flip

The serialized payload is a linear pixel stream.

No general `.3DT` format rule says:

```text
flip image vertically
```

just to match OpenGL, Blender or another API convention.

Image origin/orientation belongs to the consumer/rendering path unless Runtime
explicitly transforms rows.

Current OpenNomad texture decoding leaves the serialized row order intact.

---

# 14. Indexed texture compressor

Runtime's decompressor begins around:

```text
0x004B4B40
```

It is a compact byte-oriented LZ/RLE codec.

It does not know:

- texture width;
- texture height;
- palette size;
- scanline boundaries.

Its effective original contract is approximately:

```c
size_t DecompressIndexedTexture(
    uint8_t* destination,
    const uint8_t* source,
    size_t compressedSize);
```

The caller separately knows the expected:

```text
width * height
```

pixel count.

---

# 15. Decoder initialization

A compressed stream begins with:

```text
source[0] = first literal output byte
source[1] = first control byte
```

Runtime copies the first source byte immediately:

```text
dst[0] = source[0]
```

Then the control byte governs the next up-to-eight tokens.

Compressed data therefore requires at least enough source data to supply the
initial literal/control sequence in a valid stream.

---

# 16. Control bits

Control bits are consumed:

```text
MSB first
```

in order:

```text
bit 7
bit 6
bit 5
bit 4
bit 3
bit 2
bit 1
bit 0
```

Meaning:

```text
0 -> literal byte
1 -> sequence descriptor
```

After eight token decisions, Runtime obtains another control byte.

Conceptual stream:

```text
firstLiteral
control0
    token0
    token1
    ...
    token7
control1
    token8
    ...
```

Token payload bytes are interleaved with control bytes.

---

# 17. Sequence descriptor

For a sequence token Runtime reads:

```text
u8 d
```

and derives:

```text
type = d & 0x03
base = d >> 2
```

The upper six bits encode length; the lower two bits select one of four forms.

| Type | Meaning | Output length | Distance / source | Extra bytes |
|---:|---|---:|---|---:|
| 0 | repeat previous byte | `base + 2` | distance 1 / previous byte | 0 |
| 1 | short LZ copy | `base + 3` | `1 + next_u8` | 1 |
| 2 | long LZ copy | `base + 3` | `1 + big_endian_u16` | 2 |
| 3 | 256-granularity/self copy | `base + 3` | `next_u8 << 8` | 1 |

Maximum descriptor-derived lengths:

```text
type 0: 65
types 1..3: 66
```

---

# 18. Type 0: repeated previous byte

For:

```text
type = 0
```

Runtime emits:

```text
length = (d >> 2) + 2
value  = destination[-1]
```

Semantically:

```text
distance = 1
```

Runtime uses an optimized fill-like implementation.

A valid stream cannot use this before at least one output byte exists; the
mandatory initial literal guarantees that condition for normal decoding.

---

# 19. Type 1: short back-reference

Runtime reads:

```text
u8 n
```

and computes:

```text
length   = (d >> 2) + 3
distance = 1 + n
```

Distance range:

```text
1 .. 256
```

The copy advances forward one byte at a time.

Overlapping copies are therefore intentional and valid.

---

# 20. Type 2: 16-bit back-reference

Runtime reads the two distance bytes in **big-endian order**:

```text
n = (next0 << 8) | next1

length   = (d >> 2) + 3
distance = 1 + n
```

Distance range:

```text
1 .. 65536
```

This local big-endian field does not change the little-endian nature of 3DO
structures.

Again, copies proceed forward and can overlap.

---

# 21. Type 3: 256-byte-granularity back-reference

This is the most important edge case in the current decoder work.

Runtime reads:

```text
u8 n
```

and computes exactly:

```text
length   = (d >> 2) + 3
distance = n << 8
```

Possible numeric distances:

```text
0
256
512
...
65280
```

There is:

- no `+1`;
- no `+256`;
- no conversion of zero to 65536.

## 21.1 `n == 0` means distance zero

When:

```text
n = 0
```

Runtime computes:

```text
sourcePointer = destinationPointer
```

The byte loop then repeatedly performs conceptually:

```text
*dst = *src
dst++
src++
```

with `src == dst` at each iteration.

This is therefore an **advancing self-copy**:

```text
output cursor advances
existing destination bytes are preserved
```

It is not intrinsically:

```text
a zero run
```

and it is not:

```text
a 65536-byte back-reference
```

If the destination was already zeroed, the preserved bytes happen to be zero.
If not, their previous values remain.

## 21.2 Known OpenNomad mismatch

At the time of this documentation update, OpenNomad's current
`Texture3DT::decompress()` still maps:

```text
type-3 extra byte 0
```

to:

```text
65536
```

That does **not** match the recovered Runtime behavior.

This should be treated as a known implementation debt, not as ambiguity in this
format document.

The correct Runtime-compatible semantics are:

```text
distance = extraByte << 8
```

including distance zero.

## 21.3 Destination initialization matters

A distance-zero self-copy is unusual because its visible result depends on the
bytes already in the destination.

Runtime can decode directly into a texture page in some paths and through
temporary storage in others.

To reproduce a type-3-zero stream byte-for-byte, the initialization/reuse state
of that destination must also be understood.

This remains an open runtime-memory question even though the machine-level token
semantics are established.

---

# 22. Overlapping references

Types 1, 2 and non-zero type 3 copy forward.

Conceptually:

```text
repeat length times:
    dst[0] = history[0]
    dst++
    history++
```

The source range can overlap newly written output.

Do not replace this with a copy operation whose overlap semantics are undefined.

A byte loop or explicitly overlap-safe expansion reproduces Runtime behavior.

---

# 23. Decoder termination

Runtime's decompressor is fundamentally **compressed-input-size driven**.

It receives:

```text
compressedSize
```

and computes:

```text
sourceEnd = source + compressedSize
```

It does not receive the expected uncompressed pixel count.

Therefore the original codec itself cannot decide:

```text
stop exactly at width * height
```

from dimensions.

The caller knows the expected image size separately.

## 23.1 Safety difference in OpenNomad

A modern decoder should not reproduce Runtime's unsafe assumptions literally.

Retail Runtime assumes valid game data and can read token/control bytes with
minimal boundary protection.

OpenNomad should validate every read against `dataSize`.

---

# 24. Invalid history references

The reference Blender importer contains a defensive behavior equivalent to:

```text
if distance > bytesAlreadyProduced:
    output zero
```

Runtime does not define that as codec semantics.

For types 1, 2 and non-zero type 3, a distance that points before the intended
history region is malformed/unsafe data.

A robust decoder should:

```text
report/reject invalid history
```

rather than silently making zero-fill part of the format.

The separate type-3 **distance-zero** self-copy case is legitimate machine-level
behavior and must not be rejected simply because `distance <= produced` logic
was designed for ordinary backward references.

---

# 25. Runtime-faithful decompression pseudocode

The following describes the recovered token grammar while adding modern bounds
checks.

```text
function decode(source, compressedSize, destination, expectedSize):
    require expectedSize > 0
    require compressedSize >= 2

    src = 0
    out = 0

    destination[out++] = source[src++]

    control = source[src++]
    bitsRemaining = 8

    while src < compressedSize:
        isSequence = (control & 0x80) != 0

        if not isSequence:
            require src < compressedSize
            require out < expectedSize
            destination[out++] = source[src++]

        else:
            require src < compressedSize
            d = source[src++]

            type = d & 3
            base = d >> 2

            if type == 0:
                length = base + 2
                require out > 0
                value = destination[out - 1]

                repeat length times:
                    require out < expectedSize
                    destination[out++] = value

            else if type == 1:
                require src < compressedSize
                distance = 1 + source[src++]
                length = base + 3
                require distance <= out

                repeat length times:
                    require out < expectedSize
                    destination[out] = destination[out - distance]
                    out++

            else if type == 2:
                require src + 1 < compressedSize
                n = (source[src] << 8) | source[src + 1]
                src += 2

                distance = 1 + n
                length = base + 3
                require distance <= out

                repeat length times:
                    require out < expectedSize
                    destination[out] = destination[out - distance]
                    out++

            else:
                require src < compressedSize
                n = source[src++]

                distance = n << 8
                length = base + 3

                if distance == 0:
                    require out + length <= expectedSize
                    # Runtime reads/writes the same addresses.
                    # Preserve existing destination bytes.
                    out += length
                else:
                    require distance <= out

                    repeat length times:
                        require out < expectedSize
                        destination[out] = destination[out - distance]
                        out++

        control = (control << 1) & 0xFF
        bitsRemaining--

        if bitsRemaining == 0:
            if src >= compressedSize:
                break

            control = source[src++]
            bitsRemaining = 8

    return out
```

Whether a safe high-level decoder should require:

```text
out == expectedSize
```

for every retail stream is discussed below.

---

# 26. Short decoded output and tail initialization

The original decompressor returns how far the destination pointer advanced.

The surrounding Runtime upload path does not appear to perform a simple,
universal:

```text
assert(outputCount == width * height)
```

after every decode.

Current OpenNomad behavior, at the time of writing, pads a short decoded result
with:

```text
palette index 0
```

until it reaches:

```text
width * height
```

This preserves compatibility with assets that worked under the older importer
behavior.

That padding is **not confirmed as a serialized codec rule**.

Documentation/implementation should keep these concepts separate:

```text
Runtime decompressor:
    input-size-driven

OpenNomad current compatibility policy:
    constrain to expected image size
    pad a short tail with index 0

ideal validation question:
    determine from retail assets and Runtime destination initialization
    whether every valid stream logically defines all expected pixels
```

If an asset relies on type-3 zero self-copy into preinitialized memory, replacing
that with appended zeros can also change the result.

---

# 27. Raw payload decoding

Raw material:

```text
dataSize == width * height
```

Decode is simply:

```text
indices = next dataSize bytes
```

No nibble unpacking is applied.

The payload cursor then advances by exactly:

```text
dataSize
```

after the palette.

---

# 28. Palette lookup

After local indices are decoded, a modern standalone image expansion is:

```text
rgb = palette[index]
```

with any desired alpha policy layered on top.

A safe decoder should report an index:

```text
>= colorCount
```

as malformed or at least suspicious.

Runtime's own page rebasing occurs after local decoding and is not part of the
stored local index value.

---

# 29. Runtime palette processing after load

Runtime does more than upload the three-byte palette verbatim.

A path around:

```text
0x00483C70
0x00483C80
```

builds a larger table of packed display-format colours at multiple intensity
levels.

Conceptually:

```text
serialized RGB palette
      |
      v
shared 256-entry Runtime palette page
      |
      v
multiple intensity/lighting variants
      |
      v
packed 16-bit renderer colours
```

This is runtime rendering state, not extra `.3DT` data.

A modern RGBA renderer can choose not to reproduce the physical table layout,
but original palette-lighting behavior may eventually require reproducing its
math.

---

# 30. Runtime material upload sequence

A simplified original flow for each material is:

```text
1. allocate/find palette page + palette slot
2. consume 3 * (1 << bpp) palette bytes
3. register/rebuild palette renderer state
4. allocate/find 256x256 texture page + texture slot
5. consume dataSize pixel bytes
6. if raw:
       use local indices directly
   else:
       decompress
7. if using a shared lower-bpp palette page:
       add palette-slot base to indices
8. pack/copy into the 256x256 texture page
9. invoke renderer upload/update callbacks
```

The exact allocator and asynchronous/callback details are implementation
machinery around the same serialized byte order.

---

# 31. Standalone `.3DT` versus embedded payload source

Standalone assets:

```text
MODEL.3DO
MODEL.3DT
```

Embedded scenario resources can instead provide:

```text
3DO core metadata
+
3DT-style payload bytes
```

inside an enclosing SCX/resource stream.

Therefore OpenNomad APIs should conceptually accept:

```text
3DO material metadata
+
arbitrary texture-payload byte span/source
```

rather than force the low-level decoder to open a same-basename path.

This is especially important for `aventure.SCX`, `Grid.SCX` and other
resource-packed content.

---

# 32. Current OpenNomad decoder behavior

At the time of this documentation update, `Texture3DT::load()` performs roughly:

```text
for each 3DO material:
    validate bpp <= 8
    read 3 * (1 << bpp) RGB palette bytes
    derive RGBA:
        black -> alpha 0
        other -> alpha 255

    pixelCount = width * height

    if dataSize == pixelCount:
        copy raw indices
    else:
        invoke OpenNomad decompressor

    validate each local palette index
    expand to RGBA
```

It also computes the next material offset as:

```text
offset += 3 * colorCount + dataSize
```

and checks that the consumed logical end does not exceed the provided payload.

## 32.1 Intentional modern differences

OpenNomad:

- uses independent RGBA GPU textures rather than indexed 256x256 Runtime pages;
- does not need Runtime palette-page index rebasing for rendering;
- can retain strict bounds checks missing in the original executable.

## 32.2 Known fidelity differences / technical debt

Current decoder behavior that should **not** be treated as authoritative format
semantics:

1. type-3 zero currently becomes a 65536-byte history distance — this is wrong
   relative to Runtime;
2. short decompressed output is padded with index 0 — compatibility policy, not
   a proven Runtime codec rule;
3. black is converted directly to alpha 0 during image expansion — useful engine
   rendering convention, but not serialized alpha data.

The format documentation should drive future fixes, not be rewritten to justify
those implementation shortcuts.

---

# 33. Recommended OpenNomad decoder model

Keep three layers explicit.

## 33.1 Serialized descriptor

```cpp
struct TexturePayloadDescriptor {
    std::string materialName;
    std::string textureName;
    std::string paletteName;

    std::uint32_t dataSize;
    std::uint16_t bitsPerPixelRaw;
    std::uint16_t width;
    std::uint16_t height;
};
```

## 33.2 Decoded indexed image

```cpp
struct IndexedTextureImage {
    std::vector<std::array<std::uint8_t, 3>> palette;
    std::vector<std::uint8_t> indices;

    std::uint16_t width;
    std::uint16_t height;
};
```

## 33.3 Renderer-facing image

```cpp
struct TextureRGBA {
    std::vector<std::uint8_t> rgba8;
    ...
};
```

This lets:

- file decoding;
- transparency policy;
- original palette emulation;
- modern GPU upload

evolve independently.

---

# 34. Recommended validation invariants

A safe decoder should:

1. validate the companion material metadata before consuming bytes;
2. prevent overflow in `1 << bpp`;
3. set a plausible supported bit-depth maximum;
4. bounds-check `3 * colorCount`;
5. overflow-check `width * height`;
6. consume exactly `dataSize` bytes for one pixel payload;
7. use `dataSize == pixelCount` as the raw test;
8. require enough compressed data for every literal/control/descriptor read;
9. handle type-0 only after at least one output byte;
10. reject type-1/type-2 history before the output base;
11. reject non-zero type-3 history before the output base;
12. implement type-3 zero as an advancing self-copy/preserve operation;
13. never reinterpret type-3 zero as 65536;
14. support overlapping references;
15. prevent output advancement beyond the intended destination;
16. validate local palette indices before RGB lookup;
17. keep short-output policy explicit rather than silently calling it codec
    semantics;
18. avoid an unconditional vertical flip;
19. keep black-key transparency outside the serialized palette parser if
    possible;
20. accept embedded payload byte spans, not only filesystem `.3DT` siblings.

---

# 35. Useful decoder diagnostics

For a material decode failure log:

```text
material name
texture resource name
palette resource name

bitsPerPixelRaw
effective bpp
colorCount
paletteSize

width
height
pixelCount
dataSize

material payload start
raw/compressed decision

compressed source cursor
decoded output cursor

control byte
control bit index
descriptor byte
descriptor type
descriptor length
extra distance byte(s)
computed distance

type-3 raw extra byte
whether type-3 distance was zero
```

For sequential `.3DT` parsing also log:

```text
material index
record start
palette end
payload end
whole source size
```

A wrong `dataSize` misaligns every subsequent material.

---

# 36. Corrections relative to older importer/documentation assumptions

## 36.1 No standalone texture headers

A `.3DT` record is defined by external `.3DO` metadata.

## 36.2 Raw test is not `65536`

Correct:

```text
dataSize == width * height
```

## 36.3 Decoded lower-bpp pixels are still bytes

No nibble-unpacking stage follows decompression.

## 36.4 Type 2 distance bytes are big-endian

Only this token field is big-endian.

## 36.5 Type 3 distance is exactly `u8 << 8`

No added one.

## 36.6 Type 3 zero is distance zero

It is a same-address advancing self-copy in Runtime.

It is **not** a 65536-byte back-reference.

## 36.7 Importer invalid-history zero fill is defensive behavior

It is not defined by Runtime's codec.

## 36.8 Black-key transparency is not serialized alpha

Three palette bytes remain three palette bytes.

## 36.9 Runtime local indices can be rebased after decoding

The stored local value and the page-global Runtime value are distinct.

## 36.10 Sprite UVs use page-space `/256`

Ordinary modern per-material mesh UV normalization is not a universal rule for
sprite frames.

---

# 37. Open questions

Still unresolved or incomplete:

- complete retail inventory of used bit depths;
- exact original text encoding of material resource names;
- whether every valid compressed stream logically advances exactly
  `width * height` bytes;
- intended use of type-3 distance zero;
- initialization/reuse state of Runtime destinations when type-3 zero occurs;
- which retail assets actually contain type-3-zero tokens;
- exact original texture-slot allocation algorithm for all rectangular sizes;
- exact palette-slot allocator policy;
- exact Direct3D colour-key/alpha-test state that implements black-key
  transparency;
- interaction between keyed black and ordinary opaque materials;
- exact original palette-lighting/overbright math;
- whether any palettes use black as a visible opaque colour under a mode that
  bypasses keying;
- platform differences in texture conversion;
- whether Dreamcast build-time conversion preserves this exact payload grammar;
- any format/version variants outside the current Windows retail dataset.

---

# 38. Useful Runtime locations

| Address | Role |
|---:|---|
| `0x00483C70` | palette-lighting rebuild wrapper |
| `0x00483C80` | builds intensity variants / packed renderer colours from RGB palette |
| `0x004A75E0` | indexed texture unpack and 256x256 page upload |
| `0x004A77E0` | palette read/allocation; confirms `3 * (1 << bpp)` |
| `0x004A7900` | palette upload/completion path |
| `0x004B4B40` | indexed texture LZ/RLE decompressor |

The page/palette allocator internals are runtime implementation details; the
serialized byte grammar described above is the portable format knowledge.

---

# 39. OpenNomad source locations

Relevant current code:

```text
src/core/Core/Omikron/Texture3DT.hpp
src/core/Core/Omikron/Texture3DT.cpp

src/core/Core/Omikron/Model3DO.hpp
src/core/Core/Omikron/Model3DO.cpp

src/core/Core/Sprite/SpriteFrame.cpp
src/core/Core/Sprite/SpriteResource.cpp

src/core/Core/WorldRenderer.cpp
```

A future type-3-zero fix should update both implementation tests and this
document only if new Runtime evidence changes the understanding. The existing
Runtime-derived token semantics should not be weakened to match an old decoder
shortcut.

---

# 40. Compact format reference

```text
.3DT
====

No header.

For each companion .3DO material, in material-table order:

    bpp          = low8(material.bitsPerPixelRaw)
    colorCount   = 1 << bpp
    paletteSize  = 3 * colorCount
    pixelCount   = width * height

    read paletteSize bytes:
        colorCount * RGB triplets

    read dataSize bytes:
        if dataSize == pixelCount:
            raw one-byte local palette indices
        else:
            compressed Omikron LZ/RLE stream

Compressed stream:
    first byte = literal output
    next byte  = control bits, MSB first

    control 0:
        literal byte

    control 1:
        descriptor d
        type = d & 3
        base = d >> 2

        type 0:
            length   = base + 2
            repeat previous byte

        type 1:
            length   = base + 3
            distance = 1 + u8

        type 2:
            length   = base + 3
            distance = 1 + BE_u16

        type 3:
            length   = base + 3
            distance = u8 << 8

            if u8 == 0:
                Runtime source == destination
                advance while preserving existing bytes

Runtime pages:
    indexed texture page = 256 x 256 bytes
    palette page         = 256 entries

lower-bpp local index:
    runtimeIndex =
        localIndex
        + paletteSlotIndex * (1 << bpp)
```

---

# 41. Secondary reference

Chevluh's Blender importer remains extremely useful:

<https://github.com/Chevluh/Omikron_Blender_Importer/blob/main/omikronImporter.py>

It correctly established much of the broad sequential palette/payload model and
the compression family.

Where importer behavior conflicts with disassembled Runtime behavior, this
document follows Runtime.
