# Runtime globals (`Runtime.exe`)

> **Status:** work-in-progress reverse-engineering documentation for OpenNomad.
>
> This document inventories the currently understood process-global state in the Windows retail `Runtime.exe` used by the OpenNomad reverse-engineering effort. It is intentionally more than a list of Ghidra `DAT_XXXXXXXX` symbols: globals are grouped by **owner subsystem, allocation/lifetime, structure boundaries, readers/writers, and confidence**.
>
> The goals are:
>
> 1. provide stable reverse-engineering names and cross-references while working in Ghidra; and
> 2. prevent OpenNomad from copying the original executable's global-memory architecture when the same state can be owned cleanly by modern subsystem objects.

## Live character identity

Runtime's global actor-slot namespace maps each live slot to its canonical
character ID. OpenNomad represents that slot as `Character::BodyIdentity`, not
as `RuntimeCharacter::instance_id`; the latter is vector-local and can be
renumbered. Canonical IDs remain persistent profile/save data and do not change
when an AREA or SCENE placement is rebound during possession. A canonical ID
is not a unique live-entity key: two resident worlds may contain distinct
placements with the same character ID and different `BodyIdentity` values.

AREA/SCENE table-0 `+0x00` is a mutable reference into this actor-slot
namespace. OpenNomad's immutable IAM parser keeps the serialized seed and the
`CharacterReferenceRuntime` overlay stores the live `BodyIdentity`. Camera
attachments, body transfer, placement rebinding, and structured character
launch all carry this same stable identity.

## Reference executable

The addresses in this document apply to the currently studied Windows executable:

```text
SHA-256
55f7120bfea7891b048c64e3682f3259cdbf2719a43fa24e42254b753c95d2ef

PE image base
0x00400000

PE entry point
0x00411640
```

They should **not** be assumed to match every language, patch, demo, Dreamcast build, or later distribution.

The executable is 32-bit x86. Consequently:

- native pointers are 4 bytes;
- many runtime structures are tightly coupled to 32-bit pointer-sized fields;
- absolute addresses in decompilation are often global pointers to heap memory rather than the heap objects themselves;
- zero-initialized state extends well beyond the raw initialized `.data` bytes in the PE file.

## Confidence vocabulary

- **Confirmed — Runtime:** directly demonstrated by executable reads/writes, allocation size, loop bounds, diagnostics, or dispatch behavior.
- **Corroborated:** Runtime behavior agrees with retail assets and/or an independently reconstructed subsystem.
- **Strong:** role is structurally clear, but the original source-level name or a narrow semantic detail remains unknown.
- **Tentative:** useful working interpretation, but should not be promoted to a permanent name without more evidence.
- **Unknown:** address/field is demonstrably state, but its semantic role is not solved.

## What counts as a “global” here

The disassembler presents all of the following as absolute-address state, but they are not the same thing:

```text
single scalar
    0x009103CD -> no-FMV flag

global pointer to a heap allocation
    0x00660B5C -> SpriteInstance pool

inline fixed array
    0x009106A0 -> 100 × 0x520 entity records

base of a larger global structure
    0x009103E0 -> 2 × 0x84 world contexts

field inside a global structure
    0x009103E8 -> worldContexts[0] + 0x08

mutable callback slot
    0x0090E0A8 -> active texture-upload function

read-only/static dispatch table
    0x004C4920 -> backend callback table

temporary process-global scratch
    texture decompression staging memory
```

A major purpose of this document is to keep those categories distinct.

---

# 1. Executive quick reference

| Address/base | Type / extent | Recommended working label | Meaning | Confidence |
|---|---|---|---|---|
| `0x004E7804` | `HINSTANCE` | `g_applicationInstance` | Win32 process/module handle stored by WinMain | Confirmed |
| `0x004E7808` | `HWND` | `g_renderWindow` | Primary game/render window | Confirmed |
| `0x004E780C` | `HWND` | `g_hostWindow` | Optional auxiliary/host/parent window | Strong |
| `0x0052DD28` | `UINT` | `g_queryCancelAutoPlayMessage` | Result of `RegisterWindowMessageA("QueryCancelAutoPlay")` | Confirmed |
| `0x00910309` | `u8` | `g_viewerMode` | Viewer-mode preference/state | Strong |
| `0x0091030A` | `u8` | `g_windowedModeActive` | Accepted/active alternate windowed DirectDraw mode | Strong |
| `0x0091030B` | `u8` | `g_windowedModeRequested` | Set by `WINDOW` command-line token | Confirmed |
| `0x009103CD` | `u8` | `g_noFmv` | Set by `NOFMV` or failed early display probe; gates startup FMVs | Confirmed |
| `0x0090E180` | `0xDA8` bytes | `g_omkSave` | Versioned `OMK_SAVE` persistent/configuration block | Confirmed |
| `0x00930780` | Scenario/SCX state | `g_permanentScenario` | Permanent/shared scenario slot; `aventure.scx` at normal startup | Confirmed |
| `0x009103E0` | `2 × 0x84` | `g_worldContexts` | Two world/area contexts; embedded SCX state at `+0x08` | Confirmed |
| `0x009106A0` | `100 × 0x520` | `g_entityPool` | Main fixed entity/actor pool | Confirmed |
| `0x00930724` | pointer | `g_currentEntity` (?) | Central entity pointer; player-only meaning not yet proven | Strong / tentative name |
| `0x008F5EC0` | `128 × 0x1A8` | `g_loaded3DOPool` | Fixed storage for loaded 3DO runtime records | Confirmed |
| `0x00907100` | `128` pointers | `g_loaded3DORegistry` | Pointer registry for loaded 3DO records | Confirmed |
| `0x0054ECB8` | `512 × 0x10` | `g_animation3DAPool` | Fixed Runtime 3DA animation slot pool | Confirmed |
| `0x00660B5C` | pointer | `g_spriteInstancePool` | Heap pool of `0x40`-byte sprite instances | Confirmed |
| `0x00660B58` | `u16` | `g_spriteInstanceCapacity` | Pool capacity; startup initializes to `0x800` | Confirmed |
| `0x009070B8` | pointer | `g_sortArrayLo` | Renderer sort array; allocation diagnostic names `SortArrayLo` | Confirmed |
| `0x009070E0` | pointer | `g_vertex2DRenderBuffer` | Renderer `Vertex2D` buffer, `0x30`-byte entries | Confirmed |
| `0x00907320` | pointer | `g_face3DRenderBuffer` | Renderer `Face3D` buffer, `0x7C`-byte entries | Confirmed |
| `0x009070B0` | `u8 *` | `g_indexedTexturePages` | 64-KiB-aligned indexed texture-page backing | Confirmed |
| `0x009070BC` | `u8 *` | `g_palettePageRgb` | RGB palette-page storage, `256 × 3` bytes per page | Confirmed |
| `0x00657D98` | `u16 *` | `g_convertedPaletteBase` | Aligned converted 15/16-bit palette backing | Confirmed |
| `0x0090E0A8` | function pointer | `g_upload3DOTexturePage` | Active backend callback used by 3DO/3DT texture upload | Confirmed |
| `0x0090E0A4` | function pointer | `g_upload3DOPalette` | Active backend callback used by palette upload | Confirmed |
| `0x0052B8D8` | `u32` | `g_rendererBackendIndex` | Current index into the two renderer callback variants | Confirmed |
| `0x0080B068` | COM pointer | `g_d3dDevice` (?) | Direct3D-device-like interface; vtable behavior matches render-state calls | Strong |
| `0x004E9718` | `u32` | `g_inputCurrent` | Current input bitfield | Confirmed |
| `0x004E9724` | `u32` | `g_inputPrevious` | Previous-frame input bitfield | Confirmed |
| `0x004E971C` | `u32` | `g_inputPressedThisFrame` | Rising-edge/newly-pressed input bitfield | Confirmed |
| `0x004C30D8` | `float` | `g_frameTimeScale` | Effective per-frame time/step scalar | Strong |
| `0x00531218` | `float` | `g_scriptFrameDelta` | Frame delta consumed by SCX `Script_*` playback | Strong |
| `0x004E6C74` | `u32` | `g_scenarioEngineState` | ScenarioEngine state-machine value | Confirmed |
| `0x004E6C9C` | flag/dword | `g_scenarioReloadRequested` | Mode-1 transition request; causes mode 3 then mode 2 | Confirmed |
| `0x004E6C7C` | pointer | `g_specialScenarioScriptContext` | Scenario/event context saved specially for startup interface 29 | Confirmed behavior |
| `0x004E61E8` | `32` pointers | `g_scenarioContextRegistry` | Fixed registry of compact IAM scenario/event contexts | Confirmed |
| `0x004E6D94` | pointer | `g_scenarioGlobalState` | Shared scenario/START state; `+0x08` points to the dword global-variable array | Confirmed behavior |
| `0x004C012C` | `u8` | `g_scenarioEventStatus` (?) | Event/native-operation status byte touched by `EndEvent` and presentation paths | Strong |
| `0x004C0130` | `u32` | `g_scenarioAreaOpcodeGate` (?) | Special area/opcode execution gate used by `0x2D`/`0x2F` paths | Strong |
| `0x004C013C` | `i32/u32` | `g_currentMusicTrackId` | Numeric music-track state used to avoid restarting the same track | Confirmed behavior |
| `0x004C0140` | static `8`-byte entries | `g_scenarioOpcodeDescriptors` | Native compact-scenario opcode table for `0x00..0x98`, followed by `0x99` sentinel | Confirmed |
| `0x006A05E0` | `u32` | `g_scenarioProbeMode` | Side-effect-suppression flag used while Runtime probes/scans scenario bytecode | Confirmed |
| `0x0067A0B4` | function pointer | `g_scriptMessageCallback` | Callback used by `Script_SendMessage` | Confirmed |
| `0x0067A0B8` | dword/pointer-like | `g_scriptMessageTarget` (?) | Target/context set before Script message callback | Tentative |
| `0x004E9818` | `3 × 0x7C` | `g_interfaceInstances` | Fixed runtime interface-instance pool | Confirmed |
| `0x004CB640` | static table | `g_interfaceDescriptors` | Static descriptor table scanned by `Interface_Open` | Strong |

