# Runtime main loop, frame timing, and input lifecycle

> **Status:** work-in-progress reverse-engineering documentation for OpenNomad  
> **Last updated:** 2026-08-22
>
> This document describes the Windows retail Runtime's ordinary post-startup
> message loop, frame gating, input snapshot, timing calculation, gameplay
> pause, Escape handling, and the corresponding SDL3/OpenNomad mapping.
>
> It supersedes the main-loop, input and timing material that previously lived
> in the catch-all `docs/ReverseEngineering.md`.
>
> The most important recovered behavior is that Runtime is **idle-driven**:
>
> - Windows messages are drained first;
> - a frame runs only when the message queue is empty and all activity gates
>   permit updates;
> - a blocked Runtime calls `WaitMessage()` rather than polling/sleeping;
> - the first resumed frame re-baselines its clocks;
> - input is prepared before the engine callback;
> - the callback consumes the delta calculated by the **previous** frame;
> - the just-completed frame is measured afterward and produces the delta for
>   the next callback.

Related documentation:

- [`startup-sequence.md`](startup-sequence.md) — everything before the ordinary
  message loop, including startup movies and the splash/scenario transition;
- [`runtime-globals.md`](runtime-globals.md) — process-global addresses used by
  this loop;
- [`runtime-coordinate-math.md`](runtime-coordinate-math.md) — 30 Hz logical
  units used by other Runtime systems;
- [`original-toolchain.md`](original-toolchain.md) — Win32/DirectInput/MSVCRT
  environment;
- [`iam-scenario-vm.md`](iam-scenario-vm.md) — scenario execution driven from
  the engine-frame callback.

---

# 1. Evidence model

Source precedence:

1. **direct disassembly of the supplied retail `Runtime.exe`;**
2. **Win32 import/call behavior;**
3. **current OpenNomad implementation;**
4. older reverse-engineering notes.

Confidence labels:

- **Confirmed — Runtime:** directly established from machine code.
- **Confirmed — platform:** direct Win32 API semantics.
- **Corroborated:** independent evidence agrees.
- **Strongly reconstructed:** behavior is clear, original symbol name is not.
- **Provisional:** useful working interpretation requiring more tracing.
- **OpenNomad-only:** deliberate modern behavior.

Reference executable:

```text
SHA-256:
55f7120bfea7891b048c64e3682f3259cdbf2719a43fa24e42254b753c95d2ef

image base:
0x00400000
```

Primary Runtime anchors:

```text
0x00439310  ordinary Win32 message/main loop
0x0041F740  timed-frame / UpdateFrameTiming routine
0x004200F0  engine-frame callback invoked by timed-frame routine
0x0043E0D0  input polling/snapshot routine
0x00429BB0  game-command dispatcher used by held Escape
```

---

# 2. Scope: when this loop starts

The ordinary Runtime message loop is not the entire process lifetime.

Broad startup order:

```text
CRT / WinMain
    |
    v
window and engine initialization
    |
    v
startup movies
    |
    v
permanent adventure resources
    |
    v
splash preparation
    |
    v
ordinary message loop
    |
    +-- splash frames
    +-- scenario-mode transition
    +-- main menu
    +-- gameplay
```

OpenNomad likewise runs startup-video playback through a dedicated presentation
loop before entering `Application::run()`.

Do not conflate the video player's per-frame event pump with the ordinary game
main loop documented here.

---

# 3. Main-loop function — `0x00439310`

The function beginning at:

```text
0x00439310
```

owns the ordinary Win32 message loop.

The recovered high-level structure is:

```c
for (;;) {
    while (PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE)) {
        if (GetMessage(&msg, NULL, 0, 0) == 0)
            return;

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (renderWindowActive &&
        applicationActive &&
        !updatesSuspended) {

        serviceLegacyWindowNotifications();

        if (EscapeHeld && !gameplayPauseFlag)
            DispatchGameCommand(0x1F, -1, -1);

        UpdateFrameTiming(
            resetFrameTiming,
            skipEngineFrame);

        resetFrameTiming = 0;
    }
    else {
        resetFrameTiming = 1;
        WaitMessage();
    }
}
```

This pseudocode intentionally omits SEH/compiler scaffolding and unrelated
bookkeeping.

---

# 4. Message-queue strategy

Runtime does **not** run one game frame after every Windows message.

It first checks whether messages exist using a non-removing:

```text
PeekMessage(...)
```

and, while messages remain, consumes them with:

```text
GetMessage
TranslateMessage
DispatchMessage
```

Only after the queue is empty does it evaluate whether a game frame may run.

This is the defining idle-driven behavior.

---

# 5. Why use `PeekMessage` then `GetMessage`

Recovered sequence around:

```text
0x00439347..0x00439382
```

is equivalent to:

```text
PeekMessage(..., PM_NOREMOVE)

if message exists:
    GetMessage(...)
    TranslateMessage(...)
    DispatchMessage(...)
    repeat
```

`GetMessage()` returning:

```text
0
```

terminates the loop through the normal `WM_QUIT` path.

This is not a busy-spin around `PeekMessage(PM_REMOVE)`.

---

# 6. Frame activity gates

Once the queue is empty, three globals determine whether the frame path runs:

```text
0x004E7694  render-window active state
0x0052DD58  application active state
0x0052DD4C  engine update-suspension state
```

The exact machine-code condition is:

```text
renderWindowActive != 0
&& applicationActive != 0
&& updatesSuspended == 0
```

Only then does Runtime proceed to the held-Escape/timed-frame path.

---

# 7. `0x004E7694` — render-window active

The loop reads:

