# Omikron `.3DP` path format

> **Status:** work-in-progress reverse-engineering documentation for OpenNomad  
> **Last updated:** 2026-08-25
>
> This document describes the `.3DP` path resource format used by the Windows
> retail release of *Omikron: The Nomad Soul*.
>
> A 3DP file contains one or more named subpaths. Each subpath stores:
>
> - a fixed-width name;
> - an overall maximum/end path parameter;
> - a sequence of keyed points;
> - an XYZ position at every point;
> - and a `w,x,y,z` quaternion at every point.
>
> The file itself does **not** encode an interpolation mode. Runtime callers
> select how XYZ is evaluated:
>
> - mode `1` uses linear position interpolation;
> - mode `2` uses cubic Hermite position interpolation;
> - orientation interpolation is handled separately through Runtime's
>   quaternion interpolation routine.
>
> This file is authoritative for the 3DP serialized format and the currently
> recovered Runtime sampling semantics.
>
> Related documentation:
>
> - [`scx.md`](scx.md) — `DEAD0000` path descriptors and embedded-resource
>   framing;
> - [`3da.md`](3da.md) — body-animation data used together with paths;
> - [`3do.md`](3do.md) — objects transformed along paths;
> - [`iam-script-functions.md`](iam-script-functions.md) —
>   `MoveObjectOnPath`, `Display3DSpriteOnPath`, and
>   `SelectRelativeBodyAnimation`;
> - [`script-opcodes.md`](script-opcodes.md) — SCX Script command
>   serialization and scheduling;
> - [`runtime-coordinate-math.md`](runtime-coordinate-math.md) — Runtime-native
>   coordinates, quaternions, and row-vector matrices.

---

# 1. Evidence model

Sources are used in this order:

1. **`Runtime.exe`** — authoritative for `Read3DP`, allocation, field use,
   point sampling, interpolation modes, quaternion interpolation, and path
   consumers.
2. **Retail data**, especially the `Grid_pb.3dp` payload embedded in
   `Grid.SCX`.
3. **OpenNomad implementation/tests** — useful to express the recovered
   structure, but subordinate where current behavior differs from Runtime.
4. **Community/importer material** — secondary unless corroborated.

Analyzed Runtime build:

```text
File:             Runtime.exe
Architecture:     PE32 / i386
Image base:       0x00400000
Linker timestamp: 1999-10-04 20:31:50
SHA-256:          55f7120bfea7891b048c64e3682f3259cdbf2719a43fa24e42254b753c95d2ef
```

Confidence labels:

- **Confirmed — Runtime:** directly established from executable behavior.
- **Confirmed — data:** directly established from retail bytes.
- **Corroborated:** Runtime and retail data agree.
- **Strongly reconstructed:** behavior is established, but the original
  source-level field/function name is unavailable.
- **Provisional:** current best interpretation requiring more tracing.
- **Unknown:** bytes/behavior exist but semantics remain unresolved.

---

# 2. Terminology

The original Runtime contains diagnostics such as:

```text
Read3DP %s, not enough memory for paths keys
Read3DP %s, not enough mem for Path %d
Read3DP, not enough memory for PtrPtrPath
Error while reading 3DP file %s : file corrupted !
```

The loader's original source-level name was therefore very likely:

```text
Read3DP
```

This document uses:

| Term | Meaning |
|---|---|
| **3DP resource/file** | one complete payload |
| **subpath** | one named path entry within that payload |
| **point/key** | one `0x20` record |
| **key** | unsigned integer path parameter stored at point `+0x00` |
| **parameter** | floating-point value used by Runtime when sampling between integer keys |
| **maxParameter/endKey** | serialized subpath dword after the name; strongly reconstructed |
| **mode** | caller-supplied position interpolation mode, not serialized in 3DP |

OpenNomad currently names the inner records:

```text
Path3DPSubpath
Path3DPPoint
```

which is a useful distinction because one SCX path resource can contain
multiple internal named paths.

---

# 3. Overview

3DP has:

- no magic;
- no version field;
- no global offset directory;
- no per-subpath pointer/offset table.

Its serialization is completely sequential:

```text
offset 0
    |
    v
+-----------------------------------+
| u32 subpathCount                  |
+-----------------------------------+
| Subpath 0                         |
|   name[20]                        |
|   u32 maxParameter               |
|   u32 pointCount                  |
|   Point[pointCount]               |
+-----------------------------------+
| Subpath 1                         |
|   ...                             |
+-----------------------------------+
| ...                               |
+-----------------------------------+
| EOF / enclosing resource end      |
+-----------------------------------+
```

One point is exactly:

```text
0x20 bytes
```

and contains:

```text
u32 key
float x, y, z
float w, x, y, z
```

All integer fields are little-endian in the Windows retail build.

---

# 4. No magic or version

A raw 3DP payload begins directly with:

```text
u32 subpathCount
```

There is no known signature such as:

```text
"3DP"
"PATH"
```

and no version dword.

Consequences:

1. a raw 3DP should be extracted using the enclosing SCX resource frame rather
   than magic scanning;
2. standalone files need externally known boundaries;
3. a parser cannot identify a 3DP reliably from its first four bytes alone;
4. no currently recovered version negotiation exists inside the payload.

---

# 5. Top-level header — `0x04` bytes

```c
struct Serialized3DPHeader {
    uint32_t subpathCount;
}; // 0x04
```

Runtime `Read3DP`:

1. reads this dword;
2. allocates an array of `subpathCount` pointers;
3. loads one sequential subpath for each entry.

Current OpenNomad calls this:

```text
path_count
```

internally.

`subpathCount` is preferable in documentation because the containing SCX
descriptor is already one path resource.

---

# 6. Serialized subpath header — `0x1C` bytes

Each subpath begins:

```c
struct Serialized3DPSubpathHeader {
    char     name[20];       // +0x00
    uint32_t maxParameter;   // +0x14
    uint32_t pointCount;     // +0x18
}; // 0x1C
```

