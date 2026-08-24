# `Runtime.exe` startup sequence

> **Status:** work-in-progress reverse-engineering documentation for OpenNomad.
>
> This document describes the startup sequence of the Windows retail `Runtime.exe` for *Omikron: The Nomad Soul*, from process entry through the point where the ordinary frame/scenario/interface machinery has taken ownership and the main menu is running.
>
> The emphasis is on **ordering**. A reimplementation can contain all of the right systems and still behave incorrectly if they are initialized, activated, or allowed to run in a different order from the original executable.

## Source precedence

The sources used here are, in descending order of authority:

1. **`Runtime.exe` control flow and data accesses** — authoritative for executable behavior, call ordering, global state and resource use.
2. **Retail game data** — especially `aventure.SCX`, `Grid.SCX`, `IAM/START`, and `IAM/AREA` where their structures have been correlated with Runtime.
3. **Observed retail behavior** — useful for matching invisible initialization to the visible movies, splash screen, and menu.
4. **OpenNomad implementation experiments** — corroborating evidence only; they do not override Runtime.

Where an older importer, an OpenNomad implementation detail, or a previous hypothesis conflicts with Runtime, Runtime wins.

## Confidence labels

This document uses the following labels:

- **Confirmed — Runtime:** directly established from `Runtime.exe` code/data accesses.
- **Confirmed — data:** directly established from retail data.
- **Corroborated:** multiple independent observations agree.
- **Tentative:** low-level behavior is known, but the higher-level semantic name is not yet proven.
- **Unknown:** the call/field/state is known to exist, but its purpose remains unresolved.

## Executable analyzed

The currently analyzed Windows executable has:

| Property | Value |
|---|---|
| PE image base | `0x00400000` |
| PE entry point | `0x00411640` |
| Linker version | `5.0` |
| Linker timestamp | `1999-10-04 20:31:50` |
| SHA-256 | `55f7120bfea7891b048c64e3682f3259cdbf2719a43fa24e42254b753c95d2ef` |

Addresses in this document refer to that executable. They should not be assumed stable across localized, patched, or otherwise different builds.

---

# What counts as “startup complete”

There is no single `startupComplete = true` transition in Runtime.

For OpenNomad reverse-engineering purposes, a useful operational endpoint is the point at which:

1. CRT/process setup has completed;
2. the game and render windows exist;
3. the large core engine initializer has completed;
4. startup FMVs have played or have intentionally been skipped;
5. the permanent `aventure.scx` scenario/resource bank is resident;
6. `IMAGES\\OMIKRON.BMP` has been displayed for its synchronous five-second interval;
7. the normal Win32 message/game-frame loop is active;
8. the ScenarioEngine has followed the retail `IAM/START` → `IAM/AREA` startup path;
9. startup area **118** and its `GRID` model/scenario dependencies have been established;
10. area startup script event processing has executed opcode `0x46`;
11. generic interface **29 (`0x1D`)** has been opened;
12. interface 29's initializer at `0x00479D10` has run;
13. further behavior is owned by the ordinary recurring frame, scenario, script, interface and rendering systems.

This document deliberately stops at the **running main menu**.

Selecting **New Game**, the Kay'l portal/arrival sequence, the subsequent transition to Anekbah, and handing control to the player are later game-state transitions and are outside this startup boundary.

## Later AREA handoff correction

The later compact-VM handoff is split into residency and presentation.
`0x2F` prepares the alternate AREA slot (AREA bytes, decor, SCX, and world
context) but leaves the destination `LoadedInactive` and source active. `0x47`
attaches IAM/SCENE, materializes only SCENE-local entities, queues the
independent SCENE compact event, updates the AREA-to-SCENE mapping, and commits
the prepared destination as active. `0x49` resolves its named address across
both resident AREA slots and applies it only to an already-established
controlled character. `0x30` finally releases the requested inactive source
AREA and any attached SCENE.

See [`iam-scene.md`](iam-scene.md) for the record format and replacement lifecycle.

---

# Executive overview

The currently established normal retail path is:

```text
Windows loader
    |
    v
PE / MSVC CRT entry
0x00411640
    |
    v
WinMain-equivalent
0x00410950
    |
    +--> early DirectDraw/display probe
    +--> QueryCancelAutoPlay registration
    +--> single-instance checks
    +--> custom command-line tokenization
    +--> startup diagnostics/path setup
    +--> initialize default OMK_SAVE/config state
    +--> optional CONFIG-only dialog path
    +--> load persisted OMK_SAVE/config state
    +--> process WINDOW / NOFMV
    +--> optional host/viewer window
    +--> primary game/render window creation
    |
    v
Top-level Omikron lifetime wrapper
0x00439470
    |
    +--> establish protected initialization boundary
    |
    v
Core engine initialization
0x0041FA00
    |
    +--> low-level graphics setup
    +--> 3D engine state
    +--> polygon/texture/small-texture/small-palette buffers
    +--> screen / DirectDraw mode
    +--> sound
    +--> screen format validation
    +--> renderer upload callback selection
    +--> scenario/script base path "SCPTDATA\\"
    +--> script callback registration
    +--> static collision buffers
    +--> numerous pools/registries/world systems
    +--> ScenarioEngine mode 0
    +--> TURNCD.BMP resource
    |
    v
Startup movie phase, unless suppressed
    |
    +--> FLIS\\EIDOS.mpg
    +--> FLIS\\QUANTIC.mpg
    +--> FLIS\\GAME.mpg
    |
    v
Load permanent scenario/resource bank
0x0041B5A0("aventure.scx")
    |
    +--> SCPTDATA\\aventure.scx
    +--> permanent slot around 0x00930780
    |
    v
Five-second splash
0x00420A20("IMAGES\\OMIKRON.BMP")
    |
    +--> load
    +--> draw/present
    +--> free image resource
    +--> Sleep(5000)
    |
    v
Long-lived Win32 + game-frame loop
0x00439310
    |
    +--> message pump
    +--> active-frame dispatch
    |
    v
Frame timing/input/update driver
0x0041F740
    |
    +--> recurring game-state update
    +--> ScenarioEngine mode 1 processing
    |
    v
Initial ScenarioEngine transition
    |
    +--> mode 3 reset/transition when requested
    +--> mode 2 bootstrap
    |       |
    |       +--> IAM\\START
    |       +--> initial area ID = 118
    |       +--> initial/link value = -1
    |       +--> IAM\\AREA record 118
    |       +--> model dependency GRID
    |       +--> scenario dependency GRID
    |       +--> GRID.3DO + GRID.SCX
    |       +--> active area/script context
    |
    +--> ordinary mode 1 processing
            |
            +--> startup event/script execution
            +--> area script + 0x26
            +--> bytes: 46 1D 00 FF FF 13 00
            +--> opcode 0x46
                    |
                    +--> interfaceId = 29
                    +--> arg1 = -1
                    +--> arg2 = 19
                    |
                    v
              generic interface-open path
              0x00403860
                  -> 0x0041DF30
                  -> 0x00429BB0
                    |
                    v
              interface 29 initializer
              0x00479D10
                    |
                    v
              ordinary main-menu/frame processing
```

The central architectural conclusion is:

> **The main menu is not a special hardcoded jump at the end of WinMain.** Runtime reaches it through the same data-driven area, script, and generic interface machinery used by the rest of the game.

---

# Phase 0 — PE entry and Microsoft CRT startup

## `0x00411640` — executable entry point

**Confirmed — Runtime.**

The PE `AddressOfEntryPoint` resolves to `0x00411640`. This is compiler-generated Microsoft C runtime startup code rather than Omikron-specific game logic.

Observed responsibilities include:

1. install CRT/SEH startup state;
2. call `__set_app_type(2)` for a GUI application;
3. initialize CRT file/console mode globals;
4. run CRT initializers;
5. call `__getmainargs`;
6. retrieve the raw Windows command line through `_acmdln`;
7. skip the executable-name portion of the command line;
8. obtain `STARTUPINFOA` and determine the `nCmdShow` value;
9. call `GetModuleHandleA(NULL)`;
10. call the WinMain-equivalent at `0x00410950`;
11. feed WinMain's result into the CRT exit path.

### Executable-name parsing

The CRT stub handles a quoted executable path while finding the start of `lpCmdLine`:

- if the first character is `"`, it scans to the matching quote;
- otherwise it scans to whitespace;
- it then skips following whitespace.

This is separate from Runtime's own later argument tokenizer, which is significantly less sophisticated.

### Reimplementation implication

OpenNomad does not need to imitate old MSVC CRT internals. The behavior that matters is approximately:

```c
WinMain(
    GetModuleHandleA(NULL),
    NULL,
    commandLineAfterExecutableName,
    resolvedShowCommand);
```

---

# Phase 1 — WinMain process shell

## `0x00410950` — WinMain-equivalent

**Confirmed — Runtime.**

This is the outer Omikron process bootstrap. It prepares process/global/platform state and then hands the application lifetime to `0x00439470`.

A simplified outline is:

```c
int Runtime_WinMain(hInstance, hPrevInstance, lpCmdLine, nCmdShow)
{
    noFmv = false;

    if (!EarlyDirectDrawProbe())
        noFmv = true;

    if (hPrevInstance != NULL)
        return 0;

    register QueryCancelAutoPlay;
    reject existing Nomad Soul windows;

    tokenizeCommandLine(lpCmdLine);

    StartupDiagnosticsAndPathSetup(hInstance);
    InitializeDefaultSaveAndConfigState();

    if (argument == "CONFIG")
        return RunConfigurationDialogAndExit();

    LoadPersistedSaveAndConfigState();

    process "WINDOW";
    process "NOFMV";

    maybeCreateAuxiliaryViewerOrHostWindow();
    maybeCreateModelessWindowDialog();

    renderWindow = CreateNomadSoulWindow(...);

    result = Omikron_Initialize(hInstance, nCmdShow);

    StartupDiagnosticsShutdown();
    return result;
}
```

