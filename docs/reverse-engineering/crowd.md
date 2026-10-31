# Passerby crowd: trajectories, ANI banks and levels of detail

> **Status:** recovered from the PE32/i386 retail `Runtime.exe` (image base `0x00400000`)
> **Last updated:** 2026-10-31

Addresses are evidence only and are not encoded in gameplay code. OpenNomad implements this in
[`Crowd::Runtime`](../../src/core/Core/Crowd/CrowdRuntime.hpp), parsed by
[`TrajectoryOpt`](../../src/core/Core/Omikron/TrajectoryOpt.hpp) and
[`AnimationBankAni`](../../src/core/Core/Omikron/AnimationBankAni.hpp).

The people walking the streets and the sliders flying above them are not AREA characters. They are a dedicated crowd system
(`0x00452000..0x00458800`) driven by two AREA dependencies. Retail AREAs 0 (`ANEKBAH`), 1 (`SOUK`), 64 (`LAHOREY`)
and 101 (`QCHAUD`) use `PASSANTH.ANI`; AREA 157 (`PUIT`) uses `POLICIER.ANI`. `biblio.opt` is not referenced.


| AREA field | File | Loader | Content |
|---|---|---|---|
| `+0x7C` (`animationAniName`) | `ANIMS/<name>.ANI` | stage 8, `0x00434010` | per-character-type animation bank |
| `+0x73` (`optionsOptName`) | `TRAJECTOIRES/<name>.OPT` | stage 9, `0x0041B420` → `0x00453450` | street network and crowd population |
| `+0xA4` / `+0xA8` | — | `0x0040E990` / `0x0040E950` | male / female passerby model-variant bitmasks |
| `+0xAC` / `+0xAE` | — | `0x0040EA10` / `0x0040E9D0` | slider (flying vehicle) variant masks (`i16`) |

AREA table-0 bodies named "Passant Homme/Femme" are separate scripted characters that happen to use the same models.

## 1. ANI animation bank (`"3.0V"`)

```text
+0x00 char[4] "3.0V"
+0x04 u32     entry count
+0x08 entry[count], 0x18 bytes:
        +0x00 i32 character type   (IAM definition +0xB0: 1 male passer, 2 female passer, 5 Mecagarde, ...)
        +0x04 u32 file offset of the first node, 0 = none
        +0x08 ..  unused (0 / 0xCDCD fill)
node, 0x24 bytes, linked through +0x18:
        +0x00 i32 kind   9 walk, 11 blocked, 14/15/16 activity begin/loop/end
        +0x04 i32 id     unique within the entry
        +0x08 u32 file offset of an embedded 3DA payload
        +0x0C f32 footstep progress 1, +0x10 f32 footstep progress 2 (the sign picks the sound)
        +0x14 i16 footstep sound 1,     +0x16 i16 footstep sound 2
        +0x18 u32 file offset of the next node, 0 = end
        +0x1C char[8] activity group name ("Poteau", "Assis", "Attend", "Appel", "Scato", "NULL")
```

`0x00434010` relocates the offsets and binds +0x08 with `0x0046E5F0`, which treats the payload as an ordinary 3DA
(`maxFrame`, channel count, `0x28`-byte channel records whose stream offsets are relative to the payload). Several
nodes share one payload. Lookups: `0x00434530` entry by type, `0x004345E0` random node of a kind (`rand() % count`),
`0x00434630` node by id, `0x004346C0`/`0x00434730` begin/end node of a group.

`passantH.ani` gives the male passer five activity groups and the female passer one (`Assis`), plus one walk and one
blocked animation each.

## 2. OPT street network (`"V1.0"`)

Every table offset is file-relative. Each count is stored in the dword before the next table's offset:

| Header | Meaning |
|---|---|
| `+0x04` / `+0x08` | passerby trajectories are `[begin, end)` |
| `+0x0C` | passerby spacing, metres at Crowd Density 4 |
| `+0x10` | slider spacing |
| `+0x14` | trajectory count; `[+0x08, +0x14)` are slider lanes |
| `+0x18` | trajectories, `0x18` bytes |
| `+0x1C` / `+0x20` | segment count / segments, `0x14` bytes |
| `+0x24` / `+0x28` | stop-point count / stop points, `0x14` bytes (also sizes the activity pool `0x00539928`) |
| `+0x2C` / `+0x30` | route count / routes, `0x0C` bytes |
| `+0x34` / `+0x38` | route-step count / route steps, `0x10` bytes |
| `+0x3C` / `+0x40` | crossing count / crossings, 4 bytes |
| `+0x44` / `+0x48` | conflict count / conflicts, `i16` crossing indices |

