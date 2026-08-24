# Runtime structured SCX script lifecycle and scheduler

> **Status:** work-in-progress reverse-engineering documentation for OpenNomad  
> **Last updated:** 2026-08-24
>
> This document describes the **runtime lifecycle and scheduler** of Omikron's
> structured SCX `Script_*` system.
>
> It deliberately does **not** duplicate the complete 32-bit IAM/Quantic C
> function catalogue. Use:
>
> - [`iam-script-functions.md`](iam-script-functions.md) for native function IDs,
>   handler names, reinitializers and per-function semantics;
> - [`script-opcodes.md`](script-opcodes.md) for SCX `DEAD0002` serialization and
>   the `0x64` / `0x18` on-disk structures;
> - [`iam-scenario-vm.md`](iam-scenario-vm.md) for the separate compact
>   one-byte AREA/START VM and its launch opcodes.
>
> This file is authoritative for:
>
> - the **loaded Runtime `ScriptList` topology**;
> - the distinction between the mutable primary script array and additional
>   cloned instance slots;
> - `Script_MakeInstance`, `Script_RemoveInstance` and
>   `Script_RemoveAllInstances`;
> - per-command parameter cloning and synchronized-link reconstruction;
> - `Script_PlayScript`;
> - `Script_PlayScriptList`;
> - execution-limit/count eligibility;
> - handler-return group blocking;
> - whole-script repetition and reinitialization;
> - script/list elapsed clocks;
> - AREA `0x2E`, `0x39..0x3C`, and `0x5A` structured-script activation and tracking;
> - and the important places where OpenNomad's current `ScriptRuntime` is a
>   **safe modern model rather than a byte-for-byte copy of Runtime ownership**.
>
> Several points in this document correct older wording in
> [`script-opcodes.md`](script-opcodes.md), especially the oversimplified
> “immutable template -> mutable instance” model.

Related documentation:

- [`scx.md`](scx.md) — complete SCX v5 container and resource sections;
- [`iam-script-functions.md`](iam-script-functions.md) — native function
  catalogue;
- [`script-opcodes.md`](script-opcodes.md) — serialized script records;
- [`iam-scenario-vm.md`](iam-scenario-vm.md) — compact AREA VM;
- [`runtime-main-loop.md`](runtime-main-loop.md) — engine-frame/timing driver;
- [`runtime-globals.md`](runtime-globals.md) — script callback and timing globals;
- [`sprite.md`](sprite.md) — per-instance sprite resources;
- [`audio.md`](audio.md) — structured sound commands;
- [`3da.md`](3da.md) / [`3dp.md`](3dp.md) — body-animation/path operations.

---

# 1. Evidence model

Sources are ranked:

1. **direct `Runtime.exe` disassembly and diagnostics;**
2. **retail SCX data;**
3. **the recovered compact AREA launch paths;**
4. **current OpenNomad implementation/tests;**
5. older reverse-engineering notes.

Confidence labels:

- **Confirmed — Runtime:** directly established by executable behavior.
- **Confirmed — data:** directly established from retail serialization.
- **Corroborated:** executable and data independently agree.
- **Strongly reconstructed:** behavior is clear but the original source-level
  type/name is unavailable.
- **Provisional:** useful interpretation still requiring more tracing.
- **OpenNomad-only:** deliberate safe/debug/modern behavior.

Reference executable:

```text
File:
    Runtime.exe

Supplied analysis file:
    Runtime(1).exe

Image base:
    0x00400000

SHA-256:
    55f7120bfea7891b048c64e3682f3259cdbf2719a43fa24e42254b753c95d2ef
```

---

# 2. Structured SCX scripts are not the AREA VM

Omikron has at least two distinct scripting runtimes.

Structured SCX system:

```text
0x64-byte script records
0x18-byte function records
32-bit IAM function IDs
root command + SyncFunction chains
mutable parameters/counters
Script_PlayScript
```

Compact AREA VM:

```text
u8 opcode stream
evaluation stack
global variables
relative branches
wait states
AREA event contexts
```

Example namespaces:

```text
SCX Script_* function:
    0x04000029  SetSpriteFrame

AREA VM opcode:
    0x46        OpenInterface
```

They interact, but they must remain separate interpreters.

---

# 3. Revised Runtime ownership model

The most important lifecycle correction is:

> Runtime does **not** treat every loaded SCX script as an immutable template
> that must be cloned before execution.

After load, a ScriptList contains **two script populations**:

```text
ScriptList
    |
    +-- primary loaded script array
    |      0x64 bytes each
    |      mutable
    |      executed directly every frame
    |
    +-- additional instance-slot array
           0x64 bytes each
           created by Script_MakeInstance()
           removed automatically after completion
```

`Script_PlayScriptList()` services both.

This means the word:

```text
template
```

is useful for the serialized definition concept, but is misleading if applied
to the primary in-memory Runtime array after load.

---

# 4. Serialized definition versus Runtime primary record

On disk, SCX contains a serialized `0x64` script definition whose pointer-like
fields are indexes/placeholders.

During SCX load Runtime:

- relocates parameter indexes to pointers;
- relocates synchronized-command indexes to pointers;
- establishes owner/list pointers;
- initializes mutable execution state;
- retains the resulting `0x64` record in the ScriptList's **primary array**.

That primary record is then passed directly to:

```text
Script_PlayScript()
```

and mutated during normal execution.

Thus:

```text
serialized definition
    ->
loaded mutable primary Runtime script
```

is the direct normal lifecycle.

---

# 5. Additional cloned instances are a second mechanism

Runtime separately implements:

```text
Script_MakeInstance()
```

at:

```text
0x004A0260
```

This takes:

```text
ScriptList*
source Script*
```

where the source must belong to the ScriptList's **primary script array**.

It produces another mutable `0x64` script record in a dedicated instance-slot
array.

Therefore:

```text
primary script
    ->
optional cloned instance
```

is a real secondary operation.

It should not be assumed to be the implementation of every AREA script launch.

---

# 6. Important correction: `0x004A0260`

Current OpenNomad source contains an older comment describing the
`0x004A0260` region as an:

```text
execution-limit precheck
```

That is incorrect.

Direct diagnostic xrefs prove:

```text
0x004A0260 = Script_MakeInstance
```

Evidence:

```text
"Script_MakeInstance(): Your ScriptListPtr is NULL."
    -> xref 0x004A027D

"Script_MakeInstance(): Your ScriptPtr is NULL."
    -> xref 0x004A0292
```

The scheduler's execution-limit test is directly inside
`Script_PlayScript()` around `0x0044C9D2`.

Do not propagate the stale `0x004A0260` comment.

---

# 7. Partial Runtime ScriptList layout

Current directly supported fields:

```c
struct RuntimeScriptList {
    // +0x00..+0x07 unresolved

    uint32_t primaryScriptCount;       // +0x08
    RuntimeScript *primaryScripts;     // +0x0C

    // ...

    void *spriteOrResourceTable;       // +0x34, exact type partial

    // ...

    uint32_t instanceSlotExtent;       // +0x4C
    uint32_t maxInstanceSlots;         // +0x50
    RuntimeScript *instanceSlots;      // +0x54

    float elapsedScriptFrames;         // +0x58

    void *contextState;                // +0x5C, exact type unresolved

    // ...
};
```

Only the fields used by the recovered lifecycle are listed.

Do not infer a complete structure from these sparse offsets.

---

# 8. Primary script array

Primary scripts:

```text
base:
    ScriptList +0x0C

count:
    ScriptList +0x08

stride:
    0x64
```

Range:

```text
[primaryScripts,
 primaryScripts + primaryScriptCount * 0x64)
```

`Script_MakeInstance()` explicitly validates that its source script lies within
this range.

This proves the clone source is a primary loaded script, not another clone.

---

# 9. Script lookup by 16-bit ID — `0x0044CD10`

A direct Runtime helper scans:

```text
ScriptList +0x0C
```

for:

```text
ScriptList +0x08
```

records of stride:

```text
0x64
```

and compares:

```text
WORD [script +0x1A]
```

against the requested ID.

Working signature:

```c
RuntimeScript *Script_FindById(
    RuntimeScriptList *list,
    uint16_t scriptId);
```

Address:

```text
0x0044CD10
```

This is the lookup used by the generic structured-script activation path around
`0x0041DC10`.

---

# 10. Script ID getter — `0x0044CD40`

Helper:

```text
0x0044CD40
```

returns:

```text
WORD [script +0x1A]
```

This further confirms:

```text
+0x1A = 16-bit structured script ID
```

independently of serialization.

---

# 11. Primary records are mutable

`Script_PlayScriptList()` directly calls:

```text
Script_PlayScript(primaryScript)
```

for each primary record.

`Script_PlayScript()` mutates fields including:

```text
+0x1C runtime state
+0x1E flags/status
+0x24 current root group
+0x38 repeat index
+0x58 elapsed time
+0x5D paired-script gate
+0x5E..+0x61 context state indirectly/through activation paths

command +0x14 execution counts
command parameter values through native handlers
```

Therefore the primary array is unquestionably live mutable state.

---

# 12. Additional instance-slot array

Instance storage:

```text
base:
    ScriptList +0x54

slot stride:
    0x64

maximum slots:
    ScriptList +0x50
```

A separate field:

```text
ScriptList +0x4C
```

controls how many slots the allocator/scanner currently considers.

The correct interpretation is subtler than:

```text
current live instance count
```

