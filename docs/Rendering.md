# Rendering

OpenNomad presents recovered Runtime state through SDL3 and an OpenGL 4.1 core context. Runtime and serialized data stay
in their native coordinate system; OpenGL conversions happen at the presentation boundary.

## Startup and scene ownership

The normal application path is:

1. `Application::create` initializes SDL, the OpenGL window, optional audio, scenario ownership, input, and the startup
   coordinator.
2. `StartupVideoSequence` presents the optional publisher, developer, and intro videos through `VideoScene`.
3. The gameplay-mode `aventure.SCX` is selected and `SplashScene` displays `IMAGES/OMIKRON.BMP` for five seconds with
   a fade in and out.
4. The recovered scenario modes establish the first world context. `Application` replaces the splash with one stable
   `WorldScene`, then AREA execution opens the main-menu interface.
5. `WorldScene` remains the normal presentation scene. World-context changes replace its observed GPU cache, not the
   scene itself.

`ModelViewerScene` is a development tool for inspecting standalone or SCX-embedded resources with a free camera. It is
not part of normal startup and is not the runtime world.

## Per-frame flow

For an executed frame, `Application`:

1. resolves input and recovered frame timing;
2. updates the active scene;
3. drains interface completions;
4. advances the scenario scheduler;
5. updates audio;
6. starts the ImGui/OpenGL frame through `Window::begin_frame`;
7. renders the active scene; and
8. renders ImGui and presents the backbuffer through `Window::end_frame`.

The scenario update occurs after `WorldScene::update`, so `WorldScene::render` also consumes newly emitted presentation
commands. It never advances transition clocks during rendering.

## World presentation layers

`WorldScene::render` composes these layers in order:

1. `WorldRenderer`: decor meshes, runtime characters, and Runtime-style sprites.
2. The world white-fade overlay.
3. The cinematic letterbox overlay.
4. `InterfacePresenter` / `InterfaceManager`: resident I2D interfaces.
5. The ImGui debug UI, drawn later by `Window` in Debug builds.

This ownership is intentional. Scenario and AREA runtimes emit typed camera/fade/letterbox/interface intent; they do
not know about OpenGL, viewport dimensions, or renderer timing. Presentation commands include the world scene ID and
generation so stale commands are harmless after a context is recycled.

## World renderer

`WorldRenderer` is created for one active `WorldSceneContext` generation. `ScenarioManager` continues to own decoded CPU
data and mutable runtime state; the renderer owns only OpenGL objects and presentation caches.

The current world pass:

- uploads decor geometry built from the decoded 3DO hierarchy and its 3DT textures;
- lazily uploads runtime character models and refreshes their meshes when a pose revision changes;
- builds camera-facing sprite queues from the active runtime;
- draws opaque/cutout decor, characters, and sprites first with depth writes enabled; and
- draws translucent groups and sprites afterwards with depth writes disabled.

Decor transparency is sorted back-to-front by material-group centre. Missing or invalid decor textures fall back to a
white texture and produce a diagnostic instead of invalid OpenGL access.

The development `ModelViewerScene` contains richer experimental passes for mirrors, environment mapping, explicit
model lights, and reflection rendering. `WorldRenderer` currently diagnoses mirror/environment flags but uses its base
textured pass for them. Documentation should not imply that a model-viewer experiment is already part of the runtime
world renderer.

## Game-data resources

The relevant loaders are under `Core/Omikron/`:

- `BinaryReader` — bounds-checked little-endian reads over an in-memory span.
- `Model3DO` — model header, materials, object hierarchy, mesh geometry, flags, and explicit light records.
- `Texture3DT` — palettes and LZ/raw indexed texture payloads decoded to RGBA8.
- `Animation3DA` and `Path3DP` — body animation and authored path/camera data.
- `SCX` — scenario descriptors, scripts, sprites, and embedded resource streams.
- `IamArchive`, `IamStart`, `IamArea`, and `IamStringTable` — startup/AREA/interface data.
- `BmpImage` and `IndexedBmp8` — loading screens and indexed interface images.
- `QdAdp` — original QD ADP audio payloads.

Every file loader must pass its path through `Resources::resolve_case_insensitive` before opening it. The original data
contains inconsistent casing and must behave like it did on Windows even when OpenNomad runs on a case-sensitive file
system.

Set `OPENNOMAD_GAME_DATA_ROOT` to the original game directory. Without it, game paths are resolved relative to the
executable in the original directory layout.

## Coordinates and transforms

Authoritative scenario, 3DO, camera, character, and sprite state stays in Runtime-native inches and Runtime's row-vector
math conventions. The shared presentation adapter applies the unscaled basis change:

```text
(x, y, z) -> (x, -y, -z)
```

Do not insert display scale factors, axis swaps, or sign changes into parsers or runtime state. Unit conversion belongs
at a field-specific external boundary—for example inches to metres for the audio spatializer—while the OpenGL basis
conversion belongs in `RuntimePresentation`.

See [Runtime coordinate and transform math](reverse-engineering/runtime-coordinate-math.md) for the recovered equations
and the boundary rationale.

## Rendering building blocks

Reusable OpenGL wrappers live directly in `src/core/Core/`:

- `Mesh`, `VertexArray`, `VertexBuffer`, and `IndexBuffer` own geometry resources.
- `Texture2D`, `IntegerTexture`, and `TextureCube` own texture resources.
- `Shader` owns a linked GLSL program and cached uniform locations.
- `UniformBuffer` owns std140 uniform-block storage.
- `Framebuffer` owns offscreen colour/depth targets used by development rendering.
- `Camera` stores the OpenGL-facing view/projection state; `WorldCameraSystem` derives it from Runtime camera intent.
- `Renderer` initializes shared GL state and clears the drawable pixel viewport each frame.

These wrappers are move-only RAII types. They must be created after the OpenGL context and destroyed before it.
`Application` and `Window` member ordering enforces that lifetime.

## High-DPI and debug UI

The SDL window is created with `SDL_WINDOW_HIGH_PIXEL_DENSITY`. `WindowSizeState` tracks logical size separately from
drawable pixel size; scenes and `Renderer` receive the drawable size. The ImGui Manrope font is loaded at the current
window display scale, then compensated with `FontGlobalScale`.

`DPIHandler::get_dpi_aware_window_size` scales the initial logical window settings. Runtime resize events keep both
logical and drawable measurements current; renderer code should not query or assume a fixed desktop scale.

The Manrope asset is Git LFS tracked under `src/assets/fonts/`. A pointer file in place of the font can cause startup to
fail inside ImGui, so run `git lfs pull` when assets are missing.
