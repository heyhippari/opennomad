# Omikron `.3DO` model format (version 4)

> **Status:** work-in-progress reverse-engineering documentation for OpenNomad  
> **Last updated:** 2026-08-22
>
> This document describes the version-4 `.3DO` model/resource format used by the
> Windows retail release of *Omikron: The Nomad Soul*. It deliberately separates:
>
> 1. **serialized file facts**;
> 2. **load-time/runtime behavior recovered from `Runtime.exe`**; and
> 3. **OpenNomad representation/rendering choices**.
>
> Unknown fields remain unknown. Names inherited from older tooling are retained
> only when they are useful, and are marked when Runtime has not independently
> established their semantics.

Related documentation:

- [`3dt.md`](3dt.md) — companion indexed palette/texture payload stream;
- [`runtime-coordinate-math.md`](runtime-coordinate-math.md) — native Runtime
  coordinate units, transform composition, and the presentation conversion used
  by OpenNomad;
- [`script-opcodes.md`](script-opcodes.md) — SCX resource/script container
  architecture;
- [`iam-script-functions.md`](iam-script-functions.md) — IAM functions such as
  sprite display/frame operations which consume loaded 3DO resources.

---

# 1. Source precedence and confidence

Sources are used in this order:

1. **`Runtime.exe` behavior** — authoritative for what the Windows retail game
   accepts, relocates, mutates, traverses, renders, and rejects.
2. **Retail data** — standalone assets and 3DO resources embedded in retail
   `.SCX` files.
3. **OpenNomad traces/tests** — useful for validating a Runtime-derived model,
   but not allowed to redefine the retail format.
4. **Chevluh's Omikron Blender Importer** — an important prior public
   description and useful naming source, but subordinate to Runtime where the
   two differ.

Reference importer:

<https://github.com/Chevluh/Omikron_Blender_Importer/blob/main/omikronImporter.py>

The Runtime build used for the address references in this document is:

```text
File:             Runtime.exe
Architecture:     PE32 / i386
Image base:       0x00400000
Linker timestamp: 1999-10-04 20:31:50
SHA-256:          55f7120bfea7891b048c64e3682f3259cdbf2719a43fa24e42254b753c95d2ef
```

Addresses may differ in other Windows executables, localizations, patches,
demos, or the Dreamcast build.

Confidence labels:

- **Confirmed — Runtime:** directly demonstrated by executable behavior.
- **Confirmed — data:** directly established from retail file layout.
- **Corroborated:** Runtime and retail data agree.
- **Strongly reconstructed:** multiple independent Runtime/data observations
  support the interpretation, but an original source-level name is unavailable.
- **Tentative:** plausible and useful, but not yet proven.
- **Unknown:** byte location/shape is known while semantics are not.

---

# 2. Authoring-pipeline context

Quantic Dream's contemporary development material describes **GEM** (Graphic
Editing Manager) as its real-time graphics/world editor. GEM could author
mapping, material properties, routes and other real-time scene information.

That historical context is useful, but it does **not** establish that retail
`.3DO` or `.3DT` files were GEM's native project files.

The development diaries also describe platform-specific data processing and
build steps. The safest model is therefore:

```text
GEM / animation / effects authoring data
              |
              v
      build / conversion tools
              |
              v
       retail runtime assets
         .3DO / .3DT / SCX
```

This document describes the **retail/runtime representation**, not a recovered
GEM project format.

---

# 3. Overview

Version-4 3DO resources begin with:

```text
OD3X
```

followed by:

```text
u32 version = 4
```

The first `0x2C` bytes are a **directory of relative offsets**, not one flat
monolithic header.

The directory points to:

- a separate root/control block;
- material/texture descriptors;
- global vertices;
- global triangles;
- global rectangles/quads;
- serialized object/mesh descriptors;
- relationship records;
- cameras;
- explicit light records.

A common layout looks like:

```text
core +0x0000
+----------------------------------+
| CoreDirectoryV4                  | 0x2C
+----------------------------------+
| ...                              |
+----------------------------------+ <- rootOffset
| Serialized3DORootV4              | 0x148
+----------------------------------+
| ...                              |
+----------------------------------+ <- materialsOffset
| MaterialRecordV4[]               | 0x50 each
+----------------------------------+
| ...                              |
+----------------------------------+ <- verticesOffset
| VertexV4[]                       | 0x20 each
+----------------------------------+
| ...                              |
+----------------------------------+ <- trianglesOffset
| TriangleV4[]                     | 0x1C each
+----------------------------------+
| ...                              |
+----------------------------------+ <- rectanglesOffset
| RectangleV4[]                    | 0x20 each
+----------------------------------+
| ...                              |
+----------------------------------+ <- objectsOffset
| SerializedObjectV4[]             | 0x8C each
+----------------------------------+
| ...                              |
+----------------------------------+ <- relationshipsOffset
| RelationshipRecordV4[] + ID data | fixed record 0x1C
+----------------------------------+
| ...                              |
+----------------------------------+ <- camerasOffset
| CameraRecordV4[]                 | 0x34 each
+----------------------------------+
| ...                              |
+----------------------------------+ <- lightsOffset
| ExplicitLightRecordV4[]          | 0x130 each
+----------------------------------+
```

The physical sections are offset-driven. Zero-count sections can share an
offset with the following section. Do not infer presence solely from increasing
offsets.

All scalar structures described here are little-endian unless explicitly noted
otherwise.

---

# 4. `CoreDirectoryV4` — `0x2C` bytes

**Confirmed — Runtime.**

The version-4 parser around `0x0044DF10` reads:

| Offset | Size | Type | Meaning |
|---:|---:|---|---|
| `0x00` | 4 | `char[4]` | magic, bytes `OD3X` |
| `0x04` | 4 | `u32` | version, required as `4` |
| `0x08` | 4 | `u32` | `rootOffset` |
| `0x0C` | 4 | `u32` | `materialsOffset` |
| `0x10` | 4 | `u32` | `verticesOffset` |
| `0x14` | 4 | `u32` | `trianglesOffset` |
| `0x18` | 4 | `u32` | `rectanglesOffset` |
| `0x1C` | 4 | `u32` | `objectsOffset` |
| `0x20` | 4 | `u32` | `relationshipsOffset` |
| `0x24` | 4 | `u32` | `camerasOffset` |
| `0x28` | 4 | `u32` | `lightsOffset` |

All offsets are relative to the beginning of the `OD3X` core.

## 4.1 `rootOffset`, not `versionMinor`

Older importer code names the dword at `+0x08` `versionMinor`.

Runtime instead computes:

```text
root = coreBase + *(u32 *)(coreBase + 0x08)
```

so the authoritative name is:

```text
rootOffset
```

