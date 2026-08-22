# Omikron SCX scripting and scenario/event VM

> **Status:** work-in-progress reverse-engineering documentation for OpenNomad.  
> **Intended repository path:** `docs/reverse-engineering/script-opcodes.md`  
> **Last updated:** 2026-08-22
>
> This document describes the **serialization, runtime architecture, scheduling,
> and execution model** of the scripting systems currently identified in the
> Windows release of *Omikron: The Nomad Soul*.
>
> The detailed 32-bit IAM/Quantic C function catalogue is intentionally **not**
> duplicated here. [`iam-script-functions.md`](iam-script-functions.md) is the
> authoritative source for:
>
> - `0xCC0000NN` IAM function IDs;
> - recovered `Script_*` names;
> - native action/reinit handler addresses;
> - function-family classification;
> - blocking/group-active behavior per function;
> - `Script_GetNumParam()` selector-to-slot mappings;
> - legacy/special IAM function IDs;
> - detailed per-function semantics.
>
> This file remains authoritative for:
>
> - version-5 `.SCX` script serialization;
> - script-template and function-record layouts;
> - index/pointer relocation;
> - `Script_MakeInstance()` and mutable script instances;
> - `Script_PlayScript()` scheduling and synchronized groups;
> - SCX script resource tables;
> - the separate compact IAM scenario/event bytecode VM;
> - known one-byte scenario/event opcodes;
> - the still-unresolved bridge between the two scripting layers.

---

# 1. Source precedence and confidence

The sources used here are, in descending order of authority:

1. **`Runtime.exe` behavior** — authoritative for dispatch, data layout,
   pointer/index relocation, execution state, scheduling and error handling.
2. **Observed retail data**, principally `aventure.SCX`, `Grid.SCX`,
   `IAM/START`, and `IAM/AREA`.
3. **OpenNomad experiments and implementation traces**, used to validate
   interpretation but subordinate to Runtime when they disagree.
4. **Historical Quantic Dream material**, useful for original authoring intent
   and terminology but not a substitute for retail Runtime behavior.

The analyzed Runtime build is:

```text
File:             Runtime.exe
PE image base:    0x00400000
Linker timestamp: 1999-10-04 20:31:50
SHA-256:          55f7120bfea7891b048c64e3682f3259cdbf2719a43fa24e42254b753c95d2ef
```

Addresses in this document refer to that executable and are not expected to be
stable across other builds.

Confidence labels:

- **Confirmed — Runtime:** directly demonstrated by executable behavior.
- **Confirmed — data:** directly demonstrated by retail serialized data.
- **Corroborated:** Runtime and retail data agree.
- **Tentative:** structural behavior is known but higher-level intent is not.
- **Unknown:** the field/system exists but semantics remain unresolved.

---

# 2. Critical distinction: Omikron has at least two script execution systems

The term **script** is used for more than one execution model in Runtime.

At the current stage of reverse engineering, two systems must remain separate.

## 2.1 SCX `Script_*` function system

This is the structured scripting system associated with Runtime diagnostics such
as:

```text
Script_PlayScript()
Script_GetNumParam()
Script_MakeInstance()
Script_RemoveInstance()
Script_FunctionsIndexesToAdresses()
Script_FunctionsAdressesToIndexes()
```

Its executable actions are identified by typed 32-bit IAM function IDs of the
form:

```text
0xCC0000NN
```

Examples:

```text
0x01000002
0x03000008
0x04000029
0x06000017
```

The names and semantics of those IDs are maintained in
[`iam-script-functions.md`](iam-script-functions.md).

This system is built around:

- script templates;
- mutable script instances;
- fixed-size `0x18`-byte function records;
- a shared parameter-value pool;
- synchronized/linked function records;
- mutable progress/timing state;
- function-specific reinitialization;
- repeated script groups;
- references to paths, animations, sounds, sprites and scenes.

The serialized representation is **not a bytecode instruction stream**.

It is much closer to a serialized graph/table of high-level IAM actions.

## 2.2 Compact scenario/event bytecode VM

A separate byte-oriented VM is used by scenario/game-state data under the
`IAM` resource hierarchy, including `IAM/AREA` and `IAM/START`.

This system uses:

- one-byte opcodes;
- opcode-specific variable-length operands;
- an instruction pointer;
- event/script context state;
- scenario/event scheduling;
- handlers primarily in the `0x0040xxxx` range.

Known examples include:

```text
0x46  interface transition/opening path
0x67  play ADP music track
0x84  begin cinematic letterbox
0x85  end cinematic letterbox
```

This document refers to it as the **scenario/event VM**.

## 2.3 The namespaces must not be merged

These are not equivalent:

```text
SCX/IAM function ID:
    0x04000029

scenario/event opcode:
    0x46
```

They have different:

- serialized representations;
- dispatchers;
- runtime structures;
- operand models;
- scheduling behavior;
- lifecycle rules.

OpenNomad should therefore keep them as distinct interpreters/subsystems unless
future Runtime analysis proves a deeper shared implementation.

---

# 3. High-level architecture

A useful current model is:

```text
                         RETAIL DATA

        .SCX                                  IAM/AREA, IAM/START, ...
          |                                               |
          |                                               |
          v                                               v

  structured Script system                        scenario/event VM
  ------------------------                        -----------------
  0x64 script templates                          byte instruction stream
  0x18 function records                          u8 opcodes
  parameter pool                                 opcode-specific operands
  sync-function graph                            event/script context
          |                                               |
          |                                               |
          +--------------------+--------------------------+
                               |
                               v
                           Runtime.exe
                               |
             +-----------------+-----------------+
             |                                   |
             v                                   v
      Script_MakeInstance                  VM opcode handlers
      Script_PlayScript                          |
             |                                   |
             +-----------------+-----------------+
                               |
                               v
                     engine/game subsystems
```

The exact bridge by which the scenario/event layer starts, stops or waits for
SCX script instances is still being reconstructed.

---

# 4. SCX container context

The complete `.SCX` format contains much more than scripts, but script execution
depends on several tagged resource sections.

## 4.1 Version-5 file header

**Confirmed — Runtime and data.**

