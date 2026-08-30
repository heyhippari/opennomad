# CTL character control sets

> **Status:** recovered format and controller documentation for OpenNomad
> **Last updated:** 2026-08-30
>
> CTL is Omikron's authored character-control/state-machine resource. One CTL
> bank per character role is named by the IAM character definition
> (`adventure_control_set`, `combat_control_set`); Runtime loads control sets
> generically from `ANIMS/<authored control-set filename>`. This document
> describes the recovered H1-family layout, the controller execution model,
> and the recovered Runtime addresses behind each behavior.
>
> OpenNomad implementation:
> [`CtlControlSet`](../../src/core/Core/Omikron/CtlControlSet.hpp) (immutable
> parsed bank) and
> [`CtlController`](../../src/core/Core/Character/CtlController.hpp)
> (mutable per-character controller).

Related documentation:

- [`iam-scenario-vm.md`](iam-scenario-vm.md) — compact opcodes `0x3F`,
  `0x68`, `0x69`;
- [`runtime-coordinate-math.md`](runtime-coordinate-math.md) — row-vector
  orientation and root-motion conventions;
- [`audio.md`](audio.md) — SCX `DEAD0003` sound hIDs used by animation
  markers;
- [`scx.md`](scx.md) — the embedded ordinary 3DA format reused by CTL.

Confidence labels follow the project convention: **Confirmed — Runtime**
(traced in the retail binary), **Confirmed — data** (verified against
authored game data), **OpenNomad-only** (modern structure with no retail
layout claim).

---

# 1. File layout

## 1.1 Header (0x58 bytes) — Confirmed — Runtime/data

| Offset | Type | Meaning |
| ------ | ---- | ------- |
| `+0x00` | u32 | magic `0x30374543` (bytes `"CE70"`) |
| `+0x04` | u32 | format/version (must equal `0x00000101`; other versions are rejected) |
| `+0x08` | u32 | unresolved; preserved |
| `+0x0C` | u32 | top-level move count |
| `+0x10..+0x57` | — | unresolved/pointer-era fields; preserved for diagnostics only |

The first top-level move record begins at `0x58`. Runtime validates both the
magic and version strictly; files with version != `0x101` are rejected with a
structured error.

## 1.2 Top-level move record (0x20 bytes) — Confirmed — Runtime/data

| Offset | Type | Meaning |
| ------ | ---- | ------- |
| `+0x00` | u32 | **move ID** (authored ID, not an array index) |
| `+0x04` | u32 | child state count |
| `+0x08` | u32 | flags; bit `0x00000001` marks the default move |
| `+0x0C` | u32 | serialized/runtime-pointer-era; preserved neutral |
| `+0x10` | u32 | serialized/runtime-pointer-era; preserved neutral |
| `+0x14` | char[12] | name, NUL-terminated when shorter |

Move records are contiguous. The complete child-state count is
`sum(move.child_state_count)`; child state records follow the move array, and
each child belongs to its containing move by those authored counts
(containment), never by the serialized owner field at state `+0x38`.

## 1.3 Child state record (0x58 bytes) — Confirmed — Runtime/data

| Offset | Type | Meaning |
| ------ | ---- | ------- |
| `+0x00` | u32 | **state ID** (authored ID, not an array index) |
| `+0x04` | u32 | input condition (see §3) |
| `+0x08` | u32 | flags (see §1.4) |
| `+0x0C` | u32 | unresolved |
| `+0x10` | f32 | transition window start (animation phase units) |
| `+0x14` | f32 | transition window end |
| `+0x18` | f32 | transition value (phase seed for goto-at-end) |
| `+0x1C` | u32 | dynamic block: pointer-era field |
| `+0x20` | u32 | parent refs: pointer-era field |
| `+0x24` | u32 | child refs: pointer-era field |
| `+0x28` | u32 | goto state: serialized state ID / pointer-era slot |
| `+0x2C` | u32 | pointer-era field |
| `+0x30` | u32 | pointer-era field |
| `+0x34` | u32 | unresolved |
| `+0x38` | u32 | owner move: Runtime overwrites/relocates; containment wins |
| `+0x3C` | u32 | unresolved |
| `+0x40` | u32 | callback name: pointer-era field |
| `+0x44` | u32 | animation key: pointer-era field |
| `+0x48` | u32 | Runtime-loaded animation pointer |
| `+0x4C` | u16 | animation mode (see §4.2) |
| `+0x4E` | u16 | transition count: blend parameter, **not** a length |
| `+0x50` | u16 | phase offset |
| `+0x52` | u16 | **defer ticks** (controller-service ticks, not ms) |
| `+0x54` | u16 | priority |
| `+0x56` | u8  | parent reference count |
| `+0x57` | u8  | child reference count |

