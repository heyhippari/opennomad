# Runtime sky

> **Status:** recovered from the PE32/i386 retail `Runtime.exe` (image base `0x00400000`)
> **Last updated:** 2026-10-29

Addresses are evidence only and are not encoded in gameplay code.

Runtime has no skybox, cube map or clear-colour gradient for the sky. It loads one ordinary 3DO model from
`MESHES\MISC\` and inserts it into the current 3D scene as a large object that follows the camera.

## 1. Data

AREA record `+0x85` holds a 9-byte sky 3DO stem (see [iam-area.md](iam-area.md)). Retail values include `ASKY`,
`SSKY`, `LSKY`, `MASKY`, `JANSKY`, `DOCKSKY` and `TOITSKY`; empty means "no sky".

Every retail sky model is a single top-level object with the same 12×12-quad flat grid (169 vertices,
144 rectangles) lying at local Y≈0, about 5300 inches wide, one material (`CIEL1.BMP`, `CIEL2.BMP`, `CIEL3.BMP` or
`CJAUNPUR.BMP`) and object flags `0x01000000` (U-scroll, so the clouds drift with the global U phase). Most share the
serialized position `(28.5, 401.1, 909.2)`.

## 2. Ownership — one global sky

| Global | Meaning |
|---|---|
| `0x00930728` | loaded sky 3DO resource |
| `0x00910680` | its root runtime object |

There is one sky for the whole process, not one per resident AREA slot.

**Load** — the AREA load state machine (state 5 at `0x0040C990`) appends `.3DO` to a non-empty `+0x85` name and calls
`0x0041D1A0(name)`:

1. Build `"MESHES\MISC\" + name` (`%s%s` with `0x004C30CC`).
2. If a sky is loaded and its stored path (`resource+0x30`) is equal, keep it.
3. Otherwise detach and free the current sky (`0x00437400`, `0x004374E0(obj,0,0)`, `0x00441A00`), load the new 3DO
   (`0x0044EA10`) and take its root object (`0x00436C40(res, 0)`).
4. `0x00437B90/0x00437C10/0x00437C90(root, 12.5, 8)`: scale X/Y/Z by `12.5`. Mode 8 walks the whole hierarchy
   through `0x0048CFE0`, writing runtime-object `+0x80/+0x84/+0x88`.
5. Root descriptor position Y (`+0x28`) `-= 2250.0` (`0x004BC1C0`).
6. Root flags `&= ~0x3000` (alpha blend and additive cleared).
7. `0x00437220(root, 0xC0, 4)`: root flags `|= 0x00030000` (bits 16 and 17).

**Release** — when `0x00402B70` starts loading an AREA into a slot, it calls `0x0041D320` (free the sky) only if
neither the current slot's AREA nor the other slot's AREA has a sky name. A destination without a sky therefore
keeps the previous sky. Full session teardown (`0x00419890`) also frees it.

## 3. Per-frame placement — `0x00420753`

After the frame's camera/scene setup:

```text
if displaySky (0x0090E190, OMK_SAVE +0x10):
    if sky root && current scene (0x0093076C) && camera:
        0x00446BC0(camera, &x, ..., &z, ...)          camera position
        0x004370A0(root, x, root.y, z)                X/Z follow the camera; Y stays
        0x00437400(root); 0x004374E0(root, scene, 0)  (re)insert into the scene
else if sky root:
    0x00437400(root)                                   remove from the scene
```

The resulting plane is about 66 000 inches (≈1.7 km) wide and sits at a fixed world Y of `401 − 2250 = −1849`
inches; Runtime `+Y` is down, so it is a cloud ceiling about 47 m above the world origin.

## 4. Render behaviour of bits 16 and 17

Polygon submission (`0x004951C0`) reads the object flags:

| Bit | Effect |
|---|---|
| 17 `0x00020000` | clears the per-object "far cull" flag; triangles whose nearest depth exceeds the far threshold (`0x006A2BC0`) are no longer discarded (`0x00495421`, `0x00495926`) |
| 16 `0x00010000` | selects bucket bit `0x0800`, the doubled-fog-range bucket also used by sprites; the backend multiplies depth by `0.5` before computing fog alpha (`0x004304E4`) |

The sky is otherwise an ordinary opaque, textured, back-face-culled 3DO object, sorted and depth-tested with the rest
of the scene. Because it is far above the camera and exempt from far culling, it shows wherever the clip distance
has removed world geometry.

## 5. OpenNomad mapping

- `WorldPresentationState::apply_area_sky()` keeps the selected sky name with the load/release rule above.
  `ScenarioStartupController` applies it for the initial AREA and for every transition destination;
  `ScenarioManager`'s session reset clears it.
- `SkyRenderer` (owned by `WorldScene`, independent of world contexts) loads `MESHES/MISC/<name>.3DO/.3DT`, applies
  the load-time mutations through `Sky::build_geometry()` and follows the camera X/Z with
  `Sky::follow_transform()`.
- It draws first in the world pass, with depth test and writes disabled and `GL_DEPTH_CLAMP` instead of far-plane
  clipping. This is the GL equivalent of bit 17 for a sky that is always behind world geometry inside the clip
  distance. The `video.display_sky` setting gates it.
- The sky is fogged with the current scene's fog at doubled range (bit 16); see [fog.md](fog.md).
