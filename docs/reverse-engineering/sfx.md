# Retail SFX companions

This document records the supplied Runtime and retail-data results used by
OpenNomad's `.SFX` implementation. It does not extend the reverse engineering
beyond those results.

## Scenario companion loading

**Confirmed — Runtime.** After loading a scenario SCX, Runtime attempts to load
the same stem with an `.SFX` extension, then parses and links it. Missing SFX is
valid. An existing malformed SFX is a scenario-load error. OpenNomad performs
this generically in `ScenarioManager`, through the case-insensitive game-data
resolver, for gameplay-mode and world-context packages.

The immutable file representation is `Omikron::SfxData`. Mutable emitters,
requests, particles, RNG, and timing are owned by the scenario's `Sfx::Runtime`.
Particles use the scenario's ordinary `SpritePool`; presentation remains in the
existing sprite renderer.

## File grammar

**Confirmed — Runtime and data.** All integers and floats are little-endian.
The magic is `0x56302E35`, serialized as `35 2E 30 56` (`5.0V`). Runtime stores
section counts in DWORDs but reads only their low byte:

```text
u32 magic
u32 rawCountA;          byte[0x28] recordsA[rawCountA & 0xff]
u32 rawCountB;          byte[0x2c] recordsB[rawCountB & 0xff]
u32 rawDefinitionCount; SfxDefinition[rawDefinitionCount & 0xff]

if bytes remain:
  u32 rawSectionDCount; byte[0x10] sectionD[rawSectionDCount & 0xff]
  u32 rawNodeCount;     SfxNode[rawNodeCount & 0xff]
  u32 rawTrackCount
  repeated tracks:
    SfxTrackHeader      // 0x10
    SfxTrackPoint[]     // pointCount * 0x24
```

Checked reads reject truncation, count/size overflow, track-point overflow,
variable tracks extending beyond the buffer, and trailing bytes.

- The `0x28` records are **Unknown**: parsed and preserved only.
- The `0x2C` records are **Confirmed — Runtime** animation/Cin-SFX records.
  OpenNomad stores them as typed `SfxCinAnimationRecord` values:

  | Offset | Type | Field |
  |---:|---|---|
  | `00` | u32 | association ID |
  | `04` | u32 | animation lookup raw value; its low WORD is the animation ID |
  | `08` | u32 | flags |
  | `0C` | i32 | channel 1 definition ID |
  | `10` | f32 | channel 1 start |
  | `14` | f32 | channel 1 end |
  | `18` | i32 | channel 1 model-object reference |
  | `1C` | i32 | channel 2 definition ID |
  | `20` | f32 | channel 2 start |
  | `24` | f32 | channel 2 end |
  | `28` | i32 | channel 2 model-object reference |

  Bit `0x80` participates in animation association; bits `0x08` and `0x10`
  independently enable channels 1 and 2. Other flag bits remain neutral.
  Association matches `uint16(animation_lookup_raw)` to
  `uint16(ScxAnimationRecord.animation_id)`, retaining the complete raw DWORD.
- The `0x10` section-D records are **Unknown**: parsed and preserved only.

## Definition layout (`0x50`)

**Confirmed — Runtime.** Offsets are from the record start.

| Offset | Type | Field |
|---:|---|---|
| `00` | i32 | definition ID |
| `04` | i32 | sound ID |
| `08` | u32 | raw sprite ID; low WORD is the authored SCX sprite ID |
| `0C` | u32 | flags |
| `10..18` | 3 f32 | direction XYZ |
| `1C` | f32 | vertical acceleration |
| `20` | f32 | lifetime |
| `24` | f32 | sound delay |
| `28` | f32 | emission delay |
| `2C` | f32 | preserved raw value |
| `30` | u32 | start color `0x00RRGGBB` |
| `34` | u32 | end color `0x00RRGGBB` |
| `38` | f32 | initial scale |
| `3C` | f32 | cone angle in degrees |
| `40` | f32 | angular velocity in degrees per logical tick |
| `44` | i16 | spawn count |
| `46` | char[8] | name |
| `4E` | u8 | sprite render mode `0..8` |
| `4F` | u8 | preserved raw value |