## 1.4 Confirmed state flag bits

| Bit | Meaning |
| --- | ------- |
| `0x00000001` | with `0x00008000`: end/goto family; without both: persistent restart family (§4.4) |
| `0x00000002` | transparent chained state: has no animation key, follows goto, does not remain logical current |
| `0x00000010` | a 12-byte deferred callback name is serialized |
| `0x00000020` | default state of its move |
| `0x00000040`/`0x00000100` | consumes the 0x18-byte orientation auxiliary block |
| `0x00000080`/`0x00000200` | consumes the 0x14-byte local-movement auxiliary block |
| `0x00008000` | resident transition/blend state: has no animation key, remains logical current for `transition_count` lifetime, animation comes from goto |
| `0x00080000` | input condition requires exact equality instead of the ordinary matcher |
| `0x00002000` | current state enables the owner-move fallback scan (Runtime 0x004A8BD0) |
| `0x00004000` | candidate requirement of the owner-move fallback scan; fallback candidates must have this flag |
| `0x00100000` | drop oldest input history entry on state entry |
| `0x01000000` | reset input history to no-input sentinel on state entry |
| `0x00400000` | drop oldest input history entry on state exit |
| `0x04000000` | reset input history to no-input sentinel on state exit |
| `0x00200000` | transient callback-helper flag (part of transient helper pass semantics) |
| `0x10000000` | clear current-input latch to 0 on state entry |
| `0x20000000` | collapse input history before recording new change |
| `0x02000000` | consumes the neutral 0x28-byte auxiliary block |
| `0x40000000` | input-suppression producer flag: insert input_condition into suppression set (transient helper semantics) |

Unflagged bits are preserved without semantics.

## 1.5 Variable sections — Confirmed — Runtime/data

Variable data follows the complete fixed child array. Every pass walks
children in **serialized child order**:

1. **Animation keys** — `char[12]` for every state with
   `!(flags & 0x00008002)`. Canonical comparison is the CRT `_strupr`
   uppercase; the original spelling is preserved.
2. **Child references** — `child_ref_count` u32 authored **state IDs**.
3. **Parent references** — `parent_ref_count` u32 authored **state IDs**.
4. **0x18-byte auxiliary block** — states with
   `flags & (0x100 | 0x40)`. `+0x00/+0x04` f32 raw (neutral), `+0x08` Vec3
   **authored orientation delta** (confirmed), `+0x14` f32 raw (neutral).
5. **0x14-byte auxiliary block** — states with `flags & (0x200 | 0x80)`.
   `+0x00/+0x04` f32 raw (neutral), `+0x08` Vec3 **authored local movement
   delta** (confirmed; transformed through the live actor orientation).
6. **Callback names** — `char[12]` for states with `flags & 0x10`. These are
   deferred side-effect routines, not movement-state names.
7. **0x28-byte auxiliary block** — states with `flags & 0x02000000`. Parsed
   and preserved neutrally; semantics not required for adventure locomotion.
