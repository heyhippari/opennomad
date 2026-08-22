# Omikron `.SCX` scenario/resource container format

> **Status:** work-in-progress reverse-engineering documentation for OpenNomad  
> **Last updated:** 2026-08-22
>
> This document describes the version-5 `.SCX` scenario/resource container used
> by the Windows retail release of *Omikron: The Nomad Soul*.
>
> `.SCX` is not merely a flat asset archive. It combines:
>
> - a fixed 16-byte file header;
> - an in-memory-style descriptor/tag block;
> - structured IAM/Quantic C script definitions;
> - named resource descriptors;
> - pointer/handle-shaped serialized fields which Runtime repairs or replaces;
> - untagged opaque descriptor data;
> - and a sequential appended resource stream containing paths, animations,
>   WAVs, 3DO+3DT packages, scenes, and other payloads.
>
> The format is best understood as a **serialized scenario/resource manifest and
> object graph followed by the binary resources that manifest requests**.
>
> This file is authoritative for the SCX container itself.
>
> Related documents:
>
> - [`script-opcodes.md`](script-opcodes.md) — authoritative for `DEAD0002`
>   structured-script serialization, mutable instances, scheduling, and the
>   AREA-to-SCX script bridge;
> - [`iam-script-functions.md`](iam-script-functions.md) — authoritative for
>   32-bit IAM `Script_*` function IDs;
> - [`3do.md`](3do.md) — embedded `OD3X` model cores;
> - [`3dt.md`](3dt.md) — companion/embedded indexed texture payloads;
> - [`runtime-globals.md`](runtime-globals.md) — global Runtime state touched by
>   scenario/resource loading;
> - [`startup-sequence.md`](startup-sequence.md) — when scenario packages are
>   loaded and activated.

---

# 1. Evidence model

Sources are used in this order:

1. **`Runtime.exe`** — authoritative for what retail Windows Omikron reads,
   skips, relocates, copies, allocates, mutates, and passes to subsystem
   loaders.
2. **Retail SCX data** — principally the supplied `Grid.SCX` and
   `aventure.SCX`.
3. **OpenNomad parser/runtime code** — useful for expressing the currently
   recovered grammar, but subordinate where a defensive policy or implementation
   assumption is stricter than Runtime.
4. **Historical/community material** — useful for naming and context, but not
   considered sufficient to establish binary structure on its own.

Analyzed Runtime build:

```text
File:             Runtime.exe
Architecture:     PE32 / i386
Image base:       0x00400000
Linker timestamp: 1999-10-04 20:31:50
SHA-256:          55f7120bfea7891b048c64e3682f3259cdbf2719a43fa24e42254b753c95d2ef
```

Addresses in this document refer to that executable.

Confidence labels:

- **Confirmed — Runtime:** directly established by executable behavior.
- **Confirmed — data:** directly established from retail bytes.
- **Corroborated:** Runtime and retail data agree.
- **Strongly reconstructed:** several independent observations agree.
- **Provisional:** useful working interpretation, but source-level semantics are
  incomplete.
- **Unknown:** structure/extent is known while meaning is not.

---

# 2. Scope and terminology

This document uses the following terms:

| Term | Meaning |
|---|---|
| **header** | fixed 16-byte SCX file header |
| **descriptor block** | the `descriptor_size` bytes immediately after the header |
| **tag** | a recognized `0xDEADxxxx` dword in the descriptor block |
| **descriptor section** | the structured bytes following a recognized tag |
| **descriptor gap** | untagged words present inside the descriptor block and skipped by Runtime's tag scan |
| **resource stream** | bytes after the descriptor block, consumed sequentially according to resource-bearing descriptor sections |
| **embedded resource** | one framed resource inside the appended stream |
| **model package** | special 12-byte-framed `OD3X` core + auxiliary 3DT-style payload |
| **placeholder** | serialized pointer/handle-shaped field whose file value must not be treated as a valid live pointer |

The filename extension is used for several differently scoped scenario/resource
banks. Loader slot/lifetime policy is separate from the binary SCX grammar.

---

# 3. Executive summary

Version-5 SCX has this top-level shape:

```text
file offset 0
    |
    v
+-----------------------------+
| ScxHeader                   | 0x10 bytes
+-----------------------------+
| descriptor/tag block        | header.descriptorSize bytes
|                             |
|  DEAD0002 scripts           |
|  DEAD0000 paths             |
|  DEAD0001 animations        |
|  DEAD0003 sounds            |
|  DEAD0004 sprite/models     |
|  opaque untagged words      |
|  DEAD0005 scenes            |
|  DEAD0006 opaque table      |
|  DEAD0007 global table      |
|  ...                        |
|  DEADFFFF                   |
+-----------------------------+
| appended resource stream    |
|                             |
|  [self,size]+3DP            |
|  [self,size]+3DA            |
|  [self,size]+RIFF/WAVE      |
|  [self,core,aux]+OD3X+3DT   |
|  ...                        |
+-----------------------------+
| EOF                         |
+-----------------------------+
```

The resource stream does **not** have its own directory.

Its grammar is derived from the recognized descriptor sections in the order
those sections are encountered.

---

# 4. File header — `0x10` bytes

```c
struct ScxHeaderV5 {
    uint32_t magic;           // +0x00
    uint32_t version;         // +0x04
    uint32_t unknown08;       // +0x08
    uint32_t descriptorSize;  // +0x0C
}; // 0x10
```

| Offset | Size | Type | Meaning | Confidence |
|---:|---:|---|---|---|
| `0x00` | 4 | `u32` | magic `0x00DEAD00` | Confirmed |
| `0x04` | 4 | `u32` | container version, `5` in inspected retail files | Confirmed |
| `0x08` | 4 | `u32` | unknown header word, `8` in both supplied files | Unknown |
| `0x0C` | 4 | `u32` | descriptor-block byte size | Confirmed |

The descriptor block begins at:

```text
0x10
```

and the appended resource stream begins at:

```text
resourceStreamOffset = 0x10 + descriptorSize
```

## 4.1 Observed headers

`Grid.SCX`:

```text
magic          = 0x00DEAD00
version        = 5
unknown08      = 8
descriptorSize = 0x1019
resourceStream = 0x1029
fileSize       = 0x141290
```

`aventure.SCX`:

```text
magic          = 0x00DEAD00
version        = 5
unknown08      = 8
descriptorSize = 0x41C8
resourceStream = 0x41D8
fileSize       = 0x2EF831
```

## 4.2 Descriptor size includes the terminator

In both supplied files, `descriptorSize` extends through the four-byte
`DEADFFFF` word.

For example:

```text
Grid:
    descriptor starts  0x0010
    DEADFFFF at         0x1025
    descriptor ends     0x1029

0x1029 - 0x0010 = 0x1019
```

---

# 5. Descriptor scanner semantics

Runtime does not parse the descriptor block as a rigid sequence of only known
chunks.

Instead, the recovered control flow repeatedly reads a 32-bit word and checks
whether it is a recognized `DEADxxxx` tag.

Conceptually:

```cpp
while (true) {
    uint32_t word = read_u32();

    if (word == DEADFFFF)
        break;

    if (recognized_tag(word))
        parse_that_section();
    else
        continue; // the dword has simply been consumed
}
```

This seemingly small detail is crucial.

## 5.1 Untagged words are legal in retail files

Retail descriptor blocks contain substantial spans of words that are not
recognized tags.

Runtime's loader path simply advances past them four bytes at a time.

OpenNomad currently coalesces those skipped words into `descriptor_gaps` so
they remain visible to reverse-engineering tools.

Do not assume:

```text
descriptor block = tag, section, tag, section, tag, section...
```

The actual grammar is closer to:

```text
recognized section
arbitrary/opaque words
recognized section
arbitrary/opaque words
...
```

## 5.2 `DEAD0009` is effectively skipped in this path

There is no meaningful parser body currently recovered for `DEAD0009`.

It behaves like an unhandled/no-op descriptor word in the relevant Runtime
dispatcher.

## 5.3 Section order is not numeric

`Grid.SCX` begins with:

```text
DEAD0002
DEAD0000
DEAD0001
DEAD0003
DEAD0004
...
```

Therefore section numbers are categories, not a required serialization order.

## 5.4 Recognized-tag duplication

Current OpenNomad rejects duplicate recognized tags.

That is a defensive parser policy.

No equivalent explicit duplicate-tag rejection has yet been established in
Runtime's simple tag dispatcher, so do not currently make “each tag may occur
only once” a hard retail-format claim.

## 5.5 Trailing descriptor bytes after `DEADFFFF`

Current OpenNomad requires `DEADFFFF` to coincide exactly with the end of the
declared descriptor block.

Both supplied retail files satisfy this.

Runtime's tolerance for unused bytes after the terminator inside the declared
block has not yet been separately established.

---

# 6. Recognized descriptor tags

Current map:

| Tag | Current role | Fixed descriptor record | Appended resource |
|---:|---|---:|---|
| `0xDEAD0000` | paths / `.3DP` resources | `0x20` | generic 8-byte-framed payload |
| `0xDEAD0001` | animations / `.3DA` resources | `0x24` | generic 8-byte-framed payload |
| `0xDEAD0002` | structured IAM/Quantic C scripts | variable | none outside section |
| `0xDEAD0003` | sounds | `0x1A` | generic frame containing RIFF/WAVE |
| `0xDEAD0004` | sprite/effect 3DO models | `0x24` | special 12-byte model package |
| `0xDEAD0005` | external scenes | `0x1C` | generic 8-byte-framed payload |
| `0xDEAD0006` | opaque large records | `0x318` | none currently identified |
| `0xDEAD0007` | global fixed-size records | `0x20` | none |
| `0xDEAD0008` | Runtime global-mode marker | none | none |
| `0xDEAD0009` | ignored/reserved in current path | none established | none established |
| `0xDEAD000A` | auxiliary/extra resource marker | none in descriptor | one generic framed payload |
| `0xDEADFFFF` | descriptor terminator | — | — |

---

# 7. Resource-manifest ordering

Resource-bearing sections append entries to an implicit resource manifest.

The manifest is ordered by:

1. descriptor section encounter order;
2. record order within each section.

Conceptually:

```text
descriptor:
    DEAD0000 count=1
    DEAD0001 count=3
    DEAD0003 count=7
    DEAD0004 count=4

resource stream:
    DEAD0000 resource 0

    DEAD0001 resource 0
    DEAD0001 resource 1
    DEAD0001 resource 2

    DEAD0003 resource 0
    ...
    DEAD0003 resource 6

    DEAD0004 model 0
    ...
    DEAD0004 model 3
```

There is no separate resource-stream tag before each payload.

The per-resource framing tells the loader how far to advance.

---

# 8. Generic appended-resource frame — 8 bytes

Sections `DEAD0000`, `DEAD0001`, `DEAD0003`, `DEAD0005`, and `DEAD000A`
currently use this generic envelope:

```c
struct ScxEmbeddedResourceHeader {
    uint32_t selfOffset;    // absolute file offset of this header
    uint32_t payloadSize;   // number of payload bytes following the header
};
```

Physical layout:

```text
+0x00  u32 selfOffset
+0x04  u32 payloadSize
+0x08  u8 payload[payloadSize]
```

Next resource:

```text
next =
    headerOffset
    + 8
    + payloadSize
```

## 8.1 `selfOffset` is absolute

In the supplied retail data:

```text
header.selfOffset == absolute file offset of header
```

Example from `Grid.SCX`:

```text
resource stream begins: 0x1029

header at 0x1029:
    selfOffset  = 0x1029
    payloadSize = 252

payload:
    0x1031 .. 0x112C

next header:
    0x112D
```

This absolute self-reference is unusual but consistent.

It should be preserved if rewriting/repacking a file.

## 8.2 Runtime validation versus OpenNomad validation

OpenNomad currently validates:

```text
selfOffset == actual header position
```

This is valuable corruption detection.

Whether the retail Runtime explicitly checks the first dword in every resource
path has not yet been proven.

The equality itself is a confirmed retail-data property.

## 8.3 No observed alignment padding

In the supplied files, the next resource starts immediately after:

```text
8 + payloadSize
```

No general 4-, 16-, or sector-alignment rule is currently observed.

---

# 9. Special embedded-model package — 12-byte header

`DEAD0004` resources use a different envelope:

```c
struct ScxModelPackageHeader {
    uint32_t selfOffset;
    uint32_t coreSize;
    uint32_t auxiliarySize;
};
```

Physical layout:

```text
+0x00  u32 selfOffset
+0x04  u32 coreSize
+0x08  u32 auxiliarySize

+0x0C  u8 modelCore[coreSize]
        begins "OD3X", version 4

        u8 auxiliary[auxiliarySize]
        3DT-style palette/pixel stream
```

Next package:

```text
next =
    headerOffset
    + 12
    + coreSize
    + auxiliarySize
```

## 9.1 Core

The core is a normal version-4 3DO resource:

```text
"OD3X"
u32 version = 4
...
```

See [`3do.md`](3do.md).

## 9.2 Auxiliary data

The auxiliary block is the 3DT-style encoded texture stream consumed by the
core's material descriptors.

OpenNomad cross-validates:

```text
auxiliarySize
==
sum over materials:
    3 * (1 << bpp)
    + material.dataSize
```

See [`3dt.md`](3dt.md).

## 9.3 Example

First `Grid.SCX` model package:

```text
header offset = 0x129511
selfOffset    = 0x129511
coreSize      = 1476  = 0x5C4
auxiliarySize = 17278 = 0x437E

OD3X core:
    starts 0x12951D

auxiliary:
    starts 0x129AE1

next model package:
    0x12DE5F
```

---

# 10. Serialized pointer/handle-shaped fields

A striking property of SCX is that several descriptors contain values that look
like process pointers or runtime addresses even in retail files.

Example `Grid.SCX` values:

```text
DEAD0000 "Grid_pb.3dp":
    +0x18 = 0x00A007E0

DEAD0001 "INTRO1.3DA":
    +0x18 = 0x00500318

DEAD0004 "EFFECTS2_SMOKE1.3DO":
    +0x18 = 0x00685900
    +0x1C = 0x0142DAE4
```

These are not addresses a modern loader may dereference.

They are best treated as:

```text
serialized editor/build/runtime placeholders
```

unless Runtime behavior establishes a durable semantic meaning.

This is consistent with the broader SCX design:

```text
serialized memory-like structures
    ->
Runtime load-time relocation/replacement
```

The structured-script subsystem exhibits the same pattern with indexes that
become pointers.

---

# 11. `DEAD0000` — path / `.3DP` descriptors

Current section grammar:

```text
u32 count
ScxPathDescriptor records[count]
```

Record size:

```text
0x20 bytes
```

Current layout:

```c
struct ScxPathDescriptor {
    char     name[24];                   // +0x00
    uint32_t runtimePathsPlaceholder;    // +0x18
    uint32_t serializedSubpathCount;     // +0x1C
}; // 0x20
```

## 11.1 Resource interpretation

Each descriptor corresponds to one generic 8-byte-framed appended resource.

Current OpenNomad decodes the payload with `Path3DP`.

This is strongly corroborated by retail names.

`Grid.SCX` contains:

```text
name       = "Grid_pb.3dp"
serializedSubpathCount = 2
```

and the embedded Grid_pb.3dp payload itself also contains:

```text
+subpathCount = 2 +
```

It is therefore reasonable to treat `DEAD0000` as the SCX path-resource table.

## 11.2 Runtime interpretation of `+0x18` and `+0x1C`

The 3DP loader resolves these fields more precisely than the earlier SCX-only analysis.

During SCX loading, Runtime calls `Read3DP` with the addresses of the live descriptor fields at `+0x18` and `+0x1C` as output locations.

On successful load they become approximately:

```text
+0x18 = Runtime3DPSubpath** paths
+0x1C = u32 loadedSubpathCount
```

On failure, Runtime clears both fields.

Therefore:

- `+0x18` is a serialized pointer-shaped placeholder for the live path-array pointer slot;
- `+0x1C` is a subpath-count slot, **not a stable path/resource ID**;
- a modern immutable parser should preserve the serialized values rather than pretending they are live pointers;
- the decoded 3DP payload remains authoritative for the actual loaded subpath count.

In `Grid.SCX`, serialized `+0x1C` already equals `2`, matching the internal `Grid_pb.3dp` `subpathCount`. Runtime nevertheless overwrites the live field with the count returned by `Read3DP`.

The exact original source-level names remain unknown.

---

# 12. `DEAD0001` — animation / `.3DA` descriptors

Section grammar:

```text
u32 count
ScxAnimationDescriptor records[count]
```

Record size:

```text
0x24 bytes
```

Layout:

```c
struct ScxAnimationDescriptor {
    char     name[24];             // +0x00
    uint32_t runtimePlaceholder;   // +0x18
    uint32_t field1C;              // +0x1C
    uint32_t animationId;          // +0x20
}; // 0x24
```

Each record owns one generic 8-byte-framed appended animation payload.

Current OpenNomad decodes those payloads with `Animation3DA`.

## 12.1 Grid examples

```text
INTRO1.3DA:
    +0x18 = 0x00500318
    +0x1C = 0
    id    = 3

INTRO2.3DA:
    id    = 4

INTRO3.3DA:
    id    = 5
```

## 12.2 `+0x1C`

Meaning unresolved.

The current OpenNomad 3DA consumer supports only:

```text
field1C == 0
```

and reports a structured error when it is non-zero.

That is an implementation boundary, not a format proof that the field must
always be zero.

---

# 13. `DEAD0002` — structured IAM scripts

`DEAD0002` is variable-sized and significantly more complex than the resource
descriptor tables.

Top-level structure:

```text
u32 scriptCount
ScriptTemplate[scriptCount]      // 0x64 each

u32 sharedValueCount
u32 sharedValues[sharedValueCount]

for each script:
    related-script data
    root commands
    linked commands
    binding table A
    binding table B
```

Important fixed record sizes:

```text
script template  = 0x64
script command   = 0x18
```

This section does not consume a separate appended resource.

The full serialized grammar, command layout, pointer relocation, instance model,
and scheduler behavior are documented in:

[`script-opcodes.md`](script-opcodes.md)

The 32-bit function IDs inside command records are documented in:

[`iam-script-functions.md`](iam-script-functions.md)

---

# 14. `DEAD0003` — sound descriptors

Section grammar:

```text
u32 count
ScxSoundDescriptor records[count]
```

Record size:

```text
0x1A bytes
```

Layout:

```c
struct ScxSoundDescriptor {
    char     name[22];          // +0x00
    uint16_t soundField16;      // +0x16
    uint16_t hId;               // +0x18
}; // 0x1A
```

