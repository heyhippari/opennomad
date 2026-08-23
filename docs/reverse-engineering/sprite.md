# Runtime sprite and billboard system

> **Status:** work-in-progress reverse-engineering documentation for OpenNomad  
> **Last updated:** 2026-08-22
>
> This document describes the Windows retail Runtime's sprite-instance,
> sprite-resource and sprite-rendering system.
>
> “Sprite” in Omikron does **not** mean a separate `.SPR` asset format.
> Runtime sprite instances reference effect/model resources encoded through the
> normal 3DO/3DT family, especially SCX `DEAD0004` packages whose per-object
> rectangle tables can act as animation-frame descriptors.
>
> This document supersedes the older catch-all “Sprites (SpriteInstance)”
> section in `docs/ReverseEngineering.md`.

Related documentation:

- [`3do.md`](3do.md) — 3DO object/vertex/rectangle/material serialization;
- [`3dt.md`](3dt.md) — indexed texture payloads and palette/page behavior;
- [`scx.md`](scx.md) — `DEAD0004` sprite/effect resources;
- [`runtime-coordinate-math.md`](runtime-coordinate-math.md) — native world
  basis and renderer boundary;
- [`runtime-globals.md`](runtime-globals.md) — global sprite pool pointer and
  capacity;
- [`iam-script-functions.md`](iam-script-functions.md) — native sprite
  Script_* functions.

---

# 1. Evidence model

Source precedence:

1. **Runtime executable behavior and direct disassembly**;
2. **retail `aventure.SCX` / `Grid.SCX` effect resources**;
3. **3DO/3DT format recovery**;
4. **OpenNomad sprite implementation/tests**;
5. renderer modernization.

Confidence labels:

- **Confirmed — Runtime**
- **Confirmed — data**
- **Corroborated**
- **Strongly reconstructed**
- **Provisional**
- **OpenNomad-only**

Reference executable:

```text
SHA-256:
55f7120bfea7891b048c64e3682f3259cdbf2719a43fa24e42254b753c95d2ef
```

---

# 2. High-level architecture

```text
SCX DEAD0004
    |
    +-- sprite/effect descriptor
    |       name
    |       sprite ID
    |
    +-- embedded model package
            3DO core
            3DT-style texture payload
                |
                v
        loaded effect/model resource
                |
                v
        RuntimeSpriteInstance
                |
                +-- world anchor
                +-- frame index
                +-- size/rotation/tint/etc.
                +-- render state
                |
                v
        scene intrusive sprite list
                |
                v
        renderer buckets
                |
                v
        camera-facing billboard
```

Resource lifetime and instance lifetime are separate.

---

# 3. Global sprite pool

Runtime globals:

```text
0x00660B5C
    SpriteInstance* pool

0x00660B58
    uint16_t capacity
```

Core startup initializes:

```text
capacity = 0x800
```

therefore:

```text
2048 sprite instances
```

---

# 4. Pool allocation — `0x0048EB80`

Runtime allocates:

```text
capacity * 0x40
```

bytes for the pool.

The capacity is stored in the 16-bit global.

The original pool is fixed-size after initialization.

---

# 5. Pool destruction

Runtime helper around:

```text
0x0048EBC0
```

releases the pool backing allocation.

OpenNomad instead owns its pool through modern C++ containers.

The observable instance-limit semantics are more important than matching the
heap call.

---

# 6. SpriteInstance size

Direct Runtime evidence:

```text
sizeof(RuntimeSpriteInstance) = 0x40
```

Creation clears exactly one `0x40`-byte record.

Pool scanning advances in `0x40`-byte steps.

---

# 7. Recovered RuntimeSpriteInstance layout

Current direct map:

```c
struct RuntimeSpriteInstance {
    void *renderListOwner;          // +0x00
    void *objectOrResource;         // +0x04; null = free pool slot

    float x;                        // +0x08
    float y;                        // +0x0C
    float z;                        // +0x10

    uint16_t type;                  // +0x14
    uint16_t frameIndex;            // +0x16

    float scaleX;                   // +0x18
    float scaleY;                   // +0x1C

    float rotationRadians;          // +0x20
    float diffuseAlpha;             // +0x24

    float textureOffsetU;           // +0x28
    float textureOffsetV;           // +0x2C

    uint32_t colorRgb;              // +0x30

    void *externalAssociation;      // +0x34

    RuntimeSpriteInstance *prev;    // +0x38
    RuntimeSpriteInstance *next;    // +0x3C
}; // 0x40
```

The exact storage location/path for every render-mode value is not yet
sufficiently established to insert a `renderMode` field into this binary
layout.

Keep render-mode semantics separate from the confirmed offset map until that
write/read path is pinned down.

---

# 8. Free-slot criterion

Creation helper:

```text
0x0048EBF0
```

scans the pool from the beginning.

A slot is free when:

```text
sprite +0x04 == null
```

i.e. the object/resource field is zero.

Allocation policy:

```text
first free slot
```

No compaction or stealing is involved.

---

# 9. Creation defaults

After clearing the record Runtime initializes:

```text
frameIndex      = 0xFFFF

scaleX          = 1.0
scaleY          = 1.0

rotation        = 0.0

diffuseAlpha    = 0.9
                 float bits 0x3F666666

textureOffsetU  = 0.0
textureOffsetV  = 0.0

colorRgb        = 0x00FFFFFF
```

Other cleared fields remain zero/null until assigned.

---

# 10. Invalid frame sentinel

Runtime uses:

```text
0xFFFF
```

for:

```text
no valid sprite frame selected
```

A sprite with this frame should not be rendered as though frame zero had been
requested.

OpenNomad preserves this sentinel.

---

# 11. Position

Fields:

```text
+0x08
+0x0C
+0x10
```

are native Runtime XYZ coordinates.

Setter:

```text
0x0048EEF0
```

writes the three floats.

Coordinate space:

```text
+X right
+Y down
+Z forward
unit = inch
```

The GL basis conversion belongs only at the renderer boundary.

---

# 12. Sprite type

Field:

```text
+0x14 u16
```

Setter:

```text
0x0048EF50
```

directly writes the value.

Working semantic name:

```text
sprite type
```

is supported by the recovered native setter family.

Its complete behavioral effect across renderer/game logic remains to be
inventoried.

---

# 13. Frame index

Field:

```text
+0x16 u16
```

Setter:

```text
0x0048EF10
```

validates the requested frame against the referenced object's frame count.

On valid index:

```text
frameIndex = requested
```

On invalid index:

```text
frameIndex = 0xFFFF
```

This failure-side mutation is important.

---

# 14. Scale

Fields:

```text
+0x18 float scaleX
+0x1C float scaleY
```

Setters:

```text
0x0048EF70  scale X
0x0048EF90  scale Y
```

Creation default:

```text
1.0, 1.0
```

Sprite scale is a billboard-plane scale, not 3DO hierarchy object scale.

---

# 15. Rotation

Field:

```text
+0x20 float
```

Setter:

```text
0x0048EFB0
```

The value is interpreted as rotation around the billboard center.

Current OpenNomad stores it in radians.

The recovered Runtime consumer should remain the authority for unit semantics;
current evidence is consistent with radians.

---

# 16. Field `+0x24`

**Confirmed — Runtime.** This normalized float is sprite diffuse/vertex alpha.
Runtime multiplies it by 255 at `0x00496EC3`, stores the result in both
generated `Face3D+0x6C` fields, then packs that byte into the high byte of
diffuse ARGB at backend submission around `0x00460442`.

Creation default:

```text
0.9f
```

OpenNomad names the semantic field:

```text
diffuse_alpha
```

It is copied to all six generated billboard vertex tint alphas. Texture alpha
and diffuse alpha therefore multiply in the sprite fragment shader. Additive
modes retain their confirmed `ONE, ONE` RGB blend factors; diffuse alpha does
not replace those factors.

