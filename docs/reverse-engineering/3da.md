# Omikron `.3DA` hierarchical animation format

> **Status:** work-in-progress reverse-engineering documentation for OpenNomad  
> **Last updated:** 2026-08-22
>
> This document describes the `.3DA` hierarchical body/object animation format
> used by the Windows retail release of *Omikron: The Nomad Soul*.
>
> The format itself is compact:
>
> - an 8-byte header;
> - a fixed `0x28`-byte track descriptor for each animated object/channel;
> - optional position-key streams;
> - optional quaternion-key streams.
>
> The difficult part is not parsing those bytes. The important reverse-
> engineering work is understanding what Runtime does with the keys:
>
> - track IDs bind to 3DO object `scriptID` values;
> - key 0 is reference/rest data and is not a normal playable body-animation
>   frame;
> - rotations are selected discretely by frame, with no interpolation in the
>   recovered body-animation path;
> - position keys after key 0 are per-frame **root-motion increments**, not
>   absolute positions;
> - only the top/root hierarchy object contributes position motion in the
>   recovered body-animation path.
>
> This file is authoritative for the 3DA binary format and its currently
> recovered Runtime semantics.
>
> Related documentation:
>
> - [`3do.md`](3do.md) — 3DO object hierarchy and the `scriptID` used for 3DA
>   track binding;
> - [`3dp.md`](3dp.md) — path data used together with 3DA by relative body
>   animation;
> - [`scx.md`](scx.md) — `DEAD0001` animation descriptors and embedded-resource
>   framing;
> - [`script-opcodes.md`](script-opcodes.md) — structured script instances and
>   scheduling;
> - [`iam-script-functions.md`](iam-script-functions.md) —
>   `SelectBodyAnimation`, `SelectRelativeBodyAnimation`, and related IAM
>   functions;
> - [`runtime-coordinate-math.md`](runtime-coordinate-math.md) — Runtime-native
>   units, transform conventions, and row-vector matrix math.

---

# 1. Evidence model

Sources are used in this order:

1. **`Runtime.exe` behavior** — authoritative for loading, relocation, track
   binding, frame selection, root-motion integration, quaternion conversion,
   hierarchy application, and validation.
2. **Retail data**, particularly the `INTRO1.3DA`, `INTRO2.3DA`, and
   `INTRO3.3DA` resources embedded in `Grid.SCX`.
3. **OpenNomad implementation/tests** — used to express and validate the
   recovered model, but subordinate where an implementation convenience differs
   from Runtime.
4. **Community/importer material** — useful only when independently corroborated
   by Runtime or retail data.

Analyzed Runtime build:

```text
File:             Runtime.exe
Architecture:     PE32 / i386
Image base:       0x00400000
Linker timestamp: 1999-10-04 20:31:50
SHA-256:          55f7120bfea7891b048c64e3682f3259cdbf2719a43fa24e42254b753c95d2ef
```

Confidence labels:

- **Confirmed — Runtime:** directly established by executable behavior.
- **Confirmed — data:** directly established from retail bytes.
- **Corroborated:** Runtime and retail data agree.
- **Strongly reconstructed:** several independent observations agree but the
  original source-level name is unavailable.
- **Provisional:** current best interpretation requiring further tracing.
- **Unknown:** serialized field/behavior exists but meaning is not recovered.

---

# 2. Terminology

Runtime diagnostics use the term:

```text
TrackData
```

and one diagnostic names the position stream:

```text
PosKey
```

This document therefore prefers **track** and **position key** for the binary
format.

OpenNomad currently uses the synonym:

```text
Animation3DAChannel
```

for a decoded track.

Terminology used below:

| Term | Meaning |
|---|---|
| **animation** | one complete 3DA payload |
| **track / TrackData** | one `0x28` descriptor associated with a 3DO object |
| **track ID** | dword at track `+0x00`, used to bind to 3DO `scriptID` |
| **position key** | `float3` record in the optional position stream |
| **rotation key** | `float4` quaternion record in the optional rotation stream |
| **frame 0 / key 0** | reference/rest key, not a normal body-animation playback frame |
| **playable frame** | body-animation frame in the range `1..lastFrame` |
| **root motion** | accumulated translation produced from position keys `1..N` |

---

# 3. Overview

A 3DA payload has no magic and no version field.

Its serialized shape is:

```text
offset 0
    |
    v
+----------------------------------+
| u32 lastFrame                    | +0x00
| u32 trackCount                   | +0x04
+----------------------------------+
| TrackData[trackCount]            | +0x08
| each 0x28 bytes                  |
+----------------------------------+
| optional position-key streams    |
| optional rotation-key streams    |
| ...                              |
+----------------------------------+
| end of payload                   |
+----------------------------------+
```

Every stream is located through an offset stored in its track descriptor.

The offsets are relative to:

```text
the beginning of the 3DA payload
```

not:

- the track descriptor;
- the end of the descriptor table;
- the containing SCX file;
- the enclosing SCX resource header.

A zero stream offset means:

```text
no stream
```

even if the associated serialized count is non-zero.

---

# 4. No magic or embedded version

Unlike `.3DO`, a 3DA does not begin with:

```text
"OD3X"
```

or another known signature.

The first two dwords are meaningful animation data.

Consequences:

1. a raw 3DA payload cannot be identified reliably by a four-byte magic scan;
2. the containing SCX descriptor/resource framing is important when extracting
   embedded animations;
3. a standalone parser must be given the correct resource boundaries;
4. file-format versioning, if it existed in the authoring toolchain, is not
   represented in the currently recovered payload header.

---

# 5. Top-level header — `0x08` bytes

```c
struct Serialized3DAHeader {
    uint32_t lastFrame;    // +0x00
    uint32_t trackCount;   // +0x04
}; // 0x08
```

## 5.1 `+0x00`: last playable frame index

OpenNomad currently names this:

```text
max_frame_index
```

Runtime treats it as the upper valid frame/progress bound in the recovered body
animation path.

Retail evidence strongly establishes the indexing model:

```text
header.lastFrame = N

playable body frames:
    1 .. N

common rotation-key count:
    N + 1

key 0:
    reference/rest key
```

Examples from `Grid.SCX`:

