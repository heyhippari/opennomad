
# IAM / Quantic C 32-bit Script Function Catalogue

> **Status:** reverse-engineering reference for OpenNomad  
> **Last updated:** 2026-08-22  
>
> This document reconstructs the 32-bit function-ID catalogue used by the `Script_*`
> subsystem in the Windows retail build of *Omikron: The Nomad Soul*. Historical
> Quantic Dream material strongly indicates that these functions are the runtime
> side of the designer-facing **IAM** adventure authoring system and its internal
> scripting representation, described by Quantic Dream as **Quantic C**.
>
> These IDs are often called “opcodes” in OpenNomad reverse-engineering notes.
> That shorthand is convenient, but technically misleading: Runtime executes
> fixed-size function records containing a typed 32-bit **function ID**, parameter
> references, synchronization links, and mutable execution state. It is not a
> conventional variable-length bytecode stream.

This file is deliberately focused on the **IAM/SCX 32-bit `Script_*` function
catalogue and ABI**. For the complete version-5 `.SCX` container grammar,
`0xDEAD0002` script section layout, script-template structure, and the separate
one-byte scenario/event VM, see [`script-opcodes.md`](script-opcodes.md).

---

## 1. Executive summary

The retail Windows executable currently gives us three overlapping sets of
function IDs:

1. **28 current-format IDs** recognized by `Script_GetNumParam()`.
2. **18 of those 28** directly dispatched by the main `Script_PlayScript()`
   execution tree.
3. **At least 5 additional legacy/special IDs** referenced by other script
   machinery but not accepted by the current parameter mapper.

Of the 28 current-format IDs:

- **27 have a recovered or strongly reconstructed semantic name**.
- **1 remains genuinely unidentified:** `0x02000026`.
- Most names are preserved directly in Runtime diagnostic strings such as
  `Script_MoveObjectOnPath()` or `Script_SetSpriteFrame()`.
- `0x01000001` is semantically a camera-selection function and is documented
  here as `SelectCamera`, but the exact original source-level spelling has not
  been recovered from a diagnostic.
- `0x06000017` is `Wait`; the name is strongly established by
  `Script_Reinit_Wait()` and the handler's scheduler behavior.

The 32-bit IDs have the observed form:

```text
0xCC0000NN
```

where:

- `CC` behaves like a **function/subsystem class**;
- the middle 16 bits are zero in every currently identified ID;
- `NN` behaves very strongly like a **global IAM function ordinal**.

The current family interpretation is:

| Class | Working subsystem label |
|---:|---|
| `0x01` | cameras |
| `0x02` | character/body animation |
| `0x03` | world/object/path operations |
| `0x04` | sprites/effects/object chaining |
| `0x05` | sound |
| `0x06` | script control and messaging |

The low-byte ordinal is globally unique across every currently identified
current and legacy/special ID. That is strong evidence that the high byte was a
classification field added to a globally numbered IAM function catalogue,
rather than six independent opcode namespaces.

---

## 2. Binary baseline

All addresses in this document refer to the analyzed Windows retail executable:

```text
File:             Runtime.exe
PE type:          PE32 / i386
Image base:       0x00400000
Linker timestamp: 1999-10-04 20:31:50
SHA-256:          55f7120bfea7891b048c64e3682f3259cdbf2719a43fa24e42254b753c95d2ef
```

Addresses may differ in other localized releases, patches, demos, Dreamcast
code, or development builds. Function IDs are expected to be much more stable
than native addresses, but that still needs cross-build validation.

The most important recovered Runtime entry points are:

| Address | Current name / role | Evidence |
|---:|---|---|
| `0x0044C090` | `Script_GetNumParam` | diagnostic string + behavior |
| `0x0044C680` | raw 32-bit function-parameter accessor | call behavior |
| `0x0044C860` | `Script_PlayScript` | diagnostic string + execution tree |
| `0x0044A7E0` | generic script-function reinit dispatcher | behavior |
| `0x0044A0F0` | script lookup by 16-bit script ID | behavior |
| `0x00449750` | SCX loader | behavior |
| `0x00449xxx` | SCX tagged-section parsers | behavior |

---

## 3. Historical provenance: why these are IAM functions

### 3.1 IAM was Quantic Dream's adventure authoring environment

Contemporary Quantic Dream development diaries describe IAM as the studio's
adventure editor. The surviving English translation is sometimes awkward
because French *éditeur* is rendered as “publisher”; in this context it means
**editor/tool**.

The development material establishes that IAM:

- was intended to let designers build the adventure without writing native C/C++;
- used an icon-oriented authoring interface;
- embedded the real game for immediate testing;
- managed objects, characters, zones, conditions and adventure logic;
- exposed functions implemented by Fabien Fessard to the editor;
- generated/represented adventure scripting through an internal language
  referred to as **Quantic C**;
- was used in production by dedicated IAM scripters/game builders.

Relevant historical pages include:

- [Week 11 — IAM progress and inventory](https://omikrongame.blogspot.com/1997/07/week-11.html)
- [Weeks 14 & 15 — embedded engine / adventure scripting](https://omikrongame.blogspot.com/1997/08/weeks-14-15.html)
- [Weeks 25 & 26 — IAM/GEM/GALE tool descriptions](https://omikrongame.blogspot.com/1997/11/weeks-25-26.html)
- [Weeks 33 & 34 — conditions and zones as core adventure logic](https://omikrongame.blogspot.com/1998/01/weeks-33-34.html)
- [Week 37 — editor interface for functions already written by Fabien](https://omikrongame.blogspot.com/1998/01/week-37.html)
- [Week March 3 — complete interactive scene scripted in IAM in under an hour](https://omikrongame.blogspot.com/1998/03/week-march-3.html)
- [Developer editor screenshots](https://omikrongame.blogspot.com/1999/06/nomad-soul-developer-screens.html)

The especially important Week 37 statement is that the remaining IAM work
chiefly involved interfacing the editor with functions Fabien had already
written. That is almost exactly the architecture visible in Runtime: serialized
function IDs drive native `Script_*` handlers.

### 3.2 The retail executable preserves an IAM namespace

`Runtime.exe` contains literal resource paths:

```text
IAM\
IAM\%s
IAM\%s.TAG
IAM\AREA
IAM\DIALOG
IAM\GAMES
IAM\GLOBAL
IAM\OBJECT
IAM\SCENE
IAM\SNEAK
IAM\START
```

The supplied retail `.TAG` files are name/ID registries corresponding directly
to editor concepts:

```text
[VARIABLES]
[ZONES]
[AREAS]
[ADDRESSES]
[SCENES]
[OBJECTS]
[CAMERAS]
[DIALOGS]
```

For example:

```text
[VARIABLES]
13=ObjetUtilisé
...

[ZONES]
131=Moine
132=Poursuite Moine
...

[CAMERAS]
228=cam Moine Yeshu
229=cam Moine Sorcellerie 1
...
```

This is strong evidence that the data-driven runtime and the original IAM
authoring model are two sides of the same system.

### 3.3 Important terminology correction

Two different Runtime systems have historically both been called “IAM scripts”
in OpenNomad discussions:

1. the **32-bit `Script_*` function system** documented here and serialized in
   SCX script sections; and
2. a **separate compact one-byte scenario/event VM** found in `IAM/AREA`,
   `IAM/START`, and related game-state data.

They are not the same opcode namespace.

Example:

```text
32-bit IAM/SCX function ID:
    0x04000029  SetSpriteFrame

compact scenario/event VM opcode:
    0x46
```

The authoring tools may have generated or coordinated both forms of data, but
Runtime executes them through different structures and dispatchers.

---

## 4. Evidence model and naming rules

Function names in this document use the following confidence levels:

- **confirmed** — Runtime contains a diagnostic naming the action and the
  recovered native implementation matches that behavior;
- **reconstructed/strong** — no exact action diagnostic was recovered, but a
  matching reinit name and/or direct runtime semantics make the intended name
  effectively certain;
- **reconstructed** — semantics are clear, but the original source-level name
  remains unproven;
- **unknown** — the function ID is recognized by Runtime, but no reliable
  semantic identity has been recovered.

Diagnostic strings are extremely valuable, but they are **not infallible symbol
information**. There are visible copy/paste mistakes and generic labels in
neighboring routines. A name should therefore be accepted only after
correlating:

- the diagnostic;
- function-ID comparisons;
- callers/dispatch tables;
- reinitialization pairing;
- resource access;
- parameter usage.

`Script_StopSound` is a good example: the action name is explicit, while its
reset target uses more generic/copy-pasted sound diagnostics.

---

## 5. Function-ID encoding

All currently identified current-format function IDs match:

```text
0xCC0000NN
```

### 5.1 High byte: function class

The high byte groups operations by subsystem with remarkable consistency:

```text
01xxxxxx  cameras
02xxxxxx  body/character animation
03xxxxxx  world/object/path manipulation
04xxxxxx  sprites/effects/object chaining
05xxxxxx  sound
06xxxxxx  control/messaging
```

These labels are behavior-based. The original enum/type names have not been
recovered.

### 5.2 Low byte: probable global function ordinal

Across the 28 current-format IDs, every low byte is unique.

The five additional legacy/special IDs also use previously unused low-byte
values. The known low-byte set is currently:

```text
01 02 04 05 06 07 08 0A 0C 0D 11 13 14 15 16 17
1A 1B 1C 1D 1E 1F 20 21 23 24 25 26 27 28 29 2A 2B
```

Known gaps below `0x2C` are:

```text
03 09 0B 0E 0F 10 12 18 19 22
```

The clean global uniqueness makes this model likely:

```text
function ID
    = (subsystemClass << 24)
    | globalFunctionOrdinal
```

This is a **strong hypothesis**, not yet a recovered source declaration.

One useful implication is that missing ordinal values may correspond to
functions removed during development, editor-only operations, or functions
whose retail runtime path has not yet been found.

---

## 6. Complete current-format function catalogue

`Script_GetNumParam()` recognizes **28 current-format IDs**. The table below
combines parameter-mapper recognition, native implementations, reinit
implementations and the main `Script_PlayScript` execution tree.

“Blocks group” means the handler's return participates in the
**group-still-active** accumulator. It does **not** mean success/failure.

| Function ID | Class | Name | Name confidence | Action handler | Reinit | Main dispatch | Blocks group | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0x01000001 | 0x01 | SelectCamera | reconstructed | 0x004A4580 | — | yes | no | Camera-selection operation; exact source-level name is not exposed by a diagnostic string. |
| 0x01000002 | 0x01 | InterpolateCameras | confirmed | 0x004A4630 | 0x004A4450 | yes | yes | Runtime diagnostics expose both action and reinit names. |
| 0x02000004 | 0x02 | SelectBodyAnimation | confirmed | 0x004A35D0 | 0x004A3290 | yes | yes | Body/character animation operation. |
| 0x02000026 | 0x02 | UNKNOWN_02000026 | unknown | — | — | no known path | unknown | `Script_GetNumParam` recognizes this current-format ID, but no normal action/reinit implementation has been identified. |
| 0x0200002A | 0x02 | SelectRelativeBodyAnimation | confirmed | 0x004A3AD0 | 0x004A3430 | yes | yes | Relative body-animation operation; exact meaning of “relative” still needs field-level tracing. |
| 0x03000008 | 0x03 | MoveObjectOnPath | confirmed | 0x0046F400 | 0x0046EEB0 | yes | yes | Moves an object using a loaded path resource. |
| 0x0300001A | 0x03 | AnimationFromExternalScene | confirmed | 0x00470060 | 0x0046F0C0 | yes | yes | Uses an object from an external scene and moves/unlinks it between scene contexts while animating it. |
| 0x03000021 | 0x03 | MorphObject | confirmed | 0x00470440 | 0x0046F200 | yes | yes | Morphs between object resources over time. |
| 0x03000023 | 0x03 | ScaleObjectX | confirmed | 0x004707A0 | 0x0046EB50 | yes | yes | Timed X-axis object scaling. |
| 0x03000024 | 0x03 | ScaleObjectY | confirmed | 0x004709E0 | 0x0046EC70 | yes | yes | Timed Y-axis object scaling. |
| 0x03000025 | 0x03 | ScaleObjectZ | confirmed | 0x00470C20 | 0x0046ED90 | yes | yes | Timed Z-axis object scaling. |
| 0x0300002B | 0x03 | SwapObject | confirmed | 0x00470E60 | 0x0046E9C0 | yes | no | Immediate scene/external-scene object swap operation. |
| 0x0400000C | 0x04 | SetSpriteType | confirmed | 0x004A2520 | — | not in main dispatcher | n/a | Concrete native implementation and diagnostic; alternate invocation path unresolved. |
| 0x0400000D | 0x04 | Display3DSpriteOnPath | confirmed | 0x004A2150 | 0x004A1B20 | not in main dispatcher | n/a | Sprite/path operation; concrete action and reinit implementations exist. |
| 0x04000011 | 0x04 | ChainObjects | confirmed | 0x004A1E80 | 0x004A19C0 | yes | yes | Runtime term is “ChainObjects”; exact object relationship still requires field-level tracing. |
| 0x0400001B | 0x04 | ScaleSpriteOnX | confirmed | 0x004A25E0 | 0x004A1810 | not in main dispatcher | n/a | Concrete action/reinit implementation. |
| 0x0400001C | 0x04 | ScaleSpriteOnY | confirmed | 0x004A2790 | 0x004A18A0 | not in main dispatcher | n/a | Concrete action/reinit implementation. |
| 0x0400001D | 0x04 | SetSpriteRolling | confirmed | 0x004A2940 | 0x004A1930 | not in main dispatcher | n/a | Runtime terminology retained; exact angle/axis convention remains to be proven. |
| 0x0400001E | 0x04 | SetSpritePalette | confirmed | 0x004A2AF0 | — | not in main dispatcher | n/a | Assigns a loaded palette to a sprite. |
| 0x0400001F | 0x04 | SetSpriteDefaultPalette | confirmed | 0x004A2C60 | — | not in main dispatcher | n/a | Restores/selects the sprite's default palette. |
| 0x04000020 | 0x04 | MorphPaletteSprite | confirmed | 0x004A2D10 | 0x004A1C90 | yes | yes | Stateful palette morph/interpolation. |
| 0x04000028 | 0x04 | Display3DSprite | confirmed | 0x004A2FB0 | 0x004A1DE0 | not in main dispatcher | n/a | Concrete action/reinit implementation; per-instance sprite state is allocated elsewhere. |
| 0x04000029 | 0x04 | SetSpriteFrame | confirmed | 0x004A31B0 | — | not in main dispatcher | n/a | Selects a sprite frame; concrete native implementation. |
| 0x05000014 | 0x05 | PlaySound | confirmed | 0x004A12D0 | 0x004A0FB0 | yes | no | Starts sound without holding the current synchronized group open. |
| 0x05000015 | 0x05 | PlaySyncSound | confirmed | 0x004A14D0 | 0x004A1100 | yes | yes | Synchronized/blocking sound playback. |
| 0x05000016 | 0x05 | StopSound | confirmed | 0x004A16D0 | 0x004A11F0 | yes | no | Reset target is certain from dispatch; reset diagnostics use a more generic/copy-pasted sound label. |
| 0x06000017 | 0x06 | Wait | reconstructed/strong | 0x004A0EE0 | 0x004A0E50 | yes | yes | `Script_Reinit_Wait` plus execution behavior makes the name effectively certain, despite no equivalent action diagnostic string being required. |
| 0x06000027 | 0x06 | SendMessage | confirmed | 0x004A0E80 | — | yes | no | Immediate message/event side effect. |


### 6.1 Main-dispatched set

The main `Script_PlayScript()` tree directly dispatches **18 IDs**:

```text
0x01000001
0x01000002

0x02000004
0x0200002A

0x03000008
0x0300001A
0x03000021
0x03000023
0x03000024
0x03000025
0x0300002B

0x04000011
0x04000020

0x05000014
0x05000015
0x05000016

0x06000017
0x06000027
```

### 6.2 Current-format IDs outside the main dispatcher

Ten IDs are recognized by `Script_GetNumParam()` but are not in the currently
identified main per-frame switch:

```text
0x02000026

0x0400000C
0x0400000D
0x0400001B
0x0400001C
0x0400001D
0x0400001E
0x0400001F
0x04000028
0x04000029
```

This is one of the most important architectural findings.

Do **not** equate:

```text
not in Script_PlayScript main switch
```

with:

```text
unused
```

Most of the family-`0x04` entries above have concrete native implementations,
several have reinit handlers, and `Script_MakeInstance()` performs
sprite-specific allocation work. Their alternate execution/lifecycle path
remains to be reconstructed.

Likewise, do not equate:

```text
has a native Script_* implementation
```

with:

```text
normal per-frame blocking action
```

The evidence sets are intentionally kept separate.

---

## 7. Main execution semantics

### 7.1 Function records are synchronized groups, not bytecode instructions

`Script_PlayScript()` selects a primary function record for the current script
group, then follows the record's `SyncFunction`/linked-function relationship.

Conceptually:

```text
current script group
    |
    +-- primary function
    |
    +-- synchronized/linked function
    |
    +-- synchronized/linked function
    ...
```

Each function can execute independently, but selected handler returns are ORed
into a group-level accumulator.

A conceptual approximation is:

```cpp
bool groupStillActive = false;

for (ScriptFunction* fn : synchronizedFunctions) {
    switch (fn->functionId) {
        case MoveObjectOnPath:
            groupStillActive |= Script_MoveObjectOnPath(...);
            break;

        case PlaySound:
            Script_PlaySound(...);
            break;

        case Wait:
            groupStillActive |= Script_Wait(...);
            break;
    }
}

if (!groupStillActive) {
    advanceToNextGroup();
}
```

The exact control flow is more complicated, but this captures the crucial
meaning of handler return values.

### 7.2 Blocking/current-group operations

The known blocking functions are:

```text
InterpolateCameras
SelectBodyAnimation
SelectRelativeBodyAnimation
MoveObjectOnPath
AnimationFromExternalScene
MorphObject
ScaleObjectX
ScaleObjectY
ScaleObjectZ
ChainObjects
MorphPaletteSprite
PlaySyncSound
Wait
```

These operations naturally take time or wait for a completion condition.

### 7.3 Immediate/non-blocking operations

The known main-dispatched immediate operations are:

```text
SelectCamera
SwapObject
PlaySound
StopSound
SendMessage
```

They can cause side effects without, by themselves, keeping the synchronized
group active.

### 7.4 Reinitialization is part of the ABI

Runtime has a generic reinit dispatcher at approximately:

```text
0x0044A7E0
```

Many stateful functions have paired `Script_Reinit_*` handlers.

This matters for:

- repeated script groups;
- repeated entire scripts;
- restoring progress counters;
- restoring resources or transforms;
- resetting per-function state after an instance is reused.

OpenNomad should model reinit explicitly rather than treating it as an
implementation detail of first execution.

---

## 8. Function-record ABI

The function record stride is:

```text
0x18 bytes
```

A conservative runtime representation is:

```cpp
struct IAMScriptFunction {
    uint32_t functionId;          // +0x00
    uint32_t unknown04;           // +0x04
    uint32_t* parameters;         // +0x08, pointer after relocation
    IAMScriptFunction* sync;      // +0x0C, pointer after relocation
    uint32_t runtimeState10;      // +0x10
    uint32_t runtimeState14;      // +0x14
};
static_assert(sizeof(IAMScriptFunction) == 0x18);
```

This is **not** a proposed final OpenNomad structure. It describes the observed
32-bit Runtime layout.

### 8.1 `+0x00`: function ID

This is the `0xCC0000NN` value documented in this file.

Handlers frequently validate it directly and emit diagnostics such as:

```text
Script_MorphObject(): Bad function ID.
Script_ScaleObjectX(): Bad function ID.
Script_SwapObject(): Bad function ID.
```

### 8.2 `+0x04`: unresolved metadata

No universal meaning has been established.

Do not rename this field based on one or two handlers.

### 8.3 `+0x08`: parameter reference

On disk/template form, this is an **index** into function-parameter storage.

At runtime it is converted into a native pointer.

The diagnostic names use the term:

```text
FuncParams
```

### 8.4 `+0x0C`: synchronization/link reference

On serialized/template form this is an index into a sync-function array.

At runtime it is converted into a pointer.

Diagnostics explicitly call this:

```text
SyncFunction
```

A null/no-link serialized value is represented by the format's null/sentinel
convention; see `script-opcodes.md` for the full SCX serialization discussion.

### 8.5 `+0x10` and `+0x14`: mutable execution state

These values behave broadly like:

```text
configured execution/frame/timing state
current mutable progress
```

but the exact generic names are not yet proven and handlers may interpret them
differently.

OpenNomad should preserve them as explicit per-instance state until their
per-function semantics are fully mapped.

---

## 9. Index/pointer relocation

Two unusually helpful Runtime diagnostic names are:

```text
Script_FunctionsAdressesToIndexes()
Script_FunctionsIndexesToAdresses()
```

The spelling `Adresses` is preserved from the executable.

These routines do **not** map IAM IDs to native C function pointers.

Instead, they convert pointer-rich in-memory script graphs to/from
index-based representations.

Relevant diagnostics include:

```text
Script_FunctionsAdressesToIndexes(): Index of FuncParams too big: %d/%d.
Script_FunctionsAdressesToIndexes(): Index of SyncFunction too big : %d/%d.
Script_FunctionsAdressesToIndexes(): Ptr is NULL.

Script_FunctionsIndexesToAdresses(): Address of FuncParams isn't valid.
Script_FunctionsIndexesToAdresses(): Address of SyncFunction isn't valid.
```

This confirms a fundamental serialization rule:

```text
serialized function record
    +0x08 = parameter index
    +0x0C = sync-function index

            load / instance fixup

runtime function record
    +0x08 = parameter pointer
    +0x0C = sync-function pointer
```

A parser/reimplementation should therefore keep serialized structures and
runtime structures conceptually separate. Treating the on-disk dwords as raw
pointers is incorrect.

---

## 10. Script templates and mutable instances

Runtime has explicit instance-management code:

```text
Script_MakeInstance()
Script_RemoveInstance()
Script_RemoveAllInstances()
```

`Script_MakeInstance()` diagnostics prove that creating an instance can allocate
or copy:

- script-function records;
- parameter lists;
- function parameters;
- sync-function parameters;
- instance-local sprite state.

It also validates that `SyncFunction` relationships remain inside the owning
script and fixes them to point into the cloned instance.

This implies:

```text
loaded template
    !=
mutable playing instance
```

OpenNomad should not normally execute by mutating a shared SCX script template.

Instance-local mutability is especially important for:

- `+0x10` / `+0x14` progress state;
- parameter values that handlers mutate;
- synchronized links;
- sprite instances;
- repetition/reinitialization.

---

## 11. `Script_GetNumParam()` and the semantic parameter ABI

### 11.1 Purpose

`Script_GetNumParam()` at:

```text
0x0044C090
```

takes a function record and a **semantic parameter selector**, then returns the
numeric slot occupied by that semantic parameter for that function type.

It therefore proves that the parameter model is not simply:

```text
parameter 0
parameter 1
parameter 2
...
```

Runtime has an additional semantic type/role layer.

Unsupported requests produce diagnostics:

```text
Script_GetNumParam(): Function type not supported yet.
Script_GetNumParam(): Bad parameter type %x. Can't be found in this kind of function!
```

### 11.2 Why this is important

The same selector can live in different positional slots for different
functions.

This likely allowed generic editor/runtime helper routines such as:

```text
Script_ModifyObject1()
Script_ModifyObject1Name()
Script_ModifyObject2()
Script_ModifyObject2Name()

Script_ModifyCamera1()
Script_ModifyCamera1Name()
Script_ModifyCamera2()
Script_ModifyCamera2Name()

Script_ModifyXYZPtr()
```

to locate a semantic operand without hard-coding each function's raw slot
layout.

### 11.3 Parameter selector names are not yet fully recovered

The numeric selector values are known, but the original enum names are not.

A few roles are strongly suggested by repeated usage:

- `0x00` is repeatedly associated with a primary object reference;
- `0x01` is used where a second object relationship is expected;
- `0x04` / `0x05` align with first/second camera operands;
- `0x09` consistently selects sprite-related operands;
- `0x0A` / `0x0B` appear in palette operations;
- `0x0C` is used by sound functions;
- `0x0D` is used by `SendMessage`.

Those interpretations should remain **working names**, not source-level enum
names, until the selector-resolver/helper code has been fully traced.

---

## 12. Complete current parameter-selector matrix

The following table is the current complete map recovered from
`Script_GetNumParam()`.

Notation:

```text
SS→PP
```

means:

```text
semantic selector 0xSS maps to positional parameter slot PP
```

Missing positional slots are real and must not be automatically compacted.
They may contain literals, flags, indices, runtime-only values, or parameters
accessed through other mechanisms.

| Function ID | Name | Selector → slot map |
| --- | --- | --- |
| 0x01000001 | `SelectCamera` | `04→0` |
| 0x01000002 | `InterpolateCameras` | `04→0, 05→1, 10→2, 11→3` |
| 0x02000004 | `SelectBodyAnimation` | `00→0, 06→1, 11→3, 12→4, 13→5, 14→6, 15→7, 16→8, 17→9` |
| 0x02000026 | `UNKNOWN_02000026` | `00→0, 02→1, 03→2, 10→3, 11→4` |
| 0x0200002A | `SelectRelativeBodyAnimation` | `00→0, 06→1, 07→7, 08→8, 11→3, 12→4, 13→5, 14→6, 15→9, 16→10, 17→11` |
| 0x03000008 | `MoveObjectOnPath` | `00→0, 07→1, 08→2, 20→4, 21→5, 10→6, 11→7, 1C→9, 1D→10, 1E→11, 12→12, 13→13, 14→14` |
| 0x0300001A | `AnimationFromExternalScene` | `0F→0, 00→1, 10→6, 11→7` |
| 0x03000021 | `MorphObject` | `00→0, 20→1, 10→2, 11→3` |
| 0x03000023 | `ScaleObjectX` | `00→0, 15→2, 16→3, 10→5, 11→6` |
| 0x03000024 | `ScaleObjectY` | `00→0, 15→2, 16→3, 10→5, 11→6` |
| 0x03000025 | `ScaleObjectZ` | `00→0, 15→2, 16→3, 10→5, 11→6` |
| 0x0300002B | `SwapObject` | `00→0, 01→1` |
| 0x0400000C | `SetSpriteType` | `09→0` |
| 0x0400000D | `Display3DSpriteOnPath` | `09→0, 07→1, 08→2, 00→4, 10→5, 11→6` |
| 0x04000011 | `ChainObjects` | `00→0, 01→1, 19→2, 1A→3, 1B→4, 10→5, 11→6` |
| 0x0400001B | `ScaleSpriteOnX` | `09→0, 15→1, 16→2, 10→4, 11→5` |
| 0x0400001C | `ScaleSpriteOnY` | `09→0, 15→1, 16→2, 10→4, 11→5` |
| 0x0400001D | `SetSpriteRolling` | `09→0, 17→1, 18→2, 10→4, 11→5` |
| 0x0400001E | `SetSpritePalette` | `09→0, 0A→1` |
| 0x0400001F | `SetSpriteDefaultPalette` | `09→0` |
| 0x04000020 | `MorphPaletteSprite` | `09→0, 0A→1, 0B→2, 10→3, 11→4` |
| 0x04000028 | `Display3DSprite` | `09→0, 0E→1, 10→2, 11→3` |
| 0x04000029 | `SetSpriteFrame` | `09→0, 1F→1` |
| 0x05000014 | `PlaySound` | `0C→0, 00→3` |
| 0x05000015 | `PlaySyncSound` | `0C→0, 1F→1, 00→4` |
| 0x05000016 | `StopSound` | `0C→0, 00→1` |
| 0x06000017 | `Wait` | `10→0, 11→1` |
| 0x06000027 | `SendMessage` | `0D→0` |


### 12.1 Important parser rule

Do not infer a function's raw parameter count by taking only the number of
entries above.

For example, `SelectBodyAnimation` has selector mappings reaching slot 9 but
does not expose selector mappings for every slot in between. The underlying
function can therefore contain parameters that are not addressed by the
semantic selector API.

Until the complete parameter ABI is recovered, a safe decoder should:

1. preserve raw 32-bit parameter values;
2. preserve positional slot indexes;
3. layer recovered semantic selector names on top;
4. let each handler interpret its full raw layout.

---

## 13. Family `0x01`: cameras

### `0x01000001` — `SelectCamera` (reconstructed name)

```text
Action: 0x004A4580
Reinit: —
Main-dispatched: yes
Blocks group: no
```

The handler resolves a camera in the current scene and performs an immediate
camera-selection operation.

The exact source-level name is not exposed by a dedicated Runtime diagnostic.
`SelectCamera` is used here because:

- the semantics are direct;
- the IAM editor visibly exposed camera-selection operations;
- the neighboring `0x01000002` is `InterpolateCameras`;
- the handler does not block the group.

Until an original symbol/editor label is recovered, code that wants to be
maximally conservative may use a name such as:

```cpp
CameraSelect_01000001
```

while documenting `SelectCamera` as the reconstructed semantic name.

### `0x01000002` — `InterpolateCameras`

```text
Action: 0x004A4630
Reinit: 0x004A4450
Main-dispatched: yes
Blocks group: yes
```

Runtime diagnostics include:

```text
Script_InterpolateCameras(): Can't find camera "%s" in current scene.
Script_Reinit_InterpolateCameras(): Can't find camera "%s" in current scene.
```

Its two camera selectors (`0x04`, `0x05`) plus two timing/state selectors
(`0x10`, `0x11`) are consistent with a timed transition between camera states.

---

## 14. Family `0x02`: body/character animation

### `0x02000004` — `SelectBodyAnimation`

```text
Action: 0x004A35D0
Reinit: 0x004A3290
Main-dispatched: yes
Blocks group: yes
```

Runtime diagnostics expose:

```text
Script_SelectBodyAnimation()
Script_Reinit_SelectBodyAnimation()
```

OpenNomad implements the recovered ten-slot mutable ABI:

```text
arg0  binding-table-A object index
arg1  3DA animation descriptor index
arg2  previous progress (mutable float)
arg3  current progress (mutable float)
arg4-6 body-animation vector (floats; meaning still unresolved)
arg7-9 authored non-path anchor offset (floats)
```

It operates on the explicitly character-bound SCX instance. On its first tick
it binds 3DA channels to the selected 3DO hierarchy by numeric `script_id`,
anchors from the selected object's current runtime position plus the authored
centimetre-to-inch offset, and then uses the shared 3DA rotation/root-motion
playback path. `arg2`/`arg3` reset to `0`/`1` on command reinitialization.

Unlike `SelectRelativeBodyAnimation`, it does not resolve a 3DP path or
subpath. Both commands remain group-active while their 30 Hz script-frame
progression has not reached the animation endpoint.

Representative diagnostics:

```text
Script_SelectBodyAnimation(): Animation "%s" can't be loaded. File doesn't exist
Script_SelectBodyAnimation(): Animation not loaded.
Script_SelectBodyAnimation(): Can't find object "%s" in current scene.
```

### `0x02000026` — unknown current-format function

This is the only one of the 28 `Script_GetNumParam()`-recognized IDs that
currently lacks a reliable semantic name.

Known selector mapping:

```text
00→0
02→1
03→2
10→3
11→4
```

Its high-byte family strongly associates it with character/body animation, but
that alone is insufficient to name it.

No confirmed:

- main `Script_PlayScript` case;
- normal action implementation;
- reinit implementation;
- self-identifying diagnostic

has yet been recovered.

Recommended OpenNomad name:

```cpp
Unknown_02000026
```

Do not invent a gameplay name until retail asset usage or a native execution
path is found.

### `0x0200002A` — `SelectRelativeBodyAnimation`

```text
Action: 0x004A3AD0
Reinit: 0x004A3430
Main-dispatched: yes
Blocks group: yes
```

Runtime diagnostics expose:

```text
Script_SelectRelativeBodyAnimation()
Script_Reinit_SelectRelativeBodyAnimation()
```

The action uses the same selected-hierarchy 3DA playback and progress machinery
as `SelectBodyAnimation`, but its 12-slot ABI additionally contains a 3DP
resource/subpath pair. Its first anchor comes from the sampled 3DP position
plus the authored offset rather than the selected object's current runtime
position.

The exact meaning of “relative” should remain Runtime terminology until the
transform/root-motion semantics are traced.

---

## 15. Family `0x03`: world/object/path operations

### `0x03000008` — `MoveObjectOnPath`

```text
Action: 0x0046F400
Reinit: 0x0046EEB0
Main-dispatched: yes
Blocks group: yes
```

Representative diagnostics:

```text
Script_MoveObjectOnPath(): Path "%s" not loaded.
Script_MoveObjectOnPath(): Can't find object "%s" in parameters list ...

Script_Reinit_MoveObjectOnPath(): Bad path file.
Script_Reinit_MoveObjectOnPath(): Can't find object "%s" in current scene.
```

The function is one of the strongest links between IAM actions and world/path
resources authored by Quantic Dream's tooling.

### `0x0300001A` — `AnimationFromExternalScene`

```text
Action: 0x00470060
Reinit: 0x0046F0C0
Main-dispatched: yes
Blocks group: yes
```

This function exposes a particularly important scene-ownership model.

Diagnostics show Runtime can:

- find an object in **external scenes**;
- move it into the current scene;
- animate it;
- unlink it from the current scene;
- move it back into its source scene.

Representative strings:

```text
Script_AnimationFromExternalScene(): Can't find object "%s" in external scenes.
Script_AnimationFromExternalScene(): Can't move object "%s" with frame %d into current scene
Script_AnimationFromExternalScene(): Can't unlink object "%s" from current scene.
Script_AnimationFromExternalScene(): Can't move object "%s" into its source scene.
```

OpenNomad should preserve the conceptual distinction between:

```text
current/resident scene
external scene resources
```

even if its modern resource management differs internally.

### `0x03000021` — `MorphObject`

```text
Action: 0x00470440
Reinit: 0x0046F200
Main-dispatched: yes
Blocks group: yes
```

Diagnostics refer to:

```text
source object in external scenes
destination object in external scenes
current-scene object
NbTrames
```

The function is stateful and frame/timing based.

### `0x03000023` — `ScaleObjectX`

```text
Action: 0x004707A0
Reinit: 0x0046EB50
Main-dispatched: yes
Blocks group: yes
```

### `0x03000024` — `ScaleObjectY`

```text
Action: 0x004709E0
Reinit: 0x0046EC70
Main-dispatched: yes
Blocks group: yes
```

### `0x03000025` — `ScaleObjectZ`

```text
Action: 0x00470C20
Reinit: 0x0046ED90
Main-dispatched: yes
Blocks group: yes
```

All three expose `NbTrames`/timed behavior in their reset paths and should be
modeled as progressive operations rather than instantaneous scale assignment.

### `0x0300002B` — `SwapObject`

```text
Action: 0x00470E60
Reinit: 0x0046E9C0
Main-dispatched: yes
Blocks group: no
```

Diagnostics show the function operates across current and external scenes:

```text
Script_SwapObject(): Can't find object "%s" in current scene nor in external scenes.
Script_SwapObject(): Can't move object "%s" into current scene.
Script_SwapObject(): Can't unlink object "%s" from scene.
```

From the script scheduler's perspective this is an immediate side effect.

---

## 16. Family `0x04`: sprites, effects and chaining

This family is the clearest evidence that the main `Script_PlayScript` switch is
not the complete function ABI.

### `0x0400000C` — `SetSpriteType`

```text
Action: 0x004A2520
Reinit: —
Main-dispatched: no known main case
```

Diagnostic:

```text
Script_SetSpriteType(): Bad function type.
```

### `0x0400000D` — `Display3DSpriteOnPath`

```text
Action: 0x004A2150
Reinit: 0x004A1B20
Main-dispatched: no known main case
```

Representative diagnostics:

```text
Script_Display3DSpriteOnPath(): Path "%s" not loaded.
Script_Display3DSpriteOnPath(): Sprite "%s" isn't loaded.
Script_Display3DSpriteOnPath(): Can't find object "%s" in current scene.
```

### `0x04000011` — `ChainObjects`

```text
Action: 0x004A1E80
Reinit: 0x004A19C0
Main-dispatched: yes
Blocks group: yes
```

The exact meaning of “chain” is not yet proven. Preserve the Runtime name rather
than prematurely translating it to parenting, attachment, animation chaining,
or another narrower model.

### `0x0400001B` — `ScaleSpriteOnX`

```text
Action: 0x004A25E0
Reinit: 0x004A1810
Main-dispatched: no known main case
```

### `0x0400001C` — `ScaleSpriteOnY`

```text
Action: 0x004A2790
Reinit: 0x004A18A0
Main-dispatched: no known main case
```

### `0x0400001D` — `SetSpriteRolling`

```text
Action: 0x004A2940
Reinit: 0x004A1930
Main-dispatched: no known main case
```

Keep “rolling” as original Runtime terminology until the exact angle/axis
convention is mapped.

### `0x0400001E` — `SetSpritePalette`

```text
Action: 0x004A2AF0
Reinit: —
Main-dispatched: no known main case
```

Representative diagnostics:

```text
Script_SetSpritePalette(): Palette "%s" isn't loaded.
Script_SetSpritePalette(): Can't assign palette "%s" to sprite "%s".
```

### `0x0400001F` — `SetSpriteDefaultPalette`

```text
Action: 0x004A2C60
Reinit: —
Main-dispatched: no known main case
```

### `0x04000020` — `MorphPaletteSprite`

```text
Action: 0x004A2D10
Reinit: 0x004A1C90
Main-dispatched: yes
Blocks group: yes
```

Diagnostics reference source and destination palettes plus `NbTrames`, strongly
indicating a stateful palette interpolation.

The exact color space and relationship to palette/gamma conversion should be
documented together with the renderer once traced.

### `0x04000028` — `Display3DSprite`

```text
Action: 0x004A2FB0
Reinit: 0x004A1DE0
Main-dispatched: no known main case
```

### `0x04000029` — `SetSpriteFrame`

```text
Action: 0x004A31B0
Reinit: —
Main-dispatched: no known main case
```

This function is directly relevant to OpenNomad's sprite implementation.

Runtime checks the function type, resolves the sprite, and changes sprite frame
state. The unresolved question is **how this native implementation is reached
from the script lifecycle**, not whether the function exists.

---

## 17. Family `0x05`: sound

### `0x05000014` — `PlaySound`

```text
Action: 0x004A12D0
Reinit: 0x004A0FB0
Main-dispatched: yes
Blocks group: no
```

A normal sound start does not hold the synchronized group open.

### `0x05000015` — `PlaySyncSound`

```text
Action: 0x004A14D0
Reinit: 0x004A1100
Main-dispatched: yes
Blocks group: yes
```

The contrast with `PlaySound` is a clean example of IAM synchronization
semantics:

```text
PlaySound      -> start side effect; continue
PlaySyncSound  -> remain active until synchronization/completion condition
```

### `0x05000016` — `StopSound`

```text
Action: 0x004A16D0
Reinit dispatch target: 0x004A11F0
Main-dispatched: yes
Blocks group: no
```

The action diagnostic confirms `Script_StopSound`. The reset-side string naming
is less clean and appears generic/copy-pasted, but the dispatch pairing is
reliable.

---

## 18. Family `0x06`: scheduler/control and messaging

### `0x06000017` — `Wait`

```text
Action: 0x004A0EE0
Reinit: 0x004A0E50
Main-dispatched: yes
Blocks group: yes
```

`Wait` is one of the most useful functions for understanding the scheduler.

The reset path explicitly exposes:

```text
Script_Reinit_Wait()
```

and the handler behaves as a timed/progressive operation.

Its contributing nonzero return means:

```text
this group is still active
```

not:

```text
the call succeeded
```

### `0x06000027` — `SendMessage`

```text
Action: 0x004A0E80
Reinit: —
Main-dispatched: yes
Blocks group: no
```

Diagnostic:

```text
Script_SendMessage(): Bad function type.
```

The exact message namespace, recipient model and relationship to the separate
scenario/event VM remain open questions.

---

## 19. Legacy, reserved and special IDs

The retail executable knows about typed function IDs beyond the 28 accepted by
`Script_GetNumParam()`.

These must not be silently discarded by an exhaustive parser.

| Function ID | Class | Current interpretation | Evidence |
| --- | --- | --- | --- |
| 0x02000005 | 0x02 | unknown legacy/special animation function | Recognized by the animation-reference script walker; parameter 1 is treated as an animation reference. Not accepted by the current `Script_GetNumParam` tree. |
| 0x02000006 | 0x02 | unknown legacy/special animation function | Recognized by the same animation-reference walker; parameter 1 is treated as an animation reference. |
| 0x03000007 | 0x03 | unknown legacy/special object-animation function | Recognized by the animation-reference walker; parameter 1 is treated as an animation reference. |
| 0x0300000A | 0x03 | unknown legacy/special object-animation function | Recognized by the animation-reference walker; parameter 1 is treated as an animation reference. |
| 0x05000013 | 0x05 | unknown special/reserved sound-family ID | Appears in script-instance/progress initialization comparisons. Not recognized by the current `Script_GetNumParam` tree and no normal handler has been recovered. |


### 19.1 Four legacy/special animation-reference IDs

A script-record walker associated with the diagnostic:

```text
Internal error, AnimInScript, anim not found, C-YA Antoine (et maudissez Hakim)
```

recognizes this set of animation-bearing IDs:

```text
0x02000004
0x02000005
0x02000006
0x0200002A

0x03000007
0x0300000A
0x0300001A
```

For the four otherwise-unknown IDs:

```text
0x02000005
0x02000006
0x03000007
0x0300000A
```

the walker retrieves positional parameter **1** and treats it as an animation
reference.

They are not accepted by the current `Script_GetNumParam()` function tree.

The safest current interpretation is:

- valid in an older/special IAM function vocabulary;
- still recognized by generic retail tooling that scans script animation
  references;
- no longer normal current-format executable actions in the mapped retail path.

Do not assign screenshot/editor labels such as `PlayAnim...` to these IDs until
a direct binary or asset correlation is obtained.

### 19.2 `0x05000013`

`0x05000013` is different from the four animation-reference IDs.

It participates in script-instance/progress initialization comparisons but:

- is not accepted by `Script_GetNumParam()`;
- has no confirmed normal per-frame handler;
- has no recovered source-like diagnostic name.

Its class places it immediately before:

```text
0x05000014 PlaySound
0x05000015 PlaySyncSound
0x05000016 StopSound
```

which makes a removed/legacy sound operation tempting, but **that is not
proven**.

Recommended name:

```cpp
LegacyOrSpecial_05000013
```

---

## 20. Current known global ordinal map

If the low byte is indeed the global IAM function ordinal, the currently known
catalogue can be visualized chronologically/numerically:

```text
01  class 01  SelectCamera
02  class 01  InterpolateCameras
03  ????????

04  class 02  SelectBodyAnimation
05  class 02  legacy animation function
06  class 02  legacy animation function
07  class 03  legacy object-animation function
08  class 03  MoveObjectOnPath
09  ????????
0A  class 03  legacy object-animation function
0B  ????????
0C  class 04  SetSpriteType
0D  class 04  Display3DSpriteOnPath
0E  ????????
0F  ????????
10  ????????
11  class 04  ChainObjects
12  ????????
13  class 05  special/reserved ID
14  class 05  PlaySound
15  class 05  PlaySyncSound
16  class 05  StopSound
17  class 06  Wait
18  ????????
19  ????????
1A  class 03  AnimationFromExternalScene
1B  class 04  ScaleSpriteOnX
1C  class 04  ScaleSpriteOnY
1D  class 04  SetSpriteRolling
1E  class 04  SetSpritePalette
1F  class 04  SetSpriteDefaultPalette
20  class 04  MorphPaletteSprite
21  class 03  MorphObject
22  ????????
23  class 03  ScaleObjectX
24  class 03  ScaleObjectY
25  class 03  ScaleObjectZ
26  class 02  UNKNOWN_02000026
27  class 06  SendMessage
28  class 04  Display3DSprite
29  class 04  SetSpriteFrame
2A  class 02  SelectRelativeBodyAnimation
2B  class 03  SwapObject
```

This presentation is useful because it exposes likely historical holes in the
editor catalogue instead of hiding them inside subsystem groups.

---

## 21. Complete recovered `Script_*` diagnostic vocabulary

The executable preserves many source-like function names in error strings.
They fall into several categories.

### 21.1 Core script lifecycle / serialization

```text
Script_PlayScript
Script_GetNumParam
Script_MakeInstance
Script_RemoveInstance
Script_RemoveAllInstances
Script_FunctionsAdressesToIndexes
Script_FunctionsIndexesToAdresses
```

### 21.2 Generic parameter/resource mutation helpers

```text
Script_ModifyObject1
Script_ModifyObject1Name
Script_ModifyObject2
Script_ModifyObject2Name

Script_ModifyCamera1
Script_ModifyCamera1Name
Script_ModifyCamera2
Script_ModifyCamera2Name

Script_ModifyXYZPtr
```

These helper names are especially relevant to reconstructing the semantic
parameter-selector enum.

### 21.3 Resource-list helpers

```text
Script_AddSoundFile
Script_AddSpriteFile

Script_CleanAnimList
Script_CleanPaletteList
Script_CleanPathList
Script_CleanSceneList
Script_CleanSoundList
Script_CleanSpriteList
```

A surviving diagnostic also says:

```text
no Script_AddXYZPtr anymore !
```

which is useful evidence of an older API that had already been removed by the
retail build.

### 21.4 Action functions

```text
Script_InterpolateCameras

Script_SelectBodyAnimation
Script_SelectRelativeBodyAnimation

Script_MoveObjectOnPath
Script_AnimationFromExternalScene
Script_MorphObject
Script_ScaleObjectX
Script_ScaleObjectY
Script_ScaleObjectZ
Script_SwapObject

Script_ChainObjects
Script_Display3DSpriteOnPath
Script_SetSpriteType
Script_ScaleSpriteOnX
Script_ScaleSpriteOnY
Script_SetSpriteRolling
Script_SetSpritePalette
Script_SetSpriteDefaultPalette
Script_MorphPaletteSprite
Script_Display3DSprite
Script_SetSpriteFrame

Script_PlaySound
Script_PlaySyncSound
Script_StopSound

Script_SendMessage
```

The camera-selection operation and `Wait` action do not need an exact
self-identifying action string to be recoverable; their semantics are addressed
earlier in this document.

### 21.5 Reinit functions

```text
Script_Reinit_InterpolateCameras

Script_Reinit_SelectBodyAnimation
Script_Reinit_SelectRelativeBodyAnimation

Script_Reinit_MoveObjectOnPath
Script_Reinit_AnimationFromExternalScene
Script_Reinit_MorphObject
Script_Reinit_ScaleObjectX
Script_Reinit_ScaleObjectY
Script_Reinit_ScaleObjectZ
Script_Reinit_SwapObject

Script_Reinit_ChainObjects
Script_Reinit_Display3DSpriteOnPath
Script_Reinit_ScaleSpriteOnX
Script_Reinit_ScaleSpriteOnY
Script_Reinit_SetSpriteRolling
Script_Reinit_MorphPaletteSprite
Script_Reinit_Display3DSprite

Script_Reinit_PlaySound
Script_Reinit_PlaySyncSound
Script_Reinit_Sound

Script_Reinit_Wait
```

The existence/nonexistence of an explicit reinit name should not be treated as
the only evidence that an action is resettable. The generic reinit dispatcher is
the authoritative source for pairings.

---

## 22. Resource relationships

Although this document focuses on function IDs, the handlers make sense only in
the context of the SCX resource lists documented in `script-opcodes.md`.

The strongest current relationships are:

| Function family | Resource relationship |
|---|---|
| camera | current-scene camera database |
| body animation | animation resources / character objects |
| `MoveObjectOnPath` | path resources (`.3DP` in observed SCX data) |
| `AnimationFromExternalScene` | scene resources + animation resources |
| sprite functions | sprite/visual resources, embedded/associated `.3DO`, palettes |
| sound functions | SCX sound-resource list |
| `SendMessage` | unresolved runtime message/event namespace |

This reinforces the interpretation of an IAM function record as a high-level
designer action over typed engine resources, not a low-level CPU-like
instruction.

---

## 23. Reconstructed Quantic C/IAM execution model

The currently supported conceptual model is:

```text
IAM editor
    |
    | designer selects functions/actions and supplies typed parameters
    v
Quantic C / internal IAM representation
    |
    | build/serialization
    v
SCX script template
    |
    +-- function groups
    +-- 0x18-byte function records
    +-- semantic/raw parameter storage
    +-- synchronization links
    +-- resource references
    |
    | load-time index -> pointer relocation
    v
Runtime script template registry
    |
    | Script_MakeInstance()
    v
mutable script instance
    |
    +-- cloned function records
    +-- cloned/mutable parameters
    +-- fixed sync links
    +-- per-instance sprite state where required
    |
    | Script_PlayScript()
    v
current synchronized function group
    |
    +-- native Script_* action
    +-- native Script_* action
    +-- ...
    |
    | selected handler returns OR into "group still active"
    v
advance / remain / repeat / reinit
```

This model explains several otherwise puzzling Runtime features at once:

- semantic parameter selectors;
- fixed-size function records;
- `SyncFunction`;
- pointer/index conversion;
- explicit instance construction;
- paired action/reinit handlers;
- blocking vs immediate handler returns.

---

## 24. Recommended OpenNomad representation

The implementation should distinguish serialized IDs from native dispatch
metadata.

A useful high-level enum is:

```cpp
enum class IAMFunctionId : std::uint32_t {
    SelectCamera                = 0x01000001,
    InterpolateCameras          = 0x01000002,

    SelectBodyAnimation         = 0x02000004,
    Unknown_02000026            = 0x02000026,
    SelectRelativeBodyAnimation = 0x0200002A,

    MoveObjectOnPath            = 0x03000008,
    AnimationFromExternalScene  = 0x0300001A,
    MorphObject                 = 0x03000021,
    ScaleObjectX                = 0x03000023,
    ScaleObjectY                = 0x03000024,
    ScaleObjectZ                = 0x03000025,
    SwapObject                  = 0x0300002B,

    SetSpriteType               = 0x0400000C,
    Display3DSpriteOnPath       = 0x0400000D,
    ChainObjects                = 0x04000011,
    ScaleSpriteOnX              = 0x0400001B,
    ScaleSpriteOnY              = 0x0400001C,
    SetSpriteRolling            = 0x0400001D,
    SetSpritePalette            = 0x0400001E,
    SetSpriteDefaultPalette     = 0x0400001F,
    MorphPaletteSprite          = 0x04000020,
    Display3DSprite             = 0x04000028,
    SetSpriteFrame              = 0x04000029,

    PlaySound                   = 0x05000014,
    PlaySyncSound               = 0x05000015,
    StopSound                   = 0x05000016,

    Wait                        = 0x06000017,
    SendMessage                 = 0x06000027,
};
```

Keep legacy/special IDs separate so they are not accidentally treated as fully
supported current operations:

```cpp
enum class IAMLegacyFunctionId : std::uint32_t {
    UnknownAnimation_02000005 = 0x02000005,
    UnknownAnimation_02000006 = 0x02000006,
    UnknownAnimation_03000007 = 0x03000007,
    UnknownAnimation_0300000A = 0x0300000A,
    Unknown_05000013          = 0x05000013,
};
```

A dispatcher table should carry behavior metadata explicitly rather than
re-encoding it in switch structure:

```cpp
struct IAMFunctionDescriptor {
    IAMFunctionId id;
    std::string_view debugName;

    bool mainDispatch;
    bool contributesToGroupActive;
    bool hasReinit;

    // Eventually:
    // parameter schema
    // action handler
    // reinit handler
};
```

This makes the reverse-engineered distinctions visible in the codebase.

---

## 25. Implementation invariants for OpenNomad

The following rules should be treated as architectural constraints.

### Function identity

- Compare the complete 32-bit function ID.
- Do not reduce the ID to the low byte.
- Preserve unknown IDs during parsing.
- Log unknown IDs with all raw function-record state and parameters.

### Serialized/runtime separation

- `+0x08` and `+0x0C` are serialized indexes before fixup, not file pointers.
- Build explicit runtime references after validation.
- Keep raw serialized structures separate from mutable script-instance types.

### Instances

- Do not normally mutate shared loaded script templates during playback.
- Clone mutable function/progress state per instance.
- Rebuild synchronization links inside the instance.
- Preserve/copy mutable parameter state.
- Support instance-local sprite allocation/state.

### Scheduling

- A contributing handler's return means **group still active**, not generic
  success.
- Non-contributing actions may execute inside the same synchronized group.
- Advance only when no contributing action remains active.
- Preserve reinit/repeat behavior.

### Alternate function paths

- Do not delete or stub family-`0x04` functions merely because they are absent
  from the currently mapped main switch.
- Investigate lifecycle/alternate dispatch before deciding how they are invoked.

### Unknowns

- `0x02000026` must remain a first-class parsed function ID.
- Legacy/special IDs should produce structured diagnostics rather than parser
  failure.
- Unknown parameter selectors/slots should remain available for future tracing.

---

## 26. Recommended diagnostics

For every instantiated IAM function, debug builds should be able to log:

```text
script name / script ID
group index
function index
function ID
reconstructed function name
class
low-byte ordinal

raw +0x04
raw/relocated parameter base
raw/relocated sync reference
+0x10 state
+0x14 state

raw positional parameter values
known semantic selector -> slot aliases

main-dispatch status
contributes-to-group status
reinit availability
```

For unsupported functions, prefer a warning shaped like:

```text
IAM function unsupported:
  script="..."
  id=0x02000026
  class=0x02
  ordinal=0x26
  group=...
  params=[...]
```

rather than the misleading term “unknown bytecode opcode”.

---

## 27. Highest-value remaining reverse-engineering work

### 27.1 Identify `0x02000026`

This is the only unresolved current-format `Script_GetNumParam` ID.

Best next steps:

1. scan **all retail SCX files** for `0x02000026`;
2. dump containing script names and full raw parameter vectors;
3. compare those scripts with gameplay footage/scene purpose;
4. inspect every cross-reference to the parameter selector pattern:
   `00, 02, 03, 10, 11`;
5. search the IAM editor screenshot/catalogue for a body-animation function
   matching the observed parameter shape;
6. compare other executable/localized builds for preserved diagnostics.

### 27.2 Resolve the family-`0x04` alternate invocation path

The strongest targets are:

```text
SetSpriteType
Display3DSpriteOnPath
ScaleSpriteOnX
ScaleSpriteOnY
SetSpriteRolling
SetSpritePalette
SetSpriteDefaultPalette
Display3DSprite
SetSpriteFrame
```

Trace:

- `Script_MakeInstance`;
- generic reinit;
- sprite-instance construction;
- function-record walkers;
- indirect calls;
- any callback/function-pointer tables;
- scripts containing those IDs in retail SCX assets.

### 27.3 Recover the semantic parameter enum

Trace all callers of:

```text
Script_GetNumParam
```

especially:

```text
Script_ModifyObject1/2
Script_ModifyCamera1/2
Script_ModifyXYZPtr
```

The goal is an enum such as:

```cpp
enum class IAMParameterKind : uint32_t {
    // original/semantic names once proven
};
```

with an exact schema per function.

### 27.4 Recover the missing ordinal IDs

Search for comparisons or tables involving:

```text
03 09 0B 0E 0F 10 12 18 19 22
```

in the low byte while preserving the class byte.

These may be:

- removed retail functions;
- editor-only functions;
- old format compatibility;
- paths hidden behind indirect dispatch;
- simply unused ordinals.

### 27.5 Correlate with the IAM editor function catalogue

The surviving IAM screenshot visibly contains a designer-facing function list.

A careful reconstruction should correlate:

```text
editor-visible action label
    ↕
0xCC0000NN
    ↕
Script_* native implementation
```

The final goal is not merely to know that `0x03000008` calls
`Script_MoveObjectOnPath`, but to reconstruct the original IAM/Quantic C
designer API as closely as possible.

---

## 28. Suggested automated SCX inventory report

A small offline analysis tool should iterate every retail SCX and emit at least:

```text
SCX file
script template name
script ID
function group
function ID
known function name
raw +0x04
parameter pool index
raw positional parameters
sync-function index/link
+0x10
+0x14
```

Aggregate reports should include:

```text
distinct function IDs across the entire game
count per function ID
count per SCX
count per script name

all occurrences of 0x02000026
all occurrences of 0x05000013
all occurrences of legacy IDs

which family-0x04 IDs appear in shipping assets
which functions occur in synchronized groups together

parameter-vector distributions per ID
+0x04 distributions per ID
+0x10/+0x14 initial-state distributions
```

This will likely solve several remaining questions faster than further isolated
decompilation.

---

## 29. Known pitfalls

### Do not call these simple bytecode opcodes

They are typed function IDs inside structured records.

### Do not collapse the 32-bit IAM/SCX system with the one-byte scenario VM

They have different instruction/record formats, dispatchers and lifecycles.

### Do not trust diagnostic strings blindly

They are source-like and extremely useful, but copy/paste errors exist.

### Do not infer “unused” from absence in the main switch

The sprite family disproves that assumption.

### Do not infer a dense parameter list from `Script_GetNumParam`

Semantic selectors map only selected roles to positional slots.

### Do not infer source-level parameter enum names yet

Several numeric selectors have obvious-looking behavior, but exact original
names remain unrecovered.

### Do not treat `0x02000026` as invalid data

Runtime's current parameter mapper explicitly recognizes it.

### Do not throw away legacy IDs

The retail executable retains compatibility/inspection logic for them.

---

## 30. Source references

### Primary binary/data evidence

- Windows retail `Runtime.exe`, SHA-256
  `55f7120bfea7891b048c64e3682f3259cdbf2719a43fa24e42254b753c95d2ef`
- `aventure.SCX`
- `Grid.SCX`
- retail `IAM` resources including `AREA`, `GLOBAL`, `START`
- retail `.TAG` registries (`VARIABLES`, `ZONES`, `AREAS`, `ADDRESSES`,
  `SCENES`, `OBJECTS`, `CAMERAS`, `DIALOGS`)

### OpenNomad reverse-engineering documentation

- [`script-opcodes.md`](script-opcodes.md)
- [`runtime-globals.md`](runtime-globals.md)

### Historical Quantic Dream material

- [Omikron development archive / collected diary](https://omikrongame.blogspot.com/1999/12/omikron-nomad-soul_31.html)
- [Week 11 — IAM](https://omikrongame.blogspot.com/1997/07/week-11.html)
- [Weeks 14 & 15](https://omikrongame.blogspot.com/1997/08/weeks-14-15.html)
- [Weeks 25 & 26 — editor/tool descriptions](https://omikrongame.blogspot.com/1997/11/weeks-25-26.html)
- [Weeks 33 & 34 — zones and conditions](https://omikrongame.blogspot.com/1998/01/weeks-33-34.html)
- [Week 37 — exposing Fabien's functions in IAM](https://omikrongame.blogspot.com/1998/01/week-37.html)
- [Week March 3 — production IAM scripting](https://omikrongame.blogspot.com/1998/03/week-march-3.html)
- [Nomad Soul developer editor screenshots](https://omikrongame.blogspot.com/1999/06/nomad-soul-developer-screens.html)

---

## Appendix A. Compact reference table

| ID | Name | Action | Reinit | Main dispatch | Blocks |
| --- | --- | --- | --- | --- | --- |
| 0x01000001 | SelectCamera | 0x004A4580 | — | yes | no |
| 0x01000002 | InterpolateCameras | 0x004A4630 | 0x004A4450 | yes | yes |
| 0x02000004 | SelectBodyAnimation | 0x004A35D0 | 0x004A3290 | yes | yes |
| 0x02000026 | UNKNOWN_02000026 | — | — | no known path | unknown |
| 0x0200002A | SelectRelativeBodyAnimation | 0x004A3AD0 | 0x004A3430 | yes | yes |
| 0x03000008 | MoveObjectOnPath | 0x0046F400 | 0x0046EEB0 | yes | yes |
| 0x0300001A | AnimationFromExternalScene | 0x00470060 | 0x0046F0C0 | yes | yes |
| 0x03000021 | MorphObject | 0x00470440 | 0x0046F200 | yes | yes |
| 0x03000023 | ScaleObjectX | 0x004707A0 | 0x0046EB50 | yes | yes |
| 0x03000024 | ScaleObjectY | 0x004709E0 | 0x0046EC70 | yes | yes |
| 0x03000025 | ScaleObjectZ | 0x00470C20 | 0x0046ED90 | yes | yes |
| 0x0300002B | SwapObject | 0x00470E60 | 0x0046E9C0 | yes | no |
| 0x0400000C | SetSpriteType | 0x004A2520 | — | not in main dispatcher | n/a |
| 0x0400000D | Display3DSpriteOnPath | 0x004A2150 | 0x004A1B20 | not in main dispatcher | n/a |
| 0x04000011 | ChainObjects | 0x004A1E80 | 0x004A19C0 | yes | yes |
| 0x0400001B | ScaleSpriteOnX | 0x004A25E0 | 0x004A1810 | not in main dispatcher | n/a |
| 0x0400001C | ScaleSpriteOnY | 0x004A2790 | 0x004A18A0 | not in main dispatcher | n/a |
| 0x0400001D | SetSpriteRolling | 0x004A2940 | 0x004A1930 | not in main dispatcher | n/a |
| 0x0400001E | SetSpritePalette | 0x004A2AF0 | — | not in main dispatcher | n/a |
| 0x0400001F | SetSpriteDefaultPalette | 0x004A2C60 | — | not in main dispatcher | n/a |
| 0x04000020 | MorphPaletteSprite | 0x004A2D10 | 0x004A1C90 | yes | yes |
| 0x04000028 | Display3DSprite | 0x004A2FB0 | 0x004A1DE0 | not in main dispatcher | n/a |
| 0x04000029 | SetSpriteFrame | 0x004A31B0 | — | not in main dispatcher | n/a |
| 0x05000014 | PlaySound | 0x004A12D0 | 0x004A0FB0 | yes | no |
| 0x05000015 | PlaySyncSound | 0x004A14D0 | 0x004A1100 | yes | yes |
| 0x05000016 | StopSound | 0x004A16D0 | 0x004A11F0 | yes | no |
| 0x06000017 | Wait | 0x004A0EE0 | 0x004A0E50 | yes | yes |
| 0x06000027 | SendMessage | 0x004A0E80 | — | yes | no |


## Appendix B. Confidence-sensitive names

The following names should be treated differently from the directly
diagnostic-confirmed actions:

| ID | Working name | Status |
|---:|---|---|
| `0x01000001` | `SelectCamera` | Semantics are clear; exact original source/editor label not recovered. |
| `0x02000026` | `UNKNOWN_02000026` | Current-format ID; semantics unresolved. |
| `0x06000017` | `Wait` | Strongly established by `Script_Reinit_Wait` and handler behavior. |
| `0x02000005` | unknown | Legacy/special animation-reference ID. |
| `0x02000006` | unknown | Legacy/special animation-reference ID. |
| `0x03000007` | unknown | Legacy/special animation-reference ID. |
| `0x0300000A` | unknown | Legacy/special animation-reference ID. |
| `0x05000013` | unknown | Special/reserved family-`0x05` ID. |

---

## Appendix C. Working reverse-engineering terminology

| Term | Meaning in this documentation |
|---|---|
| **IAM function** | One high-level 32-bit `Script_*` operation likely exposed/generated by IAM/Quantic C. |
| **function ID** | Full `0xCC0000NN` identifier stored in a `0x18`-byte function record. |
| **class** | High byte `CC`, grouping related subsystem functions. |
| **ordinal** | Low byte `NN`; strongly suspected global IAM function number. |
| **function record** | `0x18`-byte serialized/runtime structure containing ID, parameters, sync link and mutable state. |
| **function group** | Working term for the primary function plus linked/synchronized functions evaluated together by `Script_PlayScript`. |
| **blocks group** | Handler return contributes to the accumulator that prevents advancement to the next group. |
| **reinit** | Opcode-specific reset logic used when script/function state is restarted or repeated. |
| **parameter selector** | Semantic type/role key consumed by `Script_GetNumParam()` to obtain a positional parameter slot. |
| **legacy/special ID** | Typed function ID recognized by some retail script machinery but not the current general parameter/execution path. |
| **scenario/event VM** | Separate one-byte script interpreter used by IAM area/start/game data; not the `0xCC0000NN` namespace. |

---

## Appendix D. OpenNomad implementation checklist

- [ ] Add a typed `IAMFunctionId` enum using the 28 current IDs.
- [ ] Preserve five known legacy/special IDs separately.
- [ ] Keep `0x02000026` parseable and diagnosable.
- [ ] Represent the complete 32-bit ID; never dispatch on low byte alone.
- [ ] Preserve `ScriptFunction +0x04` until semantics are proven.
- [ ] Decode serialized parameter indexes separately from runtime pointers.
- [ ] Decode serialized sync-function indexes separately from runtime pointers.
- [ ] Maintain mutable per-instance `+0x10` / `+0x14` state.
- [ ] Clone mutable function and parameter state in script instances.
- [ ] Rebuild synchronization links inside each instance.
- [ ] Keep per-function `contributesToGroupActive` metadata explicit.
- [ ] Implement main-dispatched functions independently of alternate-path functions.
- [ ] Do not mark family-`0x04` functions unused merely because they are absent from the main switch.
- [ ] Implement/reproduce opcode-specific reinit behavior.
- [ ] Add structured unknown-function diagnostics.
- [ ] Add raw parameter dumps for unsupported functions.
- [ ] Add an offline all-SCX function inventory tool.
- [ ] Recover and name the semantic parameter-selector enum.
- [ ] Identify `0x02000026`.
- [ ] Resolve `0x05000013`.
- [ ] Resolve the alternate invocation path for sprite functions.
- [ ] Correlate editor-visible IAM function names with function IDs.
- [ ] Keep this system separate from the compact IAM scenario/event VM.