Every embedded retail core examined during the current work used:

```text
rootOffset = 0x2C
```

but Runtime does not hard-code that value. OpenNomad must follow the serialized
offset.

This distinction fixed a class of parser bugs where a displaced root caused
fields from following sections to be misread as counts.

---

# 5. `Serialized3DORootV4` — `0x148` bytes

The root begins at:

```text
coreBase + rootOffset
```

The version-4 root used by Runtime spans `0x148` bytes.

A sparse offset map is preferable to inventing names for the still-large
unknown regions.

| Root offset | Size | Type | Current meaning | Confidence |
|---:|---:|---|---|---|
| `0x00..0x47` | `0x48` | bytes | unknown | Unknown |
| `0x48` | 4 | `u32` | sprite/model `frameCount` field | Confirmed access; broader semantics incomplete |
| `0x4C..0xB3` | `0x68` | bytes | unknown | Unknown |
| `0xB4` | 4 | `u32` | `rootObjectId` / root mesh ID | Confirmed — Runtime |
| `0xB8` | 4 | `f32` | global scalar converted by Runtime to an 8-bit grayscale-like runtime value | Behavior confirmed; semantic unknown |
| `0xBC` | 4 | `u32` | total triangle count | Confirmed |
| `0xC0` | 4 | `u32` | total rectangle/quad count | Confirmed |
| `0xC4` | 4 | `u32` | total vertex count | Confirmed |
| `0xC8..0xCF` | 8 | bytes / `u64` | unknown/reserved | Unknown |
| `0xD0` | 4 | `u32` | material count | Confirmed |
| `0xD4` | 4 | `u32` | unknown count/mode | Unknown |
| `0xD8` | 4 | `u32` | unknown/reserved | Unknown |
| `0xDC` | 4 | `u32` | camera count | Corroborated |
| `0xE0` | 4 | `u32` | serialized object/mesh-descriptor count | Confirmed — Runtime |
| `0xE4` | 4 | `u32` | relationship-record count | Confirmed structurally |
| `0xE8` | 4 | `u32` | serialized aggregate/authoring light count | Serialized field confirmed; exact role unresolved |
| `0xEC` | 4 | `u32` | non-explicit / mesh-light count | Strongly reconstructed |
| `0xF0` | 4 | `u32` | explicit light-record count processed by Runtime | Confirmed behavior |
| `0xF4..0x147` | `0x54` | bytes | unknown | Unknown |

## 5.1 Root object is explicit

`root+0xB4` is not merely an optional hint.

Runtime resolves the value against serialized object `meshID` fields and keeps
the corresponding runtime object as the distinguished hierarchy/traversal root.

Current evidence supports:

```text
rootObjectId
    ->
find SerializedObjectV4.meshID
    ->
resolved runtime root object
```

Failure to resolve the requested root is a load error in the recovered path.

This object is not necessarily the logical actor representative stored at
`actor+0x08`; see section 14.1.

Do not replace this with:

```text
"first parentless object"
```

or:

```text
object 0
```

as a generic fallback.

## 5.2 `frameCount` at `+0x48`

The original sprite frame setter reads this field when validating a requested
sprite frame.

That confirms that `+0x48` participates in sprite-capable 3DO resources.

However, every retail file examined by the current OpenNomad parser has so far
reported this field as zero. OpenNomad therefore currently falls back, for
sprite decoding only, to the selected object's rectangle count.

That fallback is an **OpenNomad provisional compatibility rule**, not a
recovered serialized-format rule.

Do not generalize `frameCount` to ordinary mesh animation without additional
Runtime evidence.

## 5.3 Light-count distinction

Three adjacent serialized light-related counts exist:

```text
+0xE8
+0xEC
+0xF0
```

The important Runtime behavior is:

1. the parser copies the value at `+0xF0` over the runtime copy of `+0xE8`;
2. it then consumes exactly `root+0xF0` records from `lightsOffset`;
3. each explicit record has a `0x130` stride.

The current OpenNomad interpretation is:

```text
+0xEC  lights represented through/baked into mesh data or vertex colours
+0xF0  explicit 0x130-byte light records
```

This matches observed assets and the fact that only the latter has a physical
record stream in the parser.

`+0xE8` should still be preserved from serialized data because its authoring or
aggregate meaning may matter later.

---

# 6. Material / texture descriptor — `0x50` bytes

One descriptor corresponds to one material/texture slot and supplies the
metadata needed to interpret the companion 3DT-style payload.

| Offset | Size | Type | Current name | Notes |
|---:|---:|---|---|---|
| `0x00` | 20 | `char[20]` | `name` | material identifier |
| `0x14` | 20 | `char[20]` | `textureName` | Runtime texture/cache identity; often BMP-like name |
| `0x28` | 20 | `char[20]` | `paletteName` | Runtime palette identity; often TGA-like name |
| `0x3C` | 4 | `u32` | `dataSize` | stored pixel-payload byte count |
| `0x40` | 2 | `u16` | texture page index / pre-load slot field | reused/mutated by Runtime |
| `0x42` | 2 | `u16` | texture slot index | reused/mutated by Runtime |
| `0x44` | 2 | `u16` | palette page index | reused/mutated by Runtime |
| `0x46` | 2 | `u16` | palette slot index | reused/mutated by Runtime |
| `0x48` | 2 | `u16` | bit-depth field | Runtime uses the low byte for `1 << bpp` |
| `0x4A` | 1 | `u8` | runtime atlas U/X offset | page-placement state |
| `0x4B` | 1 | `u8` | runtime atlas V/Y offset | page-placement state |
| `0x4C` | 2 | `u16` | width | pixels |
| `0x4E` | 2 | `u16` | height | pixels |

## 6.1 Serialized vs runtime fields

The material record is a good example of why file format and runtime structure
must be separated.

Runtime loads the record and then reuses several bytes as allocator state:

```text
serialized record
    |
    v
palette allocation
texture-page allocation
atlas placement
    |
    v
mutated runtime descriptor
```

A modern parser should keep immutable serialized metadata and store runtime/GPU
allocation separately.

Retail files commonly use sentinel-like values such as `0xFFFF` in the
page/slot fields before Runtime allocation.

## 6.2 `textureName` and `paletteName`

The importer historically calls the two strings:

```text
BMPfile
TGAfile
```

Those names are useful clues about the production pipeline, but Runtime behavior
is more specific:

- `+0x14` is used as a texture-resource/cache key;
- `+0x28` is used as a palette-resource/cache key.

The resource names can retain `.BMP` / `.TGA`-style text even though retail
rendering uses indexed 3DT-style payloads.

## 6.3 Companion texture data

The descriptor does not contain the palette or pixel bytes.