```text
WORD [0x004E7694]
```

and requires it to be nonzero.

This is the render target/window activation gate.

It is driven by focus/window messages rather than by gameplay pause.

Recommended working name:

```c
bool g_renderWindowActive;
```

The actual stored width is larger/legacy-specific; the semantic representation
may safely be Boolean.

---

# 8. `0x0052DD58` — application active

The loop separately requires:

```text
DWORD [0x0052DD58] != 0
```

This represents application foreground/activation state.

It is distinct from the render-window gate.

The original Win32 application can therefore represent states such as:

```text
render child/window condition
versus
process/application activation
```

independently.

---

# 9. `0x0052DD4C` — updates suspended

Third gate:

```text
DWORD [0x0052DD4C] == 0
```

This is an engine-controlled update suspension flag.

When nonzero, Runtime does not call the timed-frame routine at all.

This is **not** gameplay pause.

---

# 10. Three different pause/block concepts

Keep these mechanisms distinct:

```text
window/application inactivity
    ->
main loop does not run timed frame
WaitMessage()
next frame resets timing baseline

updatesSuspended
    ->
same main-loop blocking behavior
timed frame is not called

gameplay pause / 0x004E9728
    ->
timed frame still runs
input still runs
engine callback still runs according to normal ordering
newly calculated effective simulation delta is zeroed
```

Conflating these produces subtle timing/input bugs.

---

# 11. Blocked-loop behavior

If any of the three activity gates fails:

```text
0x004C5944 = 1
WaitMessage()
```

Then Runtime starts the message-drain loop again.

There is no busy polling while blocked.

This is important for CPU usage and for resume timing.

---

# 12. `0x004C5944` — reset timing on next frame

Working name:

```text
g_resetFrameTiming
```

Blocked path:

```text
g_resetFrameTiming = 1
WaitMessage()
```

Frame path:

```text
UpdateFrameTiming(
    g_resetFrameTiming,
    skipEngineFrame)

g_resetFrameTiming = 0
```

Crucially, message processing alone does not clear it.

It remains set until a frame actually executes.

---

# 13. Why the reset flag matters

Suppose the game loses focus for:

```text
30 seconds
```

Without special handling, the next frame would measure a 30-second frame and
generate a catastrophic simulation delta.

Instead, the first resumed timed frame sees:

```text
resetFrameTiming != 0
```

and re-baselines its clocks **immediately before input and the engine callback**.

The inactive interval never enters normal frame-time measurement.

---

# 14. Legacy synchronous window notification pass

Before Escape/timing, Runtime contains an optional pass gated by:

```text
BYTE [0x0091030A]
```

When enabled, it iterates:

```text
16 entries
```

using arrays around:

```text
0x0052D490
0x0052C938
```

For each active/non-null registered window it sends synchronously:

```text
message 0x0800
wParam  = 0
lParam  = 0
```

through `SendMessage`.

---

# 15. Correction to older notes: message `0x0800`

Older catch-all notes described this as:

```text
WM_USER
```

That is imprecise.

The literal sent message is:

```text
0x0800
```

while Win32 defines:

```text
WM_USER = 0x0400
```

Thus it is:

```text
WM_USER + 0x0400
```

within the application's user-defined message range, not literally `WM_USER`.

OpenNomad currently has no meaningful HWND-based subsystem-notification
equivalent and intentionally omits this fixed 16-window registry.

---

# 16. Held Escape check

Runtime performs a per-frame held-key test:

```text
GetAsyncKeyState(VK_ESCAPE)
```

with:

```text
VK_ESCAPE = 0x1B
```

It tests the high bit of the returned state.

Therefore Escape is handled as:

```text
held this frame
```

not:

```text
WM_KEYDOWN edge event
```

---

# 17. Escape command

When Escape is held and the pause/interaction guard allows it, Runtime calls:

```text
0x00429BB0
```

with:

```text
command = 0x1F
arg1    = -1
arg2    = -1
```

Working representation:

```text
DispatchGameCommand(0x1F, -1, -1)
```

The exact semantic name of command `0x1F` is not yet recovered.

---

# 18. `0x004E9728` and Escape

Escape is suppressed when:

```text
DWORD [0x004E9728] != 0
```

The same global is later read by the timed-frame routine and suppresses the
effective simulation delta.

Thus this global genuinely behaves as a:

```text
gameplay pause / interaction pause state
```

even though the original source-level variable name remains unknown.

A safe wording is:

> `0x004E9728` is a gameplay-pause-like state that both blocks held-Escape
> command dispatch and zeroes the effective frame delta.

---

# 19. Timed-frame routine — `0x0041F740`

Recovered conceptual signature:

```c
void RunTimedOmikronFrame(
    bool resetFrameClock,
    bool skipEngineFrame);
```

At the main-loop callsite:

```text
argument 0 =
    DWORD [0x004C5944]

argument 1 =
    WORD [0x0090EF2E]
```

The routine orchestrates:

```text
clock reset
input
engine callback
external-clock bookkeeping
frame measurement
FPS
next-frame delta
overrides
pause
```

---

# 20. Timed-frame exact high-level order

Recovered order:

```text
1. Optional timing baseline reset.

2. Poll/snapshot input.

3. Calculate per-frame pressed input state.

4. Clear one per-frame input-consumption field.

5. Unless skipEngineFrame:
       call 0x004200F0.

6. Update external-clock state.

7. Measure the frame that just completed.

8. Update moving average and FPS.

9. Calculate simulation delta for NEXT callback.

10. Apply external-clock correction.

11. Apply forced delta override.

12. Copy base/effective delta fields.

13. Advance optional timed accumulator.

14. If gameplay pause is active:
       zero effective delta.
```