Immediately following:

```text
Path3DPPoint points[pointCount]
```

with no intervening pointer or offset.

## 6.1 Why the current `field_14` can now be named more usefully

OpenNomad currently calls the dword at serialized `+0x14`:

```text
field_14
```

Runtime provides substantially stronger semantics.

A tiny helper at:

```text
0x004B0C60
```

returns exactly the corresponding runtime field.

Path-based IAM functions convert that value to float and use it as the
subpath's overall parameter limit/end value.

In the inspected retail resource:

```text
maxParameter = 2
last point key = 2
```

for both subpaths.

The best current name is therefore:

```text
maxParameter
```

or:

```text
endKey
```

Confidence:

```text
strongly reconstructed from Runtime use
```

The exact original C field name remains unknown.

## 6.2 It is not the interpolation mode

This is important.

Runtime's sampling mode is supplied as a separate function argument.

For example:

```text
SelectRelativeBodyAnimation:
    hardcodes mode 1

MoveObjectOnPath:
    mode comes from a Script command value
```

Therefore serialized `+0x14 == 2` in `Grid_pb.3dp` does **not** mean
“interpolation mode 2”.

---

# 7. Point record — `0x20` bytes

```c
struct Serialized3DPPoint {
    uint32_t key;    // +0x00

    float x;         // +0x04
    float y;         // +0x08
    float z;         // +0x0C

    float qw;        // +0x10
    float qx;        // +0x14
    float qy;        // +0x18
    float qz;        // +0x1C
}; // 0x20
```

| Offset | Size | Type | Meaning |
|---:|---:|---|---|
| `0x00` | 4 | `u32` | path key/parameter coordinate |
| `0x04` | 4 | `f32` | position X |
| `0x08` | 4 | `f32` | position Y |
| `0x0C` | 4 | `f32` | position Z |
| `0x10` | 4 | `f32` | quaternion W |
| `0x14` | 4 | `f32` | quaternion X |
| `0x18` | 4 | `f32` | quaternion Y |
| `0x1C` | 4 | `f32` | quaternion Z |

Runtime `Read3DP` reads the point as eight consecutive 32-bit words.

No additional per-point flags or interpolation data are present.

---

# 8. Point keys

The point's first dword is used as an unsigned path-coordinate key.

Runtime's path sampler converts keys to floating-point values and searches for
two adjacent keys that bracket the requested floating parameter.

Conceptually, valid authored paths are expected to satisfy:

```text
key[0] < key[1] < key[2] < ...
```

but the sampler does not appear to perform one separate whole-array
monotonicity validation pass.

## 8.1 Keys are not array indexes

Retail data happens to use:

```text
0, 1, 2
```

in `Grid_pb.3dp`.

The interpolation math nevertheless uses the serialized differences:

```text
next.key - current.key
```

so a path such as:

```text
0, 10, 25
```

is structurally meaningful.

Do not replace keys with implicit array indexes.

## 8.2 Floating sampling parameter

The sampler accepts a:

```text
float parameter
```

even though the stored keys are integers.

Therefore fractional positions such as:

```text
0.25
1.5
10.75
```

can be evaluated between keys.

---

# 9. Position representation

Positions are three uncompressed IEEE-754 floats:

```text
X, Y, Z
```

They use Runtime's native coordinate system.

Do not apply:

- Blender axis conversion;
- metre conversion;
- centimetre conversion;
- vertical flipping;
- path-relative subtraction;

inside the 3DP parser.

Those are higher-level consumer responsibilities.

For the current Runtime/OpenNomad coordinate model, see:

[`runtime-coordinate-math.md`](runtime-coordinate-math.md)

---

# 10. Quaternion representation

Quaternions are serialized:

```text
w, x, y, z
```

as four IEEE-754 floats.

This is the same order used by 3DA.

At an exact path key, Runtime passes the stored four floats directly to its
quaternion-to-matrix routine around:

```text
0x00442A00
```

No separate orientation format is stored.

---

# 11. Sequential file grammar

Unlike 3DA, 3DP contains no internal offsets.

The offset of each next subpath is computed from the previous point count:

```text
subpathSize =
    0x1C
    + pointCount * 0x20
```

Thus:

```text
offset = 4

repeat subpathCount times:
    read name[20]
    read maxParameter
    read pointCount
    read pointCount * 0x20 bytes
```

This makes a bad `pointCount` destructive to all subsequent parsing.

Every count multiplication must be bounds-checked.

---

# 12. Runtime `Read3DP` loader

The main recovered loader begins around:

```text
0x0049FCA0
```

A wrapper around:

```text
0x004A0040
```

is used when reading from the current SCX/resource stream.

The loader:

1. opens/selects the input stream;
2. reads `subpathCount`;
3. allocates:

```text
subpathCount * sizeof(void*)
```

4. for every subpath:
   - allocates a zeroed `0x24` runtime object;
   - reads 20 name bytes;
   - reads `maxParameter`;
   - reads `pointCount`;
   - allocates `pointCount * 0x20`;
   - reads every point as eight 32-bit values;
5. returns:
   - pointer to the array of subpath pointers;
   - subpath count.

This directly confirms the serialized grammar above.

---

# 13. Runtime subpath expansion — `0x24` bytes

Runtime does not keep the serialized subpath header verbatim.

It expands it to:

```c
struct Runtime3DPSubpath {
    uint32_t reserved00;      // +0x00, zero from allocation in recovered loader

    char     name[20];        // +0x04

    uint32_t maxParameter;    // +0x18
    uint32_t pointCount;      // +0x1C

    Serialized3DPPoint* points; // +0x20
}; // 0x24
```

Important:

```text
+0x00
+0x20
```

are runtime-only.

They are not present in the serialized file.

## 13.1 Why Runtime shifts the name by four bytes

Serialized:

```text
name starts +0x00
```

Runtime object:

```text
name starts +0x04
```

because Runtime prepends one zero/reserved dword.

A reverse-engineered C structure based only on the runtime object will
therefore be four bytes off if applied directly to file bytes.

---

# 14. Point records are not relocated

Runtime allocates a new contiguous point array and copies the serialized
`0x20` point records into it.

The point representation itself does not gain pointers or additional fields.

Therefore:

```text
serialized point layout
==
runtime point layout
```

for the currently recovered Windows path.

---

# 15. SCX `DEAD0000` embedding

Inside SCX, a path descriptor is followed later by one generic framed resource
in the appended resource stream.

Resource frame:

```text
u32 absoluteSelfOffset
u32 payloadSize
u8  threeDP[payloadSize]
```

The actual 3DP payload begins:

```text
8 bytes after the SCX resource-frame header
```

See [`scx.md`](scx.md).

---

# 16. Important SCX `DEAD0000` field correction

The Runtime path loader also clarifies two fields in the SCX `DEAD0000`
descriptor.

A current descriptor is physically:

```text
char name[24]
u32 field18
u32 field1C
```

During SCX load, Runtime calls the 3DP loader with:

```text
&descriptor.field18
&descriptor.field1C
```

as output locations.

On success they become approximately:

```text
+0x18 = Runtime3DPSubpath** paths
+0x1C = loaded subpath count
```

On load failure, Runtime zeros both.

Therefore the initial serialized values at these locations are **not**
authoritatively immutable resource IDs in this load path.

This corrects the current OpenNomad name:

```text
ScxSection0Record::resource_id
```

for `+0x1C`.

`Grid.SCX` happens to contain:

```text
serialized +0x1C = 2
```

and the loaded `Grid_pb.3dp` also contains:

```text
subpathCount = 2
```

but Runtime overwrites the descriptor field with the count read from the 3DP.

The SCX documentation/parser should be updated accordingly.

---

# 17. Runtime path sampler

The central recovered sampler begins around:

```text
0x004B0C70
```

A descriptive prototype is approximately:

```c
bool SamplePath(
    Runtime3DPSubpath* path,
    float parameter,
    float* outX,
    float* outY,
    float* outZ,
    Matrix3* outOrientation,  // may be null
    int interpolationMode);
```

The original source-level function name is not yet recovered.

The return value is:

```text
non-zero -> sample produced
zero     -> parameter could not be bracketed / sample failed
```

---

# 18. Segment search

For a path with:

```text
N points
```

Runtime searches:

```text
N - 1
```

adjacent segments.

For segment `i`:

```text
from = points[i]
to   = points[i + 1]
```

it looks for a parameter in the inclusive interval:

```text
from.key <= parameter <= to.key
```

If no adjacent pair brackets the parameter:

```text
return false
```

## 18.1 Runtime does not clamp out-of-range parameters

A request below the first key or above the last key does not automatically
return the first/last point.

This is an important difference from current OpenNomad.

## 18.2 A single-point path cannot form a segment

With:

```text
pointCount < 2
```

the recovered sampler cannot bracket a segment and returns failure.

Again, current OpenNomad behaves differently.

---

# 19. Exact-key behavior

After finding a segment, Runtime handles the endpoints specially.

If:

```text
parameter == from.key
```

it copies:

```text
from.position
```

exactly and, if orientation was requested, converts:

```text
from.quaternion
```

directly to a matrix.

Likewise, if:

```text
parameter == to.key
```

it copies the `to` point exactly.

Only a parameter strictly between the two keys reaches the interpolation path.

---

# 20. Normalized segment parameter

For an interior sample:

```text
from.key < parameter < to.key
```

Runtime calculates:

```text
t =
    (parameter - from.key)
    / (to.key - from.key)
```

so:

```text
0 < t < 1
```

This `t` is then used independently for:

- position interpolation according to the caller-selected mode;
- quaternion interpolation.

---

# 21. Interpolation mode is caller-supplied

The 3DP resource itself stores no interpolation-mode byte/flag.

The final argument of `0x004B0C70` selects position interpolation.

Known consumers prove that the mode comes from the caller.

## 21.1 `SelectRelativeBodyAnimation`

At:

```text
0x004A3D2D
```

the handler samples a selected 3DP subpath with:

```text
parameter = 1.0
mode      = 1
```

hard-coded.

## 21.2 `MoveObjectOnPath`

At:

```text
0x0046F637
```

the mode comes from a raw structured-script command value.

Thus different authored Script actions can use the same 3DP with different
interpolation behavior.

---

# 22. Mode 1 — linear XYZ interpolation

For:

```text
interpolationMode == 1
```

Runtime performs ordinary linear interpolation:

```text
outX = from.x + (to.x - from.x) * t
outY = from.y + (to.y - from.y) * t
outZ = from.z + (to.z - from.z) * t
```

This portion of current OpenNomad's `sample_mode_1()` is correct.

---

# 23. Mode 2 — cubic Hermite XYZ interpolation

For:

```text
interpolationMode == 2
```

Runtime calls a helper around:

```text
0x004B0EE0
```

which constructs cubic coefficients from:

- the current segment endpoints;
- neighboring path points;
- endpoint-aware neighboring selection.

The sampler then evaluates a cubic polynomial for X/Y/Z.

The basis matrix stored by Runtime is:

```text
[  2  -2   1   1 ]
[ -3   3  -2  -1 ]
[  0   0   1   0 ]
[  1   0   0   0 ]
```

which is the standard cubic Hermite coefficient basis.

For one scalar component:

```text
[a]   [ 2 -2  1  1] [P0]
[b] = [-3  3 -2 -1] [P1]
[c]   [ 0  0  1  0] [T0]
[d]   [ 1  0  0  0] [T1]
```

and:

```text
value(t) =
    ((a * t + b) * t + c) * t + d
```

where `T0/T1` are Runtime-generated endpoint tangents.