8. **Audio-marker block** — states with `animation_mode & 0x0008`:
   `u32 marker_count`, `u32 runtime pointer placeholder`, then
   `marker_count` records of 0x20 bytes:

   | Offset | Type | Meaning |
   | ------ | ---- | ------- |
   | `+0x00` | f32 | sync duration |
   | `+0x04` | f32 | active start |
   | `+0x08` | f32 | active end |
   | `+0x0C` | f32 | **one-shot phase** |
   | `+0x10` | u32 | sound property (raw) |
   | `+0x14` | u16 | synced sound ID |
   | `+0x16` | u16 | **one-shot sound hID** (SCX `DEAD0003` hID, not an index) |
   | `+0x18` | u8  | attachment selector |
   | `+0x19` | u8  | marker flags |
   | `+0x1A` | u16 | raw |
   | `+0x1C` | f32 | raw scalar |

   Only the ordinary one-shot locomotion mode (one-shot phase + hID, other
   fields zero) is consumed by OpenNomad's Phase 4.1 controller.
9. **Embedded 3DA streams** — one `u32 size + byte[size]` payload per **first
   occurrence of a unique canonical animation key**, in serialized child
   order. Payloads are ordinary 3DA and decode with `Animation3DA::load`.
   Later states with an equal (case-insensitive) key reuse the same immutable
   resource. A correct generic parser reaches EOF after the last payload.

H1AVNT game-data verification (data-only facts, never production constants):
57 moves, 274 child states, 126 key-bearing states, 81 unique embedded 3DAs,
65 dynamic marker states; the flag rules select move 100 ("A_Moves") and its
default state ("H_Stand").

---

# 2. Resource loading and caching

- Runtime loads character control sets from `ANIMS/<authored name>.CTL`
  (**Confirmed — Runtime**); the IAM definition names the set without an
  extension (e.g. `H1AVNT` → `ANIMS/H1AVNT.CTL`), resolved through the
  platform's case-insensitive lookup.
- The immutable parsed bank is shared; Runtime cached banks in four native
  slots — a fixed cache size with no observable gameplay semantic, which
  OpenNomad replaces with a name-keyed shared-resource cache
  (**OpenNomad-only**).
- Move/state references resolve by exact authored ID; malformed or truncated
  sections and unresolved references are structured parse/link errors.

---

# 3. Canonical input and conditions

## 3.1 Canonical input mask — Confirmed — Runtime

CTL conditions operate on a 32-bit canonical mask. Fourteen ordinary positive
action bits exist: slot `n` = `1 << n` for slots 0..13. The canonical
no-input sentinel is `0x40000000`; no ordinary action sets it. Required-not-
held inputs encode at bit `1 << (slot + 15)`.

Runtime's profile-0 retail keyboard defaults: slot 0 = Left, 1 = Right,
2 = Up, 3 = Down, 4 = E, 5 = R, 6 = D, 7 = F, 8 = Left Ctrl, 9 = Space,
10 = G, 11 = H, 12 = Left Shift, 13 = Tab.

Recovered H1 adventure meanings relevant to basic locomotion
(**Confirmed — data**): slot 0 left turn, 1 right turn, 2 forward,
3 backward/turn-around path, 10 strafe modifier, 11 run. Other slots cover
action/use, jump, head/look, sneak/device and combat actions; the canonical
mask is lower-level than any one bank's interpretation.

Controller/input flag `0x08` swaps slots 0 and 1 (controller/profile option).

## 3.2 Condition matcher — Runtime 0x004A8AD0 — Confirmed — Runtime

```cpp
bool condition_matches(uint32_t authored, uint32_t current)
{
    for (unsigned slot = 0; slot < 14; ++slot) {
        const uint32_t positive = 1u << slot;
        const uint32_t negative = 1u << (slot + 15u);
        if ((authored & negative) != 0 && (current & positive) != 0)
            return false;
    }
    if ((((authored ^ current) & authored) & 0x00007FFFu) != 0)
        return false;
    if (authored == 0x80000000u)
        return current <= 0x00002000u;
    return true;
}
```

Note that the `0x40000000` no-input sentinel is numerically above the special
condition's `0x2000` ceiling, so `0x80000000` does not match it.

## 3.3 Input history and suppression — Confirmed — Runtime

Two separate mechanisms exist:

### Input History
A fixed **16-entry chronological input history** (oldest→newest, not reversed):
- Initialized at state `history[0] = 0x40000000` (no-input sentinel), count 1
- Records canonical input *changes* at `history[count]`
- At capacity 16, the oldest entry is shifted out when a new change occurs
- **State-driven mutations** (flags applied on state entry/exit):
  - `0x00100000` (entry): reset to NO_INPUT singleton, clear count → 0
  - `0x01000000` (entry): reset to NO_INPUT singleton, clear count → 0 (same semantic as 0x100000)
  - `0x10000000` (entry): clear latch to 0 (no longer hold the current input)
  - `0x20000000` (pre-record): if count > 1, collapse to single NO_INPUT entry before recording change; if count == 1 and entry is NO_INPUT, clear history
  - `0x00400000` (exit): drop oldest entry via left-shift before next state activation
  - `0x04000000` (exit): drop oldest entry via left-shift before next state activation (same as 0x00400000)

### Input Suppression Set
A fixed **20-entry sparse suppression set** with first-empty-slot insertion:
- Each nonzero canonical mask in the set that still matches the current input strips its bits (`current &= ~mask`)
- An entry that no longer matches is removed via swap-and-pop (no auto-compaction, sparse slots allowed)
- Duplicate masks are rejected (only first insertion of unique mask succeeds)
- Zero masks (0) are always ignored
- At capacity 20, no new entries are added (silent overflow)
- Uniqueness is the controlling constraint: re-entering the suppression set uses first-empty slot if available

---

# 4. Controller execution

## 4.1 Lifecycle — Confirmed — Runtime

Controller initialization (0x0045A700/0x0045A920) clears mutable state, seeds
the canonical no-input input, selects the **default move** (0x0046AD90: first
move with `flags & 0x00000001`) and that move's **default state**
(0x0047DD40: first child with `flags & 0x00000020`), and logically activates
it at phase 1. The controller exists and holds a current move/state **while
disabled** — this is how a scripted cinematic can play over an
already-initialized adventure controller.

- Compact `0x3F` (`StartCurrentCharacterMove`): 0x0041B6F0 → current
  character's CTL bank → exact move-ID lookup 0x0046ACE0 → 0x0045A630:
  input-history/transient reset, no-input reseed, restart phase 1, default
  child activation, current move switch. The operand is a **move ID**, never
  a state ID. Works while enabled or disabled.
- Compact `0x68`/`0x69`: 0x004050A0 → 0x0041BD10 → 0x00468DA0 (and the
  disable counterpart). They only gate whether the existing controller
  participates in the character update — no repositioning, no transform
  reset, no explicit state selection, no bank load, no pose clear. On the
  first enabled service the current CTL state's authored animation replaces
  the completed cinematic pose.

Direct player control uses the native `0x81` flag family: the same-state
restart count is forced to zero and MDSTAND's autonomous wait diversion is
suppressed by bit `0x80`. Input is continuously driven from the player
profile.

## 4.2 Animation start and phase domain — Confirmed — Runtime

- `segment_count = animation_mode >> 12`. `<= 1` uses ordinary 3DA playback;
  `> 1` (observed `0x6011`/`0x6019`/`0x9011`/`0x9019`) selects a
  packed/segmented sampler belonging to advanced interaction states. These
  high nibbles are **not** loop counts. Phase 4.1 parses them and fails
  safely with a diagnostic when reached.
- CTL animation starts at **phase 1.0**, not zero. One logical phase advance
  (0x0045C680) runs per 30 Hz controller tick; the transition evaluator sees
  the exact most-recently-traversed interval. Authored windows are animation
  phase/frame values, not milliseconds or percentages.
- Ordinary locomotion modes are `0x0001`/`0x0009` (**Confirmed — data**).
- Direct start 0x0045C3B0, authored transition/blend 0x0045C510, effective
  animation end 0x0045D220.

## 4.3 Transition evaluator — Runtime 0x004A8BD0 — Confirmed — Runtime

Inputs: controller, current state, current canonical input, two independent
candidate flag filters, and a timing switch.

1. **Traversal**: current state's child refs in authored order; reversed when
   the current state's `animation_mode & 0x0020`.
2. **Flag filters**: a filter of `-1` is disabled; otherwise the candidate
   requires `candidate.flags & filter != 0`.
3. **Input**: the §3.2 matcher; candidates with `flags & 0x00080000` use
   exact equality instead.