Observed version-5 files begin with:

| Offset | Type | Meaning | Confidence |
|---:|---|---|---|
| `0x00` | `u32` | magic `0x00DEAD00` | Confirmed |
| `0x04` | `u32` | version, observed/required as `5` | Confirmed |
| `0x08` | `u32` | unknown; observed as `8` in `aventure.SCX` and `Grid.SCX` | Unknown |
| `0x0C` | `u32` | size of the main tagged block | Confirmed |

The main tagged block begins at:

```text
file + 0x10
```

Runtime reads exactly the number of bytes stored at `+0x0C`.

Observed:

```text
Grid.SCX:
    mainBlockSize = 0x1019
    main block = file 0x10 .. 0x1028 inclusive
    next byte  = 0x1029

aventure.SCX:
    mainBlockSize = 0x41C8
    main block = file 0x10 .. 0x41D7 inclusive
    next byte  = 0x41D8
```

Therefore:

```text
mainBlockEnd = 0x10 + mainBlockSize
```

is a confirmed boundary.

## 4.2 Main-block tags

The main block is parsed as a sequence of 32-bit tags:

```text
0xDEAD0000 .. 0xDEAD000A
0xDEADFFFF
```

The parser dispatches through logic around:

```text
0x0044A040
```

Current section meanings:

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
| `0xDEAD0008` | fixed global/limit setup, observed value `0x100` | Unknown |
| `0xDEAD0009` | reserved/no-op in current parser | Confirmed behavior |
| `0xDEAD000A` | auxiliary/external block passed to another loader | Unknown |
| `0xDEADFFFF` | end of main tagged block | Confirmed |

The Script subsystem is rooted primarily in:

```text
0xDEAD0002
```

but function parameters refer to resources supplied by several other sections.

## 4.3 Sections are optional and not numerically ordered

Observed files prove that tag order cannot be assumed.

A parser must:

1. read a tag;
2. dispatch by tag value;
3. advance according to that section's format;
4. stop on `0xDEADFFFF`.

Do not assume that tag `0` precedes tag `1`, or that every section exists.

---

# 5. Tag `0xDEAD0002`: script-definition section

## 5.1 Section beginning

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
    8 scripts

aventure.SCX:
    22 scripts
```

Observed names include:

```text
Grid.SCX:
    "1KaylArrives"

aventure.SCX:
    "effects2_smoke2"
```

These names are debug/authoring labels for structured script templates.

Their presence does **not** imply automatic execution when the SCX is loaded.

## 5.2 Shared function-parameter value pool

After the script template table, tag 2 contains a pool of 32-bit values.

The loader stores approximately:

```text
ScriptList + 0x44 = valueCount
ScriptList + 0x48 = pointerToValues
```

The pool is conceptually:

```text
u32 functionParameterValueCount
u32 functionParameterValues[functionParameterValueCount]
```

Function records do not contain native pointers on disk.

Their serialized `+0x08` field indexes this parameter storage.

At runtime:

```text
serialized parameter index
        |
        v
scriptList->parameterValues + index * 4
        |
        v
native parameter pointer
```

## 5.3 Two function-record arrays

Each script template refers to two classes of `0x18`-byte function records:

1. a primary array used to select the current function/group;
2. a secondary array referenced through serialized `SyncFunction` indexes.

The exact original editor/source terminology for these arrays is not fully
recovered.

Runtime explicitly uses the diagnostic term:

```text
SyncFunction
```

for the linked relationship.

At runtime, `+0x0C` becomes a pointer and is traversed as a function chain.

---

# 6. Script template record

Each serialized script-template record is:

```text
0x64 bytes
```

A conservative partial schema is:

```c
struct SerializedScriptTemplateV5 {
    uint32_t owner_placeholder;       // +0x00
    char     name[20];                // +0x04

    uint16_t unknown_18;              // +0x18
    uint16_t script_id;               // +0x1A

    uint16_t runtime_state;           // +0x1C
    uint16_t flags_state;             // +0x1E

    uint32_t function_group_count;    // +0x20
    uint32_t current_group_index;     // +0x24
    uint32_t function_groups;         // +0x28 serialized/runtime-dependent

    uint32_t sync_function_count;     // +0x2C
    uint32_t sync_functions;          // +0x30 serialized/runtime-dependent

    int32_t  repeat_limit;            // +0x34
    uint32_t repeat_index;            // +0x38

    uint8_t  unknown_3C[0x18];        // +0x3C .. +0x53

    uint32_t paired_or_linked_script; // +0x54 runtime pointer
    float    elapsed_time;            // +0x58

    uint8_t  unknown_5C;              // +0x5C
    uint8_t  paired_gate;             // +0x5D
    uint8_t  context_flags[4];        // +0x5E .. +0x61
    uint16_t unknown_62;              // +0x62
}; // 0x64
```

This is documentation-oriented, not a final OpenNomad ABI.

Several fields are mutated, relocated or repurposed after load, so serialized
and runtime structures should be separate.

## 6.1 `+0x00`: owner/list placeholder

The first dword is pointer-shaped but is not a meaningful serialized process
address.

Runtime fixes/overwrites it so a loaded script can refer to its owning
`ScriptList`.

## 6.2 `+0x04`: script name

**Confirmed.**

```text
char name[20]
```

contains a fixed-size script name.

Observed shorter names are NUL-terminated.

## 6.3 `+0x1A`: script ID

**Confirmed — Runtime.**

A lookup function around:

```text
0x0044A0F0
```

compares a requested 16-bit ID with:

```text
WORD [script + 0x1A]
```

This is distinct from the textual name.

## 6.4 `+0x1C`: runtime state

`Script_PlayScript()` checks this field before normal execution.

A zero state causes an early/non-normal return.

Reinitialization restores an active state.

The exact enum remains unknown.

## 6.5 `+0x1E`: flags/state

The low nibble is manipulated by execution logic.

Upper bits are involved in broader context/lifecycle checks.

Do not assign final bit names yet.

## 6.6 `+0x20`, `+0x24`, `+0x28`: primary function groups

Runtime uses:

```text
+0x20  number of primary groups
+0x24  current group index
+0x28  pointer to primary function-record array
```

Conceptually:

```cpp
function =
    script->functionGroups
    + script->currentGroupIndex;
