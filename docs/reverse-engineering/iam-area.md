# Omikron IAM `AREA` archive and area-record format

## Runtime character-reference overlay

Table 0 is a 0x14-byte character placement. `+0x00` is the serialized seed for
Runtime's mutable actor-slot field and `+0x02` is the serialized character
reference. OpenNomad keeps IAM bytes immutable and represents the mutable values
in `CharacterReferenceRuntime`: `+0x00` maps to runtime-only `BodyIdentity` and
`+0x02` maps to mutable `reference_character_id`. `+0x12` remains a separate
state-bit field.

> **Status:** work-in-progress reverse-engineering documentation for OpenNomad  
> **Last updated:** 2026-08-25
>
> This document describes the Windows retail `IAM/AREA` archive and the
> serialized AREA records stored inside it.
>
> AREA is one of the central pieces of Omikron's data-driven world/scenario
> architecture. One AREA record combines:
>
> - dependency/resource names;
> - character placements and character definitions;
> - object/item placements and definitions;
> - trigger/zone geometry;
> - named world addresses/spawn points;
> - links to other areas;
> - a compact event bytecode pool;
> - camera definitions;
> - and several still-unresolved configuration fields.
>
> AREA bytecode is **not** the same script representation as SCX `DEAD0002`.
> AREA uses the compact one-byte-opcode VM documented in
> [`script-opcodes.md`](script-opcodes.md).
>
> This file is authoritative for:
>
> 1. the indexed `IAM/AREA` archive;
> 2. the fixed `0xB4` AREA-record header;
> 3. AREA table layout and the currently recovered table semantics;
> 4. record-relative event entrypoints and bytecode-pool placement;
> 5. the relationship between AREA data and the compact AREA VM.
>
> Related documentation:
>
> - [`script-opcodes.md`](script-opcodes.md) — authoritative for AREA VM
>   opcodes, execution state, waits, and the AREA → SCX bridge;
> - [`scx.md`](scx.md) — SCX resource/scenario packages named by AREA;
> - [`3do.md`](3do.md) — AREA decor/model dependencies;
> - [`runtime-coordinate-math.md`](runtime-coordinate-math.md) — conversion of
>   AREA position and angle fields;
> - [`startup-sequence.md`](startup-sequence.md) — `IAM/START` → `IAM/AREA`
>   startup order;
> - [`iam-scene.md`](iam-scene.md) — attached SCENE archive/record format and
>   the subsequent AREA-to-SCENE presentation handoff;
> - [`save-format.md`](save-format.md) — persistent state used by AREA character
>   and object activation.

---

# 1. Evidence model

Sources are used in this order:

1. **`Runtime.exe` behavior** — authoritative for archive lookup, AREA
   relocation/use, character/camera resolution, event activation, and compact
   VM behavior.
2. **Retail `IAM/AREA`** — the supplied 1,253,376-byte Windows retail archive.
3. **Retail TAG metadata**, especially:
   - `AREAS.TAG`
   - `OBJECTS.TAG`
   - `CAMERAS.TAG`
   - `ZONES.TAG`
   - `ADDRESSES.TAG`
   - `VARIABLES.TAG`
4. **OpenNomad's `IamArchive`, `IamArea`, `AreaScriptRuntime`, and scenario
   startup code** — useful for expressing the recovered model, but subordinate
   when current naming or bounds assumptions differ from Runtime/data.

Analyzed Runtime build:

```text
File:             Runtime.exe
Architecture:     PE32 / i386
Image base:       0x00400000
Linker timestamp: 1999-10-04 20:31:50
SHA-256:          55f7120bfea7891b048c64e3682f3259cdbf2719a43fa24e42254b753c95d2ef
```

Confidence labels:

- **Confirmed — Runtime:** directly established by executable behavior.
- **Confirmed — data:** directly established from retail bytes/TAG metadata.
- **Corroborated:** Runtime and retail data agree.
- **Strongly reconstructed:** multiple independent observations agree but the
  original source-level name is unavailable.
- **Provisional:** useful working interpretation requiring further tracing.
- **Unknown:** physical field/record is known but semantics remain unresolved.

---

# 2. Two nested formats

`IAM/AREA` contains two distinct structures:

```text
IAM/AREA archive
    |
    +-- paged index
    |
    +-- AREA record 0
    +-- AREA record 1
    +-- ...
    +-- AREA record 258
```

Each record then contains:

```text
AREA record
    |
    +-- fixed 0xB4 header
    +-- table 0
    +-- table 1
    +-- table 2
    +-- table 3
    +-- table 4
    +-- variable character text
    +-- table 5
    +-- table 7
    +-- AREA bytecode pool
    +-- table 6
```

The table numbers are the positions of their offset/count fields in the
serialized header.

They are **not** physical-order numbers: table 6 is physically last, while
table 7 appears immediately before the bytecode pool.

---

# 3. Retail archive summary

Supplied retail `IAM/AREA`:

```text
file size:
    0x132000
    1,253,376 bytes

populated AREA IDs:
    0 .. 258

populated records:
    259
```

`AREAS.TAG` likewise contains:

```text
259 named AREA IDs
0 .. 258
```

This is a strong cross-check that the archive record ID is the global AREA ID.

Examples:

```text
0   = Anekbah
1   = Jaunpur
2   = Anekbah Grotte Gandhar
...
118 = startup/menu GRID area
...
258 = final retail area entry
```

---

# 4. Indexed IAM archive

`IAM/AREA` uses the same paged indexed-archive mechanism represented in
OpenNomad by `IamIndexedArchive`.

Index constants:

```text
index page size:
    0x800 bytes

entry size:
    8 bytes

entries per page:
    256
```

Record `id` is located at:

```text
page  = id >> 8
entry = id & 0xFF

entryOffset =
    page * 0x800
    + entry * 8
```

One index entry is:

```c
struct IamArchiveEntry {
    uint32_t offset;   // absolute archive file offset
    uint32_t size;     // exact record byte size
}; // 0x08
```

Both values are little-endian.

---

# 5. AREA archive page layout

Because the retail archive contains IDs through `258`, it requires two index
pages:

```text
page 0:
    IDs 0..255
    file range 0x0000..0x07FF

page 1:
    IDs 256..511
    file range 0x0800..0x0FFF
```

The first actual AREA record begins at:

```text
0x1000
```

Therefore retail `IAM/AREA` begins:

```text
+---------------------------+ 0x0000
| index page 0              | 0x800
+---------------------------+ 0x0800
| index page 1              | 0x800
+---------------------------+ 0x1000
| AREA record 0             |
+---------------------------+
| alignment padding         |
+---------------------------+
| AREA record 1             |
+---------------------------+
| ...                       |
```

Unused index entries in the second page have zero size/offset state as
appropriate.

---

# 6. Record alignment

Every one of the 259 populated retail AREA records begins on a:

```text
0x800-byte boundary
```

The index entry's `size` does **not** include the alignment padding after a
record.

For every adjacent populated retail record:

```text
nextOffset =
    align_up(currentOffset + currentSize, 0x800)
```

The final record similarly ends before archive EOF and the archive is padded to
the next `0x800` boundary.

This is a confirmed property of the supplied retail archive.

A generic IAM archive parser should still use the explicit index offsets rather
than deriving record locations from this alignment.

---

# 7. Example archive entry — area 118

AREA ID:

```text
118
```

Index entry offset:

```text
118 * 8
= 0x3B0
```

Retail entry:

```text
recordOffset = 0x8D800
recordSize   = 0x09C0
```

Thus area 118 occupies:

```text
IAM/AREA:
    0x8D800 .. 0x8E1BF
```

OpenNomad correctly extracts this record through the archive index before
passing the isolated `0x9C0` bytes to `IamAreaRecord`.

---

# 8. AREA-record fixed header — `0xB4` bytes

Every analyzed record begins with a fixed:

```text
0xB4-byte header
```

Current sparse layout:

```text
+0x00  u32 runtime/context placeholder
+0x04  u32 primary/default event entry offset

+0x08  8 * u32 unresolved header fields

+0x28  8 * u32 table offsets
+0x48  8 * u16 table counts

+0x58  char model3doName[9]
+0x61  char scenarioScxName[9]
+0x6A  char mapMptName[9]
+0x73  char optionsOptName[9]
+0x7C  char animationAniName[9]
+0x85  char sky3doName[9]

+0x8E  unresolved header tail
...
+0xB3

+0xB4  first variable table
```