OpenNomad's modern reconstruction draws modes 0, 1 and 8 directly into the
scene-linear HDR target using the texture's sRGB sampling representation.
Modes 2 through 7 use the encoded representation and transient legacy
operator accumulator. Alpha sources premultiply RGB by their combined alpha;
additive and darken sources deliberately do not. This pipeline boundary is an
OpenNomad implementation choice, not an additional Runtime claim.

---

# 17. Texture offsets

Fields:

```text
+0x28 float textureOffsetU
+0x2C float textureOffsetV
```

Creation default:

```text
0,0
```

They are added to the frame descriptor's normalized UV coordinates.

This supports animated/scrolling texture behavior without rewriting the source
frame table.

---

# 18. Tint colour

Field:

```text
+0x30 uint32_t
```

Creation default:

```text
0x00FFFFFF
```

Recovered interpretation:

```text
0x00RRGGBB
```

OpenNomad converts this to normalized RGB tint.

Alpha/translucency belongs to sprite render-state behavior rather than this
24-bit tint value.

---

# 19. External association

Field:

```text
+0x34
```

is an external gameplay/runtime association.

Runtime destruction performs additional cleanup when it is non-null.

Related scratch/association storage exists around:

```text
0x006A2F00
```

with occupancy bookkeeping around:

```text
0x006A2EE8
```

Exact semantic ownership remains unresolved.

OpenNomad treats the association opaquely and never dereferences an invented
type.

---

# 20. Intrusive render-list links

Fields:

```text
+0x38 prev
+0x3C next
```

form an intrusive doubly linked list.

The list owner is stored at:

```text
+0x00
```

when attached.

This list controls scene sprite traversal independently from pool allocation.

---

# 21. Attach — `0x0048ECE0`

Recovered behavior:

1. require `sprite->renderListOwner == null`;
2. set `sprite->renderListOwner = owner`;
3. read current owner list head from owner-relative storage;
4. set:
   ```text
   sprite.prev = null
   sprite.next = oldHead
   ```
5. if old head exists:
   ```text
   oldHead.prev = sprite
   ```
6. set owner head to sprite.

Thus attach:

```text
prepends at list head
```

---

# 22. Detach — `0x0048ED30`

Recovered behavior repairs both neighbors.

Conceptually:

```text
if prev:
    prev.next = next
else:
    owner.head = next

if next:
    next.prev = prev

sprite.prev = null
sprite.next = null
sprite.owner = null
```

This is ordinary intrusive-list removal.

---

# 23. Destroy/free — `0x0048EC90`

Runtime validates that the pointer belongs to the sprite pool.

It performs external-association cleanup as appropriate, then frees the pool
slot by clearing:

```text
+0x04 objectOrResource = null
```

Important:

> Runtime destruction does **not** automatically detach the sprite from its
> scene list.

The caller is expected to respect lifecycle ordering.

---

# 24. OpenNomad destroy safety difference

Current OpenNomad:

```text
destroy()
    auto-detaches if attached
```

before freeing the slot.

This deliberately prevents an intrusive-list dangling reference.

It is a safety improvement, not literal Runtime lifecycle behavior.

Document both behaviors rather than weakening the modern API.

---

# 25. OpenNomad handle safety

Runtime exposes raw pointers into a fixed pool.

OpenNomad uses:

```text
SpriteHandle {
    index
    generation
}
```

A recycled slot increments generation, invalidating stale handles.

This is an implementation safety feature with no serialized counterpart.

---

# 26. OpenNomad capacity difference

Original:

```text
fixed 2048-slot pool
allocation fails when exhausted
```

Current OpenNomad:

```text
reserves 2048 initially
can grow beyond it
```

This is currently a fidelity difference.

If exact pool-exhaustion behavior ever matters to retail scripts, a strict
compatibility mode should enforce 2048.

---

# 27. Sprite assets are effect/model resources

A sprite instance does not reference a special sprite bitmap file directly.

SCX `DEAD0004` provides:

```text
sprite/effect descriptor
+
embedded 3DO core
+
auxiliary 3DT-style texture data
```

OpenNomad decodes this into:

```text
SpriteResource
    name
    sprite_id
    Model3DOData
    decoded Texture3DT images
```

