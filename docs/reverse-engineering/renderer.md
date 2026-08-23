# Runtime 3D renderer architecture

> **Status:** work-in-progress reverse-engineering documentation for OpenNomad  
> **Last updated:** 2026-08-22
>
> This document describes the architecture of the Windows retail
> `Runtime.exe` 3D renderer: renderer initialization, legacy DirectDraw /
> Direct3D state, render-context setup, indexed texture and palette memory,
> backend callbacks, object/polygon submission, sorting/buckets, lighting,
> sprites, and the boundary to OpenNomad's modern OpenGL renderer.
>
> This is deliberately **not** a description of OpenNomad's current
> `WorldRenderer` presented as though it were the original engine. The original
> Runtime and the modern implementation solve the same presentation problem
> with very different resource and API architectures.
>
> The existing [`../Rendering.md`](../Rendering.md) remains useful as a
> description of the **current OpenNomad renderer**. This file is the
> Runtime-first reverse-engineering counterpart.

Related documentation:

- [`3do.md`](3do.md) — serialized model/object/material/mesh data;
- [`3dt.md`](3dt.md) — indexed texture and palette payloads;
- [`sprite.md`](sprite.md) — billboard instances, frame descriptors and sprite
  render modes;
- [`runtime-coordinate-math.md`](runtime-coordinate-math.md) — Runtime native
  coordinates, row-vector transforms, camera and projection conventions;
- [`runtime-globals.md`](runtime-globals.md) — renderer allocations, callback
  slots and DirectX globals;
- [`original-toolchain.md`](original-toolchain.md) — DirectX 6.1-era platform
  environment and original `libdirect3d` source-path evidence;
- [`runtime-main-loop.md`](runtime-main-loop.md) — frame-driver ordering;
- [`interface.md`](interface.md) — separate 2D/I2D presentation architecture.

---

# 1. Evidence model

Sources are ranked:

1. **direct `Runtime.exe` machine-code behavior;**
2. **retail 3DO/3DT/SCX assets;**
3. **embedded original source paths and diagnostics;**
4. **legacy Direct3D/DirectDraw enum/API definitions;**
5. **current OpenNomad implementation and tests;**
6. older importer/tool nomenclature.

Confidence labels:

- **Confirmed — Runtime:** directly established by executable behavior.
- **Confirmed — data:** directly established from retail data.
- **Corroborated:** Runtime and retail data independently agree.
- **Strongly reconstructed:** multiple machine-code/data observations agree,
  but original source-level name is absent.
- **Provisional:** useful working interpretation requiring more tracing.
- **Importer-derived:** inherited from older tooling and not yet independently
  established by Runtime.
- **OpenNomad-only:** deliberate modern renderer behavior.

Reference executable:

```text
SHA-256:
55f7120bfea7891b048c64e3682f3259cdbf2719a43fa24e42254b753c95d2ef

image base:
0x00400000
```

---

# 2. Architectural overview

The original PC path can be summarized as:

```text
3DO / 3DT resource data
        |
        v
Runtime resource loader
        |
        +-- indexed 256×256 texture-page memory
        +-- RGB palette-page memory
        +-- converted 15/16-bit palette memory
        +-- renderer/backend-specific surfaces/resources
        |
        v
loaded Runtime3DObject / materials
        |
        v
RenderContext
        |
        +-- camera / projection setup
        +-- object hierarchy transforms
        +-- clipping
        +-- lighting/overlap
        +-- sprite traversal
        +-- polygon submission
        |
        v
Vertex2D / Face3D working buffers
        |
        v
16,384-entry sort/bucket structure
        |
        v
selected renderer backend callback
        |
        v
legacy Direct3D / DirectDraw presentation
```

OpenNomad instead uses:

```text
decoded 3DO / 3DT
        |
        v
immutable CPU model data
        |
        v
RGBA GPU textures + vertex/index buffers
        |
        v
OpenGL shaders / draw passes
```

The second architecture should reproduce the visible semantics of the first
without reproducing its global-memory design.

---

# 3. Original source-module provenance

`Runtime.exe` preserves original source-path strings including:

```text
C:\Omikron\Sources\libdirect3d\bw.c
C:\Omikron\Sources\libdirect3d\InitCartes.c
C:\Omikron\sources\libdirect3d\acc3d.c
C:\Omikron\sources\libdirect3d\include\acc3d.h
C:\Omikron\sources\libdirect3d\acc3Dprivate.h

C:\Omikron\Sources\libscreen\libscreen.c
C:\Omikron\Sources\libpoly2d\gereaff.c
C:\Omikron\Sources\LIBI2D\libi2dpc.c
```

This establishes original module boundaries:

```text
libdirect3d
libscreen
libpoly2d
LIBI2D
```

The 3D renderer's native diagnostics heavily reference:

```text
acc3d.h
```

and related `libdirect3d` sources.

Names such as:

```text
Acc3D_...
```

are therefore reasonable Ghidra module prefixes when callgraph/string-xref
evidence agrees.

---

# 4. DirectX generation

Runtime contains the literal diagnostic:

```text
DirectX error : This game needs DirectX 6.1 or higher.
```

The PC renderer is therefore built around a:

```text
DirectX 6.1-era API model
```

with:

- DirectDraw creation/enumeration imported through `PATCH.dll`;
- legacy Direct3D COM interfaces;
- explicit render-state calls through device vtables;
- old-style indexed texture/palette/surface management.

Do not model it as a D3D9/11-style programmable-pipeline renderer.

---

# 5. DirectDraw entry boundary

The analyzed executable imports:

```text
PATCH.dll!DirectDrawCreate
PATCH.dll!DirectDrawEnumerateA
```

rather than importing those functions directly from `DDRAW.dll`.

The precise purpose/provenance of `PATCH.dll` remains unresolved.

For renderer architecture the important fact is:

```text
Runtime
    ->
PATCH.dll DirectDraw boundary
    ->
legacy DirectDraw / Direct3D objects
```

See `original-toolchain.md` for the platform/toolchain implications.

---

# 6. Direct3D device-like global

Global:

```text
0x0080B068
```

is repeatedly dereferenced as a COM object and receives legacy Direct3D-style
render-state calls through vtable offset:

```text
+0x58
```

A safe working label is:

```c
IDirect3DDeviceLike *g_d3dDevice;
```

The exact original interface revision:

```text
IDirect3DDevice2
IDirect3DDevice3
...
```

must remain unresolved until the complete vtable/IID path is typed.

---

# 7. Renderer mode/configuration globals

Known renderer-wide state includes:

```text
0x004C951C  renderer configuration byte
0x0053ADF0  renderer mode/state
0x0053AAC0  renderer/device status byte
0x0052B8D8  renderer resource/backend variant index
```

Relevant accessors:

```text
0x0045EF20  set renderer configuration byte
0x0045EF30  get device/status byte
0x0045EF40  set renderer mode
0x0045EF50  get renderer mode
0x0045EF60  renderer/DirectX initialization
```

`0x0053ADF0` is clearly a mode value, but its complete user-facing enum is not
yet recovered.

---

# 8. DirectX initialization — `0x0045EF60`

Renderer initialization:

```text
0x0045EF60
```

clears and constructs a broad DirectX state cluster.

Observed globals include:

```text
0x0080B060
0x0080B064
0x0080B068
0x0080B078
0x0080B07C
0x0080B084

0x007CADxx
0x0053ADxx
0x006A53xx
```

The function:

- stores the requested renderer mode;
- initializes status fields;
- creates/queries legacy DirectDraw objects;
- produces DirectX-specific diagnostics on failure;
- participates in graphics-card/device selection.

Do not type this entire region as one structure until the DirectDraw and
Direct3D object ownership boundaries are individually established.

---

# 9. Device/card selection diagnostics

Runtime contains:

```text
DirectX error : This game needs DirectX 6.1 or higher.

DirectX error : DirectDraw could not be created.

Voulez-vous utiliser cette carte video ?
```

The last string is direct evidence that the original graphics initialization
could enumerate/select a video device/card rather than blindly accepting one
default adapter.

Original path:

```text
C:\Omikron\Sources\libdirect3d\InitCartes.c
```

is particularly relevant to this device-selection layer.

---

# 10. Core 3D-engine initialization — `0x004403E0`

A separate 3D-engine initialization function:

```text
0x004403E0
```

constructs the CPU-side renderer/resource infrastructure after low-level
platform setup.

It performs, in broad order:

```text
initialize conversion/projection tables
initialize capacities/state
select default resource/render backend
allocate sort/bucket array
allocate Vertex2D render buffer
allocate Face3D render buffer
allocate conversion mapping table
initialize lighting/object-overlap subsystem
initialize sprite pool
```

