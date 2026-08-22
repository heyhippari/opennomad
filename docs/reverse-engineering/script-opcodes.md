# Omikron SCX scripting and IAM scenario/event VM

> **Status:** work-in-progress reverse-engineering documentation for OpenNomad  
> **Last updated:** 2026-08-22
>
> This document describes the **serialization, runtime architecture, instance
> model, scheduling, and cross-system execution flow** of the two scripting
> systems currently identified in the Windows retail build of
> *Omikron: The Nomad Soul*:
>
> 1. the structured `.SCX` `Script_*` system built from fixed-size function
>    records containing 32-bit IAM/Quantic C function IDs; and
> 2. the compact byte-oriented scenario/event VM used by `IAM/AREA`,
>    `IAM/START`, and related scenario data.
>
> The detailed 32-bit IAM function catalogue is intentionally **not** duplicated
> here. [`iam-script-functions.md`](iam-script-functions.md) is authoritative for:
>
> - `0xCC0000NN` IAM function IDs;
> - recovered `Script_*` names;
> - native action/reinit handler addresses;
> - function families and global ordinals;
> - `Script_GetNumParam()` selector-to-slot mappings;
> - per-function semantics;
> - legacy/special IAM IDs.
>
> This file is authoritative for:
>
> - `.SCX` version-5 script serialization;
> - the `0x64` script-template record;
> - the `0x18` script-command/function record;
> - shared value-pool representation;
> - related-script and binding-table data;
> - index-to-pointer relocation;
> - mutable script instances;
> - `Script_PlayScript()` group scheduling;
> - execution-limit/count gating;
> - synchronized linked-command traversal;
> - handler-return blocking semantics;
> - the compact IAM scenario/event VM;
> - currently recovered one-byte AREA opcodes;
> - the now-recovered bridge from AREA bytecode to structured SCX scripts.

---

# 1. Evidence model

Sources are used in this order:

1. **`Runtime.exe` behavior** — authoritative for execution, field meaning,
   pointer relocation, timing, waits, scheduling, and resource side effects.
2. **Retail data** — principally `Grid.SCX`, `aventure.SCX`, `IAM/AREA`,
   `IAM/START`, and their associated TAG/name registries.
3. **OpenNomad parser/runtime behavior** — useful where it mirrors Runtime, but
   explicitly subordinate where the implementation is knowingly approximate.
4. **Historical Quantic Dream material** — useful for authoring intent and
   terminology, but not a replacement for Runtime evidence.

Analyzed Runtime build:

```text
File:             Runtime.exe
Architecture:     PE32 / i386
Image base:       0x00400000
Linker timestamp: 1999-10-04 20:31:50
SHA-256:          55f7120bfea7891b048c64e3682f3259cdbf2719a43fa24e42254b753c95d2ef
```

Confidence labels:

- **Confirmed — Runtime:** directly established from executable behavior.
- **Confirmed — data:** directly established from retail serialized data.
- **Corroborated:** Runtime and data agree.
- **Strongly reconstructed:** several independent observations agree, but no
  original source-level symbol is available.
- **Provisional:** useful working interpretation requiring further tracing.
- **Unknown:** structure exists but semantics are not yet recovered.

---

# 2. Two different script systems

The term “script” refers to at least two independent runtime systems.

They share the IAM/scenario authoring ecosystem, but their serialized forms and
execution machinery are different.

## 2.1 Structured SCX `Script_*` system

This is the system associated with Runtime diagnostics such as:

```text
Script_PlayScript()
Script_GetNumParam()
Script_MakeInstance()
Script_RemoveInstance()
Script_RemoveAllInstances()
Script_FunctionsIndexesToAdresses()
Script_FunctionsAdressesToIndexes()
```

Its executable actions are identified by 32-bit IAM function IDs:

```text
0xCC0000NN
```

Examples:

```text
0x0200002A  SelectRelativeBodyAnimation
0x04000029  SetSpriteFrame
0x05000015  PlaySyncSound
0x06000017  Wait
```

Its serialized representation is a graph/table of high-level actions, not a
traditional bytecode stream.

Core concepts:

```text
0x64-byte script template
shared 32-bit value pool
0x18-byte root command records
0x18-byte linked command records
binding tables
related-script link
mutable execution counters
mutable parameter values
per-instance resources
```

## 2.2 Compact IAM scenario/event VM

`IAM/AREA` and related data use a different interpreter:

```text
u8 opcode
opcode-specific operands
evaluation stack
global/START-variable namespace
relative control flow
native scenario operations
explicit wait states
```

Known examples:

```text
0x03 EndEvent
0x04 JumpRelative
0x06 BranchIfFalse
0x19 Equal
0x39 StartScxScript
0x46 OpenInterface
0x67 PlayMusic
0x84 BeginCinematicLetterbox
```

This document calls this the **AREA VM** or **scenario/event VM**.

## 2.3 Never merge the namespaces

These values are unrelated namespaces:

```text
structured IAM function ID:
    0x04000029

AREA VM opcode:
    0x46
```

They differ in:

- instruction/record width;
- operand representation;
- lookup/dispatch path;
- scheduling;
- lifetime;
- wait/completion behavior;
- serialization.

OpenNomad should keep the interpreters architecturally separate.

---

# 3. Current high-level architecture

The current recovered model is:

```text
                         RETAIL DATA

        .SCX                                    IAM/AREA / IAM/START
          |                                               |
          v                                               v
 structured Script templates                      compact AREA bytecode
          |                                               |
          |                              +----------------+----------------+
          |                              |                                 |
          |                              v                                 v
          |                       evaluation stack                  native scenario ops
          |                       global variables                  interface/music/etc.
          |                              |                                 |
          |                              +---------------+-----------------+
          |                                              |
          |                                              v
          |                                  AREA script runtime/context
          |                                              |
          |                   +--------------------------+--------------------------+
          |                   |                          |                          |
          |                   v                          v                          v
          |             0x39 StartScxScript        0x3B character          0x3C tracked
          |                   |                    script launch            character script
          |                   |                          |                          |
          +-------------------+--------------------------+--------------------------+
                                              |
                                              v
                                    Script_MakeInstance
                                              |
                                              v
                                       mutable instance
                                              |
                                              v
                                     Script_PlayScript
                                              |
                                root command + linked chain
                                              |
                                              v
                                      native Script_* ops
```

The bridge between the two scripting layers is no longer hypothetical:
`0x39`, `0x3B`, and `0x3C` explicitly start structured SCX scripts.

