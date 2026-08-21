# Runtime coordinate and transform math

This document records the coordinate, matrix, camera, and presentation rules
recovered directly from retail `Runtime.exe`. These rules take precedence over
Blender importer conventions and OpenNomad's former presentation-local state.

## Native Runtime space

Runtime gameplay and object state uses inches with this basis:

- `+X`: right
- `+Y`: down
- `+Z`: forward
- camera-space positive Z: in front of the camera

Serialized 3DO vectors are ordinary XYZ floats in that order. Resource parsing
does not scale, swap, or negate their components. In particular, the Blender
importer's `0.025`, axis swap, and sign change are not Runtime operations.

## AREA positional integers

AREA positional integers are not already native coordinates. Only fields whose
use has been independently confirmed should be normalized. Runtime materializes
the confirmed fields with:

```text
runtime_inches = trunc_toward_zero(
    serialized * 39.37007874015748 / 256.0 - 1.0)
```

This reproduces Runtime's integer/x87/`_ftol` path. `round` and `floor` are not
equivalent. Confirmed fields are table-0 character XYZ and table-6 camera eye
and target XYZ. Examples:

| Source | Serialized | Runtime inches |
|---|---|---|
| character 310 | `(-2588, -271, -816)` | `(-399, -42, -126)` |
| camera 2148 eye | `(-3178, -246, -1507)` | `(-489, -38, -232)` |
| camera 2148 target | `(-3157, -316, -743)` | `(-486, -49, -115)` |

Other AREA integers remain raw until their field-specific semantics are traced.

Confirmed signed 16-bit angular fields use:

```text
degrees = trunc_toward_zero(units * 360.0 / 4096.0)
```

This applies to table-0 character orientation, camera roll at `+0x1C`, and
camera horizontal FOV at `+0x1E`.

## Row-vector matrices

Runtime matrices are row-major 3x3 matrices. Vectors are row vectors:

```text
v' = v * M
C  = A * B
```

Therefore `A` is applied before `B`. The primitive rotations are:

```text
Rx(a) = [1   0    0 ]   Ry(b) = [ cb  0  sb]   Rz(c) = [cc -sc 0]
        [0   ca  -sa]           [  0  1   0]           [sc  cc 0]
        [0   sa   ca]           [-sb  0  cb]           [ 0   0 1]
```

The recovered Euler builder is `Ry(b) * Rx(a) * Rz(c)`, applying Y, then X,
then Z. Runtime's vertex transform is:

```text
x' = x*m00 + y*m10 + z*m20 + tx
y' = x*m01 + y*m11 + z*m21 + ty
z' = x*m02 + y*m12 + z*m22 + tz
```

Object scale X/Y/Z multiplies transform rows 0/1/2 before this operation.

## 3DO object hierarchy

Runtime expands each serialized `0x8C` object into `0xB8` bytes of runtime
state. Local orientation starts at identity and scale at `(1,1,1)`. The root
translation is its serialized native position. A child uses its serialized
offset at `+0x80/+0x84/+0x88`:

```text
child.translation = child.localOffset * parent.matrix + parent.translation
child.matrix      = child.localMatrix * parent.matrix
```

When an animation matrix exists, the effective local matrix is
`localMatrix * animationMatrix` before parent composition. OpenNomad preserves
serialized descriptors and stores this resolved state separately; animation
format semantics and opcode `0x0200002A` remain unresolved and unimplemented.

## Runtime camera

For `direction = target - eye`:

```text
horizontal = sqrt(dx*dx + dz*dz)
yaw   = atan2(dx, dz)
pitch = -atan2(dy, horizontal)
Rview = Ry(yaw) * Rx(pitch) * Rz(roll)
```

View translation is the negative dot product of the eye with each matrix
column. World-to-camera transformation is `pWorld * Rview + translation`.
This makes look `+Z` identity, maps look `+X` to camera `+Z`, maps look `+Y`
to camera `+Z`, and retains roll.

Camera `+0x1E` is a horizontal 4:3 FOV. Runtime projection uses:

```text
tanHalf = tan(horizontalFov / 2)
factorX = (W/2) / tanHalf
factorY = ((H/2) * 4/3) / tanHalf
screenX = W/2 + cameraX/cameraZ * factorX
screenY = H/2 + cameraY/cameraZ * factorY
```

OpenGL receives the equivalent vertical FOV:

```text
verticalFov = 2 * atan(tan(horizontalFov / 2) / (4/3))
```

At 4:3 this matches Runtime. OpenNomad preserves that derived vertical FOV on
widescreen, intentionally expanding horizontal view. The native near distance
is 2 inches. Until preference loading is recovered, far distance defaults to
`50 metres * 39.37007874015748 = 1968.503937... inches`.

## Presentation and audio boundaries

Only renderer-facing data uses the OpenGL basis adapter:

```text
B = diag(1, -1, -1)
(x, y, z)Runtime -> (x, -y, -z)GL
```

There is no scale. `det(B) = +1`, so winding is unchanged. For a Runtime
rotation `R`, GLM's equivalent column-vector rotation is
`B * transpose(R) * B`; translation is transformed through `B`. Geometry,
scripted cameras, sprites, and model-viewer diagnostics share this adapter.

Gameplay-owned listener and emitter positions remain Runtime-native. The
software audio boundary converts inches to metres with exactly `* 0.0254`; it
does not make renderer coordinates authoritative.

## Explicitly unresolved

Camera attachment fields, animation formats, opcode `0x0200002A`, and AREA
integer fields outside the confirmed character/camera positions remain
unresolved. No coordinate behavior should be inferred for them by analogy.