---

# 13. `+0x4C` is a slot extent/high-water value

`Script_MakeInstance()`:

1. scans slots:
   ```text
   [0, list+0x4C)
   ```
   looking for a free slot;
2. if a free slot exists, reuses it;
3. if none exists and:
   ```text
   list+0x4C < list+0x50
   ```
   it takes the next slot at the end and increments `+0x4C`;
4. if the extent has reached the maximum and no free slot exists, it reports
   instance exhaustion.

`Script_RemoveInstance()` zeroes a slot but does **not** decrement `+0x4C`.

Therefore:

```text
+0x4C = allocated/scannable slot extent
```

or:

```text
instance high-water count
```

is more accurate than:

```text
number of currently live instances
```

---

# 14. Instance-slot free marker

During allocation the free-slot scan tests:

```text
slot +0x28 == 0
```

The field at `+0x28` is the root-command pointer in the Runtime script record.

Removal zeroes the entire `0x64` record, so this naturally becomes zero.

Thus:

```text
instance +0x28 == NULL
```

acts as the slot allocator's free marker.

This is an implementation detail of the original fixed-slot pool.

---

# 15. Script record size remains `0x64`

Both:

```text
primary scripts
instance slots
```

use exactly:

```text
0x64 bytes
```

`Script_MakeInstance()` begins the clone with:

```text
rep movsd
25 dwords
```

which is:

```text
25 * 4 = 100 = 0x64
```

The same structure is therefore used for a loaded primary script and a cloned
instance.

Ownership of pointed-to command/parameter data differs.

---

# 16. Runtime script record

Current lifecycle-oriented view:

```c
struct RuntimeScript {
    RuntimeScriptList *owner;          // +0x00

    char name[22];                     // +0x04..+0x19
    uint16_t scriptId;                 // +0x1A

    uint16_t runtimeState;             // +0x1C
    uint16_t flagsAndStatus;           // +0x1E

    uint32_t rootCommandCount;          // +0x20
    uint32_t currentRootIndex;          // +0x24
    RuntimeScriptCommand *rootCommands;// +0x28

    uint32_t linkedCommandCount;        // +0x2C
    RuntimeScriptCommand *linkedCommands;// +0x30

    int32_t repeatLimit;                // +0x34
    uint32_t repeatIndex;               // +0x38

    uint32_t bindingTableA[3];          // +0x3C..+0x44
    uint32_t bindingTableB[3];          // +0x48..+0x50

    RuntimeScript *relatedScript;       // +0x54
    float elapsedScriptFrames;          // +0x58

    uint8_t modeFlags;                  // +0x5C
    uint8_t relatedScriptGate;          // +0x5D
    uint8_t contextActive[4];            // +0x5E..+0x61

    uint8_t unresolved62[2];             // +0x62..+0x63
}; // 0x64
```

This is still a sparse/reconstructed type.

The exact meaning of every bit in `+0x1E` and `+0x5C` remains incomplete.

---

# 17. Script `+0x5C` bit 1

Runtime helpers:

```text
0x0044CD50
0x0044CD60
```

directly get/set:

```text
(script +0x5C) bit 1
```

Getter:

```text
(byte >> 1) & 1
```

Setter:

```text
value != 0:
    byte |= 0x02

value == 0:
    byte &= ~0x02
```

The bit is definitely a first-class script mode flag.

Its source-level semantic name remains unresolved.

---

# 18. Context bytes `+0x5E..+0x61`

There are exactly four bytes:

```text
+0x5E
+0x5F
+0x60
+0x61
```

used as external/context activation selectors.

`Script_PlayScript()` can select among these four.

Helper around:

```text
0x0041D9C0
```

looks up a script by ID and returns true when **any** of the four bytes is
nonzero.

This makes the current description:

```text
four context-active bytes
```

substantially stronger than the older generic “tail metadata” wording.

The identity of the four contexts still requires more naming work.

---

# 19. `+0x1E` contains multiple subfields

`Script_PlayScript()` manipulates the `uint16_t` at:

```text
+0x1E
```

as several fields.

Observed behavior includes:

```text
low nibble:
    cleared before reinit/deactivation
    updated with final per-call status/active value

high byte low nibble:
    context-slot index 0..3

bit 12 / high-byte 0x10:
    set when final script result is active/nonzero
```

Therefore `+0x1E` should not be modeled as one Boolean flags field.

A future typed bitfield can be introduced once all bits are traced.

---

# 20. Runtime command record

Runtime command layout:

```c
struct RuntimeScriptCommand {
    uint32_t functionId;                // +0x00
    uint32_t valueCount;                // +0x04
    uint32_t *values;                   // +0x08
    RuntimeScriptCommand *syncFunction; // +0x0C
    uint32_t executionLimit;            // +0x10
    uint32_t executionCount;            // +0x14
}; // 0x18
```

This is the relocated form of the serialized command documented in
`script-opcodes.md`.

Serialized:

```text
+0x08 = firstValueIndex
+0x0C = linked index / -1
```

Runtime:

```text
+0x08 = parameter pointer
+0x0C = SyncFunction pointer / NULL
```

---

# 21. `SyncFunction` terminology

Runtime diagnostics explicitly call the linked command pointer:

```text
SyncFunction
```

This is the strongest surviving original terminology for the relation.

A root command plus its reachable `SyncFunction` chain behaves as one
synchronized scheduler group.

Documentation may use:

```text
group
```

as a behavioral shorthand, but:

```text
SyncFunction
```

should be retained when describing the actual field.

---

# 22. `Script_MakeInstance` validates both arguments

At entry:

```text
0x004A0260
```

Runtime validates:

```text
ScriptListPtr != NULL
ScriptPtr != NULL
```

then verifies that the source script pointer belongs to:

```text
[list->primaryScripts,
 list->primaryScripts + list->primaryScriptCount * 0x64)
```

An arbitrary clone or foreign script pointer is not accepted as a valid source.

---

# 23. Instance capacity error

If:

```text
no reusable free slot
```

and:

```text
instanceSlotExtent >= maxInstanceSlots
```

Runtime reports a diagnostic equivalent to:

```text
There are %d instances in script. Max number is %d...
```

and returns failure.

The wording sounds like `+0x4C` is a live count, but the allocator/remover
behavior proves it is really the high-water/scannable extent.

Machine behavior wins over diagnostic shorthand.

---

# 24. Initial `0x64` clone

Once a slot is selected:

```text
memcpy(instance, sourcePrimary, 0x64)
```

is performed.

Immediately afterward Runtime changes cloned ownership fields.

Notably:

```text
instance +0x54 = 0
```

so the clone does **not** retain the primary script's related-script pointer.

This is a concrete lifecycle difference between primary scripts and instances.

---

# 25. Root command arrays are independently allocated

Runtime allocates:

```text
source.rootCommandCount * 0x18
```

and copies the source root command array.

The instance's:

```text
+0x28
```

is changed to the newly allocated array.

Thus root-command mutation in a clone cannot mutate the primary script's command
records.

---

# 26. Linked command arrays are independently allocated

Likewise:

```text
source.linkedCommandCount * 0x18
```

is allocated and copied.

Instance:

```text
+0x30
```

points to the clone-owned linked-command array.

This creates an entirely independent mutable command graph.

---

# 27. Command parameter arrays are cloned per command

This is another important correction to the simplified model.

For each cloned root and linked command, `Script_MakeInstance()`:

1. reads:
   ```text
   command.valueCount
   ```
2. allocates:
   ```text
   valueCount * 4
   ```
3. stores the new pointer at:
   ```text
   instanceCommand +0x08
   ```
4. copies exactly the source command's parameter words.

Therefore the original instance allocation topology is:

```text
instance script
    |
    +-- root command array
    |      |
    |      +-- command-local parameter allocation
    |      +-- command-local parameter allocation
    |      ...
    |
    +-- linked command array
           |
           +-- command-local parameter allocation
           +-- command-local parameter allocation
           ...
```

It is **not** one monolithic clone of the complete SCX shared value pool.

---

# 28. Why serialized SCX still has a shared value pool

On disk, commands refer into the shared serialized value pool by index.

The SCX loader relocates that representation into Runtime parameter pointers.

`Script_MakeInstance()` then clones from those **already relocated source
parameter spans** into command-local allocations.

So both statements are true:

```text
serialized format:
    shared value pool

cloned Runtime instance:
    per-command private value arrays
```

This distinction matters when comparing parser architecture with Runtime heap
ownership.

---

# 29. OpenNomad value-pool model

Current OpenNomad instead performs:

```cpp
instance.value_pool = scx.shared_values;
```

and preserves:

```text
first_value_index
```

in every runtime command.

This is a safe, simple modern representation.

For valid scripts it can preserve parameter mutation semantics as long as:

- each logical script execution owns an independent copy;
- commands address their correct slices;
- overlapping serialized slices, if any, preserve intended sharing semantics.

But it is **not** the original Runtime allocation topology.

---

# 30. Parameter aliasing is a future compatibility question

Because Runtime clones parameters per command, two different commands in a
`Script_MakeInstance` clone do not automatically share one mutable word merely
because their serialized source ranges overlap.

OpenNomad's whole-pool copy could preserve such aliasing.

Therefore a retail corpus audit should check whether command parameter spans
ever overlap in ways that would make this distinction observable.

If they do, OpenNomad should follow Runtime's per-command ownership semantics.

---

# 31. Binding-table data is also cloned

