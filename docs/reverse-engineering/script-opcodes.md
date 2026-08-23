# SCX `DEAD0002` structured-script serialization

> **Status:** work-in-progress reverse-engineering reference for OpenNomad  
> **Last updated:** 2026-08-22
>
> The filename `script-opcodes.md` is retained for repository continuity, but
> the name is historically imprecise. The values stored in SCX command records
> are **32-bit IAM / Quantic C function IDs**, not bytecode opcodes.
>
> This document is the serialization/ABI reference for the structured script
> section (`DEAD0002`) of SCX version 5. It intentionally stops at the boundary
> where serialized records become live Runtime script state.
>
> Use the other dedicated documents for execution semantics:
>
> - [`script-runtime.md`](script-runtime.md) — Runtime `ScriptList` ownership,
>   primary scripts versus cloned instances, `Script_PlayScript`, scheduling,
>   repetition, activation, and AREA launch integration;
> - [`iam-script-functions.md`](iam-script-functions.md) — the 32-bit IAM
>   function catalogue, native action/reinit handlers, parameter roles, and
>   per-function behavior;
> - [`iam-scenario-vm.md`](iam-scenario-vm.md) — the **separate** compact
>   byte-oriented AREA/START virtual machine and its one-byte opcodes;
> - [`scx.md`](scx.md) — the complete SCX v5 container, descriptor chunks, and
>   appended resource stream.
>
> This split replaces the previous version of this file, which had gradually
> become a second copy of the scheduler and AREA VM documentation.

---

# 1. Scope

This file is authoritative for:

```text
SCX tag DEAD0002
fixed 0x64 script records
shared 32-bit value pool
fixed 0x18 function/command records
root-command arrays
linked/SyncFunction arrays
related-script serialized block
binding-table serialized blocks
index -> pointer relocation
pointer -> index conversion
format validation
retail corpus examples
```

It is **not** authoritative for:

```text
Script_PlayScript scheduling
handler AL/BL blocking semantics
Script_MakeInstance ownership
script activation/reentrancy
AREA VM opcode decoding
AREA wait-state behavior
individual Script_* function semantics
```

Those topics now have dedicated documents.

---

# 2. Evidence model

Sources are ranked:

1. **direct `Runtime.exe` machine-code behavior;**
2. **retail `Grid.SCX` and `aventure.SCX`;**
3. **current OpenNomad parser implementation;**
4. older reverse-engineering notes.

Reference executable:

```text
File:
    Runtime.exe

Supplied analysis file:
    Runtime(1).exe

Architecture:
    PE32 / i386

Image base:
    0x00400000

Linker timestamp:
    1999-10-04 20:31:50

SHA-256:
    55f7120bfea7891b048c64e3682f3259cdbf2719a43fa24e42254b753c95d2ef
```

Confidence labels:

- **Confirmed — Runtime:** directly established from executable behavior.
- **Confirmed — data:** directly established from retail serialized data.
- **Corroborated:** Runtime and data independently agree.
- **Strongly reconstructed:** several independent observations agree, but no
  original source-level symbol survives.
- **Provisional:** useful working interpretation requiring more tracing.
- **Unknown:** structure is known but semantics are not.
- **OpenNomad-only:** modern parser/runtime representation rather than original
  memory ownership.

---

# 3. Terminology

The structured SCX system is easiest to discuss with four distinct terms.

## 3.1 Script definition

One serialized `0x64` record plus its variable auxiliary data:

```text
fixed script record
root functions
linked functions
related-script block
binding table A
binding table B
```

## 3.2 Function record

One serialized `0x18` command/action record containing a 32-bit IAM function
ID.

Runtime diagnostics frequently call these:

```text
Function
SyncFunction
FuncParams
```

`command` remains a convenient OpenNomad term, but `function record` is closer
to the original Runtime vocabulary.

## 3.3 Root function

One entry of the script's root-function array.

The current root selects the synchronized group being serviced by the runtime.
The actual scheduler belongs in `script-runtime.md`.

## 3.4 SyncFunction / linked function

A function record reachable through the `+0x0C` link field.

Runtime diagnostics explicitly use:

```text
SyncFunction
```

The serialized form stores an index; the loaded form stores a pointer.

---

# 4. This is not bytecode

A structured script function is not encoded as:

```text
opcode byte
operand bytes
next opcode
```

Instead, the file stores fixed-width records:

```text
0x18-byte function record
    |
    +-- 32-bit IAM function ID
    +-- number of parameter words
    +-- index of first parameter word
    +-- linked-function index
    +-- execution limit
    +-- execution count
```

Example function IDs:

```text
0x0200002A  Script_SelectRelativeBodyAnimation
0x04000029  Script_SetSpriteFrame
0x05000015  Script_PlaySyncSound
0x06000017  Wait
```

The low byte alone is **not** sufficient for dispatch.

For the real bytecode-like VM, see:

[`iam-scenario-vm.md`](iam-scenario-vm.md).

---

# 5. SCX v5 container context

SCX v5 begins:

```c
struct ScxHeaderV5 {
    uint32_t magic;           // 0x00DEAD00
    uint32_t version;         // 5
    uint32_t field08;         // observed 8
    uint32_t descriptorSize;
}; // 0x10
```

Descriptor/tag block:

```text
start = file + 0x10
end   = file + 0x10 + descriptorSize
```

`DEAD0002` lives entirely inside this descriptor block.

It owns **no appended resource** in the later SCX resource stream.

---

# 6. `DEAD0002` tag

Tag value:

```text
0xDEAD0002
```

In both supplied retail packages it happens to be the first descriptor tag:

```text
Grid.SCX:
    DEAD0002 at file offset 0x10

aventure.SCX:
    DEAD0002 at file offset 0x10
```

This position is not a general parser invariant.

SCX sections must still be dispatched by tag value.

---

# 7. Complete serialized grammar

After the `DEAD0002` tag:

```text
u32 scriptCount

SerializedScriptV5 scripts[scriptCount]      // 0x64 each

u32 sharedValueCount
u32 sharedValues[sharedValueCount]

for script in scripts:
    u8 relatedScriptPresent

    if relatedScriptPresent != 0:
        char relatedScriptName[21]

    SerializedScriptFunction rootFunctions[rootFunctionCount]
                                                // 0x18 each

    SerializedScriptFunction linkedFunctions[linkedFunctionCount]
                                                // 0x18 each

    SerializedBindingBlock bindingTableA
    SerializedBindingBlock bindingTableB
```

Binding block:

```text
u32 count
u32 pointerStorage[count]
u32 runtimeStorage[count]
char names[count][21]
```

This is the full currently recovered `DEAD0002` grammar.

---

# 8. Section-size formula

Let:

```text
N  = scriptCount
V  = sharedValueCount
Ri = rootFunctionCount of script i
Li = linkedFunctionCount of script i
Ai = binding A count of script i
Bi = binding B count of script i
Pi = 1 when related-script name is present, otherwise 0
```

Then the bytes **after the 4-byte `DEAD0002` tag** are:

```text
4
+ N * 0x64
+ 4
+ V * 4
+ sum_i (
      1
    + Pi * 21
    + Ri * 0x18
    + Li * 0x18
    + 4 + Ai * (8 + 21)
    + 4 + Bi * (8 + 21)
  )
```

or:

```text
4
+ N * 100
+ 4
+ V * 4
+ sum_i (
      1
    + Pi * 21
    + (Ri + Li) * 24
    + 8
    + (Ai + Bi) * 29
  )
```

This makes the section self-delimiting from its nested counts.

No separate section byte length follows the tag.

---

# 9. Supplied package section extents

## 9.1 `Grid.SCX`

```text
DEAD0002 tag:
    0x0010

payload begins:
    0x0014

next descriptor tag:
    0x07DB
    DEAD0000

bytes including DEAD0002 tag:
    0x7CB

payload bytes after tag:
    0x7C7
```

## 9.2 `aventure.SCX`

```text
DEAD0002 tag:
    0x0010

payload begins:
    0x0014

next descriptor tag:
    0x189A
    DEAD0000

bytes including DEAD0002 tag:
    0x188A

payload bytes after tag:
    0x1886
```

These extents agree exactly with the recovered nested grammar.

---

# 10. Fixed script record — `0x64` bytes

Serialized structure:

```c
struct SerializedScriptV5 {
    uint32_t ownerPlaceholder;             // +0x00

    char     name[22];                     // +0x04 .. +0x19
    uint16_t scriptId;                     // +0x1A

    uint16_t runtimeState;                 // +0x1C
    uint16_t flagsAndStatus;               // +0x1E

    uint32_t rootFunctionCount;            // +0x20
    uint32_t currentRootIndex;             // +0x24
    uint32_t rootFunctionsPlaceholder;     // +0x28

    uint32_t linkedFunctionCount;          // +0x2C
    uint32_t linkedFunctionsPlaceholder;   // +0x30

    int32_t  repeatLimit;                  // +0x34
    uint32_t repeatIndex;                  // +0x38

    uint32_t bindingAFields[3];            // +0x3C .. +0x44
    uint32_t bindingBFields[3];            // +0x48 .. +0x50

    uint32_t relatedScriptPlaceholder;     // +0x54
    uint32_t elapsedTimeBits;              // +0x58

    uint8_t  runtimeTail[8];               // +0x5C .. +0x63
}; // 0x64
```