All offsets stored in an AREA record are:

```text
relative to the beginning of that AREA record
```

not to the beginning of `IAM/AREA`.

---

# 9. Header `+0x00` — runtime/context slot

Retail area 118 stores:

```text
+0x00 = 0
```

The current Runtime/OpenNomad model treats this as a runtime/context slot rather
than durable authored content.

Do not deserialize it as a live pointer.

A modern representation should preserve the serialized value separately from
the AREA VM/context object.

---

# 10. Header `+0x04` — event entrypoint, not bytecode start

This field was previously called:

```text
scriptOffset
```

in OpenNomad.

That name is too broad.

Retail corpus analysis establishes:

```text
if +0x04 != 0:
    +0x04 always lies inside the AREA bytecode pool
```

It does **not** identify the beginning of that pool.

Many AREA records contain substantial valid bytecode even when:

```text
+0x04 = 0
```

Example:

```text
area 3:
    header +0x04 = 0

    bytecode pool:
        0x0600 .. 0x084B

    nonzero bytecode is present
```

The best current interpretation is:

```text
primary/default/startup event entrypoint
```

or:

```text
primaryEventOffset
```

Confidence:

```text
strongly reconstructed
```

For area 118, this entrypoint happens to equal the start of the bytecode pool:

```text
+0x04 = 0x03FC
```

which is why the earlier `scriptOffset` model appeared to work.

---

# 11. Header `+0x08..+0x27`

Eight dwords occupy:

```text
+0x08
+0x0C
+0x10
+0x14
+0x18
+0x1C
+0x20
+0x24
```

They are frequently:

```text
0xFFFFFFFF
```

but are not universally so.

Earlier OpenNomad naming referred to this area as:

```text
related area IDs
```

Retail data does **not** support that name strongly enough.

Some repeated interior/dependency templates contain packed-looking non-`-1`
values far outside the valid AREA ID range.

Authoritative documentation should therefore leave these as:

```text
unknown08[8]
```

until Runtime consumers establish their semantics.

---

# 12. Table-offset directory — `+0x28`

Eight record-relative `u32` offsets:

```text
tableOffset[0]  +0x28
tableOffset[1]  +0x2C
tableOffset[2]  +0x30
tableOffset[3]  +0x34
tableOffset[4]  +0x38
tableOffset[5]  +0x3C
tableOffset[6]  +0x40
tableOffset[7]  +0x44
```

The physical order of tables is not the numeric table order.

Do not sort or walk tables by table number.

---

# 13. Table-count directory — `+0x48`

Eight `u16` counts:

```text
tableCount[0]  +0x48
tableCount[1]  +0x4A
tableCount[2]  +0x4C
tableCount[3]  +0x4E
tableCount[4]  +0x50
tableCount[5]  +0x52
tableCount[6]  +0x54
tableCount[7]  +0x56
```

Record counts are independent from offsets.

A zero-count table can share its offset with the following physical region.

---

# 14. Dependency-name block — `+0x58..+0x8D`

Six fixed fields, each exactly:

```text
9 bytes
```

Current recovered names:

| Offset | Width | Current role |
|---:|---:|---|
| `0x58` | 9 | decor/model 3DO dependency |
| `0x61` | 9 | SCX scenario/resource dependency |
| `0x6A` | 9 | map `.MPT` dependency |
| `0x73` | 9 | options `.OPT` dependency |
| `0x7C` | 9 | animation-bank `.ANI` dependency |
| `0x85` | 9 | sky 3DO dependency |

Strings are NUL-terminated/padded when shorter.

---

# 15. Area 118 dependency names

Retail startup area 118:

```text
model3doName:
    GRID

scenarioScxName:
    GRID

mapMptName:
    empty

optionsOptName:
    empty

animationAniName:
    empty

sky3doName:
    empty
```

This drives the startup dependency path:

```text
MESHES/DECORS/GRID.3DO
SCPTDATA/GRID.SCX
```

The names are data-driven properties of area 118; `GRID` is not a globally
hardcoded main-menu scenario name.

---

# 16. Example world-area dependency names

AREA 0, Anekbah:

```text
model3doName      = ANEKBAH
scenarioScxName   = ANEKBAH
mapMptName        = empty
optionsOptName    = ANEKBAH
animationAniName  = PASSANTH
sky3doName        = ASKY
```

These names illustrate that the six fields genuinely select different resource
classes.

---

# 17. Header tail — `+0x8E..+0xB3`

The remaining:

```text
0x26 bytes
```

of the fixed header are still only partially understood.

Retail corpus analysis shows that several fields are clearly authored
configuration rather than random padding.

For example:

```text
+0xA0
```

is `80` in most retail records but has other authored values in a minority of
areas.

Do not flatten this region into “padding”.

For now preserve:

```text
raw bytes +0x8E..+0xB3
```

and assign names only when Runtime access establishes semantics.

---

# 18. All eight table strides are now structurally known

Current retail-corpus analysis establishes:

| Table | Stride | Current semantic status |
|---:|---:|---|
| 0 | `0x14` | character placement |
| 1 | `0x18` | object/item placement |
| 2 | `0x44` | zone/trigger record |
| 3 | `0x18` | object/item definition |
| 4 | `0x114` | character/body definition core |
| 5 | `0x10` | address/spawn point |
| 6 | `0x2C` | camera |
| 7 | `0x08` | area link / event-entry mapping |

This corrects the current OpenNomad state where table 3's stride is still
reported as unresolved.

## 18.1 Table 3 stride proof

There are:

```text
125 retail AREA records with non-empty table 3
```

In every one:

```text
table4Offset - table3Offset
==
table3Count * 0x18
```

Therefore:

```text
table 3 stride = 0x18
```

is now a confirmed retail-data property.

---

# 19. Physical record order

Ignoring optional variable text after table 4, retail AREA records follow:

```text
0xB4 header
    |
    v
table 0
    |
    v
table 1
    |
    v
table 2
    |
    v
table 3
    |
    v
table 4 fixed 0x114-byte records
    |
    +-- optional character biography/personality strings
    |
    v
table 5
    |
    v
table 7
    |
    v
AREA bytecode pool
    |
    v
table 6 cameras
    |
    v
record end
```

For every analyzed retail record:

```text
table0Offset = 0xB4
```

and the fixed-size tables 0→1→2→3→4 are contiguous.

Table 5 begins either immediately after table 4 fixed records or after optional
variable character text.

Table 7 begins immediately after table 5.

Table 6 is physically final.

---

# 20. Bytecode pool boundaries

The AREA bytecode pool has no separate byte-size field.

Its boundaries are nevertheless recoverable from surrounding tables.

Start:

```text
bytecodeStart =
    table7Offset
    + table7Count * 0x08
```

End:

```text
bytecodeEnd =
    table6Offset
```

Thus:

```text
bytecode =
    record[bytecodeStart .. bytecodeEnd)
```

This rule works across the complete 259-record retail corpus.

---

# 21. Why `[header +0x04, record end)` is wrong

Current OpenNomad exposes:

```text
script_bytes() =
    [header.scriptOffset, record end)
```

That was sufficient for early startup experimentation but is not the actual AREA
layout.

Area 118:

```text
primary entrypoint:
    0x03FC

bytecode:
    0x03FC .. 0x051B

camera table:
    0x051C .. 0x09BF
```

Therefore:

```text
[0x03FC, record end)
```

incorrectly includes all 27 camera records as if they were bytecode.

The VM happens to stop on event termination before reaching those bytes.

The authoritative bytecode span is:

```text
[0x03FC, 0x051C)
```

for area 118.

---

# 22. AREA bytecode is a shared event pool

AREA does not contain one single linear startup script.

Instead, several structures point into a common bytecode pool.

Known entrypoint sources:

```text
header +0x04
table 2 +0x00
table 7 +0x00
```

All nonzero values in those fields in the retail corpus point inside:

```text
[bytecodeStart, bytecodeEnd)
```

This is the correct architecture:

```text
AREA record
    |
    +-- shared compact bytecode pool
            ^
            |
            +-- primary/default entry
            +-- zone-trigger entry
            +-- area-link entry
            +-- likely other Runtime-selected events
```

The bytecode itself is documented in `script-opcodes.md`.

---

# 23. Table 0 — character placements

Record size:

```text
0x14 bytes
```

Recovered layout:

```c
struct AreaCharacterPlacement {
    int16_t  runtimeOrSlot;       // +0x00, frequently -1
    int16_t  characterId;         // +0x02

    int32_t  positionX;           // +0x04
    int32_t  positionY;           // +0x08
    int32_t  positionZ;           // +0x0C

    int16_t  orientationUnits;    // +0x10
    uint16_t stateBitIndex;       // +0x12
}; // 0x14
```

Runtime opcode `0x4E` resolves characters through this table.

---

# 24. Table 0 character identity

Table 0 `+0x02` is the character identity used by AREA operations.

Every retail table-0 record pairs one-for-one with a table-4 character
definition.

Across the entire supplied archive:

```text
table0Count == table4Count
```

for all:

```text
259 AREA records
```

and, pairwise:

```text
table0[i].characterId
==
table4[i].characterId
```

for all:

```text
830 character records
```

This is stronger than a heuristic name match.

---

# 25. Table 0 `+0x12` — persistent state-bit index

Runtime opcode `0x4E` passes the final word to a persistent one-bit-state setter.

Thus:

```text
+0x12
```

is not the table-4 character ID.

The best current name is:

```text
stateBitIndex
```

This distinction matters because area 118 contains:

```text
character 310:
    stateBitIndex = 468

character 136:
    stateBitIndex = 469
```

while the corresponding table-4 character IDs remain:

```text
310
136
```

---

# 26. Table 0 coordinates

Serialized position uses three signed 32-bit integers.

Conversion belongs to Runtime coordinate math rather than the AREA parser.

Recovered conversion:

```text
inches =
    trunc(
        serialized * 39.37007874015748 / 256
        - 1
    )
```

using Runtime's x87 / `_ftol` truncation behavior.

See:

[`runtime-coordinate-math.md`](runtime-coordinate-math.md)

---

# 27. Table 0 orientation

`+0x10` uses Runtime's signed 16-bit angle units.

Recovered conversion:

```text
degrees =
    trunc(
        units * 360 / 4096
    )
```

Again, parsing should preserve the raw integer.

---

# 28. Area 118 character placements

Retail:

```text
record 0:
    runtimeOrSlot     = -1
    characterId       = 310
    position          = (-2588, -271, -816)
    orientationUnits  = 4084
    stateBitIndex     = 468

record 1:
    runtimeOrSlot     = -1
    characterId       = 136
    position          = (-3107, -264, -2989)
    orientationUnits  = 2286
    stateBitIndex     = 469
```

Character 310 is the Kay'l-related startup body used in the portal sequence.

---

# 29. Table 1 — object/item placements

Record size:

```text
0x18 bytes
```

Strongly recovered layout:

```c
struct AreaObjectPlacement {
    int16_t  runtimeOrSlot;       // +0x00, frequently -1
    uint16_t objectId;            // +0x02, OBJECTS.TAG identity

    int32_t  positionX;           // +0x04
    int32_t  positionY;           // +0x08
    int32_t  positionZ;           // +0x0C

    int16_t  orientationUnits;    // +0x10
    uint16_t field12;             // +0x12
    uint16_t field14;             // +0x14
    uint16_t field16;             // +0x16
}; // 0x18
```

The semantics of the final three words are not yet fully recovered.

---

# 30. Table 1 object identity

Table 1 pairs one-for-one with table 3.

Across all 259 retail areas:

```text
table1Count == table3Count
```

There are:

```text
546 paired object records
```

and for every pair:

```text
table1[i].objectId
==
low16(table3[i].idAndType)
```

Every such object ID exists in `OBJECTS.TAG`.

This establishes tables 1/3 as the AREA object/item pair.

---

# 31. Table 1 / OBJECTS.TAG examples

Area 35 includes objects such as:

```text
object ID 0:
    OBJECTS.TAG = Anneau
    table-3 model = ANNEAU

object ID 4:
    OBJECTS.TAG = Instrument Chirurgical
    table-3 model = CHIRINST

object ID 15:
    OBJECTS.TAG = Medikit Grand
    table-3 model = MEDIKITG

object ID 16:
    OBJECTS.TAG = Medikit Petit
    table-3 model = MEDIKITP
```

This cross-correlation is direct retail-data evidence, not naming guesswork.

---

# 32. Table 1 unresolved tail

Fields:

```text
+0x12
+0x14
+0x16
```

remain partially unresolved.

Observations:

- `+0x12` is often zero;
- `+0x14` commonly falls in the `0..4095` range and may be another authored
  angle/orientation-like quantity;
- `+0x16` is globally varied and unique across many object placements, making a
  persistent/runtime-state index plausible.

Do not assign final semantic names until Runtime consumers confirm them.

---

# 33. Table 3 — object/item definitions

Record size:

```text
0x18 bytes
```

Current layout:

```c
struct AreaObjectDefinition {
    uint16_t objectId;            // +0x00
    uint16_t typeOrFlags;         // +0x02

    uint16_t field04;             // +0x04
    uint16_t field06;             // +0x06
    uint16_t field08;             // +0x08
    uint16_t field0A;             // +0x0A
    uint16_t field0C;             // +0x0C

    char modelResource[10];       // +0x0E
}; // 0x18
```

The low word at `+0x00` matches the paired table-1 object ID.

The high word behaves like an authored type/category/flag value but its exact
source-level meaning is unresolved.

---

# 34. Table 3 model resource

The final:

```text
10 bytes
```

are a NUL-padded model/resource name.

Examples:

```text
ANNEAU
MEDIKITG
MEDIKITP
CHIRINST
PASSLAHO
BATPOUV
WIKI
```

These clearly correspond to world object/item visual resources.

---

# 35. Table 2 — zones / trigger geometry

Record size:

```text
0x44 bytes
```

AREA table 2 shares the immutable `IamZoneRecord` representation with SCENE
table 2. The event names remain deliberately neutral: only `event1` is
confirmed as first qualifying spatial contact; `event2` must not be promoted
to a per-frame “stay” event.

Confirmed layout:

```c
struct AreaZoneRecord {
    uint32_t event1Offset;        // +0x00, 0 means no entry
    uint32_t event2Offset;        // +0x04, 0 means no entry
    uint32_t event3Offset;        // +0x08, 0 means no entry

    int32_t point0[3];            // +0x0C
    int32_t point1[3];            // +0x18
    int32_t point2[3];            // +0x24
    int32_t point3[3];            // +0x30

    int16_t  orientationCenter;   // +0x3C, Runtime angle units
    int16_t  orientationSpan;     // +0x3E, Runtime angle units

    int16_t  zoneId;              // +0x40
    int16_t  unknown42;           // +0x42, commonly -1; semantics unknown
}; // 0x44
```

The four serialized XYZ points form an X/Z quadrilateral for spatial contact;
Y is ignored. Runtime uses an ordinary even/odd ray-crossing test. A zero
orientation span accepts every heading; otherwise the heading must lie within
the wrapped center ± half-span interval.

The persistent ZONE bit controls whether the record is available to the
transient contact producer; geometric overlap alone does not create a contact.
Fresh reporting is restricted to the current controlled character while its
body is resident and its controller is enabled. Disabling the controller while
inside a zone therefore suppresses a new contact, while re-enabling it allows
the next contact-production pass to report the still-matching zone. An already
reported contact keeps its compact context while its event runs, including
after the zone disables itself, and is released once that event becomes idle.

---

# 36. Table 2 event entrypoint

Across the full retail corpus:

```text
nonzero table2[i].event1Offset/event2Offset/event3Offset
```

always lies within that AREA record's shared bytecode pool.

Counts:

```text
nonzero zone event offsets:
    2891

zero zone event offsets:
    1175
```

No nonzero value falls outside the bytecode span.

All three fields are record-relative event entrypoints. Zone contexts use the
complete owning AREA record as immutable byte storage, because a zone entry is
not limited to the top-level `script_bytes()` span. A nonzero entry must be
within the owning record; zero is a missing, harmless event.