This is a central bridge between generic engine data and the chosen graphics
backend.

---

# 11. `SortArrayLo`

Runtime allocates:

```text
0x4000 × 4
=
0x10000 bytes
```

at:

```text
0x009070B8
```

Failure diagnostic:

```text
Error while initializing engine : not enough memory for SortArrayLo
```

Thus the original internal name:

```text
SortArrayLo
```

is directly preserved.

---

# 12. 16,384-entry sort/bucket structure

At render-context execution:

```text
0x00441030
```

Runtime clears exactly:

```text
0x4000 dwords
```

starting at:

```text
g_sortArrayLo
```

every pass/frame-context setup.

The sprite renderer and polygon submission paths build packed sorting/state
keys that index into this rendering infrastructure.

Safe wording:

> Runtime has a 16,384-entry low sort/bucket array cleared before submission.

The exact complete key layout for every polygon category is not yet fully
decoded, so avoid declaring every bit of `SortArrayLo` to be a direct
material-state field.

---

# 13. Vertex2D render buffer

Global:

```text
0x009070E0
```

is allocated as:

```text
vertexCapacity * 0x30
```

Failure diagnostic:

```text
Vertex2D (Render Buffer)
```

Therefore:

```text
sizeof(RuntimeVertex2D) = 0x30
```

is strongly established.

The structure contains transformed/projected vertex state rather than the
serialized `0x20`-byte 3DO vertex record.

---

# 14. Face3D render buffer

Global:

```text
0x00907320
```

is allocated as:

```text
faceCapacity * 0x7C
```

Failure diagnostic:

```text
Face3D (Render Buffer)
```

Therefore:

```text
sizeof(RuntimeFace3D) = 0x7C
```

is confirmed structurally.

This is a runtime polygon/face structure used after model decoding and
transform/clipping.

---

# 15. Renderer capacities

Known globals:

```text
0x00907308  vertex/render capacity
0x009070D4  face/render capacity
0x009070B4  related working/copy capacity
```

Exact semantic differences among all counters remain incomplete.

Do not collapse them into one `maxVertices` value until every producer/consumer
is typed.

---

# 16. Conversion mapping table

Runtime allocates:

```text
256 × 4 bytes
```

at:

```text
0x009070CC
```

with diagnostic:

```text
conv mapping table
```

`0x004403E0` fills it from a numeric conversion formula.

Polygon submission later indexes this table by byte-valued texture coordinates
before adding per-object/global UV phases.

This is one reason the old renderer can work efficiently with byte-scale UV
source data.

---

# 17. Native indexed texture architecture

Runtime does **not** expand every material into an independent 32-bit RGBA
texture on load.

It maintains shared indexed pages:

```text
page dimensions:
    256 × 256

bytes per texel:
    1

page size:
    0x10000 = 65536 bytes
```

Usable page base:

```text
0x009070B0
```

---

# 18. Indexed page allocation

Allocator:

```text
0x004406B0
```

allocates raw storage:

```text
(texturePageCount + 1) << 16
```

at:

```text
0x009070C8
```

then aligns the usable base to:

```text
0x10000
```

and stores it at:

```text
0x009070B0
```

Thus a page address is naturally:

```text
g_indexedTexturePages
    + pageIndex * 0x10000
```

The extra allocated page exists to permit 64-KiB alignment.

---

# 19. Texture-page descriptors

Runtime allocates:

```text
texturePageCount * 0x24
```

at:

```text
0x009070DC
```

These records track resource/occupancy/backend state associated with indexed
texture pages.

The complete `0x24` schema remains unfinished.

---

# 20. RGB palette pages

Global:

```text
0x009070BC
```

holds:

```text
totalPageCount * 256 * 3
```

bytes.

One full RGB palette page is therefore:

```text
256 × RGB888
=
0x300 bytes
```

This is separate from both:

```text
indexed texture pixels
```

and:

```text
converted display-format palette data
```

---

# 21. Material/palette page descriptors

Global:

```text
0x00907324
```

is allocated as:

```text
totalPageCount * 0x20
```

These descriptors are used by palette/material allocation.

Current safe type:

```text
RuntimeMaterialPage[totalPageCount]
```

with:

```text
sizeof(RuntimeMaterialPage) = 0x20
```

and unresolved fields.

---

# 22. 3DO material runtime allocation fields

The serialized/runtime `0x50` material descriptor contains:

```text
+0x40 texture page index
+0x42 texture slot index

+0x44 palette page index
+0x46 palette slot index

+0x48 palette bit depth

+0x4A atlas U/X placement
+0x4B atlas V/Y placement

+0x4C width
+0x4E height
```

Retail files commonly carry sentinel values before load.

Runtime mutates/reuses these fields after page/palette allocation.

See `3do.md` for the complete serialized record.

---

# 23. Sub-page atlas allocation

Runtime can place material texture data into the shared 256×256 page.

The allocation code requires dimensions:

```text
< 256
```

and uses divisions of:

```text
256 / width
256 / height
```

with exact divisibility checks in relevant paths.

The material's runtime byte fields:

```text
+0x4A
+0x4B
```

store page-placement information.

OpenNomad currently does not need to reproduce this atlas allocator because
modern GPU textures are independent objects.

---

# 24. Native display-format conversion tables

Function:

```text
0x00440A20
```

builds 256-entry contribution tables from native display pixel-format channel
masks.

Known table bases:

```text
0x00907540
0x00907340
0x00907740
```

A following helper around:

```text
0x00440AF0
```

converts RGB to native 15/16-bit output by table lookup and summation.

This is part of Runtime's explicit adaptation to the selected display pixel
format.

---

# 25. Converted palette memory

Setup around:

```text
0x00483B80
```

allocates a renderer-native converted palette arena.

Globals:

```text
0x0065FE70
    raw allocation

0x00657D98
    aligned converted palette base
```

Conversion routine:

```text
0x00483C80
```

uses:

```text
RGB palette bytes
selected screen pixel format
conversion/gamma-related tables
```

to generate 15/16-bit display-oriented palette values.

---

# 26. Converted palette slot stride

`0x00483C80` indexes converted palette storage with a large per-slot stride
around:

```text
0x2000
```

The exact internal sublayout across different palette depths/modes requires
additional typing.

The important architecture is clear:

```text
authored RGB palette
    ->
renderer/display-specific converted palette
```

rather than one globally assumed RGB565 format.

---

# 27. Texture-loader scratch

3DO/3DT loading uses process-global staging addresses including:

```text
0x0067A3E8
0x006823E8
```

They participate in:

- file reads;
- compressed input;
- decompression output;
- indexed pixel copying.

Their address spacing does **not** justify declaring them two independent
64-KiB arrays.

Treat them as working addresses inside a larger overlapping/static scratch
arena until exact linker/BSS boundaries are recovered.

---

# 28. Renderer resource/backend variants

Runtime supports two selectable families of renderer-resource callbacks.

Backend index:

```text
0x0052B8D8
```

Selection helpers:

```text
0x0042F9A0
0x0042FA00
0x0042FA60
0x0042FA70
```

`0x0042FA00(index)` copies one callback from each static two-entry table into
active mutable callback slots.

---

# 29. Backend callback tables

Static tables:

```text
0x004C4910:
    variant 0 -> 0x0042FC10
    variant 1 -> 0x0042FE80

0x004C4918:
    variant 0 -> 0x00460060
    variant 1 -> 0x0042FF80

0x004C4920:
    variant 0 -> 0x004617F0
    variant 1 -> 0x004311C0

0x004C4928:
    variant 0 -> 0x00461B30
    variant 1 -> 0x00430D90

0x004C4930:
    variant 0 -> 0x00433740
    variant 1 -> 0x00431410

0x004C4938:
    variant 0 -> 0x00433780
    variant 1 -> 0x00431460
```

These addresses are direct static-data facts for the analyzed Runtime.

---

# 30. Active callback slots

Selected callbacks are copied into:

```text
0x0090E09C
0x0090E0A8
0x0090E0A4
0x0090E0AC
0x0090E0A0
```

Confirmed:

```text
0x0090E0A8
    active 3DO texture-page upload callback

0x0090E0A4
    active 3DO palette upload callback
```

The other slots are definitely backend-selected callbacks, but their source
names/complete roles remain under investigation.

---

# 31. Do not call the variants “hardware/software” yet

It is tempting to label:

```text
variant 0 = hardware
variant 1 = software
```

or the reverse.

Current evidence is not strong enough for that binary naming.

Both families interact with old graphics surfaces/renderer state, and Runtime
also contains substantial software-side transformation/raster support.

Use:

```text
RendererBackendVariant0
RendererBackendVariant1
```

