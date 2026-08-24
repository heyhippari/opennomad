# Omikron IAM `SCENE` archive and attached-scene lifecycle

> **Status:** recovered serialized format and implemented OpenNomad support.  
> **Last updated:** 2026-08-24

`IAM/SCENE` uses the same paged indexed-IAM archive as `IAM/AREA`: `0x800`-byte
index pages with little-endian `{ uint32 offset, uint32 size }` entries. The
retail archive has 71 populated records (`0..70`); explicit index values, not
record alignment, define every record's bounds.

## Record layout

Every record has a little-endian `0x44`-byte header: a runtime-context
placeholder at `+0x00`, top-level script offset at `+0x04`, eight `uint32`
table offsets at `+0x08`, eight signed `int16` table counts at `+0x28`, and a
12-byte reserved/runtime tail at `+0x38`.

Counts are signed; negative counts are malformed. Table 5 is empty in the
supported format (`offset = 0`, `count = 0`). Pointer-shaped fields are kept as
immutable serialized offsets, never relocated into process pointers.

Physical order is: header; tables 0, 1, 2, 3, and 4; optional table-4 strings;
table 7; top-level compact bytecode when present; table-6 cameras; EOF. It is
not numeric table order.

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

The top-level script is exactly `[script_offset, table6_offset)`. Camera bytes
must never be interpreted as compact VM code.

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
three neutral `event_offsets[0..2]` program offsets at `+0x00/+0x04/+0x08`,
opaque bytes `+0x0C..+0x3D`, signed `field_3e` at `+0x3E`, signed authored
zone ID at `+0x40`, and two opaque tail bytes. The trigger meanings of the
three offsets are not named or fired by parsing/residency synchronization.
Table 7 is `{ uint32 program_offset, int32 field_04 }` with intentionally
unresolved higher-level semantics. Table 6 reuses the checked AREA camera
parser.

## Compact context and two-slot residency

SCENE uses the same compact IAM interpreter as AREA; OpenNomad retains the
historical `AreaScriptRuntime` class name for this shared context. Attaching a
scene creates a separate context over the SCENE script span, queues event 1,
and activates it for normal scheduler service. It is never run recursively by
the attach opcode, and an unsupported SCENE opcode pauses only that context.

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
only records whose START-backed ZONE bit is enabled. This is not yet collision
or zone-event execution.

Related: [`iam-area.md`](iam-area.md), [`iam-scenario-vm.md`](iam-scenario-vm.md),
and [`startup-sequence.md`](startup-sequence.md).