```text
trajectory  +0x00 f32[3] start   +0x0C runtime list   +0x10 i16 first segment   +0x12 i16 first route
            +0x14 i8 route count (0 is rewritten to 1)   +0x15 u8 segment count
segment     +0x00 runtime list   +0x04 f32[3] vector   +0x10 i16 stop point or -1
stop point  +0x00 f32[3] offset from the segment start   +0x0C f32 yaw degrees
            +0x10 i16 ANI loop-node id (0 = disabled, cleared while in use)   +0x12 i8 extra loops
route       +0x00 runtime list   +0x04 i16 next trajectory   +0x06 i16 first step   +0x08 i16 entry crossing
            +0x0A i8 step count
route step  +0x00 f32[3] vector   +0x0C i16 crossing acquired before the following step
crossing    +0x00 i16 first conflict   +0x02 i8 conflict count   +0x03 i8 runtime lock counter
```

Retail: `anekbah.opt` has 216 passerby trajectories, 2781 segments, 84 stop points, 344 routes and 482 crossings.

## 3. Prototypes and levels of detail

`0x00453450` builds prototypes from the built-in tables `0x004C25B8` (male: `PSH_FN CMH_FN KSH_FN PVH_FN AMH_FN CEM_FN
KHO_FN TEH_FN YES_FN PSH1_FN KSH1_FN PVH1_FN`) and `0x004C2738` (female: `FSH_FN CWH_FN KWH_FN VFH_FN AN1_FN KIL2_FN
FSH1_FN KWH1_FN VFH1_FN`), loading `MESHES\PERSOS\%s.3do` for every bit set in the AREA mask. A sex is skipped when the
ANI has no walk node for it. Anekbah enables `0x201` (PSH_FN, PSH1_FN) and `0x41` (FSH_FN, FSH1_FN).

`0x00453A70` collects up to four **parentless objects in serialized order** (`0x00437DA0`) and selection-sorts them
by their own triangle+rectangle count, highest first. In PSH_FN these are `PhBassin`, `PiBassin`, `PmBassin`,
`PwBassin`: four complete 19-object skeletons, serialized about 80 inches apart along X. `0x00453910` links them
into a level-of-detail chain whose switch depths come from `0x004C8870`: 10, 20, 30, 40 m (sliders start at level 1
with `0x004C8860`: 20..50 m). `0x00453E80` gives each variant an integer share of 100 allocations.

The renderer (`0x0048D7F0`) walks each world's instance list (`world+0x184`). For a visible instance it skips
`1 - detail` leading levels (Detail Level, `0x0090E727`, default 1), then advances while the level's switch depth is
`<=` the instance's camera-space depth; the last level is never depth-culled. It writes the instance position into
the chosen root's serialized position and animates the subtree in preorder, matching a 3DA channel to an object when
`channel.id == object.scriptID - level * levelObjectCount` (`0x00470FE0`). One walk animation therefore drives every
level.

Characters never show those extra trees: an actor presents only its `actor+0x08` object (the highest-polygon
top-level sibling, `0x0041A7D0`), and later instances of a shared model clone just that subtree (`0x00437850`).
OpenNomad's `Character::Runtime::load_model_resource` likewise poses only the actor subtree.

## 4. Population

`0x00453B40` walks every passerby trajectory with spacing `39 * (+0x0C) * (5 - CrowdDensity)` inches
(`0x0090E726`, default 3). At each segment start, once the accumulated length exceeds the spacing, it allocates a
passerby (`0x00453ED0`; 200 slots, 240 nodes) and resets the accumulator. Allocation picks the sex with
`rand() & 1`, the first variant with a remaining share, and a random walk node. The new passerby's leading point is
the segment start; the walker stands 117 inches behind it. Its route is `firstRoute + counter % routeCount`, using the
shared counter `0x004C8854`. Afterwards every passerby gets a playback rate `1 + (5 - rand() % 10) * 0.05` and a
leading-point speed of that rate times the walk's root speed (`0x00453D80`, x256 per tick).

Anekbah fills all 200 slots at the default density.

## 5. Per-tick behaviour

`0x00454BB0` services every passerby (`0x004200F0` world update):

- **Leading point** (`0x00454F40`). It advances `speed * delta` along the segment. When the segment runs out it
  moves to the next segment (`0x004555C0`; a stop point there starts an activity on every other counter value), or
  into the route's junction steps (`0x004554B0`), and finally onto the route's next trajectory, which picks a new
  route. The passerby ahead (the next entry of its occupancy list, else the newest occupant of a later segment or of
  the route) blocks it when the candidate point would come within both radii (`0x00455230/0x00455340/0x004552B0`).
  List changes and crossing locks happen in `0x00455680`: entering a step acquires its crossing unless something
  locked it, which locks every listed conflicting crossing (`0x00453230`).
