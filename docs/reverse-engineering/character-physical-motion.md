# Character physical motion

> **Status:** Phase 4.2C.3C exact upward swept-sphere collision complete
> **Last updated:** 2026-09-02

This document separates recovered retail actor semantics from OpenNomad's modern C++ representation. Runtime did not contain a C++ abstraction named `PhysicalMotionService`.

## Confirmed Runtime state

**Confirmed — Runtime:** ordinary actors hold two physical position triplets:

| Native actor offset | Recovered meaning |
| --- | --- |
| `+0xE8/+0xEC/+0xF0` | last accepted/authoritative physical XYZ |
| `+0xF4/+0xF8/+0xFC` | candidate/prospective physical XYZ |

Relevant routines:

| Address | Confirmed behavior |
| --- | --- |
| `0x0041BDF0` | authoritative actor position setter; seeds accepted and candidate XYZ |
| `0x0041C140` | additional dual-position synchronization/reset path |
| `0x0045C2F0` | transforms authored local movement through live orientation and adds it to candidate XYZ only |
| `0x00466580` | ordinary state-1 actor update |
| `0x004A8160` | CTL/controller update within the ordinary path |
| `0x004672D0` | physical actor resolution |
| `0x00467770` | subsequent ordinary spatial/contact service |
| `0x00467685` region | successful candidate-to-accepted commit |
| `0x004676AB` region | failed accepted-to-candidate rollback |
| `0x0046C170` | `MDROT000` helper sets actor `+0x508` bit 0 |
| `0x004682E6..0x00468309` region | current-actor post-dispatch clear of bit 0 |

The recovered ordinary ordering is:

```text
CTL/controller update
-> other ordinary motion production where applicable
-> physical resolution
-> ordinary spatial/contact service
```

State 4 structured character/body-script ownership bypasses this ordinary state-1 path. Completion hands ownership back without retroactively running a missed physical update in that call.

## OpenNomad ordinary physical architecture

**OpenNomad-only architecture reproducing confirmed Runtime ordering:** `RuntimeCharacter` owns `PhysicalMotionState`, containing candidate XYZ, accepted XYZ, the shared ordinary 30 Hz accumulator, and initialization state. It moves with the complete logical body between world runtimes.

For every due ordinary fixed step:

```text
synchronize or defensively re-anchor from the live transform
-> one CTL logical tick when enabled
-> drain that tick's deferred CTL callbacks
-> compose D8/E0-equivalent per-tick X/Z terms and DC/30 vertical movement
-> ordinary mode-1 horizontal finite-cylinder collision and wall sliding
-> C.2 collision-induced heading correction
-> clear D8/E0 equivalents on a real forward collision
-> owning-world static support query
-> floor-resolved vertical displacement and fall-travel accumulation
-> upward swept-sphere ceiling clamp when resolved Y is negative
-> final Y application, support post-gaps, grounding, steep mode-4, mover response, and fall-state resolution
-> accepted-position publication or complete rollback
-> play CTL markers at the accepted actor position
-> advance ordinary actor service generation
```

CTL root motion and one-shot/continuous movement auxiliaries update only the actor-owned candidate. The physical stage adds actor-owned horizontal physical X/Z terms directly to that authored movement, adds `vertical_velocity / 30` to Y, and only then captures the complete desired displacement. A real mode-1 forward collision clears the X/Z terms after C.2 steering and before support response; pure depenetration does not.

## Native D8/E0 semantics

**Confirmed — Runtime:** actor `+0xD8` and `+0xE0` are horizontal physical displacement terms in Runtime-native inches per logical 30 Hz tick. The ordinary integrator applies them directly to candidate X/Z; unlike actor `+0xDC`, they are not multiplied by `1/30`. OpenNomad therefore names them `horizontal_physical_x_per_tick` and `horizontal_physical_z_per_tick`, not velocities.

Authoritative placement resets D8/DC/E0 equivalents while preserving the gravity parameter and OpenNomad's fractional accumulator remainder. Transfer between loaded world runtimes moves the complete actor-owned state unchanged and does not synchronize it.

**Confirmed — Runtime:** current-character address placement (`0x0041BF50`)
treats AREA table-5 Y as a floor/body-contact coordinate. It derives the logical
origin with `actorY = addressY - bodyBottom`, where `bodyBottom` is the maximum
raw authored collision-sphere `center.y + radius`. This is not the horizontally
trimmed collision cylinder extent. Placement also sets principal X to zero and
Y to the address yaw, preserves principal Z, synchronizes accepted/candidate
positions, and resets transient physical state. It does not manufacture an
ordinary actor generation or full physical tick.

Runtime immediately invokes a support-probe path after placement. OpenNomad's
`PhysicalMotionService::synchronize()` represents the confirmed authoritative
reanchor and reset while preserving gravity configuration and fractional
accumulator state. No exact probe-only helper currently exists, so that narrow
post-placement probe remains deferred rather than being approximated with
`resolve_tick()`.

Authoritative address placement and materialization with `apply_transform=true` synchronize both positions explicitly. Ordinary service also re-anchors when the live transform diverges from accepted XYZ, covering structured scripts and direct debug/test mutation without stale-position snapback.

Structured ownership is gated before ordinary time accumulation. No CTL ticks, physical ticks, generations, or catch-up debt accrue while it is active. The first later ordinary tick re-anchors before CTL can produce new movement.

