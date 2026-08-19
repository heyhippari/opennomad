# Omikron `.3DO` model format (version 4)

> **Status:** work-in-progress reverse-engineering documentation for OpenNomad.
>
> This document describes the `.3DO` model format used by the Windows release of *Omikron: The Nomad Soul*, with emphasis on behavior observed directly in `Runtime.exe`. Fields whose purpose is not yet established are deliberately left as unknown or tentative rather than assigned names from older community tooling.

## Source precedence and confidence

The sources used here are, in descending order of authority:

1. **`Runtime.exe` behavior** — authoritative for how the retail game reads and mutates the data.
2. **Observed retail assets**, including standalone resources and `.3DO` objects embedded in `aventure.SCX` and `Grid.SCX` — useful for confirming sizes, counts, offsets, and invariants.
3. **Chevluh's Omikron Blender Importer** — a very useful prior description of the format, but not authoritative where it disagrees with the executable: <https://github.com/Chevluh/Omikron_Blender_Importer/blob/main/omikronImporter.py>

The `Runtime.exe` currently used for this analysis has:

- PE image base: `0x00400000`
- linker timestamp: `1999-10-04 20:31:50`
- SHA-256: `55f7120bfea7891b048c64e3682f3259cdbf2719a43fa24e42254b753c95d2ef`

Addresses in this document refer to that executable and are not expected to be stable across other builds.

Confidence labels used below:

- **Confirmed — Runtime:** directly demonstrated by code in `Runtime.exe`.
- **Confirmed — data:** strongly established from section boundaries and multiple retail assets.
- **Corroborated:** Runtime behavior and asset observations agree, often also agreeing with the Blender importer.
- **Tentative:** plausible interpretation, but the semantic meaning has not yet been demonstrated by Runtime behavior.
- **Unknown:** byte layout is known or bounded, but semantic meaning is not.

## Overview

`.3DO` is Omikron's serialized 3D model format. Version 4 models begin with the four bytes `OD3X`, followed by a small offset directory. The directory points to a separate root block and to global streams for materials, vertices, triangles, quadrilaterals, object descriptors, relationship records, cameras, and lights.

A key correction to older descriptions is that the first `0x2C` bytes form a **directory**, not the beginning of one large fixed header. In particular, the dword at `0x08`, called `versionMinor` by the Blender importer, is actually an offset to the root block. Runtime honors that offset.

All numeric values observed so far are little-endian. Runtime is a 32-bit x86 program and accesses most serialized scalar fields directly.

A typical version 4 core looks conceptually like this:

```text
+-----------------------------+  0x0000
| CoreDirectoryV4             |  0x2C bytes
+-----------------------------+
| ...                         |
+-----------------------------+  directory.rootOffset
| Serialized3DORootV4         |  at least 0x148 bytes
+-----------------------------+
| ...                         |
+-----------------------------+  directory.materialsOffset
| MaterialRecordV4[]          |  0x50 bytes each
+-----------------------------+
| ...                         |
+-----------------------------+  directory.verticesOffset
| VertexV4[]                  |  0x20 bytes each
+-----------------------------+
| ...                         |
+-----------------------------+  directory.trianglesOffset
| TriangleV4[]                |  0x1C bytes each
+-----------------------------+
| ...                         |
+-----------------------------+  directory.quadsOffset
| QuadV4[]                    |  0x20 bytes each
+-----------------------------+
| ...                         |
+-----------------------------+  directory.objectsOffset
| Serialized3DOObjectV4[]     |  0x8C bytes each
+-----------------------------+
| ...                         |
+-----------------------------+  directory.relationshipsOffset
| RelationshipRecordV4[]      |  0x1C bytes each, plus ID lists
+-----------------------------+
| ...                         |
+-----------------------------+  directory.camerasOffset
| CameraRecordV4[]            |  0x34 bytes each (layout partial)
+-----------------------------+
| ...                         |
+-----------------------------+  directory.lightsOffset
| LightRecordV4[]             |  0x130 bytes each
+-----------------------------+
```

The diagram shows logical sections, not a requirement that every section occupy unique space. If a section's count is zero, retail files can give it the same offset as the next section. Parsers should therefore use the directory offsets and counts rather than infer presence from monotonically increasing offsets alone.

## `CoreDirectoryV4` — `0x2C` bytes

**Confirmed — Runtime.** `Parse3DOv4Core` at `0x0044DF10` reads this structure from the beginning of the loaded core block.

| Offset | Size | Type | Field | Status / notes |
|---:|---:|---|---|---|
| `0x00` | 4 | char[4] / u32 | magic | Must be bytes `OD3X`, i.e. little-endian dword `0x5833444F`. Runtime rejects anything else. |
| `0x04` | 4 | u32 | version | Must be `4`. Runtime rejects other versions in this parser. |
| `0x08` | 4 | u32 | `rootOffset` | **Confirmed.** Relative to the beginning of the core. Runtime computes `coreBase + rootOffset`. |
| `0x0C` | 4 | u32 | `materialsOffset` | Relative offset to `MaterialRecordV4[]`. |
| `0x10` | 4 | u32 | `verticesOffset` | Relative offset to the global vertex stream. |
| `0x14` | 4 | u32 | `trianglesOffset` | Relative offset to the global triangle stream. |
| `0x18` | 4 | u32 | `quadsOffset` | Relative offset to the global quad/rectangle stream. |
| `0x1C` | 4 | u32 | `objectsOffset` | Relative offset to the serialized object descriptor table. |
| `0x20` | 4 | u32 | `relationshipsOffset` | Relative offset to relationship records. Older tooling calls these “doors”; that semantic name is not yet confirmed. |
| `0x24` | 4 | u32 | `camerasOffset` | Relative offset to camera records. |
| `0x28` | 4 | u32 | `lightsOffset` | Relative offset to the light records Runtime processes. |