This structure deliberately uses `Placeholder` for pointer-shaped fields.

Retail files preserve values that resemble addresses from a build/editor
memory image, but Runtime overwrites them during loading.

---

# 11. Fixed script record summary

| Offset | Size | Serialized interpretation | Loaded Runtime interpretation |
|---:|---:|---|---|
| `+0x00` | 4 | owner/list pointer-shaped placeholder | `ScriptList*` |
| `+0x04` | 22 | script name | script name |
| `+0x1A` | 2 | script ID | script ID |
| `+0x1C` | 2 | serialized runtime-state seed | mutable runtime state |
| `+0x1E` | 2 | flags/status seed | composite mutable flags/status/context |
| `+0x20` | 4 | root-function count | root-function count |
| `+0x24` | 4 | current-root seed | mutable root cursor |
| `+0x28` | 4 | pointer-shaped placeholder | root-function pointer |
| `+0x2C` | 4 | linked-function count | linked-function count |
| `+0x30` | 4 | pointer-shaped placeholder | linked-function pointer |
| `+0x34` | 4 | repeat limit | repeat limit |
| `+0x38` | 4 | repeat-index seed | mutable repeat index |
| `+0x3C` | 12 | binding A descriptor seed | live binding A descriptor |
| `+0x48` | 12 | binding B descriptor seed | live binding B descriptor |
| `+0x54` | 4 | related-pointer placeholder | related script pointer |
| `+0x58` | 4 | elapsed-time seed bits | mutable `float` elapsed script time |
| `+0x5C` | 8 | runtime-tail seed bytes | mutable mode/pair/context state |

Runtime lifecycle semantics for the mutable fields are documented in
`script-runtime.md`.

---

# 12. `+0x00` — owner placeholder

Runtime's `DEAD0002` loader writes the owning ScriptList pointer directly to:

```text
script +0x00
```

The serialized value is therefore not a process pointer that a modern loader
may trust.

Supplied package examples:

```text
Grid.SCX:
    every script +0x00 = 0x00668D68

aventure.SCX:
    every script +0x00 = 0x00596C60
```

Those values are useful historical evidence that the serialized record was
produced from a pointer-rich in-memory/editor structure.

They are not valid relocation addresses in a new process.

---

# 13. `+0x04..+0x19` — script name

Width:

```text
22 bytes
```

This is a fixed-size C-style string field.

Modern parsing should:

```text
scan for NUL within 22 bytes
otherwise preserve all 22 bytes as the bounded name
```

Older notes that described the name as 20 bytes plus an unknown word at
`+0x18` are obsolete.

---

# 14. `+0x1A` — 16-bit script ID

Width:

```text
uint16_t
```

Runtime's simple primary-script lookup at:

```text
0x0044CD10
```

walks the ScriptList's `0x64` records and compares:

```text
WORD [script + 0x1A]
```

against the requested ID.

Helper:

```text
0x0044CD40
```

returns the same field directly.

AREA structured-script launch instructions use this ID namespace.

See `iam-scenario-vm.md` and `script-runtime.md` for activation semantics.

---

# 15. Correction: old `0x0044A0F0` lookup label

Older versions of this document described:

```text
0x0044A0F0
```

as the simple script-ID lookup.

That label is too narrow and should be removed.

Direct disassembly shows `0x0044A0F0` searches for **two** script IDs and then
examines their runtime state/flags.

The straightforward one-ID lookup is:

```text
0x0044CD10
```

This is a documentation correction, not a change to the serialized field at
`+0x1A`.

---

# 16. `+0x1C` — mutable runtime state seed

The field is serialized as a 16-bit value.

In both supplied packages every script contains:

```text
+0x1C = 1
```

During `DEAD0002` loading Runtime calls helper:

```text
0x0044AA20
```

for each primary script.

That helper immediately performs:

```text
WORD [script +0x1C] = 0
```

Therefore the serialized `1` is not a stable “start executing immediately”
semantic that a modern parser should apply literally.

Treat the on-disk field as:

```text
serialized runtime-state seed / memory-image residue
```

while preserving the raw value for format fidelity.

The live state machine belongs in `script-runtime.md`.

---

# 17. `+0x1E` — flags/status seed

The same loader helper:

```text
0x0044AA20
```

performs:

```text
WORD [script +0x1E] &= 0xFFF0
```

Thus:

```text
low nibble:
    cleared at load
    runtime status/state

upper 12 bits:
    preserved by this initialization step
```

Both supplied packages serialize:

```text
+0x1E = 0
```

for all examined scripts.

Runtime later uses the field as a composite flags/status/context word.

Do not flatten it into one Boolean.

---

# 18. `+0x20` — root-function count

```text
uint32_t rootFunctionCount
```

Controls the number of `0x18` root records in the variable per-script block.

The fixed record does **not** contain those records inline.

In both supplied SCX files every current script happens to have:

```text
rootFunctionCount = 1
```

That is a corpus observation, not a format invariant.

---

# 19. `+0x24` — current root index

This is the mutable scheduler cursor in the loaded Runtime record.

Supplied files serialize:

```text
0
```

for every examined script.

Its execution semantics belong in `script-runtime.md`.

For format parsing it should be preserved as the serialized initial value.

---

# 20. `+0x28` — root-function pointer placeholder

On disk this field contains pointer-shaped values.

Examples from `Grid.SCX`:

```text
1KaylArrives:
    0x00A00B40

2KaylStand:
    0x00A014B0

3KaylLeaves:
    0x00A01620
```

Runtime does not dereference those serialized values.

During section parsing it overwrites:

```text
script +0x28
```

with the actual address of the root-function array in the loaded descriptor
buffer.

Modern parser representation:

```text
root_command_count
root_commands_placeholder   // preserve raw for RE/round-trip
root_commands[]             // parsed semantic array
```

is appropriate.

---

# 21. `+0x2C` — linked-function count

```text
uint32_t linkedFunctionCount
```

Controls the number of `0x18` linked / `SyncFunction` records following the
root records in the variable per-script block.

A script can have zero linked functions.

---

# 22. `+0x30` — linked-function pointer placeholder

Serialized pointer-shaped field.

Runtime overwrites it with the actual loaded linked-array address.

The serialized value must never be treated as a valid process address.

---

# 23. `+0x34` — whole-script repeat limit

Runtime scheduler behavior now establishes this field as:

```text
int32_t repeatLimit
```

Special value:

```text
-1 / 0xFFFFFFFF
    infinite repetition
```

This is distinct from each command record's:

```text
executionLimit at +0x10
```

Supplied examples:

```text
Grid.SCX 1KaylArrives:
    repeatLimit = 1

Grid.SCX 2KaylStand:
    repeatLimit = -1

Grid.SCX Wait5sec:
    repeatLimit = -1
```

OpenNomad models this field directly as:

```text
ScxScript::repeat_limit
```

---

# 24. `+0x38` — repeat index/count

Runtime increments this field at whole-script end and compares it against:

```text
+0x34 repeatLimit
```

Safe name:

```text
repeatIndex
```

or:

```text
repeatCount
```

Supplied packages serialize:

```text
0
```

for every examined script.

OpenNomad preserves the serialized seed as:

```text
ScxScript::initial_repeat_index
```

and copies it into mutable `ScriptInstance::repeat_index` state.

---

# 25. `+0x3C..+0x50` — two binding descriptors

The fixed record contains two adjacent `0x0C` descriptors:

```c
struct SerializedBindingDescriptor {
    uint32_t count;                 // +0x00
    uint32_t pointerArrayPlaceholder;// +0x04
    uint32_t runtimeArrayPlaceholder;// +0x08
}; // 0x0C
```

Within the script record:

```text
binding A:
    +0x3C count
    +0x40 pointer-array placeholder
    +0x44 runtime-array placeholder

binding B:
    +0x48 count
    +0x4C pointer-array placeholder
    +0x50 runtime-array placeholder
```

The actual variable binding blocks still follow the command arrays.

---

# 26. Binding descriptor count duplication

Each binding count appears twice in the serialized script section:

```text
fixed script descriptor:
    script +0x3C or +0x48

variable binding block:
    leading u32 count
```

Across both supplied packages the values agree exactly for every script.

OpenNomad should validate this agreement if it wants strict retail-format
checking.

A tolerant parser may prefer the variable block's count as the immediate
physical-layout authority while retaining the fixed value for diagnostics.

---

# 27. Fixed binding pointer placeholders

`Grid.SCX` scripts with binding table A demonstrate the memory-image character
of these fields.

Examples:

```text
1KaylArrives:
    A.count = 1
    A.+04   = 0x00A01410
    A.+08   = 0x00A01440

2KaylStand:
    A.count = 1
    A.+04   = 0x00A01580
    A.+08   = 0x00A015B0

3KaylLeaves:
    A.count = 1
    A.+04   = 0x00A01950
    A.+08   = 0x00A01980
```

Runtime replaces the pointer fields while loading.

---

# 28. `+0x54` — related-script pointer placeholder

The **serialized relationship itself is not encoded solely by this dword**.

Per-script auxiliary data contains:

```text
u8 relatedScriptPresent
if nonzero:
    char relatedScriptName[21]
```

Runtime resolves that name against the primary script array and constructs live
related-script pointers at `+0x54`.

Both supplied `Grid.SCX` and `aventure.SCX` happen to contain:

```text
relatedScriptPresent = 0
```

for every script, so the variable related-name path is confirmed principally
from Runtime parsing behavior rather than those two corpus files.

---

# 29. Related-script lookup by name

Runtime helper:

```text
0x0044B1B0
```

walks the primary script array in `0x64` strides and performs an ordinary
case-sensitive C-string comparison against:

```text
script +0x04
```

When a matching name is found, the loader can establish the relationship with
helper:

```text
0x0044B280
```

The execution/handoff semantics of the resulting pair belong in
`script-runtime.md`.

---

# 30. Related-name width differs from script-name width

Fixed script name:

```text
22 bytes
```

Serialized related-script name:

```text
21 bytes
```

Do not reuse one constant for both fields.

Current OpenNomad parser correctly uses separate widths.

---

# 31. `+0x58` — elapsed script time seed

Runtime treats this dword as a:

```text
float
```

and accumulates native script-frame delta into it during execution.

Both supplied packages serialize:

```text
0x00000000
```

for every examined script.

For an immutable file parser, preserve raw bits or expose:

```text
serialized_elapsed_time_bits
```

rather than pretending the serialized field must always be zero.

The runtime clock behavior belongs in `script-runtime.md`.

---

# 32. `+0x5C..+0x63` — runtime tail

Eight bytes remain at the end of the `0x64` record.

Runtime analysis now gives several of them live meanings, including:

```text
+0x5C mode bits
+0x5D related-script/pair gate
+0x5E..+0x61 four context-active bytes
```

Those semantics belong in `script-runtime.md` because they are execution state,
not physical section grammar.

Serialized corpus observation:

```text
Grid.SCX:
    all eight bytes zero in all 8 scripts

aventure.SCX:
    all eight bytes zero in all 22 scripts
```

Do not generalize that to every retail SCX without broader corpus evidence.

---

# 33. Shared value pool

Immediately after all fixed `0x64` script records:

```text
u32 sharedValueCount
u32 sharedValues[sharedValueCount]
```

Each value is exactly one raw 32-bit word.

Serialization assigns no intrinsic type.

Possible interpretations include:

```text
uint32_t
int32_t
IEEE-754 float bits
resource/table index
object ID
sound index
frame count
duration
mutable elapsed/progress
flags
sentinel -1
```

Type is determined by the consuming IAM function and parameter slot.

---

# 34. Do not eagerly type shared values

Correct parser representation:

```c
union-like/raw view:
    uint32 raw

interpretation helpers:
    as_unsigned()
    as_signed()
    as_float()
```

Current OpenNomad `ScriptValue` follows this model.

This is preferable to deciding at SCX parse time that a given raw word is
always a float or always an integer.

---

# 35. Shared pool is physically global to `DEAD0002`

The value count appears once for the entire structured-script section.

Every function record stores an index into that same pool.

Thus the physical representation is:

```text
DEAD0002
    |
    +-- scripts[]
    |
    +-- one sharedValues[]
    |
    +-- per-script function records referencing sharedValues[]
```

It is **not** serialized as one independent value array per script.

---

# 36. Supplied corpus value counts

```text
Grid.SCX:
    scripts      = 8
    shared values = 124

aventure.SCX:
    scripts      = 22
    shared values = 392
```

These counts are direct retail data.

---

# 37. Observed value-slice partitioning

Across the supplied `Grid.SCX` and `aventure.SCX` structured scripts:

```text
no two command value slices overlap
```

This is true both:

```text
within one script
and
between scripts
```

for these two files.

So although the file contains one physically shared pool, the examined authored
data partitions it into disjoint command slices.

This is **not yet a proven format invariant**.

It matters because Runtime clone instances allocate command-local parameter
copies, while current OpenNomad copies the entire shared pool per modern
instance. See `script-runtime.md`.

---

# 38. Function/command record — `0x18` bytes

Serialized structure:

```c
struct SerializedScriptFunction {
    uint32_t functionId;          // +0x00
    uint32_t valueCount;          // +0x04
    uint32_t firstValueIndex;     // +0x08
    int32_t  nextLinkedIndex;     // +0x0C
    uint32_t executionLimit;      // +0x10
    uint32_t executionCount;      // +0x14
}; // 0x18
```

This layout is strongly corroborated by:

- Runtime loader relocation;
- reverse relocation functions;
- scheduler accesses;
- current retail data;
- current OpenNomad parser.

---

# 39. `+0x00` — 32-bit IAM function ID

The complete 32-bit value identifies the native IAM / Quantic C action.

Common form:

```text
0xCC0000NN
```

where current evidence suggests:

```text
CC = broad subsystem/class
NN = global function ordinal
```

The detailed catalogue belongs in:

[`iam-script-functions.md`](iam-script-functions.md).

Never dispatch from only:

```text
functionId & 0xFF
```

unless the caller has independently proved the full class.

---

# 40. `+0x04` — value count

```text
uint32_t valueCount
```

Number of raw 32-bit words belonging to this function record.

Parameter span:

```text
[firstValueIndex,
 firstValueIndex + valueCount)
```

must fit inside the `DEAD0002` shared value pool.

This raw count is distinct from semantic selectors exposed by:

```text
Script_GetNumParam()
```

A native function can have internal/mutable raw words that are not surfaced as
one user-facing semantic parameter each.

---

# 41. `+0x08` — first value index

Serialized representation:

```text
uint32 index into DEAD0002 sharedValues[]
```

Loaded Runtime representation:

```text
uint32_t *FuncParams
```

The same 4-byte slot changes meaning during relocation.

This is one of the clearest examples of SCX serializing a pointer-rich Runtime
structure in index form.

---

# 42. Value-slice bounds

For every serialized function:

```text
firstValueIndex <= sharedValueCount
```

and:

```text
valueCount <= sharedValueCount - firstValueIndex
```

Equivalent end check:

```text
firstValueIndex + valueCount <= sharedValueCount
```

The subtraction form is preferable in hardened code because it avoids unsigned
addition overflow.

Current OpenNomad validates this.

---

# 43. `+0x0C` — next linked / SyncFunction index

Serialized form:

```text
int32_t nextLinkedIndex
```

Special sentinel:

```text
-1
    no next SyncFunction
```

Any other valid value is an index into the **current script's linked-function
array**.

Loaded form:

```text
RuntimeScriptFunction *SyncFunction
```

No link points directly into the root-function array through this field.

---

# 44. Linked-index bounds

For:

```text
nextLinkedIndex != -1
```

require:

```text
0 <= nextLinkedIndex < linkedFunctionCount
```

Negative values other than:

```text
-1
```

should be rejected by a modern parser.

Current OpenNomad does this.

---

# 45. `+0x10` — execution limit

Serialized and Runtime scheduler field:

```text
uint32_t executionLimit
```

Special value:

```text
0xFFFFFFFF
    unbounded / unlimited
```

Execution-limit scheduler semantics belong in `script-runtime.md`.

Do not confuse this command-local limit with script:

```text
+0x34 repeatLimit
```

---

# 46. `+0x14` — execution count

Mutable command-local execution state.

Serialized files provide an initial value.

In both supplied packages every function record has:

```text
executionCount = 0
```

Runtime handlers and reinitializers can mutate the loaded value.

---

# 47. Supplied command-limit corpus

Across:

```text
Grid.SCX:
    22 function records

aventure.SCX:
    96 function records
```

every examined command has:

```text
executionLimit = 1
executionCount = 0
```

This is useful retail evidence but **not** a format restriction.

Runtime explicitly supports:

```text
executionLimit == 0xFFFFFFFF
```

and its scheduler is written generically.

---

# 48. Root-function array

For each script, after the optional related-script name:

```text
rootFunctionCount * 0x18
```

bytes are stored.

There is no additional array count here because the count is already in the
fixed script record at:

```text
+0x20
```

Runtime writes the actual in-memory array pointer to:

```text
script +0x28
```

while loading.

---

# 49. Linked-function array

Immediately after roots:

```text
linkedFunctionCount * 0x18
```

bytes are stored.

Count source:

```text
script +0x2C
```

Runtime writes the loaded array pointer to:

```text
script +0x30
```

---