`Script_MakeInstance()` copies the six dwords covering:

```text
binding table A descriptor
binding table B descriptor
```

and allocates/copies associated table/name-owned data.

The exact descriptor internals remain documented conservatively in
`script-opcodes.md`.

The lifecycle fact is:

> a cloned instance owns more than just command arrays; relevant
> binding/resource-side data is duplicated or rebuilt as well.

---

# 32. Synchronized links are rebuilt inside the clone

After command arrays are copied, Runtime walks source root/linked chains and
reconstructs:

```text
instanceCommand +0x0C
```

to point at the corresponding **instance linked command**.

It validates that the linked index lies within:

```text
source.linkedCommandCount
```

and emits an error for an out-of-range SyncFunction index.

Therefore:

```text
clone root
    -> clone linked command
```

never intentionally points back into the primary script's command array.

---

# 33. Instance basic execution state reset

Before normal clone post-processing Runtime clears/reset fields including:

```text
low nibble of +0x1E

+0x24 currentRootIndex = 0
+0x38 repeatIndex      = 0
+0x58 elapsed time     = 0
```

then calls generic:

```text
Script_Reinit
0x0044A7E0
```

This makes the clone a freshly initialized execution object rather than a
snapshot of the source's current progress.

---

# 34. Generic reinit — `0x0044A7E0`

`Script_Reinit` dispatches function-specific reset handlers over a script.

Depending on function it can reset:

- progress values;
- interpolation current values;
- animation state;
- wait elapsed state;
- sound latches;
- other mutable command parameters.

Whole-script reset therefore cannot be represented as only:

```text
currentRootIndex = 0
```

This applies both to repetition and to activation/relaunch.

---

# 35. Clone execution-count initialization is specialized

A subtle but important `Script_MakeInstance()` behavior occurs after reinit.

For many commands Runtime writes:

```text
executionCount = executionLimit
```

rather than leaving count zero.

Special classes are treated differently.

This means:

> `Script_MakeInstance()` is not simply “copy a script and start every command
> from zero.”

It creates a specialized instance state with selective active/per-instance
resources.

---

# 36. Sprite-owning commands are special during instance creation

The post-reinit walk special-cases at least:

```text
0x0400000D
0x04000020
0x04000028
```

These are the same function IDs associated with instance-owned sprite behavior
in the recovered code.

For these commands Runtime obtains the semantic sprite parameter, resolves a
per-instance resource slot and creates/copies a live `SpriteInstance`.

This is direct evidence that cloned scripts can own private sprite resources.

---

# 37. Per-instance sprite setup

The clone path includes:

```text
sprite allocation:
    0x0048EBF0

SetSpriteFrame(..., 0):
    0x0048EF10
```

and copies one `0x40` sprite-state/prototype block.

It then clears intrusive ownership/link fields so the cloned sprite has valid
independent runtime state.

See `sprite.md` for the `0x40` sprite ABI.

---

# 38. Sound functions are also treated specially

The clone post-processing distinguishes sound-family IDs around:

```text
0x05000014
0x05000015
0x05000016
```

from the generic:

```text
executionCount = executionLimit
```

case.

This is consistent with sound commands having mutable latches/voice state that
must retain runtime behavior.

The exact clone-start semantics of every sound command belong in
`iam-script-functions.md`.

---

# 39. Do not map all AREA launches to `Script_MakeInstance`

A whole-`.text` direct-call scan has not identified a normal direct caller of:

```text
0x004A0260 Script_MakeInstance
```

and the recovered AREA launch handlers do **not** call it directly.

This does **not** prove the function is unused:

- an indirect call may still exist;
- another subsystem may use it through unresolved dispatch;
- a rare path may not be obvious from simple rel32 scans.

But it does prove:

> “AREA opcode launch -> Script_MakeInstance” is not an established call chain.

Do not document it as one.

---

# 40. `Script_RemoveInstance` — `0x004A0C30`

Diagnostic xrefs identify:

```text
0x004A0C30
```

as:

```text
Script_RemoveInstance
```

The function validates:

```text
ScriptList != NULL
instance pointer belongs to instance-slot range
```

then performs resource/command cleanup and zeroes the slot.

---

# 41. Instance range validation

Valid instance pointer range:

```text
base =
    list +0x54 pointer

end =
    base + list.instanceSlotExtent * 0x64
```

The pointer must lie inside this range.

The function is therefore specifically for cloned instance slots, not primary
script records.

---

# 42. Instance resource cleanup — `0x004A0DA0`

Before freeing the slot Runtime traverses:

```text
root command
+
SyncFunction chain
```

and performs instance-specific cleanup.

The same sprite-owning function family appears in this path.

It resolves the command's semantic sprite parameter and releases the
corresponding per-instance sprite resource through a helper around:

```text
0x004A5910
```

This closes the ownership loop established by `Script_MakeInstance`.

---

# 43. Command/parameter cleanup

`Script_RemoveInstance()` also calls cleanup helpers around:

```text
0x0044A340
0x0044A460
```

before clearing the script record.

These release command arrays, parameter allocations and associated cloned
script data.

Exact helper names should remain provisional until their full individual
responsibilities are split.

---

# 44. Slot zeroing

After cleanup Runtime clears:

```text
25 dwords
=
0x64 bytes
```

of the instance slot.

This restores the allocator's:

```text
+0x28 == 0
```

free marker.

The instance extent/high-water field is not decremented.

---

# 45. `Script_RemoveAllInstances` — `0x004A0CC0`

This function:

1. validates the ScriptList;
2. iterates:
   ```text
   list.instanceSlots[0 .. instanceSlotExtent)
   ```
3. runs the same per-instance resource cleanup;
4. runs command/parameter cleanup;
5. zeroes each `0x64` slot.

It does not simply free the whole slot-array allocation as one opaque block.

---

# 46. Instance extent remains allocated

The examined `Script_RemoveAllInstances()` path does not reset:

```text
ScriptList +0x4C
```

to zero.

Thus old slot addresses remain part of the scannable extent and are simply
available for reuse.

This reinforces the:

```text
high-water/extent
```

interpretation.

---

# 47. Global ScriptList registry

A helper around:

```text
0x004A0D30
```

scans ScriptLists from a global registry around:

```text
0x00903B00
```

using a count around:

```text
0x00531260
```

to determine which ScriptList instance range owns a given pointer.

The precise higher-level use remains unresolved.

The important lifecycle fact is that cloned instances can be mapped back to
their owning ScriptList.

---

# 48. `Script_PlayScriptList` — `0x0044CC50`

This newly separated scheduler function is one of the most important runtime
entrypoints.

Working signature:

```c
uint8_t Script_PlayScriptList(
    RuntimeScriptList *list);
```

It services:

```text
all primary scripts
then all clone instance slots
```

and advances the ScriptList-level elapsed clock.

---

# 49. Primary-script scheduling loop

`Script_PlayScriptList()` obtains:

```text
count = list +0x08
base  = list +0x0C
```

and walks:

```text
count
```

records of stride:

```text
0x64
```

For each:

```text
result = Script_PlayScript(script)
```

If:

```text
result == 0xFF
```

the list scheduler aborts/returns the error status.

Otherwise it ORs the returned code into an aggregate result.

Primary records are **not removed** when their result becomes zero.

---

# 50. Instance scheduling loop

Next:

```text
extent = list +0x4C
base   = list +0x54
```

and each `0x64` slot is passed to:

```text
Script_PlayScript()
```

If result is:

```text
0
```

Runtime immediately calls:

```text
Script_RemoveInstance(list, instance)
```

If result is:

```text
0xFF
```

the scheduler aborts/returns the error status.

Other nonzero result codes keep the instance slot resident.

---

# 51. Primary and clone completion are intentionally different

This gives a clean ownership distinction:

```text
primary script reaches inactive/completed result:
    record remains in primary array

clone instance reaches zero:
    runtime destroys clone immediately
```

This is another reason a single modern `vector<ScriptInstance>` is not an
exact mirror of the original ownership topology.

---

# 52. ScriptList elapsed time

After servicing both populations Runtime executes:

```text
list[+0x58] += g_scriptFrameDelta
```

with:

```text
g_scriptFrameDelta = float [0x00531218]
```

Thus there are at least **two levels of script time**:

```text
ScriptList +0x58
RuntimeScript +0x58
```

Both are measured in the same native script-frame domain.

---

# 53. Per-script elapsed time

`Script_PlayScript()` similarly performs:

```text
script[+0x58] += g_scriptFrameDelta
```

on its ordinary playback path.

This elapsed value participates in context/external-scene timing and is also
conceptually related to script-timed functions such as synchronized sound.

It is separate from each command's own mutable duration/progress values.

---

# 54. Script timing unit

Global:

```text
0x00531218
```

is the delta consumed by structured scripts.

The runtime timing convention is:

```text
1.0 script frame
=
1 nominal 30 Hz tick
=
1/30 second
```

See `runtime-main-loop.md`.

Runtime does not require each native Script_* handler to convert from seconds.

They receive/use the already established native script-frame delta.

---

# 55. Engine-frame integration

`Script_PlayScriptList()` is called directly from the engine-frame region.

Observed call:

```text
0x004203E4
    call 0x0044CC50
```

At that point the argument points into the active world/context structure at
its embedded ScriptList/SCX runtime state.

This proves structured scripts are serviced as part of recurring engine-frame
processing.

They are not only advanced when an AREA VM opcode explicitly polls them.