| Animation | `lastFrame` | Common rotation-key count |
|---|---:|---:|
| `INTRO1.3DA` | `185` | `186` |
| `INTRO2.3DA` | `31` | `32` |
| `INTRO3.3DA` | `55` | `56` |

This is why `lastFrame` is more precise than simply calling the field a
“frame count”.

For the recovered body-animation path:

```text
number of playable frame intervals = lastFrame
highest playable integer frame     = lastFrame
stored key indices                  = 0..lastFrame
```

## 5.2 `+0x04`: track count

Number of `0x28`-byte `TrackData` descriptors immediately following the header.

Runtime iterates the descriptors with stride:

```text
0x28
```

---

# 6. TrackData descriptor — `0x28` bytes

Serialized layout:

```c
struct Serialized3DATrack {
    uint32_t trackId;             // +0x00
    char     name[20];            // +0x04

    uint32_t positionKeyCount;    // +0x18
    uint32_t positionKeysOffset;  // +0x1C

    uint32_t rotationKeyCount;    // +0x20
    uint32_t rotationKeysOffset;  // +0x24
}; // 0x28
```

| Offset | Size | Type | Meaning | Confidence |
|---:|---:|---|---|---|
| `0x00` | 4 | `u32` | track/object binding ID | Confirmed |
| `0x04` | 20 | `char[20]` | track name | Confirmed |
| `0x18` | 4 | `u32` | position-key count | Confirmed |
| `0x1C` | 4 | `u32` | payload-relative position-key offset; `0` = absent | Confirmed |
| `0x20` | 4 | `u32` | rotation-key count | Confirmed |
| `0x24` | 4 | `u32` | payload-relative rotation-key offset; `0` = absent | Confirmed |

---

# 7. Track ID and 3DO object binding

The field at track `+0x00` is the runtime binding key.

Runtime code around:

```text
0x00471040
0x00470FE0
```

walks the 3DO object hierarchy and, for each object, reads the serialized 3DO
field:

```text
object +0x0C
```

documented in [`3do.md`](3do.md) as:

```text
scriptID
```

It scans 3DA tracks and compares:

```text
track.trackId == object.scriptID
```

When a match is found, Runtime stores the track pointer in the object's mutable
runtime state.

Therefore, in the normal recovered body-animation path:

```text
3DA track ID
    ==
3DO object scriptID
```

The track name is not required for this binding.

## 7.1 Names are descriptive, IDs are authoritative

Retail track names clearly mirror body-part/object names, but Runtime binds by
numeric ID.

Do not implement animation binding as:

```cpp
if (track.name == object.name)
```

when the numeric IDs are available.

## 7.2 Rebased ID variant

Runtime has evidence of a related path involving a global ID base around:

```text
0x006A50A4
```

The normal wrapper used by the currently recovered body-animation path clears
that base before matching.

Until the alternate path is fully mapped, the authoritative normal rule is:

```text
trackId == object.scriptID
```

without rebasing.

---

# 8. Track names

Track names occupy exactly:

```text
20 bytes
```

They are generally NUL-terminated/padded when shorter.

`Grid.SCX` intro animations contain 19 tracks:

```text
ID  Name
--  --------
 0  UAvantd
 1  UAvantg
 2  UBassin
 3  UBrasd
 4  UBrasg
 5  UBuste
 6  UCou
 7  UCuissed
 8  UCuisseg
 9  UEpauled
10  UEpauleg
11  UJambed
12  UJambeg
13  UMaind
14  UMaing
15  UPiedd
16  UPiedg
17  UTete
18  UVentre
```

The same ID/name set occurs in all three examined intro animations.

The strings are useful for diagnostics and forensic matching.

No specific text encoding beyond fixed-width 8-bit strings should be baked into
the format definition until localized asset evidence establishes one.

---

# 9. Stream offsets and load-time relocation

Runtime loads the entire 3DA payload into one backing allocation.

It then relocates stream offsets in place.

For every track:

```text
if track.positionKeysOffset != 0:
    track.positionKeysOffset += payloadBase

if track.rotationKeysOffset != 0:
    track.rotationKeysOffset += payloadBase
```

After relocation, those two fields are live pointers.

This establishes several format facts.

## 9.1 Offsets are payload-relative

Serialized value:

```text
0x1A40
```

means:

```text
payloadBase + 0x1A40
```

not:

```text
trackAddress + 0x1A40
```

## 9.2 Zero is a null stream

Runtime only relocates non-zero offsets.

Thus:

```text
offset == 0
```

is the authoritative absence marker.

## 9.3 Count does not imply stream presence

Retail tracks can contain:

```text
positionKeyCount != 0
positionKeysOffset == 0
```

Runtime treats the position stream as absent because the pointer remains null.

This is important.

A parser must not do:

```cpp
if (positionKeyCount != 0)
    assume position data follows;
```

It should use:

```cpp
if (positionKeysOffset != 0)
    parse positionKeyCount records;
```

## 9.4 Serialized and runtime layouts differ

A modern implementation should not overwrite offsets with pointers.

Prefer:

```text
Serialized3DATrack
    offsets

Decoded3DATrack
    vectors/spans/references
```

---

# 10. Position-key stream

One position key is:

```c
struct PositionKey3DA {
    float x;
    float y;
    float z;
}; // 0x0C
```

Record size:

```text
12 bytes
```

Coordinates are Runtime-native 3D values:

```text
X, Y, Z
```

No axis swap belongs in the parser.

For body animation, values are in the same Runtime-native coordinate system used
by 3DO object transforms and world placement.

See:

[`runtime-coordinate-math.md`](runtime-coordinate-math.md)

---

# 11. Rotation-key stream

One rotation key is:

```c
struct RotationKey3DA {
    float w;
    float x;
    float y;
    float z;
}; // 0x10
```

Record size:

```text
16 bytes
```

Quaternion serialization order is:

```text
w, x, y, z
```

This is established by both Runtime's quaternion-to-matrix conversion and retail
key 0 values such as:

```text
(1.0, 0.0, 0.0, 0.0)
```

which produce the identity matrix.

---

# 12. Frame 0 is reference/rest data

The most important semantic property of the currently recovered 3DA body
animation format is:

```text
key 0 is special
```

For the retail intro animations:

- every rotation track has an identity quaternion at key 0;
- the root-position track contains a large authored/reference position at key 0;
- body rotation sampling clamps to frame 1 or later;
- root-motion integration begins with position key 1, never key 0.