The engine callback occurring before steps 7–14 is the critical detail.

---

# 21. Optional clock reset

When argument 0 is nonzero, Runtime calls its millisecond clock helper around:

```text
0x004120F0
```

then writes the returned time to:

```text
0x004E96FC
0x0090E158
0x009103D0
```

These are timing baselines/current-clock copies used by the frame machinery.

The existing moving average and simulation delta are not discarded.

---

# 22. Clock unit

The recovered time values behave as:

```text
milliseconds
```

and FPS is calculated from:

```text
1000.0 / milliseconds
```

The OpenNomad equivalent uses:

```text
SDL_GetTicks()
```

which provides a monotonic millisecond clock suitable for the same behavior.

---

# 23. Input call occurs before the engine callback

`0x0041F740` invokes:

```text
0x0043E0D0
```

before the game engine callback.

The input routine fills the current input bitfield used immediately afterward
to derive pressed edges.

Therefore engine/gameplay code sees this frame's freshly prepared input.

---

# 24. Input bitfield globals

Recovered core bitfields:

```text
0x004E9718  current input state
0x004E971C  per-frame pressed input
0x004E9720  input edge mask
0x004E9724  previous input state
```

These are separate from the lower-level 256-entry raw DirectInput keyboard
state used by the input subsystem.

---

# 25. Pressed-input formula

After polling:

```text
current = [0x004E9718]
previous = [0x004E9724]
edgeMask = [0x004E9720]
```

Runtime computes:

```text
pressed =
    current
    & ~(previous & edgeMask)
```

then stores:

```text
previous = current
```

Machine-code algebra is:

```text
((previous & edgeMask) ^ current) & current
```

which is equivalent.

---

# 26. Meaning of the edge mask

For one action bit:

```text
edgeMask bit = 1
    ->
pressed only on rising edge

edgeMask bit = 0
    ->
pressed remains set whenever current is held
```

This lets the same compact action bitfield mix:

```text
edge-triggered actions
held/repeating actions
```

without maintaining separate representations.

---

# 27. Current input versus raw keyboard state

Do not confuse:

```text
DirectInput 256-byte key state
```

with:

```text
0x004E9718 compact game-action bitfield
```

The lower-level input subsystem maps physical input into the compact action
state consumed by the engine.

The exact retail Omikron key-to-action mapping is not fully reconstructed yet.

OpenNomad must not invent one and then call it original.

---

# 28. Per-frame input clear — `0x0090E0E0`

Immediately before the engine callback Runtime writes:

```text
DWORD [0x0090E0E0] = 0
```

Its exact consumer remains unresolved.

It is clearly a per-frame field because it is reset on every timed-frame path.

Recommended neutral name:

```text
g_perFrameInput
```

until all readers are mapped.

---

# 29. `skipEngineFrame` — `0x0090EF2E`

Second argument to `0x0041F740` comes from:

```text
WORD [0x0090EF2E]
```

Behavior:

```text
if zero:
    call 0x004200F0

if nonzero:
    skip only 0x004200F0
```

Input and frame-timing bookkeeping still happen.

Thus:

```text
skipEngineFrame
```

is a much better description than:

```text
pause
```

or:

```text
suspend updates
```

---

# 30. Engine-frame callback — `0x004200F0`

The detailed internals of:

```text
0x004200F0
```

are not yet completely decomposed into final subsystem names.

It is the main engine update/render boundary invoked once per permitted timed
frame.

OpenNomad maps this position conceptually to:

```text
Application::run_engine_frame()
```

which advances scenario/interface/scene state and renders the current frame.

This mapping remains architectural rather than a claim that one C++ function
exactly corresponds instruction-for-instruction.

---

# 31. One-frame delta latency

At callback time Runtime has **not yet measured the frame currently executing**.

Therefore:

```text
callback N
    consumes delta calculated after callback N-1
```

Then Runtime measures the elapsed time of frame N and calculates:

```text
delta for callback N+1
```

This one-frame latency is direct executable behavior.

---

# 32. Why this ordering matters

A tempting modern rewrite is:

```text
measure dt
update(dt)
render()
```

Runtime does:

```text
update/render(using previous dt)
measure completed frame
calculate next dt
```

For most steady-state frames the distinction is small.

It becomes observable around:

- pause transitions;
- forced delta changes;
- focus resume;
- frame-rate mode changes;
- external-clock state changes.

---

# 33. Frame-time measurement globals

Recovered timing fields include:

```text
0x004E96FC
    frame-start/baseline millisecond clock

0x004E9700
    moving-average frame time in milliseconds

0x004E9714
    just-completed frame time in milliseconds

0x0090E170
    average FPS

0x0090E174
    instantaneous FPS
```

The names are reconstructed from the arithmetic.

---

# 34. Frame-time calculation

After the callback:

```text
now =
    GetTimeMs()

frameTime =
    now - frameStart

averageFrameTime =
    (oldAverageFrameTime + frameTime) >> 1
```

Then zero values are protected:

```text
if averageFrameTime == 0:
    averageFrameTime = 1

if frameTime == 0:
    frameTime = 1
```

This prevents divide-by-zero FPS values.

---

# 35. FPS calculation

Runtime uses constant:

```text
1000.0
```

and calculates:

```text
instantaneousFPS =
    1000.0 / frameTimeMs

averageFPS =
    1000.0 / averageFrameTimeMs
```

The moving average is therefore a very simple exponentially weighted filter:

```text
newAverageMs =
    (oldAverageMs + newMs) / 2
```