---

# 37. Table 2 / ZONES.TAG relationship

The identity field near the end of table 2 correlates with `ZONES.TAG`.

Many records use straightforward low values:

```text
Anekbah table-2 records:
    zone IDs 0, 1, 2, ...
```

Other records use the high bit in the serialized word, suggesting:

```text
zone ID + one or more flag bits
```

The exact bit packing remains unresolved.

`ZONES.TAG` names only a subset of all zone-like IDs present in retail AREA,
which is consistent with unnamed/internal trigger zones.

Do not use TAG coverage as a parser validity requirement.

---

# 38. Table 4 — character/body definitions

Fixed core size:

```text
0x114 bytes
```

Each table-4 core corresponds one-for-one with table 0.

Several fields are now structurally established:

```c
struct AreaCharacterDefinitionCore {
    uint32_t biographyOffset;        // +0x00, 0 or record-relative text
    uint32_t personalityOffset;      // +0x04, 0 or record-relative text

    char displayName[32];            // +0x08
    char roleOrProfession[32];       // +0x28

    char adventureControl[18];       // +0x48
    char combatControl[18];          // +0x5A

    // many authored fields remain unresolved

    char modelResource[10];          // +0x90

    // many authored stats/flags/etc.

    int32_t characterId;             // +0x110
}; // 0x114
```

The exact source-level names of `adventureControl` and `combatControl` are not
yet proven, but retail values such as:

```text
H1AVNT
H1CMBT
```

correspond directly to the game's `.CTL` control banks.

---

# 39. Table 4 text offsets

`+0x00` and `+0x04` are not ordinary scalar properties.

Across the retail corpus:

```text
nonzero +0x00 pointers:
    35

nonzero +0x04 pointers:
    35
```

Every one points into the variable text area located after the fixed table-4
cores and before table 5.

No nonzero pointer falls outside that region.

Strong current names:

```text
+0x00 biography/description offset
+0x04 personality/likes-dislikes offset
```

Both offsets are:

```text
AREA-record-relative
```

---

# 40. Variable character text region

When present:

```text
fixed table-4 cores
    |
    v
NUL-terminated biography/personality strings
    |
    v
table 5
```

This explains why:

```text
table5Offset
```

is sometimes larger than:

```text
table4Offset
+ table4Count * 0x114
```

The extra bytes are not alignment padding.

They contain authored character metadata.

---

# 41. Area 118 character biography example

Area 118 has two table-4 character definitions.

The second definition begins at record offset:

```text
0x01F0
```

and contains:

```text
+0x00 = 0x0304
+0x04 = 0x03A5
```

The fixed table-4 cores end at:

```text
0x0304
```

At exactly `0x0304` begins the biography text for KUMA'R.

At exactly `0x03A5` begins the second personality/preferences string.

The text region ends before table 5 at:

```text
0x03EC
```

This is direct proof that the first two table-4 dwords are text offsets.

---

# 42. Area 118 KUMA'R fixed fields

Representative table-4 values:

```text
displayName:
    KUMA'R 825

roleOrProfession:
    Adventurer

adventureControl:
    H1AVNT

combatControl:
    H1CMBT

modelResource:
    KUM_FN

characterId:
    136
```

The corresponding table-0 placement has:

```text
characterId = 136
```

as expected.

---

# 43. Table 5 — named addresses / spawn points

Record size:

```text
0x10 bytes
```

Recovered layout:

```c
struct AreaAddressRecord {
    int32_t  positionX;          // +0x00
    int32_t  positionY;          // +0x04
    int32_t  positionZ;          // +0x08

    int16_t  orientationUnits;   // +0x0C
    uint16_t addressId;          // +0x0E
}; // 0x10
```

This table is now strongly confirmed by retail TAG correlation.

---

# 44. Table 5 / ADDRESSES.TAG proof

Across all 259 AREA records:

```text
total table-5 records:
    791

unique table-5 address IDs:
    791

minimum ID:
    0

maximum ID:
    790
```

`ADDRESSES.TAG` contains exactly:

```text
791 entries
IDs 0..790
```

The sets match exactly:

```text
AREA table-5 IDs
==
ADDRESSES.TAG IDs
```

This is effectively definitive identification of table 5 as the address/spawn
table.

---

# 45. Address examples

`ADDRESSES.TAG` includes entries such as:

```text
0  = Anekbah - Appartement de Kay'l
1  = Anekbah - Sas vers Qalisar
2  = Anekbah - Appartement de Jenna
3  = Anekbah - Morgue zone 42
...
```

The corresponding table-5 record supplies:

```text
XYZ position
orientation
address ID
```

These are suitable for teleports, entry points, respawn/travel locations, and
other systems that resolve a global address.

---

# 46. Table 7 — area links / transition event mapping

Record size:

```text
0x08 bytes
```

Recovered layout:

```c
struct AreaLinkRecord {
    uint32_t eventOffset;       // +0x00, 0 or record-relative bytecode entry
    uint32_t targetAreaId;      // +0x04
}; // 0x08
```

There are:

```text
133
```

table-7 records in the supplied archive.

---

# 47. Table 7 target AREA proof

For every table-7 record:

```text
targetAreaId
```

falls in:

```text
0..258
```

which is exactly the populated retail AREA-ID namespace.

Examples:

```text
area 2 links to:
    3
    1
    8
    7
    6
    5
    9
    0
    11
    10
```

Those values are valid records in `AREAS.TAG`.

The semantic identification as an area-link/transition table is therefore very
strong.

---

# 48. Table 7 event offsets

Every nonzero table-7 `eventOffset` points into the same AREA record's bytecode
pool.

Thus a link entry combines:

```text
target area
+
event handler/entrypoint associated with that transition
```

One retail entry contains:

```text
eventOffset = 0
```

which should be preserved as a valid null/no-handler case.

---

# 49. Table 7 physically precedes bytecode

For every record:

```text
bytecodeStart =
    table7Offset + table7Count * 8
```

When table 7 is empty:

```text
bytecodeStart == table7Offset
```

This is not just a convenient heuristic; it holds across the complete supplied
archive.

---

# 50. Table 6 — cameras

Record size:

```text
0x2C bytes
```

Current layout:

```c
struct AreaCameraRecord {
    int32_t eyeX;              // +0x00
    int32_t eyeY;              // +0x04
    int32_t eyeZ;              // +0x08

    int32_t targetX;           // +0x0C
    int32_t targetY;           // +0x10
    int32_t targetZ;           // +0x14

    int16_t  cameraId;         // +0x18
    uint16_t cameraType;       // +0x1A

    int16_t rollUnits;         // +0x1C
    int16_t horizontalFovUnits;// +0x1E

    int16_t targetAttachmentSelector; // +0x20
    int16_t eyeAttachmentSelector;    // +0x22

    uint16_t tail24;           // +0x24
    uint16_t tail26;           // +0x26
    uint16_t tail28;           // +0x28
    uint16_t tail2A;           // +0x2A
}; // 0x2C
```

Runtime camera-selection handlers copy the first two position vectors and use
the recovered roll/FOV fields. Camera controller state retains authored
participant character IDs and resolves their live entity transforms during
camera updates.

Compact camera IDs are not resolved only against the calling AREA. Runtime's
fixed two-resident AREA/SCENE order and session-wide fallback are documented in
[`iam-global.md`](iam-global.md).

- Selector `-1` uses the normalized vector as an absolute endpoint.
- Selector `0` resolves participant A and uses
  `A.position - transform_vector(relative, A.principalOrientation)`.
- Selector `6` resolves participants A and B. Let
  `M = (A.position + B.position) * 0.5`, normalize the full XYZ vector
  `N = (A.position - B.position) / length(A.position - B.position)`, and derive
  `yaw = atan2(N.z, N.x) * 180/pi + 90`. The endpoint is
  `M - transform_vector(relative, euler_rotation_degrees({0,yaw,0}))`.

Target and eye each transform their own authored vector but share the same
live selector-6 midpoint and yaw. Selector 6 does not use either participant's
principal orientation. OpenNomad uses the absolute endpoint as a modern safe
fallback when either participant is missing or their positions coincide.