## 23.1 Tangent generation

The helper derives tangents from neighboring point positions using a
four-point stencil and constants including:

```text
 1/16   =  0.0625
-9/16   = -0.5625
 3/16   =  0.1875
```

The exact source-level interpolation family/name and a simplified closed-form
description of that tangent-generation code remain to be documented.

The Hermite basis itself is directly established.

## 23.2 OpenNomad status

Current `Path3DP` does not implement mode 2.

That is an implementation gap, not an unknown file-format feature.

---

# 24. Other interpolation-mode values

The recovered sampler has explicit interior XYZ branches for:

```text
1
2
```

Other values skip those known interior position branches.

Several Runtime callers use the sampler in more specialized ways, so do not
currently declare:

```text
mode 0 = <specific named interpolation>
```

without tracing those call sites fully.

The authoritative currently named modes are:

```text
1 -> linear
2 -> cubic Hermite
```

---

# 25. Quaternion interpolation is independent of XYZ mode

For an interior point sample, orientation uses the same quaternion interpolation
routine regardless of whether position mode is 1 or 2.

The path sampler calls:

```text
0x00442C80
```

on:

```text
from.quaternion
to.quaternion
t
```

then converts the result to a row-vector 3×3 matrix using:

```text
0x00442A00
```

OpenNomad's mode-1 sampler follows this recovered orientation path.

---

# 26. Runtime quaternion interpolation

The routine at `0x00442C80` computes:

```text
dot =
    q0.w*q1.w
  + q0.x*q1.x
  + q0.y*q1.y
  + q0.z*q1.z

angle = acos(dot)
```

Then it uses two regimes.

## 26.1 Small-angle fallback

When:

```text
angle <= 0.1 radians
```

Runtime uses ordinary linear weights:

```text
weight0 = 1 - t
weight1 = t
```

and:

```text
q =
    q0 * weight0
    + q1 * weight1
```

No normalization step is visible in this fallback.

## 26.2 Larger-angle SLERP

When:

```text
angle > 0.1 radians
```

Runtime uses sine-weighted spherical interpolation:

```text
weight0 =
    sin(angle * (1 - t))
    / sin(angle)

weight1 =
    sin(angle * t)
    / sin(angle)

q =
    q0 * weight0
    + q1 * weight1
```

This is standard SLERP weighting.

## 26.3 No observed shortest-path sign flip

The recovered routine does not first do the common modern step:

```text
if dot < 0:
    q1 = -q1
```

No shortest-arc sign correction is currently visible.

## 26.4 No observed dot clamp

No explicit:

```text
dot = clamp(dot, -1, +1)
```

is visible before `acos`.

Retail data is expected to contain sane normalized quaternions.

---

# 27. Runtime orientation output is a matrix

The path point stores a quaternion.

The central Runtime sampler does **not** expose an interpolated quaternion as
its final orientation output.

Instead:

```text
stored/interpolated quaternion
    ->
0x00442A00
    ->
row-vector Matrix3
```

The matrix output can be omitted by passing a null orientation pointer.

This is a runtime API distinction, not a serialization difference.

A modern decoder can reasonably return a quaternion and let the consumer build
the matrix later, as long as the interpolation math matches Runtime.

---

# 28. Exact endpoint orientation

When the requested parameter exactly matches a point key, Runtime bypasses
`0x00442C80`.

It simply converts the stored quaternion for that exact point using:

```text
0x00442A00
```

Thus no interpolation, normalization, or sign handling occurs at an exact key.

---

# 29. Quaternion-to-matrix convention

The conversion routine is the same one documented for 3DA.

For:

```text
q = (w, x, y, z)
```

Runtime builds a row-vector 3×3 matrix:

```text
[ 1 - 2(y² + z²)   2(xy + wz)       2(xz - wy)     ]
[ 2(xy - wz)       1 - 2(x² + z²)   2(yz + wx)     ]
[ 2(xz + wy)       2(yz - wx)       1 - 2(x² + y²) ]
```

Runtime does not normalize the quaternion inside this conversion routine.

See [`3da.md`](3da.md) and
[`runtime-coordinate-math.md`](runtime-coordinate-math.md).

---

# 30. `maxParameter` / end-key semantics

The helper:

```text
0x004B0C60
```

is effectively:

```c
uint32_t GetPathMaxParameter(Runtime3DPSubpath* path) {
    return path->maxParameter;
}
```

The value is used by path-driven operations as the overall path range/duration
bound.

Strong current model:

```text
valid authored parameter range:
    approximately key[0] .. maxParameter

maxParameter:
    expected to correspond to the last authored key
```

`Grid_pb.3dp` confirms:

```text
maxParameter = 2
last key     = 2
```

for both subpaths.

## 30.1 Stored redundantly with the final point key

The sampler itself uses each point's actual key when bracketing.

It does not need `maxParameter` to interpolate.

The separate field is used by higher-level consumers for progress/range logic.

Thus the file redundantly stores:

```text
per-point keys
+
overall end/max parameter
```

## 30.2 Do not derive one and discard the other

A parser should preserve both.

A validation tool can report:

```text
maxParameter != lastPoint.key
```

as suspicious without silently rewriting either value.

---

# 31. Path timing is not intrinsically seconds or frames

3DP stores integer keys and an integer max parameter.

It does **not** contain:

- an FPS;
- milliseconds;
- seconds-per-key;
- a sample-rate field.

Higher-level consumers determine how quickly the floating parameter advances.

Therefore:

```text
key 1
```

means:

```text
path parameter 1
```

not inherently:

```text
1 second
```

or:

```text
frame 1 at 30 Hz
```

Script functions can map their own timing/progress onto this parameter domain.

---

# 32. `MoveObjectOnPath`

The structured IAM function:

```text
0x03000008
Script_MoveObjectOnPath
```

uses 3DP directly.

Relevant Runtime handler:

```text
0x0046F400
```

Reinit:

```text
0x0046EEB0
```

Current recovered behavior includes:

- resolving the SCX path resource;
- selecting one internal subpath;
- obtaining its `maxParameter`;
- managing script progress;
- passing a caller/script-supplied interpolation mode to `0x004B0C70`;
- applying sampled position and orientation to an object;
- supporting absolute placement (`arg5 == 0`) and relative/rebased translation
  (`arg5 == 1`).

The 15-slot ABI uses args 7 and 8 as mutable path progress. In rebase mode,
args 9-11 are mutable captured base world XYZ. At the direction-specific
reference endpoint, Runtime captures the selected object's current world
translation `B` and then computes every translation without accumulation:

```text
forward reference: P(0)
reverse reference: P(maxParameter)

desiredWorldTranslation = B + P(current) - P(reference)
```

Absolute mode continues to use `P(current)` directly. Both variants apply the
quaternion-derived matrix sampled at the current parameter and convert the
desired world pose back through the parent inverse before writing object-local
state.

Args 12-14 participate in an additional Runtime rotation transform. Their
designer-facing names and nonzero semantics are not yet recovered, so
OpenNomad keeps nonzero values structured as an unsupported variant rather
than guessing.

This is the clearest proof that interpolation mode belongs to the Script
operation rather than to the 3DP bytes.

---

# 33. `Display3DSpriteOnPath`

`Script_Display3DSpriteOnPath` also consumes path data.

Current handler/reinit addresses:

```text
action: 0x004A2150
reinit: 0x004A1B20
```

The path's maximum parameter is used in the path-progress logic.

This independently supports the `maxParameter/endKey` interpretation of
serialized subpath `+0x14`.

Detailed IAM argument semantics belong in:

[`iam-script-functions.md`](iam-script-functions.md)

---

# 34. `SelectRelativeBodyAnimation`

`Script_SelectRelativeBodyAnimation`:

```text
0x0200002A
handler 0x004A3AD0
```

uses a 3DP together with a 3DA animation.

The path call around:

```text
0x004A3D2D
```

supplies:

```text
parameter = 1.0
mode      = 1
```

for the selected internal path.

Runtime obtains:

- XYZ position;
- an orientation matrix output.

The surrounding function then combines path placement with authored Script
offsets and body-animation state.

Current OpenNomad uses the sampled XYZ position for the intro anchor.

---

# 35. Worked retail resource: `Grid_pb.3dp`

`Grid.SCX` contains one `DEAD0000` path descriptor:

```text
name:
    Grid_pb.3dp
```

The appended SCX resource is:

```text
SCX frame header:
    0x1029

3DP payload:
    0x1031

payload size:
    252 bytes
    0xFC
```

The 3DP contains:

```text
subpathCount = 2
```

---

# 36. `Grid_pb.3dp` payload accounting

Each subpath has:

```text
header:
    20 + 4 + 4
    = 28 bytes
    = 0x1C

points:
    3 * 0x20
    = 96 bytes
    = 0x60

total per subpath:
    124 bytes
    = 0x7C
```

Whole payload:

```text
4-byte top-level count
+ 2 * 124-byte subpaths

= 4 + 248
= 252
= 0xFC
```

This exactly matches the enclosing SCX payload size.

---

# 37. `Grid_pb.3dp` subpath 0

```text
name:
    "UBas.p1"

maxParameter:
    2

pointCount:
    3
```

Points:

```text
key 0:
    position =
        (-478.393341,
          -43.900246,
           27.611773)

    quaternion approximately =
        (1.0,
         4.371139e-08,
         0.0,
         0.0)

key 1:
    position =
        (-478.393341,
          -43.900246,
           27.611773)

    quaternion approximately =
        (1.0,
         8.742278e-08,
         0.0,
         0.0)

key 2:
    position =
        (-478.393341,
          -43.900246,
           27.611773)

    quaternion approximately =
        (1.0,
         1.311342e-07,
         0.0,
         0.0)
```

The position is constant across all three keys.

Orientation is effectively identity with tiny floating-point X components.

---

# 38. `Grid_pb.3dp` subpath 1

```text
name:
    "UBas.p2-3"

maxParameter:
    2

pointCount:
    3
```

All three positions are approximately:

```text
(-486.818512,
  -43.900276,
  -79.123238)
```

Keys:

```text
0
1
2
```

Quaternions follow the same near-identity sequence as `UBas.p1`.

---

# 39. What the Grid path demonstrates

`Grid_pb.3dp` is not describing Kay'l's detailed walking/root-motion trajectory.

Instead, for the recovered intro usage:

```text
3DP:
    supplies stable scene/path anchor coordinates

3DA:
    supplies the detailed body pose
    and root-motion increments

Script authored offset:
    participates in final placement
```

This division of responsibility is important.

Do not expect every 3DP paired with a body animation to encode the body
animation's full movement curve itself.

---

# 40. Current OpenNomad parser

Current implementation:

```text
Path3DP::load()
```

correctly parses the basic serialized grammar:

```text
u32 subpathCount

for each subpath:
    name[20]
    u32 field_14
    u32 pointCount

    for each point:
        u32 key
        float3 position
        float4 quaternion
```

The parser currently stores:

```text
field_14
```

without assigning its recovered max-parameter semantics.

That naming should now be revisited.

---

# 41. OpenNomad mode-1 sampler

Current:

```text
Path3DPSubpath::sample_mode_1()
```

implements:

- linear XYZ interpolation;
- exact authored endpoint quaternions;
- ordinary unnormalized linear quaternion weighting through `0.1` radians;
- sine-weighted interpolation above that threshold;
- no shortest-path sign flip or dot clamp;
- structured failure for a parameter outside the keyed range.

---

# 42. Quaternion interpolation status

Runtime and OpenNomad use:

```text
angle <= 0.1 rad:
    ordinary LERP

angle > 0.1 rad:
    SLERP
```

