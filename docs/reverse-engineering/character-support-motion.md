# Character static support and vertical motion

> **Status:** Phase 4.2C.3B unified support and attachment response complete
> **Last updated:** 2026-09-02
> **Runtime:** PE32/i386 `Runtime.exe`, image base `0x00400000`, SHA-256
> `55f7120bfea7891b048c64e3682f3259cdbf2719a43fa24e42254b753c95d2ef`

This document distinguishes **Confirmed — Runtime**, **OpenNomad implementation
choice**, and **Deferred parity**. Native addresses are evidence only and are
not encoded into gameplay logic.

## Ordering and actor state

**Confirmed — Runtime:** ordinary state-1 service (`0x00466580`) runs CTL
(`0x004A8160`), physical resolution (`0x004672D0`), then later spatial/contact
service (`0x00467770`). The resolver integrates vertical velocity, preserves the
complete candidate-minus-accepted Y displacement, resolves X/Z separately, probes
support (`0x00467030`), applies vertical response (`0x00465460`), and either
commits the complete candidate or rolls it back.

Actor `+0xD8/+0xE0` are horizontal physical displacement terms added directly
to candidate X/Z per logical tick. Actor `+0xDC` is vertical velocity and
`+0xE4` is the persistent velocity delta applied once per 30 Hz base tick.
Positive Y and positive vertical velocity are downward. OpenNomad stores:

```text
gravity velocity delta/tick = 12.8608922958 in/s
normal acceleration         = 12.8608922958 * 30 = 385.8267689 in/s^2 (~9.8 m/s^2)
terminal downward velocity  = 787.40155 in/s (~20 m/s)
steep-response velocity     = 11.8110237 in/s downward
```

Each logical tick adds the gravity delta, clamps velocity to terminal speed, and
adds `vertical_velocity / 30` to candidate Y. The outer accumulator is the only
consumer of real frame time. Gravity runs without an enabled CTL controller.
The D8/E0 equivalents are native inches per logical tick and receive no `1/30`
or display-frame scaling.

**Confirmed — Runtime:** authoritative placement (`0x0041BDF0`) synchronizes
candidate/accepted XYZ and resets D8/DC/E0 and fall episode fields, but
does not overwrite the gravity parameter. **OpenNomad implementation choice:**
`synchronize()` also preserves the accumulator's fractional remainder. Body
transfer preserves the entire actor-owned state; it is not placement.

## Authored body geometry and probe

**Confirmed — Runtime:** body top is the minimum `center.y - radius`; body bottom
is the maximum `center.y + radius`. Helper `0x00444360` selects the first sphere
with maximum radius. The support helper selects the first bottom-most sphere by
maximum `center.y + radius`, then the greatest strictly smaller result as the
second-bottom sphere; if none exists it reuses the bottom sphere.

The second-bottom center-Y anchor is transformed through current actor
orientation. The query point deliberately combines prospective X/Z with
accepted Y:

```text
Q.x = prospective.x + transformed_offset.x
Q.y = accepted.y    + transformed_offset.y
Q.z = prospective.z + transformed_offset.z
radius = largest authored sphere radius
```

Characters without authored spheres have no physical body. OpenNomad rolls the
attempt back deterministically and emits a one-time debug diagnostic; it does
not synthesize a capsule or use render bounds.

## Unified support query

**Confirmed — Runtime:** objects matching `flags & 0x00000041` are excluded.
Objects matching `flags & 0x00080000` enter a transformed/moving path and are
not ordinary static supports. The original source names of these masks are
unknown. OpenNomad's CPU-only query consumes immutable `Model3DOData`, raw face
vertices and authored face normals together with the owning runtime's current
object transforms. It has no renderer, OpenGL, GPU geometry, or material test.

A static floor face requires `face_normal.y < -0.0001`. For a world-space face
normal `n` and first vertex `v0`:

```text
D = -dot(n, v0)
S = dot(n, Q) + D
vertical_distance = -S / n.y
clearance = vertical_distance - query_radius
```

The vertical intersection must lie beneath the probe and inside the actual
triangle or quad. OpenNomad uses the dominant normal axis for a projected
boundary-inclusive point-in-polygon test. Negative clearance is valid. The
nearest clearance wins, with authored object/face order breaking ties.