## 1. Reset FMV suppression state

At the beginning of WinMain:

```text
0x009103CD = 0
```

A useful working name is:

```c
g_noFmv;
```

The same byte is later set by the `NOFMV` argument and tested by the startup movie phase.

## 2. Early DirectDraw/display capability probe — `0x0043B300`

**Confirmed — Runtime.**

Runtime performs an early DirectDraw/display probe before full engine initialization.

The function temporarily creates/queries DirectDraw state, obtains display/capability information, records mask/capability data in globals around `0x0052DDA8`–`0x0052DDBC`, and releases temporary objects.

If it reports failure:

```text
0x009103CD = 1
```

Therefore FMVs can be suppressed by either:

- explicit `NOFMV`; or
- failure of this early display probe.

### What this is not

This is **not** the full renderer/device initialization. Substantial screen and 3D-engine initialization occurs later inside `0x0041FA00`.

### Open question

Why movie suppression is the chosen response to failure of this early probe is not yet established. It may simply be a safety check around the movie/display backend.

## 3. `hPrevInstance` guard

WinMain checks `hPrevInstance` and returns `0` if it is non-null.

On Win32 this parameter is normally unused/zero. The following explicit `FindWindowA` checks are therefore the meaningful single-instance mechanism.

## 4. Register `QueryCancelAutoPlay`

Runtime calls:

```c
RegisterWindowMessageA("QueryCancelAutoPlay");
```

and stores the resulting registered message ID at:

```text
0x0052DD28
```

This is consistent with suppressing Windows AutoPlay behavior while the game is active.

The complete handling of this registered message in the window procedures is not yet fully documented.

## 5. Single-instance checks

Runtime checks, in order:

```c
FindWindowA("The Nomad Soul game", NULL);
FindWindowA("The Nomad Soul", NULL);
```

If either succeeds, WinMain exits quietly.

**Confirmed — Runtime.**

## 6. Runtime's custom command-line tokenizer

Runtime clears:

```text
0x500 bytes
```

of local storage, corresponding to:

```text
10 × 0x80-byte argument slots
```

It then copies `lpCmdLine` into these fixed slots.

### Tokenization rule

The delimiter is literally:

```text
ASCII 0x20 (' ')
```

Every space terminates the current token and advances to the next `0x80`-byte slot.

The final token is terminated and counted at the end.

### Consequences

This is not normal quote-aware Windows command-line parsing. Known consequences include:

- quoted user arguments are not treated as a single token in the usual way;
- repeated spaces can yield empty tokens;
- each token has a fixed `0x80`-byte slot;
- aggregate storage is only ten slots;
- robust bounds behavior has not been demonstrated.

OpenNomad does not need to reproduce unsafe fixed-buffer behavior, but should preserve recognized option semantics.

## 7. Startup diagnostics/path setup — `0x00413120`

Runtime calls:

```text
0x00413120(hInstance)
```

before initializing default game state.

**Confirmed — Runtime.**

Known work includes:

- obtaining the current directory;
- ensuring a trailing backslash;
- copying path information into engine-global state;
- emitting startup/debug messages;
- calling `GlobalMemoryStatus`;
- logging memory statistics;
- allocating/initializing a diagnostics-related object.

### Open question

The complete resource search policy has not yet been mapped. Runtime later uses many relative asset names (`SCPTDATA\\...`, `FLIS\\...`, `IMAGES\\...`), but the exact current-directory/CD/fallback logic still needs tracing.

## 8. Initialize default `OMK_SAVE` / configuration state — `0x0041F4C0`

**Confirmed — Runtime.**

Runtime clears:

```text
0x36A dwords = 0xDA8 bytes
```

starting at:

```text
0x0090E180
```

It writes the signature:

```text
OMK_SAVE
```

and version:

```text
0x00010001
```

It also installs default configuration/state values including:

```text
width  = 640
height = 480
```

at:

```text
0x0090E18C
0x0090E18E
```

respectively.

Many other flags, preferences, tables and state values are initialized here.

### Ordering rule

Defaults are installed **before** persisted data is loaded.

Persisted state therefore overlays a known initialized structure. OpenNomad should preserve this logical order rather than loading settings first and synthesizing defaults afterward.

## 9. Special `CONFIG` mode

After defaults are installed, Runtime scans the parsed arguments for exact token:

```text
CONFIG
```

When found, Runtime takes a separate configuration-only path.

Observed sequence includes:

```text
0x0043A3E0
0x0043B100(0xFF, 0xFF)
0x0043A3F0(...)
DialogBoxParamA(
    hInstance,
    resource 0x68,
    NULL,
    dialogProc = 0x00410070,
    0)
0x0043B190(...)
0x0043A900
0x00413270
return 1
```

### Critical behavior

`CONFIG` is a **configuration-only execution mode**.

Runtime does **not** close the dialog and then continue into normal game startup.

### Gaps

Still incomplete:

- semantic mapping of every control in resource `0x68`;
- all fields modified by the dialog;
- exact persistence path on acceptance;
- whether all hardware probes are identical to those used by normal startup.

## 10. Load persisted `OMK_SAVE` state — `0x00409200`

If `CONFIG` was not requested, Runtime calls:

```text
0x00409200
```

**Confirmed — Runtime.**

The persistence backend is opened using the identifier:

```text
IAM\\GAMES
```

The loader requests:

```text
0xDA8 bytes
```

and checks the first eight bytes against:

```text
OMK_SAVE
```

Observed accepted versions are:

```text
0x00010000
0x00010001
```

For accepted data Runtime copies:

```text
0x36A dwords
```

into the state block at `0x0090E180`, then normalizes the in-memory version to:

```text
0x00010001
```

The older version receives a small compatibility adjustment that clears at least two bytes associated with later state/configuration.

### Do not over-interpret `IAM\\GAMES`

The exact storage abstraction behind the `0x0040FE90` / `0x0040FFxx` family is not yet completely understood.

This document does **not** claim that `IAM\\GAMES` is simply a normal filesystem path.

## 11. Mark startup/save state active

After persisted state loading:

```text
0x00910315 = 1
```

The exact semantic name of this flag remains unresolved.

## 12. Process `WINDOW`

Runtime scans tokens for:

```text
WINDOW
```

and sets:

```text
0x0091030B = 1
```

A useful working interpretation is:

```c
g_windowRequested;
```

This is a **request**, not proof that the alternate/windowed DirectDraw mode was successfully activated.

## 13. Process `NOFMV`

Runtime scans tokens for:

```text
NOFMV
```

and sets:

```text
0x009103CD = 1
```

This is the same flag set by a failed early display probe.

## 14. Optional auxiliary/viewer/host window

Runtime tests:

```text
0x0091030A
0x00910309
```

If either is nonzero, it calls:

```text
0x00410D60(hInstance)
```

and uses the returned HWND as an auxiliary/host/parent window.

### `0x0091030A`

Later screen initialization strongly establishes this as the **active/supported alternate windowed DirectDraw mode**, distinct from the raw request at `0x0091030B`.

During screen setup, the requested mode is accepted only when the required DirectDraw capability is present.

### `0x00910309`

Other preference-reading code identifies this as a `viewer` preference/mode.

The exact path by which this field can become active before this WinMain test remains only partially mapped.

### `0x00410D60`

This function participates in creation/registration of the auxiliary window path and in `QueryCancelAutoPlay` handling.

The precise architectural distinction among:

- normal render window;
- alternate/windowed mode;
- viewer mode;
- host/parent window;

still needs dedicated window-system analysis.

## 15. Optional modeless dialog resource `0x67`

If both:

```text
0x0091030B != 0
0x0091030F != 0
```

Runtime creates:

```c
CreateDialogParamA(
    hInstance,
    resource 0x67,
    hostWindow,
    dialogProc = 0x00410BC0,
    0);
```

and restores focus to the host window.

The exact preference represented by `0x0091030F` remains unknown.

## 16. Create primary game/render window — `0x00438740`

Runtime stores platform handles in globals including:

```text
HINSTANCE             -> 0x004E7804
primary render HWND   -> 0x004E7808
host/parent HWND      -> 0x004E780C
modeless dialog HWND  -> 0x004E768C
```

It calls:

```text
0x00438740(hInstance, hostOrParent)
```

for the primary game/render window.

Strings associated with the window system include:

```text
The Nomad Soul
The Nomad Soul game
```

The main window procedure is around:

```text
0x00438A30
```

and remains only partially documented.

## 17. Handoff to the Omikron lifetime wrapper

WinMain calls:

```text
0x00439470(hInstance, nCmdShow)
```

This function owns the game lifetime from engine initialization through the long-lived message/game loop and subsequent shutdown.

When it eventually returns, WinMain calls outer diagnostics/platform cleanup at:

```text
0x00413270
```

and returns the initializer's result.

---

# Phase 2 — top-level Omikron initializer

## `0x00439470`

**Confirmed — Runtime.**

This is the top-level lifetime wrapper around:

- protected initialization/failure handling;
- core engine initialization;
- startup FMVs;
- permanent `aventure.scx` load;
- the five-second splash;
- the long-lived message/game loop;
- main engine shutdown.

A high-level representation is:

```c
int Omikron_Initialize(hInstance, nCmdShow)
{
    establishProtectedInitializationBoundary();

    if (returnedByInitializationFailure)
        goto fatal;

    maybeCreateDebugOrAllocatorHelper();

    CoreInitialize();                 // 0x41FA00
    LogMemoryStatus();                // 0x411E30

    sendCustomWindowMessage(0xC00, 0);

    if (!g_noFmv)
        PlayStartupMovies();

    sendCustomWindowMessage(0xC00, 1);

    LoadNamedScenario("aventure.scx");
    ShowSplashForFiveSeconds("IMAGES\\OMIKRON.BMP");

    RunMessageAndGameLoop(hInstance); // 0x439310

    MainClose();                      // 0x420000
    freeOptionalHelper();
    return 0;

fatal:
    MainClose();
    freeOptionalHelper();
    MessageBoxA(...);
    return 1;
}
```

## Protected initialization boundary

At entry Runtime calls an exception/setjmp-like helper around:

```text
0x004BBC18
```

using storage around:

```text
0x0052C8F8
```

Subsystem initialization can therefore escape to a common failure path rather than every call manually propagating errors.

The exact mechanics and complete list of helpers that use the protected escape remain to be mapped.

## Fatal initialization message

The failure branch displays:

```text
Title: The Nomad Soul fatal error
Text:  Can't initialize Omikron
```

and returns failure.

This top-level message is separate from individual subsystem-specific diagnostic strings emitted during core initialization.

---

# Phase 3 — core engine initialization

## `0x0041FA00`

**Confirmed — Runtime.**

This is the large engine initializer.

It logs:

```text
START OF INITIALIZATION
```

near its beginning and:

```text
END OF INITIALIZATION
```

near its end.

The **order of calls** is substantially better understood than the semantic identity of every anonymous helper. The table below is therefore deliberately conservative with names.

## Ordered initialization ledger

| Order | Address / call | Current interpretation | Confidence |
|---:|---|---|---|
| 1 | `0x0043D0B0(...)` | low-level display/graphics environment setup using selected HWND and module handle | Call order confirmed; exact role tentative |
| 2 | `0x0043CE60()` when needed | fallback graphics/display setup | Branch confirmed; semantics partial |
| 3 | `0x0045BFF0(0)` | subsystem reset/init | Unknown semantic |
| 4 | `0x0045EF20(0x0090E724)` | renderer/config mode setup | Tentative |
| 5 | `0x0045EF60(0x0090E725)` | second renderer/config setup | Tentative |
| 6 | `0x0041EBB0()` | engine-global setup | Unknown semantic |
| 7 | `0x004403E0()` | initialize 3D-engine/global renderer state | Strongly corroborated by failure text |
| 8 | `0x00440340(sizeA,sizeB)` | allocate polygon buffer(s) | Confirmed by failure text |
| 9 | `0x004406B0(0x3A,0)` | allocate texture buffer | Confirmed by failure text |
| 10 | `0x004407A0(0x40,0x40,0x40)` | allocate small-texture buffer | Confirmed by failure text |
| 11 | `0x00440910(0x40,0x10)` | allocate small-palette buffer | Confirmed by failure text |
| 12 | `0x00432280(&width,&height,windowRequested)` | screen/display-mode initialization | Confirmed |
| 13 | `0x00427E80()` | post-screen graphics/resource setup | Exact semantic unknown |
| 14 | `0x0091030C = 1` | mark screen/engine stage active | Write confirmed; name tentative |
| 15 | `0x0046C3A0()` | sound-system initialization | Confirmed by failure text |
| 16 | `0x0042BA30()` | timing/audio-related setup | Tentative |
| 17 | `0x0042BC10(0x00910316)` | timing/audio-related mode setup | Tentative |
| 18 | write word `0x004C30D4 = 2` | mode/state setup | Semantic unresolved |
| 19 | `0x0043B840()` | media/display helper initialization | Unknown exact semantic |
| 20 | `0x00433420(...)` | query screen attributes | Confirmed by failure text |
| 21 | `0x00433560()` | query effective screen bit depth | Confirmed; only 15/16 accepted |
| 22 | `0x00440A20(screenAttrs)` | configure renderer from screen attributes/masks | Strongly corroborated |
| 23 | `0x0042F9A0()` | select renderer upload callbacks | Confirmed by renderer RE |
| 24 | `0x00431010()` | graphics/resource continuation | Exact semantic unknown |
| 25 | `0x00449340("SCPTDATA\\")` | establish scenario/script data base path | Confirmed |
| 26 | `0x0046E9B0(0x004691B0)` | register/init script callback/hook | Strongly suspected; exact naming tentative |
| 27 | `0x0045DB30()` | subsystem initialization | Unknown semantic |
| 28 | optional `0x0045DCF0(1)` | conditional subsystem enable | Unknown semantic |
| 29 | `0x00443280(100,1024)` | initialize static-decoration collision buffers | Confirmed by adjacent diagnostic text |
| 30 | `0x00431500(256,1)` | initialize pool/table | Exact semantic unknown |
| 31 | `0x00431600(0x00910354)` | configure previous pool/system | Exact semantic unknown |
| 32 | `0x0045DF50(1,320)` | initialize pool/system | Exact semantic unknown |
| 33 | `0x0045B600(2,25,25)` | initialize pool/system | Exact semantic unknown |
| 34 | `0x00429800()` | interface/input/game-state setup | Partial |
| 35 | `0x00413970()` | subsystem initialization | Unknown semantic |
| 36 | `0x00419060()` | subsystem initialization | Unknown semantic |
| 37 | `0x0046E690(...)` twice | construct/register two runtime descriptors/contexts | Structure observed; naming incomplete |
| 38 | viewer-only resource branch | load/build optional configured viewer resources | Confirmed branch; not normal retail path with viewer off |
| 39 | `0x0046DAC0()` | finalize script/world subsystem | Exact semantic unknown |
| 40 | optional `0x00457040(...)` | conditional setup when a global resource exists | Exact semantic unknown |
| 41 | `0x00408410(0,0)` when not viewer | ScenarioEngine mode-0 initialization | Confirmed |
| 42 | `0x0041E260()` | post-scenario setup | Exact semantic unknown |
| 43 | `0x0043FFC0()` | renderer/engine finalization | Exact semantic unknown |
| 44 | allocation statistics | log total allocated memory/allocation count | Confirmed |
| 45 | `END OF INITIALIZATION` | diagnostic milestone | Confirmed |
| 46 | `0x0045B240()` | final subsystem call | Exact semantic unknown |
| 47 | `0x00428A20("IMAGES\\TURNCD.BMP")` | load turn/change-CD image resource | Confirmed; handle stored at `0x004E973C` |

This is an **ordering ledger**, not a claim that every core subsystem has been fully understood.

## Fatal subsystem anchors

The following strings identify several calls with high confidence:

```text
3d engine error : impossible to initialize the 3d engine.
3d engine error : impossible to allocate the polygon buffer.
3d engine error : impossible to allocate the texture buffer.
3d engine error : impossible to allocate the small textures buffer.
3d engine error : impossible to allocate the small palettes buffer.
Game error : impossible to initialize the screen.
Configuration error : impossible to initialize the sound system.
DirectX error : impossible to get the screen attributes.
DirectX error : you should configure your desktop to use 65536 colors (16 bits).
```

These strings are valuable because they establish subsystem meaning independently of guessed function names.

## Display-depth validation

After querying screen attributes, Runtime calls `0x00433560`.

Only:

```text
15
16
```

are accepted.

Anything else emits the user-facing 65536-colors/16-bit configuration message.

The code therefore distinguishes 15- and 16-bit effective formats internally even though the user-facing requirement is phrased as “16 bits.”

## Requested vs active windowed mode

The `WINDOW` token sets:

```text
0x0091030B
```

before core initialization.

During `0x00432280`, Runtime tests a DirectDraw capability before accepting the requested alternate mode. If supported it sets:

```text
0x0091030A
```

The most useful current interpretation is:

```text
0x0091030B = requested windowed/alternate mode
0x0091030A = mode actually accepted/active
```

These should not be collapsed into one boolean in a behavioral reimplementation.

## Renderer upload callback selection

`0x0042F9A0` installs renderer-dependent function pointers later used by texture/palette upload paths.

This imposes an ordering dependency: resources that rely on renderer upload callbacks must not be treated as fully renderer-ready before this point.

## Script/scenario base path

Runtime passes:

```text
SCPTDATA\\
```

to `0x00449340` during core initialization.

The script/scenario subsystem therefore knows its base path **before** the later permanent `aventure.scx` load.

## ScenarioEngine mode 0 during core initialization

Near the end of normal, non-viewer initialization Runtime calls:

```c
ScenarioEngine_Control(0, 0);
```

through the wrapper/dispatcher chain around:

```text
0x00408410
0x00407DC0
```

Known effects of mode 0 include:

- call `0x0040E170`;
- reset several transition globals;
- initialize timing/state values;
- put the main ScenarioEngine state around `0x004E6C74` into its initial state.

This happens **before** startup movies and before `aventure.scx` is loaded by the top-level initializer.

That means ScenarioEngine control infrastructure exists before its permanent startup scenario bank is populated.

---

# Phase 4 — startup FMVs

After core initialization and memory-status logging, the top-level initializer performs startup movie playback unless:

```text
0x009103CD != 0
```

## Pre-movie window synchronization

Runtime sends custom message:

```text
0x0C00
```

with a zero-like state to relevant windows before movie playback.

The exact contract of custom message `0xC00` is still unknown.

## Movie subsystem initialization

Runtime calls:

```text
0x0043B4A0
```

before the first movie.

## Exact playback order

**Confirmed — Runtime.**

The filenames are attempted in this order:

```text
1. FLIS\\EIDOS.mpg
2. FLIS\\QUANTIC.mpg
3. FLIS\\GAME.mpg
```