with integer truncation.

---

# 36. Timing-mode selector — `0x004E972C`

Runtime reads:

```text
signed WORD [0x004E972C]
```

and dispatches modes:

```text
0..4
```

through a jump table.

Recovered modes:

```text
0 dynamic
1 fixed 30 Hz
2 fixed 60 Hz
3 fixed 300 Hz
4 fixed 15 Hz
```

The labels describe the resulting simulation delta, not necessarily original UI
names.

---

# 37. Omikron delta units

Simulation delta is not stored directly in seconds.

The convention is:

```text
1.0 =
    one nominal 30 Hz simulation tick
    =
    1/30 second
```

Thus:

```text
seconds =
    delta / 30.0
```

for modern APIs expecting seconds.

---

# 38. Dynamic delta

Mode 0 computes approximately:

```text
delta =
    30.0 / averageFPS
```

then clamps:

```text
delta <= 3.0
```

So maximum normal dynamic simulation step is:

```text
3.0 ticks
=
0.1 seconds
```

This prevents very slow frames from producing arbitrarily huge normal
simulation steps.

---

# 39. Fixed timing modes

Recovered direct assignments:

```text
mode 1:
    delta = 1.0

mode 2:
    delta = 0.5

mode 3:
    delta = 0.1

mode 4:
    delta = 2.0
```

Equivalent nominal rates:

```text
1.0 -> 30 Hz
0.5 -> 60 Hz
0.1 -> 300 Hz
2.0 -> 15 Hz
```

These values replace the measured dynamic delta; they are not multipliers.

---

# 40. Out-of-range timing mode

The original switch has meaningful direct cases only for:

```text
0..4
```

The best modern fallback is:

```text
dynamic calculation
```

rather than inventing a new fixed mode.

OpenNomad uses this behavior.

---

# 41. External-clock subsystem

Runtime tracks:

```text
0x004E9730  previous external-clock state
0x004E9734  current external-clock state
0x004E9704  baseline/accumulated float
```

using helpers around:

```text
0x0042CC10
0x0042BC30
```

A:

```text
0 -> 1
```

state transition captures a baseline.

While both previous/current states equal 1, Runtime applies an additional
clock-derived correction to the calculated delta.

---

# 42. External-clock correction remains unresolved

The exact subsystem identity behind:

```text
0x0042CC10
0x0042BC30
```

is not fully named.

Therefore OpenNomad currently implements the ordinary:

```text
external clock inactive
```

path and deliberately omits this correction.

Do not invent a media-clock/network-clock subsystem simply to fill the gap.

---

# 43. Forced delta — `0x004C30E8`

Runtime compares:

```text
float [0x004C30E8]
```

against:

```text
-1.0
```

If it is not the sentinel:

```text
delta =
    [0x004C30E8]
```

This is a direct override of the calculated delta.

Recommended:

```c
float g_forcedDeltaTime;
```

with original sentinel:

```text
-1.0 = disabled
```

OpenNomad represents this more safely as:

```text
std::optional<float>
```

---

# 44. Delta globals after calculation

Runtime writes the calculated/unpaused value into several globals.

The most important behavior is:

```text
0x004C30D8
    primary effective/game delta

0x004C30DC
    second effective consumer copy

0x004C30E0
    unpaused/base copy
```

before applying gameplay pause.

Exact consumer-level distinctions between `D8` and `DC` remain incomplete.

---

# 45. Gameplay pause — `0x004E9728`

At the end of `0x0041F740`:

```text
if [0x004E9728] != 0:
    [0x004C30D8] = 0.0
    [0x004C30DC] = 0.0
```

It does **not** zero:

```text
0x004C30E0
```

in that branch.

This is why OpenNomad models:

```text
base_delta
effective_delta
```

separately.

---

# 46. Pause ordering

Because pause is applied only after the current frame's engine callback:

```text
callback N
    sees effective delta produced by previous frame

then frame N calculation sees pause state
    ->
writes effective delta 0

callback N+1
    sees zero
```

Thus a newly asserted pause has the same one-frame timing latency as other delta
changes.

This is direct consequence of the Runtime order, not an OpenNomad invention.

---

# 47. Optional timed accumulator — `0x004C30E4`

Runtime compares:

```text
float [0x004C30E4]
```

against:

```text
-1.0
```

When enabled, it adds the frame delta.

If it exceeds approximately:

```text
10000.0
```

Runtime resets it to:

```text
-1.0
```

The consumer/purpose of this accumulator remains unresolved.

OpenNomad intentionally does not invent a public field or behavior for it.

---

# 48. Key constants in `0x0041F740`

Recovered floating constants:

```text
30.0
1000.0
0.0
3.0
-1.0
10000.0
```

Their uses correspond to:

```text
30 Hz delta convention
milliseconds-per-second FPS conversion
zero comparisons
dynamic delta cap
disabled override/accumulator sentinel
accumulator cutoff
```

---

# 49. Low-level input source

The input poller around:

```text
0x0043E0D0
```

contains DirectInput device acquisition/state reads.

The executable imports:

```text
DINPUT.dll!DirectInputCreateA
```

and uses 256-byte keyboard state buffers.

Device-lost/reacquire behavior is visible in the input path.

The compact game-action bitfield is produced after this lower-level device
state processing.

---

# 50. OpenNomad input mapping

OpenNomad replaces the original DirectInput device layer with SDL3.

Current layering:

```text
SDL events/device state
    |
    v
HeldInputState / RawInputState
    |
    v
InputManager + ControlScheme
    |
    v
game action state
```

This is a platform modernization.

