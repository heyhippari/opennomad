# Runtime coordinate and transform math

> **Status:** work-in-progress reverse-engineering documentation for OpenNomad  
> **Last updated:** 2026-09-04
>
> This document records the coordinate, unit, matrix, quaternion, animation,
> path, camera, and presentation rules recovered directly from retail
> `Runtime.exe`.
>
> These rules take precedence over Blender-importer conventions and over
> convenience transforms introduced by OpenNomad's renderer.
>
> Related documents:
>
> - [`3do.md`](3do.md) — serialized model/object hierarchy;
> - [`3da.md`](3da.md) — skeletal/body animation streams;
> - [`3dp.md`](3dp.md) — keyed path data and interpolation;
> - [`iam-area.md`](iam-area.md) — serialized AREA positions/cameras;
> - [`runtime-globals.md`](runtime-globals.md) — process-global Runtime state;
> - [`original-toolchain.md`](original-toolchain.md) — x87/MSVC-era numerical context.

---

# 1. Evidence and terminology

Reference executable:

```text
SHA-256:
55f7120bfea7891b048c64e3682f3259cdbf2719a43fa24e42254b753c95d2ef

image base:
0x00400000
```

Confidence labels:

- **Confirmed — Runtime:** direct executable behavior.
- **Confirmed — data:** direct retail asset observation.
- **Corroborated:** executable behavior and data agree.
- **Strongly reconstructed:** multiple observations agree, but the original
  source-level name is unavailable.
- **Provisional:** useful working interpretation requiring more tracing.

---

# 2. Native Runtime coordinate space

Runtime gameplay/object coordinates use this basis:

```text
+X = right
+Y = down
+Z = forward
```

Camera-space:

```text
positive Z = in front of the camera
```

The ordinary world-space unit is:

```text
inch
```

This native coordinate system should remain the authoritative simulation space
inside OpenNomad.

Renderer-facing conversions belong at the renderer boundary.

---

# 3. Serialized 3DO coordinates

3DO vectors are serialized as ordinary:

```text
float x
float y
float z
```

Runtime does not apply a Blender-style import transform while parsing.

In particular, the following are **not** native Runtime operations:

```text
multiply by 0.025
swap Y/Z
negate an axis to become Blender space
```

Those are external-tool conventions.

A decoded 3DO position should therefore remain unchanged until a genuine
Runtime transform consumes it.

---

# 4. AREA positional integers

AREA uses a different serialized representation for several world positions.

For fields whose Runtime conversion has been independently confirmed:

```text
runtime_inches =
    trunc_toward_zero(
        serialized
        * 39.37007874015748
        / 256.0
        - 1.0
    )
```

This reproduces the original x87 / Microsoft `_ftol` path.

Do not replace it with:

```text
round()
floor()
```

because negative coordinates differ.

AREA table-2 zone vertices are a confirmed field-specific exception. Runtime
first applies the ordinary conversion to all three components, then adjusts Y:

```text
zoneRuntimeY = area_position_to_inches(serializedY) - 9
```

This adjustment belongs to zone loading only. It does not apply to character
placements, named addresses, or camera vectors. For example, serialized
`-511` converts generically to `-79`, but a table-2 zone Y becomes `-88`.

Runtime builds each static zone's spatial AABB from the four normalized XYZ
vertices, then extends only its lower Y bound by 50 cm:

```text
minimum = component_min(normalizedVertices)
maximum = component_max(normalizedVertices)
minimum.y -= 19.685039520263672 inches
```

X/Z and maximum Y receive no corresponding extension.

---

# 5. Confirmed AREA position fields

The conversion above is directly confirmed for:

```text
AREA table 0:
    character placement X/Y/Z

AREA table 5:
    named address X/Y/Z consumed by compact opcode 0x49
```

Table-5 conversion produces an address contact point. Its Y component is not
the final logical actor-origin Y: authoritative placement subtracts the actor's
raw authored body-bottom extent after conversion. X and Z are used directly.

Examples:

| Source | Serialized | Runtime inches |
|---|---|---|
| character 310 | `(-2588, -271, -816)` | `(-399, -42, -126)` |