---

# 28. SCX `DEAD0004` descriptor

Serialized descriptor size:

```text
0x24
```

Current recovered fields:

```c
struct ScxSpriteEntry {
    char name[24];                    // +0x00
    uint32_t runtimePlaceholder;      // +0x18
    uint32_t serializedField1C;       // +0x1C
    uint32_t spriteId;                // +0x20
}; // 0x24
```

The appended resource uses the special SCX model-package framing documented in
`scx.md`.

---

# 29. Example sprite/effect assets

`Grid.SCX` contains:

```text
EFFECTS2_SMOKE1.3DO
EFFECTS1_IMPACT2.3DO
EFFECTS1_IMPACT1.3DO
EFFECTS3_SMOKB.3DO
```

`aventure.SCX` contains:

```text
20
```

sprite/effect model packages.

The existence of `.3DO` names here reinforces that “sprite” is an instance/use
mode over model/effect data, not its own file extension.

---

# 30. Frame descriptors use 3DO rectangle records

For sprite-capable objects, the per-object:

```text
0x20-byte rectangle table
```

acts as the frame descriptor table.

This was verified against retail `aventure.SCX` during the animated-sprite
mapping work.

A sprite frame does **not** use all four rectangle corners exactly as a static
3DO quad does.

---

# 31. Sprite-frame vertex slots

For a frame descriptor, Runtime interpretation uses:

```text
rectangle +0x00
    first vertex index

rectangle +0x04
    third vertex index
```

These are opposite corners.

The slots at:

```text
+0x02
+0x06
```

belong to the ordinary four-corner static-quad interpretation and are not the
two frame-corner references used by the sprite frame resolver.

---

# 32. Frame point records

The two frame vertex indices resolve against:

```text
the object's own vertex block
```

not a global arbitrary object.

The underlying point/vertex records use the normal 3DO vertex storage.

Only their first two position components are required to derive billboard
frame width/height.

---

# 33. Frame dimensions

Let opposite frame corners be:

```text
p0
p1
```

Then:

```text
width =
    abs(p1.x - p0.x)

height =
    abs(p1.y - p0.y)
```

Negative authored corner deltas are normal.

Do not treat them as invalid or use their sign to infer final UV mirroring.

---

# 34. Degenerate frames

If:

```text
width == 0
or
height == 0
```

the frame is degenerate.

A modern renderer should skip/report it rather than constructing invalid
geometry.

---

# 35. Sprite-frame UV slots

The frame uses UV byte pairs at:

```text
rectangle +0x08/+0x09
rectangle +0x0C/+0x0D
```

corresponding to the first and third frame corners.

This was the critical distinction in the earlier animated-sprite texture
mapping fix.

Using the wrong UV pairs makes frames animate but sample the wrong texture
region.

---

# 36. UV normalization

Runtime conversion:

```text
normalized =
    byteValue / 256.0
```

not:

```text
byteValue / 255.0
```

Then per-instance offsets are added:

```text
u += sprite.textureOffsetU
v += sprite.textureOffsetV
```

This is authoritative for the recovered sprite path.

---

# 37. Frame material/texture

Rectangle:

```text
+0x10
```

material ID selects the resource texture/material used by that frame.

The material resolves through the same decoded 3DO/3DT resource.

Each frame can therefore select its own material index.

---

# 38. Frame count

Current unresolved issue:

```text
3DO root frameCount field around +0x48
```

is zero in every observed relevant retail file.

OpenNomad currently uses:

```text
if serialized root frameCount != 0:
    use it
else:
    use object's rectangle count
```

The fallback is practical and works with observed resources.

The exact original meaning/use of that serialized root field remains
provisional.

---

# 39. Render modes

Recovered sprite render-mode numeric domain:

```text
0..8
```

Values remain distinct even where two produce the same observed renderer
state.

Current names:

```text
0 default
1 cutout
2 alpha
3 alpha + cutout
4 additive
5 additive + cutout
6 darken
7 darken + cutout
8 alternate cutout
```

These names describe recovered behavior, not necessarily original enum labels.