---

# 4. SCX v5 context

The complete SCX format deserves its own dedicated document, but script
serialization depends on the container layout and resource manifest.

## 4.1 Header

Observed files begin:

| Offset | Type | Meaning |
|---:|---|---|
| `0x00` | `u32` | magic `0x00DEAD00` |
| `0x04` | `u32` | version, currently `5` |
| `0x08` | `u32` | unresolved header word; observed as `8` |
| `0x0C` | `u32` | descriptor-block byte size |

The descriptor block begins at:

```text
file + 0x10
```

and ends at:

```text
resourceStreamOffset = 0x10 + descriptorSize
```

This is better described as a **descriptor/tag block** than as the resource
payload itself.

## 4.2 Descriptor block versus appended resource stream

The file is conceptually:

```text
ScxHeader
    |
    +-- descriptor/tag block
    |      DEAD0000
    |      DEAD0001
    |      DEAD0002
    |      ...
    |      DEADFFFF
    |
    +-- appended resource stream
           resource payload
           resource payload
           resource payload
           ...
```

Descriptor sections act as a manifest describing resources that are consumed
later from the appended stream.

Not every descriptor section owns an appended resource.

## 4.3 Known section meanings

Current parser/runtime understanding:

| Tag | Descriptor payload | Appended payload |
|---:|---|---|
| `DEAD0000` | `0x20` named resource descriptors | 8-byte-framed path/3DP-like resources |
| `DEAD0001` | `0x24` animation descriptors | 8-byte-framed animation/3DA payloads |
| `DEAD0002` | structured scripts | none outside the section's own serialized data |
| `DEAD0003` | `0x1A` sound descriptors | RIFF/WAVE resources |
| `DEAD0004` | `0x24` sprite/effect descriptors | embedded 3DO package + auxiliary texture block |
| `DEAD0005` | `0x1C` scene descriptors | 8-byte-framed external-scene resources |
| `DEAD0006` | `0x318` opaque records | none currently identified |
| `DEAD0007` | count/flags + `0x20` records | none currently identified |
| `DEAD0008` | global mode/limit marker | none |
| `DEAD0009` | reserved/no-op in current parser | none |
| `DEAD000A` | extra-block descriptor state | one 8-byte-framed extra block |
| `DEADFFFF` | end marker | — |

Section order is data-dependent.

A parser must dispatch by tag value rather than assuming numerical order.

---

# 5. `DEAD0002`: structured script section

This section contains:

```text
script templates
shared argument/value pool
per-script related-script data
root command arrays
linked command arrays
binding table A
binding table B
```

## 5.1 Top-level order

Current recovered serialized order:

```text
u32 scriptCount

SerializedScriptTemplateV5 scripts[scriptCount]  // 0x64 each

u32 sharedValueCount
u32 sharedValues[sharedValueCount]

for each script in template order:
    u8 relatedScriptPresent

    if relatedScriptPresent != 0:
        char relatedScriptName[21]

    ScriptCommand rootCommands[rootCommandCount]       // 0x18 each
    ScriptCommand linkedCommands[linkedCommandCount]   // 0x18 each

    BindingTable tableA
    BindingTable tableB
```

This per-script auxiliary sequence is essential.

The root/linked command arrays are **not** stored inline inside the fixed
`0x64` template record.

## 5.2 Shared value pool

The value pool is:

```text
u32 sharedValueCount
u32 sharedValues[sharedValueCount]
```

Each word is intentionally untyped at serialization level.

Depending on the IAM function, a value can represent:

- unsigned integer;
- signed integer;
- float bit pattern;
- resource index;
- object ID/index;
- frame count;
- duration;
- mutable progress;
- flags;
- coordinate component;
- table index.

The same shared serialized pool is deep-copied into each mutable script instance.

## 5.3 Binding table format

Each binding table currently parses as:

```text
u32 count
u8 slotMetadata[count * 8]   // exact two-dword entry semantics unresolved
char names[count][21]
```

The fixed template stores a three-dword descriptor for each binding table:

```text
+0x3C .. +0x44  binding table A descriptor
+0x48 .. +0x50  binding table B descriptor
```

The first dword of each descriptor agrees with the corresponding appended
binding-table count in the examined retail data.

The remaining two dwords are pointer/index/runtime-oriented and remain
unresolved.

## 5.4 Known binding-table use

`Script_SelectRelativeBodyAnimation` uses argument 0 as an index into
**binding table A**.

The resulting 21-byte name identifies the object/body channel binding used by
the animation operation.

Observed example from `Grid.SCX`:

```text
script: "1KaylArrives"

binding table A:
    [0] "UBassin"
```

Binding table B's role remains unresolved.

---

# 6. Script template record — `0x64` bytes

Current conservative layout:

```c
struct SerializedScriptTemplateV5 {
    uint32_t scenarioOwnerPlaceholder;    // +0x00

    char     name[22];                    // +0x04 .. +0x19
    uint16_t scriptId;                    // +0x1A

    uint16_t runtimeState;                // +0x1C
    uint16_t flags;                       // +0x1E

    uint32_t rootCommandCount;            // +0x20
    uint32_t currentRootCommandIndex;     // +0x24
    uint32_t rootCommandsPlaceholder;     // +0x28

    uint32_t linkedCommandCount;          // +0x2C
    uint32_t linkedCommandsPlaceholder;   // +0x30

    int32_t  repeatLimit;                 // +0x34
    uint32_t repeatIndex;                 // +0x38

    uint32_t bindingTableAFields[3];      // +0x3C .. +0x44
    uint32_t bindingTableBFields[3];      // +0x48 .. +0x50

    uint32_t relatedScriptPlaceholder;    // +0x54
    uint32_t elapsedTimeBits;             // +0x58, runtime treats as float

    uint8_t  tail[8];                     // +0x5C .. +0x63
}; // 0x64
```

The exact serialized-vs-runtime interpretation varies by field.

OpenNomad should keep immutable parsed data separate from mutable runtime
instances.

---

# 7. Script-template fields

## 7.1 `+0x00`: owner/scenario placeholder

The first dword is overwritten/fixed up after load.

It is not a serialized process pointer that should be trusted directly.

## 7.2 `+0x04 .. +0x19`: script name

Confirmed width:

```text
22 bytes
```

This is distinct from the numeric script ID at `+0x1A`.

Older documentation incorrectly described a 20-byte name plus an unknown
`+0x18` word.

That is obsolete.

## 7.3 `+0x1A`: script ID