The original physical-key mapping should remain a separate RE target.

---

# 51. Win32 window-event semantics

Recovered Runtime window procedure handles separate activation/focus concepts.

Useful high-level mapping:

| Runtime Win32 event | Original state effect |
|---|---|
| `WM_ACTIVATE: WA_INACTIVE` | render-window inactive; application inactive |
| `WM_ACTIVATE: WA_ACTIVE/WA_CLICKACTIVE` | both active |
| `WM_SETFOCUS` | render-window active |
| `WM_KILLFOCUS` | render-window/focus handling |
| `WM_ACTIVATEAPP` | application-active flag follows `wParam` |
| `WM_CHILDACTIVATE` | application activated |
| `WM_KEYDOWN/UP` | low-level keyboard state |
| mouse-button messages | mouse held state |
| `WM_CHAR` | optional one-byte text input |
| `WM_MOVE` | window position state |
| `WM_SIZE` | window-size/minimize state |

Exact message handler addresses belong in a future window/input-specific audit
if needed.

---

# 52. SDL collapsed activation mapping

Desktop SDL does not expose the exact same parent/child Win32 activation model.

Current OpenNomad therefore collapses several original events.

On:

```text
SDL_EVENT_WINDOW_FOCUS_GAINED
```

it approximately performs:

```text
render_window_active =
    !window.minimized

application_active =
    true
```

On:

```text
SDL_EVENT_WINDOW_FOCUS_LOST
```

it clears both.

This most closely matches Runtime's `WM_ACTIVATE` behavior.

---

# 53. Focus loss and stuck inputs

SDL/desktop window systems may not deliver release events for keys/buttons
released while the application is unfocused.

OpenNomad therefore clears/reconciles held input on focus loss.

This is:

```text
OpenNomad platform-safety behavior
```

rather than a recovered Runtime field mutation.

It prevents stale held keys from surviving an Alt-Tab/focus transition.

---

# 54. Minimize/restore

A minimized OpenNomad window closes the render-window activity gate.

Restore/show events do not blindly mark it active.

The gate is reopened only when keyboard focus is actually present.

This is necessary for correct SDL behavior and especially for Wayland.

---

# 55. Wayland first-present accommodation

On Wayland, a newly created surface may not receive input focus until it has
been mapped/presented.

If OpenNomad initialized:

```text
render_window_active = false
```

and refused to draw until focus arrived, it could deadlock:

```text
no frame
    ->
no present
    ->
surface not mapped
    ->
no focus
    ->
no frame
```

Therefore OpenNomad initially sets both activity gates optimistically true.

Real focus/application events correct them immediately afterward.

This is **OpenNomad-only platform accommodation**, not Runtime behavior.

---

# 56. SDL background/foreground events

Desktop SDL normally has no separate process-background concept equivalent to
Win32's exact `WM_ACTIVATEAPP` path.

OpenNomad still routes SDL's application foreground/background events where the
platform provides them, keeping the second activity gate meaningful on mobile
and other platforms.

On ordinary desktop operation, focus events effectively keep both gates in sync.

---

# 57. Waiting on SDL

Original blocked path:

```text
WaitMessage()
```

OpenNomad equivalent:

```text
SDL_WaitEvent(...)
```

Both mean:

```text
yield/block until platform event
```

instead of:

```text
sleep N ms
busy spin
continue rendering unfocused
```

This is a semantic mapping rather than an API emulation.

---

# 58. MainLoopController

OpenNomad isolates the pure gate decision in:

```text
Core/MainLoopController.hpp
```

Decision:

```text
!running
    -> exit

may_run_frame()
    -> run frame

otherwise
    -> set reset flag
    -> wait for event
```

Keeping this logic free of SDL calls makes Runtime sequencing unit-testable.

---

# 59. RuntimeActivityState

OpenNomad explicitly models:

```text
render_window_active
application_active
updates_suspended
reset_frame_timing_on_next_update
```

This mirrors the four key Runtime globals rather than collapsing everything
into one:

```text
paused
```

flag.

That separation should be preserved.

---

# 60. `may_run_frame()`

OpenNomad condition:

```cpp
render_window_active
&& application_active
&& !updates_suspended
```

is a direct semantic translation of the Runtime gate at:

```text
0x00439384..0x004393A3
```

This portion is Runtime-exact at the logical level.

---

# 61. Reset flag lifecycle in OpenNomad

While blocked:

```text
reset_frame_timing_on_next_update = true
```

On first run-frame decision:

```text
decision.reset_frame_timing = true
```

After the frame actually completes:

```text
clear_reset_flag_after_frame()
```

This preserves the important original property:

```text
events processed while blocked do not themselves clear the reset
```

---

# 62. OpenNomad timed-frame implementation

`FrameTiming::run_timed_frame()` preserves the recovered order:

```text
optional reset
input callback
optional engine callback
measure elapsed frame
moving average
FPS
calculate next delta
forced override
base/effective split
```

External-clock correction remains deliberately deferred.

---

# 63. OpenNomad time conversion

`FrameTimingState` stores:

```text
Omikron delta units
```

rather than seconds.

Conversion to seconds occurs once at the engine/scene API boundary:

```text
deltaSeconds =
    effectiveDelta / 30.0
```

This prevents accidental double conversion.

---

# 64. OpenNomad fixed 60 Hz accumulator

Current `Application::run_engine_frame()` contains a placeholder:

```text
60 Hz fixed timestep accumulator
```

for future physics/game logic.

At present it contains no recovered Runtime simulation workload.

Therefore:

```text
the 60 Hz accumulator is OpenNomad-only scaffolding
```