---

# 40. Render bucket bits

Recovered mapping:

```text
mode 0:
    0x0000

mode 1:
    0x0400

mode 2:
    0x2000

mode 3:
    0x2400

mode 4:
    0x2100

mode 5:
    0x2500

mode 6:
    0x2200

mode 7:
    0x2600

mode 8:
    0x0400
```

Modes 1 and 8 are distinct gameplay values but share observed bucket behavior.

---

# 41. Cutout modes

Bucket flag:

```text
0x0400
```

selects the cutout/colour-key path.

Modern OpenNomad implements this as alpha testing/discard in the sprite shader.

The original used old renderer/palette/colour-key semantics.

Visible transparency behavior is the fidelity target, not the exact API.

---

# 42. Standard alpha modes

For the recovered `0x2000` translucent family:

```text
blend enabled

source:
    source alpha

destination:
    one minus source alpha

depth write:
    disabled

depth test:
    remains enabled
```

Translucent sprites are also excluded from the normal scene fog path.

---

# 43. Additive modes

Recovered blend:

```text
source      = ONE
destination = ONE
```

with:

```text
depth write disabled
depth test enabled
```

Cutout variant adds the cutout condition.

---

# 44. Darken modes

Recovered blend:

```text
source      = ZERO
destination = ONE_MINUS_SOURCE_COLOR
```

with depth writes disabled.

This produces inverse-source-colour darkening rather than ordinary alpha
transparency.

---

# 45. Fog behavior

Recovered:

```text
0x2000 translucent sprites:
    not fogged
```

Additional bucket flag:

```text
0x0800
```

doubles the fog range.

Current OpenNomad reserves/supports the semantic flag but its exact original
fog-distance equation remains incomplete.

---

# 46. Second UV path

Recovered bucket flag:

```text
0x0040
```

selects a second-UV/multitexture-related path.

Current OpenNomad reserves this in:

```text
SpritePipelineKey::second_uv
```

but does not yet reproduce the original full rendering path.

Do not silently render such a path as an ordinary single-texture sprite without
diagnostic visibility.

---

# 47. Renderer-wide grayscale

Runtime sprite rendering includes a grayscale conversion:

```text
Y =
    (299*R + 587*G + 114*B) / 1000
```

OpenNomad reproduces this with:

```text
0.299
0.587
0.114
```

shader weights.

This is a renderer state/effect, not a mutation of sprite texture resources.

---

# 48. Render buckets

Original Runtime maintains:

```text
16,384 render buckets
```

keyed by packed texture/state information.

Sprites are inserted/traversed through the original renderer's bucket pipeline
rather than each issuing a standalone API draw immediately.

---

# 49. OpenNomad render batching

OpenNomad modernizes this to:

```text
SpritePipelineKey
+
SpriteDrawCommand
+
stable sort/batches
+
dynamic GPU vertex buffer
```

This preserves the useful idea:

```text
group equivalent texture/render state
```

without recreating a huge process-global fixed bucket array.

---

# 50. Original billboard projection

Runtime performs substantial sprite billboard/projection work on the CPU for
the old renderer.

OpenNomad instead:

1. keeps sprite anchor in Runtime-native world space;
2. converts anchor once at the GL presentation boundary;
3. constructs a camera-facing world-space quad from camera right/up;
4. lets the modern GPU view/projection matrix finish transformation.

This is a rendering modernization.

The visible billboard geometry should match Runtime behavior.

---

# 51. Camera-facing quad construction

Current modern implementation uses:

```text
halfWidth  = frame.width  * scaleX / 2
halfHeight = frame.height * scaleY / 2
```

then rotates local billboard coordinates by:

```text
sprite.rotation
```

and expands them along:

```text
camera right
camera up
```

around the sprite anchor.

This is a clean equivalent representation of a centered billboard.

---

# 52. Depth behavior

OpenNomad currently culls sprites:

```text
behind camera
outside near/far depth range
```

before queueing geometry.

The exact old renderer clipping path should remain the authority for
pixel-perfect edge cases.