---

# 2. Global-address geography

Address proximity is useful for orientation but **is not by itself evidence of common ownership**.

## `0x004Cxxxx`: initialized/static engine data

This range contains:

- constants;
- strings;
- default tables;
- callback tables;
- interface descriptors;
- some mutable initialized scalars.

Important examples:

```text
0x004C012C  scenario event/native-operation status byte
0x004C0130  special scenario area/opcode execution gate
0x004C013C  current scenario music-track ID
0x004C0140  compact scenario-VM opcode descriptor table
0x004C30D8  effective frame-time scalar
0x004C4910..0x004C493F renderer-backend callback tables
0x004CB640  interface descriptor table
0x004C8F90 / 0x004C9070 / 0x004C9150
            static configuration tables copied into OMK_SAVE defaults
```

Do not assume every `0x004Cxxxx` address is read-only.

## `0x004Exxxx`: core runtime state

This region contains many compact process-lifetime structures:

```text
0x004E61E8..0x004E6267 32-entry compact scenario-context registry
0x004E6Bxx..0x004E6Dxx ScenarioEngine/event state cluster
0x004E7804..0x004E780C Win32 module/window handles
0x004E96FC..0x004E9734 timing/input/frame-state cluster
0x004E9818..0x004E998B 3-entry interface-instance pool
```

## `0x0052xxxx` / `0x0053xxxx`: engine/device/control state

Known examples:

```text
0x0052B8D8 renderer backend index
0x0052DD28 registered Win32 message
0x0052DD54 movie state
0x00531218 Script frame delta
0x0053AAD8 DirectX/HRESULT diagnostic string buffer
0x0053ADF0 renderer mode/state
0x0054ECB8..0x00550CB7 fixed 512-entry 3DA animation pool
```

## `0x0065xxxx`–`0x006Axxxx`: renderer, palette, sprite, scratch-heavy BSS

Well-understood examples:

```text
0x00657D98 converted 16-bit palette base pointer
0x00660B2C..0x00660B54 lighting/object-overlap allocations
0x00660B58..0x00660B5C sprite pool capacity/pointer
0x0067A0B4..0x0067A0B8 Script message callback state
0x0067A3E8 / 0x006823E8 texture loading/decompression work addresses
0x006A05E0 scenario-VM probe / side-effect-suppression flag
```

Many nearby software-rasterizer globals remain unnamed.

## `0x0080xxxx`: DirectX/renderer device state

The best-understood address is:

```text
0x0080B068
```

which holds a COM interface used through a legacy Direct3D-style vtable.

The surrounding `0x0080B060..0x0080B088` cluster is initialized together by renderer setup.

## `0x008Fxxxx`: resource-loader state and fixed model storage

Important:

```text
0x008F5EC0 128-entry loaded-3DO backing pool
0x008F56D8 cached legacy-D3D render-state value
```

## `0x0090xxxx`: persistent state, renderer allocations, resource registries

Important bases:

```text
0x009070B0..0x00907324 texture/palette/render resource state
0x00907100             loaded-3DO pointer registry
0x0090E09C..0x0090E0AC active renderer callbacks
0x0090E180             OMK_SAVE block
```

## `0x0091xxxx`: runtime options, world contexts, entity pool

```text
0x009103xx runtime option/transition state
0x009103E0 two world contexts
0x009106A0 entity pool
```

## `0x0093xxxx`: post-entity world state and permanent scenario

The entity pool ends at `0x00930720`.

Important globals immediately afterward include:

```text
0x00930724 central entity pointer
0x00930780 permanent/shared Scenario/SCX state
```

---

# 3. Persistent `OMK_SAVE` / configuration block

## Base and extent

**Confirmed — Runtime.**

```text
base: 0x0090E180
size: 0x00000DA8 bytes
end:  0x0090EF28 (exclusive)
```

`0x0041F4C0` explicitly performs:

```asm
mov ecx, 0x36A
mov edi, 0x0090E180
xor eax, eax
rep stosd
```

Therefore:

```text
0x36A dwords × 4 = 0xDA8 bytes
```

are cleared.

The block is then populated with the signature:

```text
OMK_SAVE
```

and default state.

The persistence loader at `0x00409200` reads/copies the same extent.

### Recommended Ghidra model

```c
struct OmkSaveState {
    uint8_t raw[0xDA8];
};
```

Replace `raw` ranges with named fields only as they are proven.

## Confirmed and raw-known early fields

| Address | Offset | Type | Known value/meaning |
|---:|---:|---|---|
| `0x0090E180` | `+0x000` | `char[8]` | `"OMK_SAVE"` |
| `0x0090E188` | `+0x008` | `u32` | version, initialized/normalized to `0x00010001` |
| `0x0090E18C` | `+0x00C` | `u16` | width, default `640` |
| `0x0090E18E` | `+0x00E` | `u16` | height, default `480` |
| `0x0090E190` | `+0x010` | `u8` | default `1`, semantics unknown |
| `0x0090E191` | `+0x011` | `u8` | default `1`, semantics unknown |
| `0x0090E194` | `+0x014` | `u32` | default `50`, semantics unknown |
| `0x0090E198` | `+0x018` | `u32` | default `0` |
| `0x0090E19C` | `+0x01C` | `u32` | default `0` |
| `0x0090E1A0` | `+0x020` | `u32` | default `0` |
| `0x0090E1A4` | `+0x024` | `u8` | default `1` |
| `0x0090E1A5` | `+0x025` | `u8` | default `1` |
| `0x0090E1A6` | `+0x026` | `u16` | default `1` |
| `0x0090E1A8` | `+0x028` | `u16` | default `1` |
| `0x0090E1AA` | `+0x02A` | `u8` | default `1` |
| `0x0090E1AC` | `+0x02C` | `u16` | default `20` |
| `0x0090E1AE` | `+0x02E` | `u16` | default `15` |
| `0x0090E1B0` | `+0x030` | `u8` | default `0` |
| `0x0090E1B1` | `+0x031` | `u8` | default `0` |

These raw defaults are useful evidence even before every UI preference has a semantic name.

## Three copied default-table regions

`0x0041F4C0` copies from:

```text
0x004C8F90 -> 0x0090E1B4
0x004C9070 -> 0x0090E294
0x004C9150 -> 0x0090E374
```

Each destination is a related configuration table inside `OMK_SAVE`.

Later runtime paths can restore these regions from the same static sources.

The exact user-facing meaning is still incomplete; they are plausibly control/device mapping or related preset tables, but that should remain tentative.

## Cleared subrange at `0x0090E454`

The initializer clears:

```text
0xB4 dwords = 0x2D0 bytes
```

starting here.

This is an embedded table/array inside `OMK_SAVE`, not 180 unrelated globals.

## Renderer-related bytes near the end

Known startup use:

```text
0x0090E724 -> 0x0045EF20
0x0090E725 -> 0x0045EF60
```

Defaults:

```text
0x0090E724 = 0
0x0090E725 = 0
0x0090E726 = 3
0x0090E727 = 1
```

The first two are renderer/configuration inputs. Their precise settings-dialog semantics are still open.

## Version handling

The persistence loader accepts at least:

```text
0x00010000
0x00010001
```

and normalizes the live version to:

```text
0x00010001
```

An older-version compatibility path clears some later state.

This is a versioned persistent structure, not merely an arbitrary memory dump.

## Boundary warning

`OMK_SAVE` ends at:

```text
0x0090EF28
```

Addresses beyond that point are adjacent runtime globals unless independently proven serialized.

---

# 4. Runtime option/state cluster around `0x00910300`

The default initializer also clears/initializes many values in `0x009103xx`.

This region is **outside** `OMK_SAVE`.

