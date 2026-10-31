# Runtime camera controller and the adventure third-person camera

> **Status:** recovered from `Runtime.exe` (Ghidra headless decompiles plus the
> objdump listing) and implemented by `WorldCameraFollow` /
> `WorldCameraSystem` (`src/core/Core/WorldCameraFollow.*`,
> `src/core/Core/WorldCamera.*`). Fight cameras (0x00413CD0), first-person
> mode 4, selector resolvers 1..5/7..9 and the transition blender internals are
> documented below as gaps and not implemented.

## 1. Overview

Omikron's adventure camera is not a free orbit camera. Every frame Runtime
updates one **camera object** from its current camera definition:

- AREA/SCENE/GLOBAL/DIALOG camera records of type 12 or 20 (almost all retail
  AREA cameras) supply authored eye/target vectors. Each endpoint is either
  absolute (selector `-1`) or attached to a live actor (selector `0`, `6`, …).
- The *third-person follow* look comes from records whose **eye** is attached
  to the player by selector 0. They take the adventure follow setup: eased
  endpoints, plus a target-to-eye line of sight that pulls the eye in front of
  walls and raises it toward head height.
- Mode 0 is the hard-coded automatic player camera (3 m behind the actor).
  Runtime only selects it in fallback situations (section 6).

| Address | Role |
|---|---|
| `0x004187B0` | per-frame camera service (called from `0x004200F0`) |
| `0x00417CF0` | camera-object update (mode 13 copies `0x009103D4`; others below) |
| `0x00414BF0` | camera request: mode, request block `0x00930800` |
| `0x004147F0` | request-changes-camera predicate; maps mode 12 to the record type |
| `0x004146C0` | install a 0x30-byte runtime camera record into the camera object |
| `0x00414520` | per-mode setup after install |
| `0x00413C00` | adventure follow setup |
| `0x00413CD0` | fight camera setup (dispatcher states 11/13/14) — not implemented |
| `0x00415A10` | selector resolver dispatch (0 → `0x00414F30`, 6 → `0x00415540`, …) |
| `0x00415D10` | target endpoint follow |
| `0x00415E60` | eye endpoint follow (angles then position) |
| `0x00417070` | adventure line of sight / pull-in |
| `0x00416450` | flag-0x10 floor correction (unreachable, section 5.4) |
| `0x00441EB0` / `0x00442D70` | camera Euler matrix `Rx·Ry·Rz` / row-vector transform |
| `0x00444810` → `0x00444460` → `0x00444BB0` | decor segment query |
| `0x004C20C8` | mode preset table, 0x30 bytes per mode |

## 2. Camera requests and records

`0x00404940` (AREA opcode `0x5F`) normalises an IAM `0x2C` record into a
`0x30` runtime record at `0x004E6C40` and calls `0x00414BF0(12, 0x00930800)`:

| runtime record | IAM source | camera object |
|---|---|---|
| `+0x00` id | `+0x18` | `+0x08` |
| `+0x04` eye | `+0x00` | `+0x14` if eye selector `-1`, else offset `+0xB0` |
| `+0x10` target | `+0x0C` | `+0x20` if target selector `-1`, else offset `+0x7C` |
| `+0x1C` roll, `+0x20` FOV | `+0x1C`, `+0x1E` | `+0x2C`, `+0x30` |
| `+0x24` type | `+0x1A` | `+0x0C` (mode) |
| `+0x26` eye selector | `+0x22` | `+0x8C` |
| `+0x28` target selector | `+0x20` | `+0x58` |
| `+0x2A` target position ease frames | tail `+0x24` | `+0x88` |
| `+0x2C` eye position ease frames | tail `+0x26` | `+0xBC` |
| `+0x2E` eye angle ease frames | tail `+0x28` | `+0xC0` |

The request block records the target actor (`[2]` → `+0x5C`) and eye actor
(`[3]` → `+0x90`); for AREA requests both are the current player
(`0x004C2BC4`). OpenNomad's participant A is that actor.

