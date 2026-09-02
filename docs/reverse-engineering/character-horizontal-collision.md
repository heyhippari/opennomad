# Character horizontal collision

> **Status:** Phase 4.2C.2 automatic collision heading implemented
> **Last updated:** 2026-09-02
> **Runtime.exe SHA-256:** `55f7120bfea7891b048c64e3682f3259cdbf2719a43fa24e42254b753c95d2ef`

This document records the recovered ordinary horizontal physical path from the PE32/i386 retail `Runtime.exe` image based at `0x00400000`. Addresses are evidence only and are not encoded in gameplay code.

## Runtime evidence

| Address | Confirmed role |
| --- | --- |
| `0x004672D0` | ordinary physical resolver |
| `0x00469580` | shared movement/collision solver |
| `0x0046A020` | collision-response configuration and horizontal normal masking |
| `0x004AD360` | general sweep-query entry |
| `0x004AD460` | per-runtime-object collision processing |
| `0x004A9AB0` | polygon collision processing |
| `0x004A9D30` | lower-level polygon feature collision family |

## Confirmed mode-1 body

**Confirmed - Runtime:** ordinary mode 1 does not sweep the five authored collision spheres independently and does not construct a capsule. It collapses the authored set into a world-Y-aligned finite cylinder:

```text
radius = maximum authored sphere radius * collidescale
top = minimum(sphere.centerY - sphere.radius)
bottom = maximum(sphere.centerY + sphere.radius)
effective bottom = bottom - 11.8110237 in
```

The retail `collidescale` default is `1.0`. Only the horizontal radius is scaled. The 11.8110237-inch (30 cm) lower trim is the mode-1 step-over allowance; it is semantically independent from the equal-valued grounded velocity bias.

OpenNomad represents the future preference seam as `PhysicalMotionEnvironment::collision_scale`, defaulting to `1.0F`. Invalid/non-finite dimensions or scale make horizontal resolution identity-only; the existing B-series body/support path still decides the physical outcome. This validation is OpenNomad hardening.

## Geometry and object filtering

The query consumes `Model3DOData` CPU meshes, vertices, serialized triangle/quad topology, authored face normals, and the owning world's current `RuntimeObjectState` values. It has no renderer or OpenGL dependency.

Ordinary horizontal filtering is:

```text
flags & 0x00000041 != 0  -> skip
flags & 0x20000000 != 0  -> skip
flags & 0x00080000 != 0  -> do not skip for this reason
```

This intentionally differs from static B-series support filtering. Runtime-transformed objects use current world translation, matrix, and component scale. OpenNomad transforms polygon vertices into world space; retail Runtime instead transforms the sweep into object-local space. These are mathematically equivalent. Normals use inverse component scale followed by the Runtime row-vector rotation matrix, then normalization, so non-uniform scale preserves plane geometry and sidedness.

Triangle parent references continue to resolve through `skin_parent_index`. Malformed references and near-zero transform scales skip the affected polygon safely.

## Continuous polygon query

Runtime performs a continuous swept finite-body query, not endpoint overlap. OpenNomad builds a swept-cylinder AABB, applies conservative object and polygon rejection, then traverses objects, polygons, and features in stable serialized order. Each triangle or actual four-edge quad contributes:

- face/interior contact from the finite-cylinder support point swept against the authored plane;
- continuous cylinder-side versus finite-edge contact with vertical interval overlap;
- continuous horizontal circle versus vertex contact with vertical interval overlap.

The earliest qualified contact wins. Quads remain one plane with four authored edges, so render triangulation cannot introduce a diagonal collision feature. Feature qualification preserves polygon one-sidedness and rejects normals without a usable horizontal component; flat floors and ceilings therefore do not become horizontal walls.

Mode 1 flattens every accepted response normal before use:

```text
horizontal normal = normalize(hitNormal.x, 0, hitNormal.z)
```

This stage never changes Y and remains independent from B-series vertical support.

## Solver constants

| Meaning | Value |
| --- | ---: |
| near-zero movement threshold | `0.0001` |
| forward-query lookahead | `2.0 in` |
| collision skin | `1.0 in` |
| depenetration retry multiplier | `1.1` |
| mode-1 maximum forward collision passes | `3` |
| mode-1 lower-body trim | `11.8110237 in` |

OpenNomad additionally caps depenetration retries at 16 iterations. This is malformed-data safety hardening, not recovered Runtime behavior. Depenetration retries do not consume the three forward collision passes.

A lookahead-only contact beyond requested travel does not shorten movement. A contact exactly at the endpoint is a forward collision. For a normal contact at distance at least one inch, allowed travel is `hitDistance - 1 in`.