4. **Timing** (when enabled), against the traversed interval:

```cpp
bool timing_matches(float previous, float current, float start, float end)
{
    if (start == 0.0F && end == 0.0F) return true;
    if (current < start) return false;
    if (previous > end) return false;
    if (previous >= start) return true;
    return current >= end;
}
```

   This is deliberately crossing-aware; do not simplify to interval overlap.
5. **Priority mode** (controller flag `0x00000400`): candidates above the
   current threshold are rejected; a candidate equal to the threshold returns
   immediately; otherwise the highest numerical priority below the threshold
   wins with authored order breaking ties. Without priority mode the first
   valid candidate wins.
6. **Owner-move fallback**: when no child candidate was selected and the
   current state has `flags & 0x00200000`, scan every state of the owner move
   in contiguous authored order; fallback candidates additionally require
   `flags & 0x00400000`, then the same input/timing/priority predicates
   apply.

**Deferred transitions**: a selected candidate with `defer_ticks != 0` is
stored as pending with a counter starting at 1; later logical services
increment it, and the transition becomes eligible when
`pending_tick_count > defer_ticks`, then clears. These are controller-service
ticks, not milliseconds.

## 4.4 State end behavior — Confirmed — Runtime

- `(state.flags & 0x00008001) == 0`: persistent/restart family. At the
  effective animation end with no winning transition, the **same state
  re-enters at phase 1.0**. This is how standing/walk/run loop — there is no
  3DA loop boolean; the CTL state machine loops by state re-entry.
- Otherwise (end/goto family): at animation end without an interrupting
  transition, activate `goto_state` using `transition_value` as the authored
  phase seed.

## 4.5 State activation — Runtime 0x004A7B80 — Confirmed — Runtime

Central activation performs same-state restart bookkeeping
(`new == old ? ++count : count = 0`; direct-control `0x81` forces zero),
installs the state's animation resource, seeds the phase, computes the
effective end, queues the authored callback (deferred, capacity 10), resets
per-execution audio-marker firing state, and preserves null-animation
transition states correctly (they never blindly reset the character pose).

## 4.6 Pose application and root motion — Confirmed — Runtime/data

- Channels map to model objects by `Animation3DAChannel::channel_id ==
  MeshDescriptor::script_id` — never by vector index or `mesh_id`.
- CTL animation sample 0 is a reference value, **not** a world anchor: root
  motion integrates only intervals 1..N through
  `Animation3DAChannel::integrate_translation(previous, current)` and the
  result is transformed through the character's live root orientation before
  updating the candidate character position. Lateral (sidestep) displacement
  is exactly this generic root motion; there is no separate strafe velocity.
- The orientation auxiliary block applies its authored principal Euler delta
  with angle wrapping (helpers 0x0045C080/0x0045C1B0); the local-movement
  block transforms its local vector through the live orientation into the
  candidate position (0x0045C2F0).
- OpenNomad's Phase 4.1 motion model keeps a candidate/accepted position
  pair: CTL updates the candidate full XYZ and Phase 4.1 accepts it directly;
  Phase 4.2 inserts gravity/collision/floor resolution between the two
  (**OpenNomad-only** seam).

## 4.7 Callbacks — Runtime 0x0045D0E0 queue — Confirmed — Runtime

Callbacks are queued at state activation and dispatched by a later
character/frame service — never recursively inside transition evaluation.
Unknown names log once and remain nonfatal. Recovered subset:

| Name | Address | Behavior |
| ---- | ------- | -------- |
| `MDSTAND` | `0x0046BF90` | without direct-control bit `0x80` and in adventure mode 1: `restart_count > 10` alternates move IDs 43/44; resets both restart snapshots |
| `MDWALK`  | `0x0046C050` | `walk_snapshot = restart_count`; no movement |
| `MDSTOPW` | `0x0046C070` | `run_snapshot > 30 && walk_snapshot < 3` selects move 166; resets both |
| `MDRUN`   | `0x0046C0C0` | `run_snapshot = restart_count`; no movement |
| `MDSTOPR` | `0x0046C0E0` | `run_snapshot > 30` selects move 164; resets run snapshot |
| `RSTAVNT` | `0x0046C120` | adventure mode = 1, principal orientation X = 0 (**yaw preserved**), input profile 0 (reseeds no-input history) |
| `MDROT000`| `0x0046C170` | sets the transient suppress-automatic-movement-heading flag; does **not** rotate |