until the renderer-mode/card-selection call paths establish the user-facing
meaning.

---

# 32. Texture upload callsite

3DO texture load path:

```text
0x004A75E0
```

reads/decompresses/prepares indexed pixel data and then calls through:

```text
[0x0090E0A8]
```

This cleanly separates:

```text
format/resource decode
```

from:

```text
selected renderer backend upload
```

The architecture is one of the strongest clues for how to structure OpenNomad
without coupling 3DT parsing to OpenGL.

---

# 33. Palette upload callsite

Palette loader:

```text
0x004A7900
```

calls active backend callback:

```text
[0x0090E0A4]
```

after loading/preparing palette data.

Again:

```text
3DT/3DO palette semantics
    !=
backend object creation
```

The modern decoder should remain renderer-agnostic.

---

# 34. Backend variant texture paths

The two active texture upload functions are:

```text
0x004617F0
0x004311C0
```

Both manipulate Runtime material/page state but through different resource
paths.

The first uses DirectX-side resource records associated with the material's
runtime texture-page allocation.

The second creates/uses another DirectDraw-style surface/conversion path,
including conversion from indexed source data into a 16-bit representation.

This further supports keeping the variants neutrally named.

---

# 35. Backend variant palette paths

Palette functions:

```text
0x00461B30
0x00430D90
```

operate on the material's:

```text
palette page
palette slot
bit depth
```

and palette RGB data.

The second path clearly contains low-bit-depth subpalette handling and COM
palette/surface interaction.

The complete difference between the two resource representations remains a
high-value future target.

---

# 36. Render-context object

Runtime passes a fairly large mutable render-context structure through the
3D pipeline.

Function:

```text
0x00441030
```

is a central per-context render/submission entrypoint.

A complete type is not yet safe.

Useful confirmed/strong offsets are documented below.

---

# 37. Partial RenderContext layout

Current sparse layout:

```c
struct RuntimeRenderContext {
    void *modelOrSceneRoot;        // +0x000, type partial

    // ...

    void *explicitLights;          // +0x020, 0x130-byte entries

    // ...

    float projectionScaleX;        // +0x0E4, computed by 0x4943D0
    float projectionScaleY;        // +0x0E8

    float cameraTranslationX;      // +0x0EC
    float cameraTranslationY;      // +0x0F0
    float cameraTranslationZ;      // +0x0F4

    // ...

    RuntimeMatrix3 viewMatrix;     // around +0x11C

    // ...

    float nearOrDepthA;            // +0x144
    float depthThresholdB;         // +0x148
    float depthThresholdC;         // +0x14C

    uint32_t fogOrBackgroundRgb;   // +0x150
    float farOrDepthD;             // +0x154
    float field158;                // +0x158
    uint32_t field15C;             // +0x15C

    uint32_t renderType;           // +0x160, 0 wireframe / 1 solid

    // ...

    uint32_t frameCounter168;      // +0x168, cleared on render
    uint32_t frameCounter16C;      // +0x16C, cleared on render

    // ...

    void *linkedContext;           // +0x17C, role partial

    // ...

    uint16_t viewportX;            // +0x198
    uint16_t viewportY;            // +0x19A
    uint16_t viewportWidth;        // +0x19C
    uint16_t viewportHeight;       // +0x19E

    // ...
};
```

Several names remain deliberately broad.

Do not paste this as a final original C structure without uncertainty comments.

---

# 38. Viewport setter — `0x00440C40`

This helper writes four words directly:

```text
context +0x198 = x
context +0x19A = y
context +0x19C = width
context +0x19E = height
```

These fields are then consumed by both:

- CPU projection setup;
- graphics-backend viewport setup.

Thus their viewport meaning is strong.

---

# 39. `SetRenderType` — `0x00440B40`

This field can now be named much more confidently.

The helper accepts only:

```text
0
1
```

and otherwise emits:

```text
SetRenderType, invalid flag
```

It writes the value to:

```text
RenderContext +0x160
```

Therefore:

```c
bool/enum renderType; // +0x160
```

is a direct Runtime concept.

---

# 40. `renderType` maps to Direct3D fill mode

Later, `0x00441030` reads:

```text
context +0x160
```

and calls legacy Direct3D render state:

```text
state = 8
```

with:

```text
renderType 0 -> value 2
renderType 1 -> value 3
```

Legacy Direct3D enum values identify:

```text
render state 8:
    FILLMODE

fill value 2:
    WIREFRAME

fill value 3:
    SOLID
```

Thus the recovered mapping is:

```text
SetRenderType(context, 0)
    -> wireframe

SetRenderType(context, 1)
    -> solid
```

This is much stronger than the previous generic “render mode” interpretation.

---

# 41. Fill-mode cache

Global:

```text
0x008F56C0
```

caches the currently applied fill-mode value.

`0x00441030` avoids redundant Direct3D state calls when the desired value is
already active.

This is part of a wider renderer state cache under:

```text
0x008F56xx..0x008F57xx
```

---

# 42. Context depth-distance setter

Helper:

```text
0x00440BE0
```

always stores its primary float at:

```text
+0x154
```

and, depending on a Boolean-like argument, either:

```text
+0x148 = value * 0.25
+0x14C = value * 0.95
```

or:

```text
+0x148 = value
+0x14C = value
```

These three fields subsequently drive depth/fog/projection-related renderer
globals.

A full original semantic naming is not yet safe.

Current useful description:

```text
far/depth range + derived thresholds
```

---

# 43. Context near/depth field `+0x144`

Setter around:

```text
0x00440BB0
```

clamps the supplied value to at least:

```text
1.0
```

then stores it at:

```text
+0x144
```

Projection setup uses:

```text
1.0 / context[+0x144]
```

and converts the field through a `*16384` path.

This is strongly consistent with a near/depth clipping parameter.

The exact source-level name should remain cautious until all callers are
classified.

---

# 44. Context colour `+0x150`

Backend setup paths extract three bytes from:

```text
context +0x150
```

and construct a packed RGB value.

When fog support/state is active they pass that packed value to Direct3D render
state:

```text
34
```

which is the legacy fog-colour state.

Thus:

```text
+0x150
```

is strongly a renderer colour used as:

```text
fog/background colour
```

depending on path.

Avoid narrowing it to only one role until clear/scene-background callsites are
fully mapped.

---

# 45. Per-context render entry — `0x00441030`

The function begins by clearing per-context/global counters:

```text
0x00907314 = 0
0x009070D8 = 0

context +0x168 = 0
context +0x16C = 0
```

Then the high-level order is:

```text
projection/camera setup
lighting/object setup

clear one light-record flag across explicit lights

clear 16,384-entry SortArrayLo

additional light/object processing

sprite submission/traversal

other scene/object submission

lighting/final submission work

set backend/render auxiliary state

apply wireframe/solid fill mode

invoke selected backend render callback
```

The individual helper names remain partially reconstructed.

---

# 46. `0x004943D0` — projection/camera precompute

Called near the start of:

```text
0x00441030
```

This routine:

- reads the active camera pointer from `0x0090730C`;
- reads camera FOV-like data;
- reads render-context viewport size;
- computes half-width/half-height;
- uses x87 `fptan`;
- builds projection scaling values;
- builds camera view rotation/translation;
- writes CPU projection globals around `0x006A2B90..0x006A2BDC`.

Runtime therefore performs substantial view/projection preparation on the CPU.

---

# 47. Active camera global

Projection setup begins from:

```text
0x0090730C
```

which points at the currently selected camera-like runtime structure.

Recovered camera fields used here include:

```text
eye/target vectors
roll/FOV state
```

The camera-space formulas are documented in:

```text
runtime-coordinate-math.md
```

and should not be duplicated with a different convention here.

---

# 48. Projection FOV path

`0x004943D0` reads camera:

```text
+0x30
```

and applies:

```text
* 0.5
* pi/180
fptan
```

before deriving viewport projection scales.

This independently confirms that the relevant camera field is a degree-valued
full FOV at this runtime stage.

AREA camera conversion into that runtime value is documented separately.

---

# 49. 4:3 projection relationship

Projection setup combines:

```text
viewport width
viewport height
1.333333...
```

with the tangent of half-FOV.

This matches the recovered 4:3 horizontal-FOV projection semantics documented
in `runtime-coordinate-math.md`.

Modern widescreen expansion is an OpenNomad presentation policy, not an
original RenderContext field.

---

# 50. CPU projection globals

`0x004943D0` writes many derived values under:

```text
0x006A2B90..0x006A2BDC
```

including:

- viewport half extents;
- projection scale factors;
- depth thresholds;
- inverse-depth values;
- camera transform components.

These are dense software/legacy-renderer scratch globals.

