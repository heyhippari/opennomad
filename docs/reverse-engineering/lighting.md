# Runtime vertex lighting and day-cycle light

> **Status:** recovered from the PE32/i386 retail `Runtime.exe` (image base `0x00400000`)
> **Last updated:** 2026-10-29

Addresses are evidence only and are not encoded in gameplay code. Runtime lights on the CPU, per vertex, and writes
a final packed colour into each transformed vertex (`+0x2C`, `0x00RRGGBB`). That colour becomes the Direct3D diffuse
colour, which modulates the texture.

## 1. Render-context colour bounds

| Field | Meaning |
|---|---|
| context `+0x1A0` | ambient: the floor of the baked path and the base of the lit path |
| context `+0x1A4` | ceiling of the baked path |

**Context creation** (`0x0044E00D`): `+0x1A0 = grey(_ftol(scene3DO.root+0xB8 * 255))`, `+0x1A4 = 0xFFFFFF`.
`root+0xB8` is the decor's base-light scalar, e.g. ANEKBAH `0.1` → `0x191919`, SHALL01B `0.3` → `0x4C4C4C`.

**Day cycle** (`0x0041E90C`), only for the scene whose AREA environment word marks it as day-cycle owner (see
[fog.md](fog.md)). With `span = 0x80` (`0x004C2C28`), `t = clock % day` and
`ramp = (t % phaseLength) * (span/2) / phaseLength` (integer):

| Phase | Level |
|---|---|
| 0 | `span/2 + ramp` (64 → 128) |
| 1 | `span − ramp` (128 → 64) |
| 2 | `span/2 − ramp` (64 → 0) |
| 3 | `ramp` (0 → 64) |

```text
context +0x1A0 = grey(level)
context +0x1A4 = grey(level + (0xFF - span))      level + 127
```

A new game (clock `2 000 000`) starts at level 50: floor `0x323232`, ceiling `0xB1B1B1`.

## 2. Two vertex-colour paths

Scene submission (`0x0048D7F0`, actor list at context `+0x184`; and `0x0048D3B0` / `0x00440CA0` for scene objects)
picks one path per object:

- **Baked** (`0x004947F0`): instances without a light slot and scene objects whose root `+0xB4 == -1`, which
  covers all static decor and the sky.
- **Lit** (`0x00494E80` + `0x00493E40` per light): instances registered by `LightInstance` (`0x004380B0`, called for
  every character instance that is added to a scene) and objects registered by `LightObject` (`0x00436F50`, for
  example script toggles through `0x0041CEE0`).

### 2.1 Baked path

```text
c = vertex colour dword (high byte 0 in retail data)
if c < +0x1A0: c = +0x1A0          unsigned compare of the packed value
if c > +0x1A4: c = +0x1A4
```

The comparison is on the whole packed integer, so it is lexicographic in R, G, B rather than per channel. At night
(ceiling `0x7F7F7F`) a vertex `0x80FF00` becomes `0x7F7F7F`, while `0x7EFFFF` is kept.

Two variants replace this path: environment-mapped (bit 26) and underwater (bit 27) objects, which use the caustic
tables (`0x004DDBB0`, `0x006A2C60..`). They are not covered here.

### 2.2 Lit path

`0x00494E80` sets every vertex of the node to `+0x1A0` and ignores the serialized colour (retail character models
store all-white colours). Then, for each light paired with the actor and belonging to the same scene context,
`0x00493E40(context, node, light)`:

```text
skip nodes with flags & 0x03000000 (UV scroll)
d² = |nodePosition − lightPosition|²
if d² > outer²: no contribution
s = intensity * 256
if d² > inner²: s *= 1 − (√d² − inner) / (outer − inner)
L = light direction in node space
for each vertex with normal n:
    t = −(n · L) * s
    if t > 0: k = _ftol(t); each byte = min(255, byte + (k * lightByte >> 8))
```

Intensities `< 1.0` use the lookup tables built by `0x00494240`: `0x00660BA8[b*256 + k] = (k*b) >> 8` and
`0x006A2CE0[x]`, a clamp of `x` to `0..255` (the bytes before it are 0 and after it 255). The result is the same
saturating sum.

Pairs come from a sweep-and-prune broadphase (`0x0048DCE0`, capacity 300 lights × 300 objects). A light's box is the
AABB of its six shape points (`0x0048DEA0`). An actor's box is its position ± the radius of its actor object
(`0x0048E590`). A pair is active when the boxes overlap on all three axes.

### 2.3 Light record preparation

`0x00493C30` runs once at scene load:

```text
+0x70 = normalize(+0x50 − +0x30)       direction (position → target)
+0xF0 = (+0x18)²                        outer radius²
+0xF4 = (+0x1C)²                        inner radius²
```

These offsets are serialized as a shape point and zeros; Runtime overwrites them. `0x00493CE0` transforms the
position and direction into camera space once per frame and sets record flag bit 3.

## 3. Far alpha fade

The backend writes `min(vertexAlpha, clamp((end − z') / (end − 0.95·end), 0, 1) · 255)` into the diffuse alpha of
every vertex (see [fog.md](fog.md)). Opaque buckets do not blend, so only alpha-blended buckets visibly fade near the
view distance. Additive and darken buckets use `ONE/ONE` and `ZERO/INV_SRCCOLOR` blending, which ignore alpha.

## 4. OpenNomad mapping

| Runtime | OpenNomad |
|---|---|
| `+0x1A0/+0x1A4`, day ramp | `Lighting::base_ambient`, `day_ambient`, `scene_ambient` |
| baked clamp | world vertex shader `u_lighting_mode = 1`, reference `Lighting::clamp_baked` |
| lit instance | world vertex shader `u_lighting_mode = 2`, lights chosen per character node by `Lighting::select_node_lights` and uploaded by `WorldRenderer::apply_actor_lighting`, reference `Lighting::accumulate` |
| (OpenNomad enhancement) | `PER_PIXEL_LIGHTING` variant: the same terms evaluated in the fragment stage with the interpolated normal |
| (OpenNomad enhancement) | `CUBIC_VERTEX_COLOR` variant: clamped baked decor colours interpolated by a monotone cubic Bezier triangle (`CubicColor::build_corner_slopes`) |
| light preparation and broadphase | `Lighting::prepare`, `touches`, `node_scale` |
| far alpha fade | `runtime_far_fade()` in the blended world and sprite shaders |

Decor, placed objects and the sky use the baked path; characters use the lit path. Before this change OpenNomad
drew the serialized colours unclamped, so characters, which ship white colours, were drawn at full brightness and
the day cycle had no effect on light.

Approximations and open items:

- A lit node's corners that reference its parent's vertex block take this node's lights. Runtime lights them with
  the parent node's lights. Only the per-node distance attenuation differs.
- `LightObject` registration of placed objects (script-driven) is not modelled; objects stay on the baked path.
- At most 16 lights per character node are evaluated. The saturating sum does not depend on order.
- The per-pixel enhancement matches the vertex path exactly where the interpolated normal equals a vertex normal.
  Between vertices it differs by design. It does not add specular light, since the 3DO carries no gloss data.
- Environment-map, underwater and caustic variants, and the alpha-test interaction of the far fade, are not
  implemented.