A Runtime lookup around:

```text
0x0044A0F0
```

compares the requested ID against:

```text
WORD [script + 0x1A]
```

AREA opcode `0x39` uses this ID to locate a structured script.

## 7.4 `+0x1C`: runtime state

`Script_PlayScript()` checks this before normal execution.

The complete enum is not yet mapped.

## 7.5 `+0x1E`: flags/state

The low nibble and upper bits participate in execution/context logic.

Individual bits remain incompletely named.

## 7.6 `+0x20`, `+0x24`, `+0x28`: root commands

```text
+0x20 rootCommandCount
+0x24 currentRootCommandIndex
+0x28 root-command pointer/placeholder
```

Runtime selects one root command by:

```text
rootCommands[currentRootCommandIndex]
```

with a `0x18` stride.

## 7.7 `+0x2C`, `+0x30`: linked commands

```text
+0x2C linkedCommandCount
+0x30 linked-command pointer/placeholder
```

The root command can link into this array through its `+0x0C` field.

Linked commands can themselves link to additional linked commands.

## 7.8 `+0x34`, `+0x38`: whole-script repetition

Runtime confirms:

```text
+0x34 repeatLimit
+0x38 current repeat count/index
```

At end-of-script, Runtime increments the current repeat state and compares it
with the configured limit.

A limit of:

```text
0xFFFFFFFF / -1
```

is treated specially as unlimited/infinite.

When repeating, Runtime invokes reinitialization rather than merely resetting
the root index.

## 7.9 `+0x3C .. +0x50`: two binding-table descriptors

These six dwords correspond to:

```text
binding table A descriptor
binding table B descriptor
```

The associated serialized variable-length binding-table data follows each
script's command arrays.

## 7.10 `+0x54`: related/paired script

Runtime treats this as a related script pointer after fixup.

The exact authoring relationship remains unresolved:

- parent/child;
- continuation;
- synchronized companion;
- lifecycle ownership;
- completion dependency.

Do not rename it more narrowly yet.

## 7.11 `+0x58`: elapsed script time

Runtime executes:

```text
script[+0x58] += globalScriptFrameDelta
```

as a floating-point operation.

This is separate from command-local progress parameters and command
execution-count fields.

## 7.12 `+0x5D`: related-script/pairing gate

Runtime uses this byte in conjunction with the related-script path.

Exact source-level meaning remains unresolved.

## 7.13 `+0x5E .. +0x61`: context-selection bytes

These four bytes participate in global/scenario context gating.

They are real execution metadata and should be preserved.

Their exact semantic names remain unresolved.

---

# 8. Script command/function record — `0x18` bytes

The serialized record is now substantially recovered.

```c
struct SerializedScriptCommand {
    uint32_t functionId;           // +0x00
    uint32_t valueCount;           // +0x04
    uint32_t firstValueIndex;      // +0x08
    int32_t  nextLinkedIndex;      // +0x0C, -1 = none
    uint32_t executionLimit;       // +0x10, 0xFFFFFFFF = unlimited
    uint32_t executionCount;       // +0x14
}; // 0x18
```

This replaces older documentation that treated `+0x04`, `+0x10`, and `+0x14`
as largely unknown generic runtime state.

## 8.1 `+0x00`: IAM function ID

The full 32-bit ID is documented in:

[`iam-script-functions.md`](iam-script-functions.md)

Examples:

```text
0x0200002A
0x04000029
0x05000015
0x06000017
```

Do not dispatch using only the low byte.

## 8.2 `+0x04`: `valueCount`

Number of raw 32-bit words used by this command.

This is not the number of semantic selectors reported by
`Script_GetNumParam()`.

## 8.3 `+0x08`: `firstValueIndex`

Serialized form:

```text
index into the ScriptList/shared value pool
```

Runtime form after relocation:

```text
pointer to first command value
```

Therefore:

```text
command values =
    sharedValues[firstValueIndex
                 .. firstValueIndex + valueCount)
```

## 8.4 `+0x0C`: `nextLinkedIndex`

Serialized form:

```text
signed index into the linked-command array
```

with:

```text
-1 = no next linked command
```

Runtime form after relocation:

```text
pointer to next linked ScriptFunction
```

Runtime diagnostics use the term:

```text
SyncFunction
```

for this relationship.

## 8.5 `+0x10`: `executionLimit`

Command-level execution limit.

Special value:

```text
0xFFFFFFFF
```

means unlimited/infinite.

## 8.6 `+0x14`: `executionCount`

Mutable command-local count/progress.

Runtime compares it with `executionLimit` before dispatch.

Handlers/reinitializers can also mutate function-specific value-pool fields in
addition to this count.

---

# 9. Example command records

Retail `Grid.SCX` makes the recovered fields concrete.

Conceptually:

```text
Script_SelectRelativeBodyAnimation
    functionId      = 0x0200002A
    valueCount      = 12
    firstValueIndex = 0
    nextLinkedIndex = 0
    executionLimit  = 1
    executionCount  = 0
```

and a synchronized sound command can appear as:

```text
Script_PlaySyncSound
    functionId      = 0x05000015
    valueCount      = 5
    executionLimit  = 1
    executionCount  = 0
```

The linked relationship is structural; the functions' own durations and
completion behavior are independent.

---

# 10. Index-to-pointer relocation

Runtime diagnostic names:

```text
Script_FunctionsIndexesToAdresses()
Script_FunctionsAdressesToIndexes()
```

confirm that script graphs have serialized index form and pointer-rich runtime
form.

The misspelling `Adresses` is preserved from Runtime.

## 10.1 Parameter/value relocation

Serialized:

```text
+0x08 = firstValueIndex
```

Runtime:

```text
+0x08 = pointer into shared/mutable value pool
```

## 10.2 Linked-command relocation

Serialized:

```text
+0x0C = linked-command index
```

Runtime:

```text
+0x0C = pointer to linked ScriptFunction
```

The inverse path validates that runtime addresses belong to the expected arrays
before converting them back to indexes.

Representative diagnostics:

```text
Script_FunctionsAdressesToIndexes(): Index of FuncParams too big: %d/%d.
Script_FunctionsAdressesToIndexes(): Index of SyncFunction too big : %d/%d.
Script_FunctionsAdressesToIndexes(): Ptr is NULL.

Script_FunctionsIndexesToAdresses(): Address of FuncParams isn't valid.
Script_FunctionsIndexesToAdresses(): Address of SyncFunction isn't valid.
```

---

# 11. Templates versus mutable instances