Selectors `1..5` and `7..9` dispatch to distinct Runtime resolver functions;
their transform semantics remain unsupported and deliberately use the safe
absolute fallback rather than being treated as selector `0` or `6`.

---

# 51. Table 6 is physically last

Across all 259 retail records:

```text
table6Offset
+ table6Count * 0x2C
==
recordSize
```

There are no bytes after the camera table inside an AREA record.

This is an extremely useful parser invariant for the retail format.

It also establishes:

```text
bytecodeEnd = table6Offset
```

because the shared bytecode pool is the region immediately preceding table 6.

---

# 52. Area 118 cameras

Area 118 contains:

```text
27 cameras
```

Table 6:

```text
offset = 0x051C
count  = 27

0x051C + 27 * 0x2C
=
0x09C0
=
record size
```

A representative camera:

```text
eye:
    (-3287, -159, -1701)

target:
    (-3214, -269, -944)

cameraId:
    2172

cameraType:
    12

rollUnits:
    0

horizontalFovUnits:
    853
```

---

# 53. Camera IDs and CAMERAS.TAG

Many AREA camera IDs correlate directly with the global editor metadata in:

```text
CAMERAS.TAG
```

The TAG file is not a complete list of every camera ID found in all AREA
records, so absence from TAG is not a format error.

This is similar to ZONES.TAG: named/editor-exposed entries are a subset of all
runtime data.

---

# 54. Compact AREA bytecode

The bytecode pool is a raw stream:

```text
u8 opcode
inline operands
u8 opcode
inline operands
...
```

There is:

- no per-instruction size byte;
- no SCX-style `0x18` command record;
- no command pointer table;
- no bytecode magic;
- no embedded global bytecode length.

Instruction lengths are opcode-specific.

Authoritative opcode layouts belong in:

[`script-opcodes.md`](script-opcodes.md)

---

# 55. Area 118 bytecode pool

Area 118:

```text
bytecodeStart = 0x03FC
bytecodeEnd   = 0x051C
bytecodeSize  = 0x0120
              = 288 bytes
```

First bytes:

```text
0D AF 00
0E AA 00 32
38 88 00
4F FF FF
68
5C E5 03
83 00 00 01 00
67 6D 00 01 00 01 00
...
```

This is valid compact AREA bytecode.

---

# 56. Area 118 primary entrypoint

Header:

```text
+0x04 = 0x03FC
```

so the primary/default event starts at the beginning of the bytecode pool.

Later in that same shared pool is the startup/main-menu sequence including:

```text
46 1D 00 FF FF 13 00
```

decoded as:

```text
OpenInterface(
    interfaceId    = 29,
    argument       = -1,
    resultVariable = 19)
```

The AREA VM waits in Runtime state 6 until interface completion.

---

# 57. AREA event architecture

The recovered model is:

```text
AREA record
    |
    +-- authored tables
    |
    +-- shared event bytecode
            ^
            |
            +-- primary event pointer
            +-- zone event pointers
            +-- area-link event pointers
```

Runtime creates/uses an AREA scenario context and activates individual event
entrypoints rather than treating the entire bytecode pool as one linear script.

This explains why:

```text
header +0x04 may be zero
```

without implying that the AREA has no script logic.

---

# 58. AREA VM state model

Detailed VM semantics are documented in `script-opcodes.md`.

High-level recovered lifecycle:

```text
context created
    |
    v
event queued
    |
    v
context explicitly activated
    |
    v
event executes from selected entrypoint
    |
    +-- complete -> ready for another event
    |
    +-- wait -> native/subsystem completion
    |
    +-- error/unsupported
```

Known Runtime numeric states include:

```text
1  running
4  tracked character-script wait
6  interface wait
7  camera wait
```

Do not serialize those runtime states into AREA file structures.

---

# 59. VM stack and global variables

AREA bytecode is a small evaluation VM, not merely a list of commands.

Recovered core operations include:

```text
0x07 PushInt8
0x0A PushGlobalVariable
0x19 Equal
0x06 BranchIfFalse
0x04 JumpRelative
0x0D SetGlobalVariableOne
0x0E SetGlobalVariable
```

Global variables are the same wider IAM/START/global-variable namespace
referenced by `VARIABLES.TAG`.

This makes AREA the event/control layer coordinating persistent game state,
world objects, characters, cameras, interfaces, music, and SCX scripts.

---

# 60. AREA → SCX bridge

AREA bytecode can launch SCX structured scripts.

Known operations:

```text
0x39:
    launch SCX script by script ID
    wait for exact child instance

0x3B:
    launch character-bound SCX script
    fire-and-forget

0x3C:
    launch character-bound SCX script
    wait in Runtime state 4
```

This is why AREA and SCX must remain separate documentation domains.

AREA:

```text
event/world orchestration VM
```

SCX:

```text
structured timed action scripts/resources
```

---

# 61. AREA characters and SCX body animation

A typical bridge is:

```text
AREA event
    |
    +-- resolve/activate character from table 0
    |
    +-- launch character-bound SCX script
            |
            +-- SelectRelativeBodyAnimation
                    |
                    +-- 3DO body hierarchy
                    +-- 3DA animation
                    +-- 3DP path
```

AREA provides the world/entity context.

SCX/3DA/3DP provide the more detailed animated action.

---

# 62. Object activation

AREA opcode:

```text
0x5C
```

is an object-related activation operation.

The table 1 / table 3 pair is the natural AREA-side source for those object
identities/resources.

Current OpenNomad still treats the downstream object behavior as provisional.

The file-format identification of tables 1/3 is stronger than the current
runtime implementation coverage.

---

# 63. Camera operations

AREA camera opcodes:

```text
0x5F
0x60
```

resolve camera IDs through the complete resident AREA/SCENE namespace and then
`IAM/GLOBAL`; see [`iam-global.md`](iam-global.md). The calling compact context
still owns command presentation and wait completion.

Current recovered distinction:

```text
0x5F:
    select/schedule camera
    no Runtime state-7 wait

0x60:
    select/schedule camera
    nonzero duration waits in Runtime state 7
```

The camera data itself remains immutable authored AREA data.

---

# 64. Addresses versus cameras versus zones

These three concepts are separate tables:

```text
table 2:
    zone/trigger geometry
    optional event entrypoint

table 5:
    named address/spawn point
    position + orientation + global address ID

table 6:
    camera definition
    eye + target + camera ID/type/FOV/etc.
```

Do not merge them into one generic “location” table.

Their global TAG namespaces are also distinct:

```text
ZONES.TAG
ADDRESSES.TAG
CAMERAS.TAG
```

---

# 65. AREA links versus header unknown dwords

Table 7 is the structure that demonstrably contains valid target AREA IDs.

This is why the eight dwords at header `+0x08..+0x27` should **not** currently
be named “related area IDs”.

If a field is meant to model area connectivity, table 7 has the direct evidence.

---

# 66. Record-relative pointers/offsets

Known AREA-record-relative offsets include:

```text
header +0x04 primary event entry
tableOffset[0..7]
table2 +0x00 zone event entry
table4 +0x00 biography string
table4 +0x04 personality string
table7 +0x00 link event entry
```

These should never be interpreted relative to:

```text
IAM/AREA archive start
```

unless explicitly stated otherwise.

The outer archive index is the layer that uses archive-absolute offsets.

---

# 67. Serialized offsets versus live pointers

Runtime frequently converts or replaces serialized offset/state fields during
loading.

A safe OpenNomad model should preserve:

```text
serialized AREA record
```

separately from:

```text
runtime context
resolved character/object handles
bytecode instruction pointers
camera instances
```

Avoid emulating in-place pointer relocation simply because the original
32-bit Runtime did so.

---

# 68. Character-definition text is not padding

Thirty retail AREA records contain bytes between:

```text
end of fixed table 4
```

and:

```text
table 5
```

Those bytes contain human-readable character biography/personality data.

Examples include authored descriptions of:

- KUMA'R;
- combat specialists;
- monks;
- researchers;
- police characters;
- merchants;
- athletes;
- other NPCs.

A parser that merely skips this region as alignment would throw away genuine
game data.

---

# 69. Table-4 text-pointer validation

A robust AREA parser can validate:

```text
if biographyOffset != 0:
    fixedTable4End <= biographyOffset < table5Offset

if personalityOffset != 0:
    fixedTable4End <= personalityOffset < table5Offset
```

