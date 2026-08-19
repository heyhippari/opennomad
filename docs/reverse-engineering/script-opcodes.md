# Omikron scripting systems and script opcodes

> **Status:** work-in-progress reverse-engineering documentation for OpenNomad.
>
> This document describes the scripting systems currently identified in the Windows release of *Omikron: The Nomad Soul*, with particular emphasis on the `Script_*` subsystem serialized in `.SCX` files and its 32-bit function/opcode identifiers.
>
> It also documents the separate compact scenario/event bytecode VM used by `IAM` area scripts, because both systems are legitimately “scripts” in Runtime and must not be accidentally merged into one opcode namespace.

## Source precedence and confidence

The sources used here are, in descending order of authority:

1. **`Runtime.exe` behavior** — authoritative for dispatch, data layout, pointer/index relocation, execution state, handler behavior, and error handling.
2. **Observed retail data**, principally `aventure.SCX`, `Grid.SCX`, `IAM/START`, and `IAM/AREA` — authoritative for actual serialized values and useful for validating strides, counts, resource names, and instruction bytes.
3. **OpenNomad experiments and previous reverse-engineering notes** — useful corroboration, but subordinate to Runtime where there is a conflict.

The `Runtime.exe` currently used for this analysis has:

- PE image base: `0x00400000`
- linker timestamp: `1999-10-04 20:31:50`
- SHA-256: `55f7120bfea7891b048c64e3682f3259cdbf2719a43fa24e42254b753c95d2ef`

Addresses in this document refer to that executable and are not expected to be stable across other builds.

Confidence labels used below:

- **Confirmed — Runtime:** directly demonstrated by code in `Runtime.exe`.
- **Confirmed — data:** directly demonstrated by retail serialized data.
- **Corroborated:** Runtime behavior and retail data agree.
- **Tentative:** the structural behavior is known, but the intended higher-level meaning is not yet proven.
- **Unknown:** the field/function exists, but its semantics have not yet been established.

---

# 1. Critical distinction: Omikron has at least two script systems

The word **script** is used for more than one execution system in Runtime.

At the current stage of reverse engineering, two distinct systems must be kept separate.

## 1.1 `.SCX` `Script_*` function system

This is the system described by Runtime diagnostics such as:

```text
Script_PlayScript()
Script_GetNumParam()
Script_MakeInstance()
Script_Reinit_InterpolateCameras()
Script_MoveObjectOnPath()
Script_SelectBodyAnimation()
Script_SetSpriteFrame()
Script_PlaySound()
Script_Wait()
Script_SendMessage()
```

Its executable operations are identified by 32-bit values of the form:

```text
0xGG0000NN
```

Examples:

```text
0x01000002  InterpolateCameras
0x02000004  SelectBodyAnimation
0x03000008  MoveObjectOnPath
0x04000029  SetSpriteFrame
0x05000014  PlaySound
0x06000017  Wait
```

This system is stored in `.SCX` script-definition sections and is designed around:

- script templates;
- mutable runtime script instances;
- arrays of fixed-size function records;
- a shared parameter-value pool;
- linked/synchronized function records;
- per-frame progress;
- opcode-specific reinitialization;
- repeated script groups;
- resources such as animations, paths, sounds, sprites, and scenes.

This document refers to it as the **SCX Script system**.

## 1.2 Compact scenario/event bytecode VM

A separate VM is used by scenario/event scripts embedded in `IAM` data.

The best-confirmed example is the startup script in `IAM/AREA` record `118`:

```text
46 1D 00 FF FF 13 00
```

which is interpreted as:

```text
opcode 0x46
operand 0 = 29
operand 1 = -1
operand 2 = 19
```

and eventually opens interface `29`, the main-menu interface.

This system uses:

- one-byte opcodes;
- opcode-specific variable-length operands;
- script/event contexts;
- an instruction pointer;
- scenario/event scheduling;
- handlers in the `0x0040xxxx` region.

This document refers to it as the **scenario/event VM**.

## 1.3 Do not combine the opcode namespaces

The following are **not** equivalent:

```text
SCX Script function ID:
    0x04000029

scenario/event opcode:
    0x46
```

They use different:

- serialized layouts;
- dispatchers;
- runtime structures;
- operand mechanisms;
- scheduling models;
- responsibilities.

A reimplementation should model them as separate interpreters/subsystems unless future Runtime analysis proves a stronger shared abstraction.

---

# 2. High-level architecture of the SCX Script system

A simplified model is:

```text
.SC X file
   |
   +-- main tagged block
       |
       +-- resource tables
       |
       +-- tag 0xDEAD0002
           |
           +-- ScriptTemplate[scriptCount]      0x64 bytes each
           |
           +-- shared function parameter values
           |
           +-- function-record arrays          0x18 bytes each
           |
           +-- sync/linked function arrays     0x18 bytes each
           |
           +-- auxiliary script metadata

Runtime loader
   |
   +-- allocates/copies data
   +-- converts serialized indexes to pointers
   +-- resolves named resources
   +-- prepares ScriptList

Script_MakeInstance()
   |
   +-- clones mutable script state
   +-- clones function records
   +-- clones parameter lists
   +-- fixes sync-function links
   +-- may allocate per-instance sprites

Script_PlayScript()
   |
   +-- chooses current function group
   +-- walks linked/sync functions
   +-- calls supported per-frame handlers
   +-- ORs selected handler returns into
       "group still active"
   +-- advances group when nothing blocks
   +-- repeats/reinitializes if requested
   +-- finishes script otherwise
```

The serialized representation is therefore not just a stream of bytecode instructions.

It is closer to a serialized graph/table representation of script functions and their parameters, transformed into pointer-rich runtime objects when loaded.

---

# 3. SCX container context

The complete `.SCX` format is broader than scripting, but the container fields relevant to the Script subsystem are included here because they determine where script data begins and ends.

## 3.1 File header

**Confirmed — Runtime and data.**

The beginning of the observed version-5 `.SCX` files is:

| Offset | Type | Meaning | Confidence |
|---:|---|---|---|
| `0x00` | `u32` | magic `0x00DEAD00` | Confirmed |
| `0x04` | `u32` | version, observed/required as `5` | Confirmed |
| `0x08` | `u32` | unknown, observed as `8` in `aventure.SCX` and `Grid.SCX` | Unknown |
| `0x0C` | `u32` | size of the main tagged block | Confirmed |

The main block starts at:

```text
file + 0x10
```

and Runtime reads exactly the size stored at `+0x0C`.

Observed examples:

```text
Grid.SCX:
    mainBlockSize = 0x1019
    main block: file 0x10 .. 0x1028 inclusive
    next byte:  0x1029

aventure.SCX:
    mainBlockSize = 0x41C8
    main block: file 0x10 .. 0x41D7 inclusive
    next byte:  0x41D8
```

This resolves an earlier unknown: header field `+0x0C` is not merely an arbitrary count or offset.

## 3.2 Main-block tags

The main block is parsed as a sequence of 32-bit tags:

```text
0xDEAD0000 .. 0xDEAD000A
0xDEADFFFF
```

The parser dispatches through a jump table around `0x0044A040`.

Current section meanings are:

| Tag | Current meaning | Confidence |
|---|---|---|
| `0xDEAD0000` | `.3DP` path-resource list | Strongly corroborated |
| `0xDEAD0001` | `.3DA` animation-resource list | Strongly corroborated |
| `0xDEAD0002` | script templates/functions/parameters | Confirmed |
| `0xDEAD0003` | sound-resource list | Strongly corroborated |
| `0xDEAD0004` | `.3DO` / sprite visual-resource list | Strongly corroborated |
| `0xDEAD0005` | scene-resource list | Strongly corroborated |
| `0xDEAD0006` | unknown large table | Unknown |
| `0xDEAD0007` | unknown fixed-size records copied to global storage | Unknown |
| `0xDEAD0008` | sets/resets a fixed global limit/value (`0x100`) | Unknown |
| `0xDEAD0009` | reserved/no-op in current parser | Confirmed behavior, purpose unknown |
| `0xDEAD000A` | external/auxiliary block passed to another loader | Unknown |
| `0xDEADFFFF` | end of main tagged block | Confirmed |

The Script subsystem is primarily rooted in tag:

```text
0xDEAD0002
```

but script functions refer to resources supplied by several of the other sections.

---

# 4. `0xDEAD0002`: script-definition section

## 4.1 Section beginning

The tag-2 parser begins with:

```text
u32 scriptCount
```

followed by:

```text
scriptCount * 0x64-byte script template records
```

Observed counts:

```text
Grid.SCX:
    8 script templates

aventure.SCX:
    22 script templates
```

The first template record begins immediately after the count.

Observed first names include:

```text
Grid.SCX:
    "1KaylArrives"

aventure.SCX:
    "effects2_smoke2"
```

The names are valuable evidence that this is the cinematic/action `Script_*` system rather than only a generic SCX resource table.

## 4.2 Shared function-parameter value pool

After the template records, the section contains:

```text
u32 functionParameterValueCount
u32 functionParameterValues[functionParameterValueCount]
```

The loader stores this approximately as:

```text
ScriptList + 0x44 = valueCount
ScriptList + 0x48 = pointerToValues
```

Function records do not store native parameter pointers on disk.

Instead, their serialized `+0x08` value is an **index** into this pool.

During loading Runtime converts:

```text
serialized index
```

into:

```text
runtime pointer =
    scriptList->parameterValues
    + index * 4
```

This is one of the clearest examples of Runtime converting a disk-friendly representation into an in-memory pointer graph.

## 4.3 Function arrays

Each script template references two classes of `0x18`-byte function records:

1. a primary function/group array;
2. a second array used by serialized `SyncFunction` indexes and runtime linked-function traversal.

The exact original authoring terminology for the distinction is not completely recovered.

Runtime diagnostics explicitly use the term:

```text
SyncFunction
```

for the second relationship.

At runtime, the `+0x0C` field of a function record becomes a pointer and is traversed like:

```text
function = function->syncOrNext;
```

so it is operationally a linked function chain.

---

# 5. Script template record

Each serialized script-template record is:

```text
0x64 bytes
```

The layout is only partially named.

A conservative representation is:

```c
struct SerializedScriptTemplateV5 {
    uint32_t owner_placeholder;       // +0x00; overwritten/fixed at load
    char     name[20];                // +0x04

    uint16_t unknown_18;              // +0x18
    uint16_t script_id;               // +0x1A

    uint16_t runtime_state;            // +0x1C
    uint16_t flags_state;              // +0x1E

    uint32_t function_group_count;     // +0x20
    uint32_t current_group_index;      // +0x24
    uint32_t function_groups;          // +0x28 serialized/runtime-dependent

    uint32_t sync_function_count;      // +0x2C
    uint32_t sync_functions;           // +0x30 serialized/runtime-dependent

    int32_t  repeat_limit;             // +0x34
    uint32_t repeat_index;             // +0x38

    uint8_t  unknown_3C[0x18];         // +0x3C .. +0x53

    uint32_t paired_or_linked_script;  // +0x54 runtime pointer
    float    elapsed_time;             // +0x58

    uint8_t  unknown_5C;               // +0x5C
    uint8_t  paired_gate;               // +0x5D
    uint8_t  context_flags[4];         // +0x5E .. +0x61
    uint16_t unknown_62;               // +0x62
}; // 0x64
```

This is intentionally not presented as a final C structure.

Several fields are mutated, relocated, or repurposed during load and instancing, so a clean implementation should maintain separate serialized and runtime types.

## 5.1 `+0x00`: owner/list pointer

The first dword in a serialized template behaves like a pointer-shaped placeholder.

Runtime overwrites/fixes it so that a script can refer back to its owning `ScriptList`.

Do not treat the serialized value as a meaningful file address.

## 5.2 `+0x04`: script name

**Confirmed.**

A fixed:

```text
char[20]
```

contains the script name.

Observed names are NUL-terminated when shorter than the field.

The exact character encoding for all localized assets has not yet been exhaustively tested.

## 5.3 `+0x1A`: script ID

**Confirmed — Runtime.**

A lookup function around `0x0044A0F0` compares a requested 16-bit value against:

```text
WORD [script + 0x1A]
```

This is therefore a script identifier, distinct from the textual name.

## 5.4 `+0x1C`: runtime active/state

**Confirmed behavior, exact enum unknown.**

`Script_PlayScript` checks this field early.

A zero state causes an immediate non-normal return rather than executing the function graph.

Reinitialization sets the field back to an active value.

## 5.5 `+0x1E`: flags/state

**Confirmed bitfield behavior, meanings partial.**

The low nibble is manipulated as execution state.

Upper bits are tested/updated by context and lifecycle logic.

The complete bit enumeration remains unknown.

## 5.6 `+0x20` / `+0x24` / `+0x28`: primary groups

`Script_PlayScript` uses:

```text
+0x20  number of primary function groups
+0x24  current function-group index
+0x28  pointer to first primary 0x18-byte function record
```

The current record is obtained approximately as:

```c
function =
    script->function_groups
    + script->current_group_index * 0x18;
```

The record can then link to additional functions through its runtime `+0x0C` pointer.

## 5.7 `+0x2C` / `+0x30`: sync/linked function table

These fields hold:

```text
+0x2C  number of second/sync function records
+0x30  pointer to second/sync function array
```

Serialized function records can refer into this table by index.

At load/instance construction those indexes become pointers.

## 5.8 `+0x34` / `+0x38`: repetition

**Confirmed.**

Runtime compares the current repetition count against the configured limit.

A repeat limit of:

```text
0xFFFFFFFF / -1
```

is treated specially as unbounded repetition.

When a script reaches the end of its groups and should repeat, Runtime invokes the reinitialization dispatcher and begins again.

## 5.9 `+0x54`: paired/chained script pointer

Runtime functions around:

```text
0x0044B280
0x0044B2C0
0x0044B300
```

manipulate this relationship.

`Script_PlayScript` also tests it as part of completion/gating behavior.

It is safe to describe the field as a **runtime link to another script**.

The exact intended authoring concept — pair, chain, parent/child, synchronization partner, or another term — is still tentative.

## 5.10 `+0x58`: elapsed script time

**Confirmed.**

Every `Script_PlayScript` call increments this float by the global frame-time value at approximately:

```text
0x00531218
```

It is therefore script-level elapsed execution time.

## 5.11 `+0x5C .. +0x61`: context/gating state

These bytes are read/written by execution-context logic and by paired-script handling.

Their exact individual names and bit meanings are not yet solved.

---

# 6. Script function record

The central executable record is exactly:

```text
0x18 bytes
```

## 6.1 Serialized form

A conservative structure is:

```c
struct SerializedScriptFunction {
    uint32_t function_id;          // +0x00
    uint32_t unknown_04;           // +0x04
    uint32_t parameter_index;      // +0x08
    uint32_t sync_function_index;  // +0x0C
    int32_t  execution_limit;      // +0x10
    int32_t  progress;             // +0x14
}; // 0x18
```

## 6.2 Runtime form

After relocation/instancing, the same logical record behaves more like:

```c
struct RuntimeScriptFunction {
    uint32_t function_id;                // +0x00
    uint32_t unknown_04;                 // +0x04
    uint32_t *parameters;                // +0x08
    RuntimeScriptFunction *sync_or_next; // +0x0C
    int32_t execution_limit;             // +0x10
    int32_t progress;                    // +0x14
}; // 0x18 on 32-bit Runtime
```

Do **not** serialize this runtime structure directly in a modern 64-bit implementation.

## 6.3 `+0x00`: function/opcode ID

**Confirmed.**

This is the 32-bit Script function identifier.

All currently recognized IDs use:

```text
0xGG0000NN
```

where:

```text
GG = family/category byte
NN = operation byte
```

Examples:

```text
0x01000002
0x03000025
0x04000029
0x06000017
```

Because the file is little-endian, `0x04000029` appears in bytes as:

```text
29 00 00 04
```

## 6.4 `+0x04`: unknown

This dword is preserved and copied by Runtime but has not yet been assigned a demonstrated generic semantic.

It should remain:

```text
unknown_04
```

until an opcode or scheduler path gives it a clear meaning.

## 6.5 `+0x08`: parameter index -> parameter pointer

**Confirmed.**

On disk:

```text
u32 index
```

into the owning ScriptList's 4-byte parameter pool.

At runtime:

```text
pointer to first parameter dword
```

for this function.

A simple accessor around `0x0044C680` behaves approximately as:

```c
uint32_t Script_GetParamU32(function, index)
{
    return function->parameters[index];
}
```

There are corresponding float-oriented accessors.

## 6.6 `+0x0C`: sync-function index -> linked function pointer

**Confirmed.**

On disk:

```text
u32 sync function index
```

with:

```text
0xFFFFFFFF
```

representing no linked function.

At runtime:

```text
RuntimeScriptFunction*
```

or null.

`Script_PlayScript` follows this field to process additional functions in the same group.

## 6.7 `+0x10`: execution/frame limit

This field gates handler execution.

The main dispatcher checks approximately:

```c
if (function->progress < function->execution_limit ||
    function->execution_limit == -1) {
    dispatch(function);
}
```

Runtime diagnostics and handler behavior suggest a concept similar to:

```text
NbTrames / frame count / execution limit
```

but the precise semantic can vary by function.

For documentation, `execution_limit` is safer than claiming it is always a duration in rendered frames.

## 6.8 `+0x14`: progress/current frame

This is mutable per-instance progress.

Many handlers increment or inspect it.

A useful working interpretation is:

```text
current frame / execution progress
```

but it should remain a generic progress counter in implementation until each opcode is understood.

---

# 7. Serialized indexes and runtime pointers

The SCX Script representation is deliberately pointer-like after loading.

Runtime contains explicit conversion paths whose diagnostics include:

```text
Script_FunctionsAdressesToIndexes()
Script_FunctionsIndexesToAdresses()

Address of FuncParams isn't valid
Address of SyncFunction isn't valid
Index of FuncParams too big
Index of SyncFunction too big
```

The typo `Adresses` is present in Runtime strings.

## 7.1 Load-time conversion

Conceptually:

```text
parameter index
    ->
pointer into shared/copy parameter list

sync-function index
    ->
pointer into sync-function array
```

## 7.2 Serialization conversion

The inverse code converts native pointers back into indexes.

This strongly suggests that the format was designed around a 32-bit in-memory authoring/runtime structure whose pointer fields were normalized into indexes for disk storage.

## 7.3 Reimplementation rule

OpenNomad should use explicit serialized structures:

```cpp
struct SerializedScriptFunction { ... };
```

and explicit runtime structures:

```cpp
struct ScriptFunction { ... };
```

rather than:

```cpp
read bytes directly into pointer-bearing struct;
```

This is especially important on 64-bit platforms.

---

# 8. Function parameter pool

## 8.1 Physical representation

Function parameter values are stored as:

```text
4-byte slots
```

in a shared pool.

Handlers interpret individual slots according to the function ID and parameter semantic.

A slot can therefore represent, depending on the opcode:

- integer;
- float;
- resource/index reference;
- signed value;
- object/sprite/script identifier;
- another encoded quantity.

A generic parser must not globally type the pool as only integers or only floats.

## 8.2 `Script_GetNumParam`

Runtime contains an explicit function whose own diagnostic names it:

```text
Script_GetNumParam()
```

at approximately:

```text
0x0044C090
```

It accepts:

- a Script function record;
- a parameter semantic/type selector;

and returns the corresponding numeric slot index for that function.

Unsupported combinations emit:

```text
Script_GetNumParam(): Function type not supported yet.

Script_GetNumParam(): Bad parameter type %x. Can't be found in this kind of function!
```

## 8.3 Why this matters

The existence of `Script_GetNumParam` shows that parameter position is not intended to be understood only as:

```text
param 0
param 1
param 2
```

Runtime has a higher-level concept of parameter kinds and maps those semantic selectors to different slot numbers per function.

The complete semantic parameter-type enumeration has not yet been recovered and should be documented separately as it is reconstructed.

## 8.4 IDs recognized by `Script_GetNumParam`

The current Runtime compare/jump tree recognizes at least these 28 function IDs:

### Family `0x01`

```text
0x01000001
0x01000002
```

### Family `0x02`

```text
0x02000004
0x02000026
0x0200002A
```

### Family `0x03`

```text
0x03000008
0x0300001A
0x03000021
0x03000023
0x03000024
0x03000025
0x0300002B
```

### Family `0x04`

```text
0x0400000C
0x0400000D
0x04000011
0x0400001B
0x0400001C
0x0400001D
0x0400001E
0x0400001F
0x04000020
0x04000028
0x04000029
```

### Family `0x05`

```text
0x05000014
0x05000015
0x05000016
```

### Family `0x06`

```text
0x06000017
0x06000027
```

This set is already larger than the earlier saved per-frame/reinit table.

It should still **not** be assumed to represent every function ID ever emitted by Omikron's original authoring tools.

---

# 9. Function-ID encoding and families

## 9.1 Current pattern

All currently recognized IDs have:

```text
bits 31..24 = family/category
bits 23..8  = 0
bits 7..0   = operation number
```

or:

```text
0xGG0000NN
```

Recognized high-byte families are:

```text
0x01
0x02
0x03
0x04
0x05
0x06
```

## 9.2 Observed family behavior