AREA/SCENE/DIALOG camera eye/target dwords use the same numeric normalization before the camera handlers consume them. The crucial ordering is that Runtime's AREA/SCENE record loader mutates all six camera dwords in place.
For each component it performs:

```text
serialized * 100
FILD
* (1 / 256)
* 0.39370078740157477
- 1.0
_ftol / truncate toward zero
```

which is exactly:

```text
runtimeCameraInteger =
    trunc_toward_zero(
        serializedCameraDword * 39.37007874015748 / 256.0 - 1.0
    )
```

The later compact camera handler does indeed `FILD` the camera dwords directly, but by then it is reading these already-normalized in-memory records. Treating the archive dwords as final Runtime coordinates skips the loader stage and is incorrect.

---

# 6. Structurally coordinate-like AREA fields

The newer AREA format work identifies additional position-like structures:

```text
table 1:
    object/item placement X/Y/Z

table 2:
    four 3D zone points

table 5:
    address/spawn X/Y/Z
```

Their structural meaning is strong.

However, the coordinate-conversion helper above should only be applied globally
after each field family has been confirmed through its Runtime consumer.

Until then:

```text
preserve raw signed integers
```

and document the likely geometric role separately.

This avoids propagating a conversion merely by analogy.

---

# 7. AREA angular units

Confirmed signed 16-bit AREA angle fields use:

```text
degrees =
    trunc_toward_zero(
        units * 360.0 / 4096.0
    )
```

One complete turn is therefore:

```text
4096 units
```

Confirmed applications include:

```text
table-0 character orientation

table-6 camera roll
    +0x1C

table-6 horizontal FOV
    +0x1E
```

Table-5 address orientation and other angle-looking fields should remain raw
until their Runtime consumers are independently confirmed.

---

# 8. Row-vector matrix convention

Runtime uses:

```text
row-major 3x3 matrices
row vectors
```

The fundamental transform is:

```text
v' = v * M
```

Composition is:

```text
C = A * B
```

With row vectors:

```text
A is applied first
B is applied second
```

This is the opposite mental ordering from the column-vector convention used by
GLM/OpenGL examples.

---

# 9. Runtime vector transform

For:

```text
v = (x, y, z)
```

and row-major matrix:

```text
M =
[m00 m01 m02
 m10 m11 m12
 m20 m21 m22]
```

Runtime computes:

```text
x' = x*m00 + y*m10 + z*m20 + tx
y' = x*m01 + y*m11 + z*m21 + ty
z' = x*m02 + y*m12 + z*m22 + tz
```

This convention is used throughout the 3DO hierarchy and body-animation
placement code.

---

# 10. Primitive rotation matrices

Runtime's row-vector primitive rotations are:

```text
Rx(a) =
[1    0     0
 0    ca   -sa
 0    sa    ca]

Ry(b) =
[ cb   0    sb
   0   1     0
 -sb   0    cb]

Rz(c) =
[cc  -sc   0
 sc   cc   0
  0    0   1]
```

The recovered Euler builder is:

```text
Ry(y) * Rx(x) * Rz(z)
```

With row vectors, this applies:

```text
Y
then X
then Z
```

---

# 11. Object scale

Runtime applies object X/Y/Z scale by multiplying transform rows:

```text
row 0 *= scaleX
row 1 *= scaleY
row 2 *= scaleZ
```

For row-vector point transformation, this is equivalent to scaling input
components before matrix multiplication.

OpenNomad's `Runtime::Transform` models this behavior explicitly.

---

# 12. 3DO hierarchy composition

Runtime expands a serialized 3DO object descriptor into larger mutable runtime
state.

For a child object:

```text
child.translation =
    child.localOffset * parent.matrix
    + parent.translation

child.matrix =
    child.localMatrix * parent.matrix
```

The serialized child offset is taken from the object's local XYZ position
fields.

The root's authored translation is already in Runtime-native coordinate space.

---

# 13. Animation matrix placement in the hierarchy

When an object has a body-animation rotation:

```text
effectiveLocalMatrix =
    localMatrix * animationMatrix
```

before parent composition.

Thus:

```text
child.matrix =
    (localMatrix * animationMatrix)
    * parent.matrix
```

The animation matrix is not a renderer-only convention adapter.

It participates in native Runtime hierarchy math.

---

# 14. Runtime quaternion serialization

Both 3DA and 3DP serialize quaternions in:

```text
w, x, y, z
```

order.

Do not reinterpret them as:

```text
x, y, z, w
```

without an explicit conversion at an API boundary.

---

# 15. Runtime quaternion-to-matrix — `0x00442A00`

This routine was re-verified directly against the retail executable during the
2026-08-22 coordinate update.

For:

```text
q = (w, x, y, z)
```

Runtime writes:

```text
[ 1 - 2(y² + z²)    2(xy - wz)        2(xz + wy)     ]
[ 2(xy + wz)        1 - 2(x² + z²)    2(yz - wx)     ]
[ 2(xz - wy)        2(yz + wx)        1 - 2(x² + y²) ]
```

This is the authoritative coefficient layout for OpenNomad's native
`Runtime::Matrix3`.

---

# 16. Important quaternion-matrix correction

An earlier 3DA/3DP reverse-engineering note recorded the transpose/opposite-sign
variant:

```text
2(xy + wz) in row 0 column 1
...
```

That version is stale.

Direct symbolic reconstruction of `0x00442A00` confirms that the current
OpenNomad implementation in:

```text
RuntimeMath::quaternion_matrix()
```

is correct.

The 3DA/3DP documentation should use the matrix in section 15.

No code change to `RuntimeMath::quaternion_matrix()` is required.

---

# 17. Quaternion normalization

`0x00442A00` uses the supplied quaternion components directly.

It does **not** visibly normalize the quaternion before constructing the matrix.

Therefore:

```text
normalize(q)
```

is not part of the recovered native conversion.

A modern decoder should not silently normalize authored quaternions merely as a
convention fix.

---

# 18. 3DA translation semantics

A 3DA translation stream is not an ordinary absolute-position curve.

For the root channel:

```text
translation[0]
```

is an authored/reference position.

Subsequent entries are motion increments:

```text
translation[1] -> interval (0,1]
translation[2] -> interval (1,2]
...
```

Runtime integrates those increments over the requested progress interval.

This behavior is recovered at:

```text
0x004711D0
```

---

# 19. 3DA root-motion integration

For an interval:

```text
previousProgress -> currentProgress
```

Runtime accumulates the overlap with each integer frame interval.

Conceptually:

```text
delta = 0

for each interval i intersecting [previous,current]:
    overlap =
        length(
            [previous,current]
            ∩
            [i-1,i]
        )

    delta += translation[i] * overlap
```

This naturally supports fractional progress.

Sample zero is deliberately not accumulated.

---

# 20. Why 3DA sample zero must not be treated as a delta

Retail `Grid.SCX` / `INTRO1` root channel includes:

```text
translation[0] =
    approximately
    (0.6299, -158.1887, 27.6118)
```

It is a large authored reference value.

Treating it as the first movement delta incorrectly displaces Kay'l by roughly
158 native inches vertically.

The correct motion domain starts with:

```text
translation[1]
```

for interval `(0,1]`.

---

# 21. 3DA rotation sampling

Recovered Runtime body-animation rotation sampling:

```text
progress is clamped to at least 1
frame = floor(progress)
quaternion = rotation[frame]
```

There is no recovered per-frame quaternion interpolation in this path.

The selected quaternion is converted with:

```text
0x00442A00
```

and applied as the object's animation matrix.

---

# 22. 3DA optional root-motion transform

`0x004711D0` accepts an optional 3x3 transform used while integrating the
root-motion vector.

This means root motion can be accumulated in a transformed local/world
orientation rather than blindly adding raw XYZ components.

For the recovered body-animation caller, this is the live top/root actor orientation. With Runtime's row-vector convention:

```text
worldDelta = integrated3DADelta * liveRootOrientation
```

The per-frame 3DA quaternion is not part of this matrix; Runtime applies it separately to object animation state.