For standalone assets the conventional pairing is:

```text
MODEL.3DO
MODEL.3DT
```

For embedded resources, an enclosing SCX/resource stream can supply the same
3DT-style palette/pixel payload without a sibling filesystem `.3DT`.

See [`3dt.md`](3dt.md).

---

# 7. Fixed-width strings

Known strings are fixed-size 8-bit byte arrays, generally NUL-terminated or
NUL-padded.

Do not make CP858, CP850, Windows-1252 or another guessed code page an intrinsic
format rule until Runtime/localized asset evidence establishes it.

A parser should preserve bytes robustly enough that future localization work can
revisit encoding.

---

# 8. Global vertex stream — `0x20` bytes per vertex

Record layout:

| Offset | Size | Type | Meaning | Confidence |
|---:|---:|---|---|---|
| `0x00` | 4 | `f32` | X position | Confirmed |
| `0x04` | 4 | `f32` | Y position | Confirmed |
| `0x08` | 4 | `f32` | Z position | Confirmed |
| `0x0C` | 4 | `f32` | normal X | Corroborated |
| `0x10` | 4 | `f32` | normal Y | Corroborated |
| `0x14` | 4 | `f32` | normal Z | Corroborated |
| `0x18` | 4 | `u32` | unknown vertex field | Unknown |
| `0x1C` | 4 | bytes | vertex colour in file order `B,G,R,A` | Corroborated |

The packed colour bytes correspond to a little-endian word resembling:

```text
0xAARRGGBB
```

when viewed numerically.

The exact original meaning of the stored alpha byte is not fully documented for
every render path.

## 8.1 Native coordinates and units

3DO position data is serialized as ordinary Runtime-native:

```text
X, Y, Z
```

floats.

Native Runtime distance units are **inches**.

Do not apply the Blender importer's axis conversion or `0.025` scaling while
parsing.

OpenNomad keeps native coordinates until the centralized presentation boundary
described in:

[`runtime-coordinate-math.md`](runtime-coordinate-math.md)

This separation was necessary to fix camera/object transform mismatches during
the New Game intro.

---

# 9. Triangle record — `0x1C` bytes

| Offset | Size | Type | Meaning |
|---:|---:|---|---|
| `0x00` | 2 | `u16` | encoded vertex reference 0 |
| `0x02` | 2 | `u16` | encoded vertex reference 1 |
| `0x04` | 2 | `u16` | encoded vertex reference 2 |
| `0x06` | 1 | `u8` | U0 |
| `0x07` | 1 | `u8` | V0 |
| `0x08` | 1 | `u8` | U1 |
| `0x09` | 1 | `u8` | V1 |
| `0x0A` | 1 | `u8` | U2 |
| `0x0B` | 1 | `u8` | V2 |
| `0x0C` | 4 | `s32` | material index |
| `0x10` | 4 | `s32` | unknown |
| `0x14` | 4 | `s32` | unknown |
| `0x18` | 4 | `s32` | unknown |

## 9.1 Encoded triangle vertex references

The currently recovered interpretation is:

```text
rawRef & 0x8000  != 0  -> parent/alternate-owner vertex reference
rawRef & 0x03FF        -> vertex index
```

OpenNomad currently represents this as:

```cpp
struct TriangleVertexRef {
    std::uint16_t index;  // raw & 0x03FF
    bool parented;        // raw & 0x8000
};
```

This is no longer merely an importer guess: the recovered hierarchy/skinning
behavior and successful posed-geometry path support it.

However:

```text
bits 10..14
```

are still unresolved.

Do not reinterpret those bits as part of the vertex index simply because the
record is 16-bit, and do not assign them flags without further evidence.

A forensic parser may wish to preserve the original raw `u16` alongside the
decoded fields.

## 9.2 Meaning of `parented`

For a normal triangle corner:

```text
vertex owner = current object
```

For a parented triangle corner:

```text
vertex owner = nearest non-joint ancestor used as the skin parent
```

in the currently recovered model.

This is important for character/skinned geometry: a child object can submit a
triangle whose bind-pose vertex belongs to an ancestor's vertex block.

See the hierarchy/skinning sections below.

---

# 10. Rectangle / quad record — `0x20` bytes

The same `0x20` record is consumed in **two distinct ways**:

1. as an ordinary static quad;
2. as a sprite-frame descriptor.

That dual use is one of the most important updates to the 3DO documentation.

Serialized layout:

| Offset | Size | Type | Static-quad interpretation |
|---:|---:|---|---|
| `0x00` | 2 | `u16` | vertex 0 |
| `0x02` | 2 | `u16` | vertex 1 |
| `0x04` | 2 | `u16` | vertex 2 |
| `0x06` | 2 | `u16` | vertex 3 |
| `0x08` | 1 | `u8` | U0 |
| `0x09` | 1 | `u8` | V0 |
| `0x0A` | 1 | `u8` | U1 |
| `0x0B` | 1 | `u8` | V1 |
| `0x0C` | 1 | `u8` | U2 |
| `0x0D` | 1 | `u8` | V2 |
| `0x0E` | 1 | `u8` | U3 |
| `0x0F` | 1 | `u8` | V3 |
| `0x10` | 4 | `s32` | material index |
| `0x14` | 4 | `s32` | unknown |
| `0x18` | 4 | `s32` | unknown |
| `0x1C` | 4 | `s32` | unknown |

## 10.1 Static-quad interpretation

For ordinary mesh rendering, all four vertices and all four UV pairs are used.

OpenNomad triangulates:

```text
(0, 1, 2)
(0, 2, 3)
```

No parent/alternate-owner encoding equivalent to triangle bit `0x8000` has been
established for rectangles.

## 10.2 Sprite-frame interpretation

The sprite path deliberately treats the same record differently.

For a sprite frame:

```text
point 0 = u16 at +0x00 = rectangle vertex[0]
point 1 = u16 at +0x04 = rectangle vertex[2]

UV 0 = bytes +0x08/+0x09
UV 1 = bytes +0x0C/+0x0D

texture/material = s32 at +0x10
```

The slots at:

```text
+0x02
+0x06
+0x0A/+0x0B
+0x0E/+0x0F
```

belong to the ordinary quad interpretation and are not required for the
two-corner sprite-frame calculation.

Frame size is derived from the two referenced points:

```text
width  = abs(point1.x - point0.x)
height = abs(point1.y - point0.y)
```

The sprite path therefore uses 3DO vertices as a compact way to encode frame
dimensions/corners.

This explains why treating every `0x20` rectangle record exclusively as static
world geometry produced incorrect sprite-frame behavior.

---

# 11. UV representation and sampling conventions

Serialized polygon UV coordinates are bytes.