The handler names strongly suggest the following organization:

| Family | Observed operations | Working interpretation |
|---:|---|---|
| `0x01` | camera operations | camera |
| `0x02` | body animation selection | body/character animation |
| `0x03` | path movement, external-scene animation, morph, scale, swap | scene/object animation |
| `0x04` | sprite display/type/scale/palette/frame, object chaining | sprite/visual |
| `0x05` | play/synchronize/stop sound | sound |
| `0x06` | wait, send message | control/messaging |

These family labels are **inferred from membership**.

No original enum names for the family byte have yet been recovered.

OpenNomad documentation/code should therefore avoid names such as:

```text
SCRIPT_FAMILY_CAMERA = 1
```

unless clearly marked as project terminology rather than an original Omikron symbol.

## 9.3 Middle 16 bits

Every currently recognized Runtime ID has zero bits in:

```text
0x00FFFF00
```

It is not yet proven that nonzero middle bits were impossible in the original authoring format.

A parser should therefore compare complete 32-bit IDs, not normalize or discard those bits unless additional evidence justifies it.

---

# 10. `Script_PlayScript`

## 10.1 Dispatcher

The main recurring Script execution function is at approximately:

```text
0x0044C860
```

Runtime diagnostics explicitly contain:

```text
Script_PlayScript(): ScriptPtr is NULL.
```

A useful recovered/working name is therefore:

```c
Script_PlayScript
```

## 10.2 High-level behavior

`Script_PlayScript` does not simply iterate every function every frame.

It implements:

- script state checks;
- paired/chained-script gating;
- context checks;
- one current primary function group;
- linked/sync functions in that group;
- per-function progress gating;
- function-ID dispatch;
- a group-level “still active” accumulator;
- group advancement;
- script repetition;
- opcode-specific reinitialization;
- script-level elapsed time;
- completion/failure behavior.

## 10.3 Group model

A primary function record acts as the root of a group.

Runtime then follows:

```text
function + 0x0C
```

as a linked/sync function pointer.

Conceptually:

```text
group 0:
    primary function
        -> sync/linked function
        -> sync/linked function
        -> null

group 1:
    primary function
        -> sync/linked function
        -> null
```

Only one primary group index is active at a time.

## 10.4 Per-function execution gate

Before dispatching a function, Runtime checks its mutable progress against the configured limit:

```c
if (progress < execution_limit ||
    execution_limit == -1) {
    dispatch(function);
}
```

This is separate from the handler's own internal timing/parameter logic.

## 10.5 The return value is a group-blocking signal

This resolves an important ambiguity in earlier notes.

For selected function IDs, Runtime does approximately:

```c
groupStillActive |= handler(function);
```

The accumulator is held in the low byte of a register during dispatch.

If, after processing the linked group:

```text
groupStillActive != 0
```

Runtime leaves the current group selected.

If:

```text
groupStillActive == 0
```

Runtime advances to the next primary group.

Therefore:

> A contributing handler's nonzero return means that its group still requires processing on a future frame.

It is **not** simply a conventional success result.

`Script_Wait` strongly corroborates this interpretation: it returns an active/nonzero state while time remains and stops blocking when the wait has completed.

## 10.6 Non-contributing handlers

Some handlers are called but their return value is ignored.

They are effectively non-blocking from the group's perspective.

This is what the earlier table called:

```text
Return contributes to group = No
```

Such a function can perform a side effect while another synchronized function determines how long the group remains active.

## 10.7 Group advancement

When no contributing function keeps the current group active:

```text
current_group_index++
```

If another group remains, execution continues from that group on subsequent processing.

## 10.8 End of group list and repetition

When the last group completes:

```text
repeat_index++
```

Runtime checks:

```text
repeat_limit
```

A value of:

```text
-1 / 0xFFFFFFFF
```

means repeat indefinitely.

If another repetition is needed, Runtime calls the script reinitialization path and restarts the function graph.

Otherwise the script moves toward completion.

## 10.9 Elapsed time

Each call adds the current global frame delta to:

```text
script + 0x58
```

regardless of the individual function progress counters.

## 10.10 Failure

A Script subsystem failure state checked through a helper around:

```text
0x0049EB70
```

causes `Script_PlayScript` to return:

```text
0xFF
```

The exact global error-state architecture is still not fully named.

## 10.11 Simplified pseudocode

The following is intentionally structural rather than instruction-for-instruction:

```c
uint8_t Script_PlayScript(Script *script)
{
    if (!script) {
        ScriptError("Script_PlayScript(): ScriptPtr is NULL.");
        return 0xFF;
    }

    if (script->runtime_state == 0)
        return 2;

    handle_context_and_paired_script_gates(script);

    bool group_still_active = false;

    if (script->function_group_count != 0) {
        ScriptFunction *fn =
            &script->function_groups[script->current_group_index];

        while (fn != NULL) {
            if (fn->execution_limit == -1 ||
                fn->progress < fn->execution_limit) {

                switch (fn->function_id) {
                    // supported main-dispatch IDs

                    // some:
                    //   group_still_active |= handler(fn);

                    // others:
                    //   handler(fn); // return ignored
                }
            }

            if (ScriptHasFailed())
                return 0xFF;

            fn = fn->sync_or_next;
        }

        if (!group_still_active) {
            ++script->current_group_index;

            if (script->current_group_index >=
                script->function_group_count) {

                ++script->repeat_index;

                if (script->repeat_limit == -1 ||
                    script->repeat_index < script->repeat_limit) {
                    Script_Reinit(script);
                    group_still_active = true;
                } else {
                    finish_script(script);
                }
            }
        }
    }

    script->elapsed_time += g_frame_delta;

    finalize_paired_and_context_state(script);

    return group_still_active ? 1 : 0;
}
```

Several exact return-state values and flag interactions have been omitted here because their full enum meanings are not yet established.

---

# 11. Script reinitialization

## 11.1 Reinit dispatcher

The main script reset/reinitialization path is around:

```text
0x0044A7E0
```

It performs generic script-state reset and then calls opcode-specific reinitialization handlers.

## 11.2 Generic reset work

Observed reset behavior includes restoring fields approximately equivalent to:

```text
runtime_state        = active
current_group_index  = 0
repeat_index         = 0
elapsed_time         = 0.0
```

and resetting execution-state bits in the `+0x1E` area.

It then walks function groups and linked/sync chains.

## 11.3 Why opcode-specific reset exists

Many functions maintain state in:

- their `+0x14` progress field;
- parameter storage;
- associated runtime resources;
- animation/path/sound/sprite objects.

Generic zeroing is therefore not sufficient.

Runtime dispatches to named functions such as:

```text
Script_Reinit_InterpolateCameras
Script_Reinit_SelectBodyAnimation
Script_Reinit_MoveObjectOnPath
Script_Reinit_MorphObject
Script_Reinit_MorphPaletteSprite
Script_Reinit_PlaySound
Script_Reinit_Wait
```

## 11.4 Reinit does not imply normal execution reachability

A notable case is family `0x04`.

Several IDs are recognized by the reinit dispatcher and have concrete action handlers, yet the main `Script_PlayScript` dispatch tree does not call those action handlers.

That could indicate:

- a second execution path not yet found;
- one-shot setup functions invoked indirectly;
- dead/legacy code;
- authoring-tool compatibility;
- functions instantiated and invoked by another subsystem;
- a dispatch mechanism not obvious from static xrefs.

The document therefore separates:

```text
main per-frame dispatched
```

from:

```text
implementation exists
```

rather than assuming they are equivalent.

---

# 12. Canonical opcode/function table

The earlier saved table was useful but partial.

The table below incorporates the current Runtime handler names and separates main-dispatch participation from merely having an implementation.

## 12.1 IDs dispatched by `Script_PlayScript`

| Function ID | Current semantic name | Main per-frame handler | Reinit handler | Return blocks/keeps group active? | Confidence / notes |
|---:|---|---:|---:|:---:|---|
| `0x01000001` | camera operation, exact name unknown | `0x004A4580` | — | No | Resolves a camera in the current scene. A diagnostic says `Script_PlayScript(): Can't find camera "%s" in current scene.`, but that string may be copy/paste and does not prove the operation's original name. |
| `0x01000002` | `InterpolateCameras` | `0x004A4630` | `0x004A4450` | Yes | Runtime diagnostics confirm `Script_InterpolateCameras` and `Script_Reinit_InterpolateCameras`. |
| `0x02000004` | `SelectBodyAnimation` | `0x004A35D0` | `0x004A3290` | Yes | Runtime diagnostics confirm both names. |
| `0x0200002A` | `SelectRelativeBodyAnimation` | `0x004A3AD0` | `0x004A3430` | Yes | Runtime diagnostics confirm both names. |
| `0x03000008` | `MoveObjectOnPath` | `0x0046F400` | `0x0046EEB0` | Yes | Runtime diagnostics confirm action and reinit. |
| `0x0300001A` | `AnimationFromExternalScene` | `0x00470060` | `0x0046F0C0` | Yes | Runtime diagnostics confirm action and reinit. |
| `0x03000021` | `MorphObject` | `0x00470440` | `0x0046F200` | Yes | Runtime diagnostics confirm action and reinit. |
| `0x03000023` | `ScaleObjectX` | `0x004707A0` | `0x0046EB50` | Yes | Runtime diagnostics confirm action and reinit. |
| `0x03000024` | `ScaleObjectY` | `0x004709E0` | `0x0046EC70` | Yes | Runtime diagnostics confirm action and reinit. |
| `0x03000025` | `ScaleObjectZ` | `0x00470C20` | `0x0046ED90` | Yes | Runtime diagnostics confirm action and reinit. |
| `0x0300002B` | `SwapObject` | `0x00470E60` | `0x0046E9C0` | No | Runtime diagnostics confirm action and reinit. Side effect does not itself block group advancement. |
| `0x04000011` | `ChainObjects` | `0x004A1E80` | `0x004A19C0` | Yes | Runtime diagnostic confirms `Script_ChainObjects`; reinit is paired by dispatcher. |
| `0x04000020` | `MorphPaletteSprite` | `0x004A2D10` | `0x004A1C90` | Yes | Runtime diagnostics confirm action/reinit. |
| `0x05000014` | `PlaySound` | `0x004A12D0` | `0x004A0FB0` | No | Runtime diagnostics confirm action/reinit. |
| `0x05000015` | `PlaySyncSound` | `0x004A14D0` | `0x004A1100` | Yes | Runtime diagnostics confirm action/reinit. |
| `0x05000016` | `StopSound` | `0x004A16D0` | `0x004A11F0` | No | Main-dispatch pairing is confirmed. Some diagnostics in the reset function appear copy/pasted from `PlaySyncSound`, so the exact original reinit symbol name is uncertain. |
| `0x06000017` | `Wait` | `0x004A0EE0` | `0x004A0E50` | Yes | Semantics strongly confirmed. It is a useful reference for the group-active return convention. |
| `0x06000027` | `SendMessage` | `0x004A0E80` | — | No | Runtime diagnostic confirms `Script_SendMessage`. Exact message destination/recipient semantics remain partial. |