Prefer a structure such as:

```text
RuntimeProjectionScratch
```

over individually naming every scalar before all consumers are understood.

---

# 51. View matrix construction

The projection/setup function constructs a Runtime row-vector view orientation
from:

```text
target - eye
roll
```

and stores the matrix in the render context around:

```text
+0x11C
```

It then computes camera translation components stored at:

```text
+0xEC
+0xF0
+0xF4
```

This agrees with the independently recovered camera math.

---

# 52. Object transformation is CPU-side

Runtime object/hierarchy rendering does not hand raw model-space vertices and a
hierarchical matrix stack to a modern programmable GPU pipeline.

Functions in the:

```text
0x00494xxx
```

region traverse objects and construct transformed state on the CPU.

This includes:

- local/parent hierarchy;
- animation matrix;
- scale;
- camera transform;
- transformed vertex records;
- clipping/projection state.

OpenNomad's GPU matrix use is therefore an implementation modernization.

---

# 53. Root-driven 3DO traversal

3DO Runtime loading identifies an explicit root object from the serialized
root ID.

Renderer/object traversal then follows the Runtime object hierarchy rather than
simply drawing every serialized object in file order.

The serialized/runtime hierarchy rules belong in `3do.md`.

The renderer consumes the already resolved runtime hierarchy.

---

# 54. Joint-only and invisible geometry

Some 3DO objects participate in hierarchy/animation but should not submit
visible geometry.

OpenNomad currently identifies flags such as:

```text
joint-only
invisible
```

from the existing 3DO model.

Exact individual flag confidence belongs in `3do.md`; renderer architecture
should simply preserve the principle:

```text
transform/hierarchy participation
    !=
polygon submission
```

---

# 55. Triangle and rectangle submission

Runtime has separate loops for serialized:

```text
0x1C-byte triangles
0x20-byte rectangles
```

Rectangles are processed as four-corner polygons through the legacy
clipping/submission path rather than being pre-triangulated in the file.

OpenNomad converts quads to indexed triangles during geometry construction.

That conversion is a modern GPU representation choice.

---

# 56. Runtime transformed-vertex records

Polygon submission works with transformed records whose stride is:

```text
0x30
```

matching the `Vertex2D` runtime buffer.

The old renderer uses transformed coordinates/depth/UV/color state before
inserting faces into sort/bucket structures.

Do not identify these with OpenNomad's compact `Vertex` struct merely because
both contain position/UV/color concepts.

---

# 57. Clipping is CPU-side

Triangle and quad submission visibly performs per-vertex comparisons against
projection/depth limits before face insertion.

Some partially clipped cases route through dedicated clipping helpers.

Thus the original engine does substantial:

```text
near/depth/frustum clipping
```

before the backend consumes the prepared face list.

OpenGL's hardware clipper replaces much of this work in OpenNomad.

---

# 58. UV byte conversion

3DO polygons serialize UV coordinates as bytes.

Runtime indexes the:

```text
256-entry conversion mapping table
```

to turn those byte values into float-like coordinates used by the transformed
face path.

This is separate from sprite frame UV normalization, whose `/256` behavior is
documented in `sprite.md`.

---

# 59. Confirmed U-scroll flag

3DO mesh flag:

```text
bit 24
```

causes polygon submission to add global cyclic U phase:

```text
0x00907304
```

to polygon U coordinates.

Confirmed in submission paths including:

```text
0x004955A9
0x00495A33
0x0049749C
```

This is native Runtime behavior.

---

# 60. Confirmed V-scroll flag

Mesh flag:

```text
bit 25
```

independently adds global cyclic V phase:

```text
0x00907300
```

The two bits are separate.

They are **not** generic “skybox” flags despite older importer/tool naming.

---

# 61. Runtime global UV-scroll phase

Runtime stores two independent cyclic globals:

```text
V phase = 0x00907300
U phase = 0x00907304
```

The recovered update path advances both from Runtime's normalized frame-time
factor:

```text
phase += frame_time_scale * 0.0004
wrap phase to [0, 1)
```

At nominal 30 Hz this is equivalently:

```text
phase += delta_seconds * 0.012
```

Polygon submission adds, rather than subtracts, the selected phase from the
authored coordinates. Bits 24 and 25 independently select U and V.

OpenNomad keeps authored UVs immutable, advances session-lifetime phase state
from the normal presentation update delta, and supplies a per-draw shader
offset to ordinary world and character 3DO material groups. Renderer recreation
on an area/decor change therefore does not reset the global phase. Sprite-frame
UVs remain a separate path and do not inherit these object flags.

`GRID.3DO` is a useful retail validation asset:

```text
circle01 flags=0x03003000 -> additive SPACY, U + V scroll
circle2  flags=0x01003000 -> additive SPACY, U only
```

Both shells use the same static 256x256 `SPACY` texture; their differing cyclic
translations create the evolving overlapping tunnel effect. This is not an
animated-texture/frame mechanism.

---

# 62. Render buckets and packed state

Runtime does not issue one immediate Direct3D call per serialized polygon.

Submission produces face records associated with sorting/state keys.

The 16,384-entry sort/bucket system groups/traverses submitted geometry before
backend rendering.

Sprite rendering exposes several known bucket bits; ordinary 3DO polygon key
semantics remain less completely mapped.

---

# 63. Sprite bucket bits

From the dedicated sprite analysis:

```text
mode 0 -> 0x0000
mode 1 -> 0x0400
mode 2 -> 0x2000
mode 3 -> 0x2400
mode 4 -> 0x2100
mode 5 -> 0x2500
mode 6 -> 0x2200
mode 7 -> 0x2600
mode 8 -> 0x0400
```

Additional known sprite bucket flags:

```text
0x0040 second UV / multitexture path
0x0800 doubled fog range
```

See `sprite.md` for blend-state semantics.

---

# 64. Why sprite evidence should not be generalized blindly

Sprite bucket modes give direct evidence for the renderer state machinery.

They do **not** prove that every ordinary 3DO mesh flag maps to an identically
encoded bucket bit.

Keep:

```text
sprite render mode encoding
```

and:

```text
3DO mesh flag encoding
```

as separate source domains until the polygon submission key builder is fully
decoded.

---

# 65. Legacy Direct3D state cache

Runtime caches applied render states to avoid redundant COM calls.

Examples around:

```text
0x008F56xx..0x008F57xx
```

are compared before calling `SetRenderState`-like vtable functions.

This is analogous to modern renderer state caching, but implemented through
process globals.

---

# 66. Z-buffer enable

Backend setup `0x00460060` applies render state:

```text
7 = enabled value 1
```

when the cached value differs.

Legacy Direct3D identifies state 7 as:

```text
ZENABLE
```

This confirms ordinary depth testing in the Direct3D-facing renderer path.

---

# 67. Z-write enable

Another recovered state path uses:

```text
state 14
value 0/1
```

with cache:

```text
0x008F56D8
```

Legacy Direct3D identifies state 14 as:

```text
ZWRITEENABLE
```

Thus the working label:

```text
cached Z-write enable
```

is strong.

This distinction matters for translucent rendering:

```text
depth test can remain enabled
while depth writes are disabled
```

---

# 68. Default blend enable

`0x00460060` checks cached state around:

```text
0x008F570C
```

and disables Direct3D render state:

```text
27
```

when necessary.

Legacy Direct3D identifies state 27 as:

```text
ALPHABLENDENABLE
```

The backend begins from an opaque/non-alpha-blended state.

---

# 69. Default blend factors

The same setup writes:

```text
state 19 -> value 2
state 20 -> value 1
```

Legacy enum mapping:

```text
19 = SRCBLEND
20 = DESTBLEND

2 = ONE
1 = ZERO
```

Thus the default framebuffer blend equation is effectively:

```text
source * 1
+
destination * 0
```

when alpha blending is off/neutral.

---

# 70. Fog colour

Backend setup maps the context RGB value to render state:

```text
34
```

when fog/state support is active.

Legacy Direct3D identifies:

```text
34 = FOGCOLOR
```

Global:

```text
0x008F5728
```

acts as the cached packed fog-colour value.

---

# 71. Fog-range fields

Runtime keeps multiple context/global depth thresholds:

```text
context +0x148
context +0x14C
context +0x154

0x008F58C0
0x0080B080
0x0080B088
```

Backend code derives reciprocal/difference values from them for fog/depth
processing.

The exact source-level names and equations across every backend remain
incomplete.

Do not substitute OpenNomad's current linear fog approximation into this
Runtime documentation as though it were recovered.

---

# 72. Grayscale rendering

Sprite analysis recovers renderer-wide grayscale luminance:

```text
Y =
    (299R + 587G + 114B) / 1000
```

This appears as part of the same broader rendering state machinery.

OpenNomad reproduces the visible operation in shaders.

A future renderer-wide state object should own this setting rather than
implementing it independently per drawable category.

---

# 73. Ordinary 3DO mesh-flag confidence

OpenNomad currently names mesh bits for:

```text
vertex-lit
alpha testing
alpha blending
additive
subtractive
mirror
environment mapping
water/underwater
FPS arm
face morph
invisible
UV scroll
...
```

Not all names have equal Runtime evidence.

In particular:

```text
UV scroll U/V
    directly Runtime-confirmed

environment_mapped
    still explicitly importer-derived in current source
```

Consult `3do.md` for per-flag confidence.

`renderer.md` should not silently promote importer-derived names to confirmed
original renderer semantics.

---

# 74. Alpha/cutout material rendering

Current OpenNomad maps its recovered/importer-derived material flags to:

```text
opaque
alpha test
source-alpha blend
additive
subtractive
```

This is a useful implementation model.

For the Runtime-authoritative record, exact ordinary-3DO flag-to-state
transitions still deserve a dedicated submission/state audit.

Sprite modes provide stronger direct blend-state evidence today.

---

# 75. Mirrors

The current model viewer implements planar mirror rendering as:

```text
reflection framebuffer
reflected camera
clip plane
one bounce
mirror composite
```

That implementation is **not yet a recovered statement of Runtime's exact
mirror algorithm**.

The 3DO mirror flag/name is useful prior evidence, but the original render path
still requires direct tracing before `renderer.md` declares:

```text
Runtime definitely renders mirrors exactly this way
```

Keep modern mirror implementation and Runtime evidence separate.

---

# 76. Environment mapping

Current OpenNomad model-viewer code includes a procedural cubemap presentation
for the field currently called:

```text
environment_mapped
```

The `Model3DO.hpp` source itself labels this name:

```text
importer-derived
```

Therefore this is **not** authoritative Runtime behavior yet.

Do not use the modern chrome/cubemap shader as evidence for the original
material operation.

---

# 77. Vertex lighting

3DO vertices store BGRA colour.

Retail models and recovered light-count behavior show that some lighting is
baked into vertex data rather than represented by explicit light records.

OpenNomad's `vertex_lit` path multiplies texture colour by vertex colour.

The broad concept is well supported.

The exact source mesh-flag meaning and original colour-space/combine equation
should still be verified directly before calling the current shader
pixel-exact.

---

# 78. Explicit 3DO lights

3DO root contains an explicit-light count whose runtime records are:

```text
0x130 bytes each
```

The render-context path iterates those records and clears bit:

```text
0x08
```

from each first dword before scene submission.

This is direct evidence that explicit light records carry mutable per-frame
runtime flags.

---

# 79. Lighting/object overlap subsystem

Initializer:

```text
0x0048DCE0
```

normal startup capacity:

```text
300
```

for both major dimensions.

Runtime allocates:

```text
light boxes:
    300 × 0x68

object boxes:
    300 × 0x68

light/object pair records:
    300 × 300 × 0x0C

overlap status:
    300 × 300 bytes

object reset flags:
    300 bytes
```

This is a substantial dynamic light/object overlap system rather than simply
iterating every light for every object.

---

# 80. Lighting overlap globals

Known:

```text
0x00660B2C  light-box array
0x00660B38  light-side capacity

0x00660B4C  object-box array
0x00660B50  object-side capacity

0x00660B54  LightPairs
0x00660B30  overlap-status bytes
0x00660B34  object reset flags
```

The `0x68` light/object box and `0x0C` pair record schemas remain unresolved.

---

# 81. Current OpenNomad lighting is not Runtime-exact

`docs/Rendering.md` currently describes:

- importer-derived explicit-light interpretation;
- spot cone assumptions;
- linear attenuation;
- a modern shader light array;
- a fallback directional light;
- an OpenNomad intensity scale.

Those are useful current rendering behaviors.

They should **not** be copied into this Runtime document as established native
lighting equations.

The original overlap/culling and light contribution math remains an active RE
area.

---

# 82. Sprite integration

`0x00441030` calls the sprite traversal/submission region around:

```text
0x004969C0
```

after clearing the sort/bucket array and performing initial light/object setup.

Sprites therefore feed the same broader frame/bucket pipeline rather than
being a completely separate final overlay pass.

See `sprite.md` for the `0x40` instance ABI and render modes.

---

# 83. World sprites are depth-aware 3D effects

Runtime sprites have:

```text
native XYZ
camera-facing projection
depth testing
blend/cutout state
fog-related state
sorting/bucket keys
```

They are not I2D/UI sprites.

Keep:

```text
3D sprite renderer
```

separate from:

```text
I2D bitmap/text interfaces
```

even when both ultimately produce screen-aligned quads.

---

# 84. Renderer backend callback invocation

At the end of `0x00441030`, Runtime:

1. applies the requested wireframe/solid fill state;
2. calls the currently active callback at:
   ```text
   0x0090E09C
   ```
   with the render context.

This active callback is selected from:

```text
0x004C4918
```

and is therefore one of:

```text
0x00460060
0x0042FF80
```

These are two major backend render/flush paths.

---

# 85. Backend path `0x00460060`

This path clearly:

- accesses `g_d3dDevice`;
- applies Z enable;
- disables alpha blending initially;
- applies source/destination blend defaults;
- propagates context colour/depth/viewport state;
- configures graphics viewport/device structures;
- consumes the populated sort/bucket structure.

It is strongly a Direct3D-facing frame rendering path.

---

# 86. Backend path `0x0042FF80`

The second selected path performs a parallel job through a different graphics
resource/presentation representation.

It:

- propagates context depth/colour fields;
- sets fog colour where supported;
- sets a viewport;
- reads the same sort/bucket array;
- traverses bucket entries through its own drawing implementation.

This confirms a common high-level submission layer feeding two distinct backend
families.

---

# 87. Shared submission, backend-specific flush

The architecture can therefore be expressed as:

```text
model/object/sprite submission
        |
        v
common transformed face/bucket representation
        |
        +---------------------------+
        |                           |
        v                           v
backend variant 0             backend variant 1
0x00460060                    0x0042FF80
        |                           |
        v                           v
legacy graphics resources / presentation
```

This is a useful modern design lesson: keep decoded scene semantics independent
from one graphics API implementation.

---

# 88. Renderer cleanup — `0x00440530`

Core renderer shutdown walks/frees:

- loaded 3DO resources;
- sprite pool;
- lighting overlap allocations;
- sort array;
- indexed texture-page raw allocation;
- RGB palette memory;
- converted palette state;
- texture-page descriptors;
- material-page descriptors;
- Vertex2D/Face3D buffers;
- conversion table.

This gives clear ownership grouping for the original renderer subsystem.

OpenNomad should retain equivalent lifetime grouping through RAII rather than
copying one manual shutdown function.

---

# 89. Current OpenNomad rendering layers

Current modern code roughly separates:

```text
Renderer
    frame/default GL state

WorldRenderer
    decor + character meshes
    material groups
    sprite renderer integration

SpriteRenderer
    Runtime-style 3D billboard effects

I2DRenderer
    interface/UI presentation

VideoScene
    FMV presentation
```

This is a cleaner ownership architecture than Runtime's large process-global
renderer while still allowing subsystem-specific fidelity.

---

# 90. OpenNomad GL frame defaults

Current `Renderer::init()` enables:

```text
depth test
stencil test
sRGB framebuffer conversion
back-face culling
multisampling
```

These are modern global OpenGL defaults.

Do not infer that Runtime had:

```text
stencil enabled
MSAA enabled
sRGB framebuffer
```

because OpenNomad does.

Each original state requires separate Direct3D/DirectDraw evidence.

---

# 91. OpenNomad texture representation

Original Runtime:

```text
indexed texture pages
+
separate RGB palettes
+
display-format conversion
+
backend surfaces
```

Current OpenNomad:

```text
Texture3DT decoder
    ->
RGBA8 image per material
    ->
one modern GPU Texture2D
```

This is one of the largest deliberate renderer architecture differences.

It is valid provided palette/key/gamma/color semantics are faithfully
reproduced before/at sampling.

---

# 92. Why page indices remain important

Even though OpenNomad does not allocate old 256×256 pages, serialized/runtime
fields such as:

```text
texture page index
texture slot
palette page
palette slot
atlas offsets
```

must remain documented.

They explain:

- original resource sharing;
- palette reuse;
- sprite/material behavior;
- backend upload callbacks;
- old memory limits;
- subtle texture-coordinate placement.

Do not delete them from parsers merely because OpenGL doesn't need them.