| Address | Type | Working name | Behavior |
|---:|---|---|---|
| `0x00910309` | `u8` | `g_viewerMode` | viewer preference/mode |
| `0x0091030A` | `u8` | `g_windowedModeActive` | set only after windowed request passes capability negotiation |
| `0x0091030B` | `u8` | `g_windowedModeRequested` | set by exact token `WINDOW` |
| `0x0091030C` | `u8` | `g_screenStageActive` (?) | set after screen setup during core initialization |
| `0x0091030F` | `u8` | optional-dialog flag (?) | participates in modeless dialog-resource `0x67` path |
| `0x00910315` | `u8` | startup/persistent-state-active (?) | set after persisted state loading |
| `0x00910316` | `u8` | unknown | passed to `0x0042BC10` |
| `0x00910329` | `u8` | unknown | gates optional `0x0045DCF0(1)` initialization |
| `0x00910354` | `u32` | unknown | passed to `0x00431600` |
| `0x009103C8` | `u32` | unknown state flag | set to `1` in later runtime/config paths |
| `0x009103CC` | `u8` | unknown | reset by default initialization |
| `0x009103CD` | `u8` | `g_noFmv` | `NOFMV` / failed early display probe |
| `0x009103D0` | timer value | frame-reset timer copy | set when `0x41F740` resets timing baselines |
| `0x009103DC` | dword | unknown world/entity state | immediately precedes world-context array |

Many bytes from roughly:

```text
0x00910308..0x00910329
```

are explicitly reset by `0x0041F4C0`.

Until consumers are fully traced, the correct abstraction is an **engine options/runtime-flags cluster**.

---

# 5. Win32 process/window globals

## `0x004E7804` — application instance

```c
HINSTANCE g_applicationInstance;
```

Stored by WinMain.

**Confirmed — Runtime.**

## `0x004E7808` — primary game/render window

```c
HWND g_renderWindow;
```

Created through `0x00438740`.

**Confirmed — Runtime.**

## `0x004E780C` — auxiliary/host/parent window

```c
HWND g_hostWindow;
```

Used in viewer/alternate-window paths.

**Strong.**

## `0x004E768C` — optional modeless dialog HWND

Created from dialog resource `0x67` in a specific configuration/window path.

Exact UI role is unresolved.

## `0x0052DD28` — `QueryCancelAutoPlay` message ID

Contains:

```c
RegisterWindowMessageA("QueryCancelAutoPlay")
```

Type:

```c
UINT
```

## Frame-readiness globals

The long-lived loop at `0x00439310` checks state including:

```text
0x004E7694
0x0052DD58
0x0052DD4C
```

before allowing normal game frames.

If the application is not in a runnable state it:

```text
sets 0x004C5944 = 1
calls WaitMessage()
```

The three individual gate meanings are not yet stable enough to name as focus/minimize/etc.

---

# 6. Movie/startup globals

## `0x009103CD` — no-FMV gate

Set by:

```text
WinMain initial reset
failed early DirectDraw/display probe
NOFMV command-line token
```

The top-level initializer uses it to skip the entire startup movie sequence.

## `0x0052DD54` — movie playback state

Checked between:

```text
FLIS\EIDOS.mpg
FLIS\QUANTIC.mpg
FLIS\GAME.mpg
```

The next movie is only attempted while the state remains acceptable.

Exact enum meanings are still unknown.

Recommended:

```c
uint32_t g_moviePlaybackState;
```

## `0x004E973C` — retained `TURNCD.BMP` handle

Near the end of core initialization Runtime loads:

```text
IMAGES\TURNCD.BMP
```

and stores the image/resource handle here.

It is distinct from the one-shot `OMIKRON.BMP` startup splash.

---

# 7. Frame timing and input globals

`0x0041F740` provides a particularly clean cluster.

## Input

### `0x004E9718` — current input bitfield

Passed by address to `0x0043E0D0`, then read as current state.

```c
uint32_t g_inputCurrent;
```

### `0x004E9724` — previous input bitfield

Runtime copies the current state here after edge detection.

```c
uint32_t g_inputPrevious;
```

### `0x004E9720` — edge/filter mask

Used in the expression:

```c
pressed = ((previous & gate) ^ current) & current;
```

Exact meaning of each mask bit is not yet mapped.

### `0x004E971C` — newly pressed bitfield

Stores the result above.

```c
uint32_t g_inputPressedThisFrame;
```

Consumed and sometimes cleared by later game/interface logic.

## Raw timing

### `0x004E96FC`

Previous sample from timer source `0x004120F0`.

### `0x004E9714`

Raw delta:

```text
now - previous
```

with zero forced to `1`.

### `0x004E9700`

Smoothed delta:

```text
(previousSmoothed + rawDelta) / 2
```

again with a minimum of `1`.

### `0x0090E158`

Current raw timer snapshot.

This address is **not** inside `OMK_SAVE`.

## Converted timing floats

### `0x0090E174`

Float derived from raw delta.

### `0x0090E170`

Float derived from smoothed delta.

The exact unit should be verified before naming either value `deltaSeconds` or `fps`.

## Timing mode

### `0x004E972C`

A 16-bit mode in range `0..4` selects a jump-table branch.

Branches can force effective scalar values such as:

```text
3.0
1.0
0.5
0.1
2.0
```

or derive the value from timing.

Recommended:

```c
uint16_t g_frameTimeMode;
```

without naming the individual modes yet.

## `0x004C30D8` — effective frame time/step scalar

One of the most important timing globals.

It is:

- computed in `0x0041F740`;
- clamped or overridden;
- consumed throughout animation/effects/game logic.

Recommended:

```c
float g_frameTimeScale;
```

The original source-level unit/name is unknown.

### Copies

Late in the function:

```text
0x004C30DC = 0x004C30D8
0x004C30E0 = 0x004C30D8
```

Exact distinction between the two consumer copies remains unresolved.

### Overrides

```text
0x004C30E4
0x004C30E8
```

can override/modify the final scalar.

Their exact control semantics remain partial.

## Additional timing gate

```text
0x004E9730 previous result of 0x0042CC10
0x004E9734 current result of 0x0042CC10
0x004E9704 baseline float captured through 0x0042BC30
```

A `0 -> 1` transition captures a baseline; while active, elapsed state influences the effective time scalar.

The subsystem behind this gate still needs a final name.

## `0x004E9728` — frame/interaction guard

Used by both:

- Escape/interface logic;
- frame-time suppression/guard behavior.

A narrow name such as `g_escapeDisabled` would be misleading.

Keep it generic until all setters are mapped.

## `0x004C5944` — reset-next-frame timing

The message loop sets this to `1` when it had to sleep/wait.

The next active frame passes it to `0x0041F740` to reset timing baselines.

Recommended:

```c
uint32_t g_resetFrameTiming;
```

## `0x0090EF2E` — second frame-driver argument

Passed to `0x0041F740` by the main loop.

Exact meaning unresolved.

It lies after `OMK_SAVE`.

---

# 8. ScenarioEngine global state

The main state is concentrated around:

```text
0x004E61E8..0x004E6267
0x004E6Bxx..0x004E6Dxx
```

This should be modeled as a larger state structure, not dozens of independent scalars.

## `0x004E61E8..0x004E6267` — compact scenario-context registry

**Confirmed — Runtime.**

Initializer:

```text
0x00406270
```

clears:

```text
0x20 dwords
```

beginning at:

```text
0x004E61E8
```

Therefore the registry is exactly:

```c
RuntimeScenarioContext *g_scenarioContextRegistry[32];
```

Context creation at:

```text
0x00406290
```

scans this array for the first null pointer and stores the new `0x2C`-byte
scenario/event context there. It also writes the selected registry index into the context's byte field at `+0x1E`.

Context destruction clears the corresponding global registry slot.

The live `RuntimeScenarioContext` layout belongs in
`iam-scenario-vm.md`; the important global fact here is the fixed 32-pointer
registry.

## `0x004E6C74` — engine state

Used by the dispatcher/state machine around `0x00407DC0`.

```c
uint32_t g_scenarioEngineState;
```

**Confirmed behavior.**

## `0x004E6C9C` — reload/start-transition request

Mode 1 effectively performs:

```c
if (g_scenarioReloadRequested) {
    ScenarioEngine_Control(3, 0);
    g_scenarioReloadRequested = 0;
    ScenarioEngine_Control(2, 0);
}
```

This is central to startup.

## `0x004E6C7C` — special scenario/event script context

Opcode `0x46` saves the current context here specially when opening startup interface:

```text
0x1D / 29
```

It is definitely context-pointer-like.

Whether it is globally “the current script context” in every mode is not proven.

## `0x004E6B28` — opcode-`0x46` operand/global

The handler stores one decoded operand here.

Other paths reset it to `-1`.

Exact semantic meaning remains unknown.

## `0x004E6D94` — shared scenario/START state pointer

This field can now be promoted out of the unresolved ScenarioEngine cluster.

Runtime global-variable helpers:

```text
0x0040E510  set scenario/global variable
0x0040E530  get scenario/global variable
```

perform:

```text
state = *(void**)0x004E6D94
variables = *(int32_t**)(state + 0x08)
variables[id] = value
```

or the corresponding read.

A conservative partial type is:

```c
struct RuntimeScenarioGlobalState {
    uint8_t  unknown00[8];
    int32_t *globalVariables;   // +0x08
    // ...
};

RuntimeScenarioGlobalState *g_scenarioGlobalState; // 0x004E6D94
```

The pointed structure contains more than the variable array; other offsets are
used throughout the ScenarioEngine.