Each descriptor corresponds to one generic 8-byte-framed appended resource.

The payload must begin with a RIFF/WAVE container in all currently supported
retail examples.

## 14.1 Runtime mutation of `+0x16`

The original Runtime replaces/uses the word at `+0x16` as loaded sound state.

Recovered behavior includes writing:

```text
0xFFFF
```

when the sound cannot be resolved/loaded.

Therefore its serialized value should be preserved separately from the live
audio handle in a modern implementation.

## 14.2 `+0x18`

Current OpenNomad names this field:

```text
hId
```

Its exact source-level semantic meaning is unresolved.

## 14.3 Grid example

```text
INTRO01.WAV:
    +0x16 = 0
    +0x18 = 0x15

INTRO02.WAV:
    +0x16 = 1
    +0x18 = 0x16

...
```

Seven sound descriptors in `Grid.SCX` correspond to seven framed RIFF/WAVE
resources in the appended stream.

---

# 15. Framed WAV resource

A sound payload uses the generic 8-byte resource frame:

```text
u32 absoluteSelfOffset
u32 payloadSize
u8  riffWave[payloadSize]
```

OpenNomad validates:

```text
payload[0..3]  == "RIFF"
payload[8..11] == "WAVE"
```

The RIFF file begins at:

```text
headerOffset + 8
```

No extra SCX-specific WAV header follows the generic frame.

---

# 16. `DEAD0004` — sprite/effect model descriptors

Section grammar:

```text
u32 count
ScxSpriteModelDescriptor records[count]
```

Record size:

```text
0x24 bytes
```

Layout:

```c
struct ScxSpriteModelDescriptor {
    char     name[24];             // +0x00
    uint32_t runtimePlaceholder;   // +0x18
    uint32_t field1C;              // +0x1C
    uint32_t spriteId;             // +0x20
}; // 0x24
```

Each descriptor corresponds to one special 12-byte-framed model package in the
appended stream.

## 16.1 Grid examples

```text
EFFECTS2_SMOKE1.3DO:
    +0x18 = 0x00685900
    +0x1C = 0x0142DAE4
    id    = 9

EFFECTS1_IMPACT2.3DO:
    id    = 10

EFFECTS1_IMPACT1.3DO:
    id    = 11

EFFECTS3_SMOKB.3DO:
    id    = 12
```

## 16.2 Aventure examples

`aventure.SCX` contains 20 effect-model descriptors, including:

```text
EFFECTS2_SMOKE2.3DO
EFFECTS3_GLOWB.3DO
EFFECTS3_GLOWC.3DO
EFFECTS3_GLOWD.3DO
EFFECTS2_IMPACT.3DO
EFFECTS3_IMPACT2.3DO
EFFECTS3_SMOKB.3DO
EFFECTS1_BUBBLE1.3DO
EFFECTS1_EXPLO1.3DO
EFFECTS2_SMOKE1.3DO
...
```

These are the resources consumed by the sprite/effect Script subsystem.

## 16.3 `field1C`

Meaning is unresolved.

Retail values can be pointer-shaped and should not be interpreted as a stable
file offset without Runtime evidence.

---

# 17. Untagged data after `DEAD0004`

Both supplied SCX files contain a large untagged region immediately after the
`DEAD0004` descriptor array and immediately before `DEAD0005`.

This is not random trailing padding.

Its size correlates exactly with the number of `DEAD0004` records in each file.

## 17.1 `Grid.SCX`

```text
DEAD0004 begins:    0x0935
4 * 0x24 records
untagged begins:    0x09CD
DEAD0005 begins:    0x100D

gap size:
    0x100D - 0x09CD
    = 0x640
    = 4 * 0x190
```

So `Grid.SCX` contains exactly:

```text
one 0x190-byte untagged block per DEAD0004 record
```

## 17.2 `aventure.SCX`

```text
DEAD0004 begins:    0x1E14
20 * 0x24 records
untagged begins:    0x20EC
DEAD0005 begins:    0x41BC

gap size:
    0x41BC - 0x20EC
    = 0x20D0
    = 20 * 0x1A4
```

So `aventure.SCX` contains exactly:

```text
one 0x1A4-byte untagged block per DEAD0004 record
```

## 17.3 Interpretation

The correlation strongly suggests that these blocks are additional serialized
per-model/per-sprite state associated with `DEAD0004`.

However:

- their stride differs between the two SCX files;
- the known Runtime descriptor dispatcher does not parse them as a recognized
  tagged section in this path;
- the data contains many pointer-shaped/runtime-looking values;
- `aventure.SCX` repeatedly contains the ASCII text:

```text
24 bit BumpLum DuDv
```

inside these blocks.

That string and the surrounding values suggest graphics/Direct3D-era runtime or
format metadata, but this is **not yet proven enough to assign a structure**.

Current recommendation:

```text
preserve as opaque descriptor-gap bytes
do not discard
do not invent fields
do not assume one universal stride
```

This is a high-value future reverse-engineering target because it is clearly
structured and resource-correlated despite being skipped by the identified tag
dispatcher.

---

# 18. `DEAD0005` — external-scene descriptors

Section grammar:

```text
u32 count
ScxSceneDescriptor records[count]
```

Record size:

```text
0x1C bytes
```

Layout:

```c
struct ScxSceneDescriptor {
    char     name[24];             // +0x00
    uint32_t runtimePlaceholder;   // +0x18
}; // 0x1C
```

Each descriptor corresponds to one generic 8-byte-framed appended scene
resource.

Runtime diagnostics include scene lookup failures such as:

```text
Scene "%s" not found !
```

The inner scene payload format remains unresolved.

Both supplied `Grid.SCX` and `aventure.SCX` have:

```text
count = 0
```

so another SCX corpus is required to characterize real scene payloads.

---

# 19. `DEAD0006` — `0x318`-byte opaque records

Runtime behavior establishes:

```text
u32 count
u8 records[count][0x318]
```

Record size:

```text
0x318 = 792 bytes
```

The Runtime path around `0x00449D7D`:

1. reads the count;
2. retains the record-array address when count is non-zero;
3. advances the descriptor cursor by exactly:

```text
count * 0x318
```

No semantic field map is currently recovered.

Both supplied files use:

```text
count = 0
```

OpenNomad therefore preserves only record offsets/sizes for non-zero future
samples rather than assigning speculative fields.