and must not be cited as original Omikron timing behavior.

Runtime's recovered global simulation delta remains 30-Hz-unit based.

---

# 65. Gameplay pause in OpenNomad

OpenNomad models the `0x004E9728` effect as:

```text
FrameTimingState::gameplay_paused
```

When active:

```text
base_delta remains unpaused
effective_delta becomes 0
```

The engine callback is not removed from the ordinary frame loop merely because
gameplay is paused.

This is an important distinction from:

```text
updates_suspended
```

which prevents the timed-frame path entirely.

---

# 66. Held Escape in OpenNomad

`MainLoopController::should_dispatch_escape()` uses:

```text
escape held
&& !gameplay_paused
```

matching the original per-frame `GetAsyncKeyState` test.

OpenNomad's actual game-command `0x1F` layer is not fully represented yet, so
the dispatch remains an integration point until that command is recovered.

---

# 67. F12 debug mouse release is not Runtime Escape behavior

Older OpenNomad behavior used Escape for debug mouse capture.

This conflicted with the original held-Escape command.

Current policy:

```text
Escape
    reserved for game-visible Runtime behavior

F12
    debug mouse-capture toggle
```

This prevents debug tooling from consuming an original game control.

---

# 68. Text input

Original Runtime conditionally captures:

```text
WM_CHAR
```

into a narrow/signed-byte character field.

This matches the wider ANSI Win32 architecture documented in
`original-toolchain.md`.

OpenNomad instead stores:

```text
SDL UTF-8 text input
```

through `TextInputState`.

That is a deliberate modernization.

---

# 69. SDL text-input ownership

SDL text input can also be coordinated by ImGui/backend code.

Future in-game text-entry interfaces need to coordinate:

```text
SDL_StartTextInput
SDL_StopTextInput
```

with debug UI ownership rather than assuming every `SDL_EVENT_TEXT_INPUT`
belongs to the game.

This is an OpenNomad integration concern, not original Runtime behavior.

---

# 70. Relative mouse mode

Original Runtime contains Win32/DirectInput mouse-capture and edge-warping
behavior.

OpenNomad uses:

```text
SDL relative mouse mode
```

while frames are allowed and the user has requested capture.

Relative mode is released when the application becomes inactive so the desktop
cursor remains usable.

This is a modern platform mapping.

---

# 71. Why inactivity is not gameplay pause

When inactive Runtime:

```text
does not call 0x0041F740
```

so:

```text
no input snapshot
no 0x004200F0 engine callback
no ordinary frame measurement
```

When gameplay-paused:

```text
0x0041F740 still runs
0x004200F0 still occurs in normal ordering
effective next-frame delta becomes zero
```

This is one of the highest-value distinctions in the entire main-loop RE.

---

# 72. Why `skipEngineFrame` is not update suspension

With:

```text
skipEngineFrame != 0
```

Runtime still:

```text
polls input
updates pressed-state history
clears per-frame input field
updates external-clock bookkeeping
measures frame time
calculates next delta
applies pause/overrides
```

It merely skips:

```text
0x004200F0
```

This differs from `updatesSuspended`, which keeps the timed-frame function from
being called at all.

---

# 73. Summary matrix

| State | Message pump | Input poll | Engine callback | Timing calc | Blocks in wait |
|---|---:|---:|---:|---:|---:|
| normal | yes | yes | yes | yes | no |
| gameplay pause | yes | yes | yes | yes, effective delta -> 0 | no |
| `skipEngineFrame` | yes | yes | **no** | yes | no |
| updates suspended | yes | no | no | no | yes |
| window inactive | yes | no | no | no | yes |
| app inactive | yes | no | no | no | yes |

This table is the easiest way to avoid collapsing distinct Runtime states.

---

# 74. Frame lifecycle diagram

```text
WINDOW MESSAGE QUEUE
        |
        | messages present
        v
GetMessage / Translate / Dispatch
        |
        +----------------------------+
                                     |
                                     v
                              queue now empty
                                     |
                                     v
                       activity gates all pass?
                         /                    \
                       no                      yes
                       |                        |
                       v                        v
             resetFrameTiming = 1      optional legacy HWND notify
             WaitMessage()                     |
                       |                        v
                       +--------------> held Escape check
                                                |
                                                v
                                      RunTimedOmikronFrame
                                                |
                           +--------------------+--------------------+
                           |                    |                    |
                           v                    v                    v
                        input            engine callback         measure
                                         unless skipped          frame
                                                                    |
                                                                    v
                                                               next delta
                                                                    |
                                                                    v
                                                            forced/pause
                                                                    |
                                                                    v
                                                        clear reset flag
                                                                    |
                                                                    v
                                                               next loop
```

---

# 75. Timing lifecycle diagram

```text
delta(N-1)
    |
    v
engine callback N
    |
    v
frame N completes
    |
    v
elapsed(N)
    |
    v
moving average / FPS
    |
    v
calculate delta(N)
    |
    +-- time-scale mode
    +-- external clock
    +-- forced override
    +-- gameplay pause
    |
    v
stored for next callback
    |
    v
engine callback N+1
```

---

# 76. Input lifecycle diagram

```text
DirectInput / platform devices
        |
        v
0x0043E0D0 input poll
        |
        v
current action bitfield
0x004E9718
        |
        +-----------------------------+
        |                             |
        v                             v
previous state                edge-mask state
0x004E9724                   0x004E9720
        |                             |
        +--------------+--------------+
                       |
                       v
pressed =
current & ~(previous & edgeMask)
                       |
                       v
0x004E971C
                       |
                       v
previous = current
                       |
                       v
engine callback
```