The crucial architectural result is:

```text
START/scenario variables are shared process/scenario state
```

and are **not** stored independently in each compact VM context.

IAM/START describes the backing array with `+0x08` as its beginning and
`+0x0C` as its end. In the studied retail data this is
`0x058C..0x1064`, exactly 694 signed dwords (IDs `0..693`). OpenNomad
therefore owns one checked `GameState` array per playthrough and injects it
into every compact context rather than copying Runtime's process-global
pointer architecture.

The compact handlers now using that shared state include:

| Opcode | Handler | ABI / effect |
|---:|---:|---|
| `0x0C` `SetGlobalVariableZero` | `0x00401EB0` | one `Scalar16`; writes zero |
| `0x56` `GetCharacterValueToVariable` | `0x00404230` | three `Scalar16`; character numeric value to global |
| `0x5D` `SetCharacterValueFromVariable` | `0x00404790` | three `Scalar16`; global to character numeric value |

For `0x56`/`0x5D`, character ID `-1` uses the separate global/session current
controlled-character identity. OpenNomad keeps numeric profiles by authored ID
for non-current bodies, but the selected body's START-style persistent
current-character fields are canonical. Selection promotes the shared full
AREA/SCENE definition into `GameState` without modifying immutable IAM bytes or
making the live render body persistent. Runtime's secondary HUD/player-status
notification after setters has no current OpenNomad equivalent and is
intentionally not fabricated.

## `0x004C0140` — compact scenario opcode descriptor table

**Confirmed — Runtime.**

The compact IAM scenario interpreter at:

```text
0x00406460
```

indexes an eight-byte static entry by opcode:

```c
struct RuntimeScenarioOpcodeDescriptor {
    void (*handler)(RuntimeScenarioContext *);
    uint32_t auxiliaryWord;
}; // 0x08
```

Table base:

```text
0x004C0140
```

Valid handler entries exist for:

```text
0x00 .. 0x98
```

Entry:

```text
0x99
```

contains:

```text
0xFFFFFFFF
0xFFFFFFFF
```

and acts as the static sentinel/end marker.

The second dword is deliberately named `auxiliaryWord`: direct handler
analysis proves it is **not** a reliable generic operand-byte count.

See `iam-scenario-vm.md` for the complete handler-address inventory.

## `0x006A05E0` — scenario VM probe mode

**Confirmed — Runtime.**

This is a 32-bit global repeatedly read by native compact-VM handlers.

Alternate event-scanning paths around:

```text
0x004060B0
0x00406120
0x00406180
```

set it while walking bytecode through the normal native handler table.

When nonzero, many side-effecting handlers decode/classify their instruction
but suppress the normal engine operation.

Recommended working name:

```c
uint32_t g_scenarioProbeMode;
```

This is best understood as:

```text
probe / dry-run / side-effect-suppression mode
```

until the original source-level terminology is recovered.

## `0x004C012C` — scenario event/native-operation status byte

This is explicitly byte-sized.

`EndEvent` at:

```text
0x00401B90
```

writes:

```text
0xFF
```

to it, and presentation/native-operation handlers also inspect it.

Its exact enum/flag semantics remain unresolved.

Keep a cautious name such as:

```text
g_scenarioEventStatus
```

rather than assigning a narrow “event complete” meaning.

## `0x004C0130` — special area/opcode execution gate

This dword is read/written by compact-VM native area-transition paths,
especially the currently provisional:

```text
opcode 0x2D
opcode 0x2F
```

The central interpreter also contains special handling associated with those
operations.

Recommended:

```text
g_scenarioAreaOpcodeGate
```

with a tentative semantic label until the area-transition lifecycle is fully
named.

## `0x004C013C` — current music-track ID

Music handler:

```text
0x00404FB0
```

compares the requested numeric track ID with this dword before starting new
music and updates it when the track changes.

This explains Runtime's observed behavior of avoiding a restart when the same
numeric track is already active.

Recommended:

```c
int32_t g_currentMusicTrackId;
```

## Wider cluster

Frequently used nearby state includes:

```text
0x004E6C90
0x004E6C98
0x004E6CA0

0x004E6D04
0x004E6D08
0x004E6D18
0x004E6D28
0x004E6D38
0x004E6D48

0x004E6D88
0x004E6D8C
0x004E6D90
```

These recur throughout `0x00407Dxx..0x0040Exxx`.

`0x004E6D94` is intentionally absent from this unresolved list now that its shared-scenario-state role and `+0x08` global-variable pointer are established.

Recommended Ghidra strategy:

```text
create ScenarioEngineGlobals covering the cluster
name only proven fields
leave the rest field_xx
```

---

# 9. Two-entry world/area context array

This corrects an earlier shorthand.

## Geometry

**Confirmed — Runtime.**

```text
base   = 0x009103E0
stride = 0x84
count  = 2
end    = 0x009104E8
```

The two record bases are:

```text
0x009103E0
0x00910464
```

The familiar addresses:

```text
0x009103E8
0x0091046C
```

are each `record + 0x08`.

## Partial record layout

```c
struct RuntimeWorldContext {
    int32_t  area_or_context_id;   // +0x00, exact meaning partial
    void    *model;                // +0x04
    Scenario scenario;             // +0x08

    // ...

    uint8_t model_loaded_flag;     // +0x6C
    uint8_t field_6D;
    uint8_t occupied_or_active;    // +0x6E
    // ...
}; // 0x84
```

Known behavior:

- `+0x00` is compared with requested context/area identifiers;
- `+0x04` holds a model/resource pointer;
- `+0x08` is passed to SCX/Scenario helpers;
- `+0x6C` is set after model-load work;
- `+0x6E` participates in occupancy/context selection.

## Correct naming of the embedded SCX fields

Use:

```text
g_worldContexts[0].scenario -> 0x009103E8
g_worldContexts[1].scenario -> 0x0091046C
```

rather than treating those addresses as standalone full contexts.

## Startup role

The startup area context eventually owns:

```text
GRID.3DO
GRID.SCX
```

while `aventure.scx` remains resident in the separate permanent scenario slot.

---

# 10. Permanent/shared SCX slot

## `0x00930780`

`0x0041B5A0("aventure.scx")` passes this address through:

```text
0x0044B140 scenario loaded/state query
0x0044AAC0 scenario unload
0x00449750 scenario load
```

At normal startup it contains the state for:

```text
SCPTDATA\aventure.scx
```

Recommended:

```c
Scenario g_permanentScenario;
```

Do **not** call it `g_currentScenario`: active world contexts contain their own separate scenarios.

---

# 11. SCX `Script_*` globals

This is distinct from the compact IAM scenario/event VM.

## `0x00903AE0` — Script context/mode byte

The SCX loader writes:

```text
5
```

during setup.

`Script_PlayScript` later reads it as execution/context gate state.

Exact enum values are unresolved.

## `0x00531218` — Script frame delta

Consumed by `Script_PlayScript` while advancing elapsed script state.

Recommended:

```c
float g_scriptFrameDelta;
```

Its exact mathematical relation to `0x004C30D8`, `0x0090E170`, and `0x0090E174` remains to be traced.

## `0x0067A0B4` — Script message callback

`Script_SendMessage` calls through this pointer.

Configured by Script/engine setup around `0x004A6BE0`.

Recommended:

```text
g_scriptMessageCallback
```

with signature still unresolved.

## `0x0067A0B8` — Script message target/context

Before invoking the callback, `Script_SendMessage` stores a resolved value here.

It is target/context/index-like, but exact type is not final.

---

# 12. Entity/actor pool

## Fixed geometry

**Confirmed — Runtime.**

```text
base   = 0x009106A0
stride = 0x520
count  = 100
end    = 0x00930720
```

An index helper validates:

```text
index < 100
```

and computes:

```text
base + index * 0x520
```

through:

```text
index * 41 << 5
```

Initialization clears exactly:

```text
100 × 0x520 = 0x20080 bytes
```

## Partially mapped fields

| Offset | Current interpretation | Confidence |
|---:|---|---|
| `+0xA4` | reference/pointer into associated `0x60`-byte record pool | Strong |
| `+0x50C` | signed/word handle/status | Unknown exact semantic |
| `+0x50E` | signed/word handle/status | Unknown exact semantic |
| `+0x510` | signed/word handle/status | Unknown exact semantic |
| `+0x514` | handle/status/pointer-like state | Unknown exact semantic |
| `+0x51B` | occupied/valid byte | Strong |

The entire `0x520` record should be typed as one `RuntimeEntity` in Ghidra and expanded gradually.

## `0x00930724` — central current entity pointer

Immediately after the fixed pool, this pointer is:

- initialized to null;
- assigned pointers to entity records;
- read hundreds of times by world/scenario/interface/render logic.

Safe conclusion:

```text
central current/primary entity-like pointer
```

Unsafe conclusion:

```text
always and exclusively the player character
```

Because Omikron supports body switching and multiple actor/control contexts, use:

```c
RuntimeEntity *g_currentEntity;
```

with an uncertainty comment.

## `0x00910690` — second entity pointer

Assigned selected entity records and used in switching/paired-control logic.

Possible meanings include controlled body, secondary actor, previous body, or selected actor.