# 50. Root and linked arrays are separate namespaces

A serialized root function's:

```text
nextLinkedIndex
```

addresses the linked-function array.

A linked function's own:

```text
nextLinkedIndex
```

also addresses that same linked-function array.

Therefore a chain is conceptually:

```text
rootFunctions[currentRoot]
    |
    +-- linkedFunctions[n]
            |
            +-- linkedFunctions[m]
                    |
                    ...
```

The scheduler behavior of that chain belongs in `script-runtime.md`.

---

# 51. Serialized `-1` becomes Runtime `NULL`

Load-time conversion performs:

```text
nextLinkedIndex == -1
    ->
SyncFunction = NULL
```

The reverse conversion performs:

```text
SyncFunction == NULL
    ->
nextLinkedIndex = -1
```

This round-trip is explicit Runtime behavior.

---

# 52. Binding block — exact physical layout

Each script has two binding blocks.

One block is:

```c
struct SerializedBindingBlock {
    uint32_t count;

    uint32_t pointerStorage[count];
    uint32_t runtimeStorage[count];

    char names[count][21];
};
```

Physical byte size:

```text
4 + count * 8 + count * 21
=
4 + count * 29
```

Earlier wording such as:

```text
slotMetadata[count * 8]
```

was structurally correct but semantically too suggestive.

Runtime demonstrates that the `count*8` region is primarily **in-file storage
reserved for pointer/runtime arrays that are rewritten at load**.

---

# 53. Runtime binding descriptor

Helper:

```text
0x0044B7D0
```

returns:

```text
script +0x3C
```

which is the start of both adjacent live binding descriptors.

Runtime effectively constructs:

```c
struct RuntimeBindingTable {
    uint32_t count;
    char   **names;
    uint32_t *runtimeSlots;
}; // 0x0C

struct RuntimeScriptBindingTables {
    RuntimeBindingTable tableA;  // script +0x3C
    RuntimeBindingTable tableB;  // script +0x48
};
```

This schema is directly visible in the `DEAD0002` loader around:

```text
0x004499E5..0x00449A72
```

---

# 54. Binding block load transformation

For table A Runtime reads:

```text
count
```

then sets:

```text
script +0x3C = count
script +0x40 = address of pointerStorage[0]
script +0x44 = address of runtimeStorage[0]
```

For table B:

```text
script +0x48 = count
script +0x4C = address of pointerStorage[0]
script +0x50 = address of runtimeStorage[0]
```

Then for each entry:

```text
pointerStorage[i] = &names[i][0]
runtimeStorage[i] = 0
```

The serialized words originally occupying those two arrays are not trusted as
live pointers/state.

---

# 55. Binding block `Grid.SCX` example

Three Grid scripts have one A-binding:

```text
1KaylArrives
2KaylStand
3KaylLeaves
```

All use name:

```text
UBassin
```

Example variable block for `1KaylArrives`:

```text
count:
    1

pointerStorage[0] serialized value:
    0x00A01470

runtimeStorage[0] serialized value:
    0xFFFFFFFF

name[0]:
    "UBassin"
```

Runtime replaces those array words with:

```text
pointerStorage[0] = live pointer to "UBassin"
runtimeStorage[0] = 0
```

This is excellent evidence that SCX retains editor/build memory-image-shaped
placeholder state.

---

# 56. Binding table A known semantic use

`Script_SelectRelativeBodyAnimation` uses one parameter as an index into:

```text
binding table A
```

The resolved string identifies the object/body binding, e.g.:

```text
UBassin
```

The function-specific meaning belongs in:

[`iam-script-functions.md`](iam-script-functions.md).

The serialization fact is simply:

```text
binding tables provide indexed 21-byte names
```

plus Runtime-owned mutable slots.

---

# 57. Binding table B

The physical format is completely parallel to table A.

Its authoring/runtime semantic role remains unresolved.

Both supplied packages contain:

```text
binding B count = 0
```

for all examined scripts.

Do not invent a semantic name from the structure alone.

---

# 58. Related-script auxiliary block

Before root functions each script stores:

```text
u8 relatedScriptPresent
```

If zero:

```text
no extra bytes
```

If nonzero:

```text
char relatedScriptName[21]
```

The presence byte should be interpreted as:

```text
zero / nonzero
```

rather than requiring exactly `1`, because Runtime branches on zero/nonzero.

---

# 59. Runtime relationship construction

During load, if the presence byte is nonzero:

```text
read 21-byte name
    |
    v
lookup primary script by name
    |
    v
build live relation at script +0x54
```

Pairing helper:

```text
0x0044B280
```

sets a symmetric pointer relationship and initializes runtime gate bytes.

Those execution semantics are documented in `script-runtime.md`.

For serialization, the important rule is:

> the separate 21-byte name block is the portable relationship key; the
> fixed-record `+0x54` value is only pointer-shaped seed storage.

---

# 60. Index-to-pointer relocation

Runtime diagnostic name:

```text
Script_FunctionsIndexesToAdresses()
```

The original misspelling:

```text
Adresses
```

is preserved here intentionally.

Bulk function:

```text
0x00449610
```

It converts function records from serialized/index form to live pointer form.

---

# 61. Parameter relocation

For every root and linked function:

Serialized:

```text
function +0x08 = firstValueIndex
```

Runtime conversion:

```text
function +0x08 = sharedValuesBase + firstValueIndex * 4
```

Bounds source in the loaded ScriptList:

```text
ScriptList +0x44 = sharedValueCount
ScriptList +0x48 = sharedValuesBase
```

An invalid index produces diagnostic:

```text
Script_FunctionsIndexesToAdresses():
Address of FuncParams isn't valid.
```

---

# 62. SyncFunction relocation

Serialized:

```text
function +0x0C = nextLinkedIndex
```

Conversion:

```text
-1:
    function +0x0C = NULL

otherwise:
    function +0x0C =
        script.linkedFunctions
        + nextLinkedIndex * 0x18
```

Out-of-range values produce:

```text
Script_FunctionsIndexesToAdresses():
Address of SyncFunction isn't valid.
```

---

# 63. Loader's single-function relocation helper

The `DEAD0002` parser also contains/uses a helper around:

```text
0x0044A070
```

that performs the same fundamental conversion for an individual function:

```text
firstValueIndex -> live parameter pointer
nextLinkedIndex -> live SyncFunction pointer
```

The existence of both bulk and single-function paths reinforces the dual
serialized/runtime representation.

---

# 64. Pointer-to-index conversion

Runtime diagnostic name:

```text
Script_FunctionsAdressesToIndexes()
```

Bulk function:

```text
0x004494C0
```

It converts live pointers back into portable indexes.

This is direct proof that the index representation is intentional rather than a
parser coincidence.

---

# 65. Parameter pointer -> index

Runtime computes:

```text
index =
    (FuncParamsPointer - sharedValuesBase) / 4
```

and verifies:

```text
index < sharedValueCount
```

Failure diagnostic:

```text
Script_FunctionsAdressesToIndexes():
Index of FuncParams too big: %d/%d.
```

---

# 66. SyncFunction pointer -> index

For a non-null pointer:

```text
index =
    (SyncFunctionPointer - linkedFunctionsBase) / 0x18
```

and verifies:

```text
index < linkedFunctionCount
```

For null:

```text
index = -1
```

Failure diagnostic:

```text
Script_FunctionsAdressesToIndexes():
Index of SyncFunction too big : %d/%d.
```

---

# 67. Null-pointer diagnostics

The reverse conversion contains diagnostics:

```text
Script_FunctionsAdressesToIndexes(): Ptr is NULL.
```

for invalid top-level arguments.

Modern code should return a structured error rather than relying on Runtime's
diagnostic/return convention.

---

# 68. Why both conversion directions matter

The paired Runtime functions prove the conceptual ABI:

```text
SERIALIZED

FuncParams:
    value-pool index

SyncFunction:
    linked-array index / -1

        <---- conversion ---->

LIVE RUNTIME

FuncParams:
    uint32_t*

SyncFunction:
    RuntimeScriptFunction* / NULL
```

A modern parser should preserve the **serialized** meaning in immutable data
structures and perform semantic resolution explicitly.

It should not stuff process pointers back into parsed file records.

---

# 69. `DEAD0002` Runtime loader case

The relevant case in the main SCX descriptor parser begins around:

```text
0x00449881
```

Its high-level order is:

```text
read scriptCount
establish primary-script array

find sharedValueCount after fixed scripts
establish shared-value array

for each script:
    set owner ScriptList pointer
    clear initial runtime status

    read related-script block
    resolve relationship when present

    establish root-array pointer
    establish linked-array pointer
    relocate function indexes to pointers

    construct binding table A
    construct binding table B
```

The parser works largely **in place** on the loaded descriptor buffer.

That is why so many serialized placeholder fields become useful storage for
live pointers.

---

# 70. SCX behaves like a relocatable memory image

`DEAD0002` is not a clean modern data-transfer schema.

It resembles:

```text
serialized copy of Runtime/editor structures
+
portable indexes in pointer slots that require relocation
+
variable backing storage deliberately shaped for in-place mutation
```

Examples:

```text
script +0x00
    pointer-shaped owner seed -> live ScriptList*

script +0x28/+0x30
    pointer-shaped seeds -> command-array pointers

function +0x08
    value index -> value pointer

function +0x0C
    linked index -> SyncFunction pointer

binding pointer arrays
    serialized placeholder words -> char* pointers

binding runtime arrays
    serialized words -> zeroed live runtime slots
```

This design explains several otherwise strange SCX fields.

---

# 71. Do not preserve pointer semantics in OpenNomad

OpenNomad should parse:

```text
indexes
counts
names
raw placeholder values
```

into stable modern objects.

Recommended immutable command model:

```cpp
struct ScxScriptCommand {
    uint32_t function_id;
    uint32_t value_count;
    uint32_t first_value_index;
    optional<uint32_t> next_linked_command_index;
    uint32_t execution_limit;
    uint32_t initial_execution_count;
};
```

Then runtime code resolves indexes against its owned containers.

This avoids:

```text
pointer invalidation
host pointer-width issues
endianness leakage
serialization/live-state aliasing
```

---

# 72. Current OpenNomad parser mapping

Current `SCX.hpp` already represents the command correctly as:

```text
opcode
value_count
first_value_index
next_linked_command_index
execution_limit
initial_execution_count
```

One naming improvement is worthwhile:

```text
opcode
    ->
function_id
```

or:

```text
iam_function_id
```

because the record is not bytecode.

This is naming cleanup rather than a structural parser change.

---

# 73. Current OpenNomad script-field naming updates

New Runtime analysis now supports better names than some current neutral fields.

Current serialized/runtime names:

```text
repeat_limit
initial_repeat_index / repeat_index
runtime_field_58
```

`runtime_field_58` can become `serialized_elapsed_time_bits` once that separate
cleanup is in scope.

Likewise:

```text
flags
```

can remain broad because `+0x1E` is genuinely composite.

The tail should remain partially opaque in the immutable file model even though
its loaded runtime use is better understood.

---

# 74. Current binding parser behavior

OpenNomad currently parses one binding block as:

```text
count
skip count * 8 bytes
read count * 21-byte names
```

For known runtime semantics this is sufficient because Runtime itself rewrites
the two dword arrays.

However the format documentation should describe those bytes accurately as:

```text
two count-dword placeholder/runtime-storage arrays
```

rather than generic meaningful metadata.

---

# 75. Round-trip preservation of binding placeholders

If OpenNomad eventually needs exact binary round-trip serialization, the parser
should preserve:

```text
pointerStorageRaw[count]
runtimeStorageRaw[count]
```

because current `ScxBindingTable` discards them.

For gameplay loading alone, discarding their original values is likely safe:

Runtime overwrites them during load.

This is therefore a distinction between:

```text
semantic game compatibility
```

and:

```text
byte-perfect file preservation
```

---

# 76. Retail corpus — `Grid.SCX`

Structured-script inventory:

```text
scriptCount:
    8

sharedValueCount:
    124

root functions total:
    8

linked functions total:
    14

related-script blocks present:
    0

binding A entries total:
    3

binding B entries total:
    0

function records total:
    22
```

Next descriptor after `DEAD0002`:

```text
DEAD0000 at 0x07DB
```

---

# 77. `Grid.SCX` script inventory

| ID | Name | Roots | Linked | Repeat limit | Bind A | Bind B |
|---:|---|---:|---:|---:|---:|---:|
| `1` | `1KaylArrives` | 1 | 7 | 1 | 1 | 0 |
| `6` | `2KaylStand` | 1 | 0 | -1 | 1 | 0 |
| `8` | `3KaylLeaves` | 1 | 7 | 1 | 1 | 0 |
| `13` | `fx3smokb` | 1 | 0 | 1 | 0 | 0 |
| `15` | `fx1 impact2` | 1 | 0 | 1 | 0 | 0 |
| `17` | `fx2 smoke1` | 1 | 0 | 1 | 0 | 0 |
| `19` | `fx1 impact1` | 1 | 0 | 1 | 0 | 0 |
| `20` | `Wait5sec` | 1 | 0 | -1 | 0 | 0 |

This is direct retail data.

---

# 78. `Grid.SCX` function-ID inventory

Across its 22 function records:

```text
0x0200002A  count 3
0x04000028  count 4
0x05000015  count 14
0x06000017  count 1
```

Names/semantics belong in `iam-script-functions.md`.

---

# 79. `1KaylArrives` serialized graph

Fixed script fields:

```text
name:
    1KaylArrives

scriptId:
    1

rootFunctionCount:
    1

linkedFunctionCount:
    7

repeatLimit:
    1

binding A:
    count 1
    name "UBassin"
```

Command graph:

```text
root[0]
    functionId = 0x0200002A
    valueCount = 12
    firstValueIndex = 0
    nextLinkedIndex = 0

linked[0]
    functionId = 0x05000015
    firstValueIndex = 12
    nextLinkedIndex = 1

linked[1]
    functionId = 0x05000015
    firstValueIndex = 17
    nextLinkedIndex = 2

linked[2]
    firstValueIndex = 22
    nextLinkedIndex = 3

linked[3]
    firstValueIndex = 27
    nextLinkedIndex = 4

linked[4]
    firstValueIndex = 32
    nextLinkedIndex = 5

linked[5]
    firstValueIndex = 37
    nextLinkedIndex = 6

linked[6]
    firstValueIndex = 42
    nextLinkedIndex = -1
```

This is an excellent compact example of:

```text
one root
+
seven SyncFunctions
+
contiguous value-pool slices
```

without requiring scheduler semantics in the format document.

---

# 80. `1KaylArrives` root parameter slice

Root:

```text
functionId:
    0x0200002A

valueCount:
    12

firstValueIndex:
    0

raw values:
    00000000
    00000000
    00000000
    3F800000
    00000000
    00000000
    00000000
    00000000
    00000000
    00000000
    00000000
    00000000
```

The fourth word is float:

```text
1.0
```

but the file format itself stores only raw 32-bit words.

Function-specific typing belongs in the IAM function catalogue.

---

# 81. `Wait5sec` example

`Grid.SCX` script:

```text
name:
    Wait5sec

scriptId:
    20

rootFunctionCount:
    1

linkedFunctionCount:
    0

repeatLimit:
    -1
```

Root function:

```text
functionId:
    0x06000017

valueCount:
    2

executionLimit:
    1

executionCount:
    0
```

Raw values:

```text
0x43160000
0x00000000
```

Interpreted by the known `Wait` function:

```text
150.0 script frames
0.0 elapsed
```

At 30 Hz:

```text
150 frames = 5 seconds
```

This is a useful cross-check between serialization and recovered function
semantics.

---

# 82. Retail corpus — `aventure.SCX`

Structured-script inventory:

```text
scriptCount:
    22

sharedValueCount:
    392

root functions total:
    22

linked functions total:
    74

related-script blocks present:
    0

binding A entries total:
    0

binding B entries total:
    0

function records total:
    96
```

Next descriptor:

```text
DEAD0000 at 0x189A
```

---

# 83. `aventure.SCX` function-ID inventory

Current supplied package uses:

```text
0x0400000C  count 4
0x0400001B  count 4
0x0400001C  count 4
0x0400001D  count 4
0x04000028  count 21
0x04000029  count 4
0x05000014  count 55
```

This reflects `aventure.scx`'s role as a permanent effects/adventure resource
package and should not be mistaken for the complete IAM function namespace.

---

# 84. Supplied fixed-field patterns

Across both supplied packages:

```text
+0x1C serialized runtimeState:
    always 1

+0x24 currentRootIndex:
    always 0

+0x38 repeatIndex:
    always 0

+0x54 related pointer placeholder:
    always 0

+0x58 elapsed bits:
    always 0

+0x5C..+0x63 runtime tail:
    all zero
```

These are valuable observations but not formal format constraints.

Runtime explicitly mutates several of them.

---

# 85. Supplied function-record patterns

Across both supplied packages:

```text
all command executionLimit values:
    1

all command executionCount values:
    0
```

Again:

```text
observed corpus fact
!=
format invariant
```

Runtime supports other values.

---

# 86. Parser requirements — fixed section

A robust parser should:

1. require at least 4 bytes for `scriptCount`;
2. bound `scriptCount` against a sane implementation maximum;
3. require:
   ```text
   scriptCount * 0x64
   ```
   bytes before reading fixed records;
4. parse fixed names with bounded string logic;
5. retain raw pointer-shaped placeholders rather than dereferencing them;
6. read `sharedValueCount` only after all fixed scripts;
7. bound:
   ```text
   sharedValueCount * 4
   ```
   against remaining descriptor bytes.

---

# 87. Parser requirements — per-script auxiliary block

For each script in fixed-record order:

1. read one related-presence byte;
2. if nonzero, require/read 21 bytes;
3. require:
   ```text
   rootFunctionCount * 0x18
   ```
4. require:
   ```text
   linkedFunctionCount * 0x18
   ```
5. parse binding A;
6. parse binding B;
7. validate every function's value slice;
8. validate every non-`-1` linked index.

Do not attempt to locate the next script's variable block by scanning for a
marker.

Counts define the layout.

---

# 88. Binding-block parser requirements

For each table:

```text
read count
```

then require:

```text
count * 8
+
count * 21
```

remaining bytes.

Safer arithmetic:

```text
uint64_t storageBytes = uint64_t(count) * 8
uint64_t nameBytes    = uint64_t(count) * 21
```

before converting to host `size_t`.

Current OpenNomad follows this bounded style.

---

# 89. Optional strict binding-count validation

Because the fixed descriptor duplicates the count, strict mode can require:

```text
script.bindingAFields[0]
    == serializedBindingBlockA.count

script.bindingBFields[0]
    == serializedBindingBlockB.count
```

Both supplied packages satisfy this for every script.

A forensic/tolerant parser may instead report a warning and continue using the
physical variable-block count.

---

# 90. Function-record parser requirements

Each `0x18` record should be read as six little-endian dwords, with the fourth
viewed as signed for the `-1` sentinel:

```text
u32 functionId
u32 valueCount
u32 firstValueIndex
i32 nextLinkedIndex
u32 executionLimit
u32 executionCount
```

Reject:

```text
nextLinkedIndex < -1
```

and:

```text
nextLinkedIndex >= linkedFunctionCount
```

when nonnegative.

---

# 91. Value-slice validation

Use overflow-safe checks:

```cpp
if (first > poolSize)
    error;

if (count > poolSize - first)
    error;
```

Do not calculate:

```text
first + count
```

first in a narrow unsigned type and only compare afterward.

---

# 92. Linked-chain validation levels

Basic format validation needs only:

```text
each next index is in bounds or -1
```

Additional modern safety can detect:

```text
cycles
excessively deep chains
repeated linked nodes
```

Runtime authored data is expected to be valid.

OpenNomad's cycle/budget safeguards are implementation protections, not part of
SCX serialization.

---

# 93. Root count versus linked count

Do not require:

```text
rootFunctionCount == linkedFunctionCount
```

or:

```text
one linked chain per root
```

They are independent counts.

The supplied files happen to have one root per script but a wide range of
linked counts.

---

# 94. Related-script name validation

The Runtime lookup behaves like an ordinary C-string comparison.

Modern parser behavior should remain bounded to the fixed 21-byte field.

For malformed data with no NUL:

```text
preserve/read at most 21 bytes
```

rather than reading into the following root-function data.

This is a safety improvement over blindly reproducing unsafe C-string behavior.

---

# 95. Endianness

All recovered integer fields are:

```text
little-endian
```

matching the Windows/x86 target.

Float-typed IAM parameters are serialized as their raw little-endian 32-bit
IEEE-754 bit patterns inside the otherwise untyped value pool.

---

# 96. Alignment

There is no additional alignment padding between:

```text
related presence/name
root functions
linked functions
binding A
binding B
next script's related block
```

The next field begins immediately after the previous counted data.

Because a binding name is 21 bytes and the presence flag is one byte, many
substructures are not naturally 4-byte aligned in file space.

A parser must use byte-wise bounded reads / `memcpy`, not cast arbitrary file
addresses to aligned C structs.

---

# 97. Fixed records are 4-byte-friendly; auxiliary data is not

The initial:

```text
ScriptV5[scriptCount]
sharedValues[]
```

region is naturally dword-oriented.

The per-script auxiliary region includes:

```text
1-byte flag
21-byte names
```

so subsequent `0x18` command arrays can begin at unaligned file addresses.

x86 tolerates unaligned loads; portable OpenNomad code should not rely on that.

---

# 98. No appended resource for `DEAD0002`

Unlike:

```text
DEAD0000 paths
DEAD0001 animations
DEAD0003 sounds
DEAD0004 models
DEAD0005 scenes
```

structured scripts consume no separate appended SCX resource.

Everything required by `DEAD0002` serialization is inline in the descriptor
block.

Function handlers can of course refer to resources from the other SCX
sections by indexes/IDs in their parameter words.

---

# 99. Cross-section references remain function-specific

The SCX script format itself does not declare one generic resource-reference
type.

A raw parameter may reference:

```text
DEAD0000 path
DEAD0001 animation
DEAD0003 sound
DEAD0004 sprite/model
DEAD0005 external scene
binding table entry
world object
character
etc.
```

depending on the IAM function ID and parameter slot.

Therefore cross-section semantic validation belongs in native function/runtime
logic, not the generic `DEAD0002` parser.

---

# 100. `Script_GetNumParam` does not define file width

Runtime helper:

```text
0x0044C090
    Script_GetNumParam
```

maps semantic parameter selectors for known IAM functions.

It does **not** replace serialized:

```text
function.valueCount
```

and it does not prove that every raw parameter word is one user-facing IAM
parameter.

Use the serialized count to parse the value slice.

Use the function catalogue to understand individual words.

---

# 101. Runtime primary scripts are not immutable templates

Previous versions of this file said, in effect:

```text
loaded template
    ->
make mutable instance
    ->
execute
```

That is now known to be incomplete.

Runtime's loaded primary `0x64` records are themselves mutable and directly
scheduled.

A separate clone-instance mechanism also exists.

That entire ownership model now belongs in:

[`script-runtime.md`](script-runtime.md).

For this format document, use the term:

```text
serialized script definition
```

rather than implying that the loaded Runtime record remains immutable.

---

# 102. Runtime clone parameter topology is not serialization

`Script_MakeInstance()` allocates private parameter arrays per cloned command.

That does **not** mean the SCX file contains per-command parameter arrays.

Serialized representation remains:

```text
one global DEAD0002 shared value pool
+
per-command firstValueIndex/valueCount
```

This distinction is important enough to state explicitly because earlier
versions of this file blurred the two layers.

---

# 103. OpenNomad immutable parse model

Current OpenNomad sensibly stores:

```text
ScxData
    scripts[]
    shared_values[]
```

with each `ScxScript` owning parsed root/linked vectors and binding names.

That model describes the file safely and independently of Runtime pointer
relocation.

It should remain the parser layer even if the live script scheduler is later
rewritten to mirror Runtime's primary/clone topology more closely.

---

# 104. Suggested parser type names

More historically accurate names would be:

```cpp
ScxScriptFunction
    instead of / alongside ScxScriptCommand

function_id
    instead of opcode

next_sync_function_index
    instead of next_linked_command_index
```

However existing OpenNomad names are understandable and can be migrated only
when convenient.

The documentation should consistently clarify the original meaning even if
source compatibility favors current class names.

---

# 105. Suggested fixed script parser fields

```cpp
struct ScxScript {
    uint32_t owner_placeholder;
    string name;
    uint16_t script_id;

    uint16_t serialized_runtime_state;
    uint16_t flags_and_status_seed;

    uint32_t root_function_count;
    uint32_t current_root_index_seed;
    uint32_t root_functions_placeholder;

    uint32_t linked_function_count;
    uint32_t linked_functions_placeholder;

    int32_t repeat_limit;
    uint32_t repeat_index_seed;

    array<uint32_t, 3> binding_a_fields;
    array<uint32_t, 3> binding_b_fields;

    uint32_t related_script_placeholder;
    uint32_t elapsed_time_bits;
    array<uint8_t, 8> runtime_tail;

    // Parsed variable data:
    RelatedScriptRef related;
    vector<ScxScriptFunction> roots;
    vector<ScxScriptFunction> linked;
    ScxBindingTable binding_a;
    ScxBindingTable binding_b;
};
```

This naming makes the serialized/runtime boundary obvious.

---

# 106. Suggested binding parser representation

Gameplay-minimal:

```cpp
struct ScxBindingTable {
    vector<string> names;
};
```

Round-trip/forensic:

```cpp
struct ScxBindingTable {
    uint32_t fixed_descriptor_count;
    vector<uint32_t> pointer_storage_raw;
    vector<uint32_t> runtime_storage_raw;
    vector<string> names;
};
```

The second representation preserves every authored byte while still refusing
to treat pointer-shaped values as addresses.

---

# 107. Recommended strict parser tests

- [ ] `DEAD0002` tag parsed by value, not assumed position;
- [ ] fixed script record size exactly `0x64`;
- [ ] function record size exactly `0x18`;
- [ ] script name width exactly 22;
- [ ] related/binding name width exactly 21;
- [ ] shared values are raw dwords;
- [ ] value slices remain in bounds;
- [ ] `-1` linked sentinel accepted;
- [ ] other negative linked indexes rejected;
- [ ] nonnegative linked indexes bounded by current script linked count;
- [ ] binding count arithmetic overflow-safe;
- [ ] fixed binding counts match variable counts in strict mode;
- [ ] no assumed alignment between per-script auxiliary structures;
- [ ] parser stops exactly on the next descriptor tag for retail fixtures.