---

# 20. `DEAD0007` — global `0x20`-byte records

Runtime behavior around `0x00449DAC` establishes the basic grammar:

```text
u32 count
u8 records[count][0x20]
```

Runtime:

1. reads the dword count;
2. stores it at global `0x0053127C`;
3. computes `count * 0x20`;
4. copies those bytes into global storage beginning around `0x00903F00`.

Record semantics are unresolved.

Both supplied files have:

```text
count = 0
```

## 20.1 Important OpenNomad discrepancy

Current OpenNomad code interprets the dword as:

```text
low 27 bits = count
high 5 bits = flags
```

using:

```text
count = serialized & 0x07FFFFFF
```

The currently rechecked Runtime fragment does **not** show that mask before
multiplying the freshly loaded value by `0x20`.

It appears to use the complete dword as the count.

Therefore the high-five-bit flag interpretation is not currently supported by
the recovered Runtime path and should be reviewed in the implementation.

Until stronger evidence appears, this document treats the serialized dword as:

```text
u32 count
```

with no proven embedded flag bits.

---

# 21. `DEAD0008` — global-mode marker

`DEAD0008` has no following descriptor payload in the recovered parser path.

Runtime behavior around:

```text
0x00449DD8
```

is:

```c
*(uint32_t*)0x004C7DA4 = 0x100;
```

then descriptor parsing continues.

So the tag itself acts as a mode/configuration marker.

Current documentation-safe interpretation:

```text
DEAD0008:
    no serialized body
    causes one Runtime global to be set to 0x100
```

The semantic meaning of that global remains unresolved.

---

# 22. `DEAD0009` — unhandled/reserved

No meaningful body parser is currently recovered for `DEAD0009`.

In the relevant word-scanning dispatcher it effectively behaves like an
unrecognized/no-op word.

Do not assign a count or payload solely because neighboring tags have one.

---

# 23. `DEAD000A` — extra appended block

`DEAD000A` acts as a marker that one additional generic framed resource exists
in the appended stream.

Descriptor side:

```text
DEAD000A
```

with no currently identified inline descriptor body.

Resource-stream side:

```text
u32 selfOffset
u32 payloadSize
u8 payload[payloadSize]
```

Runtime code around `0x00449DE4` reads an 8-byte resource header from the file
stream, allocates space for the payload, reads it, and passes the loaded block
to another subsystem around:

```text
0x0049EEF0
```

The payload's internal format remains unresolved.

---

# 24. `DEADFFFF` — descriptor terminator

`DEADFFFF` terminates descriptor scanning.

It is part of the `descriptorSize` range in the supplied files.

No appended resource is associated with it.

---

# 25. Complete observed `Grid.SCX` descriptor map

```text
file size:        0x141290
descriptor size:  0x1019
resource stream:  0x1029
```

Descriptor layout:

| File offset | Entry | Details |
|---:|---|---|
| `0x0010` | `DEAD0002` | 8 scripts |
| `0x07DB` | `DEAD0000` | 1 path |
| `0x0803` | `DEAD0001` | 3 animations |
| `0x0877` | `DEAD0003` | 7 sounds |
| `0x0935` | `DEAD0004` | 4 sprite/effect models |
| `0x09CD..0x100C` | untagged | `0x640 = 4 * 0x190` bytes |
| `0x100D` | `DEAD0005` | 0 scenes |
| `0x1015` | `DEAD0006` | 0 records |
| `0x101D` | `DEAD0007` | 0 records |
| `0x1025` | `DEADFFFF` | end |
| `0x1029` | resource stream | first appended resource |

## 25.1 Grid manifest

The descriptor produces this appended-resource sequence:

```text
1 path
3 animations
7 sounds
4 sprite/model packages
```

Total:

```text
15 appended resources
```

The parsed manifest ends exactly at:

```text
0x141290
```

which is EOF.

## 25.2 Grid named resources

Paths:

```text
Grid_pb.3dp
```

Animations:

```text
INTRO1.3DA
INTRO2.3DA
INTRO3.3DA
```

Sounds:

```text
INTRO01.WAV
INTRO02.WAV
INTRO03.WAV
INTRO04.WAV
INTRO05.WAV
INTRO06.WAV
INTRO07.WAV
```

Sprite/effect models:

```text
EFFECTS2_SMOKE1.3DO
EFFECTS1_IMPACT2.3DO
EFFECTS1_IMPACT1.3DO
EFFECTS3_SMOKB.3DO
```

Scripts include:

```text
1KaylArrives
2KaylStand
3KaylLeaves
...
```

---

# 26. Complete observed `aventure.SCX` descriptor map

```text
file size:        0x2EF831
descriptor size:  0x41C8
resource stream:  0x41D8
```

Descriptor layout:

| File offset | Entry | Details |
|---:|---|---|
| `0x0010` | `DEAD0002` | 22 scripts |
| `0x189A` | `DEAD0000` | 0 paths |
| `0x18A2` | `DEAD0001` | 0 animations |
| `0x18AA` | `DEAD0003` | 53 sounds |
| `0x1E14` | `DEAD0004` | 20 sprite/effect models |
| `0x20EC..0x41BB` | untagged | `0x20D0 = 20 * 0x1A4` bytes |
| `0x41BC` | `DEAD0005` | 0 scenes |
| `0x41C4` | `DEAD0006` | 0 records |
| `0x41CC` | `DEAD0007` | 0 records |
| `0x41D4` | `DEADFFFF` | end |
| `0x41D8` | resource stream | first appended resource |

## 26.1 Aventure manifest

Because the path/animation counts are zero, the appended stream contains:

```text
53 sounds
20 sprite/model packages
```

Total:

```text
73 appended resources
```

The manifest consumes exactly through:

```text
0x2EF831
```

which is EOF.

---

# 27. Resource-stream consumption as an integrity check

The descriptor block defines enough information to walk the appended stream
without searching for magic bytes.

A strong parser can therefore verify:

```text
position = resourceStreamOffset

for resource in manifest:
    parse frame at position
    position = exact end of frame

expect position == fileSize
```

Both supplied retail files satisfy exact EOF consumption.

This is a powerful structural validation rule.

## 27.1 Runtime tolerance

Current OpenNomad rejects unclaimed bytes after the manifest.

That is sensible for forensic correctness.