The move IDs 43/44/164/166 are part of these callbacks' own recovered native
behavior, not generic controller constants.

## 4.8 Animation audio markers — Runtime 0x0045ADF0/0x0045B260 — Confirmed — Runtime/data

States with `animation_mode & 0x0008` carry the marker block of §1.5. A
one-shot marker fires once per state execution when the current animation
interval crosses its authored phase (`previous < phase && current >= phase`);
state re-entry resets the fired set. The authored sound reference is an SCX
`DEAD0003` **hID** (lookup 0x0048CC80), never a sound-table index; the
spatial origin for the ordinary zeroed attachment-selector mode is the live
character world position. No CTL-specific attenuation metadata is recovered
for Phase 4.1; OpenNomad uses the general scenario spatial-sound defaults.

---

# 5. Structured camera frame ownership — around Runtime 0x00420399

The structured selected camera is **frame-published** state, not persistent
state: each scenario update clears the scene's selected camera, services
structured scripts (active `Script_SelectCamera` / camera-editing commands
republish), and publishes the result to the live camera controller. A script
that stopped publishing leaves no stale selection. When no structured camera
is live and controller mode 13 is active with an established player
character, the camera controller logically switches to mode 0 (the normal
automatic player camera; its follow mathematics are Phase 4.3).

---

# 6. Recovered address inventory

| Address | Role |
| ------- | ---- |
| `0x0041B6F0` | compact `0x3F` current-character move native path |
| `0x004050A0` / `0x0041BD10` / `0x00468DA0` | controller enable path (compact `0x68`) |
| `0x0045A630` | current move installation |
| `0x0045A700` / `0x0045A920` | controller initialization family |
| `0x0045A9A0` | input-history reset (16 entries, `0x40000000` seed) |
| `0x0045ADF0` / `0x0045B260` | animation audio-marker service |
| `0x0045BFF0` | input profile selection |
| `0x0045C080` / `0x0045C1B0` | authored orientation helpers |
| `0x0045C2F0` | authored local movement helper |
| `0x0045C3B0` | direct animation start |
| `0x0045C510` | authored animation transition/blend |
| `0x0045C680` | animation advance (one logical phase per tick) |
| `0x0045D0E0` | deferred callback queue (capacity 10) |
| `0x0045D220` | effective animation end |
| `0x0045D970` | CTL bank loading/cache family |
| `0x0046ACE0` | exact move-ID lookup |
| `0x0046AD90` | default move lookup (`flags & 1`) |
| `0x0046BF90` | `MDSTAND` |
| `0x0046C050` | `MDWALK` |
| `0x0046C070` | `MDSTOPW` |
| `0x0046C0C0` | `MDRUN` |
| `0x0046C0E0` | `MDSTOPR` |
| `0x0046C120` | `RSTAVNT` |
| `0x0046C170` | `MDROT000` |
| `0x0047DD40` | default state lookup (`flags & 0x20`) |
| `0x0048CC80` | sound hID lookup |
| `0x004A7B80` | state activation |
| `0x004A8160` | controller update |
| `0x004A8AD0` | canonical condition matcher |
| `0x004A8BD0` | transition evaluator |
| `~0x00420399` | structured-camera frame ownership/clear |

---

# 7. Deliberately neutral fields

The following are parsed and preserved but have **no assigned semantics**:
header `+0x08` and `+0x10..+0x57`; move `+0x0C`/`+0x10`; state `+0x0C`,
`+0x34`, `+0x3C`; the 0x28-byte auxiliary block; the leading floats of both
auxiliary vector blocks; and every marker field outside the ordinary one-shot
locomotion pair. Segmented `6`-/`9`-way animation modes
(`0x6xxx`/`0x9xxx`) are parsed and diagnosed but not sampled.