Keep a neutral working name such as:

```text
g_secondaryEntityContext
```

until transitions settle it.

## OpenNomad current-body session state

OpenNomad does not expose either uncertain native entity pointer as a direct
player-character singleton. Compact IAM establishes the selected body through
opcode `0x38`; no startup helper, name lookup, model lookup, or external UI
assignment invents that identity.

The durable session value is:

```text
ControlledCharacterRef {
    int16 authoredCharacterId;
    uint32 owningWorldSceneId;
}
```

The world ID follows the unique live `RuntimeCharacter` when `0x47` hands the
body to a prepared destination world. It is intentionally separate from the
two resident AREA slots and from presentation activation. A selected body may
be hidden by `0x4F/-1`, re-enabled by `0x4E/-1`, or remain materialized while a
different body becomes selected; none of those operations reloads its shared
model resource.

## Associated `0x60`-byte pool at `0x004E7EA0`

Initialization clears:

```text
0x480 dwords = 0x1200 bytes
```

Index calculation subtracts the base and divides by:

```text
0x60
```

Therefore:

```text
base   = 0x004E7EA0
stride = 0x60
count  = 48
```

is confirmed.

Entity field `+0xA4` can reference records here.

The record's exact role remains unresolved; a body/object/runtime-descriptor interpretation is plausible but not yet final.

## Post-entity control cluster

After the pool are several repeatedly used/reset globals:

```text
0x00930728
0x00930730
0x00930734
0x00930738
0x00930744
0x0093074C
0x00930754
0x0093076C  attached-decor linked-list head
```

`0x0093076C` is confirmed by the attach/remove/reverse helpers. Each attached
decor links to the next through `+0x17C`; `0x00441170` appends, `0x00441200`
removes, and `0x004412A0` reverses the chain during AREA event 9. The remaining
fields should still be treated as an entity/world control cluster until decisive
writers identify them.

---

# 13. Loaded 3DO model pool

## Fixed backing pool

**Confirmed at `0x0044EC80`.**

```text
base   = 0x008F5EC0
stride = 0x1A8
count  = 128
end    = 0x009032C0
```

The loader scans:

```asm
mov eax, 0x008F5EC0
cmp dword ptr [eax], 0
...
add eax, 0x1A8
cmp eax, 0x009032C0
```

A zero dword at record `+0x00` identifies a free slot.

## The old `0x6A` shorthand

Runtime clears a selected slot with:

```asm
mov ecx, 0x6A
rep stosd
```

Therefore:

```text
0x6A dwords × 4 = 0x1A8 bytes
```

`0x6A` is the dword count, **not** the byte stride.

## Known fields

| Offset | Meaning | Confidence |
|---:|---|---|
| `+0x00` | occupancy/root-loaded field; zero means free | Strong |
| `+0x2C` | pointer to allocated/read core file block | Confirmed |
| `+0x30` | resource/model name string begins here | Confirmed |
| remaining | runtime model/material/texture state | documented separately in `3do.md` |

## Separate pointer registry: `0x00907100`

The loader scans:

```text
0x00907100 .. 0x009072FF
```

in 4-byte steps.

That is exactly:

```text
128 pointer slots
```

Each non-null pointer refers to a loaded 3DO record.

Name lookup compares:

```text
registry[i]->name at +0x30
```

to the requested name.

Recommended:

```c
Loaded3DOModel *g_loaded3DORegistry[128];
```

This registry is distinct from the fixed backing pool.

## Related fixed 3DA animation pool — `0x0054ECB8`

The 3DA loaders around:

```text
0x0046E690
0x0046E8B0
```

scan fixed runtime slots beginning at:

```text
0x0054ECB8
```

in steps of:

```text
0x10
```

until:

```text
0x00550CB8
```

Therefore:

```text
base   = 0x0054ECB8
stride = 0x10
count  = 512
end    = 0x00550CB8
```

The recovered runtime slot is:

```c
struct RuntimeAnimation3DA {
    uint32_t lastFrame;          // +0x00
    uint32_t trackCount;         // +0x04
    Runtime3DATrack *tracks;     // +0x08
    void *backingAllocation;     // +0x0C
}; // 0x10
```

The serialized 3DA itself contains only its header, track descriptors and
offset-addressed sample streams; this `0x10` structure is Runtime's live slot.

Recommended global label:

```c
RuntimeAnimation3DA g_animation3DAPool[512];
```

See `3da.md` for serialization, binding and root-motion semantics.

---

# 14. SpriteInstance pool

## Pool pointer and capacity

Initializer:

```text
0x0048EB80
```

allocates:

```text
capacity × 0x40
```

Globals:

```text
0x00660B5C -> pool pointer
0x00660B58 -> uint16_t capacity
```

Core startup passes:

```text
0x800
```

so normal capacity is:

```text
2048 SpriteInstance records
```

## Shutdown

`0x0048EBC0`:

- frees the pool if non-null;
- sets pointer to null;
- sets capacity to zero.

This is a clean ownership pair.

## Slot occupancy and defaults

Creation at `0x0048EBF0` scans `0x40`-byte entries.

Field:

```text
0x04
```

nonzero means occupied.

After clearing a slot, Runtime initializes at least:

```text
0x04 = owner/model resource
0x16 = 0xFFFF
0x18 = 1.0f
0x1C = 1.0f
0x24 = 0.9f
0x28 = 0
0x2C = 0
0x30 = 0x00FFFFFF
```

The detailed sprite structure belongs in sprite/3DO documentation.

## Additional sprite/palette scratch state

An eight-slot status/scratch area exists around:

```text
0x006A2EE8
```

with related data nearby.

It participates in sprite palette/morph operations.

Exact record boundaries remain unresolved.

---

# 15. Lighting/object overlap subsystem

Initializer:

```text
0x0048DCE0
```

Shutdown:

```text
0x0048DE00
```

Core initialization passes:

```text
0x12C = 300
```

The function also hardcodes the other dimension to 300.

## Capacities

```text
0x00660B38 -> requested/light-side capacity
0x00660B50 -> object-side capacity, initialized to 300
```

At normal startup both are 300.

## Light boxes: `0x00660B2C`

Allocation:

```text
lightCapacity × 0x68
```

Failure diagnostic explicitly names:

```text
light boxes
```

Recommended:

```text
g_lightBoxes
```

with provisional record size `0x68`.

## Object boxes: `0x00660B4C`

Allocation:

```text
objectCapacity × 0x68
```

Diagnostic names:

```text
object boxes
```

Recommended:

```text
g_objectBoxes
```

## Pair records: `0x00660B54`

Allocation:

```text
lightCapacity × objectCapacity × 0x0C
```

Diagnostic names:

```text
LightPairs
```

At 300×300:

```text
1,080,000 bytes
```

Recommended:

```text
g_lightPairs
```

with provisional record size `0x0C`.

## Overlap status: `0x00660B30`

Allocation:

```text
lightCapacity × objectCapacity × 1 byte
```

At startup:

```text
90,000 bytes
```

Diagnostic identifies overlap status.

## Object reset flags: `0x00660B34`

Allocation:

```text
objectCapacity × 1 byte
```

Diagnostic identifies:

```text
object resetflag
```

## Additional counters/heads

```text
0x00660B40
0x00660B44
0x00660B48
```

are reset to zero on init and shutdown.

Exact roles are unresolved.

## Ownership

`0x0048DE00` individually frees the five allocations and zeros the pointers/capacities.

This entire group should become one modern lighting-overlap subsystem object.

---

# 16. Core 3D renderer buffers

Initialization around `0x004403E0` reports useful failure strings.

## `0x009070B8` — SortArrayLo

Allocation:

```text
0x4000 × 4 = 0x10000 bytes
```

Diagnostic:

```text
SortArrayLo
```

Recommended:

```text
g_sortArrayLo
```

Element type should still be verified from sorting consumers.

## `0x009070E0` — Vertex2D render buffer

Allocation:

```text
vertexCapacity × 0x30
```

Diagnostic:

```text
Vertex2D (Render Buffer)
```

Recommended:

```text
g_vertex2DRenderBuffer
```

with provisional:

```text
sizeof(RuntimeVertex2D) = 0x30
```

## `0x00907320` — Face3D render buffer

Allocation:

```text
faceCapacity × 0x7C
```

Diagnostic:

```text
Face3D (Render Buffer)
```

Recommended:

```text
g_face3DRenderBuffer
```

with provisional:

```text
sizeof(RuntimeFace3D) = 0x7C
```

## Capacity globals

```text
0x00907308 vertex/render capacity
0x009070D4 face/render capacity
0x009070B4 working/copy of vertex capacity
```

The exact bookkeeping distinction needs more tracing.

## `0x009070CC` — conversion mapping table

Allocation:

```text
256 × 4 bytes
```

Diagnostic calls it:

```text
conv mapping table
```

It is initialized from a numeric conversion formula.

Element type should remain cautious until all consumers are typed.

---

# 17. Material, indexed-texture, and palette-page memory

Allocator around `0x004406B0` constructs the page system used by 3DO/3DT.

## Counts

```text
0x0090731C -> texture page count
0x00907318 -> total texture + palette/material page count
```