There is **not one universal normalization rule** for every consumer.

## 11.1 Ordinary mesh path

For conventional textured mesh geometry, OpenNomad currently resolves:

```text
uNormalized = uByte / material.width
vNormalized = vByte / material.height
```

This matches the interpretation used for ordinary per-material textures in the
modern renderer.

## 11.2 Sprite path

The recovered sprite frame path uses:

```text
u = uByte / 256.0 + textureOffsetU
v = vByte / 256.0 + textureOffsetV
```

The `/256` convention is consistent with the original Runtime's 256x256 indexed
texture-page system.

This is a different consumer contract from ordinary standalone material UV
normalization.

## 11.3 UV animation flags

Two mesh-flag bits are now Runtime-confirmed as cyclic texture-coordinate
translation:

```text
bit 24 / 0x01000000 -> add global cyclic U phase
bit 25 / 0x02000000 -> add global cyclic V phase
```

The relevant Runtime globals are currently identified as:

```text
U phase: 0x00907304
V phase: 0x00907300
```

and polygon submission paths around:

```text
0x004955A9
0x00495A33
0x0049749C
```

apply the offsets.

Bit 24 was historically labeled “skybox” by older tooling. That name is
incorrect for the observed Runtime behavior.

---

# 12. Serialized object / mesh descriptor — `0x8C` bytes

Each object descriptor partitions geometry and participates in a resolved
hierarchy.

| Offset | Size | Type | Current name | Confidence |
|---:|---:|---|---|---|
| `0x00` | 4 | `u32` | flags | Partially decoded |
| `0x04` | 4 | `u32` | `moverFlags` | Importer-derived name; semantic unknown |
| `0x08` | 4 | `u32` | `meshID` | Confirmed identity |
| `0x0C` | 4 | `u32` | `scriptID` | Name plausible; game-level role not fully traced |
| `0x10` | 20 | `char[20]` | name | Confirmed data |
| `0x24` | 12 | `float3` | position | Confirmed serialized vector |
| `0x30` | 4 | `s32` | parent ID | Confirmed link |
| `0x34` | 4 | `s32` | first-child ID | Confirmed link |
| `0x38` | 4 | `s32` | next-sibling ID | Confirmed link |
| `0x3C` | 4 | `u32` | unknown count/field | Unknown |
| `0x40` | 4 | `u32` | vertex count | Confirmed |
| `0x44` | 4 | `u32` | triangle count | Confirmed |
| `0x48` | 4 | `u32` | rectangle count | Confirmed |
| `0x4C` | 4 | `f32` | unknown | Unknown |
| `0x50` | 4 | `f32` | unknown | Unknown |
| `0x54` | 4 | `f32` | unknown | Unknown |
| `0x58` | 4 | `f32` | unknown | Unknown |
| `0x5C` | 12 | `float3` | negative/min bounding extent | Strongly plausible |
| `0x68` | 12 | `float3` | positive/max bounding extent | Strongly plausible |
| `0x74` | 4 | `f32` | unknown | Unknown |
| `0x78` | 4 | `f32` | unknown | Unknown |
| `0x7C` | 4 | `f32` | unknown | Unknown |
| `0x80` | 12 | `float3` | bone/local child position | Strongly reconstructed |

The descriptor count comes from:

```text
root + 0xE0
```

The old OpenNomad `Header` type may still contain compatibility aliases such as
`mesh_count`. Those aliases are **not additional serialized fields**.

---

# 13. Global geometry streams are partitioned by object order

Objects do not need explicit offsets for their own vertices/triangles/quads.

Runtime walks descriptors in serialized order and maintains cumulative counts:

```text
object0.vertices =
    verticesBase

object1.vertices =
    verticesBase + object0.vertexCount * 0x20

objectN.vertices =
    verticesBase + sum(previous vertexCounts) * 0x20
```

Likewise:

```text
objectN.triangles =
    trianglesBase + sum(previous triangleCounts) * 0x1C

objectN.rectangles =
    rectanglesBase + sum(previous rectangleCounts) * 0x20
```

A robust parser should verify that cumulative per-object counts are consistent
with the root totals:

```text
root triangleCount
root rectangleCount
root vertexCount
```

rather than trusting either source blindly.

---

# 14. Runtime object expansion

Runtime expands each serialized `0x8C` object descriptor into a larger mutable
runtime object of approximately:

```text
0xB8 bytes
```

Recovered pointer relationships include:

| Runtime offset | Meaning |
|---:|---|
| `+0x00` | pointer back to serialized descriptor |
| `+0x04` | resolved next sibling |
| `+0x08` | resolved parent |
| `+0x0C` | resolved first child |
| `+0x10` | first owned vertex |
| `+0x14` | first owned triangle |
| `+0x18` | first owned rectangle |
| `+0x20` | optional relationship-record back-reference |

These are **runtime-only pointers** and must not be added to the file schema.

A modern implementation should instead construct stable indices/references in a
separate runtime representation.

## 14.1 Character actor representative is independent of the hierarchy root

Runtime character construction around `0x0041A730` walks the expanded runtime
objects and computes `triangleCount + rectangleCount` from object fields
`+0x44/+0x48`. The first object with the greatest polygon total is retained;
equal later totals do not replace it. Around `0x0041A7FD`, Runtime writes that
object pointer to `actor+0x08`.

Helper `0x0041C300` finds an actor by exact identity against `actor+0x08`.
This representative is independent of `Serialized3DORootV4+0xB4` and may be a
child. Retail examples demonstrate the distinction:

```text
HO1_FN.3DO:  hierarchy root UBassin [2], actor representative UTete [17]
HO1_FNM.3DO: hierarchy root UBassin [2], actor representative UVisage [19]
```

OpenNomad represents these identities as `Model3DOData::root_mesh_index` and
`ModelResource::actor_object_index`, respectively. They are not aliases.

---

# 15. Hierarchy resolution

## 15.1 Identity and links

The object table's:

```text
meshID
```

is the identity used to resolve:

```text
rootObjectId
parentID
firstChildID
nextSiblingID
relationship object IDs
```

Runtime-style loading is therefore naturally two-pass:

```text
pass 1:
    read all serialized descriptors
    build meshID -> descriptor/runtime object map

pass 2:
    resolve parent/child/sibling/root/relationship links
```

## 15.2 Root-headed forest traversal

The designated object from:

```text
root + 0xB4
```

is the traversal head. It does not imply that every reachable object is a
descendant of that descriptor.

OpenNomad now mirrors that model and records which descriptors are reachable by:

```text
top-level chain:
    rootObjectId
      -> next sibling
      -> next sibling

for each top-level object:
    first child
      -> next sibling
      -> descendants
```