### Important correction to the Blender importer

The Blender importer reads:

```text
signature
versionMajor
versionMinor
materialsOffset
...
```

For version 4 retail data, the field it calls `versionMinor` is **not a minor version number**. Runtime uses it as `rootOffset`.

In every embedded `OD3X` core currently examined in `aventure.SCX` and `Grid.SCX`, `rootOffset == 0x2C`. That makes the directory and root appear contiguous and explains why treating both as one flat header happened to work. Runtime itself does not assume this; a reimplementation should not either.

## `Serialized3DORootV4` — at least `0x148` bytes

The root is addressed as `coreBase + rootOffset`. With the common `rootOffset == 0x2C`, the next material table often starts at absolute core offset `0x174`, giving a root size of `0x148` bytes. This size is also consistent with the field layout used by the Blender importer.

Many early root fields are still unidentified. The following table intentionally documents gaps rather than inventing names.

| Root offset | Size | Type | Field | Status / notes |
|---:|---:|---|---|---|
| `0x00`–`0x47` | `0x48` | bytes | unknown | **Unknown.** |
| `0x48` | 4 | u32 | `frameCount` | **Confirmed — Runtime.** `SetSpriteFrame` (`0x0048EF10`) validates sprite frame indices against this value. It is not necessarily meaningful for ordinary meshes. |
| `0x4C`–`0xB3` | `0x68` | bytes | unknown | **Unknown.** |
| `0xB4` | 4 | u32 | `rootObjectId` | **Confirmed — Runtime.** Parser searches the serialized object table for an object whose `meshID` equals this value and stores the corresponding runtime object as the model's distinguished/root object. Failure is treated as a load error. |
| `0xB8` | 4 | float | global scalar / grayscale level | **Tentative semantic.** Runtime multiplies it by `255.0`, converts to a byte, and replicates that byte to RGB in runtime model state. Exact purpose is not yet known. |
| `0xBC` | 4 | u32 | `triangleCount` | **Confirmed.** Total number of records in the global triangle stream. |
| `0xC0` | 4 | u32 | `quadCount` | **Confirmed.** Total number of records in the global quad stream. |
| `0xC4` | 4 | u32 | `vertexCount` | **Confirmed.** Total number of records in the global vertex stream. |
| `0xC8`–`0xCF` | 8 | bytes | unknown / reserved | **Unknown.** Blender importer treats this region as a u64 reserved field. |
| `0xD0` | 4 | u32 | `materialCount` | **Confirmed.** Number of `0x50`-byte material/texture records. |
| `0xD4` | 4 | u32 | unknown count / mode | **Unknown.** Retail samples contain small non-zero values; parser behavior examined so far has not established its role. |
| `0xD8` | 4 | u32 | unknown / reserved | **Unknown.** Zero in the embedded effect models examined so far. |
| `0xDC` | 4 | u32 | `cameraCount` | **Corroborated.** Controls whether Runtime exposes `coreBase + camerasOffset`; asset section sizes agree with `0x34`-byte camera records. |
| `0xE0` | 4 | u32 | `objectCount` | **Confirmed — Runtime.** Parser allocates `objectCount * 0xB8` runtime object bytes and consumes serialized objects with stride `0x8C`. |
| `0xE4` | 4 | u32 | `relationshipCount` | **Confirmed structurally.** Number of `0x1C`-byte relationship records. Exact game-level meaning remains unconfirmed. |
| `0xE8` | 4 | u32 | serialized light count A / total | **Uncertain semantic.** Runtime overwrites this field during parsing with the value from `root+0xF0`, so its original serialized value is not preserved by the parser. |
| `0xEC` | 4 | u32 | serialized light subtype count | **Unknown semantic.** Blender importer calls this `lightsUnknown1`. |
| `0xF0` | 4 | u32 | processed light count | **Confirmed behavior.** Runtime copies this value to `root+0xE8` and then processes this many `0x130`-byte records starting at `lightsOffset`. Blender importer calls this `lightsUnknown2`. |
| `0xF4`–`0x147` | `0x54` | bytes | unknown | **Unknown.** |

The Blender importer observes that serialized `lightCount` often equals its two subtype counts added together. That may describe authoring-time categories, but Runtime's load path examined here explicitly replaces `root+0xE8` with `root+0xF0` before iterating light records. OpenNomad should preserve the raw serialized values separately if future reverse engineering needs all three.

## Material / texture descriptors — `0x50` bytes each

**Corroborated, with runtime-only mutation fields identified.** Runtime code around `0x004A75E0` treats records as `0x50` bytes; the Blender importer independently parses the same size and leading fields.

| Offset | Size | Type | Field | Status / notes |
|---:|---:|---|---|---|
| `0x00` | 20 | char[20] | `name` | Fixed-width 8-bit string, normally NUL-padded. |
| `0x14` | 20 | char[20] | `bmpFilename` | Fixed-width texture-source name. Historical/authoring meaning likely. |
| `0x28` | 20 | char[20] | `tgaFilename` | Fixed-width texture-source name. Historical/authoring meaning likely. |
| `0x3C` | 4 | u32 | `dataSize` | **Confirmed — Runtime.** Size in bytes of the indexed texture payload as stored. If it equals `width * height`, Runtime treats the payload as raw indexed pixels; otherwise it calls the Omikron decompressor. |
| `0x40` | 2 | u16 | runtime texture allocation field | Serialized meaning unknown. Runtime writes/uses it during texture allocation. Do not treat `0x40..0x47` as immutable reserved bytes after loading. |
| `0x42` | 2 | u16 | runtime texture allocation field | Exact semantic not yet named. |
| `0x44` | 2 | u16 | runtime palette field | Runtime assigns this during palette allocation. Exact coordinate/index meaning remains to be named. |
| `0x46` | 2 | u16 | runtime palette field | Runtime assigns this during palette allocation. Exact coordinate/index meaning remains to be named. |
| `0x48` | 4 | u32 | `bitsPerPixel` + runtime scratch | The serialized low byte is the indexed texture bit depth. Runtime computes `1 << value` from byte `+0x48`. Bytes `+0x4A` and `+0x4B` are later used as runtime scratch. Preserve the original value before mutation if needed. |
| `0x4C` | 2 | u16 | `width` | **Confirmed.** |
| `0x4E` | 2 | u16 | `height` | **Confirmed.** |