---

# 56. World contexts and ScriptLists

The engine-frame code works through the fixed world/context storage around:

```text
0x009103E0
```

already documented in `runtime-globals.md`.

The ScriptList passed to `0x0044CC50` sits inside/alongside the active
world/scenario context state.

This matches the SCX architecture:

```text
world/scenario
    owns loaded script list
```

rather than one process-global singleton script scheduler.

---

# 57. `Script_PlayScript` — `0x0044C860`

This is the central per-script scheduler.

Working signature:

```c
uint8_t Script_PlayScript(
    RuntimeScript *script);
```

Its returned byte is **not** a simple Boolean success value.

Observed special values and normal active-state values are discussed below.

---

# 58. Null script return

If:

```text
script == NULL
```

Runtime emits:

```text
Script_PlayScript(): Your ScriptPtr is NULL
```

and returns:

```text
0xFF
```

`Script_PlayScriptList()` treats this as an error/abort code.

---

# 59. `+0x1C == 0` return

If:

```text
WORD [script +0x1C] == 0
```

`Script_PlayScript()` immediately returns:

```text
2
```

No command chain is serviced.

Thus:

```text
+0x1C == 0
```

is some inactive/disabled script state, but return code 2 means the function's
status domain is richer than:

```text
0 inactive
1 active
```

Do not map these values directly to OpenNomad's `ScriptRunState`.

---

# 60. Related-script gate return

If:

```text
script.relatedScript != NULL
```

and:

```text
script.relatedScriptGate == 0
```

Runtime:

```text
clears low status nibble
sets script +0x1C = 0
returns 4
```

This is a second special nonzero status.

Again:

```text
Script_PlayScript return code
```

is not just a lifetime Boolean.

---

# 61. Related-script handoff

When a normally executing script finally reaches an inactive result:

```text
if relatedScript != NULL:
    script +0x5D        = 0
    relatedScript +0x5D = 1
```

then its state/low flags are cleared.

This strongly indicates a:

```text
paired/related-script handoff gate
```

but the exact IAM authoring relation remains unresolved.

Do not narrow it to:

```text
parent
child
next script
```

without more evidence.

---

# 62. External/context gating before command dispatch

Before root-command scheduling, `Script_PlayScript()` examines:

- the owning ScriptList/context;
- global context selector:
  ```text
  0x00903AE0
  ```
- script mode bits at `+0x1F`;
- context bytes:
  ```text
  +0x5E..+0x61
  ```
- an external/context helper:
  ```text
  0x0049F210
  ```

The exact external scene/context subsystem is not fully named.

The important scheduler fact is:

> command execution can be conditioned by one of up to four script context
> slots, and this state contributes to the final active result.

---

# 63. Context slot selection

When global selector:

```text
0x00903AE0 != 5
```

Runtime uses that value directly as a context index.

When it equals:

```text
5
```

Runtime searches the script's four:

```text
+0x5E..+0x61
```

bytes beginning from the context index encoded in the high byte of `+0x1E`.

This suggests:

```text
5 = automatic/scan mode
```

as a useful provisional description.

Do not promote it to an original enum name.

---

# 64. Context index persistence

Runtime stores the selected context index into:

```text
bits 8..11 of script +0x1E
```

during playback.

This provides a resume/round-robin-like cursor across the four context-active
bytes.

Exact authoring semantics remain unresolved.

---

# 65. Root command selection

Normal command execution uses:

```text
rootCommandCount =
    script +0x20

currentRootIndex =
    script +0x24

rootCommands =
    script +0x28
```

Current root:

```text
rootCommands[currentRootIndex]
```

with stride:

```text
0x18
```

This root anchors the synchronized group serviced on that call.

---

# 66. Command eligibility precheck

For every root/linked command before native dispatch:

```text
limit = command +0x10
count = command +0x14
```

Runtime dispatches when:

```text
count < limit
```

or:

```text
limit == 0xFFFFFFFF
```

Otherwise it skips that command's native call for this tick.

Conceptually:

```cpp
eligible =
    executionLimit == 0xFFFFFFFF
    || executionCount < executionLimit;
```

This answers only:

```text
may this command execute?
```

It does not determine group completion by itself.

---

# 67. `0xFFFFFFFF` execution limit

Special value:

```text
0xFFFFFFFF
```

is treated as unbounded/unlimited by the eligibility gate.

Do not apply ordinary:

```text
executionCount >= executionLimit
```

logic to it.

Individual native functions can still have their own mutable parameters and
return-state semantics.

---

# 68. Root plus SyncFunction chain

After servicing the root, Runtime follows:

```text
command +0x0C
```

until:

```text
NULL
```

Each reachable linked command is independently subject to:

```text
execution-limit eligibility
native function dispatch
handler-specific count/progress changes
```

The entire chain is serviced in one `Script_PlayScript()` call.

---

# 69. Group behavior

A useful scheduler abstraction is:

```text
GROUP N
    root command
        |
        +-- SyncFunction
                |
                +-- SyncFunction
                        ...
```

Commands are synchronized because the root index does not advance while any
**blocking/contributing native function** reports itself active.

That is different from requiring every command's execution count to reach its
limit simultaneously.

---

# 70. Native dispatch tree

`Script_PlayScript()` contains a direct comparison/branch tree over known
32-bit IAM function IDs.

It is not a generic table lookup for the main current-format execution path.

The exact function IDs/names belong in:

```text
iam-script-functions.md
```

This document only distinguishes scheduler behavior.

---

# 71. Two native-call classes

Runtime treats native functions in two scheduler classes.

## Contributing/stateful

For selected functions:

```asm
call Script_Function
or   bl, al
```

Their return contributes to the group's active accumulator.

## Immediate/non-contributing

Other functions are called without ORing their `AL` return into `BL`.

Their native return does not hold the synchronized group open.

This distinction is explicit machine code.

---

# 72. Blocking accumulator

Register:

```text
BL
```

is initialized to zero before the group.

For every contributing function:

```text
BL |= handlerAL
```

At end of the root+SyncFunction chain:

```text
BL != 0
    ->
remain on current root group

BL == 0
    ->
advance current root index
```

This is the core structured-script scheduler rule.

---

# 73. Handler return is not generic success/failure

For a contributing function:

```text
AL != 0
```

means approximately:

```text
this synchronized operation remains active
```

It does **not** mean:

```text
success
```

and:

```text
AL == 0
```

does not necessarily mean:

```text
error
```

Many stateful handlers return zero precisely when their timed operation has
finished.

---

# 74. Known contributing functions

Examples currently recovered as contributing/blocking include:

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

This list is intentionally illustrative.

The authoritative complete per-function catalogue belongs in
`iam-script-functions.md`.

---

# 75. Known immediate functions

Examples invoked without contributing `AL` include:

```text
SelectCamera
SwapObject
PlaySound
StopSound
SendMessage
```

Again, per-function details belong in the function catalogue.

---

# 76. Group advance

After all reachable commands have been considered:

```text
if BL == 0:
    currentRootIndex++
```

If the new index is still within:

```text
rootCommandCount
```

Runtime stores it to:

```text
script +0x24
```

and forces the script's active result for this call to nonzero.

Thus moving from group N to N+1 happens at the end of the current scheduler
call; group N+1 executes on a later call.

---

# 77. Whole-script end

When advancing would move past:

```text
rootCommandCount
```

Runtime increments:

```text
script +0x38 repeatIndex
```

and compares against:

```text
script +0x34 repeatLimit
```

Special:

```text
repeatLimit == -1 / 0xFFFFFFFF
    ->
infinite repetition
```

---

# 78. Whole-script repetition

If another repetition is allowed:

```text
clear low status nibble
Script_Reinit(script)
keep script active
```

The reinit path resets command-specific mutable state and returns the script to
its beginning.

This is not equivalent to only:

```text
currentRootIndex = 0
```

---

# 79. Exhausted repetition

If repeat limit is exhausted:

```text
BL = 0
```

and normal post-group completion logic deactivates the script, performs related
script handoff if applicable, and updates state flags.

For cloned instance slots, `Script_PlayScriptList()` sees return zero and
destroys the instance immediately.

## OpenNomad D5A mapping

The immutable parser names script `+0x34` as `repeat_limit` and `+0x38` as
`initial_repeat_index`. Each mutable `ScriptInstance` owns its current
`repeat_index`. At whole-script end OpenNomad increments that index, repeats
when the limit is `-1` or the incremented index is below the finite limit, and
otherwise completes the same instance.

Between passes, every root and linked command restores `execution_count` from
its serialized `initial_execution_count` and runs the existing function-specific
reinitializer (`Script_Reinit` in the recovered lifecycle). The instance ID,
launch context, character ownership, and sprite remap remain intact. An explicit
debugger reset additionally restores the serialized repeat-index seed.

The command precheck is deliberately independent of script `repeat_limit`:

```text
eligible iff execution_limit == 0xFFFFFFFF
         or execution_count < execution_limit
```

`0x004A0260` is `Script_MakeInstance`, not an execution-precheck routine.

---

# 80. Script elapsed clock update order

On the normal playback path:

```text
script.elapsed += g_scriptFrameDelta
```

occurs after command scheduling/repetition logic.

Therefore native handlers in the current `Script_PlayScript()` call see the
script's **pre-increment** `+0x58` value unless they update/read it through
another helper.

The new value is available on the next frame.

This ordering can matter for exact synchronized scheduling.