`GRID.3DO` is the confirmed retail counterexample to treating `rootObjectId`
as a single tree root:

```text
rootObjectId = 0

circle01  meshID=0 parentID=-1 nextSiblingID=1
circle2   meshID=1 parentID=-1 nextSiblingID=2
introgrid meshID=2 parentID=-1 nextSiblingID=-1
```

All three records are serialized top-level objects. The serialized structure
and values above are retail-data-confirmed. OpenNomad's traversal of the
root-headed parentless sibling chain is the general Runtime-faithful
interpretation supported by that structure; the exact Runtime machine-code
loop over this specific chain has not yet been independently documented.

Disconnected serialized descriptors should not automatically become visible
world geometry merely because they exist in the table.

## 15.3 Cycle and consistency validation

A modern parser/runtime should reject or diagnose:

- duplicate mesh IDs;
- unresolved required root ID;
- hierarchy cycles;
- sibling loops;
- a child listed under one object while naming a different `parentID`;
- out-of-range resolved references.

The 1999 Runtime can rely more heavily on valid retail data; OpenNomad should
not.

---

# 16. Runtime transform model

Current OpenNomad transform handling is based on recovered Runtime composition,
not the old Blender-import convenience transform.

Initial per-object runtime state is approximately:

```text
local orientation = identity
scale             = (1, 1, 1)
animation matrix  = absent
```

## 16.1 Top-level local offset

For every serialized top-level object (`parentID == -1`), including siblings
after the distinguished root/head:

```text
localOffset = serialized object.position
```

## 16.2 Child local offset

For a child hierarchy object:

```text
localOffset = serialized object.bonePosition
```

This distinction was necessary to fix hierarchical models whose child geometry
was displaced when the normal object position was applied to every node.

## 16.3 Composition

Using Runtime's native matrix/vector convention, conceptually:

```text
effectiveLocalMatrix =
    localMatrix
    [then animationMatrix when active]

childWorldMatrix =
    effectiveLocalMatrix * parentWorldMatrix

childWorldTranslation =
    childLocalOffset transformed by parentWorldMatrix
    + parentWorldTranslation
```

OpenNomad centralizes the exact row/column/basis details in
[`runtime-coordinate-math.md`](runtime-coordinate-math.md).

Do not “fix” the serialized 3DO axes inside the parser.

---

# 17. Joint-only objects and skin parents

Mesh flag bit 0 is used by the current hierarchy/skinning model as:

```text
joint-only / do-not-display
```

A joint-only object participates in hierarchy transforms but does not submit its
own geometry.

For skin ownership, OpenNomad walks upward from a mesh's hierarchy parent,
skipping joint-only nodes, until it finds the nearest non-joint ancestor:

```text
mesh
  -> parent
      -> joint-only?
          -> parent
              -> ...
                  -> nearest non-joint ancestor
```

That resolved node is the current **skin parent**.

Parented triangle vertex references use that skin parent's vertex block.

This model is materially stronger than the old documentation's suggestion that
the `0x8000` bit was merely an importer hypothesis.

The exact original source names for “joint-only” and “skin parent” remain
unknown.

---

# 18. Relationship records — fixed `0x1C` records plus ID lists

The directory field at:

```text
+0x20
```

has historically been called the “doors” section.

Runtime establishes a more general object relationship structure, so this
document uses `relationshipsOffset` until a narrower semantic is proven.

Fixed record:

| Offset | Size | Type | Meaning |
|---:|---:|---|---|
| `0x00..0x13` | `0x14` | bytes | unknown |
| `0x14` | 4 | `u32` | object ID count |
| `0x18` | 4 | `u32` | offset to object-ID list, relative to relationship-section base |

Runtime:

1. relocates the ID-list offset against the relationship-section base;
2. walks `objectCount` 32-bit mesh IDs;
3. resolves each mesh ID to a runtime object pointer;
4. gives resolved runtime objects a back-reference to the relationship record.

Consequences:

- the object list offset is **not relative to the 3DO root**;
- variable ID arrays accompany the fixed records;
- do not overwrite IDs with pointers in a modern parser.

The semantics of the first `0x14` bytes and whether some/all records represent
doors remain unresolved.

---

# 19. Camera records — `0x34` bytes

**Confirmed — Runtime.** The lookup at `0x00446AA0` walks `root+0xDC` records from `camerasOffset` with a `0x34` stride and compares the fixed name at record `+0x00`. The camera accessors/setters around `0x00446B70..0x00446C20` establish the complete payload:

```c
struct CameraRecordV4 {
    char  name[20];
    float eyeX;
    float eyeY;
    float eyeZ;
    float targetX;
    float targetY;
    float targetZ;
    float roll;
    float horizontalFov;
}; // 0x34
```

Current map:

| Offset | Size | Meaning |
|---:|---:|---|
| `0x00` | 20 | fixed-width camera name |
| `0x14` | 4 | eye X, Runtime-native inches |
| `0x18` | 4 | eye Y, Runtime-native inches |
| `0x1C` | 4 | eye Z, Runtime-native inches |
| `0x20` | 4 | target X, Runtime-native inches |
| `0x24` | 4 | target Y, Runtime-native inches |
| `0x28` | 4 | target Z, Runtime-native inches |
| `0x2C` | 4 | roll, degrees |
| `0x30` | 4 | horizontal field of view, degrees |

These coordinate floats are already in Runtime's native 3DO space. They are not serialized IAM AREA/SCENE camera dwords and must not pass through the `serialized * 39.370078... / 256 - 1` camera-record normalization.
 
Structured IAM/SCX camera functions resolve these records by name. In particular `SelectCamera` stores one resolved record as the scene's current camera, while `InterpolateCameras` mutates camera A's eight float fields in place toward camera B. The record name is not overwritten by interpolation.

The current scene camera is subsequently exposed to camera-controller mode 13, which continuously copies its eight live fields into the rendered camera.

---

# 20. Explicit light record — `0x130` bytes

Runtime consumes:

```text
root+0xF0
```

records from:

```text
lightsOffset
```

with stride:

```text
0x130 / 304 bytes
```

Current structural decode:

| Offset | Size | Type | Current interpretation |
|---:|---:|---|---|
| `0x00` | 4 | `u32` | two flag words / flags, unresolved |
| `0x04` | 20 | `char[20]` | light name |
| `0x18` | 4 | `f32` | attenuation end |
| `0x1C` | 4 | `f32` | attenuation start |
| `0x20` | 4 | `f32` | intensity multiplier |
| `0x24` | 4 | `f32` | unknown |
| `0x28` | 4 | `f32` | unknown |
| `0x2C` | 4 | bytes | colour in `B,G,R,A` file order |
| `0x30..0xEF` | `6 * 0x20` | repeated blocks | each begins with a `float3`, followed by 20 unknown bytes |
| `0xF0..0x12F` | `0x40` | bytes | unknown tail |