The authored sprite ID is resolved by equality with `ScxData::sprites[].sprite_id`,
never by treating it as a vector index. The definition render-mode byte is
copied to `SpriteInstance::render_mode`; modes are the existing retail-compatible
`SpriteRenderMode` table. `type` is synchronized for layout diagnostics.

### Definition flags

**Confirmed — Runtime**, except where explicitly marked unsupported:

- `0x0002`: selects a special `SpriteInstance+0x24` path. GRID does not use it.
  Without it, particles set the confirmed `diffuse_alpha` field to exactly
  `0.5`. The behavior selected by the flag itself remains unsupported.
- `0x0004`: grow, with scale velocity `+initialScale/lifetime`.
- `0x0010`: random initial billboard rotation in `[0, 2pi]`.
- `0x0040`: independently add `2*random01` to each direction component.
- `0x0080`: native spawn-count randomization path; preserved, unused by GRID,
  and unsupported.
- `0x0100`: add up to ten percent of base lifetime.
- `flags & 0x0600 == 0`: negative authored vertical acceleration.
- `flags & 0x0600 == 0x0200`: positive authored vertical acceleration.
- Other `0x0600` values are preserved and unsupported.
- `0x1000`: add `[0, baseCone]` to the cone angle.
- `0x2000`: shrink with `-initialScale/lifetime`; grow has priority.

Unknown flag bits remain unmodified.

## Nodes and activation

**Confirmed — Runtime and data.** A node record is `0x4C`:

| Offset | Type | Field |
|---:|---|---|
| `00` | i32 | node ID |
| `04` | char[4] | label |
| `08`, `0C` | i32 | trigger type, trigger ID |
| `10` | i32 | track ID |
| `14`, `18` | u32 | stale serialized track/point pointers |
| `1C..24` | 3 f32 | serialized runtime XYZ residue |
| `28`, `2C` | i32 | anchor reference type, ID |
| `30` | u32 | stale serialized anchor pointer |
| `34` | i32 | fixed definition ID |
| `38` | f32 | startup delay |
| `3C` | f32 | serialized elapsed residue |
| `40` | i32 | repeat limit |
| `44` | i32 | serialized repeat-index residue |
| `48` | u32 | flags |

Pointer-shaped fields are never dereferenced. IDs are linked against the current
parsed package.

Node flags are: `0x01` live active, `0x04` authored initial reverse, `0x08`
mutable current reverse, `0x10` ping-pong toggle, and `0x20` live startup-delay
phase. Activation resolves a non-empty track, recursively activates type-1 node
references, resets elapsed, overwrites current reverse from authored `0x04`
(serialized `0x08` is residue), sets repeat index to one, establishes delay,
recalculates duration, selects point zero or `max(count-2,0)`, and evaluates the
initial position.

After linking, nodes with trigger `(1,-1)` activate automatically. Every
successful explicit structured-SCX launch calls `trigger(0, sourceScriptId)`
once, including character-bound launches. Failed launches and script-instance
repetition do not trigger SFX.

During timing, elapsed advances by one per logical step. Delay expiry clears
the delay and resets elapsed without emitting on that step. At traversal end,
the node deactivates when its finite repeat limit is exhausted; `999` is the
infinite sentinel and never increments the repeat index. Ping-pong toggles
current reverse between traversals.

## Tracks, interpolation, and definitions

**Confirmed — Runtime.** A track header contains i32 ID, four-byte label, u32
point count, and f32 mutable-duration seed. Each `0x24` point contains i32 point
ID, i32 definition ID, XYZ floats, segment duration, i32 reference type and ID,
and a stale u32 serialized reference pointer.

Tracks interpolate XYZ linearly, not with 3DP Hermite interpolation. Forward
traversal selects low-to-high segments; reverse selects high-to-low segments.
Segment time is cumulative and `t=localTime/duration` (or zero for a zero
duration). A one-point track evaluates the point directly. Fixed positive node
definition IDs override track definitions; otherwise forward uses the current
segment point and reverse uses the preceding serialized point for that segment.

## References