---

# 77. Recommended Ghidra labels

High-confidence working labels:

```text
0x00439310
    Runtime_MainLoop

0x0041F740
    Runtime_RunTimedFrame
    or
    Runtime_UpdateFrameTiming

0x004200F0
    Runtime_EngineFrame
    provisional internal role

0x0043E0D0
    Input_PollAndBuildActionState

0x00429BB0
    DispatchGameCommand
```

Globals:

```text
0x004E7694  g_renderWindowActive
0x0052DD58  g_applicationActive
0x0052DD4C  g_updatesSuspended
0x004C5944  g_resetFrameTiming

0x0090EF2E  g_skipEngineFrame

0x004E9718  g_inputState
0x004E971C  g_pressedInput
0x004E9720  g_inputEdgeMask
0x004E9724  g_previousInputState
0x004E9728  g_gameplayPauseState
0x004E972C  g_timeScaleMode

0x004C30D8  g_gameDelta
0x004C30DC  g_gameDeltaConsumerCopy
0x004C30E0  g_baseGameDelta
0x004C30E4  g_timedAccumulator
0x004C30E8  g_forcedDelta
```

Names remain reconstructed unless an original symbol is found.

---

# 78. Recommended OpenNomad architecture

Keep the current separation:

```text
Application
    owns platform event pump

RuntimeActivityState
    owns frame gating

MainLoopController
    pure main-loop decision logic

InputManager
    platform input -> game actions

FrameTimingState
    Runtime-compatible delta/timing state

run_timed_frame()
    recovered operation order

run_engine_frame()
    current engine/scenario/interface/scene boundary
```

Do not merge these back into one giant SDL loop merely because the original
Runtime used globals.

---

# 79. Recommended tests — main loop

- [ ] queued events are drained before a frame;
- [ ] no frame runs while any activity gate fails;
- [ ] blocked path sets timing reset;
- [ ] event processing alone does not clear timing reset;
- [ ] first resumed frame consumes the reset;
- [ ] blocked path waits rather than busy-spins;
- [ ] quit exits without running another frame;
- [ ] minimized/focus-lost state does not advance engine frame.

---

# 80. Recommended tests — timing

- [ ] reset re-baselines all recovered clock copies;
- [ ] moving average survives reset;
- [ ] input occurs before engine callback;
- [ ] callback uses prior effective delta;
- [ ] frame measurement occurs after callback;
- [ ] frame time zero clamps to 1 ms;
- [ ] average time zero clamps to 1 ms;
- [ ] FPS uses 1000/ms;
- [ ] average uses `(old + new) / 2`;
- [ ] dynamic delta is `30 / averageFPS`;
- [ ] dynamic delta caps at `3.0`;
- [ ] fixed modes produce `1.0/0.5/0.1/2.0`;
- [ ] forced delta replaces calculated delta;
- [ ] gameplay pause leaves base delta but zeroes effective delta;
- [ ] skip-engine-frame still polls input and calculates timing.

---

# 81. Recommended tests — input

- [ ] edge-mask bit 1 produces rising-edge-only `pressed`;
- [ ] edge-mask bit 0 produces held `pressed`;
- [ ] previous input updates after calculation;
- [ ] per-frame input field is cleared before engine callback;
- [ ] held Escape is checked per frame, not only on key-down;
- [ ] gameplay pause suppresses Escape game-command dispatch;
- [ ] focus loss clears/reconciles modern held input to avoid stuck controls.

---

# 82. Recommended tests — platform mapping

- [ ] focus lost closes both desktop activity gates;
- [ ] focus gained opens application gate and non-minimized render gate;
- [ ] minimized window keeps render gate closed;
- [ ] restore without focus does not open render gate;
- [ ] first Wayland frame is not deadlocked by a false initial focus flag;
- [ ] relative mouse mode is disabled while inactive;
- [ ] SDL wait path wakes and returns to normal event draining.

---

# 83. Deliberately omitted original Win32 behavior

OpenNomad does not need literal equivalents for every Windows shell detail.

Known omissions include:

```text
WM_PAINT ValidateRect suppression

WM_ERASEBKGND suppression

global Win32 cursor handling

child-window-specific focus rules

16-HWND synchronous 0x0800 notification registry

other custom messages whose semantics are not recovered

SetCursorPos edge warping
    replaced by SDL relative mouse mode

Win32 structured-exception translation
    not a portable engine semantic
```

These are documented so absence is intentional rather than accidental.

---

# 84. Legacy custom messages

Runtime also uses other custom/native window messages in surrounding startup and
window code.

Examples previously observed include values such as:

```text
0x03B9
0x0400
0x0C00
```

Their semantics are not all recovered.

Do not create arbitrary SDL user events merely to mirror numeric message values.

Only reproduce them when a real game-visible subsystem dependency is found.

---

# 85. Subsystem activation callbacks

Original Runtime contains activation/deactivation notification behavior for
multiple subsystems when focus/application state changes.

The exact complete callback registry remains incompletely mapped.

OpenNomad currently routes the immediately meaningful modern consequences:

```text
frame gating
mouse capture
input reconciliation
```

Other hooks should be added only when the owning subsystem is implemented and
its original behavior is known.

---

# 86. Audio on inactivity

The old Runtime has subsystem activation behavior that may include audio/movie
state.

Current OpenNomad does not yet claim exact original pause/resume behavior for
all audio/video playback on deactivation.

This belongs to future targeted audio/FMV lifecycle tracing.

Do not infer it from the main frame gate alone.

---

# 87. Startup videos and input

