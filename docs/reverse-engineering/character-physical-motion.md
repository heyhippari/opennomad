# Character physical motion

> **Status:** Phase 4.2B static vertical physical service complete
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
-> gravity integration
-> owning-world static support query
-> vertical displacement, grounding, and fall-state resolution
-> accepted-position publication or complete rollback
-> play CTL markers at the accepted actor position
-> advance ordinary actor service generation
```

CTL root motion and one-shot/continuous movement auxiliaries update only the actor-owned candidate. The physical stage composes authored Y with one native gravity step, keeps X/Z identity-resolved, then uses authored character spheres and owning-world 3DO faces for static vertical response.

Authoritative address placement and materialization with `apply_transform=true` synchronize both positions explicitly. Ordinary service also re-anchors when the live transform diverges from accepted XYZ, covering structured scripts and direct debug/test mutation without stale-position snapback.

Structured ownership is gated before ordinary time accumulation. No CTL ticks, physical ticks, generations, or catch-up debt accrue while it is active. The first later ordinary tick re-anchors before CTL can produce new movement.

`MDROT000` remains visible after CTL callback dispatch and through entry to physical resolution. Phase 4.2A then clears it at the physical boundary; automatic movement-heading calculation is not implemented yet.

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
suppression input, 30-degree walkability, grounded velocity bias, no-support
rollback, and actor-owned fall episode tracking. Full formulas and Runtime
evidence live in [`character-support-motion.md`](character-support-motion.md).

Phase 4.2B.3 completes the ordinary static vertical state machine with latched
fall severity, pre-movement maximum-gap tracking, native fall-travel timing,
the one-tick small-step snap early return, and CTL fall/landing reactions through
the existing controller move-selection API. Serious stage entry selects move 2;
completed landing selects move 5, 4, or 100 from the recovered episode table.
Missing moves and absent or disabled controllers do not change physical results.

## Deferred physical behavior

Phase 4.2C owns:

- ordinary horizontal collision around `0x00469580`;
- wall blocking and sliding;
- automatic movement-heading rewriting;
- `MDROT000`'s actual suppression effect;
- mode-4 steep-slope horizontal response;
- transformed/moving and class-2 support response;
- moving platforms and conveyors;
- generic actor/object collision;
- exact swept ceiling collision through the general collision pipeline.

Adventure fall/landing event dispatch through `0x00414DE0` and the native jump
callback choreography remain deferred to their owning systems; they are not
substituted with physical-service event IDs or a partial jump mode.

See [`3do.md`](3do.md) for the authored collision substrate and [`ctl.md`](ctl.md)
for CTL motion production and the deferred native jump dependency.
