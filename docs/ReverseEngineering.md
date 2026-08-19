# Reverse engineering notes

Notes tying confirmed behaviors of the original Omikron executable to their
OpenNomad implementations. The main-loop behavior covered here is the idle-
driven Windows message loop and `RenderWindowProc`.

## Original globals → OpenNomad mapping

| Original | Meaning | OpenNomad |
|---|---|---|
| `DAT_004e7694` | `renderWindowActive` — activation/focus state of the render target | `App::RuntimeActivityState::render_window_active` |
| `DAT_0052dd58` | `applicationActive` — foreground/background state of the application | `App::RuntimeActivityState::application_active` |
| `DAT_0052dd4c` | `updatesSuspended` — engine-controlled suspension, independent of focus and pause | `App::RuntimeActivityState::updates_suspended` |
| `DAT_004c5944` | `resetFrameTimingOnNextUpdate` — set before waiting, consumed by the next frame | `App::RuntimeActivityState::reset_frame_timing_on_next_update` |
| `DAT_0052c4f0` | 256-entry key-state array, `= 1` on key down, `= 0` on key up | `App::Input::HeldInputState::m_keys` |
| left/right mouse globals | `leftMouseButtonDown` / `rightMouseButtonDown` | `App::Input::HeldInputState::m_buttons` |
| `WM_CHAR` variable | single captured character (signed byte) | `App::Input::TextInputState` (UTF-8) |
| `DAT_0090ef2e` | `skipEngineFrame` — skips only the engine-frame callback; input and timing still run | `App::Application::m_skip_engine_frame` |
| `g_inputState` / `g_previousInputState` | current / previous frame's input bitfield | `App::Input::InputManager` action values + `m_previous_held` |
| `g_inputEdgeMask` | per-bit edge mask: bit set = pressed only on the rising edge | `App::Input::InputManager::m_action_edge_mask` |
| `g_pressedInput` | per-frame pressed bitfield: `current & ~(previous & edgeMask)` | `App::Input::InputManager::m_action_pressed` |
| `DAT_0090e0e0` | per-frame input variable cleared to 0 before the engine callback (purpose unresolved) | `App::Input::InputManager::m_per_frame_input` |
| `g_forcedDeltaTime` | optional forced delta, `-1.0` = inactive | `App::FrameTiming::FrameTimingState::forced_delta` (`std::optional`) |
| `g_gameDeltaTime` | effective delta consumed by the engine frame | `App::FrameTiming::FrameTimingState::effective_delta` |
| `FUN_004200f0` | engine-frame callback of the timed frame (internals unresolved) | `App::Application::run_engine_frame` (provisional) |

`HeldInputState` indexes SDL scancodes (`SDL_SCANCODE_COUNT` entries) rather
than the original 256 DirectInput key codes because no Omikron key mapping has
been reverse-engineered yet; the mapping must not be invented.

## Win32 event → SDL3 event mapping (collapsed)

| Original (`RenderWindowProc`) | SDL3 equivalent |
|---|---|
| `WM_ACTIVATE: WA_INACTIVE` (`renderWindowActive = applicationActive = false`) | `SDL_EVENT_WINDOW_FOCUS_LOST` |
| `WM_ACTIVATE: WA_ACTIVE` / `WA_CLICKACTIVE` (both `true`) | `SDL_EVENT_WINDOW_FOCUS_GAINED` |
| `WM_SETFOCUS` (`renderWindowActive = true`) | `SDL_EVENT_WINDOW_FOCUS_GAINED` |
| `WM_KILLFOCUS` on an embedded child render window | n/a — OpenNomad has a single SDL window |
| `WM_ACTIVATEAPP` (`applicationActive = wParam != 0`) | `SDL_EVENT_WILL/DID_ENTER_BACKGROUND` and `..._FOREGROUND` |
| `WM_CHILDACTIVATE` (`applicationActive = true`) | `SDL_EVENT_WINDOW_FOCUS_GAINED` |
| Left click activating an embedded render window | n/a — the window manager delivers `FOCUS_GAINED` |
| `WM_QUIT` | `SDL_EVENT_QUIT` (+ `SDL_EVENT_TERMINATING`) |
| `WM_KEYDOWN` / `WM_KEYUP` | `SDL_EVENT_KEY_DOWN` / `SDL_EVENT_KEY_UP` |
| `WM_LBUTTONDOWN/UP`, `WM_RBUTTONDOWN/UP` | `SDL_EVENT_MOUSE_BUTTON_DOWN` / `SDL_EVENT_MOUSE_BUTTON_UP` |
| `WM_CHAR` | `SDL_EVENT_TEXT_INPUT` |
| `WM_MOVE` | `SDL_EVENT_WINDOW_MOVED` |
| `WM_SIZE` | `SDL_EVENT_WINDOW_RESIZED` + `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` |