Each is played through:

```text
0x0043B4E0
```

with callback:

```text
0x00439730
```

and current window/display state.

The user-visible retail sequence matches:

1. Eidos video/logo;
2. Quantic Dream video/logo;
3. the game intro movie.

## Conditional chaining

After each movie Runtime checks global movie state around:

```text
0x0052DD54
```

The next movie is attempted only while this state remains in the expected condition.

Therefore the sequence is not simply three unconditional playback calls.

## Movie teardown

After the movie chain Runtime calls:

```text
0x0043B7F0
```

## Post-movie window synchronization

Runtime sends custom message `0xC00` again, this time with a one-like state.

## `NOFMV`

The entire movie init/playback/teardown block is skipped when `0x009103CD` is set.

Known reasons for that flag to be set are:

- explicit `NOFMV` command-line token;
- failed early DirectDraw/display probe.

## FMV gaps

Still unresolved:

- exact enum/meaning of `0x0052DD54`;
- complete input/skip/abort behavior in callback `0x00439730`;
- exact MPEG backend semantics;
- how movie mode interacts with viewer/alternate window modes;
- whether all regional builds use identical filenames and sequencing.

---

# Phase 5 — permanent `aventure.scx` load

Immediately after movie teardown/window resynchronization, Runtime calls:

```c
0x0041B5A0("aventure.scx");
```

**Confirmed — Runtime.**

## `0x0041B5A0`

The helper constructs:

```text
SCPTDATA\\%s
```

and therefore resolves the startup call to:

```text
SCPTDATA\\aventure.scx
```

It operates on the global scenario slot around:

```text
0x00930780
```

The observed sequence is approximately:

```text
format "SCPTDATA\\aventure.scx"

0x0044B140(&slot_0x930780)   // query existing scenario state

if already loaded:
    0x0044AAC0(&slot_0x930780) // unload prior content

0x00449750(path, &slot_0x930780) // load scenario
```

## Architectural role

Current SCX reverse engineering supports treating this as a **permanent/shared scenario-resource bank**, distinct from the currently active world/area scenario contexts.

This distinction is essential for startup.

The main menu is **not** opened by directly running “the menu script in `aventure.scx`.” The engine later enters its normal area-selection/script/interface machinery.

## Relevant SCX slot architecture

Current findings identify:

```text
permanent/mode SCX slot:
    0x00930780

world/area context slot 0:
    0x009103E8

world/area context slot 1:
    0x0091046C
```

The two world contexts are approximately `0x84` bytes each and include, among other fields:

```text
+0x04 model
+0x08 scenario
```

The allocator can prefer a free slot, recycle inactive context state, and avoid evicting an active context.

This explains why startup can keep:

```text
aventure.scx
```

resident while later establishing an active world context containing:

```text
GRID.3DO
GRID.SCX
```

---

# Phase 6 — five-second `OMIKRON.BMP` splash

After `aventure.scx` is loaded, Runtime calls:

```c
0x00420A20("IMAGES\\OMIKRON.BMP");
```

**Confirmed — Runtime.**

This function has been traced closely enough to settle an important earlier ambiguity:

> The five-second splash is synchronous and completes **before** the long-lived message/game loop begins.

## High-level sequence

`0x00420A20` performs approximately:

```c
image = LoadImageResource("IMAGES\\OMIKRON.BMP"); // 0x428A20

GetImageDimensions(...);                          // 0x4289D0
DrawImage(..., image, ..., mode = 0x0F);          // 0x4287A0
PresentOrActivate(...);                           // 0x428B00
FinalizeFrameOrFlip();                            // 0x433860

FreeImageResource(image);                         // 0x428A90

Sleep(0x1388);                                    // exactly 5000 ms
```

At the relevant call site Runtime pushes:

```text
0x1388
```

which is decimal `5000`, then calls `Sleep`.

## Exact ordering consequence

The actual top-level order is:

```text
startup movies
    ->
load aventure.scx
    ->
draw/present OMIKRON.BMP
    ->
free splash image resource
    ->
Sleep(5000)
    ->
enter 0x00439310
```

It is **not**:

```text
enter ordinary game loop
    ->
keep a splash state alive for the first five seconds of frames
```

This is an important fidelity detail for OpenNomad.

## Splash gaps

Still unresolved or only partly named:

- exact image-resource abstraction returned by `0x00428A20`;
- precise semantics/names of `0x004287A0`, `0x00428B00`, `0x00433860`;
- whether any exceptional platform path can interrupt the direct `Sleep(5000)`.

The direct sleep strongly implies that normal frame processing does not occur during the five-second hold.

---

# Phase 7 — long-lived Win32 + game-frame loop

After the splash function returns, `0x00439470` calls:

```text
0x00439310(hInstance)
```

This is the long-lived process loop and normally does not return until the application exits.

## `0x00439310`

**Confirmed — Runtime.**

It combines:

- conventional Win32 message dispatch;
- waiting when the engine is inactive/not ready;
- custom window dispatch for alternate/windowed mode;
- an Escape-key special action;
- per-frame timing/input/game-update dispatch.

A close high-level representation is:

```c
for (;;) {
    if (PeekMessageA(&msg, NULL, 0, 0, 0)) {
        if (GetMessageA(&msg, NULL, 0, 0) == 0)
            break;

        TranslateMessage(&msg);
        DispatchMessageA(&msg);
        continue;
    }

    if (!engineReadyA || !engineReadyB || engineBlocked) {
        frameResetRequested = 1;
        WaitMessage();
        continue;
    }

    if (activeWindowedMode) {
        for (i = 0; i < 16; ++i) {
            if (window[i] && windowActive[i])
                SendMessageA(window[i], 0x800, 0, 0);
        }
    }

    if (EscapeIsDown() && escapeGuard == 0)
        Interface_Open(0x1F, -1, -1);

    FrameTimingAndUpdate(
        frameResetRequested,
        globalWord_0x90EF2E);

    frameResetRequested = 0;
}
```

## Normal message path

When a message exists, Runtime uses:

```text
PeekMessageA
GetMessageA
TranslateMessage
DispatchMessageA
```

`WM_QUIT` is observed through `GetMessageA` returning zero.

## Inactive/not-ready path

The active frame path is gated by several globals including:

```text
0x004E7694
0x0052DD58
0x0052DD4C
```

When the engine is not ready/runnable Runtime sets:

```text
0x004C5944 = 1
```

and calls:

```text
WaitMessage()
```

The next active frame receives that reset/request value as an argument to `0x0041F740`.

Exact semantic names for all readiness globals remain unresolved.

## Custom message `0x800`

When active alternate/windowed mode `0x0091030A` is enabled, Runtime scans sixteen window entries and sends:

```text
message = 0x800
wParam  = 0
lParam  = 0
```

to active windows.

The precise purpose of custom message `0x800` remains unknown.

## Escape special action

Runtime polls:

```c
GetAsyncKeyState(VK_ESCAPE)
```

If Escape is down while guard global:

```text
0x004E9728 == 0
```

it calls:

```text
0x00429BB0(0x1F, -1, -1)
```

Interface `0x1F` is decimal **31**.

This is **not** the startup main-menu interface, which is `0x1D` / decimal **29**. The two should not be conflated.

---

# Phase 8 — per-frame timing/input/update driver

The active branch of `0x00439310` calls:

```text
0x0041F740
```

once per ordinary engine frame.

## `0x0041F740`

Known work includes:

1. reset timing baselines when its first parameter requests it;
2. query time through `0x004120F0`;
3. update input state via `0x0043E0D0`;
4. derive pressed/changed input bitfields;
5. call `0x004200F0` when its second parameter permits normal update processing;
6. sample timing state again;
7. compute frame delta and smoothed timing values;
8. derive floating-point time-scale values;
9. clamp/override timing according to engine state.

It is therefore best described as a **frame timing/input/update driver**, not the complete game simulation.

## `0x004200F0`

This is a larger recurring game-state/frame routine reached by `0x0041F740` in the normal state.

Among other work it:

- performs global/interface/state checks;
- drives additional update/render systems;
- invokes ScenarioEngine mode `1` during normal non-viewer operation;
- contains a later intro/movie/scenario reload path.

### Later `FLIS\\GAME.mpg` replay path

`0x004200F0` contains a later transition path associated with an `1800`-unit state/timer condition that can:

1. execute ScenarioEngine mode `3`;
2. replay `FLIS\\GAME.mpg`;
3. reload `aventure.scx`;
4. execute ScenarioEngine mode `2`.

This is useful evidence for ScenarioEngine lifecycle semantics, but it is **not** the one-shot top-level startup movie sequence in `0x00439470`.

---

# Phase 9 — ScenarioEngine control modes

## Wrapper and dispatcher

The wrapper currently identified at:

```text
0x00408410
```

validates a mode in the range:

```text
0 .. 4
```

and dispatches through code around:

```text
0x00407DC0
```

A useful working name is:

```c
ScenarioEngine_Control(mode, argument);
```

The name is descriptive rather than recovered from symbols.

## Current mode map

| Mode | Dispatch branch | Major known behavior | Startup relevance |
|---:|---|---|---|
| `0` | around `0x00407DDB` | calls `0x0040E170`, resets transition globals, initializes timer/state, puts engine state into initial mode | Called during core initialization before movies |
| `1` | around `0x00407E2C` | recurring ScenarioEngine state-machine processing; can force mode `3` then `2` first when a transition request is set | Normal post-splash processing |
| `2` | around `0x004081D1` → `0x0040E060` | bootstrap/load start-state data including `IAM\\START`, area selection and active context creation | Critical to initial area 118 |
| `3` | around `0x00408163` | teardown/reset/transition work through `0x00401000`, `0x0041B390`, `0x0041DF90`, `0x0040E260` depending state | Used before mode 2 when a reload/start transition is requested |
| `4` | around `0x004081E3` → `0x0040E140` | reset/clean several numbered world/context slots | Not yet required to explain the normal initial menu path |