All 70 nonzero text pointers in the supplied retail archive satisfy that rule:

```text
35 biography
35 personality
```

This is a useful corruption check.

---

# 70. Physical-order invariants from the retail corpus

The following are confirmed across all 259 supplied AREA records:

```text
table0Offset == 0xB4

table1Offset ==
    table0Offset + table0Count * 0x14

table2Offset ==
    table1Offset + table1Count * 0x18

table3Offset ==
    table2Offset + table2Count * 0x44

table4Offset ==
    table3Offset + table3Count * 0x18
```

Then:

```text
fixedTable4End =
    table4Offset + table4Count * 0x114

table5Offset >= fixedTable4End
```

with the difference being variable character text when nonzero.

---

# 71. Later physical-order invariants

Across all 259 supplied records:

```text
table7Offset ==
    table5Offset + table5Count * 0x10

bytecodeStart ==
    table7Offset + table7Count * 0x08

bytecodeEnd ==
    table6Offset

recordSize ==
    table6Offset + table6Count * 0x2C
```

These relationships are strong enough to build a much stricter and more
accurate parser than the current table-independent span model.

---

# 72. Why table offsets should still be honored

Although the retail corpus is highly regular, a parser should continue to read
the serialized table offsets.

Do not hardcode:

```text
table 0 then table 1 then ...
```

without checking the fields.

Reasons:

- other builds/debug assets may differ;
- explicit offsets are part of the format;
- preserving them helps diagnose malformed/corrupted data;
- variable table-4 text already demonstrates that not everything is
  count×stride contiguous.

The physical-order invariants are validation rules, not a reason to discard the
directory.

---

# 73. AREA record 118 complete structural map

```text
record size:
    0x09C0

header:
    0x0000 .. 0x00B3

table 0 characters:
    offset 0x00B4
    count  2
    size   2 * 0x14 = 0x28
    end    0x00DC

table 1 objects:
    offset 0x00DC
    count  0

table 2 zones:
    offset 0x00DC
    count  0

table 3 object definitions:
    offset 0x00DC
    count  0

table 4 character definitions:
    offset 0x00DC
    count  2
    fixed end:
        0x0304

variable character text:
    0x0304 .. 0x03EB
    size 0xE8

table 5 addresses:
    offset 0x03EC
    count  1
    size   0x10
    end    0x03FC

table 7 area links:
    offset 0x03FC
    count  0

AREA bytecode:
    0x03FC .. 0x051B
    size 0x120

table 6 cameras:
    offset 0x051C
    count  27
    size   27 * 0x2C = 0x4A4
    end    0x09C0
```

This one record captures nearly every important AREA architectural concept.

---

# 74. Area 118 fixed header values

```text
+0x00 runtime/context:
    0

+0x04 primary event:
    0x03FC

+0x08..+0x27:
    all -1

table offsets:
    0: 0x00B4
    1: 0x00DC
    2: 0x00DC
    3: 0x00DC
    4: 0x00DC
    5: 0x03EC
    6: 0x051C
    7: 0x03FC

table counts:
    0: 2
    1: 0
    2: 0
    3: 0
    4: 2
    5: 1
    6: 27
    7: 0

dependencies:
    model  = GRID
    SCX    = GRID
```

---

# 75. Area 0 structural scale

Anekbah (AREA 0) demonstrates a large world record.

Representative counts:

```text
table 0 characters:
    28

table 1 objects:
    1

table 2 zones:
    131

table 3 object definitions:
    1

table 4 character definitions:
    28

table 5 addresses:
    34

table 6 cameras:
    151

table 7 area links:
    4
```

This makes clear that AREA is not merely a startup-script file; it is a broad
world metadata container.

---

# 76. Character definition fixed strings

Current strongly identified fixed strings in table 4:

```text
+0x08  char[32] display/name
+0x28  char[32] role/profession
+0x48  char[18] adventure control/profile resource
+0x5A  char[18] combat control/profile resource
+0x90  char[10] model resource
```

The game contains matching control files such as:

```text
H1Avnt.CTL
H1Cmbt.CTL
F1Avnt.CTL
F1Cmbt.CTL
```

which supports the interpretation of the `+0x48/+0x5A` strings.

Exact source-level names remain unrecovered.

---

# 77. Table 4 unresolved fields

Most of the `0x114` character-definition core still contains authored stats,
flags, selectors, and numeric parameters.

Do not aggressively name these based on one character.

Recommended documentation style:

```text
known string / ID offsets named
unknown numeric regions preserved
Runtime xrefs used before semantic naming
```

This is preferable to importing speculative RPG-stat names from visual
patterns.

---

# 78. Object-definition unresolved fields

Likewise, table 3 clearly contains:

```text
object ID
type/flag high word
five additional u16 fields
10-byte model/resource name
```

but the intermediate words still need consumer xrefs.

Useful future targets include:

- object pickup/use logic;
- object activation opcode `0x5C`;
- inventory conversion;
- persistence/state-bit handling;
- model/material selection.

---

# 79. Zone structure follow-up

Table 2's major structure is already visible:

```text
event pointer
metadata
four 3D points
zone ID/flags
```

The highest-value remaining work is to identify:

```text
+0x04
+0x08
+0x3C
+0x3E
+0x40 high-bit semantics
+0x42
```

and how the quadrilateral is tested at runtime.

The event pointer itself is already established.

---

# 80. Area-link follow-up

Table 7 is structurally simple:

```text
eventOffset
targetAreaId
```

Remaining questions:

- what event type causes each entry to be selected;
- whether the event runs before or after area transition;
- whether a zero event offset means direct transition;
- how this interacts with `IAM/START`'s initial/linked-area mapping;
- whether target-area transitions can also be initiated indirectly through
  address/zone records.

---

# 81. Primary event follow-up

Header `+0x04` should be renamed away from `scriptOffset`.

Likely final name family:

```text
primaryEventOffset
startupEventOffset
defaultEventOffset
```

The exact original event name requires more Runtime call-path work.

Important established facts are sufficient now:

```text
0 means no primary entry
nonzero means pointer/offset into shared AREA bytecode
it is not the start of all AREA bytecode
```

---

# 82. Current OpenNomad parser status

`IamAreaRecord` now models the recovered structure directly:

```text
k_offset_primary_event = +0x04
bytecode_pool()         = [table7 end, table6 start)
table 3 stride          = 0x18
header +0x08 fields     = unresolved/neutral
```

Nonzero primary, zone, and table-7 event offsets are checked against the
bounded compact bytecode pool. A zero primary event remains legal when other
entrypoints or bytecode exist. Camera records and trailing serialized data are
never exposed as AREA VM instructions.

---

# 83. Current OpenNomad semantic coverage

Currently implemented typed AREA access includes:

```text
table 0:
    character placements

table 4:
    selected character-definition fields

table 6:
    cameras
```

The format evidence now supports extending that to:

```text
table 1:
    object placements

table 2:
    zone records

table 3:
    object definitions

table 5:
    addresses

table 7:
    area links

bytecode pool:
    bounded independently from primary event offset
```

---

# 84. Recommended parser model

A stronger immutable representation would separate:

```cpp
struct IamAreaRecord {
    AreaHeader header;

    std::vector<CharacterPlacement> characters;
    std::vector<ObjectPlacement> objects;
    std::vector<ZoneRecord> zones;
    std::vector<ObjectDefinition> objectDefinitions;
    std::vector<CharacterDefinition> characterDefinitions;
    std::vector<AddressRecord> addresses;
    std::vector<AreaLinkRecord> areaLinks;
    std::vector<CameraRecord> cameras;

    std::span<const std::byte> bytecodePool;
};
```

Character biography/personality strings can be decoded through table-4 offsets
while the raw bytes remain owned by the record.

---

# 85. Runtime event representation

OpenNomad now represents AREA execution conceptually as:

```cpp
AreaBytecodePool pool{
    bytes = area.bytecode_pool()
};

AreaEventContext ctx{
    pool,
    entryOffset = area.primary_event_offset()
};
```