This is the corrected interpretation of the user's saved “Return contributes to group” column:

```text
Yes:
    handler return is ORed into group_still_active

No:
    handler runs, but its return does not affect
    whether Script_PlayScript remains on this group
```

## 12.2 Recognized/implemented IDs not dispatched by the main per-frame switch

The following IDs are real enough to be recognized by parameter mapping and/or have concrete Runtime implementations, but they are not called by the currently identified main `Script_PlayScript` dispatch tree.

| Function ID | Current semantic name | Action implementation | Reinit handler | Current status |
|---:|---|---:|---:|---|
| `0x02000026` | unknown | none identified | none identified | Recognized by `Script_GetNumParam`. Exact purpose and execution path unresolved. |
| `0x0400000C` | `SetSpriteType` | `0x004A2520` | — | Runtime diagnostic confirms name. Not main-dispatched. |
| `0x0400000D` | `Display3DSpriteOnPath` | `0x004A2150` | `0x004A1B20` | Action and reinit diagnostics/dispatch identified. Not main-dispatched. |
| `0x0400001B` | `ScaleSpriteOnX` | `0x004A25E0` | `0x004A1810` | Action/reinit identified. Not main-dispatched. |
| `0x0400001C` | `ScaleSpriteOnY` | `0x004A2790` | `0x004A18A0` | Action/reinit identified. Not main-dispatched. |
| `0x0400001D` | `SetSpriteRolling` | `0x004A2940` | `0x004A1930` | Action/reinit identified. Not main-dispatched. |
| `0x0400001E` | `SetSpritePalette` | `0x004A2AF0` | — | Runtime diagnostic confirms name. Not main-dispatched. |
| `0x0400001F` | `SetSpriteDefaultPalette` | `0x004A2C60` | — | Runtime diagnostic confirms name. Not main-dispatched. |
| `0x04000028` | `Display3DSprite` | `0x004A2FB0` | `0x004A1DE0` | Action/reinit identified. Not main-dispatched. |
| `0x04000029` | `SetSpriteFrame` | `0x004A31B0` | — | Runtime diagnostic confirms name. Not main-dispatched. |

Static analysis has not yet found a normal direct call path to several of these action implementations.

Their existence should therefore be documented as:

```text
implemented by Runtime
```

not automatically as:

```text
known to execute in normal retail script playback
```

## 12.3 Additional special-case ID: `0x05000013`

`0x05000013` appears in script-instance/progress initialization comparisons.

However, at the current stage:

- it is not recognized by the main `Script_GetNumParam` function-ID tree;
- no main per-frame handler has been identified;
- no definitive semantic name has been recovered.

It should be treated as a **special/reserved/legacy or alternate-path function ID** until additional call-flow or asset evidence is found.

---

# 13. Family `0x01`: camera operations

## `0x01000001` — camera operation, exact name unknown

Handler:

```text
0x004A4580
```

Main-dispatched:

```text
yes
```

Blocks group:

```text
no
```

The handler resolves a camera name/reference in the current scene.

A failure diagnostic reads:

```text
Script_PlayScript(): Can't find camera "%s" in current scene.
```

This demonstrates camera semantics but does **not** prove the original operation name is `PlayScript`.

The diagnostic may have been copied from another function or may refer to the containing script context.

Until more behavior is traced, a safe project label is:

```text
CameraOperation01000001
```

or:

```text
Select/ActivateCamera (?) 
```

with the uncertainty retained.

## `0x01000002` — `InterpolateCameras`

Handler:

```text
0x004A4630
```

Reinit:

```text
0x004A4450
```

Main-dispatched:

```text
yes
```

Blocks group:

```text
yes
```

Runtime diagnostics explicitly name:

```text
Script_InterpolateCameras
Script_Reinit_InterpolateCameras
```

The operation is stateful and therefore naturally participates in the group-active accumulator while interpolation remains incomplete.

Open questions:

- all interpolation-mode parameter meanings;
- easing behavior;
- camera-space conventions;
- relationship to camera records in `.3DO`/scene resources;
- behavior when source/target camera is missing.

---

# 14. Family `0x02`: body animation

## `0x02000004` — `SelectBodyAnimation`

Handler:

```text
0x004A35D0
```

Reinit:

```text
0x004A3290
```

Blocks group:

```text
yes
```

Runtime names:

```text
Script_SelectBodyAnimation
Script_Reinit_SelectBodyAnimation
```

The operation selects/plays animation data for a body/character and remains active until its completion condition is met.

## `0x0200002A` — `SelectRelativeBodyAnimation`

Handler:

```text
0x004A3AD0
```

Reinit:

```text
0x004A3430
```

Blocks group:

```text
yes
```

Runtime names:

```text
Script_SelectRelativeBodyAnimation
Script_Reinit_SelectRelativeBodyAnimation
```

The exact meaning of **relative** needs further behavioral analysis.

Possible dimensions include:

- transform relative to current body pose;
- animation relative to another object;
- relative animation addressing;
- relative root motion.

No one of those should be promoted to fact yet.

## `0x02000026` — unknown

Recognized by:

```text
Script_GetNumParam
```

but no normal execution/reinit handler has yet been identified.

This is a useful warning that:

> the main execution table is not necessarily identical to the complete historical function-ID enum.

---

# 15. Family `0x03`: object/scene animation

This family is particularly well named by Runtime diagnostics.

## `0x03000008` — `MoveObjectOnPath`

Handler:

```text
0x0046F400
```

Reinit:

```text
0x0046EEB0
```

Blocks group:

```text
yes
```

Runtime names:

```text
Script_MoveObjectOnPath
Script_Reinit_MoveObjectOnPath
```

This operation strongly correlates with the `.3DP` path resources loaded from SCX tag `0xDEAD0000`.

## `0x0300001A` — `AnimationFromExternalScene`

Handler:

```text
0x00470060
```

Reinit:

```text
0x0046F0C0
```

Blocks group:

```text
yes
```

Runtime names:

```text
Script_AnimationFromExternalScene
Script_Reinit_AnimationFromExternalScene
```

The exact meaning of “external scene” in the original asset architecture needs additional tracing.

## `0x03000021` — `MorphObject`

Handler:

```text
0x00470440
```

Reinit:

```text
0x0046F200
```

Blocks group:

```text
yes
```

Runtime names:

```text
Script_MorphObject
Script_Reinit_MorphObject
```

The morph data source and its relationship to model flags such as face morphing remain separate reverse-engineering questions.

## `0x03000023` — `ScaleObjectX`

Handler:

```text
0x004707A0
```

Reinit:

```text
0x0046EB50
```

Blocks group:

```text
yes
```

## `0x03000024` — `ScaleObjectY`

Handler:

```text
0x004709E0
```

Reinit:

```text
0x0046EC70
```

Blocks group:

```text
yes
```

## `0x03000025` — `ScaleObjectZ`

Handler:

```text
0x00470C20
```

Reinit:

```text
0x0046ED90
```

Blocks group:

```text
yes
```

The three scale operations are independently encoded rather than one generic vector-scale function ID.

Questions still open:

- interpolation law;
- units;
- whether scale is absolute or relative;
- how negative scale behaves;
- interaction with child transforms;
- whether all three may be synchronized in one group.

## `0x0300002B` — `SwapObject`

Handler:

```text
0x00470E60
```

Reinit:

```text
0x0046E9C0
```

Blocks group:

```text
no
```

Runtime names:

```text
Script_SwapObject
Script_Reinit_SwapObject
```

This is a side-effect-style operation from the group's perspective.

The exact object/resource swap semantics should be documented after tracing its target fields and renderer/resource consequences.

---

# 16. Family `0x04`: sprite and visual operations

This family has the largest gap between:

```text
functions Runtime implements
```

and:

```text
functions the main Script_PlayScript switch dispatches
```

That difference should remain visible in OpenNomad documentation.

## `0x0400000C` — `SetSpriteType`

Implementation:

```text
0x004A2520
```

Runtime name:

```text
Script_SetSpriteType
```

No opcode-specific reinit handler has been identified.

The function is recognized by parameter mapping but not the main per-frame dispatch tree.

## `0x0400000D` — `Display3DSpriteOnPath`

Implementation:

```text
0x004A2150
```

Reinit:

```text
0x004A1B20
```

Runtime names include:

```text
Script_Display3DSpriteOnPath
Script_Reinit_Display3DSpriteOnPath
```

This likely ties sprite resources to `.3DP` path resources, but exact transform/timing semantics remain to be reconstructed.

## `0x04000011` — `ChainObjects`

Handler:

```text
0x004A1E80
```

Reinit:

```text
0x004A19C0
```

Main-dispatched:

```text
yes
```

Blocks group:

```text
yes
```

Runtime name:

```text
Script_ChainObjects
```

The exact meaning of “chain” — transform parenting, animation chaining, object linkage, or another runtime relation — should be established from field writes before choosing a more specific model.

## `0x0400001B` — `ScaleSpriteOnX`

Implementation:

```text
0x004A25E0
```

Reinit:

```text
0x004A1810
```

Runtime names include:

```text
Script_ScaleSpriteOnX
Script_Reinit_ScaleSpriteOnX
```

## `0x0400001C` — `ScaleSpriteOnY`

Implementation:

```text
0x004A2790
```

Reinit:

```text
0x004A18A0
```

Runtime names include:

```text
Script_ScaleSpriteOnY
Script_Reinit_ScaleSpriteOnY
```

## `0x0400001D` — `SetSpriteRolling`

Implementation:

```text
0x004A2940
```

Reinit:

```text
0x004A1930
```

Runtime names include:

```text
Script_SetSpriteRolling
Script_Reinit_SetSpriteRolling
```

“Rolling” should be left as Runtime terminology until the exact axis/angle convention is established.

## `0x0400001E` — `SetSpritePalette`

Implementation:

```text
0x004A2AF0
```

Runtime name:

```text
Script_SetSpritePalette
```

No opcode-specific reset has yet been identified.

This is particularly relevant to Omikron's indexed/paletted sprite rendering.

## `0x0400001F` — `SetSpriteDefaultPalette`

Implementation:

```text
0x004A2C60
```

Runtime name:

```text
Script_SetSpriteDefaultPalette
```

No opcode-specific reset identified.

## `0x04000020` — `MorphPaletteSprite`

Handler:

```text
0x004A2D10
```