```

with each record having a `0x18`-byte stride.

## 6.7 `+0x2C`, `+0x30`: sync-function table

These fields contain:

```text
+0x2C  count of secondary/sync function records
+0x30  pointer to secondary/sync function array
```

Serialized references into this array are converted to native pointers.

## 6.8 `+0x34`, `+0x38`: repetition

Runtime compares the current repeat state with a configured repeat limit.

A repeat limit of:

```text
0xFFFFFFFF / -1
```

is treated specially as an unbounded/infinite repetition case.

When the script reaches the end and should repeat, Runtime reinitializes script
and function state rather than simply rewinding an instruction pointer.

## 6.9 `+0x54`: paired/linked script

This field becomes or behaves as a pointer to another script.

Its authoring meaning is not yet fully established.

Open questions include:

- whether the relation is parent/child;
- sequence vs parallel behavior;
- completion propagation;
- ownership;
- cancellation;
- relation to `+0x5D`.

## 6.10 `+0x58`: elapsed time

This is used as a floating-point time accumulator.

It is separate from function-local `+0x10` / `+0x14` state.

## 6.11 `+0x5E .. +0x61`: context gating

These bytes participate in execution gating involving global context state
around:

```text
0x00903AE0
```

Their exact meanings remain unresolved.

They may encode scene/area ownership or cancellation context.

---

# 7. Function record serialization

Each structured SCX/IAM function record has a stride of:

```text
0x18 bytes
```

A conservative runtime-shaped schema is:

```c
struct ScriptFunctionRecord32 {
    uint32_t function_id;       // +0x00
    uint32_t unknown_04;        // +0x04
    uint32_t parameters;        // +0x08 index on disk, pointer at runtime
    uint32_t sync_function;     // +0x0C index on disk, pointer at runtime
    uint32_t state_10;          // +0x10
    uint32_t state_14;          // +0x14
}; // 0x18
```

The detailed function-ID catalogue for `+0x00` lives in
[`iam-script-functions.md`](iam-script-functions.md).

## 7.1 `+0x00`: typed IAM function ID

This is a 32-bit ID such as:

```text
0x01000002
0x03000008
0x04000029
```

Do not:

- call it a one-byte opcode;
- dispatch using only the low byte;
- conflate it with the scenario/event VM.

## 7.2 `+0x04`: unresolved function metadata

No universal meaning has been proven.

It should remain an unknown/raw field.

Asset analysis should collect value distributions per function ID.

## 7.3 `+0x08`: function parameters

Serialized form:

```text
index into parameter storage
```

Runtime form:

```text
pointer to parameter values
```

Runtime diagnostics refer to these as:

```text
FuncParams
```

## 7.4 `+0x0C`: synchronized/linked function

Serialized form:

```text
index into sync-function array
```

Runtime form:

```text
pointer to another function record
```

Runtime diagnostics explicitly use:

```text
SyncFunction
```

## 7.5 `+0x10` and `+0x14`: mutable function execution state

These values broadly behave like:

```text
configured timing/frame/limit state
current mutable progress
```

but exact generic names are not proven.

They are function-instance state and must not be shared across independent
instances.

---

# 8. Index-to-pointer relocation

Runtime preserves two very useful diagnostic names:

```text
Script_FunctionsIndexesToAdresses()
Script_FunctionsAdressesToIndexes()
```

The misspelling `Adresses` is present in Runtime.

These routines do **not** map IAM IDs to native C function addresses.

They relocate references inside the serialized script graph.

## 8.1 Load/runtime direction

Conceptually:

```text
serialized:
    +0x08 = parameter index
    +0x0C = sync-function index

        Script_FunctionsIndexesToAdresses()

runtime:
    +0x08 = parameter pointer
    +0x0C = sync-function pointer
```

## 8.2 Reverse direction

Runtime also contains the inverse validation/conversion path.

Representative diagnostics include:

```text
Script_FunctionsAdressesToIndexes(): Index of FuncParams too big: %d/%d.
Script_FunctionsAdressesToIndexes(): Index of SyncFunction too big : %d/%d.
Script_FunctionsAdressesToIndexes(): Ptr is NULL.