Whether Runtime would ignore arbitrary trailing bytes after all requested
resources has not yet been separately established.

Treat exact EOF consumption as:

```text
confirmed retail-data invariant
current OpenNomad strictness
```

rather than an absolute claim about every possible development build.

---

# 28. Why magic-byte scanning is the wrong parser

It is tempting to locate resources by searching for:

```text
OD3X
RIFF
```

inside the file.

Do not do that.

Correct parsing is:

```text
header
    ->
descriptor block
    ->
resource manifest
    ->
framed resource stream
```

Reasons:

1. resource order is established by descriptors;
2. payload sizes are explicitly framed;
3. arbitrary payload bytes can contain apparent magic sequences;
4. self offsets provide integrity checks;
5. a future resource type may have no recognizable magic;
6. the untagged descriptor region itself can contain arbitrary bytes.

Magic scans are useful forensic aids, not file grammar.

---

# 29. Descriptor fields are not automatically file offsets

SCX contains many dwords that look pointer-like.

Do not infer:

```text
0x00A007E0
```

means:

```text
file offset 0xA007E0
```

unless Runtime demonstrates that specific field is offset-based.

Known genuine file offsets include:

```text
header.descriptorSize-derived resourceStreamOffset
embedded-resource selfOffset
model-package selfOffset
```

Known serialized indexes include structured-script value/link indexes.

Other pointer-shaped fields are often overwritten/repaired at load.

---

# 30. Descriptor block is partly a serialized memory image

The combination of:

- pointer-shaped fields;
- runtime placeholders;
- script pointer relocation;
- untagged per-model-associated blobs;
- raw fixed-size opaque tables;

strongly suggests that SCX preserves significant portions of the original
editor/runtime's memory-oriented structures rather than using a clean,
minimal, modern archive schema.

This explains otherwise odd design choices:

```text
absolute self offsets
serialized pointer-looking values
opaque bytes ignored by loader
records later copied directly into global memory
```

This is an architectural interpretation, not an original source comment, but it
fits the observed behavior unusually well.

---

# 31. Load-time mutation versus serialized state

A modern implementation should distinguish:

```text
SerializedScx
ResolvedScx
ScenarioRuntime
```

Do not model Runtime's in-place mutation literally unless necessary.

Examples:

```text
sound descriptor serialized +0x16
    ->
loaded sound ID/handle state

script firstValueIndex
    ->
pointer to mutable value pool

script nextLinkedIndex
    ->
pointer to linked command

sprite/model placeholders
    ->
loaded runtime resource pointers/handles
```

Preserving the original serialized values is useful for debugging and future
reverse engineering.

---

# 32. Recommended OpenNomad data model

Conceptually:

```cpp
struct ScxPackage {
    ScxHeader header;

    DescriptorInventory descriptors;
    ResourceManifest manifest;

    ScriptSection scripts;

    std::vector<OpaqueRange> descriptorGaps;

    std::vector<EmbeddedResourceRef> paths;
    std::vector<EmbeddedResourceRef> animations;
    std::vector<EmbeddedResourceRef> sounds;
    std::vector<ModelPackageRef> models;
    std::vector<EmbeddedResourceRef> scenes;
    std::optional<EmbeddedResourceRef> extra;
};
```

Decoded resources should remain lazy where practical:

```text
SCX parser:
    establish safe spans/offsets

consumer:
    Path3DP::load(...)
    Animation3DA::load(...)
    Model3DO::load(...)
    Texture3DT::load(...)
    WAV/audio loader
```

This mirrors the original distinction between manifest loading and subsystem
resource use while avoiding pointer mutation.

---

# 33. Documentation-oriented descriptor structures

```c
#pragma pack(push, 1)

typedef struct ScxHeaderV5 {
    uint32_t magic;
    uint32_t version;
    uint32_t unknown08;
    uint32_t descriptorSize;
} ScxHeaderV5; // 0x10

typedef struct ScxPathDescriptor {
    char     name[24];
    uint32_t runtimePathsPlaceholder;
    uint32_t serializedSubpathCount;
} ScxPathDescriptor; // 0x20

typedef struct ScxAnimationDescriptor {
    char     name[24];
    uint32_t runtimePlaceholder;
    uint32_t unknown1C;
    uint32_t animationId;
} ScxAnimationDescriptor; // 0x24

typedef struct ScxSoundDescriptor {
    char     name[22];
    uint16_t soundField16;
    uint16_t hId;
} ScxSoundDescriptor; // 0x1A

typedef struct ScxSpriteModelDescriptor {
    char     name[24];
    uint32_t runtimePlaceholder;
    uint32_t unknown1C;
    uint32_t spriteId;
} ScxSpriteModelDescriptor; // 0x24

typedef struct ScxSceneDescriptor {
    char     name[24];
    uint32_t runtimePlaceholder;
} ScxSceneDescriptor; // 0x1C

typedef struct ScxEmbeddedResourceHeader {
    uint32_t selfOffset;
    uint32_t payloadSize;
} ScxEmbeddedResourceHeader; // 0x08

typedef struct ScxModelPackageHeader {
    uint32_t selfOffset;
    uint32_t coreSize;
    uint32_t auxiliarySize;
} ScxModelPackageHeader; // 0x0C

#pragma pack(pop)
```

These are serialized-layout aids, not recommended mutable runtime structures.

---

# 34. Script-section handoff

`scx.md` should not become a second copy of the script documentation.

The SCX-specific facts are:

```text
DEAD0002 exists in the descriptor block
it owns no separate appended resource
its templates are 0x64 bytes
its commands are 0x18 bytes
it contains the shared value pool and variable-length per-script tails
```

For the complete grammar and runtime interpretation, use:

[`script-opcodes.md`](script-opcodes.md)

For function IDs:

[`iam-script-functions.md`](iam-script-functions.md)

---

# 35. Resource relationships to IAM functions

SCX descriptor tables are consumed by structured IAM functions.

Examples:

```text
DEAD0000 path table
    ->
MoveObjectOnPath
SelectRelativeBodyAnimation

DEAD0001 animation table
    ->
SelectBodyAnimation
SelectRelativeBodyAnimation
AnimationFromExternalScene

DEAD0003 sound table
    ->
PlaySound
PlaySyncSound
StopSound

DEAD0004 sprite/model table
    ->
Display3DSprite
Display3DSpriteOnPath
SetSpriteFrame
palette/sprite operations
```