---

# 53. Tint behavior

Runtime's:

```text
0x00RRGGBB
```

colour becomes normalized RGB multiplicative tint in OpenNomad.

The separate `+0x24` diffuse alpha becomes the vertex alpha byte. Runtime and
OpenNomad multiply texture and vertex alpha through the material stage.

---

# 54. Sprite render-list order

Attach prepends at the head.

OpenNomad sorts first by confirmed ascending `bucket_bits(render_mode)`, then by
texture/pipeline identity. Within an identical key it uses stable ordering so
scene-list insertion order is retained. This reconstructs the confirmed state
domain—not every unknown bit of Runtime's full material key—and guarantees
additive `0x2100` is submitted before darken `0x2200` across textures.

---

# 55. Resource versus render-list owner

A sprite can conceptually:

```text
use one loaded effect resource
```

while:

```text
belonging to a particular scene/render list
```

These ownership concepts are independent.

Current OpenNomad's:

```text
resource_index
render_list_owner
```

separation is correct.

---

# 56. Structured sprite script functions

The SCX structured-script system contains sprite operations, including recovered
families for:

```text
create/display sprite
set sprite frame
set sprite type
set sprite scale
set sprite rotation
set sprite render mode
display 3D sprite on path
```

Exact native function IDs and reset/handler mapping belong in
`iam-script-functions.md`.

The sprite runtime described here is the substrate those native functions
mutate.

---

# 57. Display sprite on path

Recovered structured operation:

```text
Script_Display3DSpriteOnPath
```

uses:

```text
3DP path sampling
+
sprite instance
```

to place an effect in Runtime-native world space.

This is a good example of subsystem composition:

```text
SCX script
    ->
3DP sampler
    ->
SpriteInstance XYZ
    ->
sprite renderer
```

Path coordinate semantics belong in `3dp.md`.

---

# 58. Current OpenNomad SpriteInstance model

OpenNomad intentionally does **not** reproduce the binary `0x40` memory layout.

It stores semantic fields:

```text
handle
resource/object indices
position
render mode
frame
type
scale
rotation
diffuse alpha
texture offsets
tint
external association
render-list owner
```

This is preferable on 64-bit hosts.

The dedicated documentation preserves the old offsets separately.

---

# 59. Binary-layout caveat: render mode

Older notes described the 0x40 record as though `renderMode` were already
assigned an exact member offset.

The current direct offset reconstruction instead accounts cleanly for:

```text
+00 owner
+04 resource
+08..10 position
+14 type
+16 frame
+18..1C scale
+20 rotation
+24 diffuse alpha
+28..2C UV offsets
+30 RGB
+34 association
+38/+3C links
```

Therefore:

> render-mode behavior is recovered, but its exact storage path should remain
> separately documented until the setter/consumer establishes the live offset
> or external association conclusively.

Do not force it into the 0x40 layout by displacing confirmed members.

---

# 60. Association helper region

Functions around:

```text
0x0048ED80
0x0048EDE0
```

participate in the external association/scratch allocation lifecycle.

The associated global storage around:

```text
0x006A2EE8
0x006A2F00
```

appears to have a small fixed occupancy-backed pool.

Exact semantic type remains a good future target.

---

# 61. Useful Runtime function anchors

```text
0048EB80  initialize sprite pool
0048EBC0  release sprite pool
0048EBF0  create sprite
0048EC90  destroy/free sprite slot
0048ECE0  attach to render list
0048ED30  detach from render list

0048EEF0  set position
0048EF10  set frame
0048EF50  set type
0048EF70  set scale X
0048EF90  set scale Y
0048EFB0  set rotation
```

Additional setters should be added only after direct confirmation.

---

# 62. Useful renderer anchors

Previously recovered traversal/lookup regions include:

```text
0x004969C0
    sprite rendering/list walk region

0x00497860
    sprite lookup/ID-related region
```

Exact function names should remain working labels until full call boundaries are
rechecked.

---

# 63. Recommended regression tests — pool