Runtime has explicit instance lifecycle functions:

```text
Script_MakeInstance()
Script_RemoveInstance()
Script_RemoveAllInstances()
```

A loaded template is therefore not the object that should be mutated during
playback.

## 11.1 Instance creation

`Script_MakeInstance()` can allocate/copy:

- root command records;
- linked command records;
- command values;
- sync-function references;
- per-instance sprite state.

## 11.2 Mutable value-pool copy

The shared serialized value pool is copied for runtime execution.

This is necessary because handlers mutate values such as:

- elapsed progress;
- current scale/roll;
- sound-start latches;
- animation progress;
- frame state;
- other command-local runtime values.

OpenNomad currently mirrors this by deep-copying `ScxData::shared_values` into
each `ScriptInstance`.

## 11.3 Command counters are instance-local

`executionCount` is mutable runtime state.

Independent instances of the same template must not share it.

## 11.4 Linked references are rebuilt inside the instance

If a serialized template says:

```text
root command A -> linked command B
```

a runtime instance must resolve:

```text
instance A -> instance B
```

not:

```text
instance A -> template B
```

---

# 12. `Script_PlayScript()` scheduling

Main playback is around:

```text
0x0044C860
```

Runtime uses **two different completion mechanisms**:

1. command execution-limit/count gating; and
2. selected handler return values that keep the current synchronized group
   active.

These mechanisms are complementary.

---

# 13. Pre-dispatch execution-count gate

Before executing a command, Runtime compares:

```text
command +0x10  executionLimit
command +0x14  executionCount
```

Conceptually:

```cpp
if (executionLimit != 0xFFFFFFFF &&
    executionCount >= executionLimit) {
    skipThisCommand();
}
```

This gate answers:

```text
is this command still eligible to execute?
```

It does **not** by itself answer:

```text
should the current root group advance this tick?
```

That second question is determined by selected handler return values.

---

# 14. Synchronized linked-command chain

One root command defines the current group.

Its `nextLinkedIndex`/runtime `SyncFunction` pointer can chain into linked
commands.

Conceptually:

```text
root group N
    |
    +-- root command
            |
            +-- linked command
                    |
                    +-- linked command
                            ...
```

Runtime services the entire reachable chain.

Each linked command can have independent:

- `executionLimit`;
- `executionCount`;
- values;
- duration;
- native handler;
- blocking/non-blocking behavior.

The term “group” is a behavior-based documentation term; `SyncFunction` is the
actual Runtime diagnostic terminology for the link.

---

# 15. Handler-return group blocking

For a subset of stateful IAM functions, `Script_PlayScript()` literally does:

```asm
call Script_...
or   bl, al
```

`BL` is the current group's “still active” accumulator.

Stateful/blocking functions include:

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

Immediate actions are invoked without ORing their return into the accumulator.

Examples include:

```text
SelectCamera
SwapObject
PlaySound
StopSound
SendMessage
```

The authoritative per-function table is in:

[`iam-script-functions.md`](iam-script-functions.md)

## 15.1 Meaning of the return

For contributing handlers:

```text
AL != 0
```

means approximately:

```text
this synchronized operation remains active
```

It is **not generic success/failure**.

## 15.2 Group advance

After all eligible commands in the root+linked chain have been serviced:

```text
BL != 0:
    remain on current root group

BL == 0:
    advance script +0x24
```

Therefore the correct scheduler model is:

```text
executionLimit/executionCount
    -> command eligibility

handler AL for selected functions
    -> group remains active this tick

BL == 0
    -> advance root group
```

---

# 16. Current OpenNomad scheduler fidelity note

At the time of writing, OpenNomad's `ScriptRuntime` advances a group when every
command in the chain is considered exhausted by its
`executionLimit/executionCount` state.

That is a practical current implementation, but it is **not an exact match** for
the recovered retail `Script_PlayScript()` logic.

Runtime explicitly combines:

```text
execution-count gating
+
handler AL -> BL blocking accumulation
```

The documentation follows Runtime.

OpenNomad should eventually be updated to reproduce the original two-stage
scheduler.

---

# 17. Reinitialization

Runtime has a generic structured-function reinit dispatcher around:

```text
0x0044A7E0
```

Many functions have `Script_Reinit_*` companions.

The authoritative per-function pairing table is in:

[`iam-script-functions.md`](iam-script-functions.md)

## 17.1 What reinit resets

Depending on the function, reinitialization can reset:

- value-pool progress;
- interpolation state;
- sound latches;
- animation progress;
- sprite state;
- execution-related state.

Repetition is therefore not correctly modeled as only:

```text
currentRootCommandIndex = 0
```

## 17.2 Whole-script repetition

At end-of-script Runtime:

1. increments the script repeat state;
2. compares against `+0x34`;
3. treats `-1` as infinite;
4. invokes reinitialization;
5. restarts according to the recovered lifecycle.

---

# 18. `Script_GetNumParam()` and semantic parameter roles

`Script_GetNumParam()` maps a semantic selector to a raw positional value slot
for a given IAM function.

Example conceptually:

```text
semantic "sprite" selector
    ->
argument slot 0

semantic "frame" selector
    ->
argument slot 1
```

The complete current selector matrix belongs in:

[`iam-script-functions.md`](iam-script-functions.md)

Important:

```text
semantic selector count != command.valueCount
```

Not every raw command value is represented by the semantic helper API.

---

# 19. Loading does not imply execution

SCX loading creates:

- resource descriptors;
- appended resource indices;
- script templates;
- shared values;
- command arrays;
- binding tables;
- runtime lookup structures.

It does not mean:

```text
execute every script template
```

For example:

```text
"1KaylArrives"
```

being present in `Grid.SCX` does not itself trigger the scene.

Activation comes from scenario/game logic.

Do not hard-code behavior by script name.

---

# 20. Compact AREA scenario/event VM

The AREA VM is now sufficiently recovered to describe as a small stack-based
scenario interpreter rather than just “variable-length one-byte opcodes”.

Core state:

```text
instruction pointer
queued event(s)
active/running state
evaluation stack
START/global-variable store
wait state
native subsystem bridges
```

Conceptually:

```text
queue event
    |
activate context
    |
run
    |
    +-- stack operations
    +-- global-variable operations
    +-- branches/jumps
    +-- SCX script launches
    +-- character operations
    +-- interface operations
    +-- camera operations
    +-- presentation effects
    +-- music
    |
wait / ready / completed / failed
```

---

# 21. AREA VM lifecycle