The Blender importer models `0x40..0x47` as an opaque u64 `reserved`. That is a reasonable description of the serialized file before load, but it is incomplete as a description of Runtime behavior: the original engine reuses this portion of the loaded record for runtime texture/palette allocation state.

### Strings and encoding

Strings in known records are fixed-width byte arrays with NUL termination/padding. The Blender importer decodes them as CP858. Runtime's exact intended code page has not yet been established, so **CP858 should be treated as an importer convention, not a confirmed format requirement**.

## Vertex stream — `0x20` bytes per vertex

**Confirmed size; field interpretation corroborated.** Runtime assigns object-local vertex stream starts using a cumulative vertex count multiplied by `0x20`.

| Offset | Size | Type | Field | Status / notes |
|---:|---:|---|---|---|
| `0x00` | 4 | float | `x` | Native game-space coordinate. |
| `0x04` | 4 | float | `y` | Native game-space coordinate. |
| `0x08` | 4 | float | `z` | Native game-space coordinate. |
| `0x0C` | 4 | float | `nx` | Vertex normal X. |
| `0x10` | 4 | float | `ny` | Vertex normal Y. |
| `0x14` | 4 | float | `nz` | Vertex normal Z. |
| `0x18` | 4 | u32 | unknown (`t1` in importer) | **Unknown.** |
| `0x1C` | 4 | u32 / bytes | packed vertex color | Blender importer reads bytes as `B, G, R, A`, which corresponds to a little-endian packed `0xAARRGGBB` value. Exact alpha semantics remain to be confirmed. |

### Coordinate conversion is not part of the on-disk structure

The Blender importer transforms a file vector as follows:

```text
Blender X = file X
Blender Y = file Z
Blender Z = -file Y
position *= 0.025
```

That is a conversion into Blender's coordinate convention, not evidence that the serialized structure stores coordinates in that rearranged order. The file contains three ordinary floats in native engine space.

Likewise, `0.025` (`1/40`) should currently be considered a useful importer scale rather than a confirmed physical unit definition. Runtime itself uses a scale-related constant of approximately `39.37007874015748` in the model initialization path, but the exact relationship between native 3DO units and physical units has not yet been completely characterized.

## Triangle stream — `0x1C` bytes per triangle

**Confirmed size; most fields corroborated.** Runtime partitions the triangle stream between objects using cumulative counts with a `0x1C` stride.

| Offset | Size | Type | Field | Status / notes |
|---:|---:|---|---|---|
| `0x00` | 2 | u16 | `vertex0Ref` | Encoded vertex reference; see below. |
| `0x02` | 2 | u16 | `vertex1Ref` | Encoded vertex reference. |
| `0x04` | 2 | u16 | `vertex2Ref` | Encoded vertex reference. |
| `0x06` | 1 | u8 | `u0` | Texture U byte. |
| `0x07` | 1 | u8 | `v0` | Texture V byte. |
| `0x08` | 1 | u8 | `u1` | Texture U byte. |
| `0x09` | 1 | u8 | `v1` | Texture V byte. |
| `0x0A` | 1 | u8 | `u2` | Texture U byte. |
| `0x0B` | 1 | u8 | `v2` | Texture V byte. |
| `0x0C` | 4 | s32/u32 | `materialIndex` | Index into the material/texture descriptor table. Values observed are non-negative. |
| `0x10` | 4 | s32 | unknown (`s2`) | **Unknown.** |
| `0x14` | 4 | s32 | unknown (`s3`) | **Unknown.** |
| `0x18` | 4 | s32 | unknown (`s4`) | **Unknown.** |

### Encoded triangle vertex references

The Blender importer interprets bit `0x8000` as “use a vertex from the parent mesh” and masks the remaining reference with `0x03FF` before indexing. That behavior is useful and clearly related to character skinning, but the complete bit layout has **not yet been re-established from Runtime strongly enough to make the importer's ten-bit mask authoritative**.

For OpenNomad documentation and parser design, the safe current position is:

- bit `0x8000` is a strong candidate for a parent/alternate-owner reference flag;
- the low bits contain a vertex index;
- bits `10..14` should remain documented as **unresolved** until Runtime's primitive-processing path is fully traced;
- do not silently discard those bits solely because the Blender importer uses `& 0x03FF`;
- the exact transform/mesh whose vertex should be used for flagged references also deserves Runtime-level verification.

This is one of the major remaining format questions.

## Quad / rectangle stream — `0x20` bytes per quad

**Confirmed size and layout.** Runtime partitions the quad stream using a `0x20` stride. The sprite renderer also indexes this same stream as `frameIndex * 0x20`, independently confirming the record size.