Reinit:

```text
0x004A1C90
```

Main-dispatched:

```text
yes
```

Blocks group:

```text
yes
```

Runtime names:

```text
Script_MorphPaletteSprite
Script_Reinit_MorphPaletteSprite
```

This is a stateful palette transition and therefore fits the group's blocking model.

Its exact interpolation space — raw palette bytes, converted display colors, gamma-adjusted values, or another representation — still needs renderer-side correlation.

## `0x04000028` — `Display3DSprite`

Implementation:

```text
0x004A2FB0
```

Reinit:

```text
0x004A1DE0
```

Runtime names include:

```text
Script_Display3DSprite
Script_Reinit_Display3DSprite
```

## `0x04000029` — `SetSpriteFrame`

Implementation:

```text
0x004A31B0
```

Runtime name:

```text
Script_SetSpriteFrame
```

This function has already been relevant to sprite-system reverse engineering.

It ultimately manipulates a sprite instance's current frame/asset entry rather than merely replacing UVs at draw time.

The exact call path from SCX Script execution to this implementation is still unresolved because it does not appear in the main `Script_PlayScript` switch currently identified.

### Important implementation warning

Do not infer:

```text
not in main dispatcher == unused
```

and do not infer:

```text
implementation exists == normal per-frame opcode
```

Both claims go beyond current evidence.

---

# 17. Family `0x05`: sound

## `0x05000014` — `PlaySound`

Handler:

```text
0x004A12D0
```

Reinit:

```text
0x004A0FB0
```

Blocks group:

```text
no
```

Runtime names:

```text
Script_PlaySound
Script_Reinit_PlaySound
```

A normal sound-start operation does not itself hold the current Script group open.

## `0x05000015` — `PlaySyncSound`

Handler:

```text
0x004A14D0
```

Reinit:

```text
0x004A1100
```

Blocks group:

```text
yes
```

Runtime names:

```text
Script_PlaySyncSound
Script_Reinit_PlaySyncSound
```

The contrast with `PlaySound` is architecturally informative:

```text
PlaySound:
    fire/start side effect
    does not block group

PlaySyncSound:
    remains active
    blocks group until synchronization condition is done
```

## `0x05000016` — `StopSound`

Handler:

```text
0x004A16D0
```

Reinit-dispatch target:

```text
0x004A11F0
```

Blocks group:

```text
no
```

Runtime name:

```text
Script_StopSound
```

Some diagnostic labels in the reset path appear to have been copied from neighboring sound functions.

The dispatcher pairing is reliable; the exact original source-level name of `0x004A11F0` is less certain.

## `0x05000013` — unresolved special case

This ID appears in script-instance/progress initialization logic.

It currently has no confirmed normal handler/name.

Potential explanations include:

- removed/legacy sound operation;
- internal timing marker;
- alternate Script runner;
- function ID retained for asset compatibility.

More asset coverage is needed.

---

# 18. Family `0x06`: control and messaging

## `0x06000017` — `Wait`

Handler:

```text
0x004A0EE0
```

Reinit:

```text
0x004A0E50
```

Blocks group:

```text
yes
```

Runtime diagnostics support:

```text
Script_Wait
Script_Reinit_Wait
```

This is the clearest opcode for understanding Script scheduler semantics.

The handler reads timing-related parameters, advances mutable progress using frame time/state, and reports a nonzero active state while waiting.

Therefore:

```text
nonzero contributing return
```

means:

```text
do not advance the Script group yet
```

not:

```text
operation succeeded
```

## `0x06000027` — `SendMessage`

Handler:

```text
0x004A0E80
```

Blocks group:

```text
no
```

Runtime name:

```text
Script_SendMessage
```

The operation validates message-related data and uses global message/callback state around:

```text
0x0067A0B4
0x0067A0B8
```

along with its parameters.

Still unknown:

- message namespace;
- target object/system;
- whether messages are queued or immediately dispatched;
- relation to scenario/event VM messages;
- whether IDs correspond to script IDs, object IDs, event IDs, or another registry.

---

# 19. Script resources referenced by functions

The tagged SCX resource sections are closely related to Script functions.

## 19.1 Tag `0xDEAD0000`: path resources

Current record stride:

```text
0x20 bytes
```

`Grid.SCX` contains a path resource named:

```text
Grid_pb.3dp
```

This strongly aligns with functions such as:

```text
Script_MoveObjectOnPath
Script_Display3DSpriteOnPath
```

## 19.2 Tag `0xDEAD0001`: animation resources

Current record stride:

```text
0x24 bytes
```

`Grid.SCX` contains names such as:

```text
INTRO1.3DA
INTRO2.3DA
INTRO3.3DA
```

The loader resolves these through an animation-related function around:

```text
0x0046E880
```

and stores a runtime pointer in the record.

This aligns with body/external-scene animation functions.

## 19.3 Tag `0xDEAD0003`: sound resources

Current record stride:

```text
0x1A bytes
```

`Grid.SCX` contains names such as:

```text
INTRO01.WAV
```

Runtime resolves a sound identifier through a function around:

```text
0x0049FC80
```

and stores:

```text
0xFFFF
```

when lookup fails.

This resource table clearly supports family `0x05`.

## 19.4 Tag `0xDEAD0004`: `.3DO` / sprite visual resources

Current serialized record stride:

```text
0x24 bytes
```

Observed `Grid.SCX` entries include effect `.3DO` names such as:

```text
EFFECTS2_SMOKE1.3DO
```

The loader:

- reads associated outer-SCX metadata;
- loads embedded/associated 3DO data through the model loader;
- stores a runtime model/resource pointer;
- conditionally allocates a sprite object through the sprite-instance path.

This table clearly supports the family-`0x04` sprite functions.

## 19.5 Tag `0xDEAD0005`: scenes

Current record stride:

```text
0x1C bytes
```

Runtime diagnostics include:

```text
Scene "%s" not found !
```

Both currently examined startup SCX files have zero records in this section, so more assets are needed to characterize it.

Functions such as:

```text
AnimationFromExternalScene
```

make this section particularly interesting.

---

# 20. Script templates vs mutable instances

Runtime diagnostics prove that loaded scripts are not always executed by mutating one shared template object.

The executable contains a substantial:

```text
Script_MakeInstance()
```

path.

Diagnostics include:

```text
Script_MakeInstance(): Your ScriptListPtr is NULL.
Script_MakeInstance(): Your ScriptPtr is NULL.
Script_MakeInstance(): Your ScriptPtr isn't a valid script. It's not from your ScriptListPtr..Bad adress.
Script_MakeInstance(): There are %d instances in script. Max number is %d...Can't add instance.
Script_MakeInstance(): Not enough memory to allocate space for script functions.
Script_MakeInstance(): Not enough memory to allocate space for parameter list.
Script_MakeInstance(): Index of SyncFunction too big : %d on %d.
Script_MakeInstance(): Not enough memory to allocate space for functions parameters.
Script_MakeInstance(): Not enough memory to allocate space for syncfunctions parameters.
Script_MakeInstance(): Not enough memory to allocate space for new sprite.
Script_MakeInstance(): Sprite "%s" isn't loaded.
Script_MakeInstance(): Sprite "%s" can't be allocated.
```

## 20.1 What an instance needs to clone

Current behavior demonstrates copying/allocation for:

- mutable Script structure;
- primary function records;
- sync/linked function records;
- parameter lists;
- function parameters;
- sync-function parameters;
- opcode-specific runtime objects such as sprites.

## 20.2 Sync-function indexes are rebuilt

`Script_MakeInstance` validates serialized/template sync-function relationships and rewrites them so the instance's function graph points into the instance's own cloned arrays.

This is important:

```text
template function A -> template sync function B
```

must become:

```text
instance function A -> instance sync function B
```

not remain pointed at shared template state.

## 20.3 Parameters can be mutable

Because parameters are copied into instance-specific storage, OpenNomad should assume that at least some handlers mutate parameter values or rely on per-instance parameter state.

A read-only shared parameter span is therefore insufficient for full fidelity.

## 20.4 Per-instance sprite allocation

The instance path contains explicit error handling for allocating a new sprite.

This is strong evidence that certain sprite-oriented functions carry instance-local sprite state rather than sharing one SCX-global sprite object.

## 20.5 Related cleanup

Runtime also contains diagnostics/functions named:

```text
Script_RemoveInstance()
Script_RemoveAllInstances()
```

The full ownership/reference-counting model is still being reconstructed.

---

# 21. Reconstructing execution groups

The term **group** is used in this document as a working description of what Runtime does.

The original source/editor terminology may have been different.

## 21.1 Why “group” is useful

A primary `0x18` record is selected by:

```text
current_group_index
```

and additional records are reached by following:

```text
sync_or_next
```

The selected records are evaluated together for the purpose of deciding whether to advance.

That behaves like a synchronized function group.

## 21.2 Example conceptual scheduling

Suppose a group contains:

```text
MoveObjectOnPath
PlaySound
Wait
```

and:

```text
MoveObjectOnPath  contributes
PlaySound         does not contribute
Wait              contributes
```

Then the group remains active while either:

```text
MoveObjectOnPath returns nonzero
OR
Wait returns nonzero
```

The sound can be started without itself preventing progress.

Conceptually:

```c
active = false;

active |= MoveObjectOnPath(...);
PlaySound(...);
active |= Wait(...);

if (!active)
    advance_group();
```

This explains why the “return contributes” distinction exists at all.

## 21.3 Sync does not necessarily mean identical duration

Functions in one linked group may have independent:

- execution limits;
- progress counters;
- parameters;
- completion conditions.

They are synchronized only in the sense that the group does not advance until no contributing member remains active.

---

# 22. Main-dispatch table vs full function enum

A key reverse-engineering lesson is that there are several different notions of “supported opcode”:

1. **recognized by `Script_GetNumParam`;**
2. **has an action implementation in Runtime;**
3. **has a reinit implementation;**
4. **is dispatched by `Script_PlayScript`;**
5. **appears in currently examined retail SCX data;**
6. **is actually reached during retail gameplay.**

These sets are not currently identical.

For example:

```text
0x04000029 SetSpriteFrame
```

has:

- a concrete implementation;
- a Runtime diagnostic name;
- parameter recognition;

but is not in the currently identified main `Script_PlayScript` switch.

Conversely, the current asset sample is too small to prove that every main-dispatch function occurs in `aventure.SCX` or `Grid.SCX`.

Documentation and implementation should therefore preserve the evidence level for each operation.

---

# 23. Error behavior and validation

The Script subsystem performs substantial validation rather than blindly trusting SCX indexes.

## 23.1 Invalid parameter indexes

Runtime emits errors for parameter indexes that exceed the available value count.

## 23.2 Invalid sync-function indexes

