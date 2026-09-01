# Character physical motion

> **Status:** confirmed Runtime ownership and ordering; neutral OpenNomad Phase 4.2A service
> **Last updated:** 2026-09-01

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

## OpenNomad Phase 4.2A architecture

**OpenNomad-only architecture reproducing confirmed Runtime ordering:** `RuntimeCharacter` owns `PhysicalMotionState`, containing candidate XYZ, accepted XYZ, the shared ordinary 30 Hz accumulator, and initialization state. It moves with the complete logical body between world runtimes.

For every due ordinary fixed step:

```text
synchronize or defensively re-anchor from the live transform
-> one CTL logical tick when enabled
-> drain that tick's deferred CTL callbacks
-> neutral physical resolution
-> play CTL markers at the accepted actor position
-> advance ordinary actor service generation
```

CTL root motion and one-shot/continuous movement auxiliaries update only the actor-owned candidate. The Phase 4.2A resolver intentionally commits candidate to accepted and publishes accepted to the live transform without gravity or collision.

Authoritative address placement and materialization with `apply_transform=true` synchronize both positions explicitly. Ordinary service also re-anchors when the live transform diverges from accepted XYZ, covering structured scripts and direct debug/test mutation without stale-position snapback.

Structured ownership is gated before ordinary time accumulation. No CTL ticks, physical ticks, generations, or catch-up debt accrue while it is active. The first later ordinary tick re-anchors before CTL can produce new movement.

`MDROT000` remains visible after CTL callback dispatch and through entry to physical resolution. Phase 4.2A then clears it at the physical boundary; automatic movement-heading calculation is not implemented yet.

## Deferred physical behavior

Phase 4.2A does not implement the later behavior reached through physical resolution, including:

- `0x00469580` ordinary world/horizontal collision query;
- `0x00467030` floor/contact probe;
- `0x00465460` vertical/floor response;
- gravity, floor snapping, slopes, stairs, collider dimensions, collision response, or automatic movement-heading rewriting.

Those belong to Phase 4.2B/4.2C and can replace the neutral resolver without changing CTL ownership again.