## Important mode-1 transition behavior

At the beginning of the recurring mode-1 branch, behavior equivalent to the following occurs:

```c
if (g_scenarioTransitionRequested != 0) {
    ScenarioEngine_Control(3, 0);
    g_scenarioTransitionRequested = 0;
    ScenarioEngine_Control(2, 0);
    // additional transition helper work
}
```

The request global is around:

```text
0x004E6C9C
```

Only after this transition path does ordinary mode-1 state processing continue.

This establishes that startup is subtler than:

```text
mode 0 -> mode 1 forever
```

The engine can enter recurring mode 1 while still having a pending request that causes a teardown/bootstrap pair first.

## Major remaining gap: who owns the initial request?

We know the retail startup reaches the mode-2 bootstrap path.

What is not yet cleanly reduced to one semantic owner is the exact first setter/call chain that causes `0x004E6C9C` to be asserted for the initial startup transition.

This is one of the most important remaining gaps in the bridge between:

```text
splash completes
```

and:

```text
IAM/START is processed
```

Multiple systems can request similar transitions later, so a write-xref alone is not enough to identify the conceptual owner.

---

# Phase 10 — ScenarioEngine mode 2 and `IAM/START`

## `0x0040E060`

Mode `2` dispatches to:

```text
0x0040E060
```

**Confirmed — Runtime.**

The function resets area/world-selection state and then loads/copies the resource identified by:

```text
IAM\\START
```

through the engine's IAM/resource backend.

Observed work includes calls such as:

```text
0x004095B0
0x00406270
0x00406730
0x00408E00
```

followed by loading/copying `IAM\\START` and continuing through helpers including `0x0040DB00`.

The exact semantics of every helper remain in progress, but the resource identity and startup values are established.

## `IAM/START` startup values

**Confirmed — data.**

The retail `IAM/START` data currently analyzed establishes:

```text
initialAreaId = 118
initial/link   = -1
```

at currently identified offsets:

```text
+0x586 = 118
+0x588 = -1
```

These are data-driven retail values, not OpenNomad defaults invented for the main menu.

## Consequence

Mode-2 startup processing uses the selected area ID to obtain the corresponding entry from:

```text
IAM\\AREA
```

Area **118** is therefore the next data-driven startup node.

---

# Phase 11 — `IAM/AREA` record 118

## Current record geometry

**Confirmed — data.**

The relevant startup record currently parses as:

```text
area ID:       118
record size:   0x9C0
fixed header:  0x0B4
scriptOffset:  0x3FC
```

Known dependency names in the record are:

```text
model:    GRID
scenario: GRID
```

which correspond to startup dependencies:

```text
GRID.3DO
GRID.SCX
```

## Some known internal tables

Current parsing has identified at least:

```text
table 0:
    offset = 0x0B4
    count  = 2
    stride = 0x14

table 6:
    offset = 0x51C
    count  = 27
    stride = 0x2C
```

Not every table's semantics are known yet.

## Active area/world context

The area-load path creates/selects a world context and associates the area's model and scenario dependencies with it.

For startup this means the permanent bank:

```text
0x00930780 -> aventure.scx
```

coexists with an active world context containing:

```text
GRID.3DO
GRID.SCX
```

rather than replacing it.

## Critical script-source distinction

The instruction that opens the main menu is in the **script embedded in `IAM/AREA` record 118**.

It is **not** simply an instruction read directly from:

```text
aventure.SCX
```

or:

```text
Grid.SCX
```

`GRID.SCX` is still a required active area scenario/resource set, but the specific menu-opening startup instruction comes from the area record's script region.

This distinction is important because merely loading an SCX file must not mean “execute every script it contains.”

---

# Phase 12 — startup area script and event processing

The area load creates a script context rooted at:

```text
areaRecord + scriptOffset
```

For area 118:

```text
scriptOffset = 0x3FC
```

The startup process activates/queues the area's startup event, currently identified as:

```text
event 1
```

During the first appropriate recurring ScenarioEngine mode-1 processing, that script context begins executing.

Current scheduler work has touched functions around:

```text
0x0040CC90
0x0040CCB1
0x0040CBC1
```

but exact event-queue structure names and ownership are still provisional.

## Menu-opening bytecode

**Confirmed — data and Runtime.**

At:

```text
script + 0x26
```

which is:

```text
area record + 0x422
```

are the bytes:

```text
46 1D 00 FF FF 13 00
```

Decoded:

```text
opcode    = 0x46
operand 0 = 0x001D = 29
operand 1 = 0xFFFF = -1
operand 2 = 0x0013 = 19
```

This is the direct data-level bridge from generic startup area processing to main-menu interface creation.

---

# Phase 13 — script opcode `0x46`

## Handler `0x00403860`

**Confirmed — Runtime.**

The handler reads three signed 16-bit operands, with support for the script VM's variable/reference encoding.

For the startup instruction they resolve directly to:

```text
interfaceId = 29
arg1        = -1
arg2        = 19
```

The handler also:

- modifies script-context execution state;
- stores one or more operands into global state under specific conditions;
- writes another operand to state around `0x004E6B28`;
- specially recognizes interface ID `0x1D`.

When the requested interface is exactly:

```text
0x1D
```

it stores the current script context at:

```text
0x004E6C7C
```

This independently corroborates that interface 29 is a specially significant interface in this startup path.

### Confirmed suspension/resumption semantics

**Confirmed — Runtime + retail data.**

Opcode `0x46`:

- advances its instruction pointer past the instruction;
- opens the interface through the generic interface path;
- sets wait state 6;
- suspends the AREA script context until the interface completes;
- resumes at the instruction immediately after opcode `0x46` when the
  interface is completed (e.g. New Game for interface 29).

This is proven by the AREA 118 startup ordering:

```text
opcode 0x67 (109, 1, 1)   -> play TRACKS/109.ADP (loop)
...
opcode 0x46 (29, -1, 19)  -> open interface 29, suspend script
opcode 0x67 (87, 1, 1)    -> replace track 109 with TRACKS/87.ADP
```

If opcode `0x46` returned immediately, track 87 would replace track 109 almost
instantly and the menu music would not remain playing. 109.ADP is the
main-menu music; 87.ADP is the looping Kay'l portal/tunnel introduction music
requested once New Game completes interface 29. The resumed AREA script owns
both music requests — the New Game callback only completes the interface.

### AREA music opcode `0x67`

Handler `FUN_00404FB0`. Three signed 16-bit operands:

```text
operand 0: numeric ADP track ID -> TRACKS/%d.ADP
operand 1: looping flag
operand 2: unresolved mode/state flag (preserved, not named)
```

See `script-opcodes.md` §25.5 and `Core/Omikron/QdAdp.{hpp,cpp}` for the ADP
container and QD IMA codec details.

## Interface resolution/open chain

The handler resolves interface metadata through:

```text
0x0041DEF0(interfaceId)
```

and proceeds through:

```text
0x0041DF30(...)
```

which ultimately calls the generic interface-opening function:

```text
0x00429BB0(...)
```

---

# Phase 14 — generic interface opening

## `0x00429BB0`

A useful working name is:

```c
Interface_Open;
```

**Confirmed behavior; exact original symbol unknown.**

The function performs work including:

1. inspect existing runtime interface instances;
2. enforce interface/flag compatibility rules;
3. find a free interface instance;
4. search the static interface descriptor table;
5. copy descriptor state into the runtime interface instance;
6. load optional interface image/resource data;
7. configure callback/state fields;
8. close conflicting interfaces when required;
9. call the interface-specific initializer indirectly.

## Descriptor table

The static descriptor table is based around:

```text
0x004CB640
```

with entries large enough to include information such as:

- interface ID;
- flags/configuration;
- optional resource string;
- callback pointers;
- initializer pointer.

The full schema is not yet established.

## Initializer dispatch

At:

```text
0x00429ED7
```

Runtime performs an indirect call equivalent to:

```asm
call dword ptr [ebx + 0x0C]
```

where `ebx` is the newly prepared runtime interface instance.

For interface:

```text
29 / 0x1D
```

this initializer resolves to:

```text
0x00479D10
```

---

# Phase 15 — interface 29 / `StartMenu_Initialize`

## `0x00479D10`

**Confirmed — Runtime.**

This is the initializer reached for startup interface 29 and is therefore the strongest current concrete boundary for:

```text
the startup path has entered the main menu
```

A useful working name is:

```c
StartMenu_Initialize;
```

## Known initialization behavior

The function begins by polling/waiting through helpers around:

```text
0x00412760
0x00412250
```

until a required engine condition is satisfied.

It then configures a substantial set of menu/interface descriptors and state.

Observed work includes repeated calls to functions such as:

```text
0x00429140
0x00429650
0x004295C0
0x00429680
0x004296D0
0x004778E0
0x00478BC0
```

against descriptor/state objects in the `0x004CE8xx`–`0x004CF4xx` range.

It stores:

```text
interfaceInstance + 0x1C = 0x004CF218
```

and later registers/configures several callback/control structures through `0x00478BC0`.

## Architectural conclusion

The startup menu is not accurately modeled as:

```text
clear to light blue
then draw a few hardcoded text buttons
```

The established chain is:

```text
IAM data selects startup area
    ->
area startup script executes
    ->
opcode 0x46 opens an interface ID
    ->
generic Interface_Open creates interface 29
    ->
interface 29's registered initializer configures menu objects/callbacks
    ->
ordinary interface/render processing owns subsequent frames
```