Script_FunctionsIndexesToAdresses(): Address of FuncParams isn't valid.
Script_FunctionsIndexesToAdresses(): Address of SyncFunction isn't valid.
```

The inverse path is useful evidence even if retail gameplay does not serialize
the same structure back to disk.

## 8.3 Reimplementation rule

Never read the serialized `+0x08` or `+0x0C` dwords as native pointers.

Use explicit validated index types in the parser and explicit references in the
runtime representation.

---

# 9. Function parameters

The parameter pool contains 32-bit values and should not be globally typed as
one C type.

Depending on the function, a value may represent:

- integer;
- signed integer;
- float bit pattern;
- resource index;
- object/script ID;
- frame count;
- duration;
- flag;
- scale;
- another encoded value.

Each function handler owns the final interpretation of its raw positional
parameters.

## 9.1 Semantic parameter selectors

Runtime contains:

```text
Script_GetNumParam()
```

which maps a **semantic parameter selector** to a positional slot for a given
function ID.

This proves that Runtime has a higher-level parameter ABI layered on top of the
raw positional values.

The full selector-to-slot matrix and current selector analysis are maintained in
[`iam-script-functions.md`](iam-script-functions.md).

This file deliberately does not duplicate that table.

## 9.2 Important parser rule

`Script_GetNumParam()` does not enumerate every raw positional field.

Therefore:

```text
number of semantic selectors
```

is not necessarily:

```text
raw parameter count
```

A parser should preserve the complete raw parameter vector.

---

# 10. Script templates vs mutable instances

Runtime has explicit instance-management functions, including:

```text
Script_MakeInstance()
Script_RemoveInstance()
Script_RemoveAllInstances()
```

This proves that a loaded SCX template and an executing script instance are
different objects/concepts.

## 10.1 `Script_MakeInstance()` validation

Representative diagnostics include:

```text
Script_MakeInstance(): Your ScriptListPtr is NULL.
Script_MakeInstance(): Your ScriptPtr is NULL.
Script_MakeInstance(): Your ScriptPtr isn't a valid script. It's not from your ScriptListPtr..Bad adress.
Script_MakeInstance(): There are %d instances in script. Max number is %d...Can't add instance.
```

Runtime verifies that the template belongs to the supplied script list and
enforces an instance count/limit.

The exact template fields for this accounting are not yet fully mapped.

## 10.2 Instance-local function arrays

Runtime can allocate/copy:

```text
script functions
sync functions
```

into instance-local storage.

Therefore mutable fields such as:

```text
+0x10
+0x14
```

must belong to the playing instance, not the immutable template.

## 10.3 Instance-local parameters

Diagnostics show allocation/copying for:

```text
parameter list
functions parameters
syncfunctions parameters
```

This is strong evidence that parameters can be mutable or otherwise
instance-specific.

A fully faithful implementation should not expose all parameters as immutable
shared spans.

## 10.4 Sync links are rebuilt per instance

If a template contains:

```text
function A -> sync function B
```

then an instance must become:

```text
instance A -> instance B
```

not:

```text
instance A -> template B
```

Runtime validates and fixes these references.

## 10.5 Instance-local sprite state

`Script_MakeInstance()` contains sprite-specific allocation failures such as:

```text
Script_MakeInstance(): Not enough memory to allocate space for new sprite.
Script_MakeInstance(): Sprite "%s" isn't loaded.
Script_MakeInstance(): Sprite "%s" can't be allocated.
```

This shows that at least some structured IAM functions require per-instance
sprite objects/state.

---

# 11. `Script_PlayScript()` scheduler

Main playback is around:

```text
0x0044C860
```

The high-level scheduler model is now sufficiently established to implement
without hardcoding script names.

## 11.1 Current group selection

Runtime uses approximately:

```text
script + 0x20  function-group count
script + 0x24  current group index
script + 0x28  primary function array
```

It selects the current primary function record and follows synchronized links.

## 11.2 Synchronized function chain

One selected primary record can lead to additional records through:

```text
function + 0x0C
```

after that field has been relocated to a pointer.

Conceptually:

```text
group N
  |
  +-- primary function
        |
        +-- sync function
              |
              +-- sync function
                    ...
```

The original IAM/editor term may not literally have been “group”; this is a
behavior-based documentation term.

## 11.3 Group-still-active accumulator

Some IAM functions return a value that is ORed into a group-level accumulator.

Conceptually:

```cpp
bool stillActive = false;

for (Function& fn : currentGroup) {
    Result result = execute(fn);

    if (descriptor(fn.id).contributesToGroupActive)
        stillActive |= result.active;
}

if (!stillActive)
    advanceToNextGroup();
```

This return value is **not generic success/failure**.

For contributing functions, it means approximately:

```text
this synchronized operation is still in progress
```

The authoritative per-function blocking metadata is in
[`iam-script-functions.md`](iam-script-functions.md).

## 11.4 Immediate functions inside synchronized groups

A group can contain operations that perform a side effect without blocking
progress.

For example, conceptually:

```text
timed movement
sound start
wait
```

can coexist in one group even if only movement and wait contribute to
`stillActive`.

## 11.5 Sync does not imply equal duration

Functions in a synchronized group can have independent:

- durations;
- progress counters;
- resource state;
- parameters;
- completion conditions.

They are synchronized only in the sense that group advancement waits until no
contributing member remains active.

---

# 12. Reinitialization and repetition

Runtime contains a generic script-function reinit dispatcher around:

```text
0x0044A7E0
```

Many IAM functions have paired `Script_Reinit_*` handlers.

The authoritative pairing table lives in
[`iam-script-functions.md`](iam-script-functions.md).

## 12.1 Why reinit exists

Reinitialization is used when script execution needs to restore function-local
state for:

- repetition;
- restart;
- replay;
- possibly instance reuse;
- reset after partial progress.

This is not equivalent to merely resetting:

```text
currentGroupIndex = 0
```

because individual functions can maintain resource-specific mutable state.

## 12.2 Repeat behavior

The template fields:

```text
+0x34  repeat limit
+0x38  repeat index/current repeat state
```

participate in end-of-script repetition logic.

A configured limit of `-1`/`0xFFFFFFFF` is treated specially as unbounded.

OpenNomad should reproduce the state reset order instead of approximating loops
at a higher level.

---

# 13. Error handling and validation

The structured Script subsystem performs substantial validation.

## 13.1 Invalid parameter indexes

Runtime checks that parameter indexes fall inside the owning parameter pool.

## 13.2 Invalid sync indexes

Serialized sync-function indexes are checked against the owning script's
sync-function count.

## 13.3 Invalid relocated addresses

The inverse pointer-to-index path validates that pointers belong to the expected
arrays before converting them back to indexes.

## 13.4 Invalid script ownership

`Script_MakeInstance()` checks that the supplied template comes from the
supplied `ScriptList`.

## 13.5 Instance limit

Runtime refuses to create another instance when the template's configured
maximum is reached.

## 13.6 Missing resources

Individual IAM functions can report missing:

- objects;
- cameras;
- animations;
- paths;
- scenes;
- sprites;
- palettes;
- sounds.

A faithful implementation should distinguish:

```text
malformed serialized script
```

from:

```text
well-formed script referring to unavailable runtime resource
```

because Runtime validates these at different layers.

---

# 14. Script resources in other SCX sections

Structured script functions depend on the SCX resource tables.

## 14.1 Tag `0xDEAD0000`: path resources

Current observed record stride:

```text
0x20 bytes
```

Observed `Grid.SCX` path:

```text
Grid_pb.3dp
```

This section supports high-level path-based IAM operations.

## 14.2 Tag `0xDEAD0001`: animation resources

Current observed record stride:

```text
0x24 bytes
```

Observed `Grid.SCX` entries include:

```text
INTRO1.3DA
INTRO2.3DA
INTRO3.3DA
```

The loader resolves them through animation-related code around:

```text
0x0046E880
```

and stores runtime references.

## 14.3 Tag `0xDEAD0003`: sound resources

Current observed record stride:

```text
0x1A bytes
```

Observed entry:

```text
INTRO01.WAV
```

Runtime resolves sound IDs through code around:

```text
0x0049FC80
```

and stores `0xFFFF` when lookup fails.

## 14.4 Tag `0xDEAD0004`: visual/sprite resources

Current observed serialized record stride:

```text
0x24 bytes
```

Observed entries include effect models such as:

```text
EFFECTS2_SMOKE1.3DO
```

The loader:

- reads outer-SCX metadata;
- loads the associated/embedded 3DO resource;
- stores a runtime resource/model pointer;
- may allocate sprite state depending on record metadata.

## 14.5 Tag `0xDEAD0005`: scenes

Current observed record stride:

```text
0x1C bytes
```

Runtime diagnostics include:

```text
Scene "%s" not found !
```

`Grid.SCX` and `aventure.SCX` currently have zero records in this section, so
additional retail files are needed to characterize it.

---

# 15. Loading does not imply execution

Loading an SCX establishes:

- resource tables;
- script templates;
- parameter pools;
- relocated references;
- runtime registries.

It does **not** mean:

```text
execute every script in the file
```

## 15.1 Templates are not a startup queue

For example, a template named:

```text
1KaylArrives
```

being present in `Grid.SCX` does not prove that it should execute when the main
menu is entered.

Activation is a separate game/scenario decision.

## 15.2 Do not hardcode by script name

This is architecturally wrong as a final implementation:

```cpp
if (script.name == "1KaylArrives")
    playKaylArrival();