`0x004147F0` replaces mode 12 by the record's own type unless that type is 12
or 20. `0x00414BF0` then installs the record itself for mode 12, and the
preset `0x004C20C8 + mode * 0x30` otherwise. Retail AREA cameras are type 12
(3769) or 20 (36); a single type-4 record exists (AREA 249, camera 4785).

## 3. Mode presets (`0x004C20C8`)

| mode | eye offset (in) | target offset (in) | sel eye/target | ease t/e/a | note |
|---|---|---|---|---|---|
| 0 | (0, 0, −118.11) | 0 | 0 / 0 | 3 / 8 / 8 | automatic player camera, FOV 75 |
| 2 | (0, 0, −39.37) | 0 | 1 / 1 | 0 / 8 / 16 | |
| 4 | 0 | (0, 0, 787.40) | 0 / 0 | — | first person; Y set to 0.7 × body bottom |
| 8 | (0, 118.11, −275.59) | (0, 78.74, 0) | 5 / 5 | 0 / 8 / 8 | |
| 11 | (0, 0, −196.85) | 0 | 4 / 4 | — | |
| 12, 13, 15, 16, 20 | — | — | −1 | — | record-driven / structured |

Every preset uses FOV 75. Modes 1, 3, 5–7, 9, 10, 14, 17–19 and 21 are other
specialised cameras that are not used by the adventure follow.

## 4. Per-frame update (`0x00417CF0`, modes other than 13)

The camera object keeps committed endpoints `+0x14` eye and `+0x20` target,
working copies `+0x34`/`+0x40`, and flags `+0x164`. Flag `0x1` is set when a
camera is installed and cleared after its first update; it forces every ease
below to snap.

1. Copy the committed endpoints into the working copies.
2. **Target** (`0x00415D10`, target selector ≠ −1): the resolver writes the
   actor position `P` (`+0xF4`) and Euler angles `A` (`+0x1A0`). Desired
   `P − transform(offset, Rx·Ry·Rz(A))`. With `n = +0x88` (1 behaves as 2):
   `target += (desired − target) · Δ / n`, or assign when `n == 0` or snapping.
3. **Eye** (`0x00415E60`, eye selector ≠ −1): first ease the stored angles
   `+0x4C` toward `A` per axis over `+0xC0` frames, using the wrapped
   difference in [−180, 180] and snapping when |difference| ≤ 0.1°; then ease
   the eye toward `P − transform(offset, Rx·Ry·Rz(eased angles))` over `+0xBC`
   frames.
4. With flag `0x4` (adventure): `+0x158 = +0x154`, `+0x154 = eye actor Y`,
   then `0x00417070` (flag `0x8`) — section 5.
5. Commit the working copies; clear flag `0x1`.

`Δ` is `0x004C30DC`, the dynamic frame delta in nominal 30 Hz units, clamped
by Runtime to at most 3.0 (`0x004BC1F4`). OpenNomad passes
`delta_seconds × 30` per display frame.

Selector 0 resolves the actor's position and angles directly. Selector 6
resolves the participant A/B midpoint and the angles `(0, atan2(N.z, N.x) +
90°, 0)` (see `iam-area.md`); the same easing applies.

## 5. Adventure follow setup and line of sight

### 5.1 Setup (`0x00414520` → `0x00413C00`)

Taken for mode 0, and for modes 12/20 whose eye selector is 0, when the player
actor exists and its dispatcher state (`+0x194`) is not 11, 13 or 14 (those
take the fight setup `0x00413CD0`). With `B` the eye actor's body bottom
(`+0x114`, the lowest collision-sphere extent below the origin, written by
`0x0041AC9A..0x0041ACE7`):

| field | value | meaning |
|---|---|---|
| `+0x138`, `+0x13C` | `−0.7 · B` | eye/target head offset from actor Y (Y down: above the origin) |
| `+0x12C` | 1.2 | ray length factor |
| `+0x140` | 8 | outward distance ease frames |
| `+0x144` | 4 | height ease frames |
| `+0x154` | actor Y | |
| flags | `|= 0x1C`, `&= ~0x600` | 0x4 follow bookkeeping, 0x8 line of sight, 0x10 floor correction |