For a contact inside the skin, Runtime advances no farther forward and pushes outward by `1 in - hitDistance`. If the retry remains inside the skin, correction grows by `1.1` before re-querying.

## Wall sliding

After a normal forward collision, let `R` be unresolved distance, `d` the normalized pre-collision direction, and `n` the flattened normalized response normal:

```text
inward = max(-dot(n, d), 0)
remaining = d * R + n * inward * R
```

This removes only the component entering the obstacle and retains tangent motion. The solver repeats query, skin stop, and projection for at most three forward collisions. Any unresolved remainder after the third collision is discarded.

## Result semantics and ordering

`forward_collision` is true only when a normal forward hit curtails requested movement. Pure starting-skin depenetration remains false, as does a lookahead-only observation beyond the endpoint. Depenetration has a separate diagnostic flag. C.2 uses this exact return semantic and never re-queries collision geometry.

One ordinary tick now runs:

```text
CTL/root-motion producers
-> gravity and saved complete desired XYZ
-> reset candidate to accepted
-> resolve desired X/0/Z through the mode-1 cylinder solver
-> apply automatic collision-heading correction
-> B support query at resolved X/Z
-> B vertical/fall response using the original saved desired Y
-> commit or B-series rollback
```

Actor-owned diagnostics preserve intended and resolved X/Z, body dimensions, collision scale, forward/depenetration state, iteration counts, last contact data, automatic-heading suppression, calculated headings/delta, and yaw before/after.

## Automatic movement heading after mode-1 collision

**Confirmed - Runtime:** after a real mode-1 forward collision, `0x004672D0` applies the following guard chain in order:

```text
mode-1 return != 0
actor dispatcher state != 3
actor+0x51D == 0
0x006A52CC == 0
fall stage == 0
0x0053AE1C == 0
actor+0x508 bit 0 (MDROT000) clear
abs(intendedX) > 0.0001
abs(resolvedX) > 0.0001
```

The X comparisons are strict and deliberately have no corresponding Z threshold. OpenNomad implements the real-forward-collision, pre-B fall-stage, MDROT, intended-X, and resolved-X guards. Its ordinary service is structurally the native state-1 path, so no fake dispatcher-state field is needed; native state 3 remains documented for future generic dispatch parity.

The remaining native guards have no proven OpenNomad producer and are intentionally deferred. Actor `+0x51D` is conservatively identified as a spatial-service heading-suppression/contact latch: `0x00467770` clears it, qualifying native spatial/event checks may set it, and the following physical tick consumes it. Generic OpenNomad zone or proxy overlap is not assumed equivalent. Global `0x006A52CC` is a jump-choreography guard associated with MDJUMP/MDJP paths. Global `0x0053AE1C` is a special movement-mode/global motion guard, with observed related code around `0x00466210`, `0x00465EED`, `0x00465F36`, `0x00465FC2`, `0x004661C4`, `0x0046AF1A`, and `0x0046B367`; its source-level semantics remain unresolved.

The heading calculation uses only C.1's original intended and final resolved X/Z:

```text
resolvedHeading = atan2(resolvedZ, resolvedX) * 180 / pi
intendedHeading = atan2(intendedZ, intendedX) * 180 / pi
delta = resolvedHeading - intendedHeading
if delta < -180: delta += 360
if delta > 180: delta -= 360
yaw += delta * 0.125
```

The delta comparisons are strict, so `-180` and `+180` retain their signs. Yaw then uses its own native one-step wrap:

```text
if yaw > 360: yaw -= 360
if yaw < 0: yaw += 360
```

These comparisons are also strict: yaw exactly `360` remains `360`. This differs from CTL's signed `std::remainder` wrapping. OpenNomad updates `principal_orientation_degrees.y` through `set_principal_orientation()`, preserving X/Z and synchronizing the transform matrix.

`MDROT000` sets a one-tick suppression transient. C.1 collision, sliding, and depenetration still run; only C.2 yaw correction is skipped, B support/vertical processing still runs, and commit or rollback clears the transient. C.2 precedes B, so it observes the fall stage entering support processing. A later B position rollback restores translation but intentionally does not restore the corrected yaw.

## Deferred behavior

Phase C.2 does not implement mode 4, D8/E0 horizontal physical velocity, conveyors, moving-platform attachment, support class 2, ceiling collision, actor-vs-actor collision, jump choreography, the native spatial-service latch, the special-movement guard, or adventure event dispatch. Runtime's normal mode-1 forward-collision side effect that clears D8/E0 remains deferred until C.3 introduces meaningful producers and consumers.
