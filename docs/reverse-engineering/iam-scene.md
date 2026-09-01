# Omikron IAM `SCENE` archive and attached-scene lifecycle

SCENE table 0 has the same mutable runtime placement semantics as AREA table 0:
`+0x00` is an actor-slot seed on disk and becomes an OpenNomad `BodyIdentity`
binding in `CharacterReferenceRuntime`; `+0x02` becomes a mutable character
reference. Detaching a SCENE removes only its overlay entries.

> **Status:** recovered serialized format and implemented OpenNomad support.  
> **Last updated:** 2026-08-29

`IAM/SCENE` uses the same paged indexed-IAM archive as `IAM/AREA`: `0x800`-byte
index pages with little-endian `{ uint32 offset, uint32 size }` entries. The
retail archive has 71 populated records (`0..70`); explicit index values, not
record alignment, define every record's bounds.

## Record layout

Every record has a little-endian `0x44`-byte header: a runtime-context
placeholder at `+0x00`, primary/default compact-event entrypoint at `+0x04`,
eight `uint32` table offsets at `+0x08`, eight signed `int16` table counts at
`+0x28`, and a 12-byte reserved/runtime tail at `+0x38`.

Counts are signed; negative counts are malformed. Table 5 is empty in the
supported format (`offset = 0`, `count = 0`). Pointer-shaped fields are kept as
immutable serialized offsets, never relocated into process pointers.

Physical order is: header; tables 0, 1, 2, 3, and 4; optional table-4 strings;
table 7; the shared compact bytecode pool; table-6 cameras; EOF. It is not
numeric table order.

| Table | Stride | Representation |
| --- | ---: | --- |
| 0 | `0x14` | character placements |
| 1 | `0x18` | object placements |
| 2 | `0x44` | neutral zone/context records |
| 3 | `0x18` | object definitions |
| 4 | `0x114` | character definitions |
| 5 | — | empty/unsupported |
| 6 | `0x2C` | shared `IamCameraRecord` |
| 7 | `0x08` | neutral script-link records |

The compact bytecode pool is exactly `[end(table7), table6_offset)`. Header
`+0x04` is one record-relative entrypoint into that pool, not its start. A zero
primary entry does not imply an empty pool. Camera bytes must never be
interpreted as compact VM code.

## Runtime pointer-relocation evidence

These addresses refer to Runtime.exe SHA-256
`55f7120bfea7891b048c64e3682f3259cdbf2719a43fa24e42254b753c95d2ef`.

The retail SCENE loader begins around `0x0040C120`. It independently relocates
every nonzero record-relative compact pointer against the SCENE record base:

- header `+0x04` around `0x0040C1A0..0x0040C1AA`;
- each table-2 zone entry at `+0x00/+0x04/+0x08`;
- each table-7 program dword around `0x0040C49C..0x0040C4B7`.

The primary context registration path around `0x0040BFB0` passes the relocated
AREA or SCENE `+0x04` entry to the common compact-context constructor at
`0x00406290`, then queues event 1 through `0x004063D0`. These independent
relocations establish that primary, zone, and table-7 pointers are peer
record-relative entries into one shared bounded pool.

## Recovered table fields

Table 0 matches the AREA placement family: runtime slot seed, character ID,
serialized XYZ, orientation units, and persistent-state bit index. Table 4 is
matched by signed character ID at `+0x110`; the parser exposes its runtime seed
at `+0x10E`, NUL-padded name/model fields, and validates optional
record-relative NUL strings at `+0x00/+0x04`. Other bytes remain opaque.

Table 1 preserves object ID, authored position, three angle components, and a
neutral final persistent-state field. Table 3 preserves object ID plus an
opaque tail. This is a data seam, not a guessed generic object runtime.

Table 2 uses the shared immutable `IamZoneRecord` representation with AREA:
neutral `event1/event2/event3` program offsets at `+0x00/+0x04/+0x08`; four
XYZ vertices at `+0x0C..+0x38`; signed orientation center/span at
`+0x3C/+0x3E`; signed authored zone ID at `+0x40`; and unknown signed
`+0x42`. Contact uses the four-point X/Z polygon and a separately wrapped
orientation filter. Event 1 is the confirmed first qualifying-contact entry;
event 2 remains neutral.
Table 7 is `{ uint32 program_offset, int32 field_04 }` with intentionally
unresolved higher-level semantics. Table 6 reuses the checked AREA camera
parser.

A SCENE camera command does not prefer its own table 6. Compact camera
definitions use Runtime's fixed slot-ordered AREA/SCENE namespace followed by
`IAM/GLOBAL`, while the SCENE context remains the presentation/wait owner. See
[`iam-global.md`](iam-global.md).

## Compact context and two-slot residency

SCENE uses the same compact IAM interpreter as AREA; OpenNomad retains the
historical `AreaScriptRuntime` class name for this shared context. Attaching a
scene with a nonzero primary event creates a separate context over the complete
bounded SCENE bytecode pool, installs the rebased primary entry as event 1, and
activates it for normal scheduler service. It is never run recursively by the
attach opcode, and an unsupported SCENE opcode pauses only that context.

Primary and zone contexts can see only `bytecode_pool()`:
`[end(table7), table6_offset)`. OpenNomad preserves serialized offsets in the
parsed record, validates each nonzero entry against that pool, then subtracts
`bytecode_pool_offset()` through `CompactProgramView`. Another zone or table-7
entry may therefore precede the primary event. No context uses the complete
SCENE record as VM storage, so table-6 cameras cannot execute as compact
instructions.

OpenNomad has two resident AREA slots. Each owns an AREA plus optional attached
SCENE record/ID and SCENE compact context. `secondary_area_id` remains the
START linked-AREA relationship; it is not a SCENE ID.

`AttachAreaScene(areaId, sceneId)` (`0x47`) replaces any old SCENE context
before dematerializing its scene-local characters, loads/parses the new record,
materializes only SCENE table-0 characters against SCENE-first table-4
definitions, queues event 1, and sets `areaMapping[areaId] = sceneId`. A
zero-object SCENE succeeds; nonempty scene-object materialization remains an
explicit compatibility gap.

When the durable selected body belongs to the source world, the same `0x47`
handoff moves its complete `RuntimeCharacter` into the prepared destination
world before activation. The transfer preserves transform, animation, pose,
overlays, runtime objects, and immutable shared model resource; it creates no
second logical owner and does not invoke the target model loader. SCENE
replacement/dematerialization similarly leaves a selected body live.

`0x2F` prepares the alternate AREA/world while it stays inactive. Successful
`0x47` is the presentation commit: the prepared destination becomes active and
the source remains loaded/inactive until `0x30` explicitly releases it. Once a
selected source body has transferred, that release can safely unload the source
without invalidating session selection.

Attached-SCENE table-2 records participate in the coordinator's transient
active-zone registry. It is rebuilt, not incrementally deduplicated, whenever
resident AREA/SCENE data or persistent ZONE enablement changes; it contains
only records whose START-backed ZONE bit is enabled. A first qualifying contact
of the session current controlled character creates one compact zone context
over the complete bounded SCENE bytecode pool and queues event 1 once. Event 2 is not
synthesized per frame. A persistent disable stops future contacts but does not
destroy a currently executing context; physical record residency removal does.

Related: [`iam-area.md`](iam-area.md), [`iam-scenario-vm.md`](iam-scenario-vm.md),
and [`startup-sequence.md`](startup-sequence.md).