The selected Runtime 3DO stores a pointer to this live orientation at `+0x9C`.
It is built from the actor's base orientation plus the persistent body-animation Euler offsets, and is distinct from the per-frame 3DA quaternion matrix.

Ordering inside `Script_Select*BodyAnimation` is significant:

```text
integrate current 3DA root interval through live +0x9C orientation
    ->
store this command's args 4/5/6 as the new persistent Euler offsets
    ->
apply root displacement
```

The newly decoded args 4/5/6 therefore affect presentation and subsequent root intervals, not the interval that was just integrated.

OpenNomad mirrors this as `transform.matrix` (base orientation) plus `body_orientation_offset_degrees` (persistent offset).

---

# 22.0.1 CTL controller root motion

CTL-driven locomotion reuses the same integration and orientation transform,
with two deliberate differences from the scripted body-animation path:

- the CTL controller starts from the actor's **current live world position**
  and accumulates; 3DA translation sample 0 is a reference value and never
  anchors the actor (no `reference_translation()` placement);
- the integrated interval updates the controller's **candidate character
  position in full XYZ** — it does not use the scripted path's X/Z-actor /
  Y-visual-residual split, which exists for cinematic fidelity only.

See [ctl.md](ctl.md) §4.6.

---

# 22.1 Camera attachment orientation selectors

Native camera attachment selector `0` anchors to the current actor position and rotates the authored camera vector using only the persistent body-animation offset Euler triple. Selector `1` uses the actor base Euler orientation plus that same body offset. Both finish with `anchor - transformedVector`.

---

# 23. 3DA has no intrinsic FPS field

The 3DA format stores:

```text
lastFrame
tracks
sample streams
```

but no serialized frame-rate field.

The wider scenario/script system commonly advances animation progress on the
game's logical 30 Hz timeline.

That consumer timing must not be mislabeled as a 3DA file-format FPS.

---

# 24. 3DP path positions

A 3DP point stores:

```text
u32 key
float x
float y
float z
float qw
float qx
float qy
float qz
```

The XYZ values are consumed directly as Runtime path coordinates.

No AREA integer conversion or Blender conversion is performed while decoding
3DP.

---

# 25. 3DP parameter space

The point key is an unsigned path parameter coordinate.

It is not intrinsically:

```text
seconds
frames
array index
```

Runtime computes interpolation parameter:

```text
t =
    (parameter - from.key)
    /
    (to.key - from.key)
```

using the actual adjacent key values.

---

# 26. 3DP bracketing

The central sampler at:

```text
0x004B0C70
```

searches adjacent point pairs satisfying:

```text
from.key <= parameter <= to.key
```

If no segment brackets the parameter:

```text
sampling fails
```

The native routine does not globally clamp an out-of-range value to the first
or last point.

A one-point path cannot form a segment in this central sampler.

---

# 27. 3DP exact-key behavior

At an exact point key Runtime bypasses interpolation and copies the authored
point directly.

That includes:

```text
position
quaternion
```

For orientation, the stored quaternion then goes directly to:

```text
0x00442A00
```

This matters when comparing OpenNomad's interpolator with retail keyframe
output.

---

# 28. 3DP position interpolation mode 1

Mode 1 is ordinary linear interpolation:

```text
p(t) =
    from
    + (to - from) * t
```

for X/Y/Z independently.

This portion of the current OpenNomad implementation is correct.

---

# 29. 3DP position interpolation mode 2

Mode 2 uses cubic Hermite interpolation.

Runtime helper:

```text
0x004B0EE0
```

constructs coefficients from the segment endpoints and neighboring positions.

The basis matrix recovered at:

```text
0x004E5B00
```

is:

```text
[  2  -2   1   1 ]
[ -3   3  -2  -1 ]
[  0   0   1   0 ]
[  1   0   0   0 ]
```

Runtime evaluates:

```text
value(t) =
    ((a*t + b)*t + c)*t + d
```

The precise closed-form tangent-generation rule still deserves a dedicated
follow-up.

---

# 30. 3DP quaternion interpolation — `0x00442C80`

For interior path samples Runtime computes:

```text
dot =
    q0.w*q1.w
    + q0.x*q1.x
    + q0.y*q1.y
    + q0.z*q1.z

angle =
    acos(dot)
```