The exact argument mapping is documented in `iam-script-functions.md`.

This cross-link is important because descriptor-array **index** and any explicit descriptor numeric **ID** are not automatically interchangeable concepts.

---

# 36. IDs versus array indexes

Some descriptor types contain explicit numeric IDs:

```text
DEAD0001 +0x20 animationId
DEAD0004 +0x20 spriteId
```

`DEAD0000 +0x1C` must **not** be included in that list: Runtime uses it as the loaded 3DP subpath-count output slot.

At the same time, some Runtime/script operations use array/table positions.

Do not assume:

```text
resource ID == descriptor array index
```

without tracing the specific consumer.

For example, `Grid.SCX` animation IDs are:

```text
3, 4, 5
```

for descriptor array indexes:

```text
0, 1, 2
```

That alone proves the namespaces can differ.

---

# 37. File-order resource pairing

Within one descriptor category, appended payloads correspond positionally:

```text
descriptor[0] <-> resource[0]
descriptor[1] <-> resource[1]
...
```

There is no per-resource filename repeated in the appended frame.

The name lives in the descriptor.

Therefore the manifest must retain this positional relationship.

---

# 38. Zero-count sections

A recognized descriptor section can have:

```text
count = 0
```

and therefore contain only:

```text
tag
u32 zero
```

Examples:

```text
Grid:
    DEAD0005 = 0
    DEAD0006 = 0
    DEAD0007 = 0

aventure:
    DEAD0000 = 0
    DEAD0001 = 0
    DEAD0005 = 0
    DEAD0006 = 0
    DEAD0007 = 0
```

Zero count does not imply that the tag itself is absent.

---

# 39. Optional tags

The parser does not require every tag to appear.

The two supplied files do not contain:

```text
DEAD0008
DEAD000A
```

yet Runtime has explicit handling for them.

Do not use the sample files as proof that those sections are unused globally.

---

# 40. String representation

Descriptor names are fixed-width byte arrays.

Known widths:

```text
24 bytes:
    DEAD0000
    DEAD0001
    DEAD0004
    DEAD0005

22 bytes:
    DEAD0003 sound name
    DEAD0002 script name

21 bytes:
    DEAD0002 related-script name
    DEAD0002 binding names
```

Strings are generally NUL-terminated/padded when shorter than their field.

Do not bake a guessed text encoding such as CP850/CP858/Windows-1252 into the
binary-format definition until localized asset evidence establishes it.

Preserve the raw bytes if exact round-tripping matters.

---

# 41. Error handling and parser safety

A modern parser should validate all arithmetic and spans.

Minimum checks:

1. file is at least `0x10` bytes;
2. magic is `0x00DEAD00`;
3. supported version is known;
4. descriptor block fits in the file;
5. every count × record-size multiplication is overflow-safe;
6. every fixed record array fits inside the descriptor;
7. `DEAD0002` value slices and linked indexes are bounded;
8. framed resource header fits;
9. framed payload fits;
10. model core + auxiliary payload fits;
11. model core starts with `OD3X`;
12. supported model version is checked;
13. WAV payload is structurally RIFF/WAVE before audio decode;
14. manifest/resource ordering is preserved;
15. descriptor gaps are retained rather than silently discarded from forensic
    output;
16. resource end positions never move backward or overflow.

---

# 42. OpenNomad strictness that is not yet a Runtime-format guarantee

Current OpenNomad intentionally adds several safety rules.

They should remain identified as implementation policy.

## 42.1 Duplicate recognized tags

OpenNomad rejects them.

Runtime's equivalent rejection is not yet proven.

## 42.2 Descriptor trailing bytes

OpenNomad requires no bytes after `DEADFFFF` inside the declared descriptor.

Retail examples satisfy it.

Runtime's tolerance is not yet proven.

## 42.3 Resource self-offset equality

OpenNomad validates it.

Retail examples satisfy it.

Runtime's exact use/check is not fully traced.

## 42.4 Exact EOF manifest consumption

OpenNomad requires the final resource to end at EOF.

Retail examples satisfy it.

Runtime may or may not tolerate unrelated trailing bytes.

## 42.5 `DEAD0007` low-27-bit mask

Unlike the safety checks above, this one appears to be a **potential
implementation mistake**, not merely a stricter policy.

Revisit it against Runtime before relying on high-bit flags.

---

# 43. Why descriptor gaps must be preserved

A parser that simply says:

```text
unknown dword -> ignore forever
```

throws away potentially valuable reverse-engineering evidence.

Better:

```text
record:
    absolute file offset
    length
    raw bytes
```

Reasons:

- the `DEAD0004`-adjacent gaps are clearly structured;
- their size correlates with model count;
- they contain pointer-like values;
- one file contains repeated graphics-format text;
- future Runtime xrefs may identify a second consumer;
- other SCX files may expose stable internal fields.

OpenNomad's `descriptor_gaps` concept is therefore useful and should be kept.

---

# 44. Suggested SCX inspection tool output

A forensic/debug dump should report:

```text
header:
    magic
    version
    unknown08
    descriptorSize
    resourceStreamOffset
    fileSize

descriptor:
    tag order
    tag file offsets
    counts
    record offsets
    names
    IDs
    raw placeholder values

gaps:
    start
    end
    size
    repeating-stride candidates
    printable strings

manifest:
    ordinal
    source tag
    source descriptor index/name
    header offset
    payload/core offset
    payload/core size
    auxiliary offset/size
    next offset

scripts:
    summary only
    detailed dump delegated to script tooling
```

This will make cross-SCX comparison much easier.

---

# 45. Suggested corpus analysis

For every retail SCX, collect:

```text
file name
file size
header unknown08
descriptor size
tag order

per tag:
    count
    descriptor stride
    names
    IDs
    placeholder distributions

descriptor gaps:
    count
    location relative to tag sections
    size
    repeating stride
    stable strings/signatures

resource frames:
    self-offset correctness
    payload sizes
    inner magic/type
```

Highest-value questions from a full corpus:

- is `unknown08` always 8?
- can a recognized tag repeat?
- does `DEAD0004` always have one opaque block per model?
- what determines `0x190` versus `0x1A4` opaque model-block size?
- do non-zero `DEAD0005/6/7` examples exist?
- which files contain `DEAD0008` or `DEAD000A`?
- do any resources have padding/alignment?
- are all self offsets absolute and exact?
- are descriptor placeholders stable across builds/localizations?