The natural model is therefore:

```text
key 0:
    reference/rest/authored state

key 1:
    first playable body-animation frame / first root-motion interval

...

key N:
    final playable frame
```

This model explains both the serialized counts and Runtime behavior.

---

# 13. Rotation sampling in Runtime

Runtime callback around:

```text
0x00471690
```

applies the bound track's quaternion to a 3DO runtime object.

The recovered steps are:

1. obtain the object's bound 3DA track;
2. ignore objects with invalid/unbound script IDs as appropriate;
3. clamp requested progress to at least:

```text
1.0
```

4. convert positive progress to an integer frame by floor/truncation;
5. verify the frame index is below `rotationKeyCount`;
6. select:

```text
rotationKeys[frame]
```

7. convert that quaternion directly to the object's 3×3 animation matrix.

Conceptually:

```cpp
float frame = max(progress, 1.0f);
uint32_t index = floor(frame);

if (index < rotationKeyCount)
    object.animationMatrix =
        QuaternionToMatrix(rotationKeys[index]);
```

## 13.1 No quaternion interpolation in this path

Runtime does **not** interpolate between adjacent quaternion keys in this
recovered body-animation path.

For:

```text
progress = 12.75
```

the selected orientation is:

```text
rotationKeys[12]
```

not a SLERP or NLERP between frames 12 and 13.

This is an important fidelity rule.

## 13.2 Fractional script progress still matters

Even though rotation is discrete, fractional progress is still meaningful
because root-motion translation is integrated over the exact fractional
interval.

Thus one animation tick can simultaneously use:

```text
rotation:
    floor(currentProgress)

translation:
    integrate exact [previousProgress, currentProgress]
```

---

# 14. Quaternion-to-matrix conversion

Runtime routine around:

```text
0x00442A00
```

converts the four serialized quaternion floats directly to the object's
row-vector 3×3 matrix.

For quaternion:

```text
q = (w, x, y, z)
```

the recovered matrix is:

```text
[ 1 - 2(y² + z²)   2(xy - wz)       2(xz + wy)     ]
[ 2(xy + wz)       1 - 2(x² + z²)   2(yz - wx)     ]
[ 2(xz - wy)       2(yz + wx)       1 - 2(x² + y²) ]
```

Algebraically, these are the coefficients commonly shown for the conventional column-vector quaternion rotation matrix. That is noteworthy because Runtime's object transform path otherwise uses row vectors.

Do **not** transpose this matrix to compensate for that convention difference.

`0x00442A00` writes these coefficients into the mutable object animation state, and Runtime's hierarchy/vertex transform path consumes those coefficients as-is.

## 14.1 Runtime does not normalize first

The recovered routine uses the stored quaternion values directly.

There is no normalization step before matrix construction.

Retail intro data is already effectively normalized:

```text
maximum observed |length - 1|
    approximately 6.2e-7
```

So normalization is unnecessary for valid retail data.

## 14.2 OpenNomad fidelity requirement

`Runtime::quaternion_matrix()` must mirror `0x00442A00` directly:

```text
input component order:
    w, x, y, z

normalization:
    none

matrix convention adaptation:
    none
```

In particular, transposing the recovered matrix inverts every unit-quaternion rotation. Large joint rotations therefore become visibly mangled even while small torso/head rotations can still look superficially plausible.

---

# 15. Position key 0 is a reference position

Runtime helper around:

```text
0x00471100
```

scans tracks looking for a non-null position stream.

For the first suitable track it:

1. checks `positionKeysOffset/pointer != 0`;
2. checks `positionKeyCount > 0`;
3. copies the first three floats:

```text
positionKeys[0]
```

to the caller.

This independently proves that position key 0 has a distinguished role.

It is not simply the first motion delta.

---

# 16. Root-motion representation

For the recovered body-animation path, position keys after key 0 are
**per-frame motion vectors**.

The correct interpretation is:

```text
positionKeys[0]
    authored/reference root position

positionKeys[1]
    motion delta over interval (0, 1]

positionKeys[2]
    motion delta over interval (1, 2]

positionKeys[3]
    motion delta over interval (2, 3]

...
```

This is not:

```text
absolute root position at every frame
```

and it is not:

```text
positionKeys[i] - positionKeys[i - 1]
```

The serialized records themselves are the increments.

---

# 17. Root-motion integration

Runtime routine around:

```text
0x004711D0
```

integrates root motion between an old and current animation progress.

Let:

```text
a = oldFrame
b = currentFrame
```

with:

```text
0 <= a <= b
```

For each key index:

```text
i >= 1
```

define its frame interval:

```text
(i - 1, i]
```

Then:

```text
overlap_i =
    length(
        [a, b]
        intersect
        [i - 1, i]
    )
```

and accumulated displacement is:

```text
delta =
    Σ positionKeys[i] * overlap_i
```

for all overlapping intervals.

This naturally handles fractional progress.

---

# 18. Fractional root-motion example

Suppose:

```text
positionKeys[0] = reference only

positionKeys[1] = ( 2,  4,  6)
positionKeys[2] = ( 8, 10, 12)
positionKeys[3] = (14, 16, 18)
```

Integrate:

```text
oldFrame     = 0.5
currentFrame = 2.25
```

Overlap:

```text
key 1 interval [0,1]:
    0.5 frame

key 2 interval [1,2]:
    1.0 frame

key 3 interval [2,3]:
    0.25 frame
```

Result:

```text
X = 2*0.5  + 8*1.0  + 14*0.25
Y = 4*0.5  + 10*1.0 + 16*0.25
Z = 6*0.5  + 12*1.0 + 18*0.25
```

This matches the recovered Runtime behavior and the OpenNomad regression tests.

---

# 19. Why key 0 must not be integrated

`INTRO1.3DA`, root track `UBassin`, begins approximately:

```text
positionKeys[0]:
    X =   0.629919946
    Y = -158.188659668
    Z =  27.611776352

positionKeys[1]:
    X =  -0.018196702
    Y = -17.782073975
    Z =   0.0
```

Treating key 0 as a motion increment incorrectly moves the character by roughly:

```text
158 inches
```

vertically.

This exact mistake occurred during early OpenNomad body-animation work.

Runtime's `0x00471100` reference-key helper and `0x004711D0` integration logic
both establish that key 0 should not contribute to motion accumulation.