```

Names are useful for diagnostics and lookup, but Runtime has:

- numeric script IDs;
- function graphs;
- instances;
- scheduler state;
- resource relationships.

OpenNomad should preserve the data-driven model.

---

# 16. Scenario/event VM overview

The compact scenario/event VM is structurally separate from SCX `Script_*`
functions.

It is less completely mapped but already has several confirmed operations.

## 16.1 Encoding

An instruction begins with:

```text
u8 opcode
```

followed by an opcode-specific sequence.

Operands can include signed 16-bit values and a variable/reference encoding.

The VM therefore behaves like:

```text
[ip]      opcode
[ip + 1]  operands...
```

with instruction size depending on the opcode.

## 16.2 Runtime context

The VM has:

- an instruction pointer;
- event/script context;
- operand-decoding helpers;
- scheduling/yield behavior;
- handlers primarily around `0x0040xxxx`.

This is unlike the SCX Script system, where a current `0x18`-byte function
record is selected and executed from a graph.

## 16.3 Variable/reference operand encoding

Some handlers read signed 16-bit operand values through helpers that can resolve
either:

- literal values;
- encoded references/variables.

The complete encoding scheme has not yet been documented.

A correct VM implementation should preserve the operand reader abstraction
rather than assuming every `s16` is literal.

---

# 17. Known scenario/event opcode `0x46`

## 17.1 Startup instruction

In `IAM/AREA` record `118`, a known startup instruction is:

```text
46 1D 00 FF FF 13 00
```

Current decoding:

```text
opcode = 0x46

operand 0 = 0x001D = 29
operand 1 = 0xFFFF = -1
operand 2 = 0x0013 = 19
```

The script byte sequence is located around:

```text
areaRecord + 0x422
```

in the currently examined record layout.

## 17.2 Handler

Known handler:

```text
0x00403860
```

It reads three signed 16-bit operands through the VM operand mechanism.

For this specific instruction the values are literal:

```text
29, -1, 19
```

## 17.3 Interface path

The call flow participates in:

```text
0x00403860
    ->
0x0041DEF0
    ->
0x0041DF30
    ->
0x00429BB0
    ->
interface 29
    ->
0x00479D10
```

Interface `29` is the main menu.

## 17.4 Scheduling implication

The instruction does not behave as a trivial fire-and-forget call in the
startup sequence.

The surrounding script behavior proves that opening interface 29 suspends or
otherwise gates progression: later instructions do not immediately overwrite
main-menu state.

The exact generic yield/completion contract for opcode `0x46` remains to be
mapped.

---

# 18. Known scenario/event opcode `0x67`: music

Known handler:

```text
FUN_00404FB0
```

Observed startup bytes:

```text
67 6D 00 01 00 01 00
```

Decode:

```text
opcode    = 0x67
operand 0 = 0x006D = 109
operand 1 = 0x0001 = 1
operand 2 = 0x0001 = 1
```

Current semantics:

| Operand | Type | Meaning |
|---:|---|---|
| 0 | `s16` | numeric ADP track ID, resolved as `TRACKS/%d.ADP` |
| 1 | `s16` | looping flag; nonzero = loop |
| 2 | `s16` | unresolved mode/state flag |

Runtime does not restart a track when the requested numeric ID already matches
the active track.

Music playback can also be suppressed by global/runtime state.

A branch on a zero third operand remains unresolved.

## 18.1 Startup evidence for interface yielding

The startup script later requests another track (`87`).

If the interface-29 instruction simply returned and execution continued
immediately, track 87 would replace track 109 almost at once.

That does not happen in the original menu sequence.

This is independent evidence that scenario/event instructions can suspend or
gate execution on higher-level state.

## 18.2 QD ADP container note

The observed music files use a compact Quantic Dream ADP container:

```text
offset  size  meaning
0x00    3     compressed payload size, little-endian 24-bit
0x03    1     stereo flag (0 = mono, 1 = stereo)
0x04    12    zero/reserved
0x10    ...   compressed QD IMA payload
```

Stream properties:

```text
sample rate = 22050 Hz
channels    = stereoFlag ? 2 : 1
frames      = payloadSize * 2 / channels
```

Looping is not encoded as a loop-point structure in the ADP file; for this VM
path it is controlled by opcode `0x67`.

The codec implementation belongs to OpenNomad's audio layer rather than the VM
itself.

---

# 19. Known scenario/event opcodes `0x84` and `0x85`: cinematic mask

These are operand-less presentation commands.

```text
0x84  begin cinematic letterbox
      handler 0x00405A90
          -> FUN_0041E1B0(1)