OpenNomad startup-video playback pumps events and updates the normal input
manager so:

```text
Escape skip
quit
debug mouse release
```

work before the ordinary main loop begins.

This is a modern integration strategy.

The ordinary `0x00439310` loop should remain documented independently.

---

# 88. Main loop and scenario 30 Hz are related but distinct

Runtime uses 30 Hz as a broad logical time basis.

However:

```text
main loop != fixed 30 Hz scheduler
```

The ordinary frame loop runs as quickly as rendering/message conditions permit.

Dynamic delta scales simulation in nominal 30 Hz units:

```text
delta = 30 / measured average FPS
```

Thus a 60 FPS renderer can produce approximately:

```text
0.5 simulation units per rendered frame
```

while scenario/script systems still interpret one unit as one nominal 30 Hz
tick.

Retail's ordinary actor dispatcher likewise performs one variable-delta
physical-to-spatial sequence per invocation. OpenNomad separately maps ordinary
actor behavior onto a nominal 30 Hz accumulator; Phase 4.2D.1 completes each
due OpenNomad actor step through a fresh post-physical spatial sample. This is
an OpenNomad scheduling choice, not evidence of a retail catch-up loop.

---

# 89. Why interpolation belongs above simulation timing

For systems such as the modernized main-menu bump effect:

```text
authored/logical state can remain 30 Hz
```

while:

```text
presentation can interpolate at display refresh
```

This is consistent with the Runtime timing model.

Do not “fix” the main loop by converting all authored 30 Hz logic to arbitrary
display-Hz stepping.

---

# 90. Frame timing versus script timing

Three units appear across OpenNomad:

```text
milliseconds
    Runtime frame clocks

Omikron delta units
    1.0 = 1/30 second

seconds
    modern scene/backend API boundary
```

Conversions should occur only at explicit boundaries.

Avoid storing mixed units in one variable.

---

# 91. Current known implementation deviations

Intentional/current deviations include:

```text
SDL instead of Win32 message dispatch

no legacy 16-HWND synchronous notification registry

Wayland optimistic initial activation

UTF-8 text input instead of signed-byte WM_CHAR

SDL relative mouse instead of cursor edge warping

safe held-input reconciliation on focus loss

external-clock correction deferred

60 Hz fixed-step accumulator exists only as empty OpenNomad scaffolding

game command 0x1F not yet implemented as a full Runtime command layer
```

These should remain visible in documentation/tests.

---

# 92. Highest-value remaining RE

## 92.1 `0x004200F0`

Decompose the engine-frame callback completely:

```text
scenario order
render order
audio update
interface update
world update
```

## 92.2 external-clock subsystem

Resolve:

```text
0x0042CC10
0x0042BC30
0x004E9730
0x004E9734
0x004E9704
```

## 92.3 input action mapping

Recover:

```text
physical DirectInput keys/buttons
    ->
compact action bits
```

and the initial value of:

```text
g_inputEdgeMask
```

## 92.4 `0x0090E0E0`

Identify the per-frame-cleared input field's consumers.

## 92.5 game command `0x1F`

Recover the exact Escape-visible action.

## 92.6 activity subsystem callbacks

Map every focus/background callback and determine exact audio/movie/input side
effects.

---

# 93. Compact Runtime reference

Main loop:

```text
00439310
```

Frame gates:

```text
004E7694  render window active
0052DD58  application active
0052DD4C  updates suspended
004C5944  reset timing next frame
```

Frame call:

```text
0041F740(resetTiming, skipEngineFrame)
```

Input:

```text
0043E0D0
004E9718 current
004E971C pressed
004E9720 edge mask
004E9724 previous
```

Pause/timing:

```text
004E9728 gameplay-pause-like state
004E972C time-scale mode

004C30D8 effective delta
004C30DC second effective copy
004C30E0 base/unpaused delta
004C30E4 optional accumulator
004C30E8 forced delta
```

Escape:

```text
GetAsyncKeyState(VK_ESCAPE)
if held && ![004E9728]:
    00429BB0(0x1F, -1, -1)
```

---

# 94. Compact behavioral reference

```text
messages?
    yes -> drain all
    no  -> evaluate activity gates

gates fail?
    resetTiming = 1
    WaitMessage

gates pass?
    optional legacy notifications
    held Escape test
    RunTimedFrame
    resetTiming = 0
```

Timed frame:

```text
optional clock reset
input
pressed-state update
clear per-frame input
optional engine callback
measure completed frame
average/FPS
calculate next delta
external-clock correction
forced override
copy base/effective
optional accumulator
gameplay pause -> effective zero
```

---

# 95. Boundary of current knowledge

Strongly recovered:

```text
ordinary message-loop structure
message draining before frames
three activity gates
WaitMessage blocked path
timing-reset lifecycle
held Escape path
input current/previous/edge/pressed formula
skip-engine-frame semantics
frame measurement and average
FPS calculation
dynamic and fixed delta modes
forced-delta override
gameplay pause effective-delta suppression
one-frame delta latency
```

Still incomplete:

```text
complete 0x004200F0 subsystem order
external-clock subsystem identity
per-frame input scratch field meaning
retail physical-input mapping
game command 0x1F meaning
complete activity/deactivation callbacks
some consumer distinctions between delta copies
```

The central architectural rule is:

> Runtime's ordinary loop is not a conventional modern
> `poll -> dt -> update(dt) -> render` loop. It is an idle-driven Win32 loop
> that drains messages, blocks completely while inactive, prepares input,
> executes the engine using the **previously calculated** simulation delta,
> and only afterward measures the completed frame to calculate the next one.