---

# 20. Root motion applies only to the top hierarchy object

Runtime body-animation application around:

```text
0x004715B0
```

does two conceptually separate things:

```text
hierarchy traversal:
    apply rotation tracks to bound objects

top/root object only:
    integrate position/root motion
```

The routine checks the runtime object's parent pointer and invokes position
integration only when the animated object is at the top of the relevant
hierarchy.

Thus:

```text
rotation:
    can exist on many body/object tracks

position/root motion:
    comes from the top/root animation track in the recovered body path
```

This matches the inspected `Grid.SCX` animations, where only `UBassin` carries a
non-null position stream.

---

# 21. Optional matrix transform on root delta

The root-motion integration routine around `0x004711D0` can apply an optional
3×3 matrix to the accumulated displacement before returning it.

Conceptually:

```text
raw integrated animation delta
    |
    | optional matrix
    v
transformed root delta
```
The multiplication uses Runtime's row-vector convention:

```text
worldDelta = integrated3DADelta * liveRootOrientation
```

The body-animation caller supplies the live top/root object orientation. This
is the actor's current world-facing basis; it is distinct from the animation
quaternion sampled for the current 3DA frame. Runtime writes that quaternion to
object animation state separately.

In OpenNomad the original top-level object state is split: actor/world placement
and orientation live in `RuntimeCharacter::transform`, while the 3DO hierarchy
keeps model/object animation state. The equivalent root-motion operation is:

```cpp
rootDelta =
    Runtime::transform_vector(integratedDelta, character.transform.matrix);

character.transform.translation += rootDelta;
```

Do not multiply the current root `animation_matrix` into this transform.

## 21.1 Absolute reseeding across executions

Ordinary `SelectBodyAnimation` calls the key-zero helper recovered around
`0x00471100` at every execution start. When the mutable previous-progress slot
wraps to zero, the top/root absolute position is therefore restored from the
3DA reference before samples `1..N` are integrated again.

`SelectRelativeBodyAnimation` shares the same repeat boundary, but its absolute
seed remains the sampled 3DP path position. This distinction prevents repeated
idle/talking animation passes from accumulating root increments indefinitely.

---

# 22. Body-animation validation in Runtime

The animation application path includes diagnostics:

```text
old frame should be inferior to current frame (Object : %s)
value for current frame is too big (Object : %s)
no animation applyed on object %s
object %s is not top hierarchy !
```

These establish several runtime rules.

## 22.1 Old progress must not exceed current progress

For the forward playback/integration path:

```text
oldFrame <= currentFrame
```

is expected.

## 22.2 Current progress is bounded by the animation header

Runtime checks current progress against:

```text
animation +0x00
```

which supports the `lastFrame` interpretation.

## 22.3 Animation tracks must be bound

An object can reach the animation application path without a matching track,
but Runtime has an explicit diagnostic for that state.

## 22.4 Hierarchy position matters

The top-object/root requirement is explicit enough to have its own diagnostic.

---

# 23. Position-stream absence is pointer-driven

Runtime diagnostics include:

```text
PtrTrackData->PosKey == NULL
PtrTrackData == NULL
```

The loader's relocation behavior and these diagnostics reinforce the correct
model:

```text
positionKeysOffset == 0
    ->
no position stream
```

This remains true even if:

```text
positionKeyCount > 0
```

Retail data demonstrates such combinations.

Do not normalize them away during parsing; preserving the serialized count can
help explain authoring/runtime conventions later.

---

# 24. Rotation-stream absence

The same relocation rule applies to the rotation stream:

```text
rotationKeysOffset == 0
    ->
no rotation stream
```

Only non-zero offsets are relocated.

A track can therefore describe:

```text
position only
rotation only
both
neither
```

although the inspected body-animation corpus is predominantly rotation-bearing,
with position reserved for the root track.

---

# 25. Stream ordering is offset-driven

Retail intro files happen to pack their streams tightly.

But the format should be parsed by offsets, not by assuming:

```text
all positions
then all rotations
```

or:

```text
track 0 data
track 1 data
track 2 data
...
```

Each descriptor independently locates:

```text
position stream
rotation stream
```

A conforming parser should therefore support arbitrary non-overlapping valid
stream order.

---

# 26. Stream sizes

For a track:

```text
position bytes =
    positionKeyCount * 12
```

when `positionKeysOffset != 0`.

Rotation:

```text
rotation bytes =
    rotationKeyCount * 16
```

when `rotationKeysOffset != 0`.

All multiplication and range arithmetic must be overflow-checked before slicing
the payload.

---

# 27. Runtime in-memory animation resource — `0x10` bytes

Runtime does not keep only the raw payload pointer.

The recovered animation-slot structure is approximately:

```c
struct RuntimeAnimation3DA {
    uint32_t lastFrame;          // +0x00
    uint32_t trackCount;         // +0x04
    TrackData* tracks;           // +0x08
    void* backingAllocation;     // +0x0C
}; // 0x10
```

Load sequence:

```text
allocate one block for serialized payload
read/copy payload
relocate TrackData stream offsets in place
fill one RuntimeAnimation3DA slot
```

The `tracks` pointer is effectively:

```text
backingAllocation + 8
```

because the descriptor array immediately follows the serialized header.

---

# 28. Runtime animation-slot pool

The recovered Runtime uses a fixed animation-resource pool approximately:

```text
0x0054ECB8 .. 0x00550CB8
```

with stride:

```text
0x10
```

yielding:

```text
512 slots
```

Runtime diagnostic:

```text
Plus de 512 anim, pas d'allocation possible...
```

confirms the limit.

This is a Runtime resource-management limit, **not part of the `.3DA` file
format**.

OpenNomad does not need to reproduce this fixed allocation strategy unless
strict runtime-limit fidelity is desired.

---

# 29. Runtime loaders

Two relevant loader paths are currently identified:

```text
0x0046E690  load/open animation resource
0x0046E880  load 3DA from an already-open/resource stream
```

The second path is used by SCX animation-resource loading.

The exact original function names are not recovered; the addresses and behavior
are more authoritative than reconstructed names.

## 29.1 Whole-payload allocation

Runtime reads the 3DA body into one memory allocation and relocates its offsets
in place.

This explains why serialized descriptor fields have a layout that can become a
direct runtime `TrackData` array.