---

# 93. OpenNomad geometry representation

Original Runtime:

```text
serialized vertices/faces
    ->
runtime object transforms
    ->
Vertex2D / Face3D
    ->
buckets
```

OpenNomad:

```text
decoded Model3DOData
    ->
MaterialGroup
    ->
GPU VBO/EBO/VAO
```

This is a sensible modernization.

The conversion should preserve:

```text
hierarchy semantics
material identity
UVs
vertex colours
render-state categories
```

rather than preserve old intermediate structure sizes.

---

# 94. Runtime-to-OpenGL coordinate boundary

Runtime-native simulation/render data uses:

```text
+X right
+Y down
+Z forward
unit = inch
row-vector matrices
```

OpenNomad applies one renderer-boundary basis:

```text
(x, y, z)
    ->
(x, -y, -z)
```

No scale is applied for rendering.

See `runtime-coordinate-math.md`.

---

# 95. Do not pre-convert simulation state to GL space

The following should remain Runtime-native until presentation:

```text
AREA positions
3DO hierarchy state
3DA animation
3DP paths
character transforms
sprite anchors
scripted camera data
collision/gameplay positions
```

Only renderer-facing geometry/matrices should adopt the GL basis.

This avoids incompatible coordinate assumptions between engine subsystems.

---

# 96. Current OpenNomad opaque/translucent split

`WorldRenderer` currently performs:

```text
opaque + cutout:
    blending off
    depth writes on

blended:
    blending on
    depth writes off
```

Decor translucent groups are sorted far-to-near by group center.

Character groups and sprites are then drawn through corresponding pass logic.

This is a modern renderer organization.

Exact Runtime ordinary-mesh sorting requires further bucket-key analysis.

---

# 97. Current OpenNomad blend mapping

Current world implementation uses:

```text
standard alpha:
    SRC_ALPHA
    ONE_MINUS_SRC_ALPHA

additive:
    ONE
    ONE

subtractive:
    ONE
    ONE
    reverse subtract
```

These choices currently follow the project's mesh-flag interpretation.

Sprite blend behavior has stronger direct Runtime evidence and is documented
separately.

Do not automatically promote all current world blend mappings to confirmed
Runtime behavior.

---

# 98. Character rendering

OpenNomad character runtime produces posed model geometry in Runtime-native
space.

`WorldRenderer` currently rebuilds/reuploads character `MaterialGroup` meshes
when:

```text
pose_revision
```

changes.

This is not how Runtime's CPU transform/bucket renderer represented animated
characters.

It is a modern implementation expedient and may later be optimized using GPU
skinning/dynamic buffers.

---

# 99. GPU skinning is compatible with fidelity

A future OpenNomad renderer may move:

```text
joint transforms
vertex skinning
```

onto the GPU.

That is compatible with reverse-engineered semantics if:

- Runtime hierarchy order is preserved;
- 3DA sampling/root-motion behavior is preserved;
- resulting posed positions/normals match;
- rendering state remains compatible.

Fidelity is about observable transform semantics, not reproducing CPU work for
its own sake.

---

# 100. GPU palette/indexed-texture emulation is optional

Likewise, OpenNomad could choose either:

```text
decode indexed texture + palette to RGBA at load time
```

or:

```text
upload indexed texture + palette and resolve in shader
```

The latter may be useful if Runtime relies on palette mutation/effects.

The correct choice depends on future evidence.

Do not prematurely lock the renderer architecture to one permanent
RGBA-per-material assumption.

---

# 101. Gamma and colour conversion

Runtime explicitly converts palette RGB into the selected native 15/16-bit
display format and has additional gamma/conversion state in the graphics
pipeline.

Modern OpenNomad uses:

```text
sRGB textures/framebuffer
```

as part of its current output pipeline.

Exact visual parity requires understanding where the original performs:

```text
palette conversion
gamma adjustment
FMV colour handling
display-format quantization
```

rather than merely enabling `GL_FRAMEBUFFER_SRGB`.

---

# 102. 15/16-bit quantization

The original PC renderer was designed to support native 15/16-bit display
formats.

That means visible retail output could include:

- channel-mask quantization;
- palette conversion;
- display-device-specific precision differences.

OpenNomad's 32-bit modern framebuffer naturally has greater precision.

Whether strict-reference mode should emulate final quantization is a separate
visual-fidelity decision.

---

# 103. Alpha source in indexed textures

Current OpenNomad's 3DT path can derive transparency from palette/key behavior.

Original Runtime may use:

- color key;
- alpha-test state;
- palette/surface keying;
- blend state

depending on material/sprite/interface path.

Do not assume:

```text
black RGB always means alpha zero
```

globally across every renderer category without path-specific evidence.

---

# 104. I2D is not the 3D renderer

The main-menu/interface architecture is separately documented in:

```text
interface.md
```

I2D can share DirectDraw/screen resources with the 3D platform layer, but its
state graph, bitmap/text elements and bump background are not ordinary 3D
`RenderContext` geometry.

Keep these subsystems separate in OpenNomad.

---

# 105. FMV is not the 3D renderer

Startup/fullscreen video presentation similarly has its own decode/presentation
pipeline.

It may share final display surfaces/gamma behavior, but MPEG frame decoding is
not part of the 3DO render bucket architecture.

A dedicated `fmv.md` should own that work.

---

# 106. Render-context chaining

Functions around:

```text
0x00441170
```

operate on render contexts linked through a field around:

```text
+0x17C
```

including insertion/relinking behavior and diagnostics when a source scene is
already linked.

This suggests Runtime can build chains/stacks of render scenes/contexts.

Exact semantics should be recovered before mapping this directly to:

```text
scene graph
render pass chain
portal list
```

or another narrow modern concept.

---

# 107. Source diagnostic for scene linking

Runtime contains:

```text
o3de_InsertScene Error : Source Scene is already linked !
```

near the context-linking code.

This is valuable original terminology:

```text
Scene
```

for at least one 3DE/render-context layer.

A working Ghidra namespace:

```text
3DE::Scene
```

may be appropriate around these functions.

---

# 108. Render context versus game Scene

Do not conflate:

```text
original 3DE Scene/render context
```

with OpenNomad's high-level:

```cpp
Scene
```

application-state abstraction.

They happen to share the English word “scene” but live at different
architectural levels.

---

# 109. `SetRenderType` source provenance

The diagnostic:

```text
SetRenderType, invalid flag
```

is followed by original source-path evidence:

```text
C:\Omikron\sources\3de\..\libdirect3d\include\acc3d.h
```

This strongly places:

```text
0x00440B40
```

at the boundary between the 3DE scene abstraction and accelerated Direct3D
renderer API.

---

# 110. Render state 8 mapping confidence

The numeric mapping:

```text
8 -> FILLMODE
2 -> WIREFRAME
3 -> SOLID
```

is stable across the legacy Direct3D enum family.

Combined with:

```text
"SetRenderType, invalid flag"
```

and the exact 0/1 switch, this makes the `+0x160` interpretation
Runtime-confirmed to a high degree.

Recommended Ghidra labels:

```text
RenderScene_SetRenderType
RuntimeRenderContext.renderType
```

---

# 111. Other context setters

Nearby helpers directly set/get fields such as:

```text
+0x158
+0x15C
+0x144
+0x154
+0x198..+0x19E
```

Their existence implies a source-level API around the render-context/scene
structure rather than random global writes.

Future RE should use:

```text
setter validation
diagnostic strings
callsite argument meaning
backend consumers
```

to recover original field names systematically.

---

# 112. State-cache strategy

Original renderer repeatedly follows:

```text
if cachedState != desired:
    device->SetRenderState(...)
    if success:
        cachedState = desired
```

This is visible for:

- fill mode;
- Z enable;
- Z write;
- alpha-blend enable;
- fog colour;
- other states still being mapped.

OpenNomad can use explicit state objects/pipeline keys instead of global cache
variables while preserving transition semantics.

---

# 113. Why state caching matters for RE

A renderer-state write may be hidden behind:

```text
"only when changed"
```

logic.

Therefore observing that one material path does not call `SetRenderState`
during one trace does **not** mean the state is irrelevant; it may already be
cached.

Static callgraph analysis should include both:

```text
desired-state calculation
```

and:

```text
cached-state comparison
```

before naming material behavior.

---

# 114. Renderer mode `2`

Several backend functions test:

```text
g_rendererMode == 2
```

and branch into specialized behavior.

The exact high-level mode name remains unresolved.

Do not currently label:

```text
2 = software
2 = hardware
2 = RGB
```

without decisive device-selection/setup evidence.

Keep numeric mode annotations until the full `InitCartes.c`/configuration path
is mapped.