Runtime validates the serialized sync index against the owning script's sync-function count.

## 23.3 Invalid pointer-to-index conversion

During inverse conversion/serialization-style code, Runtime validates that parameter and sync-function pointers actually fall inside the corresponding arrays before converting them to indexes.

## 23.4 Invalid script ownership

`Script_MakeInstance` verifies that a script pointer belongs to the supplied ScriptList.

## 23.5 Instance limit

Each script has a maximum number of instances.

Runtime emits an explicit error when the current number reaches that limit.

The field holding that maximum/current accounting has not yet been cleanly mapped in the `0x64` template structure.

## 23.6 Missing resources

Individual handlers report missing:

- cameras;
- scenes;
- sprites;
- sounds;
- other named assets.

A faithful reimplementation should distinguish:

```text
malformed serialized script
```

from:

```text
well-formed script referring to missing runtime resource
```

because Runtime handles these at different layers.

---

# 24. SCX script loader implications

## 24.1 Do not execute merely because an SCX was loaded

Loading:

```text
Grid.SCX
```

or:

```text
aventure.SCX
```

does not mean:

```text
run every script in that file
```

SCX loading establishes:

- script templates;
- resources;
- registries;
- runtime-ready pointer relationships.

Actual script activation is a separate operation.

This is especially important during startup, where `GRID.SCX` is loaded as an area dependency but startup control is orchestrated by the separate IAM scenario/event VM.

## 24.2 Templates are data, not a startup queue

A script named:

```text
1KaylArrives
```

being present in `Grid.SCX` does not prove that it should run during main-menu initialization.

Activation must be driven by the appropriate scenario/game transition.

## 24.3 Do not hardcode by script name

Names are useful debugging labels and lookup keys, but Runtime's architecture includes numeric IDs, resource relationships, instances, and scheduler logic.

OpenNomad should not replace this with:

```cpp
if (script.name == "1KaylArrives") {
    playIntro();
}
```

except as a temporary diagnostic experiment.

---

# 25. Scenario/event VM

The compact scenario/event VM is less completely documented than the SCX Script system, but enough is known to establish that it is separate.

## 25.1 Encoding style

The VM uses a byte-oriented instruction stream.

An instruction begins with:

```text
u8 opcode
```

followed by an opcode-specific operand sequence.

Operands can include signed 16-bit values and a variable/reference encoding interpreted by the VM.

It is **not** encoded as `0xGG0000NN` function records.

## 25.2 Startup example: opcode `0x46`

In `IAM/AREA` record `118`, the startup script begins at:

```text
areaRecord + 0x3FC
```

At:

```text
script + 0x26
```

or:

```text
areaRecord + 0x422
```

the bytes are:

```text
46 1D 00 FF FF 13 00
```

Current interpretation:

```text
opcode = 0x46

operand 0:
    1D 00
    signed 16-bit = 29

operand 1:
    FF FF
    signed 16-bit = -1

operand 2:
    13 00
    signed 16-bit = 19
```

## 25.3 Opcode `0x46` handler

Handler:

```text
0x00403860
```

It reads three signed 16-bit operand values with support for the VM's variable/reference mechanism.

For the startup instruction the literal values are:

```text
29, -1, 19
```

It then participates in the interface-opening path:

```text
0x00403860
    ->
0x0041DEF0
    ->
0x0041DF30
    ->
0x00429BB0
```

leading to interface:

```text
29 / 0x1D
```

and ultimately the main-menu initializer:

```text
0x00479D10
```

## 25.4 Why this proves a second VM

The scenario/event opcode:

```text
0x46
```

has:

- a bytecode instruction pointer;
- immediate operands;
- event/script context state;
- a handler unrelated to the `0x0044C860` SCX Script dispatcher.

The SCX operation:

```text
0x04000029
```

has:

- a fixed `0x18` function record;
- a shared parameter pool;
- sync-function linkage;
- per-function progress;
- the SCX Script function-ID dispatcher.

They are structurally distinct.

---

# 26. Probable relationship between the two scripting layers

The current evidence supports a layered architecture:

```text
Scenario/event VM
    |
    +-- high-level game logic
    +-- area events
    +-- interface changes
    +-- scenario transitions
    +-- invokes/coordinates engine systems
    |
    v
SCX Script system
    |
    +-- cinematic/action sequences
    +-- camera interpolation
    +-- body animation
    +-- object/path animation
    +-- sprite effects
    +-- sound synchronization
    +-- timed waits/messages
```

This should be treated as a **working architectural interpretation**, not a recovered original design document.

The exact bridge functions through which the scenario VM creates/starts/stops SCX Script instances are still being mapped.

A plausible runtime flow is:

```text
area/event instruction
    ->
look up script by ID/name
    ->
make instance
    ->
activate instance
    ->
normal frame processing calls Script_PlayScript
```

but every step of that chain has not yet been assigned final function names.

---

# 27. Script-related section parser details

For completeness, the currently mapped SCX section handlers are:

| Section | Parser branch / area | Runtime result relevant to scripts |
|---|---|---|
| tag 0 | around `0x00449AA0` | path-resource records |
| tag 1 | around `0x00449B1F` | animation-resource records |
| tag 2 | around `0x00449881` | Script templates, parameter values, function arrays |
| tag 3 | around `0x00449BA0` | sound resources |
| tag 4 | around `0x00449C15` | 3DO/sprite visual resources |
| tag 5 | around `0x00449D00` | scene resources |
| tag 6 | around `0x00449D7D` | unknown |
| tag 7 | around `0x00449DAC` | unknown |
| tag 8 | around `0x00449DD8` | unknown fixed-limit/global setup |
| tag 9 | around `0x00449E3C` | no-op/reserved in current parser |
| tag 10 | around `0x00449DE4` | auxiliary/external block |

These addresses identify branches within the SCX main-block parser rather than necessarily clean standalone source functions.

---

# 28. Observed tag order in current SCX files

## `Grid.SCX`

Known tag offsets:

```text
0x0010  0xDEAD0002  scripts
0x07DB  0xDEAD0000  paths
0x0803  0xDEAD0001  animations
0x0877  0xDEAD0003  sounds
0x0935  0xDEAD0004  sprites/3DO visuals
0x100D  0xDEAD0005  scenes, count 0
0x1015  0xDEAD0006  unknown, count 0
0x101D  0xDEAD0007  unknown, count 0
0x1025  0xDEADFFFF
```

The main block ends at:

```text
0x1029
```

matching:

```text
0x10 + 0x1019
```

## `aventure.SCX`

Known tag offsets:

```text
0x0010  0xDEAD0002  scripts
0x1E14  0xDEAD0004  sprites/3DO visuals
0x41BC  0xDEAD0005  scenes, count 0
0x41C4  0xDEAD0006  unknown, count 0
0x41CC  0xDEAD0007  unknown, count 0
0x41D4  0xDEADFFFF
```

The main block ends at:

```text
0x41D8
```

matching:

```text
0x10 + 0x41C8
```

This demonstrates that sections are optional and are not required to occur in numerical tag order.

A parser must dispatch by tag rather than assuming a fixed physical section sequence.

---

# 29. Suggested serialized structures

These are documentation-oriented structures, not final OpenNomad ABI definitions.

```c
struct ScxHeaderV5 {
    uint32_t magic;          // 0x00DEAD00
    uint32_t version;        // 5
    uint32_t unknown_08;     // observed 8
    uint32_t mainBlockSize;
};
```

Script function:

```c
struct SerializedScriptFunctionV5 {
    uint32_t functionId;
    uint32_t unknown04;
    uint32_t parameterIndex;
    uint32_t syncFunctionIndex; // 0xFFFFFFFF = none
    int32_t  executionLimit;
    int32_t  progress;
}; // 0x18
```

Runtime form:

```c
struct ScriptFunction {
    uint32_t functionId;
    uint32_t unknown04;

    std::span<uint32_t> parameters;
    ScriptFunction *syncOrNext;

    int32_t executionLimit;
    int32_t progress;
};
```

The modern form above is conceptual; ownership should likely be expressed with indexes/spans rather than native self-referential pointers wherever possible.

---

# 30. Recommended OpenNomad runtime model

A robust modern implementation can preserve Runtime semantics without reproducing its pointer-heavy memory representation.

For example:

```cpp
struct ScriptFunction {
    uint32_t id;
    uint32_t unknown04;

    uint32_t parameterStart;
    std::optional<uint32_t> syncFunctionIndex;

    int32_t executionLimit;
    int32_t progress;
};

struct ScriptTemplate {
    std::string name;
    uint16_t id;

    std::vector<ScriptFunction> groups;
    std::vector<ScriptFunction> syncFunctions;
    std::vector<uint32_t> parameters;

    int32_t repeatLimit;
};

struct ScriptInstance {
    const ScriptTemplate* source;

    std::vector<ScriptFunction> groups;
    std::vector<ScriptFunction> syncFunctions;
    std::vector<uint32_t> parameters;

    uint32_t currentGroup;
    uint32_t repeatIndex;
    float elapsedTime;

    // runtime resource/state objects
};
```

The exact API is not prescribed here.

The important invariants are:

- disk indexes remain indexes during parsing;
- validation occurs before relocation/use;
- instances get mutable state;
- template data is not accidentally mutated globally;
- sync links refer to the instance's own records;
- operation parameters retain raw 32-bit representation until interpreted by the operation;
- script and scenario/event VMs remain separate.

---

# 31. Handler dispatch strategy for OpenNomad

A useful explicit table can model known main-dispatch operations:

```cpp
struct ScriptOpcodeInfo {
    uint32_t id;
    ScriptHandler handler;
    ScriptReinitHandler reinit;
    bool returnKeepsGroupActive;
};
```

For example:

```text
0x01000002:
    handler = InterpolateCameras
    reinit  = ReinitInterpolateCameras
    returnKeepsGroupActive = true

0x05000014:
    handler = PlaySound
    reinit  = ReinitPlaySound
    returnKeepsGroupActive = false

0x06000017:
    handler = Wait
    reinit  = ReinitWait
    returnKeepsGroupActive = true
```

Do not encode the final boolean as:

```text
returnsSuccess
```

because that is semantically wrong.

A better name is:

```text
returnKeepsGroupActive
```

or:

```text
contributesToGroupActive
```

---

# 32. Functions that deserve separate execution-status metadata

For each Script function ID, OpenNomad documentation should eventually track at least:

```text
ID
Runtime name
family
parameter schema
resource dependencies
main-dispatch status
action handler
reinit handler
return-contribution behavior
progress-field meaning
execution-limit meaning
instance-local allocations
completion condition
failure behavior
retail assets containing it
known Runtime call sites
```

The current table only solves a subset of those dimensions.

---