| Offset | Size | Type | Field | Status / notes |
|---:|---:|---|---|---|
| `0x00` | 2 | u16 | `vertex0Ref` | Vertex reference. |
| `0x02` | 2 | u16 | `vertex1Ref` | Vertex reference. |
| `0x04` | 2 | u16 | `vertex2Ref` | Vertex reference. |
| `0x06` | 2 | u16 | `vertex3Ref` | Vertex reference. |
| `0x08` | 1 | u8 | `u0` | Texture U byte. |
| `0x09` | 1 | u8 | `v0` | Texture V byte. |
| `0x0A` | 1 | u8 | `u1` | Texture U byte. |
| `0x0B` | 1 | u8 | `v1` | Texture V byte. |
| `0x0C` | 1 | u8 | `u2` | Texture U byte. |
| `0x0D` | 1 | u8 | `v2` | Texture V byte. |
| `0x0E` | 1 | u8 | `u3` | Texture U byte. |
| `0x0F` | 1 | u8 | `v3` | Texture V byte. |
| `0x10` | 4 | s32/u32 | `materialIndex` | Material/texture table index. |
| `0x14` | 4 | s32 | unknown (`s2`) | **Unknown.** |
| `0x18` | 4 | s32 | unknown (`s3`) | **Unknown.** |
| `0x1C` | 4 | s32 | unknown (`s4`) | **Unknown.** |

The Blender importer does not apply its parent-reference decoding to quad vertex references. Whether quad references can use any equivalent encoded ownership bits remains **unconfirmed**.

### UV representation

UVs are serialized as bytes, not normalized floats. The Blender importer produces normalized UVs by dividing each `u` by the selected texture's width and each `v` by its height. That normalization is a consumer-side conversion.

Runtime's original renderer works with paletted texture pages/atlas allocation, so future reverse engineering should document the exact original sampling convention before OpenNomad treats the Blender normalization rule as universally exact.

## Serialized object / mesh descriptor — `0x8C` bytes

**Confirmed stride in Runtime; field layout largely corroborated by the importer.** Runtime allocates a separate `0xB8`-byte runtime object for every serialized `0x8C`-byte descriptor.