Conceptually:

```text
totalPageCount = texturePages + extraPalettePages
```

## Raw texture-page allocation: `0x009070C8`

Allocation size:

```text
(texturePageCount + 1) << 16
```

The extra page allows alignment.

## Aligned indexed-page base: `0x009070B0`

Runtime aligns a usable pointer to:

```text
0x10000 = 65536 bytes
```

Each page is:

```text
256 × 256 × 1 indexed byte = 65536 bytes
```

3DO/3DT loading uses:

```text
base + pageIndex * 0x10000
```

Recommended pair:

```text
g_indexedTexturePagesRaw -> 0x009070C8
g_indexedTexturePages    -> 0x009070B0
```

## Texture-page descriptors: `0x009070DC`

Allocation:

```text
texturePageCount × 0x24
```

Runtime iterates in `0x24`-byte strides.

Known fields include occupancy/resource metadata, but full schema is not complete.

## RGB palette pages: `0x009070BC`

Allocation:

```text
totalPageCount × 256 × 3 bytes
```

Therefore one palette page is:

```text
0x300 bytes
```

Recommended:

```text
g_palettePageRgb
```

## Material/palette page descriptors: `0x00907324`

Allocation:

```text
totalPageCount × 0x20
```

Used heavily by palette/material loading.

Recommended:

```text
g_materialPages
```

with provisional record size `0x20`.

---

# 18. Converted 15/16-bit palette memory

Setup around `0x00483B80` creates another allocation used by the renderer's native display format.

## `0x0065FE70` — raw allocation pointer

Stores the original heap pointer for later freeing.

## `0x00657D98` — aligned converted palette base

Stores an aligned pointer inside the raw allocation.

`0x00483C80` indexes it by palette slot with a stride around:

```text
0x2000
```

and fills converted colors using:

```text
g_palettePageRgb
screen pixel format
gamma/conversion tables
```

Recommended:

```text
g_convertedPaletteAllocation
g_convertedPaletteBase
```

## `0x0065DE44`

Reset by palette cleanup.

Clearly palette-renderer state, exact semantics unresolved.

---

# 19. Renderer backend callbacks

Functions:

```text
0x0042F9A0
0x0042FA00
0x0042FA70
```

select between two callback variants.

## Backend index: `0x0052B8D8`

`0x0042FA60` is a trivial getter for this global.

`0x0042FA00(index)` installs callbacks and stores the index.

`0x0042FA70` toggles to the other variant.

Recommended:

```c
uint32_t g_rendererBackendIndex;
```

## Static callback tables

Bases:

```text
0x004C4910
0x004C4918
0x004C4920
0x004C4928
0x004C4930
0x004C4938
```

Each is indexed by:

```text
backendIndex * 4
```

and currently has two variants.

## Active mutable callback slots

Selected entries are copied to:

```text
0x0090E09C
0x0090E0A8
0x0090E0A4
0x0090E0AC
0x0090E0A0
```

### `0x0090E0A8` — texture upload callback

Called by `0x004A75E0` after indexed texture data has been loaded/decompressed/prepared.

Recommended:

```text
g_upload3DOTexturePage
```

### `0x0090E0A4` — palette upload callback

Called by `0x004A7900`.

Recommended:

```text
g_upload3DOPalette
```

### Other three slots

They are definitively backend-selected function pointers.

Exact roles still require full call-site classification.

Do not give them semantic names from table position alone.

---

# 20. Renderer configuration and Direct3D globals

## `0x004C951C` — renderer configuration byte

`0x0045EF20` is effectively:

```c
g_rendererConfigByte = argument;
```

Core startup passes:

```text
0x0090E724
```

to it.

Exact meaning unresolved.

## `0x0053ADF0` — renderer mode/state

Setter:

```text
0x0045EF40
```

Getter:

```text
0x0045EF50
```

Renderer initialization `0x0045EF60` also stores its argument here.

Core startup passes:

```text
0x0090E725
```

Recommended:

```text
g_rendererMode
```

## `0x0053AAC0` — renderer/device status byte

Getter:

```text
0x0045EF30
```

Initialization sets it to:

```text
0xFF
```

Meaning unknown.

## `0x0080B068` — Direct3D device-like pointer

Renderer initialization clears it.

Rendering functions dereference it as a COM object.

A vtable call at offset:

```text
0x58
```

receives values behaving like:

```text
state = 0x0E
value = 0 or 1
```

`0x0E` matches `D3DRENDERSTATE_ZWRITEENABLE` in the legacy Direct3D API family, making the active-device interpretation strong.

Recommended:

```text
g_d3dDevice
```

but leave the exact interface revision (`IDirect3DDevice2/3/...`) annotated as unresolved until the vtable is completely typed.

## `0x008F56D8` — cached render state

Code compares this value before changing the device state above and updates it after successful calls.

Working interpretation:

```text
cached Z-write enable state
```

Strong but not source-symbol confirmed.

## Wider DirectX state cluster

Renderer init zeroes many fields together, including:

```text
0x0080B060
0x0080B064
0x0080B068
0x0080B078
0x0080B07C
0x0080B084
```

plus state in:

```text
0x007CADxx
0x0053ADxx
0x006A53xx
```

These likely contain DirectDraw/Direct3D objects, surfaces, viewport/device state, and renderer bookkeeping.

They warrant a dedicated DirectX-global structure pass.

---

# 21. 3DO/3DT texture-loading transient globals

These exist largely because Runtime's loader uses implicit global context and static scratch.

OpenNomad should make most of this local/per-load state.

## `0x0068A580` — current model during texture loading

`0x004A75E0` reads this pointer to locate the current model/material array and derive material indexes.

Recommended:

```text
g_currentTextureLoadModel
```

This is transient loader context, not long-lived ownership.

## `0x0068A578` / `0x0068A57C`

Temporary counts/state during material-reference collection.

Exact semantics unresolved.

## `0x0067A0C8`

Dword array indexed using a material-derived index during texture/palette loading.

It participates in palette-slot/index rebasing for low-bit-depth textures.

Exact semantics are not yet safe enough for `paletteOffsets[]` or similar.

## `0x0067A3E8` — primary staging address

Used as:

- file-read target;
- raw indexed texture source;
- compressed texture input;
- source of a known `0x10000`-byte copy.

It is part of process-global texture-loader scratch.

## `0x006823E8` — secondary decompression staging address

Used as a decompression destination in some paths.

Important:

```text
0x006823E8 - 0x0067A3E8 = 0x8000
```

while the first address can participate in a 64-KiB copy.

Therefore they should **not** yet be declared as two separate independent 64-KiB arrays.

Safer description:

```text
two working addresses inside an overlapping/larger static texture scratch arena
```

until linker/BSS symbol boundaries are reconstructed.

---

# 22. Runtime interface-instance globals

## Fixed pool

Generic interface code iterates:

```text
base   = 0x004E9818
stride = 0x7C
count  = 3
end    = 0x004E998C
```

Recommended:

```c
RuntimeInterface g_interfaceInstances[3];
```

## Partial field map

| Offset | Current interpretation | Confidence |
|---:|---|---|
| `+0x00` | interface identity/ID | Strong |
| `+0x08` | active/owned state or pointer | Strong, type partial |
| `+0x0C` | callback/initializer-related field | Strong |
| `+0x14` | callback used by close/conflict logic | Strong |
| `+0x2C` | relation/link to another interface instance | Tentative |
| `+0x38` | image/resource handle | Strong |
| `+0x70` | flags | Strong |
| `+0x74` | state/flags used by pool scans | Strong |

The full `0x7C` schema is still open.

## Static descriptor table: `0x004CB640`

`Interface_Open` scans a static table rooted here for interface definitions.

Interface `29` resolves through this system to the main-menu initializer at:

```text
0x00479D10
```

Conceptual distinction:

```text
0x004CB640 -> immutable/static interface class/descriptor data
0x004E9818 -> mutable runtime interface instances
```

---

# 23. Diagnostics/failure-support globals

## `0x0052C8F8` — protected initialization context

The top-level initializer establishes a setjmp/exception-like failure boundary using storage around this address via a helper near:

```text
0x004BBC18
```

Exact structure extent is not yet mapped.

Use an opaque label such as:

```text
g_initializationJumpContext
```

## `0x0053AAD8` — HRESULT/DirectX error-text buffer

A large HRESULT-to-string routine repeatedly:

1. selects a static error string;
2. copies it to `0x0053AAD8`;
3. returns `0x0053AAD8`.

This explains the address's very high static reference count.

Recommended:

```text
g_hresultTextBuffer
```

Exact array length should be derived before applying a fixed-size array type.

---

# 24. Dense software-renderer clusters not yet structurally solved

Runtime's software/legacy renderer uses substantial process-global scratch.

Dense clusters include:

```text
0x00657Dxx
0x0065DDxx..0x0065DExx
0x0065FExx
0x006A05xx
0x006A4700..0x006A4710
0x006A4Cxx
```

These participate in combinations of:

- transformed vertex state;
- scan conversion;
- rasterization;
- texture/palette lookup;
- clipping;
- screen/pitch/framebuffer state.

## Documentation rule