---

# 81. Final active/status calculation

Runtime combines:

```text
BL group-active state
+
external/context-active state
```

into a final byte result.

If the final value is zero:

- related-script gate is handed off if present;
- low status bits are cleared;
- `+0x1C` is set to zero.

The final result is also written into low bits of:

```text
+0x1E
```

and active state sets an additional bit in the high byte.

This reinforces that the return value is a compact status/active code, not a
C++-style enum with the same meanings as OpenNomad's debugger statuses.

---

# 82. `Script_PlayScript` special return summary

Known observed values:

```text
0xFF
    error/null/internal abort

0x02
    +0x1C already zero/inactive path

0x04
    related-script gate path

0x00
    normal final inactive/completed result

nonzero normal values
    active/context-active status
```

Exact original enum names are unknown.

Do not assign:

```text
Success
Failure
Running
Completed
```

one-to-one without more evidence.

---

# 83. `Script_PlayScriptList` aggregate result

The list scheduler ORs non-error script results into one aggregate byte.

This means bit patterns/status codes are intentionally composable at the list
level.

An instance is removed based specifically on:

```text
result == 0
```

not on a guessed `Completed` enum value.

---

# 84. Runtime command-count update responsibility

The central scheduler does **not** blindly increment every command's:

```text
executionCount
```

after dispatch.

Individual native function implementations control when and how their count
moves.

Examples:

- immediate functions may increment on one call;
- interpolated/timed functions may increment when a phase completes;
- function-specific reinit can reset or transform progress;
- unlimited limits still interact with function logic.

Therefore:

```text
executionCount
```

is command-local runtime state, not a generic scheduler tick counter.

---

# 85. Structured script timing versus engine timing

Runtime's main frame machinery produces a script-frame delta in the global:

```text
0x00531218
```

Structured scripts consume that value.

This keeps script functions in authored:

```text
30 Hz frame units
```

without coupling them to actual rendered FPS.

See `runtime-main-loop.md` for frame-delta production.

---

# 86. OpenNomad time conversion

Current OpenNomad API accepts:

```text
real delta seconds
```

then centrally converts:

```text
scriptFrames =
    clamp(realSeconds * 30, 0, 3)
```

This is a reasonable modern boundary.

Handlers then operate only in script-frame units.

The representation differs from Runtime's process-global delta but preserves
the intended unit domain.

---

# 87. Avoid double conversion

Callers must know whether they possess:

```text
real seconds
```

or:

```text
Omikron script frames
```

before invoking the structured script runtime.

Correct modern layering:

```text
application/scene seconds
    ->
one conversion
    ->
ScriptRuntime frame units
```

Do not divide/multiply by 30 again inside native handler implementations.

---

# 88. AREA launch family

The compact scenario VM provides four closely related structured-script launch
operations:

```text
0x39  StartScxScript
0x3A  StartScxScriptTracked

0x3B  StartCharacterScript
0x3C  StartCharacterScriptTracked
```

This four-opcode model supersedes older documentation that treated `0x39` as
the generic waiting launch.

---

# 89. `0x39` — generic fire-and-forget launch

Handler:

```text
0x004030E0
```

Operands:

```text
raw u16  scriptId
Scalar16 argumentB
Scalar16 argumentC
```

Behavior:

```text
activate/configure structured SCX script
continue AREA execution
```

It does **not** set AREA context state 4.

Therefore:

```text
0x39 = non-tracked generic variant
```

is Runtime-confirmed.

---

# 90. `0x3A` — generic tracked launch

Handler:

```text
0x004031E0
```

uses the same operand shape.

After activation it performs:

```text
context.flags |= 0x0004
context.state  = 4
```

Thus:

```text
0x3A = tracked/waiting generic variant
```

---

# 91. `0x3B` — character-bound fire-and-forget

Handler:

```text
0x00403300
```

Operands:

```text
Scalar16 characterId
raw u16  scriptId
Scalar16 cameraDurationUnits
```

It launches a character-bound structured script and continues AREA execution.

The third Scalar16 is presentation/camera-duration metadata and is not passed
to `ScriptLaunchContext.parameter`.

---

# 92. `0x3C` — character-bound tracked launch

Handler:

```text
0x00403430
```

uses the same three operands as `0x3B`.

After activation:

```text
context.flags |= 0x0004
context.state  = 4
```

Thus `0x3C` is the tracked character-bound counterpart.

---

# 93. State 4 is a generic tracked-child wait

AREA state:

```text
4
```

is entered by at least:

```text
0x3A
0x3C
```

and another native operation family.

Do not name it narrowly:

```text
character script wait
```

Use:

```text
tracked native/child-operation wait
```

until the common completion object is fully typed.

---

# 94. AREA activation does not call `Script_MakeInstance` directly

Direct tracing of:

```text
0x39
0x3A
0x3B
0x3C
```

does not show a direct call to:

```text
0x004A0260 Script_MakeInstance
```

The generic pair routes through an activation/configuration path around:

```text
0x0041DC10
```

The character pair uses character/world helpers and a related activation path
around:

```text
0x0041BA80
```

This is a major architectural distinction.

---

# 95. Generic activation helper — `0x0041DC10`

Current recovered behavior:

1. identify the appropriate active world/context;
2. obtain its ScriptList;
3. find the **primary script** by raw 16-bit ID via:
   ```text
   0x0044CD10
   ```
4. run several activation/context helpers;
5. call:
   ```text
   Script_Reinit
   0x0044A7E0
   ```
6. establish tracking/context metadata;
7. update script tail/context flags;
8. propagate the script ID into another world/script subsystem helper.

This looks like:

```text
reactivate/reinitialize primary loaded script
```

rather than:

```text
always allocate independent clone
```

---

# 96. Activation helper unresolved subcalls

`0x0041DC10` calls helpers including:

```text
0x0044B460
0x0044C7A0
0x0044C7B0
0x00451470
```

Their complete source-level roles remain unresolved.

Do not assign narrow names merely to make the launch path look cleaner.

What is already clear is enough:

```text
find primary script
reinitialize it
configure context/tracking state
```

---

# 97. Structured script activity query — `0x0041D9C0`

This helper:

1. locates the world/context ScriptList;
2. finds a primary script by ID;
3. scans:
   ```text
   script +0x5E .. +0x61
   ```
4. returns true if any of those four bytes is nonzero.

AREA launch handlers use this result after activation.

Thus the four tail bytes are directly tied to:

```text
is this structured script active in any context?
```

---

# 98. Generic activation tracking records

`0x0041DC10` can also populate a compact tracking table around:

```text
0x00910500
```

with roughly `0x0C`-byte records containing context/owner/script information
when one launch argument is not `-1`.

The exact field semantics remain incomplete.

This may be part of how tracked AREA operations observe child completion.

Do not replace it with a guessed `ScriptInstance*` field until the consumer is
fully recovered.

---

# 99. Character-bound launch metadata

`0x2E`/`0x5A` and `0x3B`/`0x3C` demonstrate that script execution can be
launched with a current or explicit external character binding not encoded in
the script's ordinary command parameter arrays.

Current OpenNomad represents this with:

```cpp
ScriptLaunchContext {
    optional<int16_t> character_id;
    int16_t parameter;
};
```

This is a useful modern abstraction. The compact `0x3B`/`0x3C` trailing
Scalar16 is not the `ScriptLaunchContext.parameter`: it is presentation/camera
duration metadata retained by the compact VM. Current-character `0x2E`/`0x5A`
also preserve that field and leave `character_id` sourced from the session's
controlled-character slot.

It is **not** a recovered `RuntimeScript` embedded field.

Runtime communicates character/world binding through surrounding native
context state and activation helpers.

---

# 100. Body-animation functions need launch context

The current intro script proves why that external binding matters.

Structured functions:

```text
Script_SelectBodyAnimation
Script_SelectRelativeBodyAnimation
```

use an object/body binding name from the script's binding table but need to
know **which character instance** receives the body animation.

OpenNomad's explicit:

```text
character_id
```

launch metadata is therefore semantically useful even though Runtime stores the
relationship differently. The handler never infers the controlled character:
an unbound instance is a structured error. Both functions use the same mutable
previous/current progress slots and reinitialize those slots to `0`/`1`; only
`SelectRelativeBodyAnimation` resolves a 3DP anchor.

---

# 101. OpenNomad `0x39`/`0x3A` implementation

OpenNomad now implements:

```text
0x39 StartScxScript
    fire-and-forget; continue in the same AREA interpreter invocation

0x3A StartScxScriptTracked
    track the exact ScriptInstance and wait in Runtime state 4
```

Both forms use the same generic SCX-ID resolution and launch sink. Only `0x3A`
installs the typed wait/completion handle.

---

# 102. Current OpenNomad instance model

Modern `ScriptInstance` stores:

```text
instance_id
source_script_index
script_name

launch context

one mutable value-pool copy
root command vector
linked command vector

current group index

sprite source -> live handle map
elapsed script frames

debugger pause/completion state
```

This is intentionally safer and easier to inspect than Runtime's raw pointer
graph.

It should be viewed as a **semantic execution model**, not the historical ABI.

---

# 103. Current OpenNomad ownership simplification

OpenNomad currently tends toward:

```text
parsed SCX script
    immutable

every activation
    ->
new ScriptInstance
```

Runtime instead has:

```text
loaded primary mutable scripts
    directly scheduled

+
optional clone instance slots
    created through Script_MakeInstance
```