Current OpenNomad use interprets:

```text
point[0] = light position
point[1] = target / direction point
```

and derives a normalized direction:

```text
normalize(point[1] - point[0])
```

The remaining four point slots appear compatible with cone/frustum-shape data.

The reference importer associates spotlight cone values equivalent to a roughly
40-degree full hotspot and 120-degree full falloff, but those exact semantic
names/angles should remain importer-derived until the full Runtime light
consumer is documented.

Similarly, attenuation names and intensity are strongly plausible and work in
practice, but the serialized record itself does not provide source symbols.

---

# 21. Mesh flags

Preserve the entire 32-bit flag word. Only some bits are currently understood.

| Bit | Mask | Working name | Current evidence |
|---:|---:|---|---|
| 0 | `0x00000001` | joint-only / do-not-display | Strongly corroborated by hierarchy/skinning behavior |
| 2 | `0x00000004` | vertex-lit | Strongly corroborated; OpenNomad uses stored vertex colour lighting |
| 4 | `0x00000010` | has-parent | Importer-derived; hierarchy IDs are independently authoritative |
| 5 | `0x00000020` | has-children | Importer-derived |
| 11 | `0x00000800` | alpha test | Strongly plausible/render-path use; exact original state mapping still under study |
| 12 | `0x00001000` | alpha blend | Partially Runtime-confirmed; parser changes runtime alpha state |
| 13 | `0x00002000` | additive | Importer-derived/render-mode working name |
| 14 | `0x00004000` | subtractive | Importer-derived/render-mode working name |
| 20 | `0x00100000` | mirror | Partially Runtime-confirmed; parser stores a flagged object in special global mirror state |
| 21 | `0x00200000` | FPS arm | Importer-derived |
| 22 | `0x00400000` | face morph | Importer-derived/plausible |
| 23 | `0x00800000` | invisible | Strongly plausible; treated as non-rendered |
| 24 | `0x01000000` | UV scroll/translate U | **Confirmed — Runtime** |
| 25 | `0x02000000` | UV scroll/translate V | **Confirmed — Runtime** |
| 26 | `0x04000000` | environment mapped | Importer-derived; independent Runtime semantics incomplete |
| 27 | `0x08000000` | underwater | Importer-derived |
| 29 | `0x20000000` | water surface / special transparent surface | Strong Runtime side effects; semantic name still partly historical |
| 30 | `0x40000000` | water-related unknown | Importer-derived |

## 21.1 Bit 12 runtime behavior

When the alpha-blend bit is present, the recovered parser initializes a runtime
alpha-like scalar to approximately:

```text
0.5
```

instead of the ordinary:

```text
1.0
```

That is direct evidence that bit 12 changes transparent/blended presentation
state.

## 21.2 Bit 20 runtime behavior

A bit-20 object is saved into a dedicated runtime global slot during 3DO load.

That strongly supports special mirror handling, although the complete reflection
rendering path is outside the serialized format itself.

## 21.3 Bit 29 runtime behavior

For bit 29 Runtime performs a compound mutation approximately equivalent to:

```text
runtime alpha-like scalar = 0.7
set bit 12 (alpha blend)
clear bit 13
clear bit 14
```

This is strong evidence for a special blended water/surface mode.

It is more useful than simply trusting the importer's `WaterSurface` label, but
the final renderer behavior should still be documented from the draw path.

## 21.4 Blend-mode precedence in OpenNomad

Current OpenNomad derives a modern blend mode approximately as:

```text
if alpha blend:
    if subtractive -> subtractive
    else if additive -> additive
    else -> ordinary alpha blend
else if alpha test:
    -> alpha test
else:
    -> opaque
```

This is an OpenNomad rendering model informed by the reference importer and
known Runtime flag effects. It should not be mistaken for a completely
recovered Direct3D state table until the original draw-state setup is fully
traced.

---

# 22. Baked/vertex lighting

The current evidence distinguishes two broad lighting sources:

1. colour already stored per vertex / mesh-light effects;
2. explicit `0x130` light records.

The root field at `+0xEC` is currently associated with the former class, while
`+0xF0` controls explicit records.

A mesh using the vertex-lit flag can therefore be rendered from its serialized
vertex colours without inventing an explicit light object for every authoring
light that contributed to those colours.

OpenNomad currently uses vertex colours directly for vertex-lit geometry and a
fallback directional-light model for geometry that relies on ordinary normals.

That modern fallback is not part of the 3DO serialization.

---

# 23. Sprite resources are ordinary 3DO resources used specially

Omikron's sprite system does not require a separate geometry file format.

A loaded 3DO can be used as a sprite resource.

Runtime/OpenNomad evidence currently supports:

```text
3DO root / object
    |
    +-- vertex data -> frame dimensions/corners
    |
    +-- rectangle records -> frame descriptors
    |
    +-- material index -> texture
    |
    +-- rectangle UV bytes -> page-space sprite UV
```

IAM functions documented in [`iam-script-functions.md`](iam-script-functions.md)
include operations such as:

```text
Display3DSprite
Display3DSpriteOnPath
SetSpriteType
SetSpriteFrame
ScaleSpriteOnX
ScaleSpriteOnY
SetSpriteRolling
SetSpritePalette
SetSpriteDefaultPalette
MorphPaletteSprite
```

This makes the dual-use rectangle interpretation an engine architecture feature,
not an accidental OpenNomad shortcut.

## 23.1 Frame selection

Original Runtime `SetSpriteFrame` logic around:

```text
0x0048EF10
```

validates/stores a frame index.

The sprite renderer around:

```text
0x004969C0
```

indexes `0x20` rectangle/frame records.

OpenNomad's current fallback rule when serialized root `frameCount == 0` is:

```text
frame count = selected object's rectangle count
```

Again: useful and currently working, but not yet proven as an original Runtime
fallback.

---

# 24. Companion `.3DT` and embedded texture payloads

A 3DO material describes texture data but does not contain the actual palette
and indexed pixels in the structural records above.

Standalone convention:

```text
NAME.3DO
NAME.3DT
```

For each material, 3DT-style data provides:

```text
3 * (1 << bitsPerPixel) palette bytes
dataSize pixel-payload bytes
```

When a 3DO core is embedded in SCX/resource data, the enclosing resource stream
can provide those payload bytes inline.

Therefore model the concepts as:

```text
3DO structural core
    +
texture payload source
```

not:

```text
3DO parser always opens same-basename .3DT
```

This separation allows the same decoder to serve standalone and embedded
resources.

See [`3dt.md`](3dt.md).

---