## 29.2 Weak retail bounds assumptions

The recovered loader's minimum-size validation appears less strict than a
modern parser should be.

Retail code assumes valid shipped assets.

OpenNomad should not emulate unsafe under-validation.

---

# 30. SCX `DEAD0001` embedding

Inside SCX, animation descriptors live in:

```text
DEAD0001
```

Descriptor layout:

```c
struct ScxAnimationDescriptor {
    char     name[24];             // +0x00
    uint32_t runtimePlaceholder;   // +0x18
    uint32_t field1C;              // +0x1C
    uint32_t animationId;          // +0x20
}; // 0x24
```

Each descriptor corresponds to one generic SCX embedded-resource frame:

```text
u32 absoluteSelfOffset
u32 payloadSize
u8  threeDA[payloadSize]
```

The 3DA parser receives only:

```text
the payload bytes after the 8-byte SCX resource header
```

See [`scx.md`](scx.md).

---

# 31. SCX descriptor `+0x1C` is load-time scratch/state

This is an important correction to current OpenNomad assumptions.

In the recovered Runtime SCX animation-loader path around:

```text
0x00449B54
```

Runtime explicitly writes:

```text
descriptor +0x1C = 0
```

before loading the animation.

It then stores the resulting runtime animation pointer/handle at:

```text
descriptor +0x18
```

Consequences:

- `+0x1C` is not currently established as required semantic input to 3DA
  decoding;
- a non-zero serialized value does not automatically mean “unsupported
  animation format”;
- Runtime clears it in this load path before use.

## 31.1 Current OpenNomad fidelity issue

Current `ScenarioRuntime::animation_resource()` rejects descriptors when:

```text
serialized_field_1c != 0
```

That check is stricter than the recovered Runtime behavior and should be
revisited.

The 3DA format itself does not depend on SCX descriptor `+0x1C`.

---

# 32. SCX animation ID versus descriptor index

`DEAD0001` has an explicit:

```text
animationId
```

at descriptor `+0x20`.

That ID is not the same thing as the descriptor-array index.

`Grid.SCX`:

```text
descriptor index 0 -> animationId 3 -> INTRO1.3DA
descriptor index 1 -> animationId 4 -> INTRO2.3DA
descriptor index 2 -> animationId 5 -> INTRO3.3DA
```

Consumers must distinguish:

```text
SCX animation-table index
SCX animation numeric ID
3DA track ID
```

These are three separate namespaces.

---

# 33. `Grid.SCX` embedded animation resources

The current `Grid.SCX` resource stream contains:

| Name | SCX frame header | 3DA payload | Payload size |
|---|---:|---:|---:|
| `INTRO1.3DA` | `0x112D` | `0x1135` | `0xE898` / `59,544` |
| `INTRO2.3DA` | `0xF9CD` | `0xF9D5` | `0x2A80` / `10,880` |
| `INTRO3.3DA` | `0x12455` | `0x1245D` | `0x4820` / `18,464` |

Each payload begins directly with:

```text
u32 lastFrame
u32 trackCount
```

There is no nested 3DA magic/header inside the SCX frame.

---

# 34. `INTRO1.3DA` worked structure

Header:

```text
lastFrame  = 185
trackCount = 19
```

Descriptor table size:

```text
8 + 19 * 0x28
= 8 + 760
= 768
= 0x300
```

All tracks have:

```text
rotationKeyCount = 186
```

Only `UBassin` has a non-null position stream.

For `UBassin`:

```text
track ID            = 2
positionKeyCount    = 186
positionKeysOffset  = 0x1A40
rotationKeyCount    = 186
rotationKeysOffset  = 0x22F8
```

The complete payload accounting is:

```text
header + descriptors:
    0x300 bytes

19 * 186 rotation keys * 16 bytes:
    56,544 bytes

186 position keys * 12 bytes:
    2,232 bytes

total:
    59,544
    = 0xE898
```

This exactly matches the SCX framed payload size.

---

# 35. `INTRO2.3DA` and `INTRO3.3DA`

`INTRO2`:

```text
lastFrame  = 31
trackCount = 19

common rotationKeyCount = 32

UBassin:
    positionKeyCount   = 32
    positionKeysOffset = 0x0700
    rotationKeyCount   = 32
    rotationKeysOffset = 0x0880
```

`INTRO3`:

```text
lastFrame  = 55
trackCount = 19

common rotationKeyCount = 56

UBassin:
    positionKeyCount   = 56
    positionKeysOffset = 0x0A00
    rotationKeyCount   = 56
    rotationKeysOffset = 0x0CA0
```

Again:

```text
stored key count = lastFrame + 1
```

for the normal body tracks.

---

# 36. Identity key 0 in retail data

For every rotation track in all three inspected intro animations:

```text
rotationKeys[0] =
    (1.0, 0.0, 0.0, 0.0)
```

exactly.

This is unusually strong data evidence for the reference/rest-key model.

Runtime body sampling's minimum-frame-1 behavior independently confirms that
key 0 is deliberately skipped during normal playback.

---

# 37. Position streams in the inspected body animations

Across `INTRO1`, `INTRO2`, and `INTRO3`:

```text
only track "UBassin" has a non-zero position stream offset
```

All body-part tracks still carry rotation streams.

This gives the expected character-animation division:

```text
body hierarchy:
    rotations per object/bone

root/pelvis/top:
    rotations
    +
    translation/root motion
```

Do not generalize this to a format restriction: another 3DA could theoretically
contain position streams on additional tracks.

The file format supports one optional position stream per track.

---

# 38. Timing is not serialized in 3DA

The 3DA header contains no:

- FPS;
- duration in seconds;
- milliseconds-per-key;
- sample-rate field.

Playback time is supplied by the consumer.

The currently recovered script/runtime timing model uses:

```text
30 logical frames per second
```

and the body-animation handler advances its progress in those script-frame
units.

Thus in the current body path:

```text
one playable 3DA frame index
    corresponds to
one logical script-frame step
```

But that is **consumer behavior**, not an intrinsic encoded 3DA frame rate.

A generic 3DA decoder should expose frame indices, not bake in 30 Hz.

---

# 39. Relationship to SCX body-animation functions

The currently recovered New Game intro path demonstrates how 3DA participates in
the wider animation system.

Conceptually:

```text
IAM AREA event
    |
    v
character-bound SCX script
    +-- 0x02000004 SelectBodyAnimation
    |       +-- selected object's current runtime position
    |       +-- authored non-path offset
    |
    +-- 0x0200002A SelectRelativeBodyAnimation
            +-- 3DP path and subpath
            +-- authored offset
    |
    v
shared 3DO hierarchy + 3DA playback -> pose character + apply root motion
```

Both SCX functions supply a selected object binding, an animation-table index,
mutable previous/current progress, and the body-animation vector. They share
the same numeric 3DA channel-to-3DO-`script_id` binding, discrete rotation
sampling, interval root-motion integration, posed geometry rebuild, and 30 Hz
script progression.

`SelectRelativeBodyAnimation` additionally supplies:

- path-table index;
- subpath index;

The 3DA itself supplies:

- object/channel rotations;
- root-motion increments.

Only the relative function's 3DP supplies:

- path anchor/orientation information.

These responsibilities should remain separate in OpenNomad.

---

# 40. Current OpenNomad track binding

OpenNomad currently binds animation tracks to 3DO objects through:

```text
channel.channel_id
    ==
model.meshes[object].script_id
```

within the selected hierarchy.

That matches Runtime's normal binding rule.

A track that does not correspond to a selected hierarchy object remains
unapplied.

---

# 41. Current OpenNomad rotation behavior

Current:

```cpp
sample_rotation(progress)
```

implements approximately:

```text
frame = clamp(max(progress, 1), valid range)
index = floor(frame)
return rotations[index]
```

This matches the recovered Runtime body-animation path.

No interpolation should be added merely because quaternions are commonly
interpolated in modern engines.

Doing so would alter original animation timing/poses.

---

# 42. Current OpenNomad position APIs

OpenNomad currently exposes two position-related helpers:

```text
sample_translation(progress)
integrate_translation(previous, current)
```

These have different status.

## 42.1 `integrate_translation()` is Runtime-derived body behavior

It implements the recovered per-frame root-motion increment model and should be
considered the authoritative helper for body-animation root translation.

## 42.2 `sample_translation()` is a generic utility

It linearly interpolates between serialized `float3` records as if they were
absolute samples.

That can be useful for inspection or a future different animation consumer.

However:

```text
it is NOT the recovered body-animation root-motion semantics
```

Do not use `sample_translation()` as the definition of 3DA root movement.

The body path must integrate keys `1..N` as increments.

---

# 43. Current OpenNomad root-motion behavior

For a bound character:

1. rotation keys are applied to matching object runtime states;
2. the root object's position stream is integrated over:

```text
[previousProgress, currentProgress]
```

3. resulting delta is added to the character transform;
4. Runtime-style 3DO hierarchy transforms are recomputed;
5. posed geometry is rebuilt/rendered.

This is broadly aligned with the recovered Runtime flow.

The known remaining distinction is the optional matrix transform described in
section 21.

---

# 44. 3DA does not contain a skeleton

The hierarchy is not serialized in 3DA.

3DA stores:

```text
tracks keyed by IDs
```

The actual object/bone hierarchy comes from:

```text
3DO
```

Therefore the animation resource cannot independently tell you:

- which track is root;
- which track is a child;
- parent/child order;
- which geometry belongs to which joint;
- which node is joint-only.

Those facts are resolved against the target 3DO.

This is an important architectural distinction.

---

# 45. 3DA does not contain model-space bind matrices

No separate per-track bind/inverse-bind matrix has been identified in the 3DA
payload.

The recovered system relies on:

- 3DO hierarchy;
- 3DO local/bone positions;
- 3DA per-frame quaternion matrices;
- 3DA root-motion position deltas.

Do not invent a modern skeletal-animation bind-pose structure inside 3DA.

---

# 46. 3DA does not encode interpolation modes

No per-track or per-key interpolation-mode field is present in the recovered
format.

Recovered body behavior is hard-coded by Runtime:

```text
rotation:
    discrete frame selection

root translation:
    piecewise constant velocity represented by per-frame deltas,
    integrated fractionally across frame intervals
```

Any modern interpolation added beyond this is a renderer/engine choice, not a
decoded format property.

---

# 47. Safe parser model

A robust parser should:

1. require at least 8 bytes;
2. read `lastFrame` and `trackCount`;
3. overflow-check:

```text
8 + trackCount * 0x28
```

4. parse every descriptor before following any stream;
5. preserve all serialized counts and offsets;
6. treat offset 0 as no stream;
7. for a non-zero position offset:
   - overflow-check `count * 12`;
   - verify complete span lies in payload;
8. for a non-zero rotation offset:
   - overflow-check `count * 16`;
   - verify complete span lies in payload;
9. permit a non-zero count with a zero offset;
10. not require streams to be contiguous;
11. not require streams to be ordered by track;
12. not require `keyCount == lastFrame + 1` as a universal parser invariant;
13. preserve duplicate IDs/names for diagnostics rather than silently choosing
    one without policy;
14. leave hierarchy validation to the 3DO binding consumer.

---

# 48. Why `keyCount == lastFrame + 1` should not be a hard parser rule

It is strongly true for the currently inspected normal rotation streams.

However the format stores an explicit count for each track and stream.

That redundancy would be unnecessary if the count were always mechanically:

```text
lastFrame + 1
```

Retail position descriptors also demonstrate odd states such as non-zero counts
with null stream offsets.

Therefore a safe decoder should trust:

```text
the explicit per-stream count
```

for bounds, while the animation consumer separately validates whether enough
keys exist for a requested frame.

---

# 49. Duplicate track IDs

Runtime's normal binding callback scans tracks and stops when it finds a
matching ID.

A malformed animation with duplicate track IDs could therefore have
order-dependent behavior.

Retail data examined so far uses unique IDs.

OpenNomad should diagnose duplicates in development tooling even if a low-level
decoder chooses to preserve them.

Do not silently rewrite or renumber IDs.

---

# 50. Track order

The inspected intro animations use ascending IDs:

```text
0..18
```

and stable body-part order.

This is convenient but not required by the recovered binary structure.

Binding should use `trackId`, not descriptor position.

---

# 51. Quaternion validity

A safe decoder can optionally report:

- zero-length quaternion;
- NaN/Inf components;
- materially non-unit magnitude.