This explains how a reimplementation can reach the “right menu state” yet still produce an empty light-blue scene: opening interface 29 is only the beginning of the menu's data-driven construction/render path.

## Menu details still outside the solved startup path

The following remain incompletely understood and should be documented separately as the menu reverse engineering advances:

- exact semantics of the `0x004CE...` / `0x004CF...` descriptor objects;
- which descriptor corresponds to each visible menu object/item;
- background/model/scene construction (the animated bump background is now recovered — see below);
- camera state;
- font/text rendering path;
- item highlight/selection transitions;
- menu animations;
- audio callbacks;
- localization;
- per-frame menu update callbacks;
- cleanup and state-transition callbacks.

## Recovered main-menu rendering (I2D bump background + source-keyed logo)

Two of the previously approximate rendering behaviours are now reproduced from `Runtime.exe` (`C:\Omikron\Sources\omikron\I2D_Bump.c`).

### `IMAGES/CLOUD.BMP` is a height map, not display imagery

`CLOUD.BMP` is a 256×256, 8-bit, uncompressed (`BI_RGB`), bottom-up indexed bitmap (validated against retail: header `BM`, DIB size 40, width 256, height 256, bpp 8, compression 0, colours-used 0). Runtime does not display its RGB appearance — it indexes the raw 8-bit pixel bytes directly as a height field:

```c
height[y * 256 + x]
```

Relevant Runtime addresses:

```text
0x004B19C0  I2D bump-background initialization
0x004B1B00  per-frame bump lighting + final surface generation
0x004B1F40  animated 480-entry / 640-entry distortion-table generation
0x004B2220  CLOUD.BMP loading and custom colour-ramp construction
```

The recovered effect (implemented as the pure-CPU `I2DBumpEffect`):

- a moving light on a circle of radius 64 around `(128, 128)`, angle starting at `10.0` and advancing `+0.0785` per tick (`_ftol` truncation toward zero);
- signed 8-bit X/Y height gradients with wrapped neighbours (`(x + 1) & 0xFF`, `(y + 1) & 0xFF`);
- per-pixel intensity `clamp(((dx * (x - light_x) + dy * (y - light_y)) >> 5) + 32, 0, 63)` (arithmetic `SAR` shift);
- a recovered 64-entry colour ramp (burnt orange → dark brown → blue-green/teal);
- two animated cosine lookup tables (480 row entries, 640 column entries) consumed in reverse order to warp the 256×256 lit map into a 640×480 surface.

The palette anchors are `palette[0] ≈ (126, 29, 0)`, `palette[31] = palette[32] ≈ (19, 17, 13)`, `palette[63] ≈ (37, 108, 102)`. The first recovered tick yields `light_x = 75`, `light_y = 94`. Effect timing is fixed at 30 original ticks per second (Runtime constants are per effect update, not per second), independent of the host frame rate.

### `I2D/bitmaps/gfxint.bmp` is source-colour-keyed

The main-menu bitmap element (`~0x004CF1A8`) carries:

```text
source:      0,0,640,150
destination: 0,0,640,150
runtime flags: 0x40000100
blit mode:   0x03
```

`runtime_flags` (`0x40000100`) and `runtime_blit_mode` (`0x03`) are distinct recovered fields. The low-level Runtime blit path (`~0x004810D0`) maps the mode byte as:

```text
bit 0 (0x01) = DDBLT_KEYSRC  = source colour key
bit 1 (0x02) = DDBLT_KEYDEST = destination colour key
```

The main-menu element uses `0x03`, requesting both keys. The source surface key is confirmed as pixel value 0 (`DDCKEY_SRCBLT`, low = high = 0); the destination surface's `DDCKEY_DESTBLT` value has not yet been located, so destination-key emulation is intentionally deferred.

This is why the black background of `gfxint.bmp` must not appear: those source pixels do not overwrite the bump background. The recovered key is "pixel value 0" in a 16-bit RGB555 surface, so the near-black palette background (retail `gfxint.BMP` palette index 255 = RGB 4,4,4) truncates to 0 and keys out; the I2D data model preserves this as a source colour key (not ordinary alpha), and the renderer reproduces the 5-bit comparison before discarding matching texels.

---

# When the game is “running on its own”

There is no single instruction that marks this concept.

The most useful operational boundary is:

```text
StartMenu_Initialize (0x00479D10) has initialized interface 29
AND
0x00439310 remains in the recurring message/frame loop
AND
ScenarioEngine mode 1 is now the ordinary recurring processing mode
```

At this point:

- one-shot startup FMVs are over;
- the synchronous splash delay is over;
- the permanent scenario bank is resident;
- an active `GRID` world/area context exists;
- startup area script execution has occurred;
- interface 29 exists;
- further behavior is driven by normal input, timing, scenario, script, interface, renderer and Win32 message processing.

That is the recommended endpoint for reproducing **Runtime startup**.

Everything after it is better treated as normal game/application state transition behavior.

---

# Visible retail sequence vs invisible engine sequence

The user-visible startup is simple:

```text
launch Runtime.exe
Eidos movie
Quantic Dream movie
game intro movie
five-second Omikron splash
main menu
```

The invisible engine sequence is much richer:

```text
CRT / WinMain
    ->
early display probe
    ->
default state
    ->
persisted state overlay
    ->
command-line overrides
    ->
windows
    ->
full engine initialization
        including ScenarioEngine mode 0
    ->
startup movies
    ->
load permanent aventure.scx
    ->
load/draw/free OMIKRON.BMP
    ->
Sleep(5000)
    ->
enter long-lived message/game loop
    ->
ScenarioEngine startup transition
    ->
mode 2: IAM/START
    ->
area 118
    ->
IAM/AREA record 118
    ->
GRID.3DO + GRID.SCX
    ->
active area/script context
    ->
startup event 1
    ->
mode-1 script processing
    ->
opcode 0x46
    ->
interface 29
    ->
StartMenu_Initialize
    ->
ordinary running menu
```

A faithful OpenNomad implementation should preserve the second sequence even when the first could be faked with fewer systems.

---

# Important globals

Names below are working names unless stated otherwise.

| Address | Working meaning | Confidence |
|---|---|---|
| `0x009103CD` | `g_noFmv` | Confirmed behavior; set by `NOFMV` or failed early display probe |
| `0x0052DD28` | registered `QueryCancelAutoPlay` message ID | Confirmed |
| `0x0090E180` | `OMK_SAVE` / persistent config-state block | Confirmed; size `0xDA8` |
| `0x0090E188` | persistent state version | Confirmed; normalized to `0x00010001` |
| `0x0090E18C` | display/default width | Confirmed; defaults to `640` |
| `0x0090E18E` | display/default height | Confirmed; defaults to `480` |
| `0x0091030B` | requested `WINDOW` mode | Confirmed |
| `0x0091030A` | active/supported alternate windowed mode | Strongly corroborated |
| `0x00910309` | `viewer` preference/mode | Corroborated; startup activation timing partial |
| `0x0091030F` | optional modeless-dialog flag/preference | Semantic unknown |
| `0x004E7804` | `HINSTANCE` | Confirmed |
| `0x004E7808` | primary render/game HWND | Confirmed |
| `0x004E780C` | auxiliary/host/parent HWND | Strongly corroborated |
| `0x004E768C` | optional modeless dialog HWND | Confirmed storage |
| `0x0052DD54` | movie playback state | Use confirmed; enum unknown |
| `0x00930780` | permanent/mode SCX slot holding `aventure.scx` at startup | Confirmed architecture |
| `0x009103E8` | world/area context slot 0 | Confirmed architecture |
| `0x0091046C` | world/area context slot 1 | Confirmed architecture |
| `0x004C5944` | frame reset/timing request passed to `0x0041F740` | Behavior confirmed; name tentative |
| `0x0090EF2E` | second frame-driver state/mode parameter | Use known; semantic unresolved |
| `0x004E6C74` | ScenarioEngine internal state | Confirmed state-machine use |
| `0x004E6C9C` | ScenarioEngine reload/start-transition request | Confirmed behavior |
| `0x004E6C7C` | special/active script context used by startup interface-29 opcode path | Confirmed behavior |
| `0x004E6B28` | opcode-`0x46` related global/argument state | Write confirmed; semantic unknown |
| `0x004E973C` | `TURNCD.BMP` image/resource handle | Confirmed |
| `0x004E9728` | Escape/interface guard | Use confirmed; exact semantic partial |

---

# Important function index

Names are working names unless an original meaning is directly established.