The remaining fields written there (`+0xE0..+0xF4`, `+0x11C`, `+0x120`,
`+0x130`, `+0x134`) are only read by the flag-0x4000 variant `0x00416AD0`.

### 5.2 Line of sight (`0x00417070`)

Let `T` and `E` be the working target and eye, `V = E − T`, `L = |V|`, and
`state = +0xD0` (0 clear, 1 pulled in, 2 recovering), `D = +0x148`.

```text
hit = segment(T, T + 1.2·V)
if hit:
    if state == 0: D = L
    state = 1
    d = |hit − T| / 1.2
    if D < d and !snap: d = D + (d − D)·Δ/8      // pull in at once, ease out
    D = d
    E' = T + normalize(hit − T)·d
    goto height
else:
    if state == 1:
        T* , E* = unsmoothed desired endpoints (eased eye angles)
        if !segment(T*, T* + 1.2·(E* − T*)): state = 2
        else:
            E = T + normalize(V)·D
            E.y = committed eye Y + (actor Y − previous actor Y)
            T.y = committed target Y + (actor Y − previous actor Y)
            return
    if state != 2: return
    if L <= D: state = 0; return
    D = D + (L − D)·Δ/8
    E' = T + normalize(V)·D
height:
    w = D > L/2 ? (D − L/2) / (L − L/2) : 0
    head = +0x138 + actor Y
    E.y = ease(committed eye Y → E'.y·w + head·(1−w), 4 frames unless snap)
    T.y = ease(committed target Y → T.y·w + head·(1−w), 4 frames unless snap)
    E.xz = E'.xz
```

The recovery ease converges asymptotically on `L`; because `D` is a float the
camera can remain in state 2 just below `L`, which is harmless.

Branches gated by camera flag `0x1000` together with object flag `0x20000000`
belong to the fight setup and are not taken in adventure.

### 5.3 Segment query (`0x00444810`)

The query walks every resident decor through `0x004430A0`, skips objects with
flags `0x41` or `0x800000` (`0x00444460`), and tests triangles and rectangles
two-sidedly (`0x00444BB0`): the segment must strictly cross the plane
(|n·d| > 0.0001) and the crossing must lie inside the polygon. The nearest
crossing parameter wins. OpenNomad: `SweptSphereQuery::find_segment`.

### 5.4 Flag 0x10 floor correction (`0x00416450`) is unreachable

`0x00416450` triggers only when `eye actor Y − 1.9685 > +0x154`, but
`0x00417CF0` copies the eye actor Y into `+0x154` immediately before calling
it, so the trigger never fires from that call site (its only caller).

## 6. When Runtime selects mode 0

- `0x004200F0`: mode 13 releases to mode 0 when no structured camera was
  published this frame, `[Preferences] autocameraplayer` is nonzero (retail
  default `"0"`), and a player exists (`ctl.md` §5). No transition.
- `0x004187B0`: when the camera's issuing resident scene slot (`+0x10`, from
  request `[10]`) is no longer resident (`0x00419A70`: environment state ≠ 2).
  No transition.
- `0x004187B0`: mode 17 (`0x11`) with flag `0x8000` when the eye-target
  distance exceeds 236.22 in (6 m), with a 60-unit transition. Not used by
  the adventure follow and not implemented.

OpenNomad implements the first two in `WorldScene` through
`WorldCameraSystem::player_camera_command`.

## 7. Other recovered details and gaps

- `0x00414100` (eye selector 6 at setup) scales the X/Z components of both
  offsets by `distance(A, B) × 0.0169333` (1/1.5 m), so two-shots keep their
  framing for any separation. Not yet implemented.
- Mode 4 (`0x00414520` case 4/5) sets the eye/target offset Y to
  `B × (actor+0x10 ? 0.7 : 0)`. Not implemented; the one retail type-4 AREA
  camera keeps the authored-record path.
- Transitions: `0x00414A90`/`0x00418410` blend from the previous camera object
  (which keeps updating) to the new one. OpenNomad freezes the start pose and
  blends to the live follow pose with its existing quadratic ease.
- Mode 14 (`0x00446500`) and mode 20's flag `0x2000` side effect
  (`0x00433320`) are not covered.