Current recovered/runtime-modeled lifecycle:

```text
created
    |
    v
ready
    |
queue event
    |
activate
    |
    v
running
    |
    +-- EndEvent -> ready
    +-- native wait -> waiting
    +-- unsupported -> paused
    +-- malformed execution -> failed
    +-- end of stream -> completed
```

Runtime numeric context states are only assigned where they have been directly
recovered.

Known wait-state values:

```text
state 4  tracked explicit-character script wait (0x3C)
state 6  interface wait (0x46)
state 7  camera wait (0x60)
```

---

# 22. AREA instruction encoding

Instruction format:

```text
u8 opcode
operand bytes...
```

Operand widths are opcode-specific.

Currently observed operand types:

```text
i8
i16 little-endian
i32 little-endian
```

Instruction length must be determined from the opcode definition.

Never consume arbitrary bytes after an unknown or operand-less opcode.

---

# 23. Evaluation stack and global variables

The VM has an explicit evaluation stack.

Currently recovered core operations prove this directly:

```text
0x07 PushInt8
0x0A PushGlobalVariable
0x19 Equal
0x06 BranchIfFalse
```

Global/START variables are manipulated by:

```text
0x0D SetGlobalVariableOne
0x0E SetGlobalVariable
```

The current high-level model is:

```text
START/global variable namespace
        |
        +-- read
        +-- write
        |
        v
evaluation stack
        |
        +-- comparison
        +-- conditional branch
```

---

# 24. Current AREA opcode catalogue

The following table reflects the current recovered compatibility set.

Names marked **provisional** are behavior-based OpenNomad names, not proven
original source symbols.

| Opcode | Working name | Operands | Current status |
|---:|---|---|---|
| `0x03` | `EndEvent` | none | recovered |
| `0x04` | `JumpRelative` | `i16` | recovered |
| `0x06` | `BranchIfFalse` | `i16` | recovered |
| `0x07` | `PushInt8` | `i8` | recovered |
| `0x0A` | `PushGlobalVariable` | `i16` | recovered |
| `0x0D` | `SetGlobalVariableOne` | `i16` | recovered |
| `0x0E` | `SetGlobalVariable` | `i16, i8` | recovered |
| `0x19` | `Equal` | none | recovered |
| `0x38` | `CharacterLookup` | `i16` | provisional |
| `0x39` | `StartScxScript` | `i16, i16, i16` | recovered bridge |
| `0x3B` | `StartCharacterScript` | `i16, i16, i16` | provisional name, behavior recovered |
| `0x3C` | `StartCharacterScriptTracked` | `i16, i16, i16` | provisional name, behavior recovered |
| `0x46` | `OpenInterface` | `i16, i16, i16` | recovered |
| `0x4E` | `ActivateCharacter` | `i16, i16` | provisional name, behavior substantially traced |
| `0x4F` | `CharacterSelectionReset` | `i16` | provisional |
| `0x5C` | `ObjectActivate` | `i16` | provisional |
| `0x5F` | `CameraSelect` | `i16, i16, i16` | provisional |
| `0x60` | `CameraMoveAndWait` | `i16, i16, i16` | provisional |
| `0x67` | `PlayMusic` | `i16, i16, i16` | recovered |
| `0x68` | `ActivateSubsystem` | none | provisional |
| `0x76` | `PresentationEffect` | `i32, i16, i16` | provisional |
| `0x77` | `PresentationEffectAlt` | `i32, i16, i16` | provisional |
| `0x83` | `SubsystemOperation` | `i16, i16` | provisional |
| `0x84` | `BeginCinematicLetterbox` | none | recovered |
| `0x85` | `EndCinematicLetterbox` | none | recovered |

This is not yet the complete retail opcode table.

---

# 25. Control-flow opcodes

## 25.1 `0x03` — `EndEvent`

```text
operands: none
```

Terminates the current queued AREA event.

The evaluation stack is cleared and the context returns to its ready state.

## 25.2 `0x04` — `JumpRelative`

```text
operands:
    i16 displacement
```

The target is computed relative to the instruction pointer immediately after
the operand.

Conceptually:

```text
target = postOperandIP + displacement
```

## 25.3 `0x06` — `BranchIfFalse`

```text
operands:
    i16 displacement
```

Pops one value from the evaluation stack.

If the value is zero:

```text
jump relative
```

otherwise execution continues to the following instruction.

## 25.4 `0x07` — `PushInt8`

```text
operands:
    i8 value
```

Pushes a signed immediate onto the evaluation stack.

## 25.5 `0x19` — `Equal`

```text
operands: none
```

Pops:

```text
rhs
lhs
```

and pushes:

```text
1 if lhs == rhs
0 otherwise
```

---

# 26. Global-variable opcodes

## 26.1 `0x0A` — `PushGlobalVariable`

```text
operands:
    i16 variableId
```

Pushes the current START/global-variable value.

Current OpenNomad behavior uses:

```text
0
```

for an unset variable.

## 26.2 `0x0D` — `SetGlobalVariableOne`

```text
operands:
    i16 variableId
```

Sets:

```text
global[variableId] = 1
```

## 26.3 `0x0E` — `SetGlobalVariable`

```text
operands:
    i16 variableId
    i8  value
```

Sets the global variable to the signed immediate value.

---

# 27. AREA -> structured SCX script bridge

This bridge is now directly represented by three opcodes.

## 27.1 `0x39` — `StartScxScript`

```text
operands:
    i16 scriptId
    i16 operandB
    i16 operandC
```

Runtime resolves operand 0 against:

```text
ScxScript +0x1A
```

and creates a structured script instance.

Current behavior:

```text
start SCX script
    |
    v
obtain concrete ScriptRuntime instance
    |
    v
AREA context waits for that exact instance
    |
    v
instance completes
    |
    v
AREA resumes after 0x39
```

The exact semantics of operands B and C remain unresolved.

This opcode is the generic world/non-explicit-character SCX-script bridge.

## 27.2 `0x3B` — `StartCharacterScript`

```text
operands:
    i16 characterId
    i16 scriptId
    i16 parameter
```

Starts an SCX script with an explicit character launch context.

Current recovered behavior is **fire-and-forget** from the AREA context:

```text
AREA continues immediately
```

The exact original source name remains unknown.

## 27.3 `0x3C` — `StartCharacterScriptTracked`

Same operand shape:

```text
i16 characterId
i16 scriptId
i16 parameter
```

but the parent AREA context tracks the spawned script.