0x85  end cinematic letterbox
      handler 0x00405AB0
          -> FUN_0041E1B0(0)
```

## 19.1 Runtime transition behavior

Runtime keeps explicit entering/leaving cinematic-mask state.

The transition timer runs for:

```text
60 scenario units
```

Scenario/presentation timing is 30 Hz, giving:

```text
2.0 seconds
```

The opcodes themselves do not wait for the visual transition.

They trigger state and script execution continues according to normal VM
scheduling.

## 19.2 Original Runtime target geometry

The original full-strength target is:

```text
top bar height    = screenHeight * 2 / 15
bottom bar height = screenHeight * 2 / 15
```

At `640x480`:

```text
top    = 64
image  = 352
bottom = 64
```

Visible aspect ratio:

```text
640 / 352 ~= 1.81818
```

## 19.3 OpenNomad modernization

OpenNomad intentionally targets a standard `1.85:1` cinematic viewport rather
than reproducing Runtime's exact `2/15` ratio.

For viewport width `W` and height `H`:

```text
fullBarHeight = max(0, (H - W / 1.85) / 2)
```

Current rendered bar height is:

```text
fullBarHeight * transitionAmount
```

This is an intentional presentation modernization, not historical Runtime
behavior.

Runtime confirms:

- enabled/disabled endpoints;
- transition direction;
- duration.

The exact original easing law remains unresolved.

---

# 20. Known unsupported startup opcode `0x84` context

During early OpenNomad intro work, the area script paused at:

```text
opcode = 0x84
offset = +0x34
bytes  = [84 07 00 0a 13 00 19 06]
```

Reverse engineering established that `0x84` itself has **no operands**.

The following bytes belong to subsequent instructions/data and must not be
consumed as `0x84` operands.

This is a useful parser lesson:

```text
unsupported opcode
```

must not imply:

```text
consume an arbitrary fixed number of following bytes
```

Instruction length is opcode-specific.

---

# 21. Probable relationship between the two scripting layers

The current evidence supports this working architecture:

```text
scenario/event VM
    |
    +-- area/game events
    +-- interface changes
    +-- music/presentation state
    +-- high-level scenario transitions
    |
    |  exact bridge still under investigation
    v
SCX structured Script system
    |
    +-- high-level IAM action sequences
    +-- synchronized functions
    +-- animation/camera/path/effect/sound actions
    +-- mutable script instances
```

Historical IAM evidence makes it plausible that both are products of the same
authoring ecosystem, but Runtime executes them differently.

## 21.1 Bridge not yet recovered

The highest-value missing call chain is:

```text
scenario/event instruction
    ->
lookup SCX script template
    ->
Script_MakeInstance
    ->
activate instance
    ->
per-frame Script_PlayScript
    ->
completion / message / callback
    ->
scenario/event VM resumes or reacts
```

Every edge in this chain should be proven from Runtime before assigning final
function names.

---

# 22. SCX parser branch map

Current main-block parser locations:

| Section | Parser branch / area | Runtime result relevant to scripts |
|---|---|---|
| tag 0 | around `0x00449AA0` | path-resource records |
| tag 1 | around `0x00449B1F` | animation-resource records |
| tag 2 | around `0x00449881` | script templates, parameters, function arrays |
| tag 3 | around `0x00449BA0` | sound resources |
| tag 4 | around `0x00449C15` | 3DO/sprite visual resources |
| tag 5 | around `0x00449D00` | scene resources |
| tag 6 | around `0x00449D7D` | unknown |
| tag 7 | around `0x00449DAC` | unknown |
| tag 8 | around `0x00449DD8` | unknown global/fixed-limit setup |
| tag 9 | around `0x00449E3C` | no-op/reserved |
| tag 10 | around `0x00449DE4` | auxiliary/external block |

These are parser branch locations and are not necessarily clean original source
function boundaries.

---

# 23. Observed tag order

## 23.1 `Grid.SCX`

Known offsets:

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

Main-block end:

```text
0x1029
```

which matches:

```text
0x10 + 0x1019
```

## 23.2 `aventure.SCX`

Known offsets:

```text
0x0010  0xDEAD0002  scripts
0x1E14  0xDEAD0004  sprites/3DO visuals
0x41BC  0xDEAD0005  scenes, count 0
0x41C4  0xDEAD0006  unknown, count 0
0x41CC  0xDEAD0007  unknown, count 0
0x41D4  0xDEADFFFF
```

Main-block end:

```text
0x41D8
```

matching:

```text
0x10 + 0x41C8
```

These files demonstrate that section presence and ordering are data-dependent.

---

# 24. Documentation-oriented serialized structures

These are intentionally conservative.

## 24.1 SCX header

```c
struct ScxHeaderV5 {
    uint32_t magic;          // +0x00 = 0x00DEAD00
    uint32_t version;        // +0x04 = 5
    uint32_t unknown_08;     // +0x08
    uint32_t mainBlockSize;  // +0x0C
};
```

## 24.2 Script function record

```c
struct SerializedScriptFunction {
    uint32_t functionId;      // +0x00
    uint32_t unknown04;       // +0x04
    uint32_t parameterIndex;  // +0x08
    uint32_t syncIndex;       // +0x0C
    uint32_t state10;         // +0x10
    uint32_t state14;         // +0x14
}; // 0x18
```

Runtime form should use proper pointer/reference types rather than preserving
the serialized dwords.

## 24.3 Script template

Use the `0x64` schema in section 6 as the current best documentation model.

OpenNomad should define separate:

```text
SerializedScriptTemplateV5
RuntimeScriptTemplate
ScriptInstance
```

rather than one structure that changes meaning after relocation.

---

# 25. Important Runtime entry points

| Address | Current role | Confidence |
|---:|---|---|
| `0x00449750` | SCX loader | Confirmed |
| `0x0044A0F0` | script lookup by 16-bit ID | Confirmed behavior |
| `0x0044A7E0` | generic structured-script reinit dispatcher | Confirmed behavior |
| `0x0044C090` | `Script_GetNumParam` | Diagnostic + behavior |
| `0x0044C680` | raw 32-bit parameter accessor | Strongly corroborated |
| `0x0044C860` | `Script_PlayScript` | Diagnostic + behavior |
| `0x00403860` | scenario/event opcode `0x46` | Confirmed |
| `0x00404FB0` | scenario/event opcode `0x67` music handler | Confirmed |
| `0x00405A90` | scenario/event opcode `0x84` | Confirmed |
| `0x00405AB0` | scenario/event opcode `0x85` | Confirmed |

Per-IAM-function native handler addresses are intentionally maintained in
[`iam-script-functions.md`](iam-script-functions.md), not here.

---

# 26. Recommended reverse-engineering breakpoints

## 26.1 SCX loading

```text
0x00449750  SCX loader
0x00449881  tag-2 script parser
0x00449AA0  path-resource parser
0x00449B1F  animation-resource parser
0x00449BA0  sound-resource parser
0x00449C15  visual/sprite-resource parser
```

Log:

```text
script count
template addresses
template names and IDs
parameter pool base/count
primary function counts
sync-function counts
serialized function IDs
serialized +0x04 values
serialized parameter indexes
serialized sync indexes
runtime pointers after relocation
```

## 26.2 Script lookup and instances

Break on:

```text
0x0044A0F0
```

and functions identified by diagnostics:

```text
Script_MakeInstance()
Script_RemoveInstance()
Script_RemoveAllInstances()
```

Watch:

```text
template -> instance copies
function-array allocation
parameter copies
sync-link fixup
sprite allocation
instance count/limits
```

## 26.3 Main playback

```text
0x0044C860  Script_PlayScript
```

Watch per call:

```text
script + 0x1C  runtime state
script + 0x1E  flags
script + 0x20  group count
script + 0x24  current group index
script + 0x34  repeat limit
script + 0x38  repeat state/index
script + 0x54  paired script
script + 0x58  elapsed time
```

For each function:

```text
+0x00 function ID
+0x04 unknown metadata
+0x08 parameter pointer
+0x0C sync pointer
+0x10 state/limit
+0x14 progress/state
```

Use `iam-script-functions.md` to interpret the function ID.

## 26.4 Reinitialization

```text
0x0044A7E0
```

Record before/after state for every function ID seen in retail assets.

## 26.5 Scenario/event VM

Known useful breakpoints:

```text
0x00403860  opcode 0x46
0x00404FB0  opcode 0x67
0x00405A90  opcode 0x84
0x00405AB0  opcode 0x85
```

For startup validation:

```text
46 1D 00 FF FF 13 00
```

and trace through:

```text
0x0041DEF0
0x0041DF30
0x00429BB0
0x00479D10
```

---

# 27. Recommended automated asset analysis

A small offline SCX inspection tool remains one of the highest-value RE aids.

It should report:

```text
SCX header
main-block size
section-tag sequence