`MDROT000` remains visible after CTL callback dispatch, through C.1 collision, and into C.2. It suppresses only collision-induced yaw for that physical tick; commit or rollback clears it at the physical boundary.

## OpenNomad Phase 4.2B data and vertical service

Phase 4.2B.1 makes Runtime's authored collision inputs available through the
typed 3DO model without changing physical execution:

```text
character model
	root CollisionSphere[5]
	-> authored actor body collider

world/decor Model3DOData
	mesh bounding radius
	mesh bounds min/max
	raw vertices
	triangle/quad topology
	authored face normals
	runtime object transforms
```

The root sphere centers, polygon face normals, and object bounds remain in
model-local Runtime XYZ; distances remain native inches. The authoritative
serialized layouts and Runtime evidence are documented in [`3do.md`](3do.md).

Phase 4.2B.2 consumes this metadata through the CPU-only static support query.
It implements native 30 Hz gravity, terminal velocity, body-bottom gaps, exact
floor clamp/snap and depenetration, the strict 0.2 m step-down snap and jump-state
suppression input, 30-degree walkability, grounded response, no-support
rollback, and actor-owned fall episode tracking. Full formulas and Runtime
evidence live in [`character-support-motion.md`](character-support-motion.md).

The later C.3 Runtime pass corrected the B-series interpretation of stable grounded DC. Ordinary walkable support clears D8, DC, and E0; `11.8110237` belongs to the steep-support downward response instead of a persistent grounded bias.

Phase 4.2B.3 completes the ordinary static vertical state machine with latched
fall severity, pre-movement maximum-gap tracking, native fall-travel timing,
the one-tick small-step snap early return, and CTL fall/landing reactions through
the existing controller move-selection API. Serious stage entry selects move 2;
completed landing selects move 5, 4, or 100 from the recovered episode table.
Missing moves and absent or disabled controllers do not change physical results.

## Phase 4.2C status

Phase C.1 implements ordinary horizontal collision around `0x00469580`, including continuous transformed-world triangle/quad feature collision, native skin/lookahead/depenetration, and iterative wall sliding. See [`character-horizontal-collision.md`](character-horizontal-collision.md) for the recovered formulas, constants, filters, and result semantics. The B-series support behavior remains documented in [`character-support-motion.md`](character-support-motion.md).

Phase C.2 consumes C.1's original intended and final resolved X/Z immediately before B support. A qualifying forward collision rotates principal yaw by one eighth of the shortest resolved-minus-intended heading difference. It uses the fall stage entering B, works independently of CTL presence or enablement, and preserves corrected yaw if later B processing rolls position back.

Phase C.3A adds the actor-owned D8/E0-equivalent terms and their complete ordinary lifecycle. Walkable support clears D8/DC/E0, then low-byte mover flags may seed exact `+/-2` inch X/Z terms for the next tick. Steep static support rewinds candidate to accepted, retries its current X/Z displacement through the C.1 finite-cylinder kernel with Y forced to zero, leaves candidate Y at accepted Y, and does not invoke C.2 a second time. Eligible steep response adds the support normal's unnormalized X/Z components to the physical terms and assigns DC `11.8110237`.

Phase C.3B unifies class-1 static and `0x00080000` class-2 transformed support. The main query retains primary and alternate hits from distinct runtime objects; persistent primary-relative-Y history can request the shared mode-4 retry for stage 0/2. A separate secondary point probe drives class-2 attachment after grounded reset and only when mover flags did not handle the support. Attachment writes one eighth of candidate-minus-primary-contact X/Z into the next-tick physical terms. See [`character-support-motion.md`](character-support-motion.md) for exact formulas and strict predicates.

Phase C.3C implements Runtime's exact upward general swept-sphere pass. It runs
only when floor resolution leaves `dy < 0`, from the current candidate actor
origin after C.1 X/Z resolution, along world direction `(0, -1, 0)`. The sphere
radius is the largest raw authored collision-sphere radius and is not multiplied
by `collidescale`; body top is independently aggregated as the minimum authored
`center.y - radius`. Eligible world geometry excludes only object flags `0x41`,
so class-2 `0x00080000` and special `0x20000000` geometry remain collidable.
Stable object/face traversal and strict-earlier replacement preserve native tie
ordering across transformed faces, finite authored outer edges, and vertices.

For nearest hit distance `d`, raw radius `r`, and aggregate body top `t`, the
exact upward displacement limit is:

```text
limit = -(t + d + r - 19.6850395)
```

The service clamps only when `dy < limit`; equality records a hit without a
clamp. Final Y is applied once after this decision, and C.3B post-movement gaps
and history consume that final value. The pass does not change DC, D8/E0,
controller state, heading, or support ownership. Its attempted/hit/clamped
record, exact inputs, object/contact, distance, and limit are transient debug
diagnostics reset on every physical tick and authoritative re-anchor.

Still deferred:

- primary `0x20000000` special-support response semantics;
- SCENE/person support association and moving-platform person integration;
- generic actor/object collision;
- native `0x08000000` adventure/gameplay support transition.

Adventure fall/landing event dispatch through `0x00414DE0` and the native jump
callback choreography remain deferred to their owning systems. The native
spatial-service heading latch, jump guard, and special movement/global motion
guard likewise remain deferred until their legitimate producers exist; they are not
substituted with physical-service event IDs or a partial jump mode.

See [`3do.md`](3do.md) for the authored collision substrate and [`ctl.md`](ctl.md)
for CTL motion production and the deferred native jump dependency.