and does not normalize the small-angle LERP result in the recovered routine.

For `Grid_pb.3dp` the difference is negligible because neighboring
quaternions are almost identical.

For other path resources it can be visually significant.

---

# 43. Out-of-range sampling

The sampler returns exact authored endpoints only for exact endpoint
parameters. A parameter outside the keyed range is a structured error; callers
must not clamp it to hide an invalid traversal state.

Runtime instead searches for a valid adjacent bracket.

If no bracket exists:

```text
return false
```

The authoritative Runtime-compatible behavior is therefore not automatic
clamping.

Higher-level consumers may separately clamp before calling the sampler, but
that is distinct from the sampler itself.

---

# 44. OpenNomad discrepancy: single-point path

Current OpenNomad returns the sole point when:

```text
points.size() == 1
```

The recovered Runtime sampler loops over:

```text
pointCount - 1
```

segments.

A one-point path has no segment and therefore cannot be sampled through that
routine.

Whether another Runtime helper supports one-point paths remains unresolved.

---

# 45. OpenNomad discrepancy: mode 2 missing

Runtime explicitly supports:

```text
mode 2 cubic Hermite XYZ
```

Current OpenNomad exposes only:

```text
sample_mode_1()
```

Implementing the recovered Hermite path will eventually be required for exact
`MoveObjectOnPath`/other consumers that request mode 2.

---

# 46. OpenNomad discrepancy: SCX descriptor naming

Current `ScxSection0Record` names descriptor `+0x1C`:

```text
resource_id
```

Runtime uses that memory location as the output slot for:

```text
loaded internal path/subpath count
```

during SCX path loading.

That parser/model name should be corrected when `scx.md` and the SCX
implementation receive their next small fidelity pass.

---

# 47. Validity and monotonic keys

Current OpenNomad reports an error if the selected interpolation segment has:

```text
to.key <= from.key
```

before dividing.

That is good defensive behavior.

Runtime does not expose an equally explicit high-level validation in the
sampler, but valid authored paths are expected to have increasing keys.

Recommended modern policy:

```text
strict parser:
    preserve raw keys

validation layer:
    report duplicate/decreasing keys

sampler:
    refuse zero/negative-width interpolation segments
```

---

# 48. Quaternion validation

A forensic validator can report:

- non-finite components;
- zero-length quaternion;
- materially non-unit length.

But the format parser should preserve the stored floats.

Runtime's path quaternion interpolation assumes sensible data.

Do not normalize or rewrite the serialized resource during load.

---

# 49. `maxParameter` validation

Useful diagnostics:

```text
pointCount == 0
maxParameter < firstKey
maxParameter != lastKey
lastKey < firstKey
```

Current retail example satisfies:

```text
firstKey    = 0
lastKey     = 2
maxParameter = 2
```

Do not make `maxParameter == lastKey` a universal hard parser invariant until a
larger retail corpus is inspected.

It is nevertheless the strongest current semantic interpretation.

---

# 50. String representation

Subpath names occupy exactly:

```text
20 bytes
```

and are generally NUL-terminated/padded.

Do not assume a particular legacy code page as an intrinsic format rule until
localized asset evidence establishes it.

Preserve raw bytes if exact round-tripping is needed.

---

# 51. Endianness and scalar representation

Windows retail 3DP uses:

```text
little-endian u32
IEEE-754 little-endian float32
```

Quaternion order:

```text
w, x, y, z
```

Position order:

```text
x, y, z
```

Point key:

```text
u32
```

---

# 52. Documentation-oriented serialized structures

```c
#pragma pack(push, 1)

typedef struct Serialized3DPHeader {
    uint32_t subpathCount;
} Serialized3DPHeader; // 0x04

typedef struct Serialized3DPSubpathHeader {
    char     name[20];        // +0x00
    uint32_t maxParameter;    // +0x14
    uint32_t pointCount;      // +0x18
} Serialized3DPSubpathHeader; // 0x1C

typedef struct Serialized3DPPoint {
    uint32_t key;             // +0x00

    float x;                  // +0x04
    float y;                  // +0x08
    float z;                  // +0x0C

    float qw;                 // +0x10
    float qx;                 // +0x14
    float qy;                 // +0x18
    float qz;                 // +0x1C
} Serialized3DPPoint;         // 0x20

#pragma pack(pop)
```

No pointer field follows `pointCount` in the file.

---

# 53. Documentation-oriented Runtime structure

For comparison only:

```c
typedef struct Runtime3DPSubpath {
    uint32_t reserved00;            // +0x00
    char     name[20];              // +0x04
    uint32_t maxParameter;          // +0x18
    uint32_t pointCount;            // +0x1C
    Serialized3DPPoint* points;     // +0x20
} Runtime3DPSubpath;                // 0x24
```

Runtime loader returns separately:

```text
Runtime3DPSubpath** paths
u32 pathCount
```

Do not confuse this expanded allocation with the serialized file grammar.

---

# 54. Recommended OpenNomad representation

```cpp
struct PathPoint {
    std::uint32_t key;
    Runtime::Vec3 position;
    Runtime::Quaternion quaternion;
};

struct PathSubpath {
    std::string name;
    std::uint32_t max_parameter;
    std::vector<PathPoint> points;
};

struct Path3DP {
    std::vector<PathSubpath> subpaths;
};
```

Sampling should be a separate runtime operation:

```cpp
sample(subpath, parameter, interpolationMode)
```

rather than encoding interpolation policy into the decoded file object.

That more closely mirrors Runtime, where interpolation mode is caller-supplied.

---

# 55. Recommended sampler architecture

A Runtime-faithful interface could return:

```cpp
struct PathSample {
    Runtime::Vec3 position;
    Runtime::Quaternion quaternion;
};

enum class PathInterpolationMode : std::uint32_t {
    Linear = 1,
    CubicHermite = 2,
};
```