Known Runtime wait state:

```text
4
```

Conceptually:

```text
start character-bound SCX script
    |
    v
store concrete child instance
    |
    v
AREA wait state 4
    |
    v
that exact child completes
    |
    v
AREA resumes
```

This distinction between `0x3B` and `0x3C` is architecturally important.

---

# 28. `0x38` — `CharacterLookup` (provisional)

```text
operands:
    i16 character/table ID
```

Current tracing associates it with character-related lookup/activation through
AREA table 0.

The final source-level semantic name is not yet established.

---

# 29. `0x4E` — `ActivateCharacter` (provisional name)

```text
operands:
    i16 characterId
    i16 applyAreaTransform
```

Current Runtime tracing establishes:

- normal character IDs are resolved through active AREA table 0;
- an existing runtime character can be reactivated;
- its AREA presence state is set;
- when operand 1 is non-zero, the serialized AREA transform is applied;
- `characterId == -1` takes a special current-character path.

The exact original opcode name remains unrecovered, but the operation is much
better understood than a generic “character opcode”.

This instruction does not introduce a wait.

---

# 30. `0x4F` — character selection/reset path

```text
operands:
    i16 value
```

Behavior is character-related and currently treated as a
selection/reset-style operation.

The name remains provisional.

---

# 31. `0x5C` — object activation path

```text
operands:
    i16 objectId
```

Current tracing associates it with object activation/load behavior.

The final semantic name remains provisional.

---

# 32. Camera opcodes

## 32.1 `0x5F` — `CameraSelect` (provisional)

```text
operands:
    i16 cameraId
    i16 durationOrMode
    i16 flags
```

Schedules/selects an IAM camera without entering Runtime wait state 7.

The exact meaning of operands 1 and 2 still needs deeper tracing.

## 32.2 `0x60` — `CameraMoveAndWait` (provisional)

Same operand shape:

```text
i16 cameraId
i16 durationUnits
i16 flags
```

When the duration is non-zero, Runtime uses:

```text
wait state 7
```

The scenario time base is 30 Hz.

OpenNomad currently models the wait by decrementing the recovered duration in
30 Hz scenario units.

The final original name and completion callback path remain unresolved.

---

# 33. `0x46` — `OpenInterface`

This opcode is now substantially recovered.

```text
operands:
    i16 interfaceId
    i16 interfaceArgument
    i16 resultVariableId
```

Observed startup instruction:

```text
46 1D 00 FF FF 13 00
```

decodes as:

```text
OpenInterface(
    interfaceId        = 29,
    interfaceArgument  = -1,
    resultVariableId   = 19
)
```

Interface 29 is the main menu.

## 33.1 Wait semantics

Opening an interface places the AREA context in:

```text
Runtime state 6
```

The context stores the concrete opened interface instance/handle.

It does not resume merely because “some interface” completed.

## 33.2 Completion result

When the matching interface completes:

```text
result -> START/global variable resultVariableId
```

and AREA execution resumes at the instruction immediately after `0x46`.

This explains why startup script execution does not run straight through the
main-menu sequence.

---

# 34. `0x67` — `PlayMusic`

Known handler:

```text
0x00404FB0
```

Instruction shape:

```text
i16 trackId
i16 loopFlag
i16 mode
```

Observed startup bytes:

```text
67 6D 00 01 00 01 00
```

decode as:

```text
trackId  = 109
loopFlag = 1
mode     = 1
```

Current semantics:

```text
TRACKS/<trackId>.ADP
```

with non-zero operand 1 requesting looping.

Operand 2 is preserved but not fully interpreted.

Runtime avoids restarting an already-active track with the same numeric ID.

Music playback can also be suppressed by global/runtime state.

---

# 35. `0x68` — subsystem activation (provisional)

```text
operands: none
```

Current implementation exposes this as a native subsystem-activation operation.

The actual target subsystem and final source name are unresolved.

---

# 36. `0x76` and `0x77` — presentation effects

Both use:

```text
i32 colorOrPackedValue
i16 operandB
i16 operandC
```

Current interpretation:

```text
0x76 = presentation/fade/effect mode 1
0x77 = presentation/fade/effect mode 2
```

These are provisional names until the original rendering/effect routines are
fully traced.

---

# 37. `0x83` — subsystem operation

```text
operands:
    i16 operandA
    i16 operandB
```

A native subsystem operation is clearly invoked, but semantics remain
unresolved.

Keep the generic name.

---

# 38. `0x84` / `0x85` — cinematic letterbox

These are operand-less.

```text
0x84 BeginCinematicLetterbox
0x85 EndCinematicLetterbox
```

Known handlers:

```text
0x84 -> 0x00405A90 -> FUN_0041E1B0(1)
0x85 -> 0x00405AB0 -> FUN_0041E1B0(0)
```

## 38.1 Transition duration

Runtime uses:

```text
60 scenario units
```

At the 30 Hz scenario time base:

```text
2.0 seconds
```

## 38.2 Runtime geometry

Full-strength original bars:

```text
barHeight = screenHeight * 2 / 15
```

At 640x480:

```text
top    = 64
middle = 352
bottom = 64
```

Visible ratio:

```text
640 / 352 ~= 1.81818
```

OpenNomad intentionally targets a standard 1.85:1 presentation viewport; that is
a modernization, not original Runtime geometry.

## 38.3 Parser lesson

Early OpenNomad development incorrectly treated bytes following `0x84` as if
they might be operands.

Runtime proves:

```text
0x84 has zero operands
```

The enduring rule is:

```text
instruction size is opcode-specific
```

---

# 39. AREA wait model

AREA has typed native waits rather than one generic “yield”.

Current recovered classes:

```text
interface wait
SCX script wait
character script wait
camera wait
```

Known Runtime numeric states:

| Wait kind | Runtime state |
|---|---:|
| tracked character script (`0x3C`) | `4` |
| interface (`0x46`) | `6` |
| timed camera (`0x60`) | `7` |

Generic SCX-script wait state for `0x39` still needs a firm numeric Runtime
mapping.

The important architecture is:

```text
opcode requests native operation
    |
    v
concrete child/handle/timer stored
    |
    v
AREA context enters typed wait
    |
    v
matching completion condition
    |
    v
resume after original opcode
```

---

# 40. 30 Hz timing model

Both structured IAM action timing and the scenario/event VM repeatedly expose a
30 Hz logical time base.

OpenNomad currently converts:

```text
real seconds
    ->
30 Hz script frames
```

at the scheduler boundary.