Collapsed semantics, implemented in `App::Application::process_event`:

- Desktop SDL has no separate foreground/background delivery, so
  `FOCUS_LOST` clears **both** activation gates and `FOCUS_GAINED` sets both,
  like `WM_ACTIVATE`. The SDL application-background/foreground events exist
  only on iOS/Android but are still routed so the state stays correct there.
- `render_window_active` additionally requires the window not to be
  minimised (the modern equivalent of the original hiding the render window).
- The gates are initialised **optimistically** (both true) after window
  creation: on Wayland, SDL's input-focus flag is only set after the
  surface's first present, so gating the first frame on it would deadlock
  (no frame → no present → no focus event). `SDL_EVENT_WINDOW_SHOWN` /
  `SDL_EVENT_WINDOW_RESTORED` therefore never close the render gate — they
  open it only when focus is already held; focus transitions alone close it.

## Main loop

The original is an idle-driven message loop: drain messages, run a frame only
when the queue is empty, set `resetFrameTimingOnNextUpdate` and call
`WaitMessage()` when any gate blocks, exit on `WM_QUIT`. OpenNomad implements
this in `App::Application::run()` with the decision logic in
`App::MainLoopController::advance`:

- `SDL_PollEvent` drains the queue before every gate evaluation.
- A blocked loop calls `SDL_WaitEvent` (not a sleep/busy loop).
- The timing-reset flag is set while blocked, survives event processing, and
  is cleared via `clear_reset_flag_after_frame()` only after a frame runs.