with:

```cpp
std::expected<PathSample, PathSampleError>
sample(
    const PathSubpath& path,
    float parameter,
    PathInterpolationMode mode);
```

Internally:

```text
find bracket
    |
    +-- exact key:
    |      copy point
    |
    +-- interior:
           compute t
           |
           +-- mode 1 -> linear XYZ
           +-- mode 2 -> Hermite XYZ
           |
           +-- Runtime quaternion interpolate
```

A renderer/runtime consumer can then convert the quaternion to Matrix3.

---

# 56. Runtime-faithful mode-1 pseudocode

```text
function SampleMode1(path, parameter):

    if path.pointCount < 2:
        fail

    for i in 0 .. path.pointCount-2:
        a = path.points[i]
        b = path.points[i+1]

        if a.key <= parameter <= b.key:
            if parameter == a.key:
                return a

            if parameter == b.key:
                return b

            if b.key <= a.key:
                fail

            t =
                (parameter - a.key)
                / (b.key - a.key)

            position =
                a.position
                + (b.position - a.position) * t

            quaternion =
                RuntimeQuaternionInterpolate(
                    a.quaternion,
                    b.quaternion,
                    t)

            return sample

    fail
```

The actual Runtime function emits an orientation matrix rather than returning
the quaternion object itself.

---

# 57. Runtime quaternion interpolation pseudocode

```text
function RuntimeQuaternionInterpolate(q0, q1, t):

    dot =
        q0.w*q1.w
      + q0.x*q1.x
      + q0.y*q1.y
      + q0.z*q1.z

    angle = acos(dot)

    if angle <= 0.1:
        a = 1 - t
        b = t
    else:
        denom = sin(angle)

        a =
            sin(angle * (1 - t))
            / denom

        b =
            sin(angle * t)
            / denom

    return q0*a + q1*b
```

Not currently observed:

```text
dot clamp
shortest-path sign flip
small-angle normalization
```

---

# 58. Runtime mode-2 position pseudocode

At a high level:

```text
function SampleMode2(path, segmentIndex, t):

    P0 = point[segmentIndex]
    P1 = point[segmentIndex + 1]

    T0, T1 =
        BuildRuntimeTangents(
            neighboring point positions)

    [a,b,c,d] =
        HermiteBasis * [P0,P1,T0,T1]

    P(t) =
        ((a*t + b)*t + c)*t + d
```

Quaternion handling remains the same as mode 1.

The exact simplified tangent formula remains a reverse-engineering follow-up.

---

# 59. Parser safety requirements

A robust decoder should:

1. require at least 4 bytes;
2. read `subpathCount`;
3. apply a sensible maximum count;
4. for each subpath require at least `0x1C` bytes;
5. read the exact 20-byte name;
6. preserve `maxParameter`;
7. read `pointCount`;
8. overflow-check:

```text
pointCount * 0x20
```

9. require the complete point array;
10. read point keys as unsigned 32-bit values;
11. preserve raw float values;
12. avoid applying coordinate conversions;
13. optionally report non-finite floats;
14. optionally report non-increasing point keys;
15. optionally report `maxParameter != lastKey`;
16. distinguish malformed serialization from an unsampleable valid resource.

---

# 60. Loader safety versus retail Runtime

Retail Runtime assumes trusted shipped data and performs allocation/read checks
appropriate to a 1999 game.

OpenNomad should additionally guard:

- integer multiplication overflow;
- unreasonable counts;
- truncated point arrays;
- malformed floating values;
- sampling division by zero;
- unsupported interpolation modes;
- out-of-range sample requests.

These safety checks do not change the serialized format.

---

# 61. Suggested forensic dump

For each 3DP:

```text
payload size
subpath count
```

For each subpath:

```text
index
name
maxParameter
pointCount
serialized start/end
```

For every point:

```text
index
key
XYZ
WXYZ
quaternion length
```

Derived diagnostics:

```text
first key
last key
maxParameter-lastKey difference
monotonic keys?
position bounds
position delta per segment
orientation angle per segment
```

This will be especially useful when a larger SCX corpus is available.

---

# 62. Corpus questions

A full retail 3DP inventory should answer:

- is `maxParameter` always the final point key?
- do paths always begin at key 0?
- are keys normally consecutive?
- do mode-2 consumers correlate with denser/non-uniform path keys?
- are single-point subpaths present?
- do any path records contain duplicate/decreasing keys?
- do path quaternions remain unit length?
- do any callers use position-interpolation modes other than 1 and 2?
- does serialized `DEAD0000 +0x1C` always happen to equal internal subpath
  count before Runtime overwrites it?
- are there standalone `.3DP` files whose outer packaging differs from SCX?

---

# 63. Highest-value remaining reverse engineering

## 63.1 Exact mode-2 tangent formula

The cubic Hermite basis is confirmed.

Recover a clean mathematical expression for the Runtime tangent generator at:

```text
0x004B0EE0
```

and its edge behavior.

## 63.2 Other interpolation modes

Trace every call to:

```text
0x004B0C70
```

and classify all final mode arguments.

Avoid inventing names for mode 0 or other values before that.

## 63.3 `maxParameter` source name

Behavior is sufficiently clear to document as max/end parameter, but the
original field name is still unknown.

## 63.4 Non-script path consumers

Runtime has path-sampling call sites outside the currently catalogued IAM
handlers.

Map them to:

- cameras;
- scene/world systems;
- special effects;
- character movement;
- editor-derived runtime helpers.

## 63.5 SCX descriptor placeholders

Update the SCX parser/documentation to reflect Runtime's overwrite of:

```text
DEAD0000 +0x18
DEAD0000 +0x1C
```

with live paths/count.

---

# 64. Useful Runtime locations