**Confirmed — Runtime.** Node anchors and track points share a namespace:

- Type 0: no reference; point XYZ is already world/native XYZ.
- Type 1: another SFX node by node ID; use its current position and recursively
  activate it when required.
- Type 2: an active, AREA-present character. The packed ID contains three ASCII
  bytes high-to-low. Match them against the uppercased first three bytes of the
  live character's `model_resource_name`, then use its current Runtime-native
  translation and 3x3 orientation. This generically resolves GRID's `HO1`.
- Type 3: ID other than `-1` is a zero/identity world anchor; `-1` is no
  reference.

When both node and point reference types are nonzero, the node reference wins.
Otherwise the point reference is used. Referenced points are evaluated with the
Runtime row-vector matrix convention, then translated.

## Emitters, requests, and particles

**Confirmed — Runtime.** An active node is a per-logical-tick emitter, not a
long-lived sprite. It enqueues a request containing its selected definition,
world position, and sound/emission countdowns. The request capacity is 100.
Countdowns decrement while nonnegative and fire once when crossing below zero;
completed requests are removed. Sound IDs `0x0000FFFF` and `0xFFFFFFFF` are
invalid. A valid SFX sound ID is a DEAD0003 sound record `h_id`, not a
sound-table index. It is resolved by hID equality and submitted through the
scenario audio path.

Cin-SFX is serviced after each successful `SelectBodyAnimation` and
`SelectRelativeBodyAnimation` pose application. Each enabled channel emits its
ordinary SFX definition repeatedly during its inclusive `[start,end]` window.
The channel object reference is normalized as `ref > 0 ? ref - 1 : 0` and
matched against `MeshDescriptor::script_id`. Its position comes from the live
animated `RuntimeObjectState::world_translation`, composed through the actor's
current presentation orientation and translation. It then enters the ordinary
SFX request, countdown, particle, and sound paths.

Each burst creates `spawn_count` ordinary attached sprites up to the retail
particle capacity of 1000. Creation selects the resource's default object and
frame zero, emitter position, authored scale, validated render mode, normalized
start tint, optional random rotation, and `diffuse_alpha=0.5` for GRID's non-`0x0002`
path. Lifetime, scale velocity, angular velocity, vertical acceleration, color
deltas, and frame count are stored per particle.

Direction generation starts with authored XYZ, applies optional component
perturbation, preserves the vector length as speed, constructs the supplied
local circular cone, and aligns its `-Y` cone axis to the normalized authored
direction with an orthonormal basis. Zero speed yields zero velocity. RNG uses
the Runtime normalization family `integer[0,32767] / 32767`; production seeding
is scenario-local and tests inject deterministic values.

At the beginning of particle service, expired particles destroy their
`SpriteHandle`. Otherwise position, rotation, both scales, and 0..255 color are
advanced; tint is normalized; vertical acceleration updates velocity; frame is
`trunc((frameCount-1)*elapsed/lifetime)`; then elapsed advances. Teardown destroys
all remaining SFX-owned handles and clears requests and mutable nodes.

## Logical timing

**Strongly reconstructed.** Runtime uses its 30 Hz-era logical frame scale.
OpenNomad's frame-rate-independent adapter accumulates real seconds and performs
one SFX step per `1/30` second with logical delta `1`. The within-step order is
confirmed as nodes, then requests, then particles. A zero-delay GRID request can
therefore create and service its particle in the same logical tick; rendering at
60, 120, or 144 Hz does not increase emissions.

## GRID retail example

**Confirmed — data.** `GRID.SFX` is 3164 bytes and contains 10 definitions, one
section-D record, 11 nodes, and five tracks. Four `(1,-1)` nodes (`p0` through
`p3`) auto-activate before interface 29 and compose the opening portal. They use
authored SCX sprite IDs 9 through 12. Portal layers use additive mode 4 except
definition 4 (`ttt`), which uses darken mode 6. Script triggers `(0,1)`, `(0,8)`,
and `(0,20)` activate one, one, and five nodes respectively. These relationships
come entirely from serialized trigger data; no GRID, AREA 118, portal, camera,
or script-name special case exists.