# 25. Embedded 3DO resources inside SCX

The same version-4 `OD3X` core occurs inside retail scenario resources such as:

```text
aventure.SCX
Grid.SCX
```

Observed embedded cores follow the same directory/root/record model.

Important distinctions:

```text
standalone:
    .3DO structure
    sibling .3DT payload

SCX-embedded:
    .3DO structure
    enclosing resource supplies 3DT-style payload
```

Do not infer a different 3DO geometry format merely because the payload source
is different.

Likewise, do not assume an SCX 3DO starts at the beginning of the entire SCX;
SCX has its own resource/container grammar.

---

# 26. Documentation-oriented structures

These are illustrative and should not be copied blindly into a packed runtime
ABI.

```c
#pragma pack(push, 1)

typedef struct CoreDirectoryV4 {
    char     magic[4];            // 0x00, "OD3X"
    uint32_t version;             // 0x04, 4
    uint32_t rootOffset;          // 0x08
    uint32_t materialsOffset;     // 0x0C
    uint32_t verticesOffset;      // 0x10
    uint32_t trianglesOffset;     // 0x14
    uint32_t rectanglesOffset;    // 0x18
    uint32_t objectsOffset;       // 0x1C
    uint32_t relationshipsOffset; // 0x20
    uint32_t camerasOffset;       // 0x24
    uint32_t lightsOffset;        // 0x28
} CoreDirectoryV4;                // 0x2C

typedef struct MaterialRecordV4 {
    char     name[20];            // 0x00
    char     textureName[20];     // 0x14
    char     paletteName[20];     // 0x28
    uint32_t dataSize;            // 0x3C
    uint16_t texturePage;         // 0x40
    uint16_t textureSlot;         // 0x42
    uint16_t palettePage;         // 0x44
    uint16_t paletteSlot;         // 0x46
    uint16_t bitsPerPixelRaw;     // 0x48
    uint8_t  atlasU;              // 0x4A
    uint8_t  atlasV;              // 0x4B
    uint16_t width;               // 0x4C
    uint16_t height;              // 0x4E
} MaterialRecordV4;               // 0x50

typedef struct VertexV4 {
    float    x, y, z;             // 0x00
    float    nx, ny, nz;          // 0x0C
    uint32_t unknown18;           // 0x18
    uint8_t  b, g, r, a;          // 0x1C
} VertexV4;                        // 0x20

typedef struct TriangleV4 {
    uint16_t vertexRef[3];        // 0x00
    uint8_t  uv[6];               // 0x06
    int32_t  materialIndex;       // 0x0C
    int32_t  unknown10;           // 0x10
    int32_t  unknown14;           // 0x14
    int32_t  unknown18;           // 0x18
} TriangleV4;                      // 0x1C

typedef struct RectangleV4 {
    uint16_t vertexRef[4];        // 0x00
    uint8_t  uv[8];               // 0x08
    int32_t  materialIndex;       // 0x10
    int32_t  unknown14;           // 0x14
    int32_t  unknown18;           // 0x18
    int32_t  unknown1C;           // 0x1C
} RectangleV4;                     // 0x20

typedef struct SerializedObjectV4 {
    uint32_t flags;               // 0x00
    uint32_t moverFlags;          // 0x04
    uint32_t meshID;              // 0x08
    uint32_t scriptID;            // 0x0C
    char     name[20];            // 0x10
    float    position[3];         // 0x24
    int32_t  parentID;            // 0x30
    int32_t  firstChildID;        // 0x34
    int32_t  nextSiblingID;       // 0x38
    uint32_t unknown3C;           // 0x3C
    uint32_t vertexCount;         // 0x40
    uint32_t triangleCount;       // 0x44
    uint32_t rectangleCount;      // 0x48
    float    unknown4C[4];        // 0x4C
    float    boundsNeg[3];        // 0x5C
    float    boundsPos[3];        // 0x68
    float    unknown74[3];        // 0x74
    float    bonePosition[3];     // 0x80
} SerializedObjectV4;             // 0x8C

typedef struct RelationshipRecordV4 {
    uint8_t  unknown00[0x14];     // 0x00
    uint32_t objectCount;         // 0x14
    uint32_t objectListOffset;    // 0x18
} RelationshipRecordV4;           // 0x1C

typedef struct CameraRecordV4 {
    char  name[20];               // 0x00
    float parameters[8];          // 0x14
} CameraRecordV4;                 // 0x34

#pragma pack(pop)
```

The root is intentionally not presented as a fully named packed C struct because
most of its first `0xB4` bytes remain unknown.

---

# 27. Recommended OpenNomad representation

Prefer three conceptual layers:

```text
Serialized3DO
    immutable file fields / raw IDs / raw flags

Resolved3DO
    validated indices
    meshID -> object links
    hierarchy
    geometry ownership
    resource relationships

Runtime3DOInstance
    mutable transforms
    animation matrices
    scale
    sprite state
    renderer/GPU resources
```

This avoids reproducing Runtime's in-place pointer relocation and allocator
scratch fields inside the serialized record itself.

## 27.1 Preserve raw information

Keep:

- raw flag words;
- unknown root bytes;
- unknown object floats;
- raw triangle references if practical;
- all three light counts;
- material pre-load page/slot values;
- relationship IDs.

Future reverse engineering frequently turns “padding” into meaningful state.

---

# 28. Parser and validation invariants

A robust implementation should:

1. require `OD3X`;
2. support version `4` explicitly;
3. follow `rootOffset`, not a hard-coded `0x2C`;
4. bounds-check every non-empty section;
5. parse root totals and object-local counts independently;
6. verify cumulative geometry counts do not overrun global streams;
7. reject duplicate object IDs where resolution would be ambiguous;
8. require the designated root ID to resolve when objects exist;
9. resolve hierarchy links in a second pass;
10. detect hierarchy/sibling cycles;
11. preserve disconnected descriptors but do not automatically render them;
12. preserve unresolved triangle reference bits;
13. resolve bit-15 triangle references against the recovered skin parent;
14. keep static-quad and sprite-frame interpretations separate;
15. preserve all unknown mesh flags;
16. keep texture payload parsing outside the structural 3DO reader;
17. treat cameras/relationships/lights as optional sections driven by counts;
18. preserve serialized light counts before emulating Runtime mutations.

---

# 29. Current OpenNomad behavior that is intentionally not “the format”

Several useful implementation choices must not be promoted to file-format facts.

## 29.1 Rendering disconnected objects

OpenNomad follows root-driven hierarchy reachability for ordinary geometry.

This mirrors recovered Runtime traversal and is preferable to drawing every
serialized descriptor unconditionally.

## 29.2 Material fallback

