# Character static support and vertical motion

> **Status:** Phase 4.2B.2 implemented; Runtime static vertical behavior confirmed
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

Actor `+0xDC` is vertical velocity and `+0xE4` is the persistent velocity delta
applied once per 30 Hz base tick. Positive Y and positive vertical velocity are
downward. OpenNomad stores:

```text
gravity velocity delta/tick = 12.8608922958 in/s
normal acceleration         = 12.8608922958 * 30 = 385.8267689 in/s^2 (~9.8 m/s^2)
terminal downward velocity  = 787.40155 in/s (~20 m/s)
ground-contact bias         = 11.8110237 in/s downward
```

Each logical tick adds the gravity delta, clamps velocity to terminal speed, and
adds `vertical_velocity / 30` to candidate Y. The outer accumulator is the only
consumer of real frame time. Gravity runs without an enabled CTL controller.

**Confirmed — Runtime:** authoritative placement (`0x0041BDF0`) synchronizes
candidate/accepted XYZ and resets current velocity and fall episode fields, but
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

## Static support query

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

If `0 < new_gap < 7.8740158 in` (0.2 m), ordinary response snaps down exactly.
The comparison is strict. `0x0047CF00` suppresses this snap when no current
adventure actor exists or `(adventureFlags & 0x00000810) != 0`. `MDJP` reaches
`0x0046B710 -> 0x0047D2E0`; successful jump start sets both jump-active `0x10`
and one-update jump-start latch `0x800`. OpenNomad exposes a per-tick
`suppress_small_support_snap` input, currently false in production. Native jump
movement is not implemented. `MDSLIDOU` temporarily sets global `0x00910327`
while doing callback-specific support reconciliation, forcing the snap despite
the jump predicate; that callback path is deferred and no persistent global is
modeled.

## Walkability and fall episodes

**Confirmed — Runtime:** support is walkable when
`-world_normal.y >= cos(30 degrees)`, including exactly 30 degrees. Stable
walkable contact is grounded and writes the 11.8110237 in/s downward bias.
Steep contact still blocks downward penetration, is not grounded, and zeros the
vertical component; mode-4 horizontal/slide response remains deferred.

Positive support gaps classify native fall stages with inclusive thresholds:

| Gap | Native stage |
| --- | ---: |
| below 59.0551186 in (1.5 m) | 2 |
| at least 59.0551186 in | 1 |
| at least 118.110237 in (3 m) | 3 |
| at least 196.850388 in (5 m) | 4 |

Maximum observed positive support gap never decreases during an episode.
Resolved positive/downward displacement accumulates fall travel; upward and
zero displacement do not. Ordinary walkable landing clears stage, accumulated
travel, and maximum gap after preserving the final contact position.

Runtime can select CTL moves 2/4/5/100 and events from stage/travel thresholds.
Those presentation reactions are **Deferred parity**; gravity does not depend
on them.

## Failure and deferred branches

No qualifying support is not infinite free fall. Runtime rolls candidate back
to accepted; OpenNomad also leaves the already-integrated velocity intact and
clears the `MDROT000` transient at the physical boundary.

Phase 4.2C retains horizontal mode-1 collision, walls/sliding, automatic heading,
mode-4 steep response, transformed support/class 2, moving-platform association,
conveyor `mover_flags`, exact ceiling sweep, and generic actor/object collision.
A future full `MDJP` implementation must provide native jump movement and feed
its adventure-state predicate into `suppress_small_support_snap`.