script template:
    name
    ID
    group count
    sync-function count
    repeat fields
    flags/context bytes

function record:
    function ID
    symbolic name from iam-script-functions map
    raw +0x04
    parameter index
    sync index
    +0x10
    +0x14

parameter-pool values
resource-table names
```

Useful aggregate reports:

```text
distinct function IDs across every SCX
function-ID frequency
script-name/function-ID matrix
raw +0x04 distribution per function ID
+0x10/+0x14 distributions
sync-chain lengths
which functions commonly synchronize together

resource usage per script
SCX files containing scenes/path/animation tables

unknown or legacy function IDs
```

Function-ID semantic analysis belongs in the IAM-function documentation/tooling,
but the inventory itself belongs with the SCX parser.

---

# 28. Highest-value open questions

## 28.1 Exact byte-for-byte tag-2 grammar

Known:

- script count;
- `0x64` template stride;
- shared 4-byte parameter values;
- `0x18` function-record stride;
- primary and sync-function arrays;
- index-to-pointer relocation.

Still needed:

- complete ordering of all arrays/subtables after the template table;
- all counts and sentinels;
- exact ownership of each pool;
- proof across diverse retail SCX files.

## 28.2 Remaining `0x64` template fields

Important unresolved regions:

```text
+0x18
+0x3C .. +0x53
+0x5C .. +0x63
```

Need to recover:

- instance limit/count fields;
- paired-script metadata;
- context ownership;
- exact flag bits.

## 28.3 Generic meaning of function `+0x04`

Collect per-ID value distributions before assigning semantics.

## 28.4 Exact generic meaning of `+0x10` / `+0x14`

These are mutable execution state, but may be interpreted differently per IAM
function.

## 28.5 Paired/chained scripts

Recover:

- creation;
- ownership;
- scheduling;
- completion propagation;
- `+0x54` and `+0x5D` semantics.

## 28.6 Context flags

Map `+0x5E..+0x61` and the associated globals.

## 28.7 Instance accounting

Find exact max/current instance fields and list ownership.

## 28.8 Unknown SCX tags `6`, `7`, `8`, `10`

Determine whether any contain script-adjacent registries/state.

## 28.9 Full scenario/event opcode table

Reconstruct:

- complete dispatch table;
- operand lengths;
- operand kinds;
- variable/reference encoding;
- jumps and conditions;
- calls;
- messages;
- termination;
- scheduler/yield rules.

This is now the single largest “opcode map” still properly belonging in this
document.

## 28.10 Scenario/event script record/header structure

For `IAM/AREA`, recover:

- event descriptors;
- entry points;
- local variables;
- scheduler metadata;
- script boundaries.

## 28.11 Exact bridge to structured SCX scripts

Find the VM/runtime operations that:

- look up a script;
- make an instance;
- start it;
- stop it;
- wait for it;
- observe completion.

## 28.12 Retail asset coverage

Validation currently leans heavily on:

```text
aventure.SCX
Grid.SCX
```

Expand to:

- area/world SCX files;
- combat/fight resources;
- shoot resources;
- character-heavy scenes;
- effect-heavy scenes;
- late-game areas.

## 28.13 Build/version differences

Compare:

- localized Windows executables;
- patched builds;
- demos;
- Dreamcast;
- any surviving development data.

---

# 29. Reimplementation checklist

## 29.1 SCX parser

- [ ] Validate magic `0x00DEAD00`.
- [ ] Validate/record version `5`.
- [ ] Respect `mainBlockSize` at `+0x0C`.
- [ ] Dispatch sections by tag value.
- [ ] Stop on `0xDEADFFFF`.
- [ ] Parse tag-2 script count.
- [ ] Parse `0x64` template records.
- [ ] Preserve all unknown template fields.
- [ ] Parse shared 4-byte parameter storage.
- [ ] Parse `0x18` function records.
- [ ] Validate parameter indexes.
- [ ] Validate sync-function indexes.
- [ ] Preserve unknown function IDs.
- [ ] Do not interpret serialized indexes as pointers.

## 29.2 Runtime template/instance model

- [ ] Separate serialized template, runtime template and instance types.
- [ ] Give instances mutable function state.
- [ ] Copy mutable parameters as Runtime does.
- [ ] Rebuild sync links within the instance.
- [ ] Preserve repetition state.
- [ ] Preserve elapsed script time.
- [ ] Preserve paired-script metadata.
- [ ] Support per-instance sprite state.

## 29.3 Structured-script scheduler

- [ ] Compare full 32-bit IAM function IDs.
- [ ] Obtain function semantics from `iam-script-functions.md`.
- [ ] Walk synchronized function links.
- [ ] Preserve per-function progress state.
- [ ] Keep `contributesToGroupActive` as explicit metadata.
- [ ] Advance only when no contributing function remains active.
- [ ] Run immediate side effects without falsely blocking the group.
- [ ] Reinitialize correctly on repetition/restart.
- [ ] Preserve distinct failure/resource-missing paths.

## 29.4 Loading/activation architecture

- [ ] Do not execute every script on SCX load.
- [ ] Do not hardcode behavior by script name.
- [ ] Let scenario/game state activate script instances.
- [ ] Preserve numeric script IDs and lookup behavior.

## 29.5 Scenario/event VM

- [ ] Use one-byte opcode dispatch.
- [ ] Keep opcode-specific instruction lengths.
- [ ] Implement the variable/reference operand reader.
- [ ] Preserve signed-16-bit literal semantics.
- [ ] Implement known `0x46`, `0x67`, `0x84`, `0x85` behavior.
- [ ] Do not consume following bytes for operand-less opcodes.
- [ ] Model yield/suspension separately from instruction side effects.
- [ ] Preserve unknown opcodes with useful byte/offset diagnostics.

## 29.6 Documentation separation

- [ ] Keep function-ID names/handler tables in `iam-script-functions.md`.
- [ ] Keep SCX serialization/scheduling in this file.
- [ ] Keep the compact scenario/event opcode map in this file until it becomes
      large enough to justify a dedicated `iam-scenario-vm.md`.

---

# 30. Terminology

| Term | Meaning in this document |
|---|---|
| **structured Script system** | SCX system using `0x18` function records and 32-bit IAM function IDs |
| **IAM function ID** | `0xCC0000NN` high-level action identifier; see `iam-script-functions.md` |
| **script template** | loaded `0x64` SCX script definition before/independent of a playing instance |
| **script instance** | mutable playing copy/state created through `Script_MakeInstance()` |
| **function record** | `0x18` record containing ID, parameters, sync link and mutable state |
| **function group** | working term for a primary function plus linked/synchronized functions evaluated together |
| **SyncFunction** | Runtime's diagnostic term for the linked secondary-function relationship |
| **group-still-active** | scheduler accumulator controlled by selected IAM function returns |
| **scenario/event VM** | separate one-byte interpreter used by `IAM/AREA`, `IAM/START`, etc. |
| **scenario opcode** | one-byte VM opcode such as `0x46`; unrelated to `0xCC0000NN` IAM IDs |

---

# 31. Related documentation

## Authoritative companion document

[`iam-script-functions.md`](iam-script-functions.md)

Use it for:

- complete current IAM function catalogue;
- `Script_*` names;
- handler and reinit addresses;
- family/ordinal interpretation;
- per-function behavior;
- `Script_GetNumParam()` selector matrix;
- legacy/special function IDs;
- unresolved IAM function IDs.

## Other reverse-engineering documents

[`runtime-globals.md`](runtime-globals.md)

- global Script/scene/runtime state relevant to the structures documented here.

[`startup-sequence.md`](startup-sequence.md)

- when SCX resources and IAM scenario scripts are loaded/activated during boot,
  intro, main menu and New Game transitions.

[`3do.md`](3do.md)

- 3DO resource format used by SCX visual/sprite entries.

[`3dt.md`](3dt.md)

- texture format and renderer-side texture resources.

---

# 32. Current reverse-engineering boundary

The current structured-Script understanding is:

```text
SCX v5
    |
    +-- main tagged block
          |
          +-- 0xDEAD0002
                |
                +-- ScriptTemplate[0x64]
                +-- parameter storage
                +-- primary ScriptFunction[0x18]
                +-- sync ScriptFunction[0x18]
                        |
                        +-- 32-bit IAM function ID
                        +-- unknown +0x04
                        +-- parameter index
                        +-- sync index
                        +-- mutable +0x10
                        +-- mutable +0x14
                |
                +-- load-time index -> pointer relocation
                |
                +-- Script template registry
                |
                +-- Script_MakeInstance()
                |
                +-- mutable instance
                |
                +-- Script_PlayScript()
                       |
                       +-- current function group
                       +-- sync chain
                       +-- execute IAM actions
                       +-- accumulate group-still-active
                       +-- advance/repeat/reinit
```

In parallel:

```text
IAM/AREA / IAM/START / related scenario data
    |
    +-- compact byte stream
          |
          +-- u8 opcode
          +-- opcode-specific operands
          +-- VM context / scheduler
          |
          +-- high-level game/interface/presentation behavior
```

The important documentation boundary is now:

```text
this file:
    how scripts are stored, instantiated, scheduled and bridged

iam-script-functions.md:
    what each 32-bit IAM function actually means
```

That separation should be maintained as reverse engineering progresses.
