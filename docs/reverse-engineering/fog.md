# Runtime fog, view distance and time of day

> **Status:** recovered from the PE32/i386 retail `Runtime.exe` (image base `0x00400000`)
> **Last updated:** 2026-10-29

Addresses are evidence only and are not encoded in gameplay code.

## 1. AREA data

The AREA header tail that [iam-area.md](iam-area.md) previously left unresolved holds the fog environment:

| Offset | Type | Meaning |
|---|---|---|
| `+0x90` | `u32[4]` | fog colour per day phase, packed `0x00RRGGBB` (the high byte is ignored and is sometimes `0xFF`) |
| `+0xA0` | `i32` | fog/view distance in metres; `0` selects the Clip Distance preference |
| `+0xB0` | `u32` | environment word: high 16 bits = day-cycle owner (`1`), low 16 bits = context environment mode |

Loader state 5 (`0x0040CB32`) passes them to `0x0041D380` (colours → environment entry `+0x70..+0x7C`),
`0x0041D4D0` (distance) and `0x0041D3F0` (environment word). `0x00402B70` repeats the colour copy when it loads an
AREA into a slot. The environment table is `0x009103E0`, two entries of `0x84` bytes, one per resident scene;
entry `+0x04` is the scene's render context.

Retail examples:

```text
AREA 0  ANEKBAH   colours B1AB5F 8C6946 523A1B 1A1309  distance 0   word 00010001
AREA 1  JAUNPUR   colours D5E8F9 78AAC5 636738 261704  distance 0   word 00010003
AREA 3  SHALL01B  colours 8ACBEA x4                    distance 80  word 00000000
AREA 59 A_SHOOTG  colours 4E4F1C x4                    distance 50  word 00000000
```

Outdoor cities follow the day cycle and the player's view distance; interiors use a fixed colour and an authored
distance.

## 2. View distance

`0x0041D4D0(scene, metres)`:

```text
context +0x140 = metres                         (authored request, kept for re-application)
effective = metres == 0 || metres > clipDistance ? clipDistance : metres
0x00440BE0(context, effective * 39.37007874, 1)
entry +0x80 = effective
```

`clipDistance` is the OMK_SAVE preference at `+0x14` (`0x0090E194`, default `50`). `0x0041D570` re-applies every
resident scene when the preference changes; a low-detail preset (`0x0043A751`) forces it to `25`.

`0x00440BE0(context, distance, 1)` writes:

```text
context +0x154 = distance          fog end and far-cull distance
context +0x148 = distance * 0.25   fog start
context +0x14C = distance * 0.95   far alpha-fade start
```

The projection setup (`0x004944C6`) copies `+0x154` into the far-cull threshold (`0x006A2BC0`): a triangle whose
nearest vertex is beyond it is discarded unless the object has flag bit 17. View distance and fog end are therefore
the same value.

## 3. Fog colour and the day cycle

The game clock is `0x004C2BD0`. New games set it to `2 000 000` (`0x0040E12A`, `0x0040E252`); save loading restores
it through `0x0041E750`. Every engine frame `0x0041E600` adds `1.0` to an accumulator and, when it exceeds `5.0`,
subtracts `5.0` and adds `0xA6` to the clock: about one clock unit per millisecond at 30 frames per second.

| Global | Value | Meaning |
|---|---|---|
| `0x004C2C0C` | `3 600 000` | day length |
| `0x004C2C10` | `900 000` | phase length (day / 4) |

`0x0041E7A0`, once per engine frame:

```text
t = clock % day; phase = t / phaseLength; next = (phase + 1) & 3
f = (t % phaseLength) / phaseLength
for R, G, B: byte = from + _ftol((to - from) * f)          (truncation)
context +0x150 = packed colour
if entry is the day-cycle owner: clear colour 0x0090EFB0 = colour (and owner lighting at context +0x1A0/+0x1A4)
distance: special mode (0x0093082C == 1) ? colour 0x405028, 590.55 in (15 m) : entry +0x80 * 39.37
```

When no resident scene, or both, own the day cycle, the clear colour becomes the current scene's `+0x150`. The frame
always clears with `0x0090EFB0` (`0x00420700`, flag `0x0090E154` set once at initialisation), so the background
behind the world and the sky is the fog colour.

## 4. Rasterisation

`0x00460060` copies the context into backend globals: `0x0080B088 = +0x154`, `0x008F58C0 = +0x148`,
`0x0080B080 = +0x14C`, and the fog colour bytes.

**Direct3D fog path** (`0x007CAD20 != 0`, renderer mode 2): per bucket, `0x00461403` sets `FOGENABLE`,
`FOGTABLEMODE = D3DFOG_LINEAR`, `FOGTABLEDENSITY = 1.0`, `FOGTABLESTART = +0x148` and `FOGTABLEEND = +0x154`;
bucket `0x0800` doubles start and end. Buckets with `0x2000` (translucent) or `0x0080` are not fogged. `FOGCOLOR`
is the context colour. This is per-pixel linear fog on eye depth.

**CPU path** (other modes): per vertex `t = clamp((z' − start) · 256 / (end − start), 0, 256)` with `z' = z/2` for
bucket `0x0800`, and the vertex specular colour becomes `fogRGB · t / 256`. Both paths share the same linear range.

Both paths also write a diffuse alpha of `clamp((end − z') / (end − 0.95·end) · 255, 0, vertexAlpha)`, a fade over
the last 5 % of the distance that only has a visible effect on alpha-blended buckets.

Polygon submission (`0x004951C0`) maps 3DO flags to buckets: bit 16 → `0x0800`, bit 12 (alpha blend) → `0x2000`.
Opaque and cutout 3DO geometry is fogged; alpha-blended 3DO geometry and translucent sprites are not.

## 5. OpenNomad mapping

- `IamAreaRecord::fog_colors()`, `fog_distance_metres()` and `environment_flags()` expose the fields;
  `prepare_area_world_context` stores them as the context's `Fog::Environment`.
- `Fog::DayClock` (owned by `WorldPresentationState`, reset to the new-game value with the session) reproduces the
  clock cadence; `Fog::day_color()` and `Fog::evaluate()` reproduce `0x0041E7A0` and `0x0041D4D0`.
- `WorldScene` evaluates each attached context's fog, clears with the Runtime clear colour, and sets the camera far
  plane to the furthest fog end, so the `video.clipping_distance` setting now controls view distance.
- World, character, object, opaque-sprite and sky shaders share one per-pixel fog stage. Mesh bit 16 and sprite
  bucket `0x0800` halve the fog depth. The blend happens in encoded colour, as the D3D fog unit did.
- `enhancements.radial_fog` (default on) measures Euclidean eye distance so the fog does not shift as the camera
  turns. Off reproduces Runtime's planar eye depth.
- The day-cycle owner lighting (`+0x1A0/+0x1A4`) and the 0.95 alpha fade on blended geometry are implemented; see
  [lighting.md](lighting.md).
- Not implemented: the low-word environment modes, the special 15 m mode (`0x0093082C`), and clock persistence in
  saves.