Then:

```text
if angle <= 0.1 radians:
    weight0 = 1 - t
    weight1 = t

else:
    weight0 = sin(angle * (1 - t)) / sin(angle)
    weight1 = sin(angle * t)       / sin(angle)
```

and:

```text
q =
    q0 * weight0
    + q1 * weight1
```

---

# 31. 3DP quaternion interpolation is not nlerp

For small angles Runtime uses an ordinary linear blend.

No normalization step is visible afterward.

For larger angles it uses spherical interpolation weights.

The routine also does not visibly perform the common modern shortest-path rule:

```text
if dot < 0:
    q1 = -q1
```

and no explicit dot clamp before `acos` has been recovered.

Therefore the current OpenNomad `normalized_lerp()` path is not retail behavior.

---

# 32. 3DP orientation output

The central path sampler's final orientation output is a:

```text
Runtime row-vector Matrix3
```

not merely a quaternion.

Conceptually:

```text
stored/interpolated wxyz quaternion
    ->
0x00442A00
    ->
Runtime Matrix3
```

A modern API may expose the quaternion internally, but the consumer-facing
orientation must remain convention-compatible with that matrix conversion.

---

# 33. `SelectRelativeBodyAnimation` placement

The recovered body-animation/path bridge uses an authored positional argument
with scalar:

```text
-0.393700778
```

OpenNomad expresses the recovered placement as:

```text
anchor =
    sampledPathCoordinate
    - authoredArgument * -0.393700778
```

equivalently:

```text
anchor =
    sampledPathCoordinate
    + authoredArgument * 0.393700778
```

The magnitude:

```text
0.393700778
```

is centimetres-to-inches.

This conversion belongs to this particular authored argument/path-placement
operation; it is not a general conversion for every animation/path vector.

---

# 34. Grid path/animation example

Retail `Grid_pb.3dp` path:

```text
UBas.p1
position =
    approximately
    (-478.3933, -43.90025, 27.61177)
```

Its points are positionally constant across keys 0, 1, 2.

The detailed Kay'l motion therefore comes primarily from:

```text
3DA root motion
+
body joint animation
```

rather than from a moving 3DP position curve in this specific sequence.

This is useful when debugging portal-intro placement: a moving character does
not imply the associated 3DP path itself must move.

---

# 35. Runtime camera orientation

For:

```text
direction =
    target - eye
```

Runtime derives:

```text
horizontal =
    sqrt(dx*dx + dz*dz)

yaw =
    atan2(dx, dz)

pitch =
    -atan2(dy, horizontal)
```

then:

```text
Rview =
    Ry(yaw)
    * Rx(pitch)
    * Rz(roll)
```

---

# 36. Runtime camera translation

View translation is the negative dot product of the camera eye against the
columns of `Rview`.

World-to-camera transform:

```text
pCamera =
    pWorld * Rview
    + translation
```

This gives:

```text
look +Z -> identity
look +X -> camera +Z
look +Y -> camera +Z
```

while preserving authored roll.

---

# 37. Horizontal FOV

AREA camera `+0x1E` is a horizontal FOV expressed in 4096-unit angle space.

After conversion to degrees, Runtime projection uses a 4:3 reference:

```text
tanHalf =
    tan(horizontalFov / 2)

factorX =
    (W/2) / tanHalf

factorY =
    ((H/2) * 4/3) / tanHalf

screenX =
    W/2
    + cameraX/cameraZ * factorX

screenY =
    H/2
    + cameraY/cameraZ * factorY
```

---

# 38. OpenGL vertical FOV conversion

The equivalent 4:3 vertical FOV is:

```text
verticalFov =
    2
    * atan(
        tan(horizontalFov / 2)
        / (4/3)
      )
```

At 4:3 this matches Runtime projection.

OpenNomad intentionally preserves the derived vertical FOV on widescreen,
expanding horizontal field of view instead of horizontally cropping the
original 4:3 image.

That is a modern presentation policy, not a serialized Runtime rule.

---

# 39. Camera clipping

Recovered native near distance:

```text
2 inches
```