**OpenNomad implementation choice:** `0x00080000` objects are support class 2
and use a true 3D swept sphere against transformed faces, finite outer edges,
and vertices. Other accepted objects are class 1. The nearest hit per runtime
object is retained first; the globally nearest object is primary and the nearest
distinct object is alternate. Multiple faces from one object cannot produce an
alternate. Transformed normals use inverse component scale, the runtime world
matrix, and normalization.

Object `flags & 0x20000000` identifies a special response branch whose source
semantic name is not recovered. OpenNomad reports it as special/deferred and
rolls back instead of treating it as ordinary walkable support or searching
through it.

## Gap and vertical response

**Confirmed — Runtime:** for the upright static path:

```text
gap = support_world_y - actor_origin_y - body_bottom
```

Positive is separation, zero is contact, and negative is penetration. Given
the pre-movement gap `g` and complete desired Y displacement `dy`:

```text
if g <= 0 and dy >= 0: dy = 0
else if dy >= g:       dy = g
new_gap = g - dy
```

Upward movement remains allowed, including exact floor depenetration. On an
ordinary walkable contact, any residual negative gap is snapped exactly to zero.
The native swept-body ceiling path through `0x00444F60` is **Deferred parity**.
Further analysis confirmed that it enters the generic collision pipeline through
the `0x004430A0` broadphase, collision callback `0x00444E60`, and the
`0x004992D0` family; it is not a standalone floor/ceiling ray query.

For native fall stage 0 or 2 only, `0 < new_gap < 7.8740158 in` (0.2 m) takes
an early snap-and-return path. The comparison is strict. Candidate Y reaches
support exactly, but that tick does not establish grounded state, apply the
walkable motion reset, seed mover terms, run a landing reaction, or clear the fall episode.
Genuine contact is processed on the following physical tick. Stages 1, 3, and
4 return from the airborne branch before this predicate and therefore are not
snapped near landing.

`0x0047CF00` suppresses the small snap when no current adventure actor exists
or `(adventureFlags & 0x00000810) != 0`. `MDJP` reaches `0x0046B710 ->
0x0047D2E0`; successful jump start sets both jump-active `0x10` and one-update
jump-start latch `0x800`. OpenNomad exposes a per-tick
`suppress_small_support_snap` input, currently false in production. Native jump
movement is not implemented. `MDSLIDOU` temporarily sets global `0x00910327`
while doing callback-specific support reconciliation, forcing its support-snap
branch independently of the ordinary predicate; that callback path is deferred
and no persistent global is modeled.

## Walkability and fall episodes

**Confirmed — Runtime:** support is walkable when
`-world_normal.y >= cos(30 degrees)`, including exactly 30 degrees. Stable
walkable contact is grounded and clears D8, DC, and E0. The prior OpenNomad
B-series interpretation of `11.8110237` as a grounded downward bias was corrected
by the C.3 reverse-engineering pass.

**Confirmed — Runtime:** unacceptable steep contact is not grounded. Runtime
saves the current candidate-minus-accepted X/Z, rewinds candidate to accepted,
and calls shared solver `0x00469580` in mode 4 with Y forced to zero. Mode 4 uses
the C.1 finite-cylinder body, skin, lookahead, depenetration, sliding, and
three-pass limit. Candidate Y remains accepted Y, and this internal retry does
not invoke C.2 automatic heading. For the ordinary non-upward response:

```text
D8 += support_normal.x
E0 += support_normal.z
DC  = 11.8110237
```

The actual support-normal components and signs are used without horizontal
normalization. Upward DC bypasses this normal steep physical-term assignment.

### Walkable mover flags

After walkable contact clears D8/DC/E0, Runtime processes the support mesh's
low-byte `mover_flags`:

| Flag | Next physical term |
| --- | --- |
| `0x10` | `D8 = +2` |
| `0x20` | `D8 = -2` |
| `0x40` | `E0 = +2` |
| `0x80` | `E0 = -2` |

Checks execute in table order, so `0x20` overwrites `0x10` and `0x80` overwrites
`0x40`; opposing bits do not cancel. Values are Runtime-native inches per
logical tick. They are seeded after contact response and first affect the next
tick, when they compose with authored movement before C.1. A mover-handled
diagnostic is retained so C.3B can skip later class-2 attachment response.

### Support history and class-2 attachment

Every successful main query updates persistent primary support history, even
before the deferred primary `0x20000000` response. For primary clearance `P`,
alternate clearance `S`, anchor `anchorY`, and largest radius `r`:

```text
supportDelta       = P + anchorY - previousPrimaryRelativeY
primaryRelativeY   = acceptedY - candidateY + P + anchorY + r
alternateRelativeY = S + primaryRelativeY - P
```