Until a cluster has a clear initializer, extent, and field schema, prefer:

```text
SoftwareRasterizerGlobals + offset
```

over dozens of confidently named one-off `g_x/g_y/g_z` fields.

A dedicated renderer pass should:

1. identify clear/initialization functions;
2. infer extents from loops;
3. separate persistent renderer configuration from per-polygon scratch;
4. identify screen/pitch/framebuffer pointers;
5. reconstruct structures before renaming individual fields.

---

# 25. Static/read-only tables that are not mutable runtime globals

Important absolute addresses are sometimes data tables rather than runtime state.

## Renderer backend tables

```text
0x004C4910
0x004C4918
0x004C4920
0x004C4928
0x004C4930
0x004C4938
```

## Interface descriptors

```text
0x004CB640
```

## Default-state tables

```text
0x004C8F90
0x004C9070
0x004C9150
```

These are copied into `OMK_SAVE` regions.

## Ghidra rule

If an address:

- is indexed by a small enum;
- contains pointers into `.text`;
- is read frequently;
- has no meaningful writers;

test whether it is a static dispatch/default table before naming it mutable state.

---

# 26. Ownership and lifetime summary

| Subsystem | Global owner state | Initialized by | Freed/reset by | Lifetime |
|---|---|---|---|---|
| persistent config | `0x0090E180`, `0xDA8` bytes | `0x0041F4C0`, persistence overlay `0x00409200` | persistence/process paths | process |
| Win32 windows | `0x004E7804/08/0C`, `0x004E768C` | WinMain/window creation | window/main-close paths | process |
| ScenarioEngine | `0x004E6Bxx..0x004E6Dxx` | mode 0 / startup | mode 3/4 and shutdown transitions | process with transitions |
| world contexts | `0x009103E0`, `2×0x84` | area/bootstrap loaders | context unload/recycle | area |
| permanent SCX | `0x00930780` | `0x0041B5A0` | scenario unload/main close | long-lived |
| entity pool | `0x009106A0`, `100×0x520` | entity subsystem init | entity subsystem reset | engine/world |
| 3DO storage | `0x008F5EC0`, `128×0x1A8` | static BSS + per-slot loader | `0x00441A00` per model | resource |
| 3DO registry | `0x00907100`, 128 pointers | model registration | model unload | resource |
| SpriteInstance pool | `0x00660B5C`, capacity at `0x00660B58` | `0x0048EB80` | `0x0048EBC0` | engine |
| lighting overlap | `0x00660B2C..0x00660B54` | `0x0048DCE0` | `0x0048DE00` | engine |
| renderer buffers | `0x009070B8`, `0x009070E0`, `0x00907320` | `0x004403E0` and helpers | renderer/main-close paths | engine |
| texture/palette pages | `0x009070B0..0x00907324` | `0x004406B0` | material/renderer shutdown | engine |
| converted palettes | `0x0065FE70`, `0x00657D98` | `0x00483B80` | palette cleanup | engine |
| renderer callbacks | `0x0090E09C..0x0090E0AC` | `0x0042F9A0` / `0x0042FA00` | overwritten on backend switch | engine |
| Script messaging | `0x0067A0B4/B8` | Script subsystem setup | subsystem reset | engine |
| interfaces | `0x004E9818`, `3×0x7C` | interface open/setup | interface close | dynamic |
| timing/input | `0x004E96FC..0x004E9734`, `0x004C30D8..E8` | frame driver | overwritten each frame | per-frame |

Where a shutdown address is not securely identified, the table intentionally uses a subsystem description rather than inventing one.

---

# 27. Recommended Ghidra labels now

The following labels are strong enough to improve decompilation immediately.

```text
DAT_004e7804 -> g_applicationInstance
DAT_004e7808 -> g_renderWindow
DAT_004e780c -> g_hostWindow

DAT_0052dd28 -> g_queryCancelAutoPlayMessage
DAT_0052dd54 -> g_moviePlaybackState
DAT_0052b8d8 -> g_rendererBackendIndex

DAT_0090e180 -> g_omkSave
DAT_00910309 -> g_viewerMode
DAT_0091030a -> g_windowedModeActive
DAT_0091030b -> g_windowedModeRequested
DAT_009103cd -> g_noFmv

DAT_009103e0 -> g_worldContexts
DAT_00930780 -> g_permanentScenario

DAT_009106a0 -> g_entityPool
DAT_00930724 -> g_currentEntity
DAT_004e7ea0 -> g_entityAssociatedRecords

DAT_008f5ec0 -> g_loaded3DOPool
DAT_00907100 -> g_loaded3DORegistry

DAT_00660b5c -> g_spriteInstancePool
DAT_00660b58 -> g_spriteInstanceCapacity

DAT_00660b2c -> g_lightBoxes
DAT_00660b4c -> g_objectBoxes
DAT_00660b54 -> g_lightPairs
DAT_00660b30 -> g_lightPairOverlapStatus
DAT_00660b34 -> g_objectLightResetFlags

DAT_009070b8 -> g_sortArrayLo
DAT_009070e0 -> g_vertex2DRenderBuffer
DAT_00907320 -> g_face3DRenderBuffer
DAT_009070cc -> g_conversionMappingTable

DAT_009070c8 -> g_indexedTexturePagesRaw
DAT_009070b0 -> g_indexedTexturePages
DAT_009070dc -> g_texturePageDescriptors
DAT_009070bc -> g_palettePageRgb
DAT_00907324 -> g_materialPages
DAT_0090731c -> g_texturePageCount
DAT_00907318 -> g_totalMaterialPageCount

DAT_0065fe70 -> g_convertedPaletteAllocation
DAT_00657d98 -> g_convertedPaletteBase

DAT_0090e0a8 -> g_upload3DOTexturePage
DAT_0090e0a4 -> g_upload3DOPalette

DAT_0080b068 -> g_d3dDevice
DAT_008f56d8 -> g_cachedZWriteState
DAT_0053adf0 -> g_rendererMode

DAT_004e9718 -> g_inputCurrent
DAT_004e9724 -> g_inputPrevious
DAT_004e9720 -> g_inputEdgeMask
DAT_004e971c -> g_inputPressedThisFrame
DAT_004e96fc -> g_previousRawTime
DAT_004e9714 -> g_rawFrameDelta
DAT_004e9700 -> g_smoothedFrameDelta
DAT_004e972c -> g_frameTimeMode
DAT_004c30d8 -> g_frameTimeScale
DAT_004c5944 -> g_resetFrameTiming

DAT_004e6c74 -> g_scenarioEngineState
DAT_004e6c9c -> g_scenarioReloadRequested
DAT_004e6c7c -> g_specialScenarioScriptContext

DAT_00531218 -> g_scriptFrameDelta
DAT_0067a0b4 -> g_scriptMessageCallback
DAT_0067a0b8 -> g_scriptMessageTarget

DAT_004e9818 -> g_interfaceInstances
DAT_004cb640 -> g_interfaceDescriptors

DAT_0053aad8 -> g_hresultTextBuffer
```

## Labels that should remain explicitly provisional

Do not yet harden names such as:

```text
g_player
g_playerEntity
g_currentLevel
g_currentScx
g_direct3DDevice3
g_deltaSeconds
g_fps
g_paletteOffsets
```

unless the narrower semantic is independently proven.

---

# 28. Corrections to earlier project shorthand

## World SCX “slots”

Earlier notes sometimes called:

```text
0x009103E8
0x0091046C
```

the two world-context slots.

More precise:

```text
world-context array base: 0x009103E0
record size:              0x84
count:                    2

SCX field in entry 0:     0x009103E8
SCX field in entry 1:     0x0091046C
```

## Loaded-3DO `0x6A` stride

Correct interpretation:

```text
0x6A = dword count cleared
0x6A × 4 = 0x1A8 bytes
```

Actual pool:

```text
128 × 0x1A8
```

## `0x00930724` is not yet proven to mean “player”

It is a central entity pointer.

Because body switching and actor/control ownership are central to Omikron, use a more neutral label until observed transitions prove the invariant.

## `OMK_SAVE` ends at `0x0090EF28`

Nearby later globals are not persistence fields by proximity alone.

## Texture staging may overlap

`0x0067A3E8` and `0x006823E8` should be treated as working addresses in a static scratch arena until exact symbol boundaries are known.

---

# 29. OpenNomad architectural guidance

The Runtime global map is evidence, **not a target architecture**.

## Convert ownership clusters into subsystem members

For example:

```cpp
class SpriteSystem {
    std::vector<SpriteInstance> instances;
};

class LightingSystem {
    std::vector<LightBox> lightBoxes;
    std::vector<ObjectBox> objectBoxes;
    std::vector<LightPair> pairs;
    std::vector<uint8_t> overlap;
    std::vector<uint8_t> resetFlags;
};

class Renderer {
    RendererBackend backend;
    TexturePageStore texturePages;
    PaletteStore palettes;
    RenderBuffers buffers;
};

class ScenarioEngine {
    Scenario permanentScenario;
    std::array<WorldContext, 2> worldContexts;
    ScenarioEngineState state;
};

class EntitySystem {
    std::vector<Entity> entities;
    Entity* current;
};
```