This difference can become observable if the same script is activated while it
is already running or if primary script context bytes carry persistent state.

---

# 104. Why topology may matter

Potential observable differences include:

```text
relaunch while already active
concurrent activation of same script ID
related-script handoff
repeat state persistence
context-slot bytes +5E..+61
script +58 elapsed time
command-local mutable values
resource/sprite ownership
```

A modern implementation can use different memory ownership, but it must define
these semantics explicitly.

---

# 105. Recommended modern model

OpenNomad does **not** need a literal two-array `0x64` memory layout.

A good semantic model could distinguish:

```text
ScriptDefinition
    immutable parsed SCX data

PrimaryScriptState
    one mutable runtime state per loaded script ID

ScriptCloneInstance
    optional additional independent execution state
```

Both state types can share a common scheduler object.

This matches Runtime semantics more closely while retaining modern ownership.

---

# 106. Primary-script activation API

Rather than always:

```cpp
create_instance(scriptId)
```

consider a semantic operation:

```cpp
activate_primary_script(
    scriptId,
    activationContext);
```

that:

- finds the loaded primary state;
- reinitializes it as Runtime does;
- updates external context metadata;
- marks relevant context slots active.

Tracked AREA launch can then hold a stable activation token for completion.

---

# 107. Clone API should remain separate

A separate operation can model true Runtime:

```text
Script_MakeInstance
```

when its gameplay call path is identified.

For example:

```cpp
clone_script_instance(primaryScript)
```

with:

- independent commands;
- independent mutable params;
- private sprite resources;
- automatic destruction on zero result.

Do not conflate this with generic AREA activation merely because both create
“something that runs.”

---

# 108. Current OpenNomad group-completion mismatch

Current `ScriptRuntime::advance_instance()`:

1. services root + linked chain;
2. then computes:
   ```text
   group_done =
       root exhausted
       && all linked commands exhausted
   ```
3. advances only if that predicate is true.

The source comment correctly calls this an:

```text
Inferred group-completion rule
```

Retail Runtime instead uses:

```text
executionLimit/count
    -> dispatch eligibility

selected handler AL
    -> BL active accumulator

BL == 0
    -> group advance
```

These are not equivalent.

---

# 109. Why exhaustion-only scheduling can fail

Consider a timed native function whose:

```text
executionLimit = 1
executionCount = 0
```

while it returns:

```text
AL = 1
```

over several frames.

Runtime:

```text
keeps current group active while handler returns 1
```

even though execution-count behavior may not map cleanly onto each intermediate
tick.

An exhaustion-only scheduler can advance too early or too late depending on
where the handler mutates its count.

The handler return must be part of the scheduler contract.

---

# 110. Recommended scheduler result type

Modern handlers should expose something equivalent to:

```cpp
struct ScriptHandlerResult {
    bool contributes_to_group;
    bool remains_active;
    // error/debug information separately
};
```

or encode contribution statically in opcode metadata.

The important point is to separate:

```text
handler runtime activity
```

from:

```text
handler success/error
```

and from:

```text
execution-limit eligibility
```

---

# 111. Do not use undefined x86 AL as generic status

Many native handlers are effectively void/immediate operations whose return
register contents are irrelevant.

Runtime only uses `AL` for scheduler blocking when the callsite explicitly:

```asm
or bl, al
```

For non-contributing functions OpenNomad should ignore a synthetic return for
group scheduling.

This is safer than inventing completion semantics for every handler.

---

# 112. OpenNomad command status enum is debugger-facing

Current modern enum:

```text
k_running
k_completed
k_paused
k_error
```

is useful for:

- structured diagnostics;
- debugger UI;
- implementation control flow.

It is not the original return ABI of:

```text
Script_PlayScript
```

or every native handler.

Keep the modern enum, but do not use its names to reinterpret Runtime's byte
status values.

---

# 113. OpenNomad pause states are modern safety features

Current runtime can pause on:

```text
unhandled function ID
bad argument count
out-of-range resource
invalid linked command
cycle
command-budget exhaustion
unsupported subsystem
invalid duration
```

This is deliberate OpenNomad behavior.

Retail Runtime generally trusted authored data more aggressively and often
reported diagnostics rather than entering a modern debugger pause state.

These safety features should remain.

---

# 114. Command budget is OpenNomad-only

Current safety limit:

```text
4096 command dispatches per script tick
```

prevents malformed/cyclic command graphs from hanging OpenNomad.

There is no evidence that Runtime had this exact scheduler budget.

It should be documented as:

```text
modern malformed-data guard
```

not original behavior.

---

# 115. Linked-cycle detection is OpenNomad-only safety

Runtime expects valid `SyncFunction` chains.

Current OpenNomad explicitly tracks visited linked indexes and pauses on a
cycle.

This is excellent defensive behavior.

It does not need a historical analogue to be retained.

---

# 116. Instance IDs/generations are modern

OpenNomad gives logical instances:

```text
monotonic instance_id
```

for stable debugger/tracked-operation references.

Runtime mostly deals in:

```text
raw pointers
fixed slot addresses
context tables
```

A stable modern ID is preferable.

Tracked AREA completion should refer to a robust activation token/ID rather
than a raw vector index.

---

# 117. Sprite remap is a modern ownership map

Current:

```cpp
unordered_map<sourceSpriteIndex, SpriteHandle>
```

represents per-instance sprite ownership.

Runtime uses fixed resource records and live sprite pointers/copies established
during `Script_MakeInstance`.

The map is a good semantic replacement, provided:

```text
each relevant clone gets independent sprite runtime state
cleanup destroys those resources
```

---

# 118. Script reset and sprite cleanup

Runtime clone removal explicitly frees per-instance sprite resources.

Whole-script reinit also runs native reinitializers.

OpenNomad reset behavior should therefore distinguish:

```text
reset mutable command progress
```

from:

```text
destroy lifetime-owned resources
```

A debugger “reset instance” may intentionally perform both to return to a clean
fresh-launch state.

---

# 119. Audio reset semantics

Sound commands maintain runtime latches/voice handles.

When an OpenNomad instance is reset/replayed:

- voices started by that instance should be stopped as appropriate;
- started/voice latches should be reset;
- authored schedule values should be restored.

This maps well to Runtime's specialized sound treatment and per-function reinit
architecture.

See `audio.md`.

---

# 120. `PlaySyncSound` and script time

Synchronized sound depends on authored script-frame timing.

Runtime maintains:

```text
script +0x58 elapsed
list   +0x58 elapsed
```

and native sound functions consume script-domain timing.

Current OpenNomad maintains:

```text
instance.elapsed_script_frames
```

which is a reasonable semantic clock.

When exact `PlaySyncSound` parity is tested, verify which Runtime elapsed level
the function actually reads in each path.

---

# 121. Message callback globals

Runtime globals:

```text
0x0067A0B4
    g_scriptMessageCallback

0x0067A0B8
    g_scriptMessageTarget
```

support:

```text
Script_SendMessage
```

and related message dispatch.

This is structured-script native subsystem state, not part of the compact AREA
VM stack.

Function-specific semantics remain in `iam-script-functions.md`.

---

# 122. Parameter access helpers

Runtime uses:

```text
0x0044C680
```

as a raw command-parameter accessor.

`Script_GetNumParam()` at:

```text
0x0044C090
```

maps semantic parameter selectors to raw slots for known function IDs.

This reinforces that:

```text
valueCount
```

is raw storage width, while:

```text
semantic parameters
```

are a function-specific API layer.

---

# 123. Function-local mutation

Native handlers can mutate:

```text
command executionCount
command parameter words
referenced world resources
script/context state
```

A serializer/debugger should therefore never assume command arguments are
immutable after load.

OpenNomad's `ScriptValue` raw/int/float views are useful for inspecting these
mutations.

---

# 124. Script list loading versus activation

Loading an SCX establishes:

```text
primary script records
commands
resources
bindings
```

but does not mean every script immediately performs its authored gameplay
effect.

Primary scripts have runtime/context gating and activation state.

AREA/world logic selects and reinitializes scripts by ID.

Thus:

```text
present in Grid.SCX
```

does not imply:

```text
run unconditionally at SCX load
```

---

# 125. Why primary scripts are still iterated every frame

`Script_PlayScriptList()` calls every primary `0x64` record.

Inactive/gated scripts cheaply return statuses such as:

```text
2
4
```

or take context-gated paths.

The scheduler therefore uses:

```text
iterate broad primary array
+
per-script internal activation state
```

rather than maintaining only a list of currently active primary pointers.

---

# 126. Runtime clone instances are automatically ephemeral

For clone slots:

```text
Script_PlayScript() == 0
    ->
Script_RemoveInstance()
```

This creates an explicit ephemeral lifetime:

```text
clone
run
complete
destroy
slot reusable
```

Primary scripts, in contrast, survive for the ScriptList lifetime.

---

# 127. Instance removal while iterating

`Script_PlayScriptList()` iterates the fixed slot array by address/stride.

When a clone returns zero, it zeroes that slot and continues to the next
`0x64` address.

Because the array does not compact, removal does not invalidate iteration.

This is one reason Runtime's high-water/fixed-slot architecture is simple and
robust for raw pointers.

---

# 128. Modern vector erasure should not emulate compaction accidentally

If OpenNomad stores clones in a vector and erases completed entries, external
activation handles/IDs must remain stable.

Prefer:

- stable IDs;
- generation handles;
- optional/tombstone slots;
- or a separate stable container.

Do not let modern container compaction introduce behavior that Runtime's fixed
slots did not have.

---

# 129. Script reactivation while inactive

The generic activation path:

```text
find primary script
Script_Reinit
configure context
```

means a primary script with:

```text
+0x1C == 0
```

can later be reactivated without allocating a new script object.

OpenNomad should model this explicitly if primary/clone topology is adopted.

---

# 130. Related-script relationship is primary-script oriented

`Script_MakeInstance()` clears clone:

```text
+0x54 relatedScript = NULL
```

while primary `Script_PlayScript()` implements the `+0x54/+0x5D` handoff.

This strongly suggests the related-script pairing is primarily a feature of the
loaded primary script graph rather than arbitrary independent clones.

That is another reason clone and primary state should remain distinct concepts.

---

# 131. Exact clone callsite remains a key unknown

Despite having a strongly recovered `Script_MakeInstance()` implementation,
the normal gameplay path that invokes it has not yet been identified.

High-value targets:

- indirect function-pointer dispatches;
- special IAM native functions;
- object/effect instancing systems;
- debug/editor paths;
- related-script operations;
- legacy paths not reached by the main current-format switch.

Until then, do not force clone creation into the AREA launch architecture.

---

# 132. ScriptList maximum instance count source remains unresolved

We know:

```text
+0x50 = max clone slots
```

from allocator behavior.

We do not yet have a fully documented source for how the value is chosen:

- SCX header/section field;
- runtime configuration;
- fixed default;
- scenario-specific allocation.

Recovering ScriptList construction will settle this.

---

# 133. ScriptList `+0x34` resource table remains partial

Clone sprite setup and cleanup index a table from around:

```text
ScriptList +0x34
```

with an apparent:

```text
0x24-byte
```

stride per indexed resource entry.

The exact owning structure/type is not yet sufficiently recovered.

Do not label it simply:

```text
sprite table
```

because it may represent a broader script resource/instance descriptor set.

---

# 134. ScriptList `+0x5C` remains partial

`Script_PlayScript()` dereferences:

```text
script->owner
```

then uses:

```text
owner +0x5C
```

during external/context selection.

Its role is real and central but not yet narrowly named.

Keep:

```text
contextState
```

or another neutral working label.

---

# 135. Runtime primary/instance diagram

```text
                       SCX DEAD0002
                            |
                            v
                  loaded Runtime ScriptList
                            |
          +-----------------+------------------+
          |                                    |
          v                                    v
  primary script array                 clone slot array
  list+0x0C                            list+0x54
  count list+0x08                      extent list+0x4C
  stride 0x64                          max list+0x50
          |                                    |
          |                           Script_MakeInstance
          |                                    |
          |                           clone commands/params
          |                           private sprite state
          |                                    |
          +------------------+-----------------+
                             |
                             v
                    Script_PlayScriptList
                    0x0044CC50
                             |
          +------------------+------------------+
          |                                     |
          v                                     v
 Script_PlayScript(primary)            Script_PlayScript(clone)
          |                                     |
          |                            result == 0 ?
          |                                     |
          |                                     v
          |                            Script_RemoveInstance
          |
          v
 primary record remains resident
```

---

# 136. Scheduler diagram

```text
Script_PlayScript
    |
    +-- validate script/state/related gate
    |
    +-- select external context
    |
    +-- root = roots[currentRootIndex]
    |
    v
execution-limit eligibility
    |
    v
dispatch root
    |
    +-- if contributing:
    |      BL |= AL
    |
    v
follow SyncFunction
    |
    +-- eligibility
    +-- native dispatch
    +-- optional BL |= AL
    |
    v
end chain
    |
    +-- BL != 0
    |      remain group
    |
    +-- BL == 0
           |
           v
       currentRootIndex++
           |
           +-- more roots
           |      keep script active
           |
           +-- no more roots
                  |
                  v
             repeatIndex++
                  |
                  +-- repeat?
                  |      Script_Reinit
                  |
                  +-- exhausted
                         deactivate / related handoff
```

---

# 137. AREA launch diagram

```text
AREA VM
    |
    +-- 0x5A current-character fire-and-forget
    |       |
    |       v
    |   character/world-bound activation
    |
    +-- 0x2E current-character tracked
    |       |
    |       v
    |   character/world-bound activation
    |   AREA state = 4
    |
    +-- 0x39 generic fire-and-forget
    |       |
    |       v
    |   activate/reinit primary SCX script
    |
    +-- 0x3A generic tracked
    |       |
    |       v
    |   activate/reinit primary SCX script
    |   AREA state = 4
    |
    +-- 0x3B character fire-and-forget
    |       |
    |       v
    |   character/world-bound activation
    |
    +-- 0x3C character tracked
            |
            v
        character/world-bound activation
        AREA state = 4
```

No arrow to `Script_MakeInstance()` should be drawn until a real call path is
found.

---

# 138. OpenNomad current model diagram

```text
parsed ScxData
    immutable
        |
        v
ScriptRuntime::create_instance()
        |
        +-- deep-copy whole shared value pool
        +-- copy command vectors
        +-- modern launch metadata
        +-- sprite remap map
        |
        v
vector<ScriptInstance>
        |
        v
advance_instance()
        |
        +-- root + linked traversal
        +-- exhaustion-based group completion
        +-- structured debugger pauses
```

This is safe and inspectable, but differs from Runtime in several key semantic
areas.

---

# 139. OpenNomad mismatch summary

Current important mismatches:

```text
1. Primary ownership
   Runtime:
       loaded primary scripts are mutable and directly scheduled
   OpenNomad:
       parsed definitions are treated as immutable and activations create instances

2. Clone parameter storage
   Runtime:
       per-command private parameter allocations
   OpenNomad:
       one complete value-pool copy per instance

3. Group completion
   Runtime:
       eligibility + selected handler AL/BL active result
   OpenNomad:
       all commands exhausted

4. AREA 0x39
   Runtime:
       fire-and-forget
   older/current OpenNomad path:
       tracked/waits

5. Clone creation
   Runtime:
       Script_MakeInstance exists as a separate specialized mechanism
   OpenNomad:
       generic instance creation represents most activations

6. Fixed slots
   Runtime:
       bounded 0x64 clone slot array + high-water extent
   OpenNomad:
       dynamic vector/IDs

7. Debug pauses/budget
   Runtime:
       not recovered as equivalent scheduler states
   OpenNomad:
       explicit modern safety/debug system
```

---

# 140. Recommended implementation priority 1 — scheduler semantics

First fix the group scheduler without changing ownership architecture.

For each command:

```text
if not eligible:
    skip native call
else:
    dispatch
```

For contributing function IDs:

```text
groupActive |= handlerRemainsActive
```

For immediate functions:

```text
do not affect groupActive
```

After full root+SyncFunction chain:

```text
groupActive == false
    -> advance root
```

This can be implemented in the current modern `ScriptInstance` representation.

---

# 141. Recommended implementation priority 2 — AREA tracking pairs

Correct the launch pairs:

```text
0x39 non-tracked generic
0x3A tracked generic

0x3B non-tracked character
0x3C tracked character
```

Then make AREA state 4 wait on a concrete modern activation token.

Do not infer tracking from script ID or opcode parity elsewhere.

---

# 142. Recommended implementation priority 3 — primary activation state

Introduce one mutable primary runtime state per loaded structured script.

AREA generic activation should:

```text
find by script ID
reinitialize
apply context metadata
activate
```

rather than automatically allocating an unrelated clone.

This will better match:

```text
0x0041DC10
0x0044CD10
Script_PlayScriptList
```

---

# 143. Recommended implementation priority 4 — true clone instances

Keep a separate clone mechanism modeled after `Script_MakeInstance`.

It can use modern memory ownership but should preserve:

- source must be a primary script;
- independent commands;
- independent mutable parameters;
- rebuilt SyncFunction links;
- instance-owned sprites/resources;
- automatic cleanup on completion;
- configured compatibility maximum if relevant.

Only connect gameplay operations to it once Runtime's caller is identified.

---

# 144. Recommended implementation priority 5 — parameter ownership audit

Scan retail SCX for:

```text
overlapping parameter ranges between commands
```

If no overlaps occur, whole-pool instance copies are semantically equivalent
for valid data.

If overlaps occur, compare against Runtime clone behavior and decide whether
OpenNomad must move to command-local parameter vectors.

This is a data-driven way to avoid unnecessary refactoring.

---

# 145. Recommended implementation priority 6 — context bytes

Recover and model:

```text
+0x5C
+0x5D
+0x5E..+0x61
+0x1E context bits
0x00903AE0
```

before declaring structured script activation fully faithful.

These fields appear central to primary script activation/reentrancy and
related-script handoff.

---

# 146. Recommended tests — ScriptList topology

- [ ] primary script count/base use `+0x08/+0x0C`;
- [ ] primary stride is `0x64`;
- [ ] clone stride is `0x64`;
- [ ] clone extent uses `+0x4C`;
- [ ] clone maximum uses `+0x50`;
- [ ] clone base uses `+0x54`;
- [ ] removing a clone does not compact/decrement extent;
- [ ] Script_PlayScriptList services primaries before clones;
- [ ] primary zero result does not destroy primary record;
- [ ] clone zero result destroys clone slot.

---

# 147. Recommended tests — cloning