Structured command durations such as `Wait` use script-frame units.

AREA camera waits and cinematic transitions likewise use scenario units
corresponding to:

```text
1 unit = 1 / 30 second
```

Runtime and OpenNomad renderer refresh rates are separate from this logical
script timing.

---

# 41. AREA event activation is explicit

Loading/constructing an AREA script context does not execute it immediately.

Current model:

```text
create context
queue event/state
activate context
run on scenario tick
```

This matters for startup ordering and prevents resource loading from becoming an
implicit script trigger.

---

# 42. START/global-variable relationship

The compact VM's global-variable instructions operate on the scenario/global
variable namespace associated with START/game-state data.

Relevant opcodes:

```text
0x0A read
0x0D set to 1
0x0E set explicit value
0x46 write interface completion result
```

The complete START data-file format belongs in future dedicated IAM-data
documentation.

---

# 43. Structured-script activation by ID, not name

AREA opcode `0x39` confirms that structured scripts have a proper numeric
activation path:

```text
AREA operand
    ->
script +0x1A ID lookup
    ->
Script_MakeInstance
```

Names remain useful for diagnostics and reverse engineering, but they are not
the sole runtime key.

Do not implement:

```cpp
if (script.name == "1KaylArrives")
    ...
```

as final architecture.

---

# 44. Character-bound structured script instances

`0x3B`/`0x3C` prove that a structured SCX script can be launched with external
context not stored directly in its value pool.

Current OpenNomad launch metadata preserves:

```text
character ID
third launch parameter
```

This is important for operations such as:

```text
SelectRelativeBodyAnimation
```

which need an explicitly owned character/model instance.

The launch context and serialized SCX template are distinct concepts.

---

# 45. Relative body-animation bridge example

Current recovered flow for the New Game intro:

```text
AREA bytecode
    |
    +-- 0x3B / 0x3C
            |
            v
character-bound SCX script instance
            |
            v
0x0200002A SelectRelativeBodyAnimation
            |
            +-- binding table A object name
            +-- animation index
            +-- previous/current progress
            +-- body animation vector
            +-- path index
            +-- subpath index
            +-- authored offset
            |
            v
3DA animation + 3DP path + character 3DO pose
```

This is one of the clearest concrete examples of how IAM AREA logic and
structured SCX scripts cooperate.

Detailed function semantics belong in `iam-script-functions.md`.

---

# 46. Validation rules for structured SCX scripts

A robust parser should validate:

1. script count fits the descriptor section;
2. all `0x64` template records fit;
3. shared-value count fits;
4. every command `valueCount` slice stays inside the shared pool;
5. every non-`-1` `nextLinkedIndex` is within the linked-command array;
6. command arrays fit their declared counts;
7. binding-table counts and names fit;
8. related-script optional name fits;
9. unknown fields are preserved;
10. malformed resource references are distinguished from malformed script
    serialization.

---

# 47. Validation rules for AREA bytecode

A safe interpreter should:

1. read exactly one opcode byte;
2. use opcode-specific operand widths;
3. reject truncated operands;
4. sign-extend signed immediate operands correctly;
5. bounds-check relative jump targets;
6. detect evaluation-stack underflow;
7. preserve unknown/unimplemented opcode offset and nearby bytes;
8. avoid consuming bytes after an unknown opcode using guessed length;
9. store concrete wait targets;
10. resume only on matching completion;
11. separate malformed bytecode from unsupported-but-well-formed operations.

---

# 48. Recommended structured-script runtime model

A faithful modern model should have three layers:

```text
ParsedScxScript
    immutable serialized definition

RuntimeScriptTemplate
    resolved references/resources

ScriptInstance
    mutable:
        value pool
        execution counts
        current root group
        linked command state
        launch context
        per-instance sprites
        elapsed time
```

Avoid mutating parsed serialized structures in place.

---

# 49. Recommended scheduler model

Ultimately OpenNomad should mirror Runtime approximately as:

```cpp
for each active ScriptInstance:
    root = currentRootCommand

    bool groupStillActive = false

    for command in root + linked chain:
        if finite(command.executionLimit) &&
           command.executionCount >= command.executionLimit:
            continue

        result = dispatch(command)

        if descriptor(command.functionId).contributesToGroupActive:
            groupStillActive |= result.runtimeActive

    if (!groupStillActive):
        ++currentRootCommandIndex

        if at end:
            if repeat allowed:
                reinitialize script/functions
            else:
                complete instance
```

The exact command-count mutation timing remains function-specific.

---

# 50. Recommended AREA VM model

```cpp
struct AreaContext {
    size_t ip;
    vector<int32_t> evaluationStack;
    map<uint16_t, int32_t> globalVariables;

    AreaState state;
    AreaWait wait;

    queue<uint16_t> events;
};
```

Native opcodes should emit typed requests rather than directly owning unrelated
engine subsystems:

```text
interface request
music request
SCX script launch
character-script launch
character activation
camera request
presentation request
letterbox request
```

This matches the current OpenNomad direction and keeps VM semantics isolated.

---

# 51. Useful Runtime locations

Structured Script system:

| Address | Role |
|---:|---|
| `0x00449750` | SCX loader |
| `0x00449881` area | `DEAD0002` script parser |
| `0x0044A0F0` | script lookup by 16-bit ID |
| `0x0044A7E0` | generic function reinit dispatcher |
| `0x0044C090` | `Script_GetNumParam` |
| `0x0044C680` | raw parameter accessor |
| `0x0044C860` | `Script_PlayScript` |

AREA VM examples:

| Address | Role |
|---:|---|
| `0x00403860` | opcode `0x46` interface path |
| `0x00404FB0` | opcode `0x67` music |
| `0x00405A90` | opcode `0x84` |
| `0x00405AB0` | opcode `0x85` |

Additional opcode-handler addresses should be added as they are individually
confirmed and named.

---

# 52. Useful OpenNomad source locations

Structured SCX parsing:

```text
src/core/Core/Omikron/SCX.hpp
src/core/Core/Omikron/SCX.cpp
```

Structured Script runtime:

```text
src/core/Core/Script/ScriptRuntime.hpp
src/core/Core/Script/ScriptRuntime.cpp
src/core/Core/Script/ScriptOpcode.hpp
```

AREA VM:

```text
src/core/Core/Script/AreaScriptRuntime.hpp
src/core/Core/Script/AreaScriptRuntime.cpp
src/core/Core/Script/AreaScriptOpcode.hpp
```