| Address | Current role |
|---|---|
| `0x00411640` | MSVC CRT / PE entry point |
| `0x00410950` | WinMain-equivalent |
| `0x0043B300` | early DirectDraw/display probe |
| `0x00413120` | startup diagnostics/current-path/memory setup |
| `0x0041F4C0` | initialize default `OMK_SAVE` / config state |
| `0x00409200` | load persisted `OMK_SAVE` state |
| `0x00410D60` | create auxiliary/viewer/host window path |
| `0x00410070` | `CONFIG` modal dialog procedure |
| `0x00410BC0` | modeless resource-`0x67` dialog procedure |
| `0x00438740` | primary Nomad Soul window creation |
| `0x00438A30` | main game window procedure |
| `0x00439470` | top-level Omikron initializer/lifetime wrapper |
| `0x0041FA00` | core engine initialization |
| `0x00432280` | screen/display-mode initialization |
| `0x0046C3A0` | sound-system initialization |
| `0x00433420` | query screen attributes |
| `0x00433560` | query effective screen depth |
| `0x0042F9A0` | select renderer upload callbacks |
| `0x00449340` | establish scenario/script base path |
| `0x0046E9B0` | script callback/hook registration |
| `0x00443280` | static-decoration collision-buffer initialization |
| `0x00408410` | ScenarioEngine control wrapper |
| `0x00407DC0` | ScenarioEngine mode dispatcher |
| `0x0040E170` | mode-0 ScenarioEngine initialization work |
| `0x0040E060` | mode-2 bootstrap / `IAM\\START` path |
| `0x0040E260` | mode-3 transition/reset work |
| `0x0041B5A0` | load named scenario into permanent/global SCX slot |
| `0x00449750` | lower-level SCX load |
| `0x0044AAC0` | scenario unload |
| `0x0044B140` | scenario loaded/state query |
| `0x0043B4A0` | movie subsystem initialization |
| `0x0043B4E0` | movie playback |
| `0x00439730` | startup movie callback |
| `0x0043B7F0` | movie subsystem teardown |
| `0x00420A20` | splash load/draw/free + `Sleep(5000)` |
| `0x00428A20` | image-resource load |
| `0x004287A0` | image draw/blit |
| `0x00428A90` | image-resource free |
| `0x00439310` | Win32 message + game-frame loop |
| `0x0041F740` | frame timing/input/update driver |
| `0x004200F0` | recurring game-state/frame update |
| `0x00403860` | script opcode `0x46` handler |
| `0x0041DEF0` | resolve interface descriptor data |
| `0x0041DF30` | script-facing interface-open wrapper |
| `0x00429BB0` | generic `Interface_Open` |
| `0x00429ED7` | indirect interface initializer call site |
| `0x00479D10` | interface-29 / `StartMenu_Initialize` |
| `0x00420000` | main engine close/shutdown |

---

# Startup resource access currently demonstrated

This list intentionally contains only resources already demonstrated in the startup path.

| Resource | Startup role |
|---|---|
| `IAM\\GAMES` | persistence backend identifier used while loading `OMK_SAVE` state |
| `OMK_SAVE` | signature/version for the `0xDA8` persistent state block |
| `SCPTDATA\\` | scenario/script base path established during core initialization |
| `IMAGES\\TURNCD.BMP` | image resource loaded near the end of core initialization |
| `FLIS\\EIDOS.mpg` | first startup movie |
| `FLIS\\QUANTIC.mpg` | second startup movie |
| `FLIS\\GAME.mpg` | third startup movie |
| `SCPTDATA\\aventure.scx` | permanent/shared startup SCX bank |
| `IMAGES\\OMIKRON.BMP` | synchronous five-second startup splash |
| `IAM\\START` | ScenarioEngine mode-2 initial area selection data |
| `IAM\\AREA` record `118` | startup area definition and embedded startup script |
| `GRID.3DO` | startup area's model/world dependency |
| `GRID.SCX` | startup area's active scenario dependency |

Many other assets are loaded transitively by these resources and by interface 29. They should be documented at the owning format/system level rather than guessed into this list.

---

# Error and shutdown paths

## Initialization failure

Subsystem failures inside `0x0041FA00` report through the engine's error path and can ultimately reach the protected failure boundary established by `0x00439470`.

The top-level failure path:

1. calls main close/cleanup;
2. releases optional helper/debug state;
3. displays:

```text
The Nomad Soul fatal error
Can't initialize Omikron
```

4. returns failure to WinMain.

## Normal process shutdown

When `0x00439310` exits after normal application termination:

```text
0x00439470
    ->
0x00420000   // MAIN_CLOSE / engine close
    ->
free optional helper
    ->
return 0
```

The close routine logs:

```text
START OF MAIN_CLOSE
```

and performs substantial subsystem teardown.

The full shutdown order is not yet part of this document.

## `CONFIG` shutdown

The configuration-only path never enters `0x00439470`.

It performs its own dialog/platform cleanup and returns directly from WinMain.

---

# Corrections to earlier assumptions

The following points are worth recording explicitly because they are easy to implement incorrectly.

## 1. The five-second splash is synchronous

Earlier high-level descriptions could be read as if `OMIKRON.BMP` were a special state processed during the first normal game frames.

Runtime instead does:

```text
draw/present splash
free image resource
Sleep(5000)
then enter 0x00439310
```

## 2. `aventure.scx` loads before the splash

The actual top-level order is:

```text
movies
-> movie teardown
-> load aventure.scx
-> show/sleep on OMIKRON.BMP
-> enter ordinary loop
```

## 3. The main menu is not a WinMain special case

There is no direct equivalent of:

```c
ShowMainMenu();
```

after the splash.

Runtime reaches the menu through:

```text
ScenarioEngine
-> IAM/START
-> IAM/AREA 118
-> area startup script
-> opcode 0x46
-> interface 29
```

## 4. The menu-opening instruction is not simply “in `GRID.SCX`”

The active startup scenario is indeed `GRID.SCX`, but the specific bytecode:

```text
46 1D 00 FF FF 13 00
```

is in the script region embedded in **`IAM/AREA` record 118**.

## 5. Loading an SCX must not automatically execute arbitrary scripts

SCX residency and script activation are separate concepts.

This matters especially because `GRID.SCX` also participates in later states such as New Game/Kay'l sequences. Loading it for the startup area must not cause arbitrary later scripts to fire.

## 6. `WINDOW` request and active windowed mode are separate

```text
WINDOW argument -> 0x0091030B
accepted mode   -> 0x0091030A
```

The second is set only after capability negotiation.

## 7. ScenarioEngine modes are lifecycle operations, not interchangeable update calls

The startup lifecycle includes roles for:

```text
mode 0: initialize control infrastructure
mode 3: transition teardown/reset when requested
mode 2: bootstrap from IAM/START
mode 1: recurring processing
```

Skipping directly to a loaded area may reproduce a screenshot but not Runtime's state ordering.

## 8. Reaching interface 29 is not equivalent to fully rendering the menu

The interface is dynamically initialized through descriptor/callback machinery. A hardcoded scene can therefore appear as an empty blue void even when the state transition itself is approximately correct.

---

# Open questions and gaps

The broad startup order is now fairly well established. The remaining gaps are increasingly about ownership, schemas, scheduler semantics, and menu construction.

The following are roughly ordered by their usefulness for a faithful reimplementation.

## Priority 1 — exact first ScenarioEngine transition-request owner

Established:

- mode 0 is called during core initialization;
- recurring mode 1 tests `0x004E6C9C`;
- when set, mode 1 performs mode 3 then mode 2 before continuing;
- retail startup reaches mode 2 / `IAM\\START`.

Still unresolved:

- exact first setter/call chain responsible for the initial startup assertion of `0x004E6C9C`;
- whether the request conceptually belongs to permanent scenario load completion, a world/global object, another startup-state flag, or a higher-level transition controller;
- whether unusual viewer/configuration paths use an alternate route.

This is currently the largest control-flow gap between:

```text
five-second splash completes
```

and:

```text
mode 2 consumes IAM/START
```

## Priority 2 — complete `IAM/START` schema

Known:

```text
+0x586 = initialAreaId 118
+0x588 = -1 initial/link value
```

Unknown:

- semantic names of the rest of the structure;
- save/load or alternate-start overrides;
- other game modes using the same data;
- complete meaning of the `-1` link value.

## Priority 3 — complete `IAM/AREA` schema

For record 118 known:

```text
size         = 0x9C0
header       = 0xB4
scriptOffset = 0x3FC
model        = GRID
scenario     = GRID
```

Still missing:

- semantic name of every header field;
- all table types;
- which data field(s) define or cause startup event 1;
- complete relationship between area-embedded scripts and SCX script/resource data;
- whether record size/layout is invariant across all entries/builds.

## Priority 4 — exact script scheduler/event queue

The resulting behavior is known:

```text
area context
-> startup event 1
-> recurring update
-> opcode 0x46
```

Still needed:

- event-queue structure layout;
- priority/order rules;
- whether event 1 is implicit on area creation or explicitly described by area data;
- exact instruction-pointer initialization;
- script context ownership/lifetime;
- semantic naming around the current `0x0040CCxx` / `0x0040CBxx` functions.

## Priority 5 — interface descriptor table schema

`Interface_Open` is understood well enough to prove the startup chain, but the table around:

```text
0x004CB640
```

still lacks a complete field schema.

This blocks a fully data-driven reimplementation of generic interfaces.

## Priority 6 — main-menu construction/rendering

`0x00479D10` is confirmed as the interface-29 initializer, but its descriptor setup is only partially named.

Need to establish:

- background scene/model construction;
- camera setup;
- visible menu object hierarchy;
- text/font path;
- localized strings;
- selection/highlight behavior;
- animation timing;
- audio;
- callback semantics;
- cleanup/transition behavior;
- exact renderer state responsible for the menu's appearance.

This is the main gap behind an implementation that enters the menu state but renders only a light-blue void.

## Priority 7 — custom window messages `0xC00` and `0x800`

Need semantic names and contracts for:

```text
0xC00
0x800
```

Questions include:

- sender/receiver ownership;
- activation/renderer-acquisition semantics;
- why `0xC00` brackets movie playback;
- how `0x800` participates in alternate/windowed mode;
- whether either causes renderer surface/device reacquisition.

## Priority 8 — viewer/host/window architecture

Need a complete model of:

- `0x00410D60`;
- host/parent HWND;
- primary render HWND;
- `viewer` mode;
- modeless dialog resource `0x67`;
- flag `0x0091030F`;
- main WndProc around `0x00438A30`;
- focus/activation handling;
- DirectDraw cooperative-level changes.

## Priority 9 — persistence backend

`0x00409200` is understood at the `OMK_SAVE` block level, but the backend behind:

```text
IAM\\GAMES
0x0040FE90
0x0040FF00
0x0040FF20
0x0040FF40
0x0040FF70
```

needs a clearer abstraction.

Questions:

- filesystem vs packed/resource storage;
- settings vs save-game separation;
- write timing;
- older-version migration behavior.

## Priority 10 — resource search/current-directory/CD policy

Relative resource strings are pervasive.

Need to map:

- installation/current directory;
- CD path;
- fallback order;
- localization paths;
- behavior when resources are absent;
- whether the imported `PATCH` component participates in path redirection.

## Priority 11 — movie subsystem details

Need:

- `0x0052DD54` state enum;
- complete semantics of callback `0x00439730`;
- skip keys and abort behavior;
- exact renderer/sound state before and after movie playback;
- error recovery.

## Priority 12 — semantic naming of anonymous core-init helpers

The ordering ledger is sufficient to preserve dependencies, but several helpers remain named only by position.

The most useful next work is likely around:

- input initialization/acquisition;
- sound details;
- low-level DirectDraw/Direct3D object creation;
- pool ownership;
- world/entity registries;
- script VM bootstrap;
- collision-system dependencies.

## Priority 13 — protected initialization failure unwinding

Need to identify:

- every call capable of escaping through the protected boundary;
- which already-initialized subsystems are conditionally closed;
- double-close guards;
- exact propagation into `MAIN_CLOSE`.

## Priority 14 — build and regional differences

This document describes one Windows retail executable/data set.

Unknown:

- localized executable differences;
- patched Windows releases;
- digital-distribution packaging differences;
- Dreamcast startup architecture;
- whether area 118 and the same menu script bytes are invariant across every retail data set.

---

# Recommended reverse-engineering breakpoints

These addresses are useful anchors for validating or extending the startup trace in a debugger.

## Process shell

```text
0x00410950  WinMain-equivalent
0x0043B300  early display probe
0x0041F4C0  default OMK_SAVE/config state
0x00409200  persisted state load
0x00438740  primary render-window creation
0x00439470  top-level Omikron lifetime wrapper
```

## Core initialization

```text
0x0041FA00  core init entry
0x004403E0  3D-engine init
0x00432280  screen init
0x0046C3A0  sound init
0x0042F9A0  renderer upload callback selection
0x00449340  SCPTDATA base path
0x00408410  ScenarioEngine control wrapper
```

## Movies and splash

```text
0x0043B4A0  movie subsystem init
0x0043B4E0  movie playback
0x0043B7F0  movie subsystem close
0x0041B5A0  named permanent-scenario load
0x00420A20  splash function
0x00420A99  immediate area around push 5000 / Sleep
0x00439310  long-lived loop entry
```

## Scenario startup transition

```text
0x00439310  message/game loop
0x0041F740  frame timing/input/update driver
0x004200F0  recurring game-state update
0x00408410  ScenarioEngine control
0x00407E2C  mode-1 branch vicinity
0x00408163  mode-3 branch vicinity
0x004081D1  mode-2 branch vicinity
0x0040E060  IAM/START bootstrap
```

Useful watchpoints:

```text
0x004E6C9C  startup/reload transition request
0x004E6C74  ScenarioEngine state
0x00930780  permanent SCX slot
0x009103E8  world context slot 0
0x0091046C  world context slot 1
```

## Main-menu script/interface boundary

```text
0x00403860  opcode 0x46 handler
0x0041DF30  script-facing interface-open wrapper
0x00429BB0  generic Interface_Open
0x00429ED7  indirect interface initializer call
0x00479D10  interface-29 / StartMenu initializer
```

Useful data breakpoint/reference within `IAM/AREA` record 118:

```text
record + 0x422
```

for:

```text
46 1D 00 FF FF 13 00
```

---

# Reimplementation ordering requirements

A modern SDL/OpenGL implementation does not need to preserve Win32/DirectDraw API mechanics. It **does** need to preserve the logical lifecycle and data dependencies where they affect game behavior.

## Recommended logical ordering

```text
1. establish application/process state

2. install default configuration/state

3. overlay persisted configuration/state

4. apply explicit command-line overrides

5. create required platform/window objects

6. initialize core engine systems in dependency order
   - renderer/resource allocators before dependent uploads
   - screen/device state before screen-format-dependent resources
   - sound/input/script/scenario infrastructure
   - collision/world pools
   - ScenarioEngine mode 0

7. play startup movies unless suppressed

8. load permanent aventure.scx

9. show OMIKRON.BMP and hold synchronously for 5000 ms

10. enter the recurring application/game loop

11. let normal ScenarioEngine transition machinery bootstrap the world
    - mode 3 when requested
    - mode 2
    - IAM/START
    - area 118
    - IAM/AREA record 118
    - GRID.3DO
    - GRID.SCX
    - active area context
    - startup event/script context

12. execute the area startup script normally

13. execute opcode 0x46 normally

14. open generic interface 29 normally

15. invoke interface 29's registered initializer

16. allow ordinary mode-1/frame/interface/render processing to continue
```

## Shortcuts to avoid

Do not replace the architecture with startup-only special cases such as:

```c
afterSplash() {
    loadGrid();
    showMainMenu();
}
```

or:

```c
if (scenarioName == "GRID")
    runAllGridScripts();
```

or:

```c
if (startup)
    createHardcodedBlueMenuScene();
```

Those can reproduce selected visible states while removing behavior that the rest of the game reuses.

Runtime demonstrates that:

- startup area selection is data-driven;
- model/scenario dependencies come from area data;
- scripts activate through context/event machinery;
- interface opening is generic;
- interface 29 is registered data/callback behavior rather than a WinMain-only screen;
- ordinary runtime systems continue seamlessly after startup.

---

# Minimal chronological checklist

Use this as a compact comparison against an OpenNomad trace.

```text
[ ] PE/CRT reaches WinMain-equivalent 0x00410950
[ ] no-FMV flag cleared
[ ] early DirectDraw/display probe 0x0043B300
[ ] QueryCancelAutoPlay registered
[ ] existing Nomad Soul windows rejected
[ ] command line tokenized
[ ] diagnostics/current path initialized
[ ] default 0xDA8 OMK_SAVE state initialized
[ ] CONFIG-only branch checked
[ ] persisted OMK_SAVE state loaded
[ ] WINDOW override applied
[ ] NOFMV override applied
[ ] optional host/viewer window handled
[ ] primary game/render window created
[ ] 0x00439470 entered
[ ] protected initialization boundary established
[ ] 0x0041FA00 core initialization begins
[ ] 3D engine initialized
[ ] polygon buffer allocated
[ ] texture buffer allocated
[ ] small-texture buffer allocated
[ ] small-palette buffer allocated
[ ] screen initialized
[ ] sound initialized
[ ] screen attributes queried
[ ] effective 15/16-bit format accepted
[ ] renderer upload callbacks selected
[ ] SCPTDATA\\ base path installed
[ ] script callback/hook registered
[ ] static collision buffers initialized
[ ] remaining pools/registries initialized
[ ] ScenarioEngine mode 0 executed
[ ] TURNCD.BMP loaded
[ ] core initialization ends
[ ] pre-movie window state message sent
[ ] EIDOS.mpg played unless suppressed
[ ] QUANTIC.mpg played if movie chain continues
[ ] GAME.mpg played if movie chain continues
[ ] movie subsystem closed
[ ] post-movie window state message sent
[ ] SCPTDATA\\aventure.scx loaded into permanent slot
[ ] IMAGES\\OMIKRON.BMP loaded
[ ] splash drawn/presented
[ ] splash image freed
[ ] Sleep(5000)
[ ] 0x00439310 long-lived loop entered
[ ] 0x0041F740 begins recurring frame processing
[ ] ScenarioEngine startup transition request handled
[ ] mode 3 reset/transition occurs when requested
[ ] mode 2 bootstrap runs
[ ] IAM\\START loaded
[ ] initial area 118 selected
[ ] IAM\\AREA record 118 processed
[ ] GRID.3DO dependency established
[ ] GRID.SCX dependency established in active world context
[ ] area script context rooted at +0x3FC created
[ ] startup event 1 activated
[ ] recurring mode-1 processing executes startup script
[ ] bytes 46 1D 00 FF FF 13 00 reached
[ ] opcode 0x46 handler 0x00403860 executes
[ ] interface 29 passed through script-facing wrapper
[ ] Interface_Open 0x00429BB0 creates/activates interface instance
[ ] indirect initializer call occurs at 0x00429ED7
[ ] StartMenu_Initialize 0x00479D10 executes
[ ] ordinary recurring menu/frame processing owns execution
```

---

# Current reverse-engineering milestone

The current startup understanding can be summarized as:

```text
Runtime.exe launch
    ->
WinMain / platform setup
    ->
complete core engine init
    ->
Eidos / Quantic / Game movies
    ->
permanent aventure.scx
    ->
OMIKRON.BMP for exactly five seconds
    ->
normal message/game loop
    ->
ScenarioEngine lifecycle
    ->
IAM/START selects area 118
    ->
IAM/AREA 118 selects GRID / GRID
    ->
active area + startup script event
    ->
opcode 0x46
    ->
interface 29
    ->
StartMenu_Initialize
    ->
main menu running under ordinary engine control
```

The major remaining gaps are no longer the broad startup order. They are the fine-grained semantics and ownership inside:

- the initial ScenarioEngine transition request;
- IAM schemas;
- event/script scheduling;
- generic interface descriptors;
- dynamic main-menu construction and rendering.

Those are the next layers to reverse engineer. They should not be bypassed with startup-specific shortcuts if OpenNomad's goal is behavioral fidelity to `Runtime.exe`.
