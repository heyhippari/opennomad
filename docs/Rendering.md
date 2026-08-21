# Rendering

The rendering pipeline is built for gradual expansion toward reimplementing a late-1999-era 3D game
(textured meshes, per-object transforms, a perspective camera) on top of an OpenGL 4.1 core context.

## Overview

A frame flows through the application in this order:

1. `Application::run()` advances the active `Scene` (`update(delta_time)`) and notifies it of the
   drawable size (`resize(width, height)`).
2. `Window::begin_frame(delta_time)` starts the ImGui frame and clears the framebuffer via
   `Renderer::begin_frame`.
3. The scene draws itself (`Scene::render()`).
4. `Window::end_frame()` draws the ImGui UI on top and presents the backbuffer.

At startup the active scene is `SplashScene`, which shows `IMAGES/OMIKRON.BMP` (centred,
contain-fit) for five seconds while `ModelViewerScene` is preloaded; the application then swaps
scenes and the splash's GL resources are released with it.

The scene (`ModelViewerScene`) loads the effect model `EFFECTS2_SMOKE2.3DO` embedded in the scenario
package `SCPTDATA/aventure.SCX` (indexed by the `Core::Omikron::SCX` reader and decoded through
the same `Model3DO` / `Texture3DT` pipeline) and renders it with a free-flying camera
(WASD + mouse look), exercising mirrors and environment-mapped meshes when the model
carries those flags. Standalone loading (`ModelViewerScene::create()`) still reads
`MESHES/DECORS/Anekbah.3DO` with its `.3DT` sidecar.

## Omikron game data

- `Core/Omikron/BinaryReader` — bounds-checked little-endian reader over in-memory buffers.
- `Core/Omikron/Model3DO` — parses .3DO model files (header, materials, mesh descriptors, vertices,
  triangles, rectangles, bone hierarchy, explicit lights) and builds static per-material
  `MaterialGroup` geometry. Material records mirror the original runtime's `Runtime3DOTexture`
  layout: three 20-byte strings (name, texture name, palette name), `data_size`, four 16-bit
  texture-page/slot allocation fields (0xFFFF in serialized files), a 16-bit palette depth,
  two atlas-offset bytes (runtime-only, not used here) and dimensions. The root header preserves
  the original `frame_count`/`texture_count`/`object_count` fields for documentation.