# 33. Important Runtime functions

| Address | Current name / role | Confidence |
|---:|---|---|
| `0x0044C860` | `Script_PlayScript` | Confirmed by diagnostic |
| `0x0044C090` | `Script_GetNumParam` | Confirmed by diagnostic |
| `0x0044C680` | raw 32-bit parameter accessor | Strongly corroborated |
| `0x0044A7E0` | generic Script reinit dispatcher | Confirmed behavior |
| `0x0044A0F0` | script lookup by 16-bit ID | Confirmed behavior |
| `0x00449750` | SCX loader | Confirmed |
| `0x00449xxx` | SCX tagged-section parsers | Confirmed |
| `0x0044B280` | paired/linked-script management | Behavior confirmed, exact name unknown |
| `0x0044B2C0` | paired/linked-script management | Behavior confirmed, exact name unknown |
| `0x0044B300` | paired/linked-script management | Behavior confirmed, exact name unknown |
| `0x0049EB70` | Script/error-state query used by Script playback | Semantic partial |
| `0x00403860` | scenario/event VM opcode `0x46` handler | Confirmed |
| `0x0041DF30` | scenario-script-facing interface-open path | Confirmed |
| `0x00429BB0` | generic interface open | Confirmed |

Opcode-specific handlers are listed in the canonical tables above.

---

# 34. Runtime diagnostic names recovered so far

The executable contains strings supporting at least these source-like names:

```text
Script_PlayScript
Script_GetNumParam
Script_MakeInstance
Script_RemoveInstance
Script_RemoveAllInstances

Script_InterpolateCameras
Script_Reinit_InterpolateCameras

Script_SelectBodyAnimation
Script_Reinit_SelectBodyAnimation
Script_SelectRelativeBodyAnimation
Script_Reinit_SelectRelativeBodyAnimation

Script_MoveObjectOnPath
Script_Reinit_MoveObjectOnPath
Script_AnimationFromExternalScene
Script_Reinit_AnimationFromExternalScene
Script_MorphObject
Script_Reinit_MorphObject
Script_ScaleObjectX
Script_Reinit_ScaleObjectX
Script_ScaleObjectY
Script_Reinit_ScaleObjectY
Script_ScaleObjectZ
Script_Reinit_ScaleObjectZ
Script_SwapObject
Script_Reinit_SwapObject

Script_ChainObjects
Script_Display3DSpriteOnPath
Script_Reinit_Display3DSpriteOnPath
Script_SetSpriteType
Script_ScaleSpriteOnX
Script_Reinit_ScaleSpriteOnX
Script_ScaleSpriteOnY
Script_Reinit_ScaleSpriteOnY
Script_SetSpriteRolling
Script_SetSpritePalette
Script_SetSpriteDefaultPalette
Script_MorphPaletteSprite
Script_Reinit_MorphPaletteSprite
Script_Display3DSprite
Script_Reinit_Display3DSprite
Script_SetSpriteFrame

Script_PlaySound
Script_Reinit_PlaySound
Script_PlaySyncSound
Script_Reinit_PlaySyncSound
Script_StopSound

Script_Wait
Script_Reinit_Wait
Script_SendMessage
```

Some error strings are visibly copied between neighboring functions, so a string must be correlated with dispatch/call behavior before being treated as the exact function's original name.

---

# 35. Parameter-schema work still needed

A full opcode-format document ultimately needs a parameter schema for every function.

`Script_GetNumParam` gives us an important route to reconstructing it.

For example, some IDs map several semantic parameter selectors to different slots, showing that operations can have many parameters and that the same semantic parameter kind may occupy different positions depending on the operation.

What remains to do:

1. identify the enum behind the parameter-type argument;
2. give each selector a semantic name;
3. map selector -> slot for every function ID;
4. determine the raw storage type of each slot;
5. determine whether values are:
   - literal;
   - resource indexes;
   - IDs;
   - pointers after relocation;
   - percentages/scales;
   - frame counts;
   - flags;
6. determine which handlers mutate their parameter storage;
7. correlate values against real retail SCX scripts.

Until that work is complete, a parser should preserve parameters as raw 32-bit values and let each handler decode them.

---

# 36. Areas where old assumptions should be avoided

## 36.1 Do not call the 32-bit value a simple bytecode opcode

It is a function ID inside a fixed-size Script function record.

There is no evidence that SCX Script playback advances a bytecode instruction pointer by decoding variable-length `0xGG0000NN` instructions.

## 36.2 Do not call `+0x08` a file pointer

It is serialized as an index and converted into a runtime parameter pointer.

## 36.3 Do not call `+0x0C` a file pointer

It is serialized as a sync-function index and converted into a runtime pointer.

## 36.4 Do not assume a single Script function per group

The `+0x0C` chain allows multiple synchronized functions to participate in one group.

## 36.5 Do not treat handler return as success/failure

For contributing functions it controls whether the current group remains active.

## 36.6 Do not mutate loaded templates as the normal instance model

Runtime has explicit script-instance construction and parameter/function cloning.

## 36.7 Do not execute all scripts when an SCX is loaded

Loading and activation are separate.

## 36.8 Do not merge IAM opcode `0x46` with SCX function family `0x04`

They belong to different interpreters.

## 36.9 Do not assume every implemented Runtime function is in the main dispatch path

Several sprite operations demonstrate otherwise.

## 36.10 Do not assume the current function-ID list is exhaustive

It is exhaustive only for the currently mapped Runtime branches, not necessarily for:

- unused legacy IDs;
- editor-only functions;
- alternate executable versions;
- all retail SCX assets.

---

# 37. Open questions and gaps

The following are the main unresolved questions.

## Priority 1 — exact serialized layout after the tag-2 template table

We know:

- script template count;
- `0x64` template stride;
- shared parameter-value count/pool;
- `0x18` function-record stride;
- primary and sync-function arrays;
- index-to-pointer conversion.

What is not yet documented byte-for-byte is the complete ordering of every per-script function array and auxiliary list after the common template table for arbitrary files.

A precise parser specification should eventually be able to walk tag 2 without using Runtime's own field transformations as an implicit guide.

## Priority 2 — complete `0x64` ScriptTemplate schema

Fields still needing names include substantial portions of:

```text
+0x18
+0x3C .. +0x53
+0x5C .. +0x63
```

Also still needed:

- max-instance field;
- current-instance accounting;
- exact pair/chaining flags;
- context flag meanings.

## Priority 3 — `ScriptFunction +0x04`

No generic semantic has yet been proven.

This should remain unknown rather than be guessed from a few operations.

## Priority 4 — exact generic meaning of `+0x10` and `+0x14`

The pair acts like:

```text
execution/frame limit
current progress/frame
```

but opcode-specific behavior may reinterpret the values.

Need per-opcode tracing before assigning final universal names.

## Priority 5 — parameter-type enum

Recover the semantic enum consumed by:

```text
Script_GetNumParam
```

This is likely the fastest path to a useful per-opcode parameter schema.

## Priority 6 — `0x02000026`

It is recognized by parameter mapping but lacks an identified normal handler.

Need:

- asset search;
- cross-references;
- comparison with neighboring animation operations;
- parameter mapping;
- any diagnostic strings reachable from related code.

## Priority 7 — `0x05000013`

This ID participates in instance/progress logic but is not otherwise well resolved.

Need to determine whether it is:

- a real operation;
- legacy compatibility;
- a marker;
- an alternate sound function;
- an internal-only pseudo-function.

## Priority 8 — family-`0x04` execution path

Runtime implements many sprite functions that are absent from the main per-frame `Script_PlayScript` switch.

This is a major outstanding control-flow question.

Need to determine whether they are invoked through:

- initialization;
- an alternate dispatcher;
- indirect function tables;
- message/event callbacks;
- script instance creation;
- old/dead code.

## Priority 9 — exact family enum names

The family grouping is structurally clear, but original terminology is not.

Current behavior-based labels should remain descriptive only.

## Priority 10 — complete reinit behavior

The dispatch pairing is known for many functions.

Still needed:

- precise state each reinit handler restores;
- whether reinit is guaranteed idempotent;
- resource side effects;
- failure conditions;
- parameter mutation.

## Priority 11 — paired/chained scripts

The runtime link at `+0x54` and related functions are real.

Need to establish:

- authoring meaning;
- ownership;
- start order;
- completion propagation;
- whether pairs run in parallel or sequence;
- how `+0x5D` participates.

## Priority 12 — context bytes `+0x5E..+0x61`

These are used by execution gating involving global context state around `0x00903AE0`.

Their meaning may be important for scene/world ownership and script cancellation.

## Priority 13 — Script instance limit/lists

Runtime checks a per-script maximum/current number of instances.

Need exact fields and list structure.

## Priority 14 — resource-reference parameter semantics

Need to determine how function parameters select records from:

- path list;
- animation list;
- sound list;
- visual/sprite list;
- scene list.

The parameter pool likely contains indexes/IDs rather than names for most execution-time references.

## Priority 15 — unknown SCX tags 6, 7, 8, and 10

These may contain data relevant to scripts or runtime registries.

Their relationship to Script execution remains unknown.

## Priority 16 — complete scenario/event opcode table

Only selected compact VM operations have been investigated in detail.

Need to reconstruct:

- opcode dispatch table;
- operand length for every opcode;
- operand types;
- variable/reference encoding;
- control flow;
- jumps/conditions;
- calls;
- messages;
- script termination;
- event scheduling.

## Priority 17 — scenario/event script header and event table

For `IAM/AREA`, the script offset is known, but the full structure around:

- event descriptors;
- entry points;
- local variables;
- scheduler metadata;

is not yet completely documented.

## Priority 18 — bridge between scenario VM and SCX Script system

Need to find the exact functions/opcodes that:

- look up an SCX Script template;
- create an instance;
- start it;
- stop it;
- wait for it;
- receive completion.

## Priority 19 — retail asset coverage

Current detailed validation is based heavily on:

```text
aventure.SCX
Grid.SCX
```

A complete format should be checked against a broad sample of:

- city/area SCX files;
- combat/fight SCX;
- shooting SCX;
- character-heavy scenes;
- effect-heavy scenes;
- late-game areas.

## Priority 20 — version/build differences

Current documentation is for one Windows Runtime executable and version-5 SCX data.

Need comparison against:

- other Windows retail/localized builds;
- patches;
- Dreamcast;
- any development/demo data.

---

# 38. Recommended reverse-engineering breakpoints

## SCX loading

```text
0x00449750  SCX loader
0x00449881  tag-2 script parsing branch
0x00449AA0  path-resource branch
0x00449B1F  animation-resource branch
0x00449BA0  sound-resource branch
0x00449C15  visual/sprite-resource branch
```

Useful observations:

- script count;
- template addresses;
- parameter pool base/count;
- primary/sync function counts;
- serialized function IDs;
- serialized indexes before relocation;
- runtime pointers after relocation.

## Script lookup and instances

```text
0x0044A0F0  script lookup by 16-bit ID
```

Set breakpoints on Runtime strings for:

```text
Script_MakeInstance()
Script_RemoveInstance()
Script_RemoveAllInstances()
```

and walk backward to assign exact function-start addresses.

Watch:

- cloned function arrays;
- cloned parameter pools;
- sync-link relocation;
- per-instance sprite creation.

## Main playback

```text
0x0044C860  Script_PlayScript
```

Watch per call:

```text
script + 0x1C  state
script + 0x20  group count
script + 0x24  current group
script + 0x34  repeat limit
script + 0x38  repeat index
script + 0x54  paired script
script + 0x58  elapsed time
```

For each active function:

```text
+0x00 function ID
+0x04 unknown
+0x08 parameters pointer
+0x0C sync pointer
+0x10 execution limit
+0x14 progress
```

## Reinitialization

```text
0x0044A7E0  Script reinit dispatcher
```

Compare function state before/after each opcode-specific handler.

## Parameter mapping

```text
0x0044C090  Script_GetNumParam
```

Log:

```text
function ID
requested parameter selector
returned parameter slot
```

This can reconstruct the parameter semantic matrix efficiently.

## Scenario/event VM

```text
0x00403860  opcode 0x46
```

For startup validation, use the known instruction:

```text
46 1D 00 FF FF 13 00
```

and trace into:

```text
0x0041DF30
0x00429BB0
0x00479D10
```

---

# 39. Recommended automated asset-analysis tooling

A small offline SCX inspection tool would accelerate this work significantly.

It should report, without executing scripts:

```text
SCX header
section tag sequence
script template names/IDs
function-group counts
sync-function counts
function IDs
raw +0x04 values
parameter indexes
sync indexes
execution limits
initial progress values
parameter-pool values
resource-table names
unknown IDs
```

Useful aggregate reports:

```text
all distinct function IDs across all game SCX files
count by function ID
count by script name
which IDs have nonzero middle 16 bits
which IDs use +0x04 != 0
execution-limit distributions
parameter-count/slot usage
which scripts contain family-0x04 one-shot functions
which assets contain 0x02000026 or 0x05000013
```

This can answer several open questions without further decompilation.

---

# 40. Reimplementation checklist

## Parser

```text
[ ] validate SCX magic 0x00DEAD00
[ ] validate/record version 5
[ ] respect mainBlockSize at +0x0C
[ ] dispatch sections by 0xDEADxxxx tag
[ ] parse tag 2 script count
[ ] parse 0x64-byte templates
[ ] preserve unknown template fields
[ ] parse shared 4-byte parameter pool
[ ] parse 0x18-byte function records
[ ] validate parameter indexes
[ ] validate sync-function indexes
[ ] treat 0xFFFFFFFF sync index as null
[ ] do not read serialized indexes as pointers
[ ] preserve unknown function IDs
```

## Runtime representation

```text
[ ] separate ScriptTemplate from ScriptInstance
[ ] give instances mutable function progress
[ ] give instances mutable parameter storage where Runtime does
[ ] rebuild sync links within each instance
[ ] preserve repeat limit/index
[ ] preserve elapsed script time
[ ] preserve paired-script relationship semantics as they are solved
[ ] model resource references explicitly
```

## Dispatcher

```text
[ ] compare complete 32-bit function IDs
[ ] implement known main-dispatch table
[ ] keep "return contributes" metadata explicit
[ ] interpret contributing return as group-still-active
[ ] run non-contributing side effects without blocking group
[ ] respect execution-limit/progress gate
[ ] walk sync/linked functions
[ ] advance group only when no contributor remains active
[ ] repeat through Script reinit when repeat policy requires it
[ ] propagate Script failure distinctly
```

## Reinit

```text
[ ] implement generic Script state reset
[ ] dispatch known opcode-specific reinit functions
[ ] reset progress/resource state with opcode-specific semantics
[ ] do not assume every implemented action has a reinit
```

## Architecture

```text
[ ] do not run every SCX script on load
[ ] do not hardcode script behavior by name
[ ] keep SCX Script system separate from scenario/event bytecode VM
[ ] let scenario/game state activate SCX Script instances normally
[ ] preserve unknown IDs/fields for future analysis
```

---

# 41. Current known function-ID summary

For quick reference:

```text
Family 0x01
    0x01000001  camera operation, exact name unknown
    0x01000002  InterpolateCameras

Family 0x02
    0x02000004  SelectBodyAnimation
    0x02000026  unknown, parameter-mapper recognized
    0x0200002A  SelectRelativeBodyAnimation

Family 0x03
    0x03000008  MoveObjectOnPath
    0x0300001A  AnimationFromExternalScene
    0x03000021  MorphObject
    0x03000023  ScaleObjectX
    0x03000024  ScaleObjectY
    0x03000025  ScaleObjectZ
    0x0300002B  SwapObject

Family 0x04
    0x0400000C  SetSpriteType
    0x0400000D  Display3DSpriteOnPath
    0x04000011  ChainObjects
    0x0400001B  ScaleSpriteOnX
    0x0400001C  ScaleSpriteOnY
    0x0400001D  SetSpriteRolling
    0x0400001E  SetSpritePalette
    0x0400001F  SetSpriteDefaultPalette
    0x04000020  MorphPaletteSprite
    0x04000028  Display3DSprite
    0x04000029  SetSpriteFrame

Family 0x05
    0x05000013  unresolved special/legacy ID
    0x05000014  PlaySound
    0x05000015  PlaySyncSound
    0x05000016  StopSound

Family 0x06
    0x06000017  Wait
    0x06000027  SendMessage
```

Of these, the currently identified main `Script_PlayScript` dispatcher directly handles:

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

That is:

```text
18 main-dispatched function IDs
```

in the currently identified dispatcher.

---

# 42. Corrected version of the earlier saved table

The earlier table can now be rewritten with semantic names and a clearer final-column meaning:

| Function ID | Main handler | Reinit handler | Handler name | Return contributes to **group-still-active** |
|---:|---:|---:|---|:---:|
| `0x01000001` | `0x004A4580` | — | camera operation, exact name unknown | No |
| `0x01000002` | `0x004A4630` | `0x004A4450` | `InterpolateCameras` | Yes |
| `0x02000004` | `0x004A35D0` | `0x004A3290` | `SelectBodyAnimation` | Yes |
| `0x0200002A` | `0x004A3AD0` | `0x004A3430` | `SelectRelativeBodyAnimation` | Yes |
| `0x03000008` | `0x0046F400` | `0x0046EEB0` | `MoveObjectOnPath` | Yes |
| `0x0300001A` | `0x00470060` | `0x0046F0C0` | `AnimationFromExternalScene` | Yes |
| `0x03000021` | `0x00470440` | `0x0046F200` | `MorphObject` | Yes |
| `0x03000023` | `0x004707A0` | `0x0046EB50` | `ScaleObjectX` | Yes |
| `0x03000024` | `0x004709E0` | `0x0046EC70` | `ScaleObjectY` | Yes |
| `0x03000025` | `0x00470C20` | `0x0046ED90` | `ScaleObjectZ` | Yes |
| `0x0300002B` | `0x00470E60` | `0x0046E9C0` | `SwapObject` | No |
| `0x04000011` | `0x004A1E80` | `0x004A19C0` | `ChainObjects` | Yes |
| `0x04000020` | `0x004A2D10` | `0x004A1C90` | `MorphPaletteSprite` | Yes |
| `0x05000014` | `0x004A12D0` | `0x004A0FB0` | `PlaySound` | No |
| `0x05000015` | `0x004A14D0` | `0x004A1100` | `PlaySyncSound` | Yes |
| `0x05000016` | `0x004A16D0` | `0x004A11F0` | `StopSound` | No |
| `0x06000017` | `0x004A0EE0` | `0x004A0E50` | `Wait` | Yes |
| `0x06000027` | `0x004A0E80` | — | `SendMessage` | No |

Reset/reinit IDs from the older table that are not main-dispatched remain valid evidence:

| Function ID | Action implementation | Reinit handler | Name |
|---:|---:|---:|---|
| `0x0400000D` | `0x004A2150` | `0x004A1B20` | `Display3DSpriteOnPath` |
| `0x0400001B` | `0x004A25E0` | `0x004A1810` | `ScaleSpriteOnX` |
| `0x0400001C` | `0x004A2790` | `0x004A18A0` | `ScaleSpriteOnY` |
| `0x0400001D` | `0x004A2940` | `0x004A1930` | `SetSpriteRolling` |
| `0x04000028` | `0x004A2FB0` | `0x004A1DE0` | `Display3DSprite` |

And the updated Runtime analysis adds:

```text
0x02000026  unknown but Script_GetNumParam-recognized

0x0400000C  SetSpriteType
0x0400001E  SetSpritePalette
0x0400001F  SetSpriteDefaultPalette
0x04000029  SetSpriteFrame

0x05000013  unresolved special-case ID
```

---

# 43. Current reverse-engineering boundary

The current state can be summarized as:

```text
SCX version-5 container
    ->
tag 0xDEAD0002
    ->
0x64-byte Script templates
    ->
shared 4-byte parameter-value pool
    ->
0x18-byte function records
        |
        +-- 32-bit 0xGG0000NN function ID
        +-- unknown dword
        +-- serialized parameter index
        +-- serialized sync-function index
        +-- execution limit
        +-- mutable progress
    ->
load-time index-to-pointer relocation
    ->
Script template registry
    ->
Script_MakeInstance
    ->
instance-local mutable functions/parameters/resources
    ->
Script_PlayScript
        |
        +-- current primary group
        +-- linked/sync functions
        +-- per-function execution gate
        +-- opcode dispatch
        +-- selected returns OR into group-still-active
        +-- advance group when all contributors are done
        +-- repeat/reinit as configured
    ->
opcode-specific engine actions
```

In parallel:

```text
IAM scenario/event script
    ->
byte-oriented VM
    ->
one-byte opcode such as 0x46
    ->
high-level game/interface/event operation
```

The broad architecture and the main SCX function dispatcher are now well enough understood to implement a non-hardcoded Script framework.

The most valuable next reverse-engineering work is:

1. complete the tag-2 byte-for-byte serialization grammar;
2. recover the parameter semantic enum and per-function schemas;
3. resolve the alternate/reachability path for family-`0x04` sprite functions;
4. identify `0x02000026` and `0x05000013`;
5. reconstruct the compact IAM scenario/event opcode table;
6. locate and document the exact bridge between the scenario VM and SCX Script instances.