| Offset | Size | Type | Field | Status / notes |
|---:|---:|---|---|---|
| `0x00` | 4 | u32 | `flags` | Mesh/render flags. Only some meanings are Runtime-confirmed; see [Mesh flags](#mesh-flags). |
| `0x04` | 4 | u32 | `moverFlags` | **Tentative name from importer.** Exact semantics unresolved. |
| `0x08` | 4 | u32 | `meshID` | **Confirmed identity field.** Used to resolve root object, hierarchy IDs, and relationship object IDs. |
| `0x0C` | 4 | u32 | `scriptID` | Importer name; game-level semantics not yet independently traced. |
| `0x10` | 20 | char[20] | `name` | Fixed-width object name. |
| `0x24` | 12 | float3 | `position` | Object position in native engine coordinates. |
| `0x30` | 4 | s32 | `parentID` | **Confirmed ID link.** `-1` is used for no parent in known assets/tooling. Runtime resolves matching `meshID` to a runtime pointer. |
| `0x34` | 4 | s32 | `firstChildID` | **Confirmed ID link.** Runtime resolves it by `meshID`. |
| `0x38` | 4 | s32 | `nextSiblingID` | **Confirmed ID link.** Runtime resolves it by `meshID`. |
| `0x3C` | 4 | u32 | unknown count / field | `unknown07_count1` in Blender importer. |
| `0x40` | 4 | u32 | `vertexCount` | **Confirmed.** Number of consecutive global vertices owned by this object. |
| `0x44` | 4 | u32 | `triangleCount` | **Confirmed.** Number of consecutive global triangles owned by this object. |
| `0x48` | 4 | u32 | `quadCount` | **Confirmed.** Number of consecutive global quads owned by this object. |
| `0x4C` | 4 | float | unknown | `unknown08` in importer. |
| `0x50` | 4 | float | unknown | `unknown09`. |
| `0x54` | 4 | float | unknown | `unknown10`. |
| `0x58` | 4 | float | unknown | `unknown11`. |
| `0x5C` | 12 | float3 | bounding extent / minimum | Blender importer calls this `boxExtentNeg`. Bounding-volume semantics are plausible but not fully traced in Runtime. |
| `0x68` | 12 | float3 | bounding extent / maximum | Blender importer calls this `boxExtentPos`. |
| `0x74` | 4 | float | unknown | `unknown18`. |
| `0x78` | 4 | float | unknown | `unknown19`. |
| `0x7C` | 4 | float | unknown | `unknown20`. |
| `0x80` | 12 | float3 | `bonePosition` | Importer name; consistent with skeletal use, but exact Runtime semantics still need documentation. |

### Global streams are partitioned by object order

This is an important Runtime-confirmed behavior. Serialized object descriptors do **not** need to carry explicit byte offsets to their geometry. `Parse3DOv4Core` walks objects in table order and maintains cumulative counts:

```text
object[0].vertices = globalVertices + 0 * 0x20
object[1].vertices = globalVertices + object[0].vertexCount * 0x20
...

object[n].triangles = globalTriangles + cumulativeTriangleCount * 0x1C
object[n].quads     = globalQuads     + cumulativeQuadCount     * 0x20
```

Thus, the per-object `vertexCount`, `triangleCount`, and `quadCount` fields partition the three global streams sequentially in serialized object order.

The sum of the object counts should normally agree with the corresponding root totals. A robust reimplementation should validate this rather than assume it.

### Runtime object created from a serialized descriptor

For reverse-engineering context, Runtime creates a `0xB8`-byte runtime object for each `0x8C`-byte serialized object. Relevant initialized pointers include:

| Runtime offset | Meaning |
|---:|---|
| `0x00` | pointer back to the serialized `0x8C` descriptor |
| `0x04` | resolved next-sibling runtime object |
| `0x08` | resolved parent runtime object |
| `0x0C` | resolved first-child runtime object |
| `0x10` | first vertex owned by this object |
| `0x14` | first triangle owned by this object |
| `0x18` | first quad owned by this object |
| `0x20` | optional back-reference to a relationship record |

These runtime pointers are **not fields in the `.3DO` file**; they are listed because they directly establish the semantics of several serialized IDs and counts.

## Relationship records — `0x1C` bytes each, plus ID arrays

Older tooling and field names sometimes describe the section at directory offset `0x20` as “doors”. Runtime behavior observed so far establishes a relationship between records and object IDs, but does not yet justify naming every record a door.

Each fixed record is `0x1C` bytes:

| Offset | Size | Type | Field | Status / notes |
|---:|---:|---|---|---|
| `0x00`–`0x13` | `0x14` | bytes | unknown | **Unknown.** |
| `0x14` | 4 | u32 | `objectCount` | **Confirmed — Runtime.** Number of object IDs in the associated list. |
| `0x18` | 4 | u32 | `objectListOffset` | **Confirmed — Runtime.** Initially a relative offset from the beginning of the relationship section. Runtime relocates this field in place by adding the section base. |

After relocation, Runtime walks `objectCount` 32-bit values at `objectListOffset`. Each value is treated as a serialized `meshID`; a matching ID is replaced with the corresponding runtime object pointer. The runtime object also receives a back-pointer to the relationship record.

Consequences for a parser:

- `objectListOffset` is **relative to `relationshipsOffset`, not to the core root**;
- the section contains fixed records followed or accompanied by variable-length arrays of object IDs;
- do not mutate serialized IDs into pointers in a modern parser; retain an immutable serialized representation and build resolved references separately.

The game-level purpose of the first `0x14` bytes and the complete meaning of each relationship remain open questions.

## Camera records — `0x34` bytes each

**Confirmed — data for record size; semantic layout incomplete.** Camera section spans in retail embedded models consistently fit `cameraCount * 0x34`. Camera names such as `CAMERA` and `CAMERA1` are visible at the start of records.

Current structural description:

| Offset | Size | Type | Field | Status / notes |
|---:|---:|---|---|---|
| `0x00` | 20 | char[20] | `name` | **Confirmed — data.** |
| `0x14` | 32 | float[8] | camera parameters | **Unknown semantics.** Likely contains position/orientation/projection values, but individual fields should not be named until traced in Runtime. |

Runtime's 3DO core parser merely exposes the camera-section pointer; it does not transform each camera record there. The consumer of this section still needs to be reversed to assign authoritative names to the eight floats.

## Light records — `0x130` bytes each

**Confirmed stride in Runtime.** The 3DO parser increments the processed light pointer by `0x130` for each light. The field subdivision below comes from the Blender importer and observed data; individual semantic names remain less certain than the stride.

| Offset | Size | Type | Field | Status / notes |
|---:|---:|---|---|---|
| `0x00` | 4 | u32 | `flags` | Meaning not fully documented. |
| `0x04` | 20 | char[20] | `name` | Fixed-width name. |
| `0x18` | 4 | float | unknown0 | **Unknown.** |
| `0x1C` | 4 | float | unknown1 | **Unknown.** |
| `0x20` | 4 | float | intensity-like value | Called `intensity` by importer; needs Runtime confirmation. |
| `0x24` | 4 | float | angle-like value 0 | Importer interpretation; tentative. |
| `0x28` | 4 | float | angle-like value 1 | Importer interpretation; tentative. |
| `0x2C` | 4 | bytes/u32 | color-like value | Importer reads four component bytes. Exact packing/alpha meaning unresolved. |
| `0x30` | `6 * 0x20` | repeated block | six point/parameter blocks | Importer reads each as `float3 position` followed by 20 unknown bytes. This subdivision fits exactly but semantic use still needs tracing. |
| `0xF0` | `0x40` | bytes | unknown tail | **Unknown.** |

Runtime performs additional per-light initialization through functions called from `Parse3DOv4Core`; those consumers should be reversed before assigning final names to the record's angle, point, and color fields.

## Mesh flags

The Blender importer provides the following useful flag names. They should not all be treated as proven format semantics yet.

| Bit | Mask | Importer name | Current status |
|---:|---:|---|---|
| 0 | `0x00000001` | `doNotDisplay_jointOnly` | **Tentative.** Strongly plausible from importer behavior and skeleton structure, not yet fully Runtime-documented. |
| 2 | `0x00000004` | `vertexLit` | **Tentative / partially corroborated.** Importer uses it to select vertex lighting. |
| 4 | `0x00000010` | `hasParent` | **Tentative.** Hierarchy itself is independently represented by IDs. |
| 5 | `0x00000020` | `hasChildren` | **Tentative.** |
| 11 | `0x00000800` | `alphaTesting` | **Tentative.** |
| 12 | `0x00001000` | `alphaBlending` | **Partially confirmed — Runtime.** Parser tests this bit and initializes a runtime alpha-like factor to `0.5` instead of `1.0`. Bit 29 handling can also force this bit on. |
| 13 | `0x00002000` | `additive` | **Tentative.** |
| 14 | `0x00004000` | `subtractive` | **Tentative.** |
| 20 | `0x00100000` | `mirror` | **Partially confirmed — Runtime.** Parser treats objects carrying this bit specially and stores one in a dedicated global slot. Mirror semantics fit the importer, but the complete rendering behavior still needs tracing. |
| 21 | `0x00200000` | `FPSarm` | **Tentative.** |
| 22 | `0x00400000` | `faceMorph` | **Tentative.** |
| 23 | `0x00800000` | `invisible` | **Tentative / strongly plausible.** |
| 24 | `0x01000000` | `skybox` | **Tentative.** |
| 26 | `0x04000000` | `environmentMapped` | **Tentative / plausible.** |
| 27 | `0x08000000` | `underwater` | **Tentative.** |
| 29 | `0x20000000` | `WaterSurface` | **Partially confirmed — Runtime.** Parser applies special behavior: sets an alpha-like runtime value to approximately `0.7`, sets bit `0x1000`, and clears other flag bits. This strongly supports a special transparent-surface interpretation, but the final semantic name should still be verified in the renderer. |
| 30 | `0x40000000` | `WaterUnknown` | **Tentative.** |

Unlisted bits are not necessarily unused; they are simply undocumented here.

A reimplementation should preserve the full 32-bit flag word even when only some bits are understood.

## Sprite use of 3DO geometry

Sprites are not a wholly separate geometry format. Runtime's sprite system uses ordinary loaded 3DO data in a specialized way.

`SetSpriteFrame` at `0x0048EF10`:

1. gets the loaded 3DO root;
2. reads `root+0x48` as the valid frame count;
3. stores a valid frame index in `SpriteInstance+0x16`;
4. writes `0xFFFF` for an invalid frame.

The sprite renderer at `0x004969C0` then:

1. obtains the loaded model's global quad stream;
2. calculates `quad = quadBase + frameIndex * 0x20`;
3. reads vertex references from the quad;
4. fetches geometry from the model's `0x20`-byte vertex stream;
5. consumes the quad's byte UV values for texture sampling.

This provides independent Runtime confirmation of:

- `root+0x48` as a frame-count field for sprite-capable models;
- `0x20` as the quad stride;
- sprite frames being associated directly with quad records.

The exact relationship between `frameCount`, quad count, animation scripts, and sprite texture atlas placement is still being reverse engineered and should not yet be generalized beyond assets that actually use the sprite path.

## Companion `.3DT` texture data

Standalone `.3DO` geometry is normally paired with a same-basename `.3DT` containing indexed texture data. The Blender importer explicitly opens this companion file, while Runtime's material-loading path confirms the palette sizing and compressed-payload behavior.

Texture payloads occur in material-table order. For each material:

```text
palette bytes: 3 * (1 << bitsPerPixel)
pixel payload: dataSize bytes
```

Examples:

- 4 bpp indexed texture: `16` colors, `48` palette bytes;
- 8 bpp indexed texture: `256` colors, `768` palette bytes.

Runtime computes the palette byte count exactly as `3 * (1 << material.bitsPerPixel)` in the palette-loading path around `0x004A77E0`.

The palette consists of three bytes per entry. The importer treats them as RGB. Runtime's palette upload path is consistent with a three-component palette, although the later hardware conversion/gamma path should be used as the final authority for exact channel interpretation.

### Transparency is not simply “black means transparent”

The Blender importer chooses alpha `0` when an RGB palette entry is `(0,0,0)` and alpha `1` otherwise. That is useful for displaying many assets in Blender, but it is an **importer heuristic**, not a confirmed serialized alpha rule. Runtime's alpha testing/blending flags and palette/texture state should be used to reproduce original transparency behavior.

## Indexed texture compression

The decompressor at `0x004B4B40` confirms the compression family implemented by the Blender importer. It is a small LZ/RLE-style byte stream driven by an 8-bit control mask.

### Inputs and termination

Runtime passes:

- destination buffer;
- compressed source buffer;
- compressed byte count (`material.dataSize`).

The decompressor stops when it has consumed the supplied compressed input. It returns the number of output bytes produced.

The caller generally expects `width * height` indexed output bytes. If `dataSize == width * height`, Runtime skips decompression and treats the source as raw indices. A dedicated `256 x 256`, `dataSize == 65536` path is also present, equivalent to that raw case.

### Stream grammar

The first source byte is copied literally to the destination. The second byte is the initial control mask. Control bits are consumed from most-significant to least-significant bit.

For each control bit:

- `0`: copy one literal byte from the input;
- `1`: read one sequence descriptor byte and emit a back-reference or run.

For a sequence descriptor `d`:

```text
type   = d & 0x03
base   = d >> 2
```

The four Runtime-confirmed forms are:

| Type | Encoded meaning | Output length | Back-reference distance |
|---:|---|---:|---:|
| 0 | repeat previous output byte | `base + 2` | `1` |
| 1 | short back-reference | `base + 3` | `1 + next_u8` |
| 2 | 16-bit back-reference | `base + 3` | `1 + ((next_u8 << 8) | next_u8)` |
| 3 | 256-byte-granularity back-reference | `base + 3` | `next_u8 << 8` |

Type 0 is implemented by Runtime as an optimized fill of the immediately preceding byte.

After eight control decisions, Runtime reads a new control byte and continues until the compressed input is exhausted.

One difference from the Blender importer is worth noting: the importer explicitly outputs zero when a back-reference points before the beginning of the produced stream. Runtime's decompressor does not contain that safety behavior; valid game data is expected not to request an invalid history reference. A fidelity-oriented decoder should validate malformed data explicitly rather than silently adopting the Blender importer's zero-fill rule as part of the format.

## Embedded 3DO resources inside `.SCX`

The same `OD3X` version 4 core format also appears embedded in scenario archives such as `aventure.SCX` and `Grid.SCX`.

For the embedded models examined so far:

- `magic == OD3X`;
- `version == 4`;
- `rootOffset == 0x2C`;
- core directory offsets point to the same record types documented above;
- after the model core, texture palette/pixel payloads can be stored inline in the enclosing SCX resource stream rather than requiring an external sibling `.3DT` file.

Twenty `OD3X` cores were found in the examined `aventure.SCX`, and four in the examined `Grid.SCX`; all used `rootOffset == 0x2C`.

This distinction is important:

- **standalone asset representation:** `.3DO` core plus a companion `.3DT` texture stream;
- **SCX-embedded representation:** the same 3DO core is embedded in a larger scenario resource and its texture payload is supplied by that container/resource stream.

Do not conclude from embedded SCX layout that a standalone `.3DO` file necessarily contains its texture pixels after the core.

## Corrections and cautions relative to the Blender importer

Chevluh's importer remains an extremely valuable reference, but OpenNomad should account for the following differences now established or suspected from Runtime analysis:

1. **`versionMinor` is actually `rootOffset`.** Runtime computes the root address from it.
2. **The first `0x2C` bytes are a directory.** The following `0x148`-byte root is separately addressed; they should not be modeled as one inseparable header.
3. **All directory offsets are relative to the beginning of the 3DO core.** They are not relative to the root.
4. **The material record's `0x40..0x47` region is reused by Runtime.** Calling it merely a reserved u64 misses runtime palette/texture allocation fields.
5. **Only the low byte at material `+0x48` is the serialized bit-depth value used by Runtime.** Nearby bytes become runtime scratch during texture allocation.
6. **Runtime processes the light count from root `+0xF0`.** It copies that value over root `+0xE8` before iterating `0x130`-byte lights; the importer's three light-count fields should not yet be given final semantic names.
7. **Triangle reference masking remains an open issue.** The importer's `0x8000` parent flag / `& 0x03FF` scheme should not be elevated to authoritative specification until the corresponding Runtime primitive path is fully traced.
8. **The Blender axis swap, sign flip, and `0.025` scale are consumer conversions.** They are not fields or transformations encoded in the serialized `.3DO` data.
9. **Black-palette-entry transparency is an importer heuristic.** Runtime rendering state determines original transparency behavior.
10. **The compression algorithm itself is now corroborated by Runtime, but malformed-input behavior differs.** Runtime expects valid history references rather than the importer's zero-fill fallback.
11. **“Doors” is not yet an authoritative name for the relationship section.** Runtime proves it contains records referencing lists of object IDs, but the higher-level purpose of every record still needs to be established.

## Suggested packed C representation

The following declarations are useful as a current reverse-engineering model. Names marked `unknown` are intentionally conservative. They are not intended to imply that Runtime itself uses these exact C types.

```c
#pragma pack(push, 1)

typedef struct CoreDirectoryV4 {
    char     magic[4];              // 0x00: "OD3X"
    uint32_t version;               // 0x04: 4
    uint32_t rootOffset;            // 0x08
    uint32_t materialsOffset;       // 0x0C
    uint32_t verticesOffset;        // 0x10
    uint32_t trianglesOffset;       // 0x14
    uint32_t quadsOffset;           // 0x18
    uint32_t objectsOffset;         // 0x1C
    uint32_t relationshipsOffset;   // 0x20
    uint32_t camerasOffset;         // 0x24
    uint32_t lightsOffset;          // 0x28
} CoreDirectoryV4;                  // sizeof = 0x2C

typedef struct MaterialRecordV4 {
    char     name[20];              // 0x00
    char     bmpFilename[20];       // 0x14
    char     tgaFilename[20];       // 0x28
    uint32_t dataSize;              // 0x3C
    uint16_t runtimeField40;        // 0x40; serialized meaning unknown
    uint16_t runtimeField42;        // 0x42
    uint16_t runtimePalette44;      // 0x44
    uint16_t runtimePalette46;      // 0x46
    uint8_t  bitsPerPixel;          // 0x48
    uint8_t  unknown49;             // 0x49
    uint8_t  runtimeScratch4A;      // 0x4A after loading
    uint8_t  runtimeScratch4B;      // 0x4B after loading
    uint16_t width;                 // 0x4C
    uint16_t height;                // 0x4E
} MaterialRecordV4;                 // sizeof = 0x50

typedef struct VertexV4 {
    float    position[3];           // 0x00
    float    normal[3];             // 0x0C
    uint32_t unknown18;             // 0x18
    uint32_t colorARGB;             // 0x1C; bytes B,G,R,A in LE memory
} VertexV4;                         // sizeof = 0x20

typedef struct TriangleV4 {
    uint16_t vertex[3];             // 0x00
    uint8_t  uv[3][2];              // 0x06
    int32_t  materialIndex;         // 0x0C
    int32_t  unknown10;             // 0x10
    int32_t  unknown14;             // 0x14
    int32_t  unknown18;             // 0x18
} TriangleV4;                       // sizeof = 0x1C

typedef struct QuadV4 {
    uint16_t vertex[4];             // 0x00
    uint8_t  uv[4][2];              // 0x08
    int32_t  materialIndex;         // 0x10
    int32_t  unknown14;             // 0x14
    int32_t  unknown18;             // 0x18
    int32_t  unknown1C;             // 0x1C
} QuadV4;                           // sizeof = 0x20

typedef struct Serialized3DOObjectV4 {
    uint32_t flags;                 // 0x00
    uint32_t moverFlags;            // 0x04; name tentative
    uint32_t meshID;                // 0x08
    uint32_t scriptID;              // 0x0C; name tentative
    char     name[20];              // 0x10
    float    position[3];           // 0x24
    int32_t  parentID;              // 0x30
    int32_t  firstChildID;          // 0x34
    int32_t  nextSiblingID;         // 0x38
    uint32_t unknown3C;             // 0x3C
    uint32_t vertexCount;           // 0x40
    uint32_t triangleCount;         // 0x44
    uint32_t quadCount;             // 0x48
    float    unknown4C;             // 0x4C
    float    unknown50;             // 0x50
    float    unknown54;             // 0x54
    float    unknown58;             // 0x58
    float    boundsA[3];            // 0x5C; exact semantics tentative
    float    boundsB[3];            // 0x68
    float    unknown74;             // 0x74
    float    unknown78;             // 0x78
    float    unknown7C;             // 0x7C
    float    bonePosition[3];       // 0x80; semantic name tentative
} Serialized3DOObjectV4;            // sizeof = 0x8C

typedef struct RelationshipRecordV4 {
    uint8_t  unknown00[0x14];       // 0x00
    uint32_t objectCount;           // 0x14
    uint32_t objectListOffset;      // 0x18, relative to relationship section
} RelationshipRecordV4;             // sizeof = 0x1C

typedef struct CameraRecordV4 {
    char     name[20];              // 0x00
    float    parameters[8];         // 0x14; meanings unknown
} CameraRecordV4;                    // sizeof = 0x34

#pragma pack(pop)
```

A root declaration is deliberately omitted from the packed example for now. Although its known offsets are stable enough to document, most of the first `0xB4` bytes are still unidentified; a sparse offset map is less likely to give a false impression of understanding than a large struct filled with arbitrarily named padding.

## Parser recommendations for OpenNomad

A modern parser should avoid reproducing Runtime's in-place relocation/mutation model. Recommended approach:

1. Read and validate `CoreDirectoryV4`.
2. Require `magic == "OD3X"` and support version `4` explicitly.
3. Bounds-check every non-empty section using both the directory offset and its record count.
4. Parse the root from `rootOffset`, not from a hard-coded `0x2C`.
5. Preserve raw root fields, including all three serialized light-count fields.
6. Parse immutable serialized material records; store GPU/atlas/palette state separately rather than overwriting bytes `0x40..0x4B` as Runtime does.
7. Parse global geometry streams using root totals.
8. Parse serialized object descriptors and independently verify that cumulative per-object counts do not exceed the corresponding global stream totals.
9. Resolve object hierarchy by `meshID` in a second pass.
10. Parse relationship records without overwriting ID arrays; resolve IDs to object references separately.
11. Preserve unknown flag bits and unknown record fields verbatim.
12. Keep encoded primitive vertex references intact until all index/ownership bits are understood; expose decoded fields alongside the raw `u16` rather than destroying information.
13. Treat `.3DT`/embedded texture bytes as a separate payload source from the `.3DO` structural core.
14. Verify decompression output length equals `width * height` and reject malformed history references safely.

These choices retain enough raw information for future reverse engineering and avoid hard-coding assumptions that may only hold for the current asset subset.

## Open questions

The following areas are intentionally unresolved and should be revisited as Runtime analysis progresses:

- complete semantics of root offsets `0x00..0x47` and `0x4C..0xB3`;
- exact meaning of the global scalar at root `+0xB8`;
- meaning of root `+0xD4` and `+0xD8`;
- original distinction among the serialized light counts at `+0xE8`, `+0xEC`, and `+0xF0`;
- exact field meanings inside `CameraRecordV4`;
- authoritative semantic names for all fields in `LightRecordV4`;
- higher-level meaning of relationship records and whether “door” applies to some or all of them;
- full mesh flag map and interactions between flag bits;
- exact encoded triangle vertex-reference bit layout, particularly bits `10..14`;
- whether quads can carry equivalent parent/alternate-owner vertex references;
- exact transform rules for parent-referenced/skinned triangle vertices;
- semantic meaning of vertex field `+0x18`;
- exact interpretation of object floats `+0x4C..+0x58` and `+0x74..+0x7C`;
- exact intended text encoding/code page;
- native coordinate units and the precise relationship between Runtime's scale initialization and the Blender importer's `1/40` conversion;
- exact original renderer UV convention, including texture-page/atlas placement;
- exact serialized meaning, if any, of material bytes `+0x40..+0x47` before Runtime repurposes them;
- how sprite `frameCount`, quad ordering, scripts, and animated texture resources relate across all sprite asset types.

## Runtime locations currently relevant to this format

These are useful starting points for continuing the reverse engineering. Names are descriptive OpenNomad/research names rather than original symbols.

| Address | Role |
|---:|---|
| `0x0044DF10` | Parse/initialize a version 4 3DO core; validates `OD3X`, resolves directory offsets, creates runtime objects, resolves hierarchy and relationships, initializes lights. |
| `0x0044EC80` | Embedded/in-memory 3DO load path used during scenario/resource loading. |
| `0x0048EF10` | Set sprite frame; confirms root `+0x48` frame count. |
| `0x004969C0` | Sprite rendering path; confirms use of `0x20`-byte quad records as frames. |
| `0x004A75E0` | Indexed texture unpack/upload path; uses material `dataSize`, BPP, width, height, and runtime texture allocation fields. |
| `0x004A77E0` | Palette load/allocation path; confirms palette size `3 * (1 << BPP)`. |
| `0x004A7900` | Palette upload callback path. |
| `0x004B4B40` | Runtime 3DO indexed-texture decompressor. |

## Secondary reference: Omikron Blender Importer

Chevluh's importer was the starting point for much of the community understanding of `.3DO` and remains useful for field discovery, geometry reconstruction, and comparison against retail assets:

<https://github.com/Chevluh/Omikron_Blender_Importer/blob/main/omikronImporter.py>

Where this document differs from the importer, the behavior observed in `Runtime.exe` takes precedence.