- `Core/Omikron/Texture3DT` — decodes the .3DT sidecar (palettes + LZ decompression). A payload is
  stored raw exactly when `data_size == width * height` (the original loader's detection); every
  other payload goes through the LZ decoder. Each material decodes against its own palette into an
  independent RGBA texture — the modern equivalent of the original runtime's shared 256x256 pages.
- `Core/Omikron/SCX` — indexes .SCX scenario packages: the bounded descriptor block's
  DEAD0004 sprite table and the appended resource stream's self-offset-framed WAV and
  3DO resources. Returns byte offsets, so the embedded core/auxiliary spans feed the
  existing Model3DO/Texture3DT entry points unchanged.
- `Core/Omikron/BmpImage` — decodes the Windows BMP loading screens shipped in the game's
  `IMAGES/` folder (via SDL's loader, converted to RGBA8); `SplashScene` uses it to show
  `OMIKRON.BMP` at startup.

Game data is resolved relative to the executable via `Resources::game_data_path`, mirroring the
original game's file tree: copy the game's `SCPTDATA` folder next to the built binary so that
`SCPTDATA/aventure.SCX` can be found. Alternatively, set the `OPENNOMAD_GAME_DATA_ROOT`
environment variable to the folder containing `SCPTDATA` (e.g. a SteamLibrary install) to avoid
copying the data next to the binary after every clean rebuild. File lookups are case-insensitive
on every platform (via `Resources::resolve_case_insensitive`), matching Windows behaviour,
because the game's files are stored with inconsistent casing.

## Building blocks

All rendering types live in `src/core/Core/` and are copy/move-deleted RAII wrappers around GL objects.

- `Vertex` — the shared interleaved vertex format (position, normal, uv, colour). Attribute
  locations are fixed: 0 = position, 1 = normal, 2 = uv, 3 = colour.
- `VertexBuffer` / `IndexBuffer` — RAII VBO/EBO upload from raw pointer + byte/count pairs.
- `VertexArray` — RAII VAO.
- `Mesh` — combines a VAO, VBO and EBO; the attribute layout mirrors `Vertex`. Call `draw()` to render.
- `Texture2D` — RAII 2D texture with an RGBA8 upload path and an sRGB storage option
  (`GL_SRGB8_ALPHA8` matches the gamma-correct `GL_FRAMEBUFFER_SRGB` pipeline). Nearest filtering and
  repeat wrapping reproduce the crisp texture look of late-90s hardware.
  `generate_checkerboard()` produces the checker pixels (pure function, unit-tested).
- `Shader` — GLSL program with cached uniform locations and a textured default
  (`u_mvp`, `u_texture0`, `u_tint`). The default vertex shader consumes the `Vertex` layout.
- `UniformBuffer` — RAII `GL_UNIFORM_BUFFER`; binds std140 block data (the light array) to a
  uniform-block binding point.
- `Camera` — perspective view/projection; the scene multiplies `projection * view * model` for the MVP.
- `Scene` — interface for a game state; owns its shaders, meshes, textures and camera.

## Mesh flags and transparency

Every 3DO mesh descriptor carries a flag word (`Omikron::MeshFlags`); `blend_mode(flags)` reduces
it to a `BlendMode` that drives the draw state:

| Flags | Result |
|---|---|
| (none) | Opaque: depth-tested, depth-write on, no blending. |
| `alpha_testing` | Cutout: the shader discards fragments whose texture alpha is below 0.5. |
| `alpha_blending` | Source-alpha blending; texture alpha comes from the palette black key (pure black → transparent). |
| `alpha_blending + additive` | Additive blending (`ONE, ONE`). |
| `alpha_blending + subtractive` | Subtractive blending (`GL_FUNC_REVERSE_SUBTRACT`, `ONE, ONE`). |
| `mirror` | Planar reflection: the scene is re-rendered through the mirror plane into an offscreen `Framebuffer` (clipped with `gl_ClipDistance`, reflected view, winding flip) and composited by a dedicated shader. `additive`/`subtractive` turn the mirror into glossy add/subtract modes. |
| `environment_mapped` | Chrome-like shading: fresnel blend of the surface colour with a procedural sky cube map (`TextureCube`). |
| `vertex_lit` | Multiplies the texture by the per-vertex colour. |
| `invisible`, `joint_only` | Mesh produces no geometry. |
| `uv_scroll_u`, `uv_scroll_v` | Runtime-confirmed independent cyclic U/V phases. Parsed and diagnosed; phase animation is not rendered yet. These bits are not ordinary-3DO skybox semantics. |
| `underwater`, `water_surface`, `water_unknown`, `fps_arm`, `face_morph`, `has_parent`, `has_children` | Parsed but not rendered yet. |

Rendering runs two passes per camera pose: opaque (and cutout) groups first with depth writes,
then blended groups far-to-near with depth writes off. Mirrors are one-bounce: the reflection pass
draws everything except mirror surfaces. The cube map used for the still-tentative environment-map
presentation is generated procedurally (`generate_sky_cubemap`) until its Runtime behavior and
assets are fully recovered.

3DO decoding, scenario state, scripted cameras, and sprites stay in Runtime-native inches. The GL
edge applies the shared unscaled basis `(x,y,z) -> (x,-y,-z)`; the model viewer and world renderer
use the same adapter. See
[Runtime coordinate and transform math](reverse-engineering/runtime-coordinate-math.md).

AREA-activated characters follow the same boundary. Their logical instance, AREA presence and
transform remain in canonical Runtime space under `ScenarioRuntime`; their model is decoded through
the shared `Model3DO`/`Texture3DT` pipeline. `WorldRenderer` lazily caches the corresponding GPU
meshes/textures and applies the character transform before the single Runtime-to-GL basis adapter.

## Model lights

The `.3DO` header carries two light counts. The first (`lights_unknown1`, "mesh lights") has no
record section — that lighting is baked into the per-vertex colours. The second
(`lights_unknown2`) counts explicit 304-byte records at `lights_offset`, which `Model3DO`
decodes into `Omikron::Light` entries:

| Field | Meaning |
|---|---|
| flags (2×16-bit) | Unresolved runtime flags; stored raw, not interpreted. |
| name | 20-byte NUL-padded name. |
| float 1 / float 2 | Native far-attenuation end / start in inches. |
| intensity | Raw intensity multiplier. |
| BGRA colour | Source colour bytes. |
| six points | Slot 0 = light position, slot 1 = target (spot direction); slots 2–5 are cone/frustum shape data and unused. |

Following the reference importer's interpretation, `ModelViewerScene` turns each record into a spot
light: position = point 0, direction = point 1 − point 0, cone = 40° full hotspot fading to
120° full falloff, linear attenuation between start and end, and colour × intensity ×
`k_light_intensity_scale` (2.0 by default). Lights whose target coincides with their position
degrade to point lights.

All lights are uploaded once into a std140 uniform block (`LightBlock`, `u_lights[255]`,
binding point 0) shared by the main, mirror and environment shaders. When a model has no
explicit lights the shaders keep the legacy single directional light (`u_light_direction` +
`u_ambient`) as a fallback. The light block is static — no per-frame upload — and applies
equally in the mirror reflection pass. Light flag semantics, points 2–5 and the original
attenuation/colour space remain open research; see the OmikronConvert `OD3X_LIGHTS.md` notes.

## Sprites (Runtime-style billboards)

Embedded effect resources from `SCPTDATA/aventure.SCX` render as camera-facing billboard quads
through the sprite system in `Core/Sprite/` (see docs/ReverseEngineering.md for the recovered
Runtime semantics). The flow:

1. `ModelViewerScene::create_from_scx` parses the SCX container, loads the static Anekbah level as a
   backdrop, and registers the 20 embedded effects as lazy `SpriteResource`s.
2. `SpritePool` holds `SpriteInstance`s behind generation-counted `SpriteHandle`s; instances
   attach to the scene's render list (head-inserted, like the original) and are destroyed
   without dangling references (`destroy` auto-detaches).
3. Each frame `SpriteRenderer::build_queue` walks the render list, resolves the selected frame
   descriptor, and emits six CPU vertices per visible sprite into one dynamic vertex buffer.
   Billboards are built in world space from the camera right/up basis, so apparent size,
   rotation and depth testing match the surrounding geometry exactly.
4. `render_scene` draws opaque/cutout sprites in its depth-writing pass and translucent sprites
   in its blended pass. Commands are stably sorted by a compact `SpritePipelineKey`
   (texture identity + render mode), preserving the scene-list insertion order inside a batch.

Render modes 0–8 map to proven Runtime behaviour (alpha, additive, darken, cutout variants;
see `SpriteRenderMode.hpp`). The sprite shader supports cutout discard, a Rec.601 grayscale
uniform and provisional linear-fog uniforms. The second-UV multitexture path (`0x0040`) is
reserved in the pipeline key but not implemented. Sprites are omitted from mirror reflections
for now.

## Expansion points

Intended next steps for a full game pipeline, none of which require redesigning the current classes:

- File-based textures (SDL3_image is already linked) and mipmaps.
- A material abstraction (shader + textures + uniform values) attached to meshes.
- Animation playback (ANIMS) driving the skinned character meshes.
- Level geometry import (MAP2D) feeding `Mesh` with larger vertex/index arrays.
- Multiple scenes (menu, world, cutscene) swapped through the existing `Scene` interface.
- A transform/scene-graph layer replacing the ad-hoc model matrix in `ModelViewerScene::render()`.
- Orthographic and screen-space passes (e.g. for a software-rendered-style HUD).

## Notes

- GL objects must be created after the window's GL context and destroyed before it;
  `Application` owns the scene after the window and resets it first in its destructor.
- The debug build runs clang-tidy with warnings as errors. The project compiles as strict C++23
  (`CMAKE_CXX_STANDARD 23`, `CMAKE_CXX_EXTENSIONS OFF` in `cmake/StandardProjectSettings.cmake`).
  Extensions being OFF forces `-std=c++23` into `compile_commands.json`, so clang-tidy parses the
  sources as C++23 as well. C++23 library features (`std::span`, `<ranges>`, `std::expected`,
  `std::flat_map`, ...) are allowed in new code.