---

# 108. Recommended Grid fixture assertions

For supplied `Grid.SCX`:

```text
scriptCount == 8
sharedValueCount == 124

DEAD0002 end == 0x07DB
next tag == DEAD0000
```

`1KaylArrives`:

```text
scriptId == 1
root count == 1
linked count == 7
repeat limit == 1
binding A count == 1
binding A name[0] == "UBassin"
```

`Wait5sec`:

```text
scriptId == 20
repeat limit == -1
root function ID == 0x06000017
value count == 2
first raw value == 0x43160000
second raw value == 0
```

---

# 109. Recommended aventure fixture assertions

For supplied `aventure.SCX`:

```text
scriptCount == 22
sharedValueCount == 392

root functions total == 22
linked functions total == 74
function records total == 96

binding A total == 0
binding B total == 0
related blocks present == 0

DEAD0002 end == 0x189A
next tag == DEAD0000
```

These give high-value regression coverage for both simple and linked scripts.

---

# 110. Recommended relocation unit tests

Given:

```text
sharedValuesBase
linkedFunctionsBase
```

verify conceptually:

```text
firstValueIndex 0
    -> sharedValuesBase

firstValueIndex N
    -> sharedValuesBase + N*4

nextLinkedIndex -1
    -> NULL

nextLinkedIndex N
    -> linkedFunctionsBase + N*0x18
```

and reverse conversion yields the original indexes.

Modern code need not store actual raw pointers in parsed records to test this
relationship.

---

# 111. Recommended malformed-data tests

Reject or report:

```text
script count exceeds remaining fixed bytes
shared value count exceeds remaining bytes
function array exceeds remaining bytes
value first index beyond pool
value count beyond remaining pool slice
negative linked index other than -1
linked index >= linked count
binding count causing overflow
binding storage/name bytes beyond section
truncated related name
truncated binding name
```

Optional additional validation:

```text
linked-chain cycles
fixed/variable binding count mismatch
nonzero unexpected placeholder patterns
```

---

# 112. Do not scan for `DEADxxxx` inside script payload

Raw values and strings can contain arbitrary bytes.

The correct end of `DEAD0002` is found by **parsing its grammar**.

Never locate the next SCX section by scanning forward for bytes resembling:

```text
DE AD xx xx
```

That can produce false positives inside value pools, names, or placeholders.

---

# 113. Round-trip writer order

A future SCX writer should emit:

```text
DEAD0002
scriptCount
fixed scripts[]
sharedValueCount
sharedValues[]

for each script in the original fixed-script order:
    related block
    roots
    linked
    binding A
    binding B
```

Changing script order can break:

- IDs/names expected by surrounding data;
- related-script lookup assumptions;
- raw placeholder reproducibility;
- deterministic binary comparison.

Preserve source order unless intentionally rebuilding the package.

---

# 114. Writer pointer placeholders

A semantic writer does **not** need to recreate historical process addresses in:

```text
+0x00
+0x28
+0x30
binding pointer fields
+0x54
binding pointerStorage[]
```

if the target Runtime loader always overwrites them and accepts neutral values.

However that compatibility has not been exhaustively tested.

For byte-identical round-trip:

```text
preserve original raw placeholders
```

rather than inventing new addresses.

---

# 115. Writer mutable-state seeds

Likewise fields such as:

```text
+0x1C
+0x1E low status bits
+0x24
+0x38
+0x58
+0x5C..+0x63
command executionCount
```

look like serialized snapshots/seeds of a runtime-shaped structure.

A content-authoring writer needs to understand original initialization rules
before choosing canonical values.

A round-trip writer should simply preserve them.

---

# 116. Original diagnostic strings

Useful Runtime strings include:

```text
Script_FunctionsAdressesToIndexes():
Index of FuncParams too big: %d/%d.

Script_FunctionsAdressesToIndexes():
Index of SyncFunction too big : %d/%d.

Script_FunctionsAdressesToIndexes(): Ptr is NULL.

Script_FunctionsIndexesToAdresses():
Address of FuncParams isn't valid.

Script_FunctionsIndexesToAdresses():
Address of SyncFunction isn't valid.
```

These diagnostics are among the strongest evidence for the command record's
serialized/runtime dual representation.

---

# 117. Useful Runtime addresses

Serialization/load boundary:

```text
0x004494C0
    Script_FunctionsAdressesToIndexes

0x00449610
    Script_FunctionsIndexesToAdresses

0x00449881
    DEAD0002 parser case begins / script-list setup

0x0044A070
    single-function index -> pointer relocation helper

0x0044AA20
    clear loaded script runtime state / low status nibble

0x0044B1B0
    find primary script by name

0x0044B280
    establish related-script pair

0x0044B7D0
    get binding-descriptor base at script +0x3C

0x0044CD10
    find primary script by 16-bit ID

0x0044CD40
    get script ID
```

Execution-only addresses belong primarily in `script-runtime.md` and
`iam-script-functions.md`.

---

# 118. Ghidra labels

Recommended high-confidence labels:

```text
004494C0  Script_FunctionsAdressesToIndexes
00449610  Script_FunctionsIndexesToAdresses

0044A070  Script_FunctionIndexToAddress
          reconstructed singular helper

0044AA20  Script_ClearRuntimeState
          reconstructed helper role

0044B1B0  Script_FindByName
0044B280  Script_SetRelatedPair
0044B7D0  Script_GetBindingTables

0044CD10  Script_FindById
0044CD40  Script_GetId
```

Keep original misspelling `Adresses` for the two functions whose diagnostics
preserve it.

---

# 119. Ghidra serialized types

Useful file-format types:

```c
typedef struct SerializedScriptFunction {
    uint32_t functionId;
    uint32_t valueCount;
    uint32_t firstValueIndex;
    int32_t  nextLinkedIndex;
    uint32_t executionLimit;
    uint32_t executionCount;
} SerializedScriptFunction;
```

and:

```c
typedef struct SerializedScriptV5 {
    uint32_t ownerPlaceholder;
    char     name[22];
    uint16_t scriptId;
    uint16_t runtimeState;
    uint16_t flagsAndStatus;
    uint32_t rootFunctionCount;
    uint32_t currentRootIndex;
    uint32_t rootFunctionsPlaceholder;
    uint32_t linkedFunctionCount;
    uint32_t linkedFunctionsPlaceholder;
    int32_t  repeatLimit;
    uint32_t repeatIndex;
    uint32_t bindingAFields[3];
    uint32_t bindingBFields[3];
    uint32_t relatedScriptPlaceholder;
    uint32_t elapsedTimeBits;
    uint8_t  runtimeTail[8];
} SerializedScriptV5;
```

Do not apply this serialized type over an already relocated live Runtime record
without changing pointer field types.

---

# 120. Ghidra loaded function type

On 32-bit Runtime the relocated form can be typed separately:

```c
typedef struct RuntimeScriptFunction RuntimeScriptFunction;

struct RuntimeScriptFunction {
    uint32_t functionId;
    uint32_t valueCount;
    uint32_t *funcParams;
    RuntimeScriptFunction *syncFunction;
    uint32_t executionLimit;
    uint32_t executionCount;
};
```

Size remains:

```text
0x18
```

because the original target uses 32-bit pointers.

This type must **not** be used as a portable on-disk C struct on 64-bit hosts.

---

# 121. Ghidra loaded binding type

```c
typedef struct RuntimeBindingTable {
    uint32_t count;
    char **names;
    uint32_t *runtimeSlots;
} RuntimeBindingTable;
```

Two adjacent records begin at:

```text
script +0x3C
```

Runtime target size:

```text
0x0C each
```

Again, this is a 32-bit live Runtime type, not the portable serialized binding
block layout.

---

# 122. Cross-document boundary: `script-runtime.md`

Do not duplicate here:

```text
primary mutable script array
clone slot pool
Script_MakeInstance
Script_RemoveInstance
Script_PlayScriptList
Script_PlayScript return codes
execution eligibility
handler AL -> BL contribution
root advancement
repeat/reinit lifecycle
related-script handoff
context bytes +5E..+61
AREA activation ownership
```

The format fields are listed here only far enough to explain their serialized
positions.

The runtime document is authoritative for their behavior.

---

# 123. Cross-document boundary: `iam-script-functions.md`

Do not duplicate here:

```text
full 0xCC0000NN catalogue
native handler address for every function
reinit handler address
semantic parameter selectors
function-specific argument typing
sprite behavior
sound behavior
animation behavior
Wait implementation
```

This file may use one or two known functions as serialization examples, but the
catalogue remains authoritative for meaning.

---

# 124. Cross-document boundary: `iam-scenario-vm.md`

The following are **not `DEAD0002` function IDs**:

```text
0x03 EndEvent
0x04 JumpRelative
0x39 StartScxScript
0x3A StartScxScriptTracked
0x3B StartCharacterScript
0x3C StartCharacterScriptTracked
0x46 OpenInterface
0x67 PlayMusic
0x84 BeginCinematicLetterbox
...
```

They belong to the compact one-byte scenario/event VM.

The previous version of `script-opcodes.md` carried an increasingly large copy
of that opcode catalogue. It has been intentionally removed.

---

# 125. Cross-system bridge, compact reference only

The only bridge fact useful to this serialization document is:

```text
AREA VM structured-script launch
    |
    v
raw 16-bit script ID
    |
    v
lookup against Serialized/RuntimeScript +0x1A
```

Everything after lookup — activation context, tracking, wait state, primary vs
clone lifetime — belongs in the runtime/AREA documents.

---

# 126. Current OpenNomad corrections suggested by this document

Implemented OpenNomad names:

```text
ScxScriptCommand::opcode
    -> function_id / iam_function_id

ScxScript::repeat_limit
ScxScript::initial_repeat_index
ScriptInstance::repeat_index

ScxScript::runtime_field_58
    -> elapsed_time_bits
```

Potential optional format-preservation improvement:

```text
retain the two raw count-dword arrays in each binding block
```

Command eligibility uses only each command's `execution_limit` and mutable
`execution_count`; the script-level repeat limit is not an eligibility gate.

---

# 127. Current parser behavior that should remain

Good existing decisions:

```text
bounded fixed-string decoding
raw ScriptValue storage
explicit little-endian reads
optional next-linked index instead of host pointer
file offsets retained for diagnostics
checked count * stride arithmetic
value-slice bounds validation
linked-index validation
separate parsed root/linked vectors
binding names decoded independently from fixed descriptors
```

Do not regress these in pursuit of literal Runtime pointer layouts.

---

# 128. What `script-opcodes.md` should no longer contain

After this cleanup the following old sections are intentionally removed from
this file:

```text
full Script_PlayScript scheduler walkthrough
handler-return blocking list
OpenNomad scheduler implementation comparison
full reinit lifecycle
complete AREA VM lifecycle
AREA evaluation stack
AREA opcode table
AREA arithmetic/branch opcodes
AREA interface/camera/music details
AREA wait states
AREA probe/suppression mode
character activation opcode analysis
```

Those facts are not lost; they now live in their dedicated documents.

This reduces contradictory duplicate sources of truth.

---

# 129. Format invariants currently strong enough to rely on

Strong:

```text
DEAD0002 has no appended resource
script record = 0x64
function record = 0x18
script name = 22 bytes
related name = 21 bytes
binding name = 21 bytes
one section-global dword value pool
function +08 is serialized value index
function +0C is serialized linked index / -1
binding block = 4 + count*29 bytes
binding fixed descriptor = two adjacent 0x0C records
Runtime relocates indexes/pointer storage in place
```

---

# 130. Observations not yet safe as universal invariants

Do **not** require globally:

```text
rootFunctionCount == 1
executionLimit == 1
executionCount == 0
relatedScriptPresent == 0
binding B count == 0
runtime tail == all zeros
serialized runtimeState == 1
parameter slices never overlap
```

Those are true of the supplied Grid/adventure corpus where stated, but Runtime
is clearly implemented for broader possibilities.

---

# 131. Highest-value remaining format RE

## 131.1 Broader retail SCX corpus

Acquire additional city/area SCX packages and inventory:

```text
related-script blocks
binding B entries
multi-root scripts
non-1 command execution limits
nonzero initial execution counts
nonzero runtime-tail seeds
parameter-slice overlap
```

This would distinguish genuine format invariants from Grid/adventure bias.

## 131.2 Binding-table second array semantics

Runtime zeroes:

```text
runtimeStorage[i]
```

on load.

Trace later users of:

```text
script +0x44
script +0x50
```

and their indexed slots to recover the exact runtime purpose.

## 131.3 Upper bits of `+0x1E`

Some are runtime context/status fields; determine which are authored/preserved
from serialized data versus entirely runtime-generated.

## 131.4 `+0x5C` serialized seed semantics

Runtime uses multiple bits at `+0x5C`, and related-pair loading inspects bit 0.
Find retail SCX examples with nonzero serialized values.

## 131.5 Reverse-conversion callers

Trace every caller of:

```text
Script_FunctionsAdressesToIndexes
```

to determine whether the reverse form is used for:

```text
saving
copying
editor compatibility
network/debug serialization
```

or another subsystem.

The function proves the representation; its higher-level purpose remains worth
naming.

---

# 132. Compact grammar reference

```text
DEAD0002

u32 scriptCount
ScriptV5[scriptCount]                    // 0x64 each

u32 sharedValueCount
u32 sharedValues[sharedValueCount]

repeat scriptCount times:
    u8 relatedPresent

    if relatedPresent != 0:
        char relatedName[21]

    Function[rootCount]                  // 0x18 each
    Function[linkedCount]                // 0x18 each

    u32 bindACount
    u32 bindAPointerStorage[bindACount]
    u32 bindARuntimeStorage[bindACount]
    char bindANames[bindACount][21]

    u32 bindBCount
    u32 bindBPointerStorage[bindBCount]
    u32 bindBRuntimeStorage[bindBCount]
    char bindBNames[bindBCount][21]
```

---

# 133. Compact script-record reference

```text
+00 u32 owner placeholder
+04 char name[22]
+1A u16 script ID
+1C u16 runtime-state seed
+1E u16 flags/status seed

+20 u32 root count
+24 u32 current-root seed
+28 u32 root pointer placeholder

+2C u32 linked count
+30 u32 linked pointer placeholder

+34 i32 repeat limit
+38 u32 repeat index

+3C 3*u32 binding A descriptor seed
+48 3*u32 binding B descriptor seed

+54 u32 related pointer placeholder
+58 u32 elapsed float bits
+5C u8 runtime tail[8]
```

Size:

```text
0x64
```

---

# 134. Compact function-record reference

```text
+00 u32 IAM function ID
+04 u32 value count
+08 u32 first value index
+0C i32 next linked index (-1 = none)
+10 u32 execution limit
+14 u32 execution count
```

Size:

```text
0x18
```

Loaded x86 Runtime reinterpretation:

```text
+08 -> uint32_t* FuncParams
+0C -> RuntimeScriptFunction* SyncFunction
```

---

# 135. Compact binding reference

Serialized variable block:

```text
u32 count
u32 pointerStorage[count]
u32 runtimeStorage[count]
char names[count][21]
```

Loaded fixed descriptor:

```text
u32 count
char** names
u32* runtimeSlots
```

Two descriptors:

```text
A at script +0x3C
B at script +0x48
```

---

# 136. Compact relocation reference

```text
SERIALIZED -> RUNTIME

FuncParams:
    shared value index
    -> sharedValues + index*4

SyncFunction:
    -1
    -> NULL

    linked index
    -> linkedFunctions + index*0x18
```

Reverse:

```text
parameter pointer
    -> (ptr - sharedBase)/4

SyncFunction pointer
    -> (ptr - linkedBase)/0x18

NULL
    -> -1
```

Runtime functions:

```text
00449610  Indexes -> Adresses
004494C0  Adresses -> Indexes
```

---

# 137. Boundary of current knowledge

Confirmed or strongly recovered:

```text
complete DEAD0002 physical grammar
0x64 script record width
22-byte script name
16-bit script ID
0x18 function record layout
one global raw dword value pool
root/linked array ordering
value index -> parameter pointer relocation
linked index/-1 -> SyncFunction pointer/NULL relocation
reverse pointer -> index conversion
repeat limit at script +0x34
repeat index at +0x38
elapsed float slot at +0x58
binding A/B fixed descriptor locations
binding variable block exact physical shape
binding in-place pointer/slot rewrite behavior
related block presence byte + 21-byte name
related name -> live pointer construction
retail Grid/adventure section counts and extents
```

Still unresolved or intentionally delegated:

```text
semantic purpose of binding table B
runtime meaning of binding runtimeSlots[]
all upper +0x1E bits
all +0x5C bits
meaning of serialized nonzero tail/state seeds in wider corpus
why reverse pointer->index conversion is needed at higher level
whether parameter slices overlap in other retail SCX files
full scheduler/lifetime semantics (script-runtime.md)
full IAM function semantics (iam-script-functions.md)
AREA bytecode semantics (iam-scenario-vm.md)
```

The central format takeaway is:

> `DEAD0002` is a relocatable serialization of a pointer-rich structured
> scripting system. It stores fixed `0x64` script records, fixed `0x18` IAM
> function records, one raw section-wide value pool, per-script root and
> `SyncFunction` arrays, optional related-script names, and two binding-name
> blocks. Several dwords are deliberately shaped like Runtime pointers or
> runtime arrays, but the loader rewrites them in place. A modern parser should
> preserve the portable indexes/counts/raw placeholder bytes while keeping live
> runtime ownership completely separate.