| Address | Current role |
|---:|---|
| `0x0049FCA0` | `Read3DP` main loader |
| `0x004A0040` | wrapper used by SCX/resource-stream loading |
| `0x004B0C60` | returns subpath max/end parameter |
| `0x004B0C70` | central keyed path sampler |
| `0x004B0EE0` | builds mode-2 cubic Hermite coefficients/tangents |
| `0x00442C80` | quaternion LERP/SLERP interpolation |
| `0x00442A00` | quaternion to row-vector Matrix3 |
| `0x0046EEB0` | `Script_Reinit_MoveObjectOnPath` |
| `0x0046F400` | `Script_MoveObjectOnPath` |
| `0x004A1B20` | `Script_Reinit_Display3DSpriteOnPath` |
| `0x004A2150` | `Script_Display3DSpriteOnPath` |
| `0x004A3AD0` | `Script_SelectRelativeBodyAnimation` |
| `0x00449AEB` | SCX `DEAD0000` path-resource load call |

---

# 65. OpenNomad source locations

```text
src/core/Core/Omikron/Path3DP.hpp
src/core/Core/Omikron/Path3DP.cpp

src/core/Core/Scenario/ScenarioRuntime.cpp

src/core/Core/Omikron/SCX.hpp
src/core/Core/Omikron/SCX.cpp

src/core/Core/RuntimeMath.hpp
src/core/Core/RuntimeMath.cpp

src/core/Tests/BodyAnimationResources.spec.cpp
```

Related structured-script code:

```text
src/core/Core/Script/ScriptRuntime.*
```

---

# 66. Recommended regression tests

Parser:

- [ ] zero subpaths;
- [ ] one subpath;
- [ ] multiple subpaths;
- [ ] zero points;
- [ ] one point;
- [ ] multiple keyed points;
- [ ] truncated subpath header;
- [ ] truncated point;
- [ ] unreasonable `subpathCount`;
- [ ] unreasonable `pointCount`;
- [ ] preserve `maxParameter`;
- [ ] preserve non-consecutive keys.

Mode 1:

- [ ] exact first key;
- [ ] exact interior key;
- [ ] exact final key;
- [ ] fractional XYZ interpolation;
- [ ] parameter below first key fails;
- [ ] parameter above last key fails;
- [ ] one-point Runtime sampler semantics;
- [ ] non-increasing segment fails safely.

Quaternion:

- [ ] small-angle branch uses LERP weights;
- [ ] large-angle branch uses SLERP weights;
- [ ] exact endpoint bypasses interpolation;
- [ ] no implicit shortest-path sign flip unless Runtime evidence changes;
- [ ] matrix conversion matches Runtime row-vector convention.

Mode 2:

- [ ] Hermite endpoint equality;
- [ ] interior cubic sample;
- [ ] edge tangent behavior;
- [ ] non-uniform point keys;
- [ ] quaternion behavior identical to mode 1.

Retail:

- [ ] `Grid_pb.3dp` decodes to two subpaths;
- [ ] both `maxParameter == 2`;
- [ ] both have keys `0,1,2`;
- [ ] payload consumes exactly `0xFC` bytes.

---

# 67. Compact binary reference

```text
3DP
===

No magic.
No version.

+0x00 u32 subpathCount

repeat subpathCount:

    serialized subpath header, 0x1C:
        +0x00 char name[20]
        +0x14 u32 maxParameter / endKey
        +0x18 u32 pointCount

    repeat pointCount:

        point, 0x20:
            +0x00 u32 key

            +0x04 float x
            +0x08 float y
            +0x0C float z

            +0x10 float qw
            +0x14 float qx
            +0x18 float qy
            +0x1C float qz
```

---

# 68. Compact Runtime sampling reference

```text
find adjacent points A/B such that:

    A.key <= parameter <= B.key

if no bracket:
    fail

if parameter == A.key:
    exact A

if parameter == B.key:
    exact B

t =
    (parameter - A.key)
    / (B.key - A.key)

position:
    mode 1:
        linear

    mode 2:
        cubic Hermite

orientation:
    angle = acos(dot(A.q, B.q))

    if angle <= 0.1:
        q = A.q*(1-t) + B.q*t
    else:
        q = SLERP(A.q, B.q, t)

    q -> Runtime row-vector Matrix3
```

---

# 69. Worked `Grid_pb.3dp` reference

```text
SCX:
    DEAD0000 descriptor name = Grid_pb.3dp

SCX embedded resource:
    header offset  = 0x1029
    payload offset = 0x1031
    payload size   = 0xFC / 252

3DP:
    subpathCount = 2

subpath 0:
    name         UBas.p1
    maxParameter 2
    pointCount   3
    keys         0, 1, 2
    XYZ          constant
                 (-478.393341,
                   -43.900246,
                    27.611773)

subpath 1:
    name         UBas.p2-3
    maxParameter 2
    pointCount   3
    keys         0, 1, 2
    XYZ          constant
                 (-486.818512,
                   -43.900276,
                   -79.123238)
```

---

# 70. Current format boundary

The binary format is now understood at high confidence:

```text
3DP
    |
    +-- subpath count
    |
    +-- named subpath
    |      |
    |      +-- max/end parameter
    |      +-- point count
    |      |
    |      +-- keyed point
    |             |
    |             +-- XYZ
    |             +-- WXYZ quaternion
    |
    +-- next subpath
```

The Runtime sampling architecture is also substantially recovered:

```text
floating parameter
    |
    v
find adjacent integer keys
    |
    +-- exact endpoint
    |
    +-- interior:
           |
           +-- mode 1 -> linear XYZ
           +-- mode 2 -> cubic Hermite XYZ
           |
           +-- quaternion:
                  small-angle LERP
                  otherwise SLERP
           |
           v
       orientation Matrix3
```

The main remaining uncertainties are:

- the clean mathematical/source-level description of mode-2 tangent
  generation;
- meanings of interpolation-mode values other than 1 and 2;
- corpus-wide variation;
- and a few SCX/runtime naming details rather than the fundamental 3DP grammar.