- [ ] default compatibility capacity 2048;
- [ ] first-free allocation;
- [ ] `+0x04` equivalent occupancy semantic;
- [ ] frame defaults `0xFFFF`;
- [ ] scale defaults 1/1;
- [x] diffuse alpha default 0.9;
- [ ] UV offsets default 0/0;
- [ ] tint default white;
- [ ] attach prepends;
- [ ] detach repairs both neighbors;
- [ ] modern stale handles fail safely;
- [ ] strict Runtime test documents that original destroy does not auto-detach.

---

# 64. Recommended regression tests — frame resolution

- [ ] rectangle count fallback documented/provisional;
- [ ] use vertex slots 0 and 2;
- [ ] resolve against object-local vertex block;
- [ ] dimensions from absolute XY deltas;
- [ ] reject zero dimensions;
- [ ] UV uses bytes 0/1 and 4/5 of rectangle UV array;
- [ ] normalize by 256, not 255;
- [ ] add per-instance UV offsets;
- [ ] material ID selects frame texture.

---

# 65. Recommended regression tests — render modes

For modes `0..8`:

- [ ] numeric value preserved;
- [ ] correct bucket bits;
- [ ] correct blend enable/factors;
- [ ] correct depth-write state;
- [ ] depth test remains enabled;
- [ ] cutout variants preserved;
- [ ] mode 1 and mode 8 remain distinct values;
- [ ] translucent modes bypass normal fog;
- [ ] second-UV and doubled-fog flags remain visible even if unsupported.

---

# 66. Recommended strictness policy

Modern implementation should safely reject:

```text
stale handles
frame outside object frame table
point index outside object block
material outside table
degenerate frame dimensions
missing resource
```

Runtime trusted retail data more aggressively.

Safety checks do not reduce fidelity when valid assets produce identical
results.

---

# 67. Compact binary reference

```text
RuntimeSpriteInstance
size 0x40

+00 owner/list context
+04 object/resource ptr; null = free
+08 x
+0C y
+10 z
+14 type
+16 frame
+18 scaleX
+1C scaleY
+20 rotation
+24 diffuse alpha (default 0.9)
+28 texture U offset
+2C texture V offset
+30 00RRGGBB
+34 external association
+38 prev
+3C next
```

Pool:

```text
pointer   00660B5C
capacity  00660B58
default   0x800 = 2048
stride    0x40
```

---

# 68. Compact frame reference

Per-object 3DO rectangle:

```text
+00 frame point 0 index
+04 frame point 1 index
+08/+09 UV0
+0C/+0D UV1
+10 material ID
```

Frame:

```text
width  = abs(p1.x - p0.x)
height = abs(p1.y - p0.y)

u,v = byte / 256.0 + instance offset
```

---

# 69. Compact render-mode reference

```text
0 -> 0000 default
1 -> 0400 cutout
2 -> 2000 alpha
3 -> 2400 alpha+cutout
4 -> 2100 additive
5 -> 2500 additive+cutout
6 -> 2200 darken
7 -> 2600 darken+cutout
8 -> 0400 alternate cutout
```

Other flags:

```text
0040 second UV/multitexture
0800 doubled fog range
```

---

# 70. Boundary of current knowledge

Strongly recovered:

```text
0x40 instance size/layout
2048-slot pool
creation defaults
first-free allocation
intrusive attach/detach
frame setter failure behavior
sprite/effect SCX resource relationship
rectangle-as-frame interpretation
UV byte selection and /256 scaling
render modes and bucket bits
ascending bucket traversal
blend/depth/fog categories
grayscale weights
field +0x24 diffuse/vertex alpha
```

Still incomplete:

```text
exact render-mode storage offset/path
external association type
root 3DO frameCount meaning
second-UV path
exact fog equation
all sprite Script_* setters
complete material identity ordering within equal state buckets
```

The central architectural rule is:

> A Runtime sprite is a small mutable billboard instance over ordinary
> effect/model resources. The 3DO rectangle table supplies frame geometry/UV
> metadata; the sprite instance supplies placement and presentation state; the
> renderer turns the selected frame into a camera-facing effect.