- On the first resumed frame the recovered frame clock is re-baselined so
  the inactive interval does not become a huge frame delta (see
  [UpdateFrameTiming](#updateframetiming-timed-frame) below).

## UpdateFrameTiming (timed frame)

`Core/FrameTiming.hpp` implements the recovered per-frame order of the
original `UpdateFrameTiming`. The provisional signature is
`RunTimedOmikronFrame(bool resetFrameClock, bool skipEngineFrame)`, called
from the main loop with `(resetFrameTimingOnNextUpdate, skipEngineFrame)`.

Executed order (see `App::FrameTiming::run_timed_frame`):

1. If the reset request is set: re-baseline `frameStart`, `currentTime` and
   the secondary frame clock to now — immediately before input, so inactive
   waiting time is never measured. The moving average and current deltas are
   deliberately preserved.
2. Snapshot/poll input (`App::Application::snapshot_input`).
3. Update the pressed bitfield — `pressed = current & ~(previous & edgeMask)`
   evaluated on the mapped action bits, not on raw SDL state.
4. Reset the per-frame input field (`DAT_0090e0e0 = 0`, neutral field,
   purpose unresolved).
5. Unless `skipEngineFrame`: run the engine callback
   (`App::Application::run_engine_frame`, provisionally `FUN_004200f0`).
6. Measure the completed frame: `frameTime = now - frameStart`,
   `average = (average + frameTime) / 2`, clamp zeros to 1 ms, then
   instantaneous and average FPS, then `frameStart = now`.
7. Calculate the delta for the **next** callback (Omikron units, 1.0 = 1/30 s):
   dynamic `min(30 / averageFPS, 3.0)`; fixed modes directly replace it:
   30 Hz = 1.0, 60 Hz = 0.5, 300 Hz = 0.1, 15 Hz = 2.0. The original switch
   had no meaningful default assignment; out-of-range values fall back to
   the dynamic calculation.
8. External-clock correction — **deferred** (see below).
9. Forced-delta override (`g_forcedDeltaTime`, `-1.0` sentinel replaced by
   `std::optional<float>`).
10. `base_delta = delta`; `effective_delta = gamePaused ? 0 : delta`.

Critical recovered behavior:

- The engine callback runs **before** the new delta is calculated, so it
  consumes the `effective_delta` produced by the preceding frame.
- Gameplay pause zeroes only the effective delta; `base_delta` keeps the
  unpaused value for consumers that may need it.
- Pause ordering is preserved: the callback runs before the pause is applied
  to the newly calculated delta, so a callback sees zero only if pause was
  already active during the preceding calculation.
- Skipping the callback does not skip input or timing work; `updatesSuspended`
  prevents the timed frame from being invoked at all.
- OpenNomad converts delta to seconds exactly once, in
  `App::Application::run_engine_frame` (`delta / 30`), because the scene API
  speaks seconds. The state itself keeps Omikron units.

### Deferred or unresolved

| Item | Status |
|---|---|
| External-clock synchronization (`FUN_0042cc10` / `GetEngineTime(0)`) | Deferred: no subsystem with these semantics exists; the normal path is "external clock inactive" |
| Timed accumulator (`+= delta`, reset to -1 above 10000) | Deferred: consumer and purpose unidentified; no field was invented |
| `FUN_004200f0` internals | Unresolved: `run_engine_frame` is the provisional boundary, routing OpenNomad's existing update/render calls |
| Default value of `g_inputEdgeMask` | Unresolved: OpenNomad defaults every action to edge-triggered |

## Escape

Original: `if (EscapeHeld && !gamePaused) DispatchGameCommand(0x1F, -1, -1);`
using `GetAsyncKeyState` — a per-frame held-key test, not a key-down event.
OpenNomad: `MainLoopController::should_dispatch_escape` +
`App::Application::dispatch_held_escape`, fed by
`App::FrameTiming::FrameTimingState::gameplay_paused` (the recovered
gameplay-pause state, distinct from the activity gates). Command `0x1F` is
not yet represented, so the dispatch is a documented no-op integration
point for the future command layer. The old
OpenNomad behavior of Escape releasing the debug-UI mouse capture was moved
to F12.

## Text input

Original: conditionally captures `WM_CHAR` into a single signed-byte
character. OpenNomad: `Input::TextInputState` keeps SDL's UTF-8
representation and only accepts input while capture is enabled. Note that
SDL text-input activation (`SDL_StartTextInput`/`SDL_StopTextInput`) is
currently coordinated by the ImGui backend; a future consumer of the
captured text must coordinate with it.

## Sprites (SpriteInstance)

Original Runtime facts and their OpenNomad mapping:

| Original | OpenNomad |
|---|---|
| `SpriteInstance` (0x40 bytes): render-list owner, object, position, render mode, frame index (0xFFFF = invalid), scaleX/Y (default 1), rotation (radians), `+0x24` unknown (default 0.9), texture offsets U/V, `0x00RRGGBB` colour (default white), external association, intrusive prev/next | `Core/Sprite/SpriteInstance` with identical defaults; `+0x24` kept as `unknown_24`, provisionally named |
| Fixed pool of 2,048 instances, first free slot (free = object null), full record clear on create | `SpritePool` with dynamic growth (2,048 initial reservation), generation-counted `SpriteHandle`s — no raw pointers, stale handles fail safely |
| Attach prepends to the scene's sprite list; detach unlinks and repairs neighbours; destruction does NOT auto-detach | `attach`/`detach` as before; `destroy` auto-detaches for safety (explicit detach still available) |
| `SetSpriteFrame` validates `frameIndex < frameCount`, stores 0xFFFF on failure; `SetSpriteRenderMode` is a direct assignment | `SpritePool::set_frame` returns `std::expected` and stores 0xFFFF on failure; `set_render_mode` never fails |
| Render modes 0–8 with bucket bits: 1/8→0x0400, 2→0x2000, 3→0x2400, 4→0x2100, 5→0x2500, 6→0x2200, 7→0x2600 | `SpriteRenderMode` (values preserved, modes 1 and 8 distinct with shared pipeline config) + `render_state()` table |
| 0x2000 translucent sprites are not fogged; 0x0800 doubles the fog range | Fog exclusion is implemented; the doubled-range flag is reserved in `SpritePipelineKey` |
| 0x0040 second-UV/multitexture path | Reserved in `SpritePipelineKey`, flagged as unsupported in the inspector |
| Renderer-wide grayscale with luminance (299R + 587G + 114B) / 1000 | `u_grayscale` uniform in the sprite shader, scene-wide toggle (3D scene only) |
| 16,384 render buckets keyed by packed texture/state bits | Modern equivalent: `SpritePipelineKey` + stable-sorted `SpriteDrawCommand`s (no global bucket table) |
| Billboard projection on the CPU into screen space | CPU-generated camera-facing world-space quads (six vertices) into one dynamic vertex buffer |

Frame descriptor semantics (verified against `aventure.SCX`, 2026-08-17): the per-object
0x20 rectangle table is the frame table. A frame uses the 16-bit slots at +0x00 and +0x04
(the rectangle's first and third vertex indices — opposite corners) and the UV byte pairs at
+0x08/+0x09 and +0x0C/+0x0D; the texture index is the material id at +0x10. The referenced
0x20-stride point records are the object's own vertex records; width/height are the absolute
x/y deltas of the two points (negative deltas are normal — mirroring lives in the UVs).
UV bytes convert exactly as `value / 256`. Zero dimensions are degenerate and skip the sprite.

Unresolved: the serialized root `frameCount` (+0x48) is 0 in every observed file — the frame
count currently falls back to the rectangle table size; the distance-fade alpha equation is
not reconstructed (a provisional linear-fog path exists behind uniforms); `+0x24` semantics.

## SCX script section (DEAD0002)

Recovered from `Scenario_LoadFromFile` (0x00449750) and implemented in
`Core/Omikron/SCX.{hpp,cpp}`. The section has no byte-size word; its end is determined by its
counts and per-script variable structures. All integers are little-endian.

Layout: `u32 scriptCount`, `scriptCount` × fixed 0x64-byte script records, `u32 sharedValueCount`,
`sharedValueCount` × `u32` shared values, then per script (in file order): an optional
related-script block (`u8 present` + 21-byte name when present), `rootCommandCount` root commands,
`linkedCommandCount` linked commands (both 0x18-byte `SerializedScriptCommandDisk`), and two
trailing binding tables (`u32 count`, two per-entry placeholder arrays, 21-byte names).

The fixed 0x64-byte script record preserves `scenarioOwnerPlaceholder`, a 22-byte name,
`scriptId` (+0x1A), `runtimeState` (+0x1C), `flags` (+0x1E), root/linked command counts and
placeholder pointers (+0x20..+0x30), `executionContextField34` (+0x34), `runtimeField38` (+0x38),
binding-table field triples (+0x3C..+0x50), `relatedScriptPlaceholder` (+0x54), `runtimeField58`
(+0x58) and 8 tail bytes (+0x5C). The Runtime loader resets `runtimeState = 0`, clears the low
nibble of `flags`, zeroes bytes +0x5E..+0x61 and (per instance) resets `currentRootCommandIndex`
and `runtimeField38` — those are retained in the parsed prototype, not applied there.

Command relocation (`0x0044A070`) is represented with safe indices: `firstValueIndex` indexes the
shared value pool, `nextCommandIndex == -1` becomes an empty optional, and every argument slice
and next index is bounds-checked (the original only partially checks these).

## SCX script runtime (`Core/Script/`)

`ScriptOpcode.hpp` is the single source of truth for opcode knowledge (name, argument count,
semantic parameter map, sprite-ownership, support status, notes); it drives dispatch and the
ImGui debugger. `ScriptRuntime` separates immutable parsed definitions (`Omikron::ScxScript`,
`Omikron::ScriptValue`) from mutable runtime state (`ScriptInstance`: deep-copied value pool,
root/linked commands with execution counters, current group index, instance-local
source-sprite → runtime-sprite remap, pause/trace state).

Supported opcodes and argument schemas:

| Opcode       | Name                | Arguments                                                                  |
|---|---|---|
| `0x0400000C` | `SetSpriteType`     | sprite, type (low 16 bits written to the sprite's type field)             |
| `0x0400001B` | `ScaleSpriteOnX`    | sprite, initial, target, current, duration, elapsed                         |
| `0x0400001C` | `ScaleSpriteOnY`    | sprite, initial, target, current, duration, elapsed                         |
| `0x0400001D` | `SetSpriteRolling`  | sprite, initial, target, current, duration, elapsed                         |
| `0x04000028` | `Display3DSprite`   | sprite, XYZ index, duration, elapsed                                        |
| `0x04000029` | `SetSpriteFrame`    | sprite, frame                                                               |

`0x0400000D` (Display3DSpriteOnPath) and `0x04000020` are registered but unsupported; any
unknown opcode pauses scenario execution with a full captured `ScriptPauseInfo`.

`SetSpriteType` (Runtime `Script_SetSpriteType`) writes the low 16 bits of argument 1 to the
runtime sprite's `type` field (Runtime +0x14; OpenNomad keeps `SpriteInstance::type`, default 0).
The write is performed even when the requested type is zero — it is never optimized away or
collapsed into an empty handler. Missing sprites, malformed argument counts and exhausted
commands follow the established error/pause paths.

### Timing model (30 Hz script frames)

Runtime durations and elapsed values are expressed in nominal 30 Hz script-frame units, not
seconds. The single conversion point is `convert_real_delta_to_script_frames` in
`Core/Script/ScriptRuntime.hpp`:

```
scriptDeltaFrames = clamp(realDeltaSeconds * 30.0, 0.0, 3.0)
```

The maximum normal script delta is therefore three script frames (0.1 s). `ScriptRuntime::tick`
takes real seconds and feeds script frames to the scheduler/handlers; `ScriptRuntime::step_tick`
takes script frames directly for deterministic debugger stepping (one nominal 30 Hz frame = 1.0).
The debugger displays the real delta, the effective script delta and whether the three-frame clamp
was applied. A duration of `60.0` is approximately two real-time seconds (60 updates at delta 1.0,
or 120 at delta 0.5).

### Startup activation (current inference)

`Scenario_LoadFromFile` parses scripts but does not visibly select a startup script inside the
DEAD0002 case; the true startup selection is a separate reverse-engineering checkpoint. The POC
therefore activates **every parsed script that owns a command group (`rootCommandCount > 0`), in
file order**, and logs the activation. This is deterministic (file order, not a hardcoded index)
and documented as an inference. The debugger additionally offers a clearly-labelled manual
activation override that is never used by the normal path.

### Scheduler group-completion rule (inference)

A script instance services its current group as the chain
`rootCommands[currentGroupIndex]` followed by the linked `nextCommandIndex` chain, one dispatch
per command per tick. Immediate commands (`SetSpriteType`, `SetSpriteFrame`) complete in the
tick; scale/roll/display commands stay active across ticks. Group completion is decided by an
explicit exhaustion predicate (`is_command_exhausted`: a finite execution limit whose count has
been reached; `0xFFFFFFFF` = unlimited never exhausts), evaluated over every command in the
chain — not from the root, the last command visited, or a single handler status. The rule is
centralized in `ScriptRuntime::advance_instance` and tested; stronger evidence would override it
in one place. The dispatch precheck (field-34-gated) and the group-completion predicate differ
only in that field-34 gate.

### Roll units compatibility decision

`SetSpriteRolling` stores degrees in the value pool. The recovered Runtime interpolates with
`currentDegrees * π/180` (converting to radians at the low-level setter boundary) but the
completion branch passes the raw target **without** the conversion — a confirmed asymmetry in the
machine code (0x004A2940). OpenNomad reproduces this faithfully: interpolation multiplies by
π/180 before `set_sprite_rotation` (which expects radians), completion passes the raw target
through. Pinned by `ScriptRuntimeTest`.

### XYZ pointer pool (deferred)

The XYZ-pointer pool is not parsed yet (it is not part of DEAD0002). `ModelScene::resolve_position`
falls back to the current model/3DO transform translation (the backdrop centre) for every index,
logging once. The out-of-range-pause safety behaviour exists behind this fallback and is tested at
the runtime level. Parsing the XYZ pool is a separate checkpoint.

## Startup sequence (explicit state machine)

OpenNomad reproduces Runtime.exe's startup order through an explicit state machine
(`Core/Startup/StartupCoordinator` + `StartupPhase`), a sequence-numbered trace
(`Core/Startup/StartupTraceRecorder`), and a scenario-mode dispatcher
(`Core/Scenario/ScenarioEngine`, modes 0/1/2/3). The recovered order is:

process bootstrap → window → core engine systems → startup videos
(`FLIS/EIDOS.mpg`, `FLIS/QUANTIC.mpg`, `FLIS/GAME.mpg`, decoded with ffmpeg and
presented through SDL3) → `aventure.SCX` permanent mode slot → splash prepare →
main loop → five-second splash → mode 3 (preliminary teardown, re-select aventure)
→ mode 2 (`IAM/START` → area 118 → `IAM/AREA` → `GRID.3DO`/`GRID.SCX` → area
context → queue event 1) → mode 1 (area script → opcode `0x46` → interface 29) →
native main menu.

Single-instance and command-line parsing are deliberately not implemented in this
milestone; the startup videos are optional presentation (a missing file records
`SkippedUnavailable`, never a startup failure).

## Deliberately omitted legacy behavior
| Original | Why omitted |
|---|---|
| `WM_PAINT` suppression (`ValidateRect`), `WM_ERASEBKGND` suppression | Win32 GDI painting; SDL/OpenGL swapchain has no equivalent |
| `WM_SETCURSOR` with a global Win32 cursor | SDL/ImGui manage cursors |
| Up to 16 registered `HWND`s receiving synchronous `WM_USER` | No subsystem notification registry exists; would be a meaningless array |
| Custom messages `0x3B9`, `0x400`, `0xC00` | Semantics unknown; do not invent custom SDL events |
| Child-window-specific focus (`WM_KILLFOCUS` on an embedded render window) | Single SDL window |
| Win32 structured-exception translation | C++ exceptions + `std::expected`; no SEH on other platforms |
| `SetCursorPos` edge-warping for relative mouse motion | SDL relative mouse mode |
| Movable/resizable flag + custom message toggling it | No fixed-window / mode-transition requirement yet |
| Six subsystem activation callbacks (`false`/`true` on de/activation) | OpenNomad has only mouse capture (gated via `should_enable_relative_mouse`); the audio stub has no pause API — future hooks route through `RuntimeActivityState` |
| Video playback pause/resume/skip on deactivation | Startup videos skip only on Escape while playing; no pause/resume-on-deactivation policy yet |