Scenario bridge/orchestration:

```text
src/core/Core/Scenario/ScenarioEngine.*
src/core/Core/Scenario/ScenarioManager.*
src/core/Core/Scenario/ScenarioRuntime.*
src/core/Core/Scenario/ScenarioStartupController.*
```

---

# 53. Highest-value remaining structured-script questions

1. exact serialized/runtime meanings of the remaining binding-table descriptor
   dwords;
2. binding table B semantics;
3. exact related-script authoring semantics;
4. all `+0x1E` flag bits;
5. exact context-byte meanings at `+0x5E..+0x61`;
6. precise instance-limit/accounting fields and ownership;
7. exact native command-count increment timing per IAM function;
8. alternate invocation/lifecycle path for IAM sprite functions not directly in
   the main `Script_PlayScript()` switch;
9. complete usage of `executionLimit == 0xFFFFFFFF`;
10. full retail SCX inventory of root/linked command patterns.

Function-ID-specific questions belong in `iam-script-functions.md`.

---

# 54. Highest-value remaining AREA VM questions

1. complete one-byte opcode table;
2. original source/editor names for provisional opcodes;
3. numeric Runtime wait state used by generic `0x39` SCX-script wait;
4. exact semantics of operands B/C for `0x39`;
5. exact meaning of the third `0x3B`/`0x3C` parameter;
6. `0x38` character lookup semantics;
7. `0x4F` semantics;
8. `0x5C` object activation semantics;
9. camera flags and callback completion path;
10. presentation effects `0x76`/`0x77`;
11. subsystem operations `0x68` and `0x83`;
12. full global-variable namespace and its relationship to `IAM/START`;
13. event descriptor/header structure around AREA bytecode;
14. calls/returns if any beyond relative jumps;
15. additional arithmetic/logical stack operations;
16. message/event dispatch opcodes;
17. exact end-of-stream versus `EndEvent` semantics.

---

# 55. Documentation boundaries going forward

The recommended split is:

```text
iam-script-functions.md
    what each 32-bit IAM function means

script-opcodes.md
    structured script serialization, instances, scheduling,
    and the AREA<->SCX bridge

scx.md
    complete SCX v5 container and resource manifest

iam-scenario-vm.md
    eventually: complete AREA bytecode opcode catalogue and VM ABI
```

Until `iam-scenario-vm.md` exists, this file remains the authoritative AREA VM
reference.

Once a dedicated VM document is added, this file should keep only:

- the high-level AREA VM architecture;
- the bridge to SCX structured scripts;
- references to the dedicated opcode catalogue.

---

# 56. Compact reference

```text
STRUCTURED SCX SCRIPT
=====================

script template: 0x64 bytes

+0x00 owner placeholder
+0x04 name[22]
+0x1A script ID
+0x1C runtime state
+0x1E flags
+0x20 root command count
+0x24 current root command index
+0x28 root-command placeholder/pointer
+0x2C linked command count
+0x30 linked-command placeholder/pointer
+0x34 repeat limit
+0x38 repeat index/count
+0x3C binding table A descriptor[3]
+0x48 binding table B descriptor[3]
+0x54 related-script placeholder/pointer
+0x58 elapsed script time (float at runtime)
+0x5C tail/context metadata

command: 0x18 bytes

+0x00 function ID
+0x04 value count
+0x08 first value index -> runtime value pointer
+0x0C linked index (-1 none) -> runtime SyncFunction pointer
+0x10 execution limit
+0x14 execution count

scheduler:
    command count/limit gate first
    execute eligible commands
    selected handler AL values OR into BL
    BL != 0 -> remain on root group
    BL == 0 -> advance group

DEAD0002 order:
    scriptCount
    templates[scriptCount]
    sharedValueCount
    sharedValues[]
    for each script:
        relatedPresent
        optional relatedName[21]
        rootCommands[]
        linkedCommands[]
        bindingTableA
        bindingTableB
```

```text
AREA VM
=======

u8 opcode
opcode-specific signed operands

state:
    IP
    event queue
    evaluation stack
    global variables
    typed wait

known wait states:
    4 character script
    6 interface
    7 camera

key bridge opcodes:
    0x39 generic SCX script + wait
    0x3B explicit-character script, fire-and-forget
    0x3C explicit-character script + tracked wait

interface:
    0x46 interfaceId, argument, resultVariable
    waits in state 6
    completion result written to global variable

timing:
    scenario/script logical rate = 30 Hz
```

---

# 57. Related documentation

[`iam-script-functions.md`](iam-script-functions.md)

- authoritative 32-bit IAM function catalogue;
- per-function behavior;
- handler/reinit addresses;
- semantic parameter-selector matrix;
- legacy/special IAM IDs.

[`3do.md`](3do.md)

- geometry, hierarchy, sprites, texture/material descriptors.

[`3dt.md`](3dt.md)

- indexed palette/pixel payload and texture decompression.

[`runtime-coordinate-math.md`](runtime-coordinate-math.md)

- Runtime-native units and transform conventions.

[`runtime-globals.md`](runtime-globals.md)

- global runtime state used by scenario/script systems.

[`startup-sequence.md`](startup-sequence.md)

- how startup reaches `IAM/START`, `IAM/AREA`, interface 29, and the main menu.

[`save-format.md`](save-format.md)

- persistent configuration and per-playthrough snapshot structures.

---

# 58. Current boundary of knowledge

The structured-script system is now understood at the level of:

```text
SCX descriptor
    |
    +-- DEAD0002
          |
          +-- 0x64 template
          +-- shared value pool
          +-- 0x18 root commands
          +-- 0x18 linked commands
          +-- binding tables
          +-- related script
          |
          +-- Script_MakeInstance
                  |
                  +-- mutable value copy
                  +-- mutable counts
                  +-- linked-pointer fixup
                  +-- per-instance resources
                  |
                  +-- Script_PlayScript
                         |
                         +-- execution-limit gate
                         +-- root + SyncFunction chain
                         +-- native Script_* dispatch
                         +-- OR selected AL returns into BL
                         +-- group advance / repeat / reinit
```

The AREA VM is now understood at the level of:

```text
AREA event bytecode
    |
    +-- stack/control flow
    +-- START/global variables
    +-- native interface/music/presentation operations
    +-- character/object/camera operations
    +-- SCX script launch opcodes
    +-- explicit typed waits
```

The major remaining work is no longer discovering whether these systems are
connected; it is completing their field/opcode semantics and reproducing the
remaining Runtime details exactly.