But the low-level format parser should normally preserve raw floats.

Runtime does not normalize the key before conversion.

A validation layer can distinguish:

```text
malformed/suspicious retail data
```

from:

```text
format parser failure
```

---

# 52. Position-key validity

Likewise, position keys are raw float triples.

The parser should detect non-finite floats for diagnostics if desired, but not
apply:

- centimetres-to-inches conversion;
- axis swapping;
- root subtraction;
- absolute-position reconstruction.

Those are consumer/semantic decisions.

The inspected root-motion values are already in the Runtime-native coordinate
space expected by the body-animation routines.

---

# 53. File size and unused bytes

Because streams are explicitly offset-based, the low-level format does not
require the final stream to end exactly at EOF as a universal structural rule.

The supplied SCX payloads do pack data to their exact framed size.

A strict forensic validator can report:

```text
unreferenced gaps
overlapping streams
unreferenced tail bytes
```

without assuming every such case is invalid before a larger retail corpus is
checked.

---

# 54. Stream overlap

No intentional overlapping 3DA position/rotation streams are known.

A robust parser should detect overlap for diagnostics.

Do not assume overlap is a supported compression/aliasing feature without
Runtime evidence.

Zero offsets are the known representation for missing streams.

---

# 55. Endianness and scalar representation

Current Windows retail format:

```text
little-endian integers
IEEE-754 32-bit floats
```

Quaternion component order:

```text
w, x, y, z
```

Position component order:

```text
x, y, z
```

No byte packing or quantization has been identified.

---

# 56. Documentation-oriented structures

```c
#pragma pack(push, 1)

typedef struct Serialized3DAHeader {
    uint32_t lastFrame;       // +0x00
    uint32_t trackCount;      // +0x04
} Serialized3DAHeader;        // 0x08

typedef struct Serialized3DATrack {
    uint32_t trackId;              // +0x00
    char     name[20];             // +0x04

    uint32_t positionKeyCount;     // +0x18
    uint32_t positionKeysOffset;   // +0x1C, payload-relative, 0 = absent

    uint32_t rotationKeyCount;     // +0x20
    uint32_t rotationKeysOffset;   // +0x24, payload-relative, 0 = absent
} Serialized3DATrack;              // 0x28

typedef struct PositionKey3DA {
    float x;
    float y;
    float z;
} PositionKey3DA;                  // 0x0C

typedef struct RotationKey3DA {
    float w;
    float x;
    float y;
    float z;
} RotationKey3DA;                  // 0x10

#pragma pack(pop)
```

These describe serialized bytes only.

Do not add Runtime's relocated pointers to the on-disk definitions.

---

# 57. Recommended OpenNomad representation

A clean model is:

```cpp
struct AnimationTrack {
    uint32_t id;
    std::string name;

    uint32_t serializedPositionCount;
    uint32_t serializedPositionOffset;

    uint32_t serializedRotationCount;
    uint32_t serializedRotationOffset;

    std::vector<Vec3> positionKeys;
    std::vector<Quaternion> rotationKeys;
};

struct Animation3DA {
    uint32_t lastFrame;
    std::vector<AnimationTrack> tracks;
};
```

For higher-level playback, keep separate mutable state:

```cpp
struct BodyAnimationPlayback {
    const Animation3DA* animation;
    float previousProgress;
    float currentProgress;
    ...
};
```

Do not mutate the decoded animation resource as playback advances.

---

# 58. Runtime versus OpenNomad summary

| Behavior | Runtime | Current OpenNomad |
|---|---|---|
| offsets | relocated to pointers in-place | decoded into vectors |
| position offset 0 | null stream | absent stream |
| track binding | ID == 3DO `scriptID` | same |
| key 0 body rotation | skipped | skipped |
| rotation sampling | floor frame, min 1 | same |
| quaternion interpolation | none | none |
| quaternion normalization | none | currently normalizes before matrix |
| root position key 0 | reference only | excluded from integration |
| root translation 1..N | per-frame deltas | same |
| fractional root motion | integrated by overlap | same |
| optional root-delta matrix | supported in integrator | not yet fully exposed |
| SCX descriptor `+0x1C` | zeroed before load | currently non-zero rejected |
| fixed animation pool | 512 | dynamic modern storage |

The documentation follows Runtime where behavior differs.

---

# 59. Known OpenNomad follow-up items

The format work exposes several implementation tasks.

## 59.1 Remove/relax SCX `field1C != 0` rejection

Runtime clears that descriptor field before loading the animation.

A non-zero serialized value should not currently be treated as an unsupported
3DA variant without more evidence.

## 59.2 Decide whether quaternion normalization should remain

For valid retail data it produces effectively the same result.

Options:

```text
strict Runtime fidelity:
    do not normalize

defensive modern behavior:
    normalize, but document it
```

Either is reasonable if deliberate.

## 59.3 Investigate optional root-delta matrix

Recover all callers of `0x004711D0` and determine when the transform is non-null
or non-identity.

Then reflect that in the body-animation runtime if required.

## 59.4 Keep `sample_translation()` clearly non-authoritative

Avoid accidentally using absolute linear interpolation for body root motion.

---

# 60. High-value remaining reverse engineering

## 60.1 Standalone `.3DA` corpus

Current strongest samples come from embedded `Grid.SCX`.

Inspect standalone/other-area animations for:

- different track counts;
- non-root position streams;
- rotation-only and position-only tracks;
- unusual key counts;
- gaps/out-of-order streams;
- duplicate IDs;
- non-identity key 0;
- non-unit quaternions.

## 60.2 Alternate ID-base path

Fully map the use of global:

```text
0x006A50A4
```

and determine when track IDs are rebased before matching 3DO objects.

## 60.3 Position reference helper callers

Trace every caller of:

```text
0x00471100
```

to determine all semantic uses of `positionKeys[0]`.

## 60.4 Root-motion matrix

Trace every caller/argument of:

```text
0x004711D0
```

to identify exact coordinate-space conversion.

## 60.5 Non-body animation consumers

`AnimationFromExternalScene` and other systems may consume 3DA-like data with
different frame/interpolation semantics.

Do not assume the recovered body-animation consumer is the only possible user.

## 60.6 Loader/resource lifetime

Map:

- allocation;
- reference ownership;
- unload;
- duplicate-resource lookup;
- the exact 512-slot lifecycle.