---

# 115. Current `docs/Rendering.md`

The existing implementation document remains useful for:

- OpenGL class ownership;
- present draw-pass organization;
- debug/model viewer architecture;
- current mirror/environment effects;
- current explicit-light shader;
- current modernization roadmap.

But it contains several implementation choices that are **not** Runtime proof.

Long-term documentation split should be:

```text
docs/Rendering.md
    current OpenNomad renderer implementation

docs/reverse-engineering/renderer.md
    original Runtime renderer behavior/evidence
```

---

# 116. Recommended renderer data layers

A robust modern design should preserve four layers:

```text
1. Serialized data
   3DO / 3DT bytes and fields

2. Runtime semantic data
   hierarchy, materials, UVs, render flags, lights

3. Presentation commands
   material/texture/state + geometry + sort category

4. Graphics API
   OpenGL objects/shaders/state
```

The original game partially fused layers 2–4 through global page and backend
state.

OpenNomad should not.

---

# 117. Recommended texture architecture

Keep:

```text
Texture3DT decoder
    renderer-independent
```

Then allow renderer/resource policy to choose:

```text
RGBA texture
```

or future:

```text
indexed texture + palette texture
```

without changing the file parser.

This mirrors the useful separation already visible in Runtime's upload
callbacks.

---

# 118. Recommended material-state type

Avoid baking current importer-derived assumptions directly into one permanent
`BlendMode`.

A future Runtime-oriented material state could carry:

```text
raw mesh flags
known confirmed state bits
provisional interpreted bits
texture/palette identity
UV phase controls
depth category
blend category when confirmed
fog category when confirmed
```

Then backend shader/pipeline selection can evolve as RE improves.

---

# 119. Recommended RenderContext abstraction

Do not reproduce the original `0x1A0+` mutable structure byte-for-byte as
OpenNomad runtime architecture.

Instead model semantic inputs:

```text
camera
viewport
near/far/depth thresholds
fog/background colour
render type
scene objects
lights
sprites
```

while preserving a separate documentation/Ghidra type for the old offsets.

---

# 120. Recommended render command representation

A modern equivalent to Runtime's face/bucket pipeline can be:

```cpp
struct RuntimeRenderCommand {
    GeometryHandle geometry;
    MaterialHandle material;

    uint32_t rawFlags;
    RenderState state;

    float sortDepth;
    uint64_t stableOrder;
};
```

Then:

```text
opaque/cutout
translucent
special passes
```

can be scheduled without copying a 16,384-head global array.

---

# 121. Preserve original sorting semantics where observable

Modern batching can reorder equivalent state for efficiency.

However:

- transparency;
- additive/darken effects;
- overlapping sprites;
- mirror/special surfaces

may expose order differences.

Confirmed sprite state bits are traversed in ascending bucket order. OpenNomad
therefore makes `bucket_bits(render_mode)` the primary sprite command key and
uses stable ties. The remaining unknown material-key bits still require care;
ordinary mesh ordering is not implied by this sprite result.

---

# 122. Recommended strict reference mode

Potential future debug/reference options:

```text
640×480 / 4:3 projection reference

confirmed bilinear retail texture filtering

optional 15/16-bit final quantization

stepped 30 Hz authored effects

Runtime-compatible UV scroll phases

wireframe/solid SetRenderType emulation

disable modern MSAA when comparing pixels
```

These should be diagnostics/fidelity options, not requirements for normal
modern presentation.

---

# 123. Do not reproduce old memory limits blindly

Original limits such as:

```text
texture page count
Vertex2D capacity
Face3D capacity
300 light/object boxes
2048 sprites
```

can matter for compatibility and malformed-data behavior.

But modern OpenNomad need not use fixed raw arrays unless the observable limit
is gameplay-significant.

Preferred pattern:

```text
modern dynamic ownership
+
optional/explicit compatibility limit
+
clear diagnostic on exhaustion
```

---

# 124. Renderer initialization tests

Recommended tests:

- [ ] SortArrayLo logical capacity is `0x4000`;
- [ ] Vertex2D runtime stride documented as `0x30`;
- [ ] Face3D runtime stride documented as `0x7C`;
- [ ] indexed texture page is exactly 256×256 bytes;
- [ ] usable indexed-page base is 64-KiB aligned;
- [ ] palette RGB page is `0x300` bytes;
- [ ] texture-page descriptor stride `0x24`;
- [ ] material-page descriptor stride `0x20`;
- [ ] backend index chooses all callbacks from one consistent variant.

---

# 125. RenderContext tests

- [ ] viewport fields map to `+0x198..+0x19E`;
- [ ] `SetRenderType(0)` accepted;
- [ ] `SetRenderType(1)` accepted;
- [ ] other render type rejected/reported;
- [ ] type 0 maps to wireframe fill;
- [ ] type 1 maps to solid fill;
- [ ] per-frame counters clear at render entry;
- [ ] sort array clears before submission;
- [ ] backend callback invoked after common submission.

---

# 126. Projection tests

- [ ] uses camera FOV / 2;
- [ ] degrees-to-radians before tangent;
- [ ] viewport width/height influence scales;
- [ ] 4:3 relationship matches coordinate doc;
- [ ] camera translation agrees with row-vector view math;
- [ ] Runtime->GL adapter applied only once in modern renderer.

---

# 127. Texture/palette tests

- [ ] material page/slot fields preserved;
- [ ] indexed data remains `/256`-style source coordinate compatible;
- [ ] texture upload occurs after decode/preparation;
- [ ] palette upload occurs separately;
- [ ] low-bit-depth palettes preserve actual entry counts;
- [ ] RGB->native16 conversion uses selected channel masks in reference tests;
- [ ] modern RGBA expansion does not change palette/key semantics.

---

# 128. Render-state tests

- [ ] Z test can be enabled independently of Z write;
- [ ] fill wireframe/solid mapping exact;
- [ ] alpha-blend enable cache behavior modeled semantically;
- [ ] default blend factors equivalent to ONE/ZERO;
- [ ] fog colour generated from context colour correctly;
- [ ] sprite blend states remain consistent with `sprite.md`;
- [ ] unconfirmed ordinary mesh flags remain visibly marked provisional.

---

# 129. UV-scroll tests

- [x] bit 24 changes U only;
- [x] bit 25 changes V only;
- [x] both can operate together;
- [x] original texture coordinates remain immutable;
- [x] global cyclic phase is applied at presentation/submission;
- [x] phase wrap/timing matches the recovered `0.0004` per nominal 30 Hz tick.

---

# 130. Lighting tests

Once more of the original path is recovered:

- [ ] explicit light count drives `0x130` records;
- [ ] light flag `0x08` per-frame reset reproduced where needed;
- [ ] overlap system capacities/box/pair logic tested;
- [ ] light/object pair rejection matches Runtime;
- [ ] contribution/attenuation equations tested from executable evidence;
- [ ] baked vertex lighting remains distinct from explicit lights.

---

# 131. High-value next RE: backend identities

Determine exactly what:

```text
backend index 0
backend index 1
renderer mode 0/1/2/...
```

mean to the original configuration/UI/device setup.

Targets:

```text
0x0042F9A0
0x0042FA00
0x0045EF60
InitCartes.c-related diagnostics
CONFIG utility behavior
OMK_SAVE renderer fields
```

This should settle whether the variants correspond to:

- 3D hardware modes;
- texture-surface modes;
- pixel formats;
- software fallback;
- or another distinction.

---

# 132. High-value next RE: ordinary mesh state keys

Decode the polygon bucket-key builder completely.

Questions:

```text
which bits encode texture page?
which bits encode material/palette?
which bits encode depth bucket?
which bits encode alpha/cutout/additive?
which bits select second UV/fog variants?
```

This would turn several current `MeshFlags` interpretations into
Runtime-confirmed renderer behavior.

---

# 133. High-value next RE: fog

Trace:

```text
context +0x148
+0x14C
+0x154

0x008F58C0
0x0080B080
0x0080B088
```

through both backend flush paths.

Recover:

- fog start/end;
- whether depth or range fog is used;
- exact interpolation;
- doubled-range behavior;
- interaction with sprite/mesh bucket bits.

---

# 134. High-value next RE: lighting

Reconstruct:

```text
0x0048D0D0
0x0048D3B0
0x0048D7F0
0x0048DCE0
```

and the `0x68`/`0x0C` overlap records.

This is more valuable than tuning OpenNomad's current approximate spot lights
by eye.

---

# 135. High-value next RE: mirrors/environment/water

Trace actual consumers of mesh bits:

```text
bit 20
bit 26
bit 27
bit 29
bit 30
```

before further specializing OpenNomad shaders.