Every `AreaScriptRuntime` receives the complete bounded bytecode pool, never a
whole AREA record or only the tail beginning at one entrypoint. Serialized
record-relative primary and zone entries are validated against the pool before
subtracting its record origin. Zero maps to no entry. Zone/link events can
therefore reuse the same immutable pool with different VM-local entrypoints,
and diagnostics recover record-relative offsets by adding the program origin.

This mirrors the serialized architecture far more closely.

---

# 86. Recommended offset APIs

Useful immutable APIs:

```text
bytecode_offset()
bytecode_size()
bytecode_bytes()

primary_event_offset()

zone.event_offset
area_link.event_offset
```

Validation helper:

```text
is_valid_event_offset(offset):
    offset == 0
    or
    bytecodeStart <= offset < bytecodeEnd
```

All retail nonzero event pointers satisfy this.

---

# 87. Recommended table-4 text APIs

```text
character.biography()
character.personality()
```

should resolve:

```text
record-relative offsets
```

and require NUL termination before:

```text
table5Offset
```

Do not assume fixed string sizes for these two variable texts.

---

# 88. Recommended archive validation

Archive-level checks:

1. index entry fits in archive;
2. zero size means absent record;
3. `offset + size` fits;
4. retail-compatible diagnostic:
   - record offset is `0x800` aligned;
5. overlapping populated record spans should be rejected/reported;
6. index pages are not treated as record data.

---

# 89. Recommended AREA-record validation

Minimum checks:

1. record size >= `0xB4`;
2. all eight table offsets <= record size;
3. all fixed table spans fit;
4. table 3 stride = `0x18`;
5. table-0/table-4 counts agree when enforcing known retail structure;
6. table-1/table-3 counts agree;
7. pairwise character IDs agree;
8. pairwise object IDs agree;
9. table4 text offsets are zero or in the variable text region;
10. table 5 records fit;
11. table 7 records fit;
12. bytecode start <= table6 offset;
13. every nonzero known event entry lies in bytecode;
14. camera table ends at or before record size;
15. retail strict mode can require camera table end == record size.

---

# 90. Recommended corpus diagnostics

For every AREA record, dump:

```text
area ID / AREAS.TAG name
archive offset
record size

primary event offset

dependency names

for table 0..7:
    offset
    count
    stride
    end

derived:
    fixed character-def end
    variable-text span
    bytecode span
```

Then summarize:

```text
characters
objects
zones
character descriptions
addresses
links
cameras
bytecode bytes
```

This will make cross-area reverse engineering dramatically easier.

---

# 91. Suggested character dump

For table 0 / table 4 pair:

```text
character ID
state bit
serialized XYZ
orientation

display name
role/profession
model
adventure control
combat control

biography
personality/preferences
```

This is enough to turn AREA into a very useful world/entity inspection tool
without pretending the remaining table-4 numeric fields are known.

---

# 92. Suggested object dump

For table 1 / table 3 pair:

```text
OBJECTS.TAG ID/name
serialized XYZ
orientation
unresolved placement tail

type/flags
unresolved definition words
model resource
```

This can directly support the future implementation of AREA opcode `0x5C`.

---

# 93. Suggested zone dump

For table 2:

```text
record index
zone ID / flags
ZONES.TAG name when available

event offset
event-relative bytecode preview

four serialized points

unknown fields
```

A spatial debug overlay of those four-point zones would be particularly useful
for validating the geometry interpretation.

---

# 94. Suggested address dump

For table 5:

```text
address ID
ADDRESSES.TAG name
XYZ
orientation
```

Because the TAG/AREA ID set matches exactly, this is one of the safest tables to
expose immediately in tooling.

---

# 95. Suggested area-link dump

For table 7:

```text
target AREA ID
AREAS.TAG name
event offset
bytecode preview
```

This can form the basis of an AREA transition graph.

A graph generated from all table-7 records would be useful for both reverse
engineering and game-navigation debugging.

---

# 96. Suggested camera dump

For table 6:

```text
camera ID
CAMERAS.TAG name when available

eye
target

type
roll
horizontal FOV

field20
field22
tail fields
```

OpenNomad already has enough coordinate math to visualize these cameras.

---

# 97. Relationship to TAG files

TAG files are external editor/debug metadata mapping numeric IDs to names.

They do not define binary AREA structure.

Useful correlations:

```text
AREAS.TAG
    <-> archive record ID
    <-> table 7 targetAreaId

OBJECTS.TAG
    <-> table 1 objectId
    <-> table 3 objectId

ADDRESSES.TAG
    <-> table 5 addressId

ZONES.TAG
    <-> table 2 zone identity subset

CAMERAS.TAG
    <-> table 6 cameraId subset

VARIABLES.TAG
    <-> AREA VM global-variable IDs
```

The TAG files can be incomplete relative to runtime-internal/generated IDs.

A missing TAG name must not make binary parsing fail.

---

# 98. AREA and persistent state

Character and object placements contain fields that Runtime uses to coordinate
persistent presence/activation state.

For table 0, `stateBitIndex` is directly recovered.

The save/persistence layer therefore participates in decisions such as:

```text
is entity active?
is entity present in current AREA?
has an authored one-shot state already happened?
```

Keep persistent state outside immutable AREA data in OpenNomad.

---

# 99. AREA and world dependencies

One AREA record chooses several resources.

Conceptually:

```text
AREA
    |
    +-- decor 3DO
    +-- SCX resource/script bank
    +-- optional MPT map
    +-- optional OPT path/optimization data
    +-- optional ANI animation bank
    +-- optional sky 3DO
```

This reinforces an important project architecture point:

> An AREA is the world/scenario **manifest and event context**, not the entire
> world asset itself.

---

# 100. AREA startup role

Retail startup follows:

```text
IAM/START
    |
    +-- initial AREA ID 118
    |
    v
IAM/AREA record 118
    |
    +-- GRID decor
    +-- GRID.SCX
    +-- AREA event context
    |
    v
primary/default event
    |
    v
OpenInterface 29
    |
    v
main menu
```

The same machinery later supports ordinary world areas.

The main menu's AREA record is therefore a useful compact sample, not a
special-case format variant.

---

# 101. Documentation-oriented header

```c
#pragma pack(push, 1)

typedef struct SerializedAreaHeader {
    uint32_t runtimeContext;        // +0x00
    uint32_t primaryEventOffset;    // +0x04, 0 or record-relative bytecode entry

    uint32_t unknown08[8];          // +0x08..+0x27

    uint32_t tableOffset[8];        // +0x28..+0x47
    uint16_t tableCount[8];         // +0x48..+0x57

    char model3doName[9];           // +0x58
    char scenarioScxName[9];        // +0x61
    char mapMptName[9];             // +0x6A
    char optionsOptName[9];         // +0x73
    char animationAniName[9];       // +0x7C
    char sky3doName[9];             // +0x85

    uint8_t unknown8E[0x26];        // +0x8E..+0xB3
} SerializedAreaHeader;             // 0xB4

#pragma pack(pop)
```

Names are descriptive reconstructions, not original source declarations.

---

# 102. Documentation-oriented table structures

```c
#pragma pack(push, 1)

typedef struct AreaCharacterPlacement {
    int16_t runtimeOrSlot;
    int16_t characterId;
    int32_t x;
    int32_t y;
    int32_t z;
    int16_t orientationUnits;
    uint16_t stateBitIndex;
} AreaCharacterPlacement; // 0x14

typedef struct AreaObjectPlacement {
    int16_t runtimeOrSlot;
    uint16_t objectId;
    int32_t x;
    int32_t y;
    int32_t z;
    int16_t orientationUnits;
    uint16_t unknown12;
    uint16_t unknown14;
    uint16_t unknown16;
} AreaObjectPlacement; // 0x18

typedef struct AreaObjectDefinition {
    uint16_t objectId;
    uint16_t typeOrFlags;
    uint16_t unknown04;
    uint16_t unknown06;
    uint16_t unknown08;
    uint16_t unknown0A;
    uint16_t unknown0C;
    char modelResource[10];
} AreaObjectDefinition; // 0x18

typedef struct AreaAddressRecord {
    int32_t x;
    int32_t y;
    int32_t z;
    int16_t orientationUnits;
    uint16_t addressId;
} AreaAddressRecord; // 0x10

typedef struct AreaLinkRecord {
    uint32_t eventOffset;
    uint32_t targetAreaId;
} AreaLinkRecord; // 0x08

#pragma pack(pop)
```