- [ ] source must belong to primary script range;
- [ ] `0x64` state is copied;
- [ ] clone `+0x54` related pointer is cleared;
- [ ] root command array cloned;
- [ ] linked command array cloned;
- [ ] command params privately cloned;
- [ ] SyncFunction points only inside clone linked array;
- [ ] current root/repeat/elapsed reset;
- [ ] function-specific reinit runs;
- [ ] sprite-owning commands acquire private sprite state;
- [ ] clone cleanup destroys private sprites;
- [ ] slot becomes reusable after zeroing.

---

# 148. Recommended tests — scheduling

- [ ] finite command skipped when `count >= limit`;
- [ ] `0xFFFFFFFF` remains eligible;
- [ ] immediate function return does not block group;
- [ ] contributing AL=1 blocks group;
- [ ] contributing AL=0 permits advance if no other contributor remains active;
- [ ] every reachable SyncFunction is considered;
- [ ] root index advances once per completed group;
- [ ] next root executes on later scheduler call;
- [x] repetition calls generic reinit;
- [x] `-1` repeat limit repeats indefinitely;
- [ ] exhausted repetition produces zero normal completion result.

---

# 149. Recommended tests — status/lifetime

- [ ] null script produces `0xFF` equivalent error;
- [ ] state `+0x1C==0` follows inactive return semantics;
- [ ] related-script gate does not masquerade as normal completion;
- [ ] clone removal is keyed to normal zero completion;
- [ ] related script handoff flips `+0x5D` states;
- [ ] primary script remains allocated after completion;
- [ ] clone is destroyed after zero completion.

Modern code does not need to expose literal numeric return bytes publicly if
tests can assert equivalent semantics.

---

# 150. Recommended tests — timing

- [ ] script-frame delta is 30-Hz-unit based;
- [ ] script elapsed clock advances once per scheduler call;
- [ ] ScriptList elapsed clock advances once after list service;
- [ ] paused OpenNomad debugger state does not mutate script clocks;
- [ ] exact handler timing uses script frames, not seconds;
- [ ] no double conversion at world/ScriptRuntime boundary.

---

# 151. Recommended tests — AREA bridge

- [x] `0x39` launches and immediately continues AREA VM;
- [x] `0x3A` launches and enters state 4;
- [x] `0x3B` preserves explicit character ID + camera-duration metadata and continues;
- [x] `0x3C` preserves explicit character ID + camera-duration metadata and enters state 4;
- [x] `0x5A` selects the session current character and immediately continues;
- [x] `0x2E` selects the session current character and waits on the exact child in state 4;
- [x] script ID is raw 16-bit lookup against `+0x1A`;
- [x] tracked completion is tied to the concrete activation, not “any script
      finished”;
- [ ] primary script reactivation invokes reinit;
- [ ] no test assumes AREA launch necessarily calls `Script_MakeInstance`.

---

# 152. Recommended debugger representation

A useful debug panel should show both conceptual populations:

```text
Primary scripts
    ID
    name
    state +1C
    root index
    repeat index
    elapsed
    context bytes
    related gate
    active result

Clone instances
    slot / modern instance ID
    source primary ID
    root index
    repeat
    elapsed
    owned sprites/sounds
    completion state
```

This would make Runtime topology visible during intro reverse engineering.

---

# 153. Trace useful scheduler facts, not only handlers

Per tick/command trace should include:

```text
primary vs clone
script ID/name
root index
chain position
function ID/name

executionLimit
executionCount
eligible yes/no

contributesToGroup yes/no
handlerActive yes/no
groupActive before/after

parameter mutations
repeat transition
script final result
```

This will make scheduler mismatches much easier to diagnose than a generic
“command completed” status.

---

# 154. Correct provenance for `Script_MakeInstance`

Recommended source comment update:

```cpp
// Runtime Script_MakeInstance begins at 0x004A0260.
// It clones a primary 0x64 script into the ScriptList's separate instance
// slot pool; it is NOT the Script_PlayScript execution-limit precheck.
```

Any current comment saying:

```text
Shared execution-limit precheck (Runtime 0x004A0260 region)
```

should be removed/corrected.

---

# 155. Recommended Ghidra labels

High-confidence:

```text
0044C860  Script_PlayScript
0044CC50  Script_PlayScriptList

0044CD10  Script_FindById
0044CD40  Script_GetId
0044CD50  Script_GetModeFlagBit1
0044CD60  Script_SetModeFlagBit1

0044A7E0  Script_Reinit

004A0260  Script_MakeInstance
004A0C30  Script_RemoveInstance
004A0CC0  Script_RemoveAllInstances
004A0DA0  Script_CleanupInstanceResources

0044C090  Script_GetNumParam
0044C680  Script_GetRawParam
```

`Script_CleanupInstanceResources` is a reconstructed role, not a recovered
symbol.

---

# 156. AREA bridge labels

```text
004030E0  Scenario_StartScxScript
004031E0  Scenario_StartScxScriptTracked

00403300  Scenario_StartCharacterScript
00403430  Scenario_StartCharacterScriptTracked

0041DC10  StructuredScript_ActivatePrimary
          working name

0041D9C0  StructuredScript_IsContextActive
          working name
```

The last two names are reconstructed from behavior.

---

# 157. Key globals

```text
00531218
    g_scriptFrameDelta

0067A0B4
    g_scriptMessageCallback

0067A0B8
    g_scriptMessageTarget

00903AE0
    structured-script context selector/mode
    exact enum unresolved

00903B00
    ScriptList registry/array region

00531260
    ScriptList registry count/state

00910500
    structured-script activation tracking records
    exact schema unresolved
```

Only the first three already have strong public working names.

---

# 158. Key RuntimeScript fields

```text
+00 owner ScriptList

+04 name[22]
+1A script ID

+1C runtime state
+1E composite flags/status/context

+20 root count
+24 current root
+28 root pointer

+2C linked count
+30 linked pointer

+34 repeat limit
+38 repeat index

+3C..50 binding descriptors

+54 related script
+58 elapsed script frames

+5C mode flags
+5D related/pair gate
+5E..61 context-active bytes
```

---

# 159. Key RuntimeScriptCommand fields

```text
+00 32-bit IAM function ID
+04 value count
+08 parameter pointer
+0C SyncFunction pointer
+10 execution limit
+14 execution count
```

Scheduler rules:

```text
eligible:
    limit == FFFFFFFF
    or count < limit

group blocking:
    selected handler AL -> OR into BL

advance root:
    BL == 0
```

---

# 160. Compact RuntimeScriptList reference

```text
+08 primary script count
+0C primary script array

+34 script resource/instance table, partial

+4C clone slot extent/high-water
+50 clone slot maximum
+54 clone slot array

+58 list elapsed script frames
+5C external/context state, partial
```

Primary and clone script stride:

```text
0x64
```

Command stride:

```text
0x18
```

---

# 161. Compact lifecycle reference

```text
SCX load
    ->
mutable primary script array

every engine frame
    ->
Script_PlayScriptList
        ->
        every primary Script_PlayScript
        ->
        every clone Script_PlayScript
            result 0 -> RemoveInstance

AREA 0x39/0x3A
    ->
find primary by ID
reinit/configure context
activate

Script_MakeInstance
    ->
separate optional clone mechanism
caller still unresolved
```

---

# 162. Boundary of current knowledge

Strongly recovered:

```text
primary scripts are directly mutable/playable
separate 0x64 clone slot array
ScriptList +08/+0C primary count/base
ScriptList +4C/+50/+54 clone extent/max/base
ScriptList +58 elapsed clock
Script_MakeInstance identity/address
per-command parameter cloning
clone SyncFunction rebuilding
clone sprite ownership
Script_RemoveInstance
Script_RemoveAllInstances
Script_PlayScriptList
primary-before-clone scheduling
automatic clone destruction on zero return
Script_PlayScript eligibility gate
handler AL -> BL group blocking
root advancement
repeat/reinit lifecycle
script +58 elapsed clock
related-script +54/+5D handoff
four context-active bytes +5E..+61
AREA 0x39/0x3A and 0x3B/0x3C tracked pairs
generic AREA activation uses primary script lookup/reinit
```

Still incomplete:

```text
exact original RuntimeScriptList full layout
source of max clone slot count
direct gameplay caller of Script_MakeInstance
full meaning of ScriptList +34 resource table
full meaning of ScriptList +5C
complete +1C state enum
complete +1E bitfield
+5C bit-1 semantic name
identity of four +5E..+61 contexts
exact 0x00903AE0 enum
all activation tracking-record fields at 0x00910500
exact related-script authoring relationship
complete return-code enum for Script_PlayScript
all native command executionCount update rules
```

The central architectural takeaway is:

> The structured SCX runtime is not simply an immutable collection of script
> templates instantiated on demand. Each loaded ScriptList contains a mutable
> **primary script array** that Runtime services directly every engine frame,
> plus a separate bounded pool of optional **cloned script instances** created
> by `Script_MakeInstance`. `Script_PlayScript()` schedules one root plus its
> `SyncFunction` chain using two independent mechanisms: execution-count
> gating decides whether a command is eligible to run, while selected native
> handlers return an `AL` active value that is ORed into a group-level `BL`
> accumulator. Only when that accumulator becomes zero does Runtime advance to
> the next root group. AREA opcodes `0x39..0x3C` activate/reinitialize primary
> scripts through world/context machinery; they should not be equated with
> `Script_MakeInstance` until an actual call path proves that relationship.