Stage 0 or 2 requests the shared mode-4 horizontal retry when an alternate
exists and both comparisons are strict:

```text
supportDelta + primaryGapAfter < 0
alternateGapAfter < -11.8110237
```

The `MDSLIDOU` environment seam suppresses only this history reason. If history
and steepness both request mode 4, it runs once; history-only response does not
seed steep D8/DC/E0 terms.

After grounded class-2 response clears D8/DC/E0, mover flags retain priority.
Without a mover, a separate zero-radius vertical point probe returns nearest
distance, normal, and runtime-object index. This is independent from the main
query's alternate and includes transformed and `0x20000000` objects. Attachment
applies when its gap is strictly greater than `11.8110237`, its normal is
nonwalkable, or its mesh has `0x20000000`:

```text
D8 = (candidate.x - primaryPoint.x) * 0.125
E0 = (candidate.z - primaryPoint.z) * 0.125
```

Secondary failure preserves grounded primary response and zero terms.

### Fall-stage lifetime and reactions

**Confirmed - Runtime:** positive support gaps use these inclusive thresholds:

| Gap | Native stage |
| --- | ---: |
| below 59.0551186 in (1.5 m) | 2 |
| at least 59.0551186 in | 1 |
| at least 118.110237 in (3 m) | 3 |
| at least 196.850388 in (5 m) | 4 |

Stage is stateful rather than recomputed every tick. Stage 0 has no established
severity and stage 2 is a small/unclassified episode; only those two stages are
eligible for classification. Serious stages 1, 3, and 4 latch until contact. A
stage-4 actor therefore remains stage 4 as its support gap decreases through
the lower thresholds while falling.

Maximum support gap is updated from the pre-movement gap at the beginning of
`0x00465460`, before resolved Y movement reduces it. Resolved positive/downward
movement is added to accumulated fall travel before the airborne/contact branch
only when the tick began with a nonzero fall stage. Consequently, the initial
stage-0 classification tick does not count its movement, while the final
movement that reaches support does count.

Entering serious stage 1, 3, or 4 requests exact CTL move 2 through native move
lookup `0x0046ACE0` and selection `0x0045A630`. Contact evaluates the completed
episode before clearing it:

```text
fall travel >= 196.850388 in                         -> move 5
else fall travel >= 118.110237 in                    -> move 4
else fall travel >= 59.0551186 in and max gap >= it -> move 4
else current CTL move is exactly 2                   -> move 100
```

Missing authored moves are nonfatal. Selection uses the controller when one
exists regardless of whether ordinary controller servicing is enabled; physics
remains valid without a controller. Physical selection occurs after that tick's
CTL service and is first serviced/presented by the next CTL tick.

Completed ordinary support contact terminates the fall episode independently
of walkability. Walkable contact separately establishes grounded state and the
D8/DC/E0 reset before optional mover seeding; steep contact remains ungrounded
and applies the confirmed mode-4 and physical-term response above.

Native actor dispatcher state `actor+0x194` suppresses moves 2/4/5/100 for
values 2, 3, and 15. OpenNomad's current ordinary player service corresponds to
native state 1, so no speculative native-state field is introduced.

The broader adventure event dispatcher `0x00414DE0` observes serious fall entry
as event `0x12`, severe current-player landing as `0x13`, and can request event
`0x10` on other landing/recovery paths. Packet construction, adventure state,
and the `0x00414BF0` subsystem remain deferred; OpenNomad does not model these
IDs as standalone events.

Before positive-gap classification/snap, native global `0x006A52CC` can return
early. It is controlled by authored jump callback choreography including
`MDJUMP0AP` and `MDJUMP0BP`, not a generic physics mode. Its producer lifecycle
remains deferred with the complete jump callback family.


## Failure and deferred branches

No qualifying support is not infinite free fall. Runtime rolls candidate back
to accepted; OpenNomad also leaves the already-integrated velocity intact and
clears the `MDROT000` transient at the physical boundary.

C.3C owns ceiling/upward swept-sphere response and primary `0x20000000`
behavior. Full `MDSLIDOU` callback choreography remains deferred; C.3B exposes
only its support-history override seam.
SCENE/person association and `0x08000000` adventure state transition remain
higher-level deferred behavior, not generic physical support.
A future full `MDJP` implementation must provide native jump movement and feed
its adventure-state predicate into `suppress_small_support_snap`.