The precise API is not prescribed; the point is ownership locality.

## Turn implicit global dependencies into explicit context

A decompiled function may appear to take one argument while implicitly consuming:

```text
g_currentEntity
g_frameTimeScale
g_currentTextureLoadModel
active renderer callbacks
global page descriptors
global scratch memory
```

A modern implementation should make those dependencies explicit where practical.

## Preserve original limits as documented behavior

Confirmed fixed capacities include:

```text
128 loaded 3DO records
100 entity records
3 interface instances
2 world contexts
2048 SpriteInstance capacity at normal startup
300×300 light/object pair grid
```

OpenNomad may choose dynamic containers, but the original limit should remain documented for compatibility and failure-behavior testing.

## Keep serialized and live state distinct

Examples:

```text
OMK_SAVE serialized state
SCX serialized script records vs live instances
3DO on-disk blocks vs 0x1A8 runtime records
static interface descriptors vs 0x7C runtime instances
```

Do not serialize modern native pointers just because Runtime used pointer-rich globals.

---

# 30. Useful debugger watchpoints

## Startup/configuration

```text
0x0090E180 OMK_SAVE
0x00910309 viewer mode
0x0091030A active windowed mode
0x0091030B requested windowed mode
0x009103CD no-FMV
```

## Scenario startup

```text
0x00930780 permanent scenario
0x009103E0 world context 0
0x00910464 world context 1

0x004E6C74 ScenarioEngine state
0x004E6C9C reload/start request
0x004E6C7C special scenario script context
```

## Entities

```text
0x00930724 current entity pointer
0x00910690 secondary entity context
0x009106A0 entity pool
```

A write watchpoint on `0x00930724` during:

- main menu;
- New Game;
- Kay'l arrival;
- first player-control handoff;
- later body switching;

would be particularly valuable.

## 3DO

```text
0x008F5EC0 first loaded-model record
0x00907100 first registry pointer
0x0068A580 current model during texture load
```

## Texture/palette

```text
0x009070B0 indexed texture-page base
0x009070BC RGB palette pages
0x00657D98 converted 16-bit palette base
0x0090E0A8 texture upload callback
0x0090E0A4 palette upload callback
0x0052B8D8 renderer backend index
```

## Sprites

```text
0x00660B5C pool pointer
0x00660B58 pool capacity
```

## Timing/input

```text
0x004E9718 current input
0x004E971C pressed this frame
0x004C30D8 frame scalar
0x004E972C timing mode
```

---

# 31. High-priority open questions

## Exact entity/current-player semantics

Resolve:

```text
0x00930724
0x00910690
```

across body ownership changes.

## Full `RuntimeEntity` `0x520` schema

High-value fields still needed:

- transform;
- model/body;
- collision;
- animation;
- scenario/script links;
- control ownership;
- health/status;
- area/world context;
- object links.

## Exact `0x60` associated-record pool role

Determine what the 48 records at:

```text
0x004E7EA0
```

represent and why entity `+0xA4` references them.

## Full `WorldContext` `0x84` schema

Map:

- context/area ID;
- model pointer;
- embedded Scenario;
- occupancy;
- transition fields;
- unload state.

## Full permanent `Scenario` runtime type

`0x00930780` is structurally proven as a Scenario/SCX state base, but the whole runtime structure is not yet fully typed.

## DirectX interface cluster

Recover exact types around:

```text
0x0080B060..0x0080B088
0x007CADxx
0x0053ADxx
```

including DirectDraw, Direct3D, surfaces, viewport, z-buffer, and device revision.

## Remaining renderer callback slots

Identify exact semantics of:

```text
0x0090E09C
0x0090E0A0
0x0090E0AC
```

for both backend variants.

## Page descriptor schemas

Complete:

```text
0x24-byte texture-page descriptors
0x20-byte material/palette-page descriptors
```

## Software renderer state

Reconstruct dense `0x0065xxxx` / `0x006Axxxx` clusters as structures rather than scalars.

## Runtime option bytes

Map `0x00910308..0x00910329` against config-dialog controls and preference getters/setters.

## Full `OMK_SAVE` field schema

Only part of the `0xDA8`-byte block has semantic names.

## Script timing relation

Determine the exact mathematical relation among:

```text
0x00531218
0x004C30D8
0x0090E170
0x0090E174
```

## Interface instance schema

Complete the `0x7C` runtime instance and static descriptor-table schema.

---

# 32. Methodology for expanding the map

For each candidate global, record:

```text
address
width/type
initial value
decisive writers
important readers
owner subsystem
allocation extent
free/reset function
serialized or runtime-only
persistent or scratch
confidence
```

## Prefer decisive writers over raw reference count

Examples:

```text
0x00660B5C
    allocation = capacity × 0x40
    -> SpriteInstance pool

0x008F5EC0
    scan stride = 0x1A8
    clear = 0x6A dwords
    -> fixed 3DO pool geometry

0x009106A0
    validated index < 100
    index × 0x520
    -> entity pool geometry
```

## Use diagnostic strings as structural evidence

Runtime includes useful names such as:

```text
SortArrayLo
Vertex2D (Render Buffer)
Face3D (Render Buffer)
light boxes
object boxes
LightPairs
overlap status
object resetflag
```

These are much stronger than naming from nearby arithmetic alone.

## Reconstruct array bases before naming repeated addresses

The world-context correction is the canonical example:

```text
0x009103E8 and 0x0091046C
```

differ by `0x84`, and both are `+0x08` inside repeated records.

---

# 33. Minimal master map

```text
PROCESS / WINDOW
  0x004E7804  HINSTANCE
  0x004E7808  primary HWND
  0x004E780C  host HWND
  0x0052DD28  QueryCancelAutoPlay message

PERSISTENT CONFIG
  0x0090E180  OMK_SAVE, 0xDA8 bytes
  0x0090E188  version
  0x0090E18C  width
  0x0090E18E  height

RUNTIME OPTIONS
  0x00910309  viewer
  0x0091030A  windowed active
  0x0091030B  windowed requested
  0x009103CD  no FMV

SCENARIO
  0x004E6Bxx..0x004E6Dxx ScenarioEngine globals
  0x004E6C74 state
  0x004E6C9C reload/start request
  0x004E6C7C special script context
  0x009103E0 world contexts[2], stride 0x84
  0x00930780 permanent scenario

ENTITIES
  0x009106A0 entities[100], stride 0x520
  0x00930724 current entity pointer
  0x00910690 secondary entity pointer
  0x004E7EA0 associated records[48], stride 0x60

3DO
  0x008F5EC0 loaded-model records[128], stride 0x1A8
  0x00907100 loaded-model registry[128]

SPRITES
  0x00660B5C SpriteInstance pool
  0x00660B58 capacity

LIGHTING
  0x00660B2C light boxes
  0x00660B4C object boxes
  0x00660B54 light pairs
  0x00660B30 overlap matrix
  0x00660B34 reset flags

RENDER BUFFERS
  0x009070B8 SortArrayLo
  0x009070E0 Vertex2D buffer
  0x00907320 Face3D buffer
  0x009070CC conversion table

TEXTURE / PALETTE
  0x009070C8 raw indexed-page allocation
  0x009070B0 aligned indexed pages
  0x009070DC 0x24 page descriptors
  0x009070BC RGB palette pages
  0x00907324 0x20 material/page descriptors
  0x0065FE70 raw converted-palette allocation
  0x00657D98 aligned converted-palette base

RENDER BACKEND
  0x0052B8D8 backend index
  0x0090E0A8 texture upload callback
  0x0090E0A4 palette upload callback
  0x0080B068 D3D-device-like pointer
  0x0053ADF0 renderer mode

INPUT / TIME
  0x004E9718 input current
  0x004E9724 input previous
  0x004E9720 input edge mask
  0x004E971C pressed this frame
  0x004E96FC previous timer
  0x004E9714 raw delta
  0x004E9700 smoothed delta
  0x004E972C time mode
  0x004C30D8 effective frame scalar

SCX SCRIPT
  0x00531218 Script frame delta
  0x0067A0B4 SendMessage callback
  0x0067A0B8 SendMessage target/context
  0x00903AE0 Script context mode

INTERFACES
  0x004E9818 runtime instances[3], stride 0x7C
  0x004CB640 static descriptor table
```

---

# 34. Current reverse-engineering boundary

The global map is now strong enough to identify several major architectural owners:

```text
persistent configuration
Win32/application state
frame timing/input
ScenarioEngine
world contexts
entity system
3DO resource system
sprite system
lighting overlap system
renderer buffers
texture/palette page manager
renderer backend callbacks
SCX Script messaging
interface system
```

The remaining work is increasingly about reconstructing the **structures around those globals** rather than discovering isolated addresses:

```text
RuntimeEntity        0x520
WorldContext         0x84
Loaded3DO runtime    0x1A8
InterfaceInstance    0x7C
Entity-associated    0x60
TexturePageDesc      0x24
MaterialPageDesc     0x20
ScenarioEngineGlobals
DirectX globals
OMK_SAVE             0xDA8
```

Those structures should become the next durable Ghidra types and the next clean subsystem boundaries in OpenNomad.