Current OpenNomad can fall back to material 0 when a face references an invalid
material so development can continue with partially understood assets.

Retail Runtime behavior for every malformed-material case is not defined here.

A strict asset validator should report the invalid reference.

## 29.3 Sprite `frameCount == 0`

OpenNomad currently uses rectangle count as a provisional frame count when the
root field is zero.

This is compatibility behavior, not a confirmed on-disk rule.

## 29.4 Modern GPU textures

Runtime packs indexed textures and palettes into shared pages.

OpenNomad normally expands material textures into independent GPU RGBA textures.

That changes runtime representation, not the serialized 3DO/3DT interpretation.

---

# 30. Corrections relative to older documentation/tooling

The most important corrections now established are:

### Directory/root separation

```text
+0x08 = rootOffset
```

not `versionMinor`.

### Object count

The real serialized object count is:

```text
root + 0xE0
```

Any OpenNomad `mesh_count` compatibility alias is not another serialized count.

### Root-driven hierarchy

The model has an explicit root ID, and child/sibling relationships are resolved
by object ID.

### Child transform source

The root uses serialized `position`; children use `bonePosition` as their local
hierarchical offset in the recovered runtime transform model.

### Triangle parent references

Bit `0x8000` and the low `0x03FF` index are part of the currently working
parented/skinned triangle reference model. Bits `10..14` remain unknown.

### Rectangle dual use

`0x20` records are ordinary quads in mesh rendering but can also be sprite-frame
descriptors with a two-corner interpretation.

### UV scroll flags

Bits 24 and 25 are U/V cyclic translation, not “skybox”.

### Light records

Only the count at root `+0xF0` drives explicit `0x130`-byte light records in the
recovered parser path.

### Authoring provenance

Retail `.3DO` should not be called the native GEM project format without direct
evidence.

---

# 31. Open questions

Still unresolved:

- complete semantics of root `+0x00..0x47`;
- complete semantics of root `+0x4C..+0xB3`;
- exact semantic name of root `+0xB8`;
- root `+0xD4` and `+0xD8`;
- original distinction among light counts `+0xE8`, `+0xEC`, `+0xF0`;
- why observed retail `frameCount` values are zero while Runtime exposes the
  field to sprite logic;
- exact camera float meanings;
- complete relationship-record semantics;
- exact `scriptID` role in object descriptors;
- `moverFlags`;
- object `+0x3C`;
- object floats `+0x4C..+0x58`;
- object floats `+0x74..+0x7C`;
- exact semantic distinction between `position` and `bonePosition` across every
  model class;
- triangle reference bits `10..14`;
- whether rectangles can contain any alternate-owner encoding;
- full mesh-flag map;
- exact original Direct3D blend-state mapping;
- full mirror implementation;
- exact environment-map semantics for bit 26;
- exact water/underwater interactions;
- vertex `+0x18`;
- vertex colour alpha semantics;
- exact authoring text encoding;
- complete explicit light flag/shape semantics;
- exact original sprite-frame count derivation for all resource types;
- whether sprite rectangles ever require the static quad's ignored point/UV
  fields for another effect;
- platform/build differences.

---

# 32. Useful Runtime locations

| Address | Role |
|---:|---|
| `0x0044DF10` | version-4 3DO parse/initialization path |
| `0x0044EC80` | embedded/in-memory 3DO loading path |
| `0x0048EF10` | sprite-frame selection; uses root frame-count field |
| `0x004969C0` | sprite rendering path using rectangle/frame data |
| `0x004955A9` | polygon path with recovered U-scroll behavior |
| `0x00495A33` | polygon path with recovered U-scroll behavior |
| `0x0049749C` | polygon path with recovered UV phase behavior |
| `0x004A75E0` | indexed texture unpack/page upload |
| `0x004A77E0` | palette allocation/load |
| `0x004A7900` | palette upload/callback path |
| `0x004B4B40` | indexed texture decompressor |

Addresses are for the Runtime build identified in section 1.

---

# 33. OpenNomad source locations

Current implementation areas most closely corresponding to this document:

```text
src/core/Core/Omikron/Model3DO.hpp
src/core/Core/Omikron/Model3DO.cpp
src/core/Core/Omikron/Texture3DT.hpp
src/core/Core/Omikron/Texture3DT.cpp

src/core/Core/Sprite/SpriteFrame.hpp
src/core/Core/Sprite/SpriteFrame.cpp
src/core/Core/Sprite/SpriteResource.hpp
src/core/Core/Sprite/SpriteResource.cpp

src/core/Core/WorldRenderer.cpp
src/core/Core/RuntimeMath.*
src/core/Core/RuntimePresentation.*
```

Tests should encode Runtime-derived invariants rather than importer-only
assumptions.

---

# 34. Compact reference

```text
3DO v4
=======

core:
    0x00 "OD3X"
    0x04 version = 4
    0x08 rootOffset
    0x0C materialsOffset
    0x10 verticesOffset
    0x14 trianglesOffset
    0x18 rectanglesOffset
    0x1C objectsOffset
    0x20 relationshipsOffset
    0x24 camerasOffset
    0x28 lightsOffset

root:
    size 0x148
    +0x48 frameCount field
    +0xB4 rootObjectId
    +0xBC triangleCount
    +0xC0 rectangleCount
    +0xC4 vertexCount
    +0xD0 materialCount
    +0xDC cameraCount
    +0xE0 objectCount
    +0xE4 relationshipCount
    +0xE8 aggregate/raw light count
    +0xEC mesh/baked-light count
    +0xF0 explicit light record count

records:
    material      0x50
    vertex        0x20
    triangle      0x1C
    rectangle     0x20
    object        0x8C
    relationship  0x1C + ID array
    camera        0x34
    explicit light 0x130

triangle vertex reference:
    parented = raw & 0x8000
    index    = raw & 0x03FF
    bits 10..14 unresolved

hierarchy:
    IDs resolve by object.meshID
    root chosen by root+0xB4
    root local offset = object.position
    child local offset = object.bonePosition
    parented triangle vertices use nearest non-joint ancestor

rectangle:
    ordinary quad:
        4 points + 4 UV pairs

    sprite frame:
        point 0 = vertex[0]
        point 1 = vertex[2]
        UV 0    = uv[0]
        UV 1    = uv[2]
        sprite UV bytes / 256

texture payload:
    supplied separately as .3DT-style data
```

---

# 35. Secondary reference

Chevluh's Blender importer remains a valuable historical/community reference:

<https://github.com/Chevluh/Omikron_Blender_Importer/blob/main/omikronImporter.py>

Where it conflicts with observed `Runtime.exe` behavior, Runtime takes
precedence.

The importer should be treated as a source of hypotheses and useful terminology,
not as the format specification.