These are currently among the areas where old importer terminology and modern
visual experimentation can most easily be mistaken for original semantics.

---

# 136. High-value next RE: final raster/resource paths

The two backend flush paths should eventually be documented instruction by
instruction enough to answer:

```text
what exactly remains CPU rasterized?
what is passed to Direct3D as transformed/lit vertices?
what surface formats are used?
how are color keys applied?
how are palettes attached?
how are clipping and fog split CPU/GPU?
```

That will establish the exact boundary between Quantic's 3DE engine and
Direct3D 6.1.

---

# 137. Useful Runtime addresses

Core renderer:

```text
004403E0  initialize CPU-side 3D renderer/resources
00440530  renderer/resource shutdown
004406B0  indexed texture/palette page allocation

00440A20  build native pixel-channel conversion tables
00440AF0  RGB -> native 15/16-bit conversion

00440B40  SetRenderType
00440BE0  set depth/far range fields
00440C40  set viewport

00441030  common RenderContext submission/render entry
004943D0  camera/projection precompute
```

---

# 138. Resource/backend addresses

```text
0042F9A0  reset/select default backend variant
0042FA00  install callback variant
0042FA60  backend-index getter
0042FA70  toggle/change backend variant

00460060  backend render/flush variant 0
0042FF80  backend render/flush variant 1

004617F0  texture upload variant 0
004311C0  texture upload variant 1

00461B30  palette upload variant 0
00430D90  palette upload variant 1

004A75E0  3DO/3DT texture load -> active upload callback
004A7900  palette load -> active palette callback
```

---

# 139. Submission/scene addresses

Known useful regions:

```text
0048D0D0  light/object setup
0048D3B0  light/object processing
0048D7F0  later lighting/final submission

004969C0  sprite traversal/submission region
00496FC0  neighboring scene/sprite/object submission region

00494xxx  camera/object transform and projection
00495xxx  triangle/quad clipping/submission
00495E40  clipping/submission helper
```

Names should remain working labels until exact function boundaries/roles are
completed.

---

# 140. Important renderer globals

```text
0052B8D8  renderer backend variant index

0080B068  Direct3D device-like COM pointer
008F56C0  cached fill mode
008F56D8  cached Z-write state
008F56BC  cached Z-enable state
008F570C  cached alpha-blend-enable state
008F5728  cached fog colour

009070B0  aligned indexed texture pages
009070B8  SortArrayLo
009070BC  RGB palette pages
009070C8  raw indexed-page allocation
009070CC  conversion mapping table
009070DC  texture-page descriptors
009070E0  Vertex2D render buffer

00907300  cyclic V phase
00907304  cyclic U phase
00907318  total texture/palette page count
0090731C  texture page count
00907320  Face3D render buffer
00907324  material/palette page descriptors

00657D98  converted palette base
0065FE70  converted palette raw allocation

0090E09C  active backend frame/render callback
0090E0A8  active texture upload callback
0090E0A4  active palette upload callback
```

---

# 141. Original versus OpenNomad quick comparison

| Area | Runtime | OpenNomad |
|---|---|---|
| API | DirectDraw / legacy Direct3D | OpenGL 4.1 |
| textures | shared 8-bit indexed pages | independent RGBA textures |
| palettes | separate RGB + converted native palettes | expanded during decode |
| display precision | native 15/16-bit supported | modern RGBA framebuffer |
| transforms | extensive CPU transform | CPU semantic transforms + GPU matrices |
| clipping | substantial CPU clipping | primarily GPU clipper |
| faces | `Vertex2D`/`Face3D` working buffers | VBO/EBO meshes |
| sorting | 16,384-entry bucket structure | explicit modern draw passes/sorts |
| state | cached global D3D states | GL state/pipeline code |
| sprites | shared 3D bucket pipeline | `SpriteRenderer` command queue |
| lighting | overlap subsystem + baked/explicit lights | current shader approximation |
| UI | separate I2D subsystem | separate I2D renderer |
| memory | global fixed buffers/pages | RAII/dynamic ownership |

---

# 142. Compact RenderContext reference

Known fields:

```text
+020  explicit-light records pointer

+0E4  projection scale
+0E8  projection scale

+0EC  camera translation X
+0F0  camera translation Y
+0F4  camera translation Z

+11C  view/orientation matrix region

+144  near/depth parameter
+148  depth threshold
+14C  depth threshold
+150  RGB fog/background colour
+154  far/depth range
+158  unresolved float
+15C  unresolved dword

+160  SetRenderType:
      0 wireframe
      1 solid

+168/+16C per-render counters

+17C  linked scene/context pointer

+198 viewport X
+19A viewport Y
+19C viewport width
+19E viewport height
```

---

# 143. Compact resource-memory reference

```text
SortArrayLo:
    16384 × 4 bytes

Vertex2D:
    stride 0x30

Face3D:
    stride 0x7C

indexed texture page:
    256 × 256 × 1
    0x10000 bytes
    0x10000 aligned

RGB palette page:
    256 × 3
    0x300 bytes

texture-page descriptor:
    0x24

material/palette-page descriptor:
    0x20
```

---

# 144. Compact D3D-state reference

Confirmed/strong numeric mappings observed through the device:

```text
state 7
    ZENABLE

state 8
    FILLMODE
    value 2 = wireframe
    value 3 = solid

state 14
    ZWRITEENABLE

state 19
    SRCBLEND
    value 2 = ONE

state 20
    DESTBLEND
    value 1 = ZERO

state 27
    ALPHABLENDENABLE

state 34
    FOGCOLOR
```

The numeric mappings are legacy Direct3D enum semantics; individual Runtime
callsite purpose is established where described above.

---

# 145. Boundary of current knowledge

Strongly recovered:

```text
DirectX 6.1-era renderer architecture
Direct3D device-like object
core renderer allocations
Vertex2D/Face3D strides
16,384-entry sort array
indexed 256×256 texture pages
separate RGB palettes
native 15/16-bit palette conversion
two backend callback families
texture/palette callback separation
RenderContext viewport
RenderContext wireframe/solid type
camera/projection precompute
CPU transform/clipping/submission architecture
U/V scroll phases
major legacy D3D depth/blend/fog state IDs
sprite integration
light/object overlap allocation architecture
```

Still incomplete:

```text
exact names/meaning of renderer modes and backend variants
complete RenderContext layout
complete sort/bucket key
ordinary 3DO mesh flag -> render state mapping
exact fog formula and thresholds
dynamic light contribution equations
mirror path
environment-map path
water/underwater path
second-UV/multitexture behavior
exact final surface/pixel formats per backend
precise hardware/software division of work
```

The central architectural takeaway is:

> Omikron's PC renderer is a hybrid late-1990s engine: Quantic's 3DE layer
> performs extensive CPU-side hierarchy transformation, projection, clipping,
> lighting preparation and face sorting into fixed working buffers/buckets,
> while selectable DirectX backend callbacks manage indexed texture/palette
> resources and flush the prepared scene through legacy DirectDraw/Direct3D.
> OpenNomad should reproduce the resulting semantics while keeping file data,
> runtime state, draw scheduling and the modern graphics API cleanly separated.

---

# 146. Color-domain and filtering facts

**Confirmed — Runtime:**

- the Direct3D 6-era renderer has no sRGB texture-decode or framebuffer-encode
  state, so texture filtering, modulation, lighting and blending manipulate
  stored/display-encoded RGB numbers directly;
- `D3DTSS_MAGFILTER` and `D3DTSS_MINFILTER` are both set to `LINEAR` at
  `0x00463BFF` and `0x00463C36`;
- sprite mode bucket bits are traversed in ascending numeric order through the
  16,384-entry sort array, so additive `0x2100` precedes darken `0x2200`
  regardless of texture identity;
- Runtime enables legacy target dithering;
- `SpriteInstance+0x24` supplies the diffuse alpha byte packed into generated
  faces (see `sprite.md`).

**Modern reconstruction:** canonical world color is scene-linear HDR in one of
two `GL_RGBA16F` ping-pong targets. Opaque/cutout sampling and lighting happen
in linear light. Runtime-compatible blended sources still filter and modulate
encoded RGB, but enter only a transient normalized `GL_RGBA16` operator
accumulator. A fullscreen pass combines it with the current linear scene and
writes the alternate scene target. All three framebuffers share one
depth/stencil attachment, so no depth copy or scene copy is involved.

The accumulator contains semantically encoded operator state, never a complete
game frame. HDR destinations split into an SDR base and positive excess for the
compatibility equation. The display pass clamps to SDR and applies the exact
standard sRGB OETF once. These target formats and transfer functions are
OpenNomad choices, not recovered Runtime behavior.