---

# 46. Highest-value remaining reverse engineering

## 46.1 `DEAD0004` opaque companion blocks

This is probably the most immediately interesting SCX-format gap.

Need to determine:

- exact start/end per model;
- stride-selection rule;
- whether Runtime uses the data elsewhere;
- relationship to 3DO materials/textures;
- meaning of embedded graphics-format strings;
- whether blocks are serialized DirectDraw/Direct3D state or editor metadata.

## 46.2 `DEAD0005` scene payload

Find an SCX with non-zero scene count and trace its consumer.

## 46.3 `DEAD0006`

Find a non-zero record and map the `0x318` structure.

## 46.4 `DEAD0007`

Find a non-zero count.

Confirm whether the count dword ever uses high bits and correct OpenNomad's mask
if necessary.

## 46.5 `DEAD0008`

Identify the meaning of Runtime global `0x004C7DA4`.

## 46.6 `DEAD000A`

Recover the payload type handled by the downstream loader near `0x0049EEF0`.

## 46.7 Header `+0x08`

Determine why retail files contain:

```text
8
```

and whether other versions/builds differ.

## 46.8 Placeholder fields

Trace which are:

- overwritten;
- relocated;
- ignored;
- used as identifiers;
- preserved from the build/editor environment.

---

# 47. Useful Runtime locations

| Address | Current role |
|---:|---|
| `0x00449750` | main SCX load path |
| `0x00449881` area | `DEAD0002` script parsing |
| `0x00449AA0` area | `DEAD0000` path descriptors |
| `0x00449B1F` area | `DEAD0001` animation descriptors |
| `0x00449BA0` area | `DEAD0003` sound descriptors |
| `0x00449C15` area | `DEAD0004` sprite/model descriptors |
| `0x00449D00` area | `DEAD0005` scene descriptors |
| `0x00449D7D` | `DEAD0006`, count × `0x318` |
| `0x00449DAC` | `DEAD0007`, count × `0x20`, copied to global storage |
| `0x00449DD8` | `DEAD0008`, writes `0x100` to global `0x004C7DA4` |
| `0x00449DE4` | `DEAD000A` appended block read/dispatch |
| `0x0049EEF0` | downstream consumer reached by `DEAD000A` path |

Some entries are branch regions inside one larger loader rather than clean
source-function boundaries.

---

# 48. Current OpenNomad source locations

```text
src/core/Core/Omikron/SCX.hpp
src/core/Core/Omikron/SCX.cpp
```

Resource consumers:

```text
src/core/Core/Omikron/Path3DP.*
src/core/Core/Omikron/Animation3DA.*
src/core/Core/Omikron/Model3DO.*
src/core/Core/Omikron/Texture3DT.*

src/core/Core/Sprite/SpriteResource.*
src/core/Core/Scenario/ScenarioRuntime.*
src/core/Core/Script/ScriptRuntime.*
```

---

# 49. Parser checklist

- [ ] Read 16-byte header.
- [ ] Verify `0x00DEAD00`.
- [ ] Verify supported container version.
- [ ] Bound `descriptorSize`.
- [ ] Scan the descriptor dword-by-dword.
- [ ] Dispatch recognized tags.
- [ ] Preserve unknown words as opaque gaps.
- [ ] Stop on `DEADFFFF`.
- [ ] Preserve tag order.
- [ ] Build appended-resource manifest in encounter order.
- [ ] Preserve descriptor record order.
- [ ] Preserve raw pointer/placeholder values.
- [ ] Parse `DEAD0002` with its dedicated grammar.
- [ ] Parse generic 8-byte resource frames.
- [ ] Parse special 12-byte model packages.
- [ ] Validate absolute self offsets where desired.
- [ ] Validate resource ranges.
- [ ] Keep 3DP/3DA/3DO/3DT/WAV decoding in their respective consumers.
- [ ] Preserve opaque descriptor gaps.
- [ ] Revisit `DEAD0007` count-mask behavior.
- [ ] Report unclaimed trailing bytes rather than silently hiding them.

---

# 50. Compact reference

```text
SCX v5
======

header 0x10:
    +0x00  0x00DEAD00
    +0x04  5
    +0x08  unknown, observed 8
    +0x0C  descriptorSize

descriptor:
    begins 0x10
    ends   0x10 + descriptorSize
    DEADFFFF included in descriptorSize

recognized tags:
    DEAD0000  paths        count + count*0x20
    DEAD0001  animations   count + count*0x24
    DEAD0002  scripts      variable
    DEAD0003  sounds       count + count*0x1A
    DEAD0004  sprite/3DO   count + count*0x24
    DEAD0005  scenes       count + count*0x1C
    DEAD0006  opaque       count + count*0x318
    DEAD0007  globals      count + count*0x20
    DEAD0008  marker       no body; Runtime global = 0x100
    DEAD0009  ignored/reserved
    DEAD000A  marker       one appended generic resource
    DEADFFFF  end

unknown descriptor dword:
    Runtime consumes 4 bytes and keeps scanning

generic appended resource:
    u32 absoluteSelfOffset
    u32 payloadSize
    payload[payloadSize]

DEAD0004 model package:
    u32 absoluteSelfOffset
    u32 coreSize
    u32 auxiliarySize
    OD3X core[coreSize]
    3DT-style auxiliary[auxiliarySize]

resource stream order:
    descriptor tag encounter order
    then record order inside each section
```

---

# 51. Current format boundary

The currently established SCX architecture is:

```text
SCX v5
    |
    +-- 16-byte header
    |
    +-- descriptor block
    |      |
    |      +-- recognized DEAD tags
    |      +-- structured scripts
    |      +-- pointer/handle-shaped serialized fields
    |      +-- opaque untagged data
    |      +-- DEADFFFF
    |
    +-- appended resource stream
           |
           +-- path payloads
           +-- animation payloads
           +-- RIFF/WAVE
           +-- OD3X + 3DT-style model packages
           +-- scene payloads
           +-- extra payload
```

The major remaining uncertainty is no longer the basic container framing.

The highest-value unknowns are the **opaque per-model descriptor blocks** and
the still-unseen/non-zero `DEAD0005`, `DEAD0006`, `DEAD0007`, `DEAD0008`, and
`DEAD000A` cases.