Current OpenNomad fallback far distance:

```text
50 metres
```

converted to Runtime space:

```text
50 * 39.37007874015748
=
1968.503937... inches
```

The far value remains a fallback until the original preference/configuration
source is fully recovered.

---

# 40. Runtime-to-OpenGL presentation adapter

Only renderer-facing data should use the basis adapter:

```text
B =
diag(1, -1, -1)
```

Therefore:

```text
(x, y, z)Runtime
    ->
(x, -y, -z)OpenGL
```

There is no scale in this conversion.

Because:

```text
det(B) = +1
```

triangle winding is unchanged by the basis change itself.

---

# 41. Runtime matrix to GLM/OpenGL matrix

For Runtime row-vector rotation:

```text
R
```

the equivalent column-vector rotation after the basis conversion is:

```text
Rgl =
    B
    * transpose(R)
    * B
```

Translation is transformed through the same basis adapter.

This conversion should occur once at the presentation boundary.

---

# 42. Do not make GL space authoritative

The following should remain Runtime-native internally:

```text
world/entity positions
AREA-authored positions after native conversion
3DO object transforms
3DA root motion
3DP paths
scripted cameras
gameplay collision
audio listener/emitter world positions
```

Only the renderer consumes the OpenGL basis representation.

This avoids subtle sign/axis bugs when different game systems communicate.

---

# 43. Audio conversion

Gameplay-owned audio coordinates remain in Runtime-native inches.

At the audio backend boundary:

```text
metres =
    inches * 0.0254
```

Audio does not use the renderer's Y/Z inversion as the authoritative gameplay
space.

---

# 44. Current OpenNomad fidelity summary

Correct/current:

```text
Runtime native Vec3 basis
AREA confirmed-position conversion
AREA angle conversion
row-vector matrix multiplication
Euler order
3DO hierarchy composition
Runtime quaternion matrix
3DA root-motion delta integration
camera orientation/projection
renderer basis adapter
audio inches-to-metres boundary
```

Known gaps:

```text
3DP mode-2 cubic Hermite interpolation

3DP Runtime quaternion interpolation:
    OpenNomad currently uses normalized lerp instead

3DP out-of-range behavior:
    OpenNomad currently clamps endpoint cases

3DP single-point behavior:
    OpenNomad currently returns the point

some AREA coordinate-looking fields:
    structural role known
    Runtime normalization path not individually confirmed
```

---

# 45. Cross-document correction

The matrix in section 15 should be treated as authoritative for:

```text
runtime-coordinate-math.md
3da.md
3dp.md
RuntimeMath.cpp
```

The current code already matches it.

Any documentation showing:

```text
row 0 col 1 = 2(xy + wz)
```

for Runtime's native row-vector matrix is using the stale transposed convention
and should be corrected.

---

# 46. Explicitly unresolved

Still unresolved or incomplete:

```text
exact semantics of every AREA integer/angle-like field

3DP mode-2 tangent-generation closed form

higher-level ownership/name of the optional 3DA root-motion matrix

some camera attachment/type-specific fields after +0x1E

whether all authored malformed/non-unit quaternions are expected to be tolerated
exactly as Runtime does

exact original far-clip preference source
```

No coordinate behavior should be inferred for an unresolved field solely
because its byte pattern resembles another known coordinate.

---

# 47. Compact reference

Native space:

```text
+X right
+Y down
+Z forward
unit = inch
```

AREA confirmed position:

```text
trunc(
    serialized * 39.37007874015748 / 256
    - 1
)
```

AREA confirmed angle:

```text
trunc(
    units * 360 / 4096
)
```

Matrix:

```text
row vectors
v' = v * M
```

Quaternion:

```text
serialized w,x,y,z

M =
[1-2(y²+z²)  2(xy-wz)    2(xz+wy)
 2(xy+wz)    1-2(x²+z²)  2(yz-wx)
 2(xz-wy)    2(yz+wx)    1-2(x²+y²)]
```

Renderer adapter:

```text
(x,y,z)
    ->
(x,-y,-z)
```

Audio:

```text
inches * 0.0254
    ->
metres
```