- **Walker** (`0x00455830`). The walk animation's root motion, rotated by the facing, moves the walker; its height
  follows the slope toward the leading point. `0x00455D10` turns the walker to face the leading point and paces it:
  twice the nominal speed under 19.5 inches, nominal up to 58.5, stopped beyond. A blocked passerby close to its
  point switches to the blocked animation (kind 11). A faster walker overtakes a slower one within 39 inches by
  swapping list positions and side-stepping.
- **Activities** (`0x00455E90`). The walker goes to the stop point, snaps to it, faces its yaw (`0x00442120`), and
  plays begin → loop (`1 + loops` times) → end, then resumes walking.

The rendered root is `walker - rootRadius` in Y plus accumulated root-motion Y; the root radius is the level-0
root's bounding radius (`PhBassin` 41.9 inches).

## 6. Sliders (flying vehicles)

Slider lanes are trajectories `[+0x08, +0x14)` of the same OPT. They share the occupancy lists, routes and crossing
locks with passersby, so crossings double as traffic lights.

**Prototypes.** When the OPT has lanes, `0x00453450` loads `sli_fn` from table `0x004C2A38` (two `sli_fn` entries)
for every bit of AREA `+0xAC` with bit 0 forced on (`0x0040EA10`), and `moto` from `0x004C28B8` for AREA `+0xAE`.
`sli_fn` objects get flag `0x400` (object flag `0x04000000`, environment-mapped; see
[renderer.md](renderer.md) §76) through `0x00437220`. Vehicle
prototypes keep their table weight of 1 (no `0x00453E80` normalisation). `SLI_FN` sorts its levels as `SlBassin`
(413 polygons, with door children; the boarding hull), `slider_fl`, `SlBasA`, `SlBasB`. Its LOD chain starts at
level 1 (`0x00453910(…, 1, 0x004C8860)`), so traffic never shows `SlBassin`; `moto` (single level) starts at 0.

**Population** (`0x004543F0`). The same placement walk as passersby (`0x00453B40`) over the lanes, with spacing
`39 * (+0x10)` inches; Crowd Density does not apply. Allocation (`0x004544B0`, 40 slots of `0x18` bytes):

```text
slot +0x00 node   +0x08 state (0 = traffic)   +0x0C top speed 5120   +0x14 i16 engine voice (-1)
     +0x16 i16 boardable (slot 0)
slot 0:  first sli_fn prototype, instance object = its level 1
others:  choice = rand() & 1; no moto -> 1; fewer than two sli_fn -> 0 (no moto -> stop)
         0 -> first moto with weight left, 1 -> first sli_fn after entry 0 with weight left
node +0x34 = +0x38 = 256; node +0x3C = 2 * level-0 root radius
```

Afterwards each slider faces its lane direction (`0x004427D0`). Anekbah enables `sli_fn` x2 and `moto` and fills all
40 slots over its 26 lanes.

**Traffic** (`0x00456530` state 0 → `0x00456C70`). The leading point advances exactly as for passersby
(`0x00454F40`). The slider steers at it with the full 3D vector (`0x00455D10` with the `0x004C8888` bands: 195/390
inches); `0x00453330` then pitches the basis, dividing its last element by the pitch cosine. Node `+0x34` is the
live velocity: `+256` per tick up to the slot's 5120 (20 inches/tick), `-768` per tick while blocked, reset to 256
after a stop. The slider moves `velocity / 256` inches per tick straight toward the point; the hull is drawn 30.75
inches above it.

**Engine sound** (`0x00456B40`). Within 585 inches of the listener a looping `SOUNDS\sliderm01.wav` follows the
slider (min distance 39, max 585, velocity from the lane direction); beyond 585 it stops.

**Taxi states** 1–7 in `0x00456530` (calling the boardable slider, boarding, riding and leaving it, `0x00452570`,
`0x004521E0`) belong to player gameplay.

## 7. Not implemented

- Slider taxi states 1–7: calling, boarding and riding the boardable slider.
- Footstep sounds (`0x004348E0`), blob shadows (`0x0041D120` with `Piedg`/`Piedd`) and passerby/slider collision
  handles (`0x0045E040`, `0x0045E110`).
- Talking to passersby (`0x00452280`, `0x004563A0`) and the `0x200` node state.