These are Runtime architecture questions more than file-format questions.

---

# 61. Useful Runtime locations

| Address | Current role |
|---:|---|
| `0x0046E690` | animation resource load/open path |
| `0x0046E880` | load animation from stream/payload |
| `0x00470FE0` | object callback binding 3DA TrackData to 3DO `scriptID` |
| `0x00471040` | hierarchy wrapper for track binding |
| `0x00471100` | obtain first position-stream key 0/reference position |
| `0x004711D0` | integrate root-motion position keys over frame interval |
| `0x004715B0` | apply body animation to hierarchy; validate frames and root motion |
| `0x00471690` | per-object rotation-key selection/application |
| `0x00442A00` | quaternion (`wxyz`) to row-vector 3×3 matrix |

The source-level names above are descriptive reconstructions unless a Runtime
diagnostic explicitly supplied the term.

---

# 62. Runtime diagnostics relevant to 3DA

Recovered strings include:

```text
PtrTrackData->PosKey == NULL
PtrTrackData == NULL
old frame should be inferior to current frame (Object : %s)
value for current frame is too big (Object : %s)
no animation applyed on object %s
object %s is not top hierarchy !
Plus de 512 anim, pas d'allocation possible...
```

These strings are useful naming/evidence anchors.

They should not be “corrected” for spelling in quoted diagnostic references.

---

# 63. OpenNomad source locations

```text
src/core/Core/Omikron/Animation3DA.hpp
src/core/Core/Omikron/Animation3DA.cpp

src/core/Core/Scenario/ScenarioRuntime.cpp

src/core/Core/RuntimeMath.hpp
src/core/Core/RuntimeMath.cpp

src/core/Tests/BodyAnimationResources.spec.cpp
```

Related resource/container code:

```text
src/core/Core/Omikron/SCX.hpp
src/core/Core/Omikron/SCX.cpp

src/core/Core/Omikron/Model3DO.hpp
src/core/Core/Omikron/Model3DO.cpp
```

---

# 64. Recommended regression tests

Parser:

- [ ] empty/minimal valid header;
- [ ] multiple tracks;
- [ ] position stream present;
- [ ] rotation stream present;
- [ ] count non-zero with zero offset;
- [ ] non-zero out-of-range position offset;
- [ ] non-zero out-of-range rotation offset;
- [ ] overflow-sized key count;
- [ ] non-contiguous valid stream offsets.

Runtime semantics:

- [ ] track ID binds to matching 3DO `scriptID`;
- [ ] rotation progress `< 1` selects key 1 when available;
- [ ] fractional rotation progress floors;
- [ ] key 0 is never selected by normal body playback;
- [ ] position key 0 is never integrated;
- [ ] integer root-motion interval integrates exact deltas;
- [ ] fractional interval integrates weighted overlap;
- [ ] reversed old/current progress is rejected or produces the intended
      guarded result at the appropriate layer;
- [ ] current frame beyond `lastFrame` is rejected;
- [ ] root translation applies only at top hierarchy;
- [ ] retail INTRO1/2/3 payload accounting matches exact sizes.

---

# 65. Compact binary reference

```text
3DA
===

No magic.
No version.

Header:
    +0x00 u32 lastFrame
    +0x04 u32 trackCount

TrackData[trackCount], stride 0x28:
    +0x00 u32 trackId
    +0x04 char name[20]
    +0x18 u32 positionKeyCount
    +0x1C u32 positionKeysOffset
    +0x20 u32 rotationKeyCount
    +0x24 u32 rotationKeysOffset

Offsets:
    relative to start of 3DA payload
    0 = no stream

Position key:
    0x0C bytes
    float x, y, z

Rotation key:
    0x10 bytes
    float w, x, y, z
```

---

# 66. Compact Runtime semantic reference

```text
binding:
    3DA.trackId == 3DO.object.scriptID

body frame range:
    playable frames = 1 .. lastFrame

rotation:
    frame = floor(max(progress, 1))
    q = rotationKeys[frame]
    no interpolation
    Runtime does not normalize q before matrix conversion

position/root motion:
    positionKeys[0] = reference/authored position
    positionKeys[i>=1] = delta over interval (i-1, i]

    delta(old,current) =
        sum(positionKeys[i] * overlap(
            [old,current],
            [i-1,i]))

    root/top hierarchy only

timing:
    no FPS stored in file
    consumer supplies frame progress
```

---

# 67. Worked `INTRO1.3DA` reference

```text
SCX descriptor:
    name         INTRO1.3DA
    animation ID 3

SCX framed resource:
    header       0x112D
    payload      0x1135
    size         0xE898 / 59544

3DA:
    lastFrame    185
    tracks       19

descriptor table end:
    0x300 relative to 3DA payload

root/pelvis track:
    ID                   2
    name                 UBassin
    positionKeyCount     186
    positionKeysOffset   0x1A40
    rotationKeyCount     186
    rotationKeysOffset   0x22F8

position key 0:
    approximately
    (0.62992, -158.18866, 27.61178)

position key 1:
    approximately
    (-0.01820, -17.78207, 0.0)

rotation key 0:
    (1, 0, 0, 0)

rotation key 1:
    approximately
    (0.925496, 0.050750, 0.364135, -0.091037)
```

This sample is the clearest demonstration that:

```text
position key 0 is reference data
```

rather than the first root-motion increment.

---

# 68. Current format boundary

The 3DA format itself is now understood at a relatively high confidence:

```text
3DA
    |
    +-- last playable frame
    +-- TrackData table
    |      |
    |      +-- object binding ID
    |      +-- name
    |      +-- optional position stream
    |      +-- optional rotation stream
    |
    +-- position keys
    +-- quaternion keys
```

The corresponding Runtime body-animation model is also substantially recovered:

```text
3DO hierarchy
    |
    +-- bind TrackData by object.scriptID
    |
    +-- per-frame:
           |
           +-- rotations:
           |      discrete key selection, frame >= 1
           |
           +-- root/top object:
                  integrate position deltas over exact frame interval
```

The main remaining uncertainties are no longer the basic file grammar.

They are:

- alternate consumers;
- alternate/rebased track-ID paths;
- the optional root-motion matrix space;
- SCX descriptor scratch-field meaning;
- and corpus-wide variation outside the three intro animations.