Zone, character-definition, and camera structures are best kept sparse until all
remaining fields are identified.

---

# 103. Compact format reference

```text
IAM/AREA archive
================

index:
    page size  0x800
    entry size 0x08
    256 entries/page

entry:
    u32 absoluteOffset
    u32 recordSize

retail AREA:
    IDs 0..258
    259 records
    record offsets 0x800 aligned
```

AREA record:

```text
+0x000  fixed header 0xB4

table 0  characters       0x14 each
table 1  objects          0x18 each
table 2  zones            0x44 each
table 3  object defs      0x18 each
table 4  character defs   0x114 each

optional table-4 variable text

table 5  addresses        0x10 each
table 7  area links       0x08 each

shared AREA bytecode

table 6  cameras          0x2C each

record end
```

---

# 104. Compact bytecode-boundary reference

```text
bytecodeStart =
    tableOffset[7]
    + tableCount[7] * 0x08

bytecodeEnd =
    tableOffset[6]

bytecodeSize =
    bytecodeEnd - bytecodeStart
```

Known event entrypoints:

```text
header +0x04
table 2 +0x00
table 7 +0x00
```

Valid nonzero entries are:

```text
bytecodeStart <= entry < bytecodeEnd
```

---

# 105. Compact entity relationships

```text
TABLE 0 CHARACTER PLACEMENT
    characterId
        |
        +----------------------+
                               |
                               v
TABLE 4 CHARACTER DEFINITION
    +0x110 characterId


TABLE 1 OBJECT PLACEMENT
    +0x02 objectId
        |
        +----------------------+
                               |
                               v
TABLE 3 OBJECT DEFINITION
    +0x00 low16 objectId
```

Retail invariants:

```text
table0Count == table4Count
table1Count == table3Count
```

Pairwise IDs match in every supplied retail record.

---

# 106. Implementation consequences of the recovered format

OpenNomad now uses the bounded bytecode pool, neutral header naming, recovered
table strides, and typed access for object, zone, address, area-link, camera,
and character data. These are consequences of the recovered format, not
changes to the serialized layout. Remaining unknown numeric fields stay
opaque rather than receiving speculative semantics.

---

# 107. Highest-value remaining reverse engineering

## 107.1 Header `+0x08..+0x27`

Find Runtime consumers for the eight mostly-`-1` dwords.

## 107.2 Header tail `+0x8E..+0xB3`

Map authored world/scenario configuration fields.

## 107.3 Table 0 `+0x00`

Determine whether the frequent `-1` is:

- runtime character slot;
- attachment;
- parent;
- cache index;
- another editor/runtime field.

## 107.4 Table 1 tail

Recover `+0x12/+0x14/+0x16` semantics through object activation/persistence
paths.

## 107.5 Table 2 zone fields

Map geometry type, flags, zone identity bits, and event trigger rules.

## 107.6 Table 3 object flags

Recover the high word of `+0x00` and five intermediate words.

## 107.7 Table 4 remaining character fields

The fixed record contains substantial gameplay/animation configuration beyond
the currently named strings.

## 107.8 Table 6 camera tail

Resolve camera attachment/type-specific behavior after `+0x1E`.

## 107.9 Event selection

Fully identify the Runtime data structures/functions that choose:

- primary event;
- zone event;
- link event;
- other possible event offsets.

---

# 108. Current OpenNomad source locations

Archive:

```text
src/core/Core/Omikron/IamArchive.hpp
src/core/Core/Omikron/IamArchive.cpp
```

AREA parser:

```text
src/core/Core/Omikron/IamArea.hpp
src/core/Core/Omikron/IamArea.cpp
```

AREA VM:

```text
src/core/Core/Script/AreaScriptOpcode.hpp
src/core/Core/Script/AreaScriptRuntime.hpp
src/core/Core/Script/AreaScriptRuntime.cpp
```

Startup/world consumers:

```text
src/core/Core/Scenario/ScenarioStartupController.hpp
src/core/Core/Scenario/ScenarioStartupController.cpp
src/core/Core/Scenario/ScenarioRuntime.*
src/core/Core/Character/CharacterRuntime.*
```

Tests:

```text
src/core/Tests/IamArea.spec.cpp
src/core/Tests/AreaScriptRuntime.spec.cpp
src/core/Tests/ScenarioRuntime.spec.cpp
```

---

# 109. Retail metadata used for cross-checking

Supplied TAG metadata:

```text
AREAS.TAG
OBJECTS.TAG
CAMERAS.TAG
ZONES.TAG
ADDRESSES.TAG
VARIABLES.TAG
```

Especially strong matches:

```text
AREAS.TAG:
    259 entries, IDs 0..258
    matches populated AREA archive IDs

ADDRESSES.TAG:
    791 entries, IDs 0..790
    exactly matches all table-5 IDs

OBJECTS.TAG:
    every table-1/table-3 object ID examined is a valid OBJECTS.TAG identity
```

TAG files are naming aids, not serialized AREA tables themselves.

---

# 110. Boundary of current knowledge

The broad AREA architecture is now well established.

We know:

```text
how the archive indexes AREA IDs
how records are aligned
the fixed header size
all eight table strides
the physical table order
the shared bytecode boundaries
three independent event-entrypoint sources
character placement/definition pairing
object placement/definition pairing
address records
area links
camera records
variable character biography/personality data
resource dependency names
```

The largest remaining gaps are therefore **field semantics inside known
structures**, not fundamental container layout.

The most important conceptual correction is:

> An AREA record does not contain one monolithic script beginning at header
> `+0x04`. It contains a **shared event bytecode pool** with multiple
> record-relative entrypoints selected by the AREA context, zones, and area
> links.

---

# 111. OpenNomad two-slot transition mapping (`AREA 0x2F`)

Compact AREA opcode `0x2F` uses handler `0x00402D20` and consumes three
Scalar16 operands (six bytes). Operand 0 is an `AREAS` ID. On an accepted
transition, the calling context advances past the instruction and waits in
recovered Runtime state 10 until the session coordinator completes the
destination handoff.

OpenNomad maps Runtime's two resident AREA slots directly onto:

```text
ScenarioStartupController::RuntimeAreaSlot[2]
ScenarioManager::WorldSceneContext[2]
```

The coordinator preserves the active source while parsing the destination
record and preparing its authored decor/SCX dependencies. Preparation leaves
the destination `LoadedInactive` and source `LoadedActive`; it does not change
presentation ownership. The old AREA bytecode context then resumes from the IP
already following `0x2F`; the destination's primary event is not substituted
for that handoff.

The concrete New Game transition is:

```text
AREA 118 Introduction Kay'l
  +0x10D  0x2F (222, -1, -1)
  -> AREA 222 Anekbah Impasse
     model3doName = AIMPASSE
     scenarioScxName = IMPASSE
     sky3doName = ASKY (preserved diagnostically; no sky renderer yet)
```

The following authored operations complete the handoff:

```text
+0x114  0x47 (222, 55)  attach/replace the resident AREA's SCENE and commit it active
+0x119  0x49 (654)      place the already-established controlled character at a resident AREA address
+0x11C  0x30 (118)      release the requested inactive source AREA
+0x11F  0x03 EndEvent
```

Opcode `0x2F` does not activate scene 55, position the player at address 654,
release AREA 118, or skip any of those subsequent instructions. Successful
`0x47` is the presentation commit: source becomes `LoadedInactive`, the
destination becomes `LoadedActive`, and the source remains resident until its
explicit `0x30` release. If compact IAM has selected a current body with `0x38`,
`0x47` first transfers that single live body to the destination world. Address
lookup for `0x49` still scans both resident AREA table-5 collections, then
applies the resolved address only to the selected body's recorded owner world;
it never guesses a current character when no `0x38` selection exists.

The native consumer confirms table-5 XYZ uses the ordinary AREA positional
normalization. Address Y denotes the body/floor contact coordinate, and
`0x0041BF50` derives logical actor Y by subtracting the maximum raw authored
collision-sphere bottom (`center.y + radius`). It preserves principal Z and the
representative visual object's actor-relative offset while replacing X/Yaw and
the authoritative physical position.
