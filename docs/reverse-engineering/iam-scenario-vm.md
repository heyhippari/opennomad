# Omikron IAM scenario/event virtual machine

> **Status:** work-in-progress reverse-engineering documentation for OpenNomad  
> **Last updated:** 2026-08-22
>
> This document describes the compact byte-oriented scenario/event interpreter
> used by the Windows retail release of *Omikron: The Nomad Soul*.
>
> The VM is used by IAM-authored scenario data, most visibly the bytecode pool
> embedded in `IAM/AREA` and `IAM/SCENE` records. It is a separate execution system from the
> structured SCX `Script_*` runtime.
>
> The recovered VM has:
>
> - a fixed `0x2C`-byte runtime context;
> - three built-in bytecode event entrypoints;
> - a four-entry event queue;
> - a fixed 16-dword evaluation stack;
> - shared START/scenario global variables;
> - compact one-byte opcodes whose handlers decode their own operands;
> - record-relative jumps and event entrypoints;
> - an optional per-event scalar-remapping block;
> - native waits and subsystem calls;
> - a fixed global registry of 32 scenario contexts;
> - and a native opcode-handler table covering opcodes `0x00..0x98`.
>
> This file is authoritative for the **scenario/event VM runtime architecture**.
>
> Related documentation:
>
> - [`iam-area.md`](iam-area.md) — AREA archive/record layout, bytecode-pool
>   boundaries, entity tables, and serialized event entrypoints;
> - [`iam-scene.md`](iam-scene.md) — attached SCENE records, exact top-level
>   script spans, and independent SCENE compact contexts;
> - [`script-opcodes.md`](script-opcodes.md) — structured SCX script
>   serialization and the broader relationship between the two script systems;
> - [`iam-script-functions.md`](iam-script-functions.md) — native structured
>   SCX `Script_*` function catalogue;
> - [`scx.md`](scx.md) — SCX resource/script container;
> - [`startup-sequence.md`](startup-sequence.md) — startup path through AREA
>   event 1 and interface 29;
> - [`save-format.md`](save-format.md) — persistent state touched by scenario
>   operations.

---

# 1. Evidence model

Sources are used in this order:

1. **`Runtime.exe` behavior** — authoritative for context layout, dispatch,
   stack behavior, event queuing, native states, handler operand decoding,
   global-variable access, and subsystem side effects.
2. **Retail IAM data**, especially `IAM/AREA` record 118 and the complete AREA
   archive.
3. **TAG metadata** for global editor/runtime ID namespaces.
4. **Current OpenNomad implementation** — useful for identifying already
   implemented behavior and mismatches, but subordinate when it differs from
   Runtime.

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
- **Confirmed — data:** directly established from retail bytes.
- **Corroborated:** Runtime and retail data independently agree.
- **Strongly reconstructed:** behavior/layout is well established but the
  original source-level symbol or semantic name is unknown.
- **Provisional:** useful working interpretation needing more tracing.
- **Unknown:** field/opcode/runtime path exists but semantics remain unresolved.

---

# 2. This is not the SCX structured-script runtime

Omikron has at least two materially different script systems.

## 2.1 Compact scenario/event VM

The subject of this document:

```text
u8 opcode
inline opcode-specific operands
evaluation stack
relative branches
shared global variables
native scenario operations
explicit runtime states/waits
```

It is embedded in AREA records and coordinated by Runtime scenario contexts.

Examples:

```text
0x03 EndEvent
0x04 JumpRelative
0x07 PushInt8
0x0A PushGlobalVariable
0x39 StartScxScript
0x46 OpenInterface
0x67 PlayMusic
```

## 2.2 Structured SCX `Script_*` system

SCX `DEAD0002` instead serializes:

```text
0x64-byte script templates
0x18-byte command records
shared 32-bit value pools
linked command chains
native 32-bit function IDs such as 0x0200002A
```

Its scheduler lives around:

```text
Script_PlayScript()
0x0044C860
```

The scenario VM can **launch** structured SCX scripts, but the two
representations must not be merged.

---

# 3. High-level architecture

The recovered relationship is:

```text
IAM/AREA or IAM/SCENE
    |
    +-- AREA or SCENE tables
    |
    +-- shared compact bytecode pool
            |
            +-- event-1 entry
            +-- event-2 entry
            +-- event-3 entry
            +-- zone/link/other contexts
                    |
                    v
          RuntimeScenarioContext
                    |
           +--------+---------+
           |                  |
           v                  v
    evaluation VM        native handlers
           |                  |
           |        +---------+-----------+-----------+
           |        |                     |           |
           |        v                     v           v
           |    interface              camera       music
           |    character              area        presentation
           |    object                 SCX script
           |                             |
           +-----------------------------+
                                         |
                                         v
                              structured SCX ScriptRuntime
```

The VM is therefore best understood as:

> **a small event/orchestration interpreter that drives the larger native game
> systems.**

---

# 4. AREA bytecode pool and VM entrypoints

The binary container is documented in `iam-area.md`.

The relevant runtime fact is:

```text
an AREA record can contain one shared bytecode pool
with multiple entrypoints into it
```

Known serialized entrypoint sources include:

```text
AREA header +0x04
AREA table 2 +0x00
AREA table 2 +0x04
AREA table 2 +0x08
AREA table 7 +0x00
```

During AREA loading Runtime relocates these nonzero record-relative offsets into
live byte pointers.

The VM context receives up to three of those live entry pointers directly.

---

# 5. Important correction: table-2 first three dwords

The earlier AREA model treated only table-2 `+0x00` as an event pointer.

Runtime loading and context creation now establish:

```text
table 2 +0x00 -> event entry pointer 1
table 2 +0x04 -> event entry pointer 2
table 2 +0x08 -> event entry pointer 3
```

All three nonzero values are relocated by the AREA loader.

Zone-context creation passes those three fields directly into the scenario
context constructor.

The exact gameplay names of the three zone events are not yet established.

Do **not** name them:

```text
enter
stay
exit
```

until their trigger call paths prove that mapping.

For now use neutral names:

```text
event1Entry
event2Entry
event3Entry
```

This finding should also be propagated back into `iam-area.md`.

---

# 6. Runtime scenario context — `0x2C` bytes

Runtime allocates exactly:

```text
0x2C bytes
```

for one compact scenario context.

The current recovered layout is:

```c
struct RuntimeScenarioContext {
    uint8_t* event1Entry;       // +0x00
    uint8_t* event2Entry;       // +0x04
    uint8_t* event3Entry;       // +0x08

    uint8_t* instructionPtr;    // +0x0C

    int32_t* evaluationStack;   // +0x10
    uint16_t stackDepth;        // +0x14

    uint16_t state;             // +0x16

    uint8_t queuedEvents[4];    // +0x18..+0x1B
    uint16_t queuedEventCount;  // +0x1C

    uint8_t registrySlot;       // +0x1E
    uint8_t ownerAreaSlot;      // +0x1F

    uint32_t activeEvent;       // +0x20

    uint16_t* parameterBlock;   // +0x24

    uint16_t flags;             // +0x28
    int16_t externalToken;      // +0x2A
}; // 0x2C
```

Names are descriptive reconstructions unless explicitly noted.

---

# 7. Context constructor — `0x00406290`

The context creator allocates:

```text
malloc(0x2C)
```

then separately allocates:

```text
malloc(0x40)
```

for the evaluation stack.

A useful reconstructed signature is:

```c
RuntimeScenarioContext* CreateScenarioContext(
    uint8_t ownerAreaSlot,
    uint8_t* event1Entry,
    uint8_t* event2Entry,
    uint8_t* event3Entry);
```

The function installs:

```text
event1Entry
event2Entry
event3Entry
ownerAreaSlot
stack pointer
initial state
queue state
registry metadata
```

and registers the new context globally when a free slot exists.

---

# 8. Context fields `+0x00/+0x04/+0x08`

These are three independent bytecode-entry pointers.

Event service code around:

```text
0x00408220
```

selects:

```text
event 1 -> +0x00
event 2 -> +0x04
event 3 -> +0x08
```

and writes the selected pointer to:

```text
+0x0C instructionPtr
```

before setting state 1.

This is direct Runtime evidence.

---

# 9. `+0x0C` instruction pointer

The central interpreter treats:

```text
context +0x0C
```

as the live bytecode IP.

Runtime dispatch behavior:

1. read one opcode byte at `*instructionPtr`;
2. increment `instructionPtr` by one;
3. call the native handler associated with that opcode;
4. handler reads and advances over its own operands;
5. if context state remains 1, continue execution.

The handler therefore receives an IP already pointing to its first operand.

---

# 10. Evaluation stack — fixed 16 dwords

Runtime allocates:

```text
0x40 bytes
```

for one context's evaluation stack:

```text
16 * int32_t
```

Fields:

```text
+0x10 = int32_t* stack
+0x14 = uint16_t stackDepth
```

This is a fixed Runtime limit.

No convincing explicit bounds check has been recovered in the primitive push
handlers; retail data is treated as trusted.

A modern implementation should enforce:

```text
0 <= depth <= 16
```

and fail safely on overflow/underflow.

---

# 11. Stack persistence across events

Runtime's `EndEvent` handler sets the context state back to zero but does not
visibly clear:

```text
stackDepth
```

The normal event-dispatch path likewise does not visibly reset it.

This suggests retail bytecode is expected to leave the evaluation stack
balanced at event boundaries.

Current OpenNomad clears the evaluation stack at event start/end.

That is a defensible safety behavior, but it is not a literal reproduction of
the recovered Runtime code.

---

# 12. Context state — `+0x16`

Known numeric Runtime states include:

```text
0  inactive / event ended / eligible for queued-event dispatch
1  actively executing bytecode
4  tracked native/child operation wait
6  interface wait
7  camera/timed native wait
```

Additional observed values:

```text
3
5
8
9
10
11
```

have specialized transition/coordination behavior but should remain
semantically unnamed until their call paths are fully recovered.

Do not force every OpenNomad lifecycle enum value to correspond to a Runtime
number.

---

# 13. Context event queue — four bytes

Queue storage:

```text
+0x18..+0x1B   uint8_t queuedEvents[4]
+0x1C          uint16_t queuedEventCount
```

Thus Runtime supports at most:

```text
4 pending event IDs
```

per context.

The event ID is stored as one byte.

---

# 14. Queue function — `0x004063D0`

Queue behavior:

```text
if queuedEventCount < 4:
    append low byte of event ID
    increment count
```

When full, Runtime reports/logs the condition and does not append another
entry.

The normal return path still reports success-like status rather than using a
modern structured error.

OpenNomad should instead make queue overflow explicit and safe.

---

# 15. Special event-2 deduplication

Event ID:

```text
2
```

has special deduplication behavior.

A new event 2 is rejected when:

```text
event 2 is already present in the four-slot queue
```

or:

```text
context.activeEvent == 2
```

Other event IDs do not use this exact deduplication rule.

This is a subtle but direct Runtime behavior.

---

# 16. Event service — `0x00408220`

Queued events are serviced outside the instruction interpreter.

Normal dispatch requires context state:

```text
0
```

with additional special handling around transitional state 9.

When a queued event is consumed:

```text
event = queuedEvents[0]
context.activeEvent = event
context.flags = 0
```

then Runtime dispatches by event ID.

---

# 17. Built-in event IDs

Recovered behavior:

```text
event 0:
    no bytecode entrypoint

event 1:
    IP = event1Entry (+0x00)
    if non-null -> state = 1

event 2:
    IP = event2Entry (+0x04)
    if non-null -> state = 1

event 3:
    IP = event3Entry (+0x08)
    if non-null -> state = 1

event 4:
    destruction/removal path
    not ordinary bytecode execution
```

IDs beyond 4 do not map to another context entry pointer in this dispatcher.

---

# 18. Queue consumption

For ordinary events 0..3 Runtime shifts the remaining queue bytes down:

```text
queue[0] = queue[1]
queue[1] = queue[2]
queue[2] = queue[3]
count--
```

This is a literal tiny FIFO.

Event 4 follows a different destruction path.

---

# 19. `+0x20` active event

On queued-event dispatch Runtime stores the selected event ID at:

```text
context +0x20
```

This is wider than the one-byte queue element:

```text
uint32_t activeEvent
```

`EndEvent` and other lifecycle paths inspect this value.

Do not model the context as merely:

```text
IP + state
```

because the currently executing event identity is meaningful runtime state.

---

# 20. `+0x1F` owner-area slot

Context destruction helper:

```text
0x00406320
```

iterates registered contexts and removes those whose:

```text
context +0x1F
```

matches the requested owner.

The best current name is therefore:

```text
ownerAreaSlot
```

This is a runtime slot/context association, not an AREA archive ID.

---

# 21. `+0x1E` registry slot

When a context is inserted into the global registry Runtime stores the selected
slot index at:

```text
context +0x1E
```

The destructor uses it to clear the corresponding global pointer.

This is runtime bookkeeping only.

---

# 22. Global context registry — fixed 32 contexts

Registry storage begins around:

```text
0x004E61E8
```

and contains:

```text
32 dword pointers
```

ending before:

```text
0x004E6268
```

Initialization at:

```text
0x00406270
```

clears all 32 entries.

Context creation scans for the first null entry and installs the new context.

This gives the retail Runtime a fixed maximum registry capacity of:

```text
32 scenario contexts
```

---

# 23. Context destruction — `0x00406390`

The destructor clears the context's registry slot and frees:

```text
optional parameter block
evaluation stack
context object
```

A related helper:

```text
0x00406320
```

destroys all contexts belonging to one owner-area slot.

A modern implementation does not need the old fixed-allocation strategy, but
should preserve ownership/lifetime semantics.

---

# 24. `+0x24` parameter/remap block

Some scenario contexts receive an optional small parameter block.

A recovered creation path around:

```text
0x004094D1
```

allocates:

```text
8 bytes
```

and installs its pointer at:

```text
context +0x24
```

The observed shape is compatible with:

```c
struct ScenarioParameterBlock {
    uint16_t selector;   // +0x00, exact semantics unresolved
    int16_t values[3];   // +0x02, +0x04, +0x06
}; // 0x08
```

Only part of this structure has been directly exercised in the traced path.

Use the conservative term:

```text
event parameter/remap block
```

until all producers are mapped.

---

# 25. Scalar16 operand encoding

Many VM handlers decode a 16-bit operand through a helper-like rule rather
than treating it as an unconditional literal.

Recovered behavior:

```c
int16_t DecodeScalar16(RuntimeScenarioContext* ctx, uint16_t raw)
{
    if (raw != 0xFFFF && (raw & 0x4000) != 0) {
        uint16_t index = raw & ~0x4000;

        return *(int16_t*)(
            (uint8_t*)ctx->parameterBlock
            + 2
            + index * 2);
    }

    return (int16_t)raw;
}
```

This is one of the most important VM encoding features.

---

# 26. Scalar16 reference bit

Bit:

```text
0x4000
```

means approximately:

```text
resolve this value from the current context's parameter/remap block
```

rather than:

```text
use the encoded 16-bit literal directly
```

Special sentinel:

```text
0xFFFF
```

remains literal `-1`, despite having bit `0x4000` set.

---

# 27. Scalar16 parameter index

For a parameter-reference operand:

```text
index = raw & ~0x4000
```

Runtime addresses:

```text
parameterBlock + 2 + index * 2
```

No robust null/range protection is evident in the old executable.

OpenNomad should validate:

```text
parameterBlock exists
index is representable by the block
```

before resolution.

---

# 28. Not every `u16` is Scalar16

Operand interpretation is handler-specific.

Examples of mixed encodings occur in script-launch opcodes.

For instance, a handler can decode:

```text
Scalar16 characterId
raw u16 scriptId
Scalar16 parameter
```

within one instruction.

Therefore a generic decoder cannot say:

```text
all two-byte operands use Scalar16
```

The opcode ABI must describe each operand role individually.

---

# 29. Current OpenNomad Scalar16 gap

Current OpenNomad's `AreaOpcodeInfo` describes two-byte operands only as:

```text
k_int16
```

and sign-extends them directly.

It does not model the:

```text
0x4000 parameter reference
```

behavior.

This is a meaningful compatibility/fidelity gap.

---

# 30. Central interpreter — `0x00406460`

The compact VM interpreter is a native-handler threaded loop.

Core behavior:

```text
while context.state == 1:
    opcode = *context.ip
    context.ip += 1
    descriptor = opcodeTable[opcode]
    descriptor.handler(context)

    if special dispatcher condition:
        return

    if context.state != 1:
        return
```

The opcode handler owns all operand consumption.

---

# 31. Handler-managed instruction length

Runtime does **not** have one generic decoder that first expands the whole
instruction.

Instead:

```text
central loop:
    consumes opcode byte only

native handler:
    reads its own operand encoding
    advances context.ip itself
```

This is why the native opcode table's second dword must not be assumed to be an
operand-byte count.

---

# 32. Native opcode descriptor table — `0x004C0140`

Runtime contains an eight-byte descriptor for each opcode.

Recovered shape:

```c
struct RuntimeScenarioOpcodeDescriptor {
    void (*handler)(RuntimeScenarioContext*);
    uint32_t auxiliaryWord;
}; // 0x08
```

Index:

```text
descriptor = table[opcode]
```

Base:

```text
0x004C0140
```

The central interpreter calls only the first dword.

---

# 33. Opcode table extent

Valid handler descriptors are present for:

```text
0x00 .. 0x98
```

The following entry:

```text
0x99
```

contains:

```text
handler       = 0xFFFFFFFF
auxiliaryWord = 0xFFFFFFFF
```

and acts as an obvious sentinel/end marker for the static table.

This is strong evidence that the retail opcode namespace has:

```text
153 entries
```

from `0x00` through `0x98`.

Not all semantic names are recovered.

---

# 34. Descriptor second dword is unresolved

The `auxiliaryWord` often resembles an operand-byte count for simple opcodes,
but that interpretation fails for multiple confirmed handlers.

Examples:

```text
opcode 0x39:
    auxiliaryWord = 4
    handler consumes 6 operand bytes

opcode 0x4E:
    auxiliaryWord = 2
    handler consumes 4 operand bytes

opcode 0x67:
    auxiliaryWord = 2
    handler consumes 6 operand bytes

opcode 0x76:
    auxiliaryWord = 6
    handler consumes 8 operand bytes
```

The central interpreter does not consult the second word.

Until a genuine consumer is identified, document it only as:

```text
auxiliaryWord
```

Possible authoring/debug/metadata roles remain speculative.

---

# 35. Trusted-data assumption

The original central loop does not show a modern bounds-safe check before
indexing the opcode descriptor table.

Retail Runtime assumes valid shipped bytecode.

A modern interpreter should enforce:

```text
opcode <= 0x98
descriptor.handler is valid
```

and report unsupported/unknown semantics separately from malformed bytecode.

---

# 36. Special interpreter behavior around `0x2D/0x2F/0x3D`

The central interpreter contains special control outside ordinary handler
dispatch.

Recovered examples:

- opcode `0x3D` causes the outer interpreter to return after dispatch rather
  than immediately looping again;
- opcode `0x2F` advances past its three Scalar16 operands and enters recovered
  context state 10 after the native transition coordinator accepts it;
- in one gated condition Runtime can rewind the current opcode byte before
  leaving when the coordinator does not accept the request;
- opcode `0x2D` remains a separate unresolved native area operation.

These opcodes are native area/transition operations rather than pure arithmetic
VM primitives.

Do not implement their lifecycle based only on generic instruction flow.

The recovered dialog record/archive format and subsystem API are documented in
[`iam-dialog.md`](iam-dialog.md). Phase D4 implements `0x3D` as one signed
Scalar16 operand, a typed bridge to `ScenarioManager::start_dialog()`, and an
immediate dispatcher yield. The AREA context remains running in Runtime state 1
with its IP already past the operand. `ScenarioStartupController` then gates
normal AREA servicing while the session `DialogRuntime` is active; gameplay and
world `ScenarioRuntime` ticks continue. At completion it consumes
`DialogRuntime::take_completion()` and permits the same AREA context to resume
from its advanced IP.

This live dialog scheduling gate is separate from both the SCX
`ScenarioControl` command namespace (`0x34..0x3F`) and OpenNomad's one-shot
`ScenarioMode::k_teardown` value 3. It is not an `AreaWaitKind` typed wait and
does not call the startup teardown path.

---

# 37. Shared global-variable storage

Scenario/START variables are not stored per VM context.

Runtime helper:

```text
0x0040E510
```

sets a global variable.

Helper:

```text
0x0040E530
```

gets one.

Both resolve a shared global state pointer around:

```text
0x004E6D94
```

then access a dword array through that global state.

Thus:

> **all scenario contexts access one shared global-variable namespace.**

---

# 38. OpenNomad global-variable mismatch

Current `AreaScriptRuntime` owns:

```cpp
std::unordered_map<std::uint16_t, std::int32_t> m_variables;
```

inside each VM instance.

That makes globals context-local.

Runtime instead uses shared scenario/game state.

This matters for:

- communication between simultaneous AREA contexts;
- interface completion variables;
- zone events;
- area transitions;
- persistent scenario logic.

A faithful implementation should move the variable store above individual
contexts.

---

# 39. Global-variable primitive opcodes

Direct Runtime analysis establishes a much larger primitive family than the
current OpenNomad subset.

```text
0x0A PushGlobalVariable
0x0C SetGlobalVariableZero
0x0D SetGlobalVariableOne
0x0E SetGlobalVariableInt8
0x0F SetGlobalVariableInt16
0x10 SetGlobalVariableInt32
0x11 CopyGlobalVariable
0x12 SetGlobalVariableFromStack
0x13 AddStackToGlobalVariable
0x14 SubtractStackFromGlobalVariable
0x15 MultiplyGlobalVariableByStack
0x16 DivideGlobalVariableByStack
0x17 AndGlobalVariableWithStack
0x18 OrGlobalVariableWithStack
```

These give the compact VM a conventional small expression/state-manipulation
core.

---

# 40. `0x02` — no-op

Handler:

```text
0x00401B80
```

is effectively a no-op/return.

Working name:

```text
NoOp
```

No inline operands have been identified.

---

# 41. `0x03` — `EndEvent`

Handler:

```text
0x00401B90
```

Core action:

```text
context.state = 0
```

It also performs event/lifecycle side effects depending on:

```text
context.flags
context.activeEvent
global scenario state
```

and writes a global byte at:

```text
0x004C012C
```

The high-level name `EndEvent` is appropriate, but it is not merely:

```text
return
```

from the interpreter.

---

# 42. `EndEvent` stack behavior

No explicit stack-depth reset has been observed inside the retail handler.

Current OpenNomad clears its evaluation vector on `EndEvent`.

Treat that as:

```text
modern safety/compatibility behavior
```

rather than an asserted Runtime invariant.

---

# 43. Relative control flow

The VM uses signed relative branches.

For the basic branch family:

```text
base =
    instruction pointer after the displacement operand

target =
    base + signed displacement
```

The displacement itself commonly uses `Scalar16`.

A modern decoder should validate the resulting target lies within the same
bytecode pool.

---

# 44. `0x04` — `JumpRelative`

Handler:

```text
0x00401C50
```

Operand:

```text
Scalar16 displacement
```

Behavior:

```text
ip =
    postOperandIp + displacement
```

---

# 45. `0x05` — `BranchIfTrue`

Handler:

```text
0x00401C90
```

Operand:

```text
Scalar16 displacement
```

Pops one stack value.

If:

```text
value != 0
```

then performs the relative jump.

Current OpenNomad did not include this opcode in its initial compatibility set.

---

# 46. `0x06` — `BranchIfFalse`

Handler:

```text
0x00401CE0
```

Operand:

```text
Scalar16 displacement
```

Pops one stack value.

If:

```text
value == 0
```

then jumps.

This matches the high-level behavior already implemented in OpenNomad.

---

# 47. Stack-push opcodes

## `0x07` — `PushInt8`

Handler:

```text
0x00401D30
```

Operand:

```text
signed int8
```

Pushes sign-extended value.

## `0x08` — `PushScalar16`

Handler:

```text
0x00401D70
```

Operand:

```text
Scalar16
```

Pushes the resolved/literal 16-bit value.

## `0x09` — `PushInt32`

Handler:

```text
0x00401DD0
```

Operand:

```text
raw little-endian int32
```

Pushes the full dword.

---

# 48. `0x0A` — `PushGlobalVariable`

Handler:

```text
0x00401E30
```

Operand:

```text
Scalar16 variableId
```

Reads:

```text
sharedGlobalVariables[variableId]
```

and pushes the dword value.

This is not a context-local lookup.

---

# 49. `0x0B` — `Pop`

Handler:

```text
0x00401EA0
```

Consumes one stack entry by decrementing the depth.

The retail handler assumes a valid stack.

A modern VM should reject underflow.

---

# 50. `0x0C` — `SetGlobalVariableZero`

Handler:

```text
0x00401EB0
```

Operand:

```text
Scalar16 variableId
```

Behavior:

```text
global[variableId] = 0
```

subject to Runtime's side-effect/probe mode.

---

# 51. `0x0D` — `SetGlobalVariableOne`

Handler:

```text
0x00401F10
```

Operand:

```text
Scalar16 variableId
```

Behavior:

```text
global[variableId] = 1
```

Observed in startup AREA bytecode.

---

# 52. `0x0E` — `SetGlobalVariableInt8`

Handler:

```text
0x00401F70
```

Operands:

```text
Scalar16 variableId
int8     value
```

Behavior:

```text
global[variableId] = sign_extend(value)
```

This supersedes the overly generic earlier name:

```text
SetGlobalVariable
```

for opcode `0x0E`.

---

# 53. `0x0F` — `SetGlobalVariableInt16`

Handler:

```text
0x00401FE0
```

Operands:

```text
Scalar16 variableId
Scalar16 value
```

Behavior:

```text
global[variableId] = resolvedValue
```

---

# 54. `0x10` — `SetGlobalVariableInt32`

Handler:

```text
0x00402070
```

Operands:

```text
Scalar16 variableId
int32    value
```

Behavior:

```text
global[variableId] = value
```

---

# 55. `0x11` — `CopyGlobalVariable`

Handler:

```text
0x00402110
```

Operands:

```text
Scalar16 destinationVariable
Scalar16 sourceVariable
```

Behavior:

```text
global[destination] =
    global[source]
```

---

# 56. `0x12` — `SetGlobalVariableFromStack`

Handler:

```text
0x00402190
```

Operand:

```text
Scalar16 variableId
```

Pops stack top and stores it.

---

# 57. Arithmetic-to-global family

Handlers:

```text
0x13  0x00402210
0x14  0x00402290
0x15  0x00402310
0x16  0x00402390
```

Each uses one:

```text
Scalar16 variableId
```

and one stack value.

Recovered semantics:

```text
0x13:
    global[var] += pop()

0x14:
    global[var] -= pop()

0x15:
    global[var] *= pop()

0x16:
    global[var] /= pop()
```

Signed integer division is used where applicable.

---

# 58. Bitwise-to-global family

Handlers:

```text
0x17  0x00402410
0x18  0x00402490
```

Operand:

```text
Scalar16 variableId
```

Semantics:

```text
0x17:
    global[var] &= pop()

0x18:
    global[var] |= pop()
```

---

# 59. Expression opcode block `0x19..0x29`

This block operates directly on the fixed evaluation stack.

For binary operations:

```text
top      = stack[depth - 1]
previous = stack[depth - 2]
```

The operation consumes two operands and leaves one result in the previous
slot.

Be careful with semantic naming because source-language operand order may have
been compiled onto the stack in reverse.

---

# 60. Comparison opcodes

Recovered low-level stack effects:

```text
0x19:
    previous = (top == previous)

0x1A:
    previous = (top < previous)       // signed

0x1B:
    previous = (top > previous)       // signed

0x1C:
    previous = (top <= previous)      // signed

0x1D:
    previous = (top >= previous)      // signed

0x1E:
    previous = (top != previous)
```

Results are integer booleans:

```text
0 or 1
```

---

# 61. Arithmetic opcodes

Recovered low-level effects:

```text
0x1F:
    previous = previous + top

0x20:
    previous = top - previous

0x21:
    previous = previous * top

0x22:
    previous = top / previous
```

`0x22` uses signed integer division.

The apparently reversed subtraction/division order is the literal Runtime stack
operation.

Do not “correct” it without first reconstructing compiler push order.

---

# 62. Bitwise and logical binary operations

```text
0x23:
    previous = previous & top

0x24:
    previous = previous | top

0x25:
    previous = bool(previous) && bool(top)

0x26:
    previous = bool(previous) || bool(top)
```

Logical operations produce:

```text
0 or 1
```

---

# 63. Unary expression operations

```text
0x27:
    top = -top

0x28:
    top = (top == 0)

0x29:
    top = ~top
```

Working names:

```text
Negate
LogicalNot
BitwiseNot
```

---

# 64. Compare-and-branch family `0x2A..0x2C`

These instructions compare the current stack top against an inline value.

They do **not** pop the stack value.

When the values differ they perform a signed relative branch.

---

# 65. `0x2A` — branch if top differs from int8

Operands:

```text
Scalar16 displacement
int8     compareValue
```

Behavior:

```text
if stackTop != compareValue:
    ip = postDisplacementBase + displacement
```

The precise base is determined by the handler's IP progression; document
relative behavior from the recovered handler rather than assuming a generic
assembler convention.

---

# 66. `0x2B` — branch if top differs from Scalar16

Operands:

```text
Scalar16 displacement
Scalar16 compareValue
```

Same branch condition:

```text
stackTop != compareValue
```

---

# 67. `0x2C` — branch if top differs from int32

Operands:

```text
Scalar16 displacement
int32    compareValue
```

Again, stack top remains present after comparison.

---

# 68. Side-effect suppression / probe mode

Runtime contains alternate event-scanning paths around:

```text
0x004060B0
0x00406120
0x00406180
```

They set global:

```text
0x006A05E0 = 1
```

then walk bytecode through the **same native opcode handlers**.

Many side-effecting handlers check this global and suppress their actual native
operation.

This is best described as:

```text
probe / dry-run / side-effect-suppression mode
```

until the original name is recovered.

---

# 69. Why probe mode matters

The VM is not only executed to perform an event.

Runtime can also preflight/classify event bytecode using the real handlers
without triggering their normal side effects.

This means opcode-handler behavior often has two logical phases:

```text
decode / classify
```

and:

```text
perform native side effect
```

guarded by global probe state.

A modern implementation should not infer that every handler always performs its
full action simply because the opcode was visited.

---

# 70. Context flag `0x0010`

Many native/side-effecting handlers set:

```text
context.flags |= 0x0010
```

Pure expression/control primitives generally do not.

Probe scanners use this flag as part of their classification logic.

Strong current interpretation:

```text
event contains/encountered native side-effecting operation
```

or a related execution-class marker.

The exact source-level name is unknown.

Keep:

```text
flags bit 0x10
```

in low-level documentation.

---

# 71. Additional context flag bits

Other bits are used by event-lifecycle/native-operation paths.

For example, `EndEvent` tests bit:

```text
0x0008
```

before freeing one area-slot-owned allocation.

Tracked child/native launch paths can set:

```text
0x0004
```

before entering state 4.

Do not assign one generic Boolean meaning to the entire flags word.

---

# 72. Script-launch opcodes: corrected architecture

A major correction from the initial OpenNomad compatibility model is that the
generic and character-bound SCX launch operations each have a **pair**:

```text
fire-and-forget
tracked/waiting
```

Generic pair:

```text
0x39
0x3A
```

Character-bound pair:

```text
0x3B
0x3C
```

---

# 73. `0x39` — `StartScxScript`

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

Total handler-consumed operand bytes:

```text
6
```

Behavior:

```text
launch generic structured SCX script/native child
continue AREA bytecode
```

It does **not** set context state 4.

Thus:

```text
0x39 = non-tracked/fire-and-forget variant
```

in the recovered Runtime path.

---

# 74. `0x3A` — `StartScxScriptTracked`

Handler:

```text
0x004031E0
```

Uses the same basic operand shape:

```text
raw u16  scriptId
Scalar16 argumentB
Scalar16 argumentC
```

After launch it sets:

```text
context.flags |= 0x0004
context.state = 4
```

This is the tracked/waiting generic variant.

---

# 75. OpenNomad `0x39`/`0x3A` implementation

OpenNomad implements the recovered split directly:

```text
0x39 StartScxScript
    launch concrete ScriptRuntime instance
    continue AREA execution immediately

0x3A StartScxScriptTracked
    launch concrete ScriptRuntime instance
    wait for that exact instance in state 4
```

---

# 76. `0x3B` — `StartCharacterScript`

Handler:

```text
0x00403300
```

Operands:

```text
Scalar16 characterId
raw u16  scriptId
Scalar16 parameter
```

Behavior:

```text
launch character-bound structured script
continue AREA bytecode
```

This is the non-tracked character variant.

---

# 77. `0x3C` — `StartCharacterScriptTracked`

Handler:

```text
0x00403430
```

Same operand shape:

```text
Scalar16 characterId
raw u16  scriptId
Scalar16 parameter
```

After launch:

```text
context.flags |= 0x0004
context.state = 4
```

This is the tracked character-bound variant.

---

# 78. State 4 is broader than “character-script wait”

Earlier documentation tied Runtime state 4 specifically to opcode `0x3C`.

That is too narrow.

State 4 is also entered by:

```text
0x3A
```

and at least one earlier native opcode path around:

```text
0x2E
```

Therefore use a broader working description:

```text
tracked native/child-operation wait
```

until the common completion mechanism is fully named.

---

# 79. SCX script IDs are raw 16-bit operands

In the launch handlers the structured SCX script ID is not necessarily passed
through the Scalar16 remapper.

This supports the existing structured-script model:

```text
AREA raw scriptId
    ->
lookup SCX template +0x1A
```

The contextual arguments around it can still use Scalar16 indirection.

---

# 80. `0x38` — character lookup/operation

Handler:

```text
0x00402F60
```

Current tracing associates this with character-related lookup/activation using
AREA character context.

Keep the working name:

```text
CharacterLookup
```

as provisional until the complete native call path is recovered.

---

# 81. `0x4E` — character activation

Handler:

```text
0x00403CB0
```

The current Runtime trace establishes a character activation/reactivation
operation associated with AREA table 0.

The exact handler consumes more data than the native descriptor's auxiliary
word might suggest, further proving that the descriptor word is not an
instruction-length field.

Current behavior includes:

- resolving an AREA character;
- restoring/reactivating resident runtime character state;
- optionally applying AREA-authored transform;
- updating persistent presence state;
- special `-1` current-character path.

The original source name remains unknown.

---

# 82. `0x46` — `OpenInterface`

Handler:

```text
0x00403860
```

Operands are three Scalar16 values:

```text
interfaceId
argumentB
result/global selector
```

Total:

```text
6 operand bytes
```

Runtime sets:

```text
context.flags |= 0x0010
context.state = 6
```

and invokes the generic interface subsystem.

---

# 83. Interface runtime globals

The handler stores additional interface/request state in globals.

Observed examples:

```text
0x004C0B64
0x004E6B28
0x004E6C7C
```

Interface 29 has a special stored scenario-context pointer in the recovered
startup path.

These are native subsystem implementation details, not extra serialized
operands.

---

# 84. Interface completion

The previously recovered high-level behavior remains:

```text
open interface
    |
    v
state 6
    |
    v
matching completion/native result
    |
    +-- result can be written to shared scenario/global variable
    |
    v
state returns to running
    |
    v
resume after 0x46
```

Because the handler has already consumed its operands, the stored IP naturally
points at the next instruction while waiting.

---

# 85. Area 118 interface instruction

Retail bytes:

```text
46 1D 00 FF FF 13 00
```

decode as:

```text
interfaceId = 29
argumentB   = -1
resultSlot  = 19
```

Interface 29 is the main menu.

After `0x46`, Runtime exits the interpreter because state is no longer 1.

---

# 86. `0x67` — music operation

Handler:

```text
0x00404FB0
```

Consumes three Scalar16 values:

```text
trackId
operandB
operandC
```

Observed startup instruction:

```text
67 6D 00 01 00 01 00
```

gives:

```text
trackId = 109
operandB = 1
operandC = 1
```

The first operand is firmly the numeric music track.

---

# 87. Music behavior

Runtime avoids restarting the currently active music track when its numeric ID
already matches.

The handler eventually drives music through a native path around:

```text
0x0041E110
```

Existing interpretation:

```text
operandB -> loop-like behavior
operandC -> additional mode/state
```

is plausible and supported by current audio behavior, but only the first
`trackId` role should be treated as fully firm.

---

# 88. Camera opcodes

Current known native handlers:

```text
0x5F -> 0x00404940
0x60 -> 0x00404AF0
```

Both are camera-related and consume six operand bytes.

`0x60` can enter:

```text
state 7
```

for a timed/native wait.

The AREA camera table itself is documented in `iam-area.md`.

---

# 89. State 7

A completion/update path around:

```text
0x00406912
```

can transition a context:

```text
7 -> 1
```

This supports the current interpretation of state 7 as a camera/timed-native
wait.

Do not model state 7 simply as “sleep” because native camera completion/state
may participate.

---

# 90. Scenario timing

The wider scenario system repeatedly exposes a logical time base of:

```text
30 Hz
```

Examples include:

- camera transitions/waits;
- cinematic bars;
- structured-script timing.

The VM instruction interpreter itself is not a “30 instructions per second”
machine.

Rather:

```text
scenario scheduler ticks at game-update cadence
native duration fields use 30 Hz logical units
interpreter runs until state changes / native boundary
```

---

# 91. Cinematic letterbox opcodes

Handlers:

```text
0x84 -> 0x00405A90
0x85 -> 0x00405AB0
```

Both are operand-less.

Recovered high-level semantics:

```text
0x84 BeginCinematicLetterbox
0x85 EndCinematicLetterbox
```

They drive native presentation state around:

```text
0x0041E1B0
```

and use a 60-scenario-unit transition.

At 30 Hz:

```text
60 / 30 = 2 seconds
```

---

# 92. Presentation opcodes `0x76/0x77`

Handlers:

```text
0x76 -> 0x00405180
0x77 -> 0x00405240
```

The native descriptor auxiliary word is:

```text
6
```

but the handlers consume:

```text
8 operand bytes
```

This is another direct counterexample to interpreting the descriptor's second
dword as instruction size.

OpenNomad presentation behavior is:

```text
0x76 / mode 1: fade into authored RGB colour, alpha 0 -> 1
0x77 / mode 2: fade out of authored RGB colour, alpha 1 -> 0
```

The signed duration magnitude is converted from 30 Hz AREA units to seconds.
Both operations are asynchronous relative to AREA; their existing dispatcher
yield does not create a typed wait. A zero-duration mode 1 ends opaque and a
zero-duration mode 2 ends transparent.

OpenNomad preserves the authored low 24-bit RGB value (`0x00RRGGBB`) and does
not infer alpha from the high byte. Presentation interpolation remains sampled
at display rate; AREA duration values remain 30 Hz units.

---

# 93. Native operation states beyond the currently named set

Observed states:

```text
3
5
8
9
10
11
```

participate in area transition, cross-context, or special native operation
paths.

Examples include:

- opcode `0x2D` area-related transition/load behavior;
- opcode `0x2F` gated transition behavior;
- specialized event-3 handling when state 9/10;
- cross-context lifecycle coordination.

Do not prematurely map these to OpenNomad enum names.

---

# 94. Event 3 special handling

Queueing event 3 while a context is in certain transition states:

```text
9
10
```

triggers a special path around:

```text
0x00408530
```

rather than being only a normal FIFO operation.

This is evidence that event IDs have semantic roles in the scenario lifecycle,
not merely arbitrary user-defined numeric labels.

Their exact high-level source names remain unknown.

---

# 95. Event 4 is context destruction

Event 4 does not point to:

```text
event4Entry
```

because no such field exists.

Instead it routes to context destruction/removal behavior.

This explains the fixed three-entry context design.

---

# 96. Main scheduler and registry service

The runtime scenario system owns all registered contexts globally.

Scheduler code around:

```text
0x004080A2
```

and nearby paths iterates/services the fixed registry.

The lifecycle is not:

```text
one AREA record = one self-contained VM object updated independently
```

but:

```text
global scenario manager
    |
    +-- registry context 0
    +-- registry context 1
    +-- ...
    +-- registry context 31
```

with cross-context and area-slot operations.

---

# 97. Context flags reset on new queued event

When the event dispatcher activates a new queued event it resets:

```text
context.flags = 0
```

before selecting the entry pointer and starting state 1.

Thus event-local native classification/tracking bits do not automatically carry
into the next event.

---

# 98. Context active-event identity

The event dispatcher stores the selected queued ID in:

```text
context.activeEvent
```

before bytecode begins.

`EndEvent` uses this field in event-specific lifecycle logic.

This is another reason OpenNomad should eventually represent events explicitly
rather than discarding the queued event value.

---

# 99. AREA primary context

For the initial AREA context, Runtime creates a context whose event-1 pointer is
the relocated primary event pointer from the AREA record.

The primary startup flow then queues:

```text
event 1
```

and scenario scheduling later activates it.

This matches the broad OpenNomad startup ordering:

```text
load AREA
create context
queue event
activate/tick
```

but not the current instruction-pointer simplification.

---

# 100. Zone contexts

AREA and SCENE table-2 zone records supply three neutral event entrypoints to
the context constructor. For SCENE, the serialized fields are retained as
`event1_offset`, `event2_offset`, and `event3_offset`; their trigger meanings
are not established.

Conceptually:

```text
zone record
    |
    +-- event pointer 1
    +-- event pointer 2
    +-- event pointer 3
            |
            v
      zone-owned RuntimeScenarioContext
```

The trigger system can then queue one of events 1/2/3 as zone state changes.

The exact gameplay condition corresponding to each event remains to be traced.

---

# 101. AREA-link/parameterized contexts

Another recovered creation path builds a context from a table-7-style event
entry and attaches the optional 8-byte parameter/remap block.

This is a strong clue that Scalar16 indirection exists to pass event-specific
arguments into shared/reusable bytecode without duplicating the event body.

Conceptually:

```text
shared bytecode event
    +
parameter/remap block
    =
parameterized invocation
```

---

# 102. Why the remap mechanism matters architecturally

Without Scalar16 indirection, authored event bytecode would need literal IDs for
every target entity/area/object.

The `0x4000` encoding allows one event body to reference values supplied by the
context that instantiated it.

This is effectively a tiny invocation-parameter mechanism.

It is not the same thing as the shared global-variable namespace.

---

# 103. Three different value sources

A scenario handler can therefore receive values from at least three different
places:

```text
literal inline operand
    e.g. int8/int32/raw u16

Scalar16 remap operand
    0x4000 | index
    -> context parameter block

shared global variable
    read/write through global variable array
```

These must remain distinct in a faithful decoder.

---

# 104. Native handler versus expression primitive

The opcode namespace mixes:

```text
pure VM primitives
```

with:

```text
native engine operations
```

Examples of pure primitives:

```text
jump
push
pop
compare
arithmetic
global variable manipulation
```

Examples of native operations:

```text
area transition
camera
character
object
interface
music
presentation
SCX script launch
```

Probe mode and context flags help Runtime distinguish/classify these categories
during event scanning.

---

# 105. Opcode `0x00/0x01`

Handlers exist:

```text
0x00 -> 0x00401B00
0x01 -> 0x00401B40
```

Their exact semantic role remains unresolved.

They appear debug/diagnostic-like in the current trace but should remain:

```text
Unknown00
Unknown01
```

in authoritative naming until behavior is fully established.

---

# 106. Opcode `0x2D`

Handler:

```text
0x00402AB0
```

is an area-related native operation.

It interacts with:

```text
global 0x004C0130
```

and can participate in a transition path using state 8.

Working name:

```text
AreaOperation
```

only.

Do not assign a final “LoadArea”/“ChangeArea” name yet.

---

# 107. Opcode `0x2E`

Handler:

```text
0x00402C30
```

enters state 4 in the recovered path.

It is another tracked native operation.

Working name:

```text
TrackedNativeOperation
```

until the actual subsystem/action is established.

---

# 108. Opcode `0x2F`

Handler:

```text
0x00402D20
```

consumes exactly six operand bytes: three Runtime Scalar16 values. Operand 0 is
an `AREAS` ID; operands 1 and 2 select transition variants whose generic names
remain unresolved. The confirmed startup instruction is:

```text
+0x10D  2F DE 00 FF FF FF FF
         target AREA 222, operand_b -1, operand_c -1
```

Accepted execution advances the instruction pointer by all seven bytes and
blocks the calling AREA context in recovered Runtime state 10. A session-level
native coordinator prepares the destination using the alternate resident AREA
slot, but leaves it `LoadedInactive` while the source remains the active
presentation world. The context resumes from its post-instruction IP after
preparation; later `0x47` attaches SCENE data and commits presentation
ownership.

OpenNomad implements this as:

```text
BeginAreaTransition request
  -> generation-tagged ScenarioStartupController coordinator
  -> alternate RuntimeAreaSlot and WorldSceneContext preparation
  -> source stays LoadedActive and destination is LoadedInactive
  -> exact handle completes AreaWaitKind::k_area_transition
  -> old AREA context resumes in Runtime state 1
```

The current loader performs resource preparation synchronously when the
coordinator is serviced on the next scenario tick; the accepted request and
state-10 boundary are nevertheless persistent and externally observable.
Unsupported non-`-1` operand variants and unresolved Scalar16 parameter
references fail explicitly rather than being guessed.

---

# 109. AREA/SCENE handoff opcodes

The compact VM supports three nonblocking operations used after the prepared
AREA transition:

| Opcode | Operands | Operation |
| --- | --- | --- |
| `0x30` | Scalar16 AREA ID | `ReleaseArea`: release the requested inactive resident AREA after resetting any attached SCENE context/state. |
| `0x47` | Scalar16 AREA ID, Scalar16 SCENE ID | `AttachAreaScene`: replace/attach the SCENE, queue its independent compact event 1, then commit the prepared destination active. |
| `0x49` | Scalar16 address ID | `PlaceCurrentCharacterAtAddress`: resolve the address across both resident AREA table-5 collections and move only the established controlled character. |

`0x47`, `0x49`, and `0x30` do not enter a VM wait state. SCENE uses the same
interpreter and service bridges as AREA, but its compact context is independent:
an unsupported SCENE opcode pauses that SCENE context without stopping its
parent AREA context.

---

# 110. Native opcode descriptor table

The complete recovered descriptor inventory follows.

The second column is the handler address.

The third is the unresolved auxiliary dword stored beside the handler.

```text
Opcode  Handler     Aux
------  ----------  ----------
00      00401B00    0
01      00401B40    0
02      00401B80    0
03      00401B90    0
04      00401C50    2
05      00401C90    2
06      00401CE0    2
07      00401D30    1
08      00401D70    2
09      00401DD0    4
0A      00401E30    2
0B      00401EA0    0
0C      00401EB0    2
0D      00401F10    2
0E      00401F70    3
0F      00401FE0    4
10      00402070    5
11      00402110    2
12      00402190    2
13      00402210    2
14      00402290    2
15      00402310    2
16      00402390    2
17      00402410    2
18      00402490    2
19      00402510    0
1A      00402550    0
1B      00402590    0
1C      004025D0    0
1D      00402610    0
1E      00402650    0
1F      00402690    0
20      004026D0    0
21      00402710    0
22      00402750    0
23      00402790    0
24      004027D0    0
25      00402810    0
26      00402860    0
27      004028B0    0
28      004028E0    0
29      00402910    0
2A      00402940    0
2B      004029A0    0
2C      00402A30    0
2D      00402AB0    2
2E      00402C30    4
2F      00402D20    6
30      00402E10    2
31      0040A440    6
32      0040A4D0    4
33      0040A5A0    4
34      0040A6A0    4
35      00402E80    2
36      00402ED0    0
37      00402EF0    2
38      00402F60    2
39      004030E0    4
3A      004031E0    4
3B      00403300    6
3C      00403430    6
3D      00403560    2
3E      004035D0    4
3F      00403730    2
40      00403780    2
41      004037F0    2
42      0040A910    2
43      0040A9D0    4
44      0040AAF0    0
45      0040AC20    2
46      00403860    6
47      00403950    4
48      00403AF0    2
49      00403C30    2
4A      00403CA0    0
4B      0040AC90    2
4C      0040ACF0    2
4D      0040ADD0    2
4E      00403CB0    2
4F      00403DD0    2
50      00403E80    0
51      00403F10    2
52      00403FB0    2
53      00404030    2
54      00404090    4
55      00404170    4
56      00404230    6
57      00404330    2
58      00404390    2
59      004043F0    2
5A      00404450    4
5B      00404530    2
5C      00404590    2
5D      00404790    6
5E      004048D0    2
5F      00404940    6
60      00404AF0    6
61      00404CE0    2
62      00404DB0    4
63      00404EB0    2
64      00404F60    4
65      00404FA0    0
66      00405090    0
67      00404FB0    2
68      004050A0    0
69      004050C0    0
6A      00405300    0
6B      00405310    0
6C      00405320    0
6D      00405330    0
6E      00405340    0
6F      00405350    0
70      00405360    0
71      00405370    2
72      004053D0    2
73      00405420    2
74      004050E0    0
75      00405130    0
76      00405180    6
77      00405240    6
78      00405480    6
79      00405540    2
7A      00405570    2
7B      004055C0    2
7C      00405610    0
7D      00405620    0
7E      00405630    6
7F      00405800    0
80      00405810    8
81      004059D0    0
82      004059F0    0
83      00405A10    4
84      00405A90    0
85      00405AB0    0
86      00405AD0    0
87      00405AF0    0
88      00405B10    4
89      00405BA0    0
8A      00405BB0    2
8B      00405C30    2
8C      00405CB0    0
8D      00405CC0    0
8E      00405CD0    4
8F      00405DD0    2
90      00405E30    2
91      00405EF0    2
92      00405F00    2
93      00405F20    2
94      00405F40    2
95      00405FC0    2
96      00406050    2
97      00406070    2
98      00406090    0
99      FFFFFFFF    FFFFFFFF
```

The table is useful even where semantics are unknown because it gives a
complete set of native handler entrypoints for systematic future RE.

---

# 111. Current named opcode map

High-confidence or useful current names:

| Opcode | Current name | Confidence |
|---:|---|---|
| `0x02` | `NoOp` | Runtime |
| `0x03` | `EndEvent` | Runtime behavior |
| `0x04` | `JumpRelative` | Runtime |
| `0x05` | `BranchIfTrue` | Runtime |
| `0x06` | `BranchIfFalse` | Runtime |
| `0x07` | `PushInt8` | Runtime |
| `0x08` | `PushScalar16` | Runtime |
| `0x09` | `PushInt32` | Runtime |
| `0x0A` | `PushGlobalVariable` | Runtime |
| `0x0B` | `Pop` | Runtime |
| `0x0C` | `SetGlobalVariableZero` | Runtime |
| `0x0D` | `SetGlobalVariableOne` | Runtime |
| `0x0E` | `SetGlobalVariableInt8` | Runtime |
| `0x0F` | `SetGlobalVariableInt16` | Runtime |
| `0x10` | `SetGlobalVariableInt32` | Runtime |
| `0x11` | `CopyGlobalVariable` | Runtime |
| `0x12` | `SetGlobalVariableFromStack` | Runtime |
| `0x13` | `AddStackToGlobalVariable` | Runtime |
| `0x14` | `SubtractStackFromGlobalVariable` | Runtime |
| `0x15` | `MultiplyGlobalVariableByStack` | Runtime |
| `0x16` | `DivideGlobalVariableByStack` | Runtime |
| `0x17` | `AndGlobalVariableWithStack` | Runtime |
| `0x18` | `OrGlobalVariableWithStack` | Runtime |
| `0x19` | `Equal` | Runtime |
| `0x1A` | low-level `<` comparison | Runtime |
| `0x1B` | low-level `>` comparison | Runtime |
| `0x1C` | low-level `<=` comparison | Runtime |
| `0x1D` | low-level `>=` comparison | Runtime |
| `0x1E` | `NotEqual` | Runtime |
| `0x1F` | `Add` | Runtime |
| `0x20` | low-level subtract | Runtime |
| `0x21` | `Multiply` | Runtime |
| `0x22` | low-level divide | Runtime |
| `0x23` | `BitwiseAnd` | Runtime |
| `0x24` | `BitwiseOr` | Runtime |
| `0x25` | `LogicalAnd` | Runtime |
| `0x26` | `LogicalOr` | Runtime |
| `0x27` | `Negate` | Runtime |
| `0x28` | `LogicalNot` | Runtime |
| `0x29` | `BitwiseNot` | Runtime |
| `0x2A` | compare top vs int8 + branch | Runtime |
| `0x2B` | compare top vs Scalar16 + branch | Runtime |
| `0x2C` | compare top vs int32 + branch | Runtime |
| `0x2D` | `AreaOperation` | provisional |
| `0x2E` | tracked native operation | provisional |
| `0x2F` | `BeginAreaTransition` | implemented; state-10 two-slot native wait |
| `0x30` | `ReleaseArea` | implemented; one Scalar16, nonblocking |
| `0x38` | `CharacterLookup` | provisional |
| `0x39` | `StartScxScript` | strongly recovered |
| `0x3A` | `StartScxScriptTracked` | strongly recovered |
| `0x3B` | `StartCharacterScript` | strongly recovered |
| `0x3C` | `StartCharacterScriptTracked` | strongly recovered |
| `0x46` | `OpenInterface` | strongly recovered |
| `0x47` | `AttachAreaScene` | implemented; two Scalar16 values, nonblocking |
| `0x49` | `PlaceCurrentCharacterAtAddress` | implemented; one Scalar16, nonblocking |
| `0x4E` | `ActivateCharacter` | provisional name, behavior traced |
| `0x4F` | character selection/reset | provisional |
| `0x5C` | object activation | provisional |
| `0x5F` | camera select | provisional |
| `0x60` | camera move/wait | provisional |
| `0x67` | music operation | track ID firm |
| `0x68` | subsystem activation | provisional |
| `0x76` | presentation effect | provisional |
| `0x77` | alternate presentation effect | provisional |
| `0x83` | subsystem operation | provisional |
| `0x84` | `BeginCinematicLetterbox` | strongly recovered |
| `0x85` | `EndCinematicLetterbox` | strongly recovered |

Unknown handlers should remain unknown rather than receiving speculative names.

---

# 112. Descriptor table is a systematic RE roadmap

Because every opcode has a stable native entrypoint, completing the VM catalogue
is straightforward in principle:

```text
for opcode 0x00..0x98:
    disassemble handler
    determine exact operand reads
    determine Scalar16/raw semantics
    determine IP advance
    determine stack/global/native side effects
    determine state changes
    determine probe-mode behavior
```

This is much safer than inferring instruction boundaries from retail byte
patterns alone.

---

# 113. Area 118 VM worked example

AREA 118:

```text
record size:
    0x09C0

bytecode pool:
    0x03FC .. 0x051B

camera table:
    begins 0x051C

primary event pointer:
    +0x04 = 0x03FC
```

On load:

```text
0x03FC record-relative
    ->
relocated live byte pointer
```

That pointer becomes the context's:

```text
event1Entry
```

---

# 114. Area 118 initial event flow

Conceptually:

```text
create context
    event1Entry = AREA + 0x03FC

queue event 1

scheduler sees context state 0
    |
    v
event service:
    activeEvent = 1
    flags = 0
    IP = event1Entry
    state = 1
    |
    v
interpreter 0x00406460
```

This is more precise than:

```text
construct VM over script bytes and set IP=0
```

---

# 115. Area 118 initial instructions

The startup event begins:

```text
0D AF 00
0E AA 00 32
38 88 00
4F FF FF
68
5C E5 03
83 00 00 01 00
67 6D 00 01 00 01 00
...
```

The first two instructions are straightforward shared-global writes:

```text
0D AF 00
    SetGlobalVariableOne(0x00AF)

0E AA 00 32
    SetGlobalVariableInt8(0x00AA, 0x32)
```

The later native instructions coordinate startup presentation/world state.

---

# 116. Area 118 main-menu wait

Eventually:

```text
46 1D 00 FF FF 13 00
```

executes.

The handler:

```text
opens interface 29
sets state 6
leaves IP pointing after the instruction
```

The central interpreter returns because:

```text
state != 1
```

The VM therefore does not execute the instructions after `0x46` until interface
completion resumes the context.

---

# 117. The main menu is an ordinary VM wait

Architectural consequence:

```text
main menu
```

is not a special hard-coded program-mode loop from the perspective of AREA
bytecode.

It is:

```text
AREA event
    ->
OpenInterface(29)
    ->
scenario context state 6
    ->
native UI owns interaction
    ->
completion
    ->
scenario VM resumes
```

This aligns with the generic interface architecture recovered elsewhere.

---

# 118. Current OpenNomad VM architecture

Current `AreaScriptRuntime` is a safe compatibility interpreter using:

```text
std::span<byte> script
deque<u16> queued events
size_t IP
vector<int32> stack
per-instance unordered_map global variables
typed wait object
typed native sinks
unsupported-opcode pause state
instruction budget
```

Many of those modern abstractions are good design choices.

They should, however, encode the recovered Runtime semantics rather than
silently replacing them.

---

# 119. OpenNomad mismatch: event IDs are discarded

Current `run()` behavior:

```text
pop one queued event
set IP = 0
clear evaluation stack
start running
```

The actual event value does not choose an entrypoint.

Runtime instead:

```text
event 1 -> event1Entry
event 2 -> event2Entry
event 3 -> event3Entry
event 4 -> destroy
```

This is a major architectural gap.

---

# 120. OpenNomad mismatch: one script span per context

Current constructor receives:

```text
one script byte span
```

and all execution begins at offset zero.

The Runtime context owns:

```text
three entry pointers into a shared bytecode pool
```

and can execute multiple events over its lifetime.

A more faithful modern model should pair:

```text
shared immutable bytecode pool
+
context-relative entry offsets
```

---

# 121. OpenNomad mismatch: event queue

Current queue:

```text
std::deque<uint16_t>
```

with unconstrained push.

Runtime:

```text
4-byte FIFO
event ID is u8
event 2 has special deduplication
```

OpenNomad need not use an unsafe fixed C array, but should reproduce the
observable capacity/dedup behavior if fidelity is desired.

---

# 122. OpenNomad mismatch: stack capacity

Current:

```text
std::vector<int32_t>
```

with dynamic growth.

Runtime:

```text
16 fixed dword slots
```

Recommended modern behavior:

```text
std::array<int32_t,16>
checked depth
structured overflow/underflow errors
```

This preserves the Runtime capacity while avoiding corruption.

---

# 123. OpenNomad mismatch: stack reset

Current OpenNomad clears the stack when a new event starts and on `EndEvent`.

Runtime does not visibly do so in the recovered paths.

Options:

```text
strict fidelity:
    preserve stack depth and require bytecode balance

safe compatibility:
    reset but document deviation
```

Given shipped bytecode should balance correctly, the practical difference may
be negligible, but the choice should be explicit.

---

# 124. OpenNomad mismatch: globals are context-local

Current:

```text
AreaScriptRuntime::m_variables
```

is private to one VM.

Runtime global-variable helpers use shared scenario/game state.

Recommended architecture:

```text
ScenarioGlobalVariables
    owned by ScenarioEngine/manager

AreaContext A -> reference
AreaContext B -> reference
AreaContext C -> reference
```

---

# 125. OpenNomad mismatch: Scalar16 indirection

Current decoder sign-extends all configured i16 operands directly.

Runtime frequently applies:

```text
0x4000 parameter reference
```

resolution.

A faithful typed operand system should distinguish:

```text
RawI16
Scalar16
RawU16
I8
I32
```

rather than only byte widths.

---

# 126. OpenNomad status: opcodes `0x39` and `0x3A`

OpenNomad now distinguishes:

```text
0x39 non-tracked/fire-and-forget
0x3A tracked state-4 variant keyed to the exact launched instance
```

---

# 127. OpenNomad mismatch: incomplete primitive set

Current compatibility set omits several now-directly-recovered core opcodes,
including:

```text
0x05 BranchIfTrue
0x08 PushScalar16
0x09 PushInt32
0x0B Pop
0x0C SetGlobalVariableZero
0x0F..0x18 global operations
0x1A..0x29 expression family
0x2A..0x2C compare/branch family
```

These are high-value implementation targets because they are pure VM behavior
and do not depend on large missing renderer/world subsystems.

---

# 128. OpenNomad safety features that should remain

Several behaviors are intentionally better than the 1999 trusted-data
implementation:

```text
bounds-check operand reads
bounds-check jump targets
detect stack underflow/overflow
reject invalid opcode indexes
structured unsupported-opcode pause information
instruction budget against infinite loops
typed native requests instead of raw subsystem globals
```

These are compatible with faithful semantics.

The goal is not to reproduce memory unsafety.

---

# 129. Recommended modern VM structure

A Runtime-faithful but safe design could be:

```cpp
struct ScenarioBytecodePool {
    std::span<const std::byte> bytes;
    std::size_t areaRecordBaseOffset;
};

struct ScenarioEventEntrypoints {
    std::optional<std::size_t> event1;
    std::optional<std::size_t> event2;
    std::optional<std::size_t> event3;
};

struct ScenarioContext {
    const ScenarioBytecodePool* pool;

    ScenarioEventEntrypoints entrypoints;
    std::optional<std::size_t> ip;

    std::array<std::int32_t, 16> stack;
    std::uint16_t stackDepth;

    RuntimeScenarioState state;

    std::array<std::uint8_t, 4> eventQueue;
    std::uint16_t queuedEventCount;

    std::uint8_t ownerAreaSlot;
    std::uint32_t activeEvent;

    std::optional<ScenarioParameterBlock> parameters;

    std::uint16_t flags;
};
```

Runtime-only registry-slot details do not need to be serialized into this object
if ownership is managed elsewhere.

---

# 130. Recommended shared services

The VM should depend on externally owned services:

```text
ScenarioGlobalVariables
ScenarioContextRegistry
Interface bridge
SCX Script bridge
Character bridge
Object bridge
Camera bridge
Area-transition bridge
Audio bridge
Presentation bridge
Persistence bridge
```

This preserves current OpenNomad's useful typed-sink direction while matching
the original shared-state architecture.

---

# 131. Recommended typed operand descriptors

Instead of width-only metadata:

```cpp
enum class ScenarioOperandKind {
    Int8,
    Int32,
    RawInt16,
    RawUInt16,
    Scalar16,
};
```

Each opcode can then describe:

```text
width
signedness
parameter-remap behavior
semantic role
```

This prevents accidental application of Scalar16 remapping to raw script IDs.

---

# 132. Recommended handler contract

A safe modern handler can still mirror Runtime's IP ownership:

```text
dispatcher:
    fetch opcode
    advance by 1
    lookup descriptor
    invoke handler

handler:
    decode exact operand kinds
    advance IP
    mutate stack/globals/context/native state
```

Alternatively OpenNomad can predecode operands, but its descriptor must encode
the **real handler ABI**, not guessed widths from samples.

---

# 133. Recommended event queue behavior

```text
queue_event(event):

    if event == 2:
        if activeEvent == 2:
            reject duplicate

        if 2 already queued:
            reject duplicate

    if queue count == 4:
        report queue full
        do not append

    append uint8 event
```

A modern API can return a typed result:

```text
Queued
DuplicateSuppressed
QueueFull
InvalidEvent
```

even though Runtime itself did not expose such a clean interface.

---

# 134. Recommended event dispatcher

Conceptually:

```cpp
void service_event_queue(Context& ctx)
{
    if (ctx.state != State::Inactive)
        return;

    if (ctx.queue.empty())
        return;

    uint8_t event = pop_front(ctx.queue);

    ctx.activeEvent = event;
    ctx.flags = 0;

    switch (event) {
    case 1:
        start(ctx.event1);
        break;

    case 2:
        start(ctx.event2);
        break;

    case 3:
        start(ctx.event3);
        break;

    case 4:
        destroy(ctx);
        break;

    default:
        break;
    }
}
```

Special state-9/10/event-3 behavior must eventually be layered on top.

---

# 135. Recommended Scalar16 representation

Rather than immediately collapsing operands to an integer:

```cpp
struct Scalar16Operand {
    std::uint16_t raw;
};
```

Resolution:

```cpp
expected<int16_t, Error>
resolve_scalar16(
    const ScenarioContext& ctx,
    Scalar16Operand operand);
```

This preserves:

- raw serialized value for traces;
- sentinel `0xFFFF`;
- parameter-reference bit;
- remap index.

---

# 136. Recommended global-variable store

A shared store should expose:

```text
get(id)
set(id,value)
```

and optionally attach:

```text
VARIABLES.TAG name
```

for diagnostics.

The VM should not know persistence/storage implementation details.

This also lets native interface completion write into the same shared variable
namespace used by bytecode.

---

# 137. Recommended trace format

For one executed instruction:

```text
context registry ID
owner area slot
active event
record-relative IP
opcode
handler address
raw operand bytes
decoded operand kinds/values
stack before/after
state before/after
flags before/after
native request / global write
```

This would make Runtime-vs-OpenNomad trace comparison much easier.

---

# 138. Recommended opcode validation

For each opcode definition:

```text
opcode number
Runtime handler address
auxiliaryWord
operand schema
Scalar16/raw semantics
stack effect
global-variable effect
context-state effect
context-flag effect
native side effects
probe-mode behavior
confidence
```

The static handler table makes this a finite catalogue task.

---

# 139. Recommended regression tests: context

- [ ] context supports three independent event entrypoints;
- [ ] event 1 selects entry 1;
- [ ] event 2 selects entry 2;
- [ ] event 3 selects entry 3;
- [ ] event 4 destroys/removes;
- [ ] active event is stored;
- [ ] flags reset when new event begins;
- [ ] queue capacity is four;
- [ ] duplicate queued event 2 is suppressed;
- [ ] currently active event 2 suppresses another event 2;
- [ ] other duplicates remain allowed unless later Runtime evidence says
      otherwise.

---

# 140. Recommended regression tests: stack

- [ ] stack has 16-value logical capacity;
- [ ] overflow reports structured error;
- [ ] underflow reports structured error;
- [ ] PushInt8 sign-extends;
- [ ] PushScalar16 resolves literal;
- [ ] PushScalar16 resolves remapped parameter;
- [ ] `0xFFFF` remains literal `-1`;
- [ ] PushInt32 preserves all bits;
- [ ] Pop reduces depth;
- [ ] binary expression stack effects match Runtime ordering;
- [ ] unary operations modify only top.

---

# 141. Recommended regression tests: globals

- [ ] two contexts see the same global store;
- [ ] SetZero;
- [ ] SetOne;
- [ ] SetInt8 sign extension;
- [ ] SetInt16 Scalar16 resolution;
- [ ] SetInt32;
- [ ] CopyVariable;
- [ ] SetFromStack;
- [ ] arithmetic-to-global;
- [ ] bitwise-to-global;
- [ ] interface result writes to the shared store.

---

# 142. Recommended regression tests: control flow

- [ ] JumpRelative;
- [ ] BranchIfTrue pops;
- [ ] BranchIfFalse pops;
- [ ] compare-and-branch `0x2A..0x2C` do not pop;
- [ ] negative displacements;
- [ ] positive displacements;
- [ ] target before pool start rejected safely;
- [ ] target beyond pool end rejected safely;
- [ ] infinite loop bounded by modern instruction budget.

---

# 143. Recommended regression tests: SCX launch pairs

- [ ] `0x39` launches generic SCX script and continues;
- [ ] `0x3A` launches generic SCX script and enters state 4;
- [ ] `0x3B` launches character-bound script and continues;
- [ ] `0x3C` launches character-bound script and enters state 4;
- [ ] raw script ID is not Scalar16-remapped;
- [ ] contextual argument Scalar16 fields are remapped;
- [ ] tracked completion resumes at already-advanced IP.

---

# 144. Recommended regression tests: startup

AREA 118:

- [ ] primary event points to record `+0x03FC`;
- [ ] event 1 begins at that entry;
- [ ] `0x0D` sets shared variable `0xAF` to 1;
- [ ] `0x0E` sets shared variable `0xAA` to `0x32`;
- [ ] music instruction selects track 109;
- [ ] `0x46` opens interface 29;
- [ ] context enters state 6;
- [ ] IP points after the seven-byte interface instruction;
- [ ] completion resumes from that exact IP;
- [ ] no camera-table bytes are treated as bytecode.

---

# 145. Useful Runtime addresses

Core context/runtime:

| Address | Role |
|---:|---|
| `0x004060B0` | probe/dry-run event scan |
| `0x00406120` | alternate probe scan |
| `0x00406180` | additional event scan path |
| `0x00406270` | clear 32-context registry |
| `0x00406290` | create `0x2C` scenario context |
| `0x00406320` | destroy contexts by owner-area slot |
| `0x00406390` | destroy one context |
| `0x004063D0` | queue event |
| `0x00406460` | central bytecode interpreter |
| `0x004080A2` | scenario-context scheduler region |
| `0x00408220` | queued-event service / entrypoint selection |
| `0x00408530` | special transition/event handling |
| `0x004094D1` | parameterized/one-shot context creation |
| `0x0040CC90` | AREA load/relocation path |
| `0x0040E510` | shared global-variable setter |
| `0x0040E530` | shared global-variable getter |

---

# 146. Useful primitive handler addresses

| Opcode | Address | Working role |
|---:|---:|---|
| `0x03` | `0x00401B90` | EndEvent |
| `0x04` | `0x00401C50` | JumpRelative |
| `0x05` | `0x00401C90` | BranchIfTrue |
| `0x06` | `0x00401CE0` | BranchIfFalse |
| `0x07` | `0x00401D30` | PushInt8 |
| `0x08` | `0x00401D70` | PushScalar16 |
| `0x09` | `0x00401DD0` | PushInt32 |
| `0x0A` | `0x00401E30` | PushGlobalVariable |
| `0x0B` | `0x00401EA0` | Pop |
| `0x0C` | `0x00401EB0` | SetGlobalVariableZero |
| `0x0D` | `0x00401F10` | SetGlobalVariableOne |
| `0x0E` | `0x00401F70` | SetGlobalVariableInt8 |
| `0x0F` | `0x00401FE0` | SetGlobalVariableInt16 |
| `0x10` | `0x00402070` | SetGlobalVariableInt32 |
| `0x11` | `0x00402110` | CopyGlobalVariable |
| `0x12` | `0x00402190` | SetGlobalVariableFromStack |
| `0x13` | `0x00402210` | AddStackToGlobalVariable |
| `0x14` | `0x00402290` | SubtractStackFromGlobalVariable |
| `0x15` | `0x00402310` | MultiplyGlobalVariableByStack |
| `0x16` | `0x00402390` | DivideGlobalVariableByStack |
| `0x17` | `0x00402410` | AND global with stack |
| `0x18` | `0x00402490` | OR global with stack |
| `0x19..0x29` | `0x00402510..0x00402910` | expression family |
| `0x2A` | `0x00402940` | compare/branch int8 |
| `0x2B` | `0x004029A0` | compare/branch Scalar16 |
| `0x2C` | `0x00402A30` | compare/branch int32 |

---

# 147. Useful native handler addresses

| Opcode | Address | Current role |
|---:|---:|---|
| `0x39` | `0x004030E0` | StartScxScript |
| `0x3A` | `0x004031E0` | StartScxScriptTracked |
| `0x3B` | `0x00403300` | StartCharacterScript |
| `0x3C` | `0x00403430` | StartCharacterScriptTracked |
| `0x46` | `0x00403860` | OpenInterface |
| `0x4E` | `0x00403CB0` | character activation path |
| `0x5F` | `0x00404940` | camera operation |
| `0x60` | `0x00404AF0` | camera wait operation |
| `0x67` | `0x00404FB0` | music |
| `0x76` | `0x00405180` | presentation effect |
| `0x77` | `0x00405240` | alternate presentation effect |
| `0x83` | `0x00405A10` | subsystem operation |
| `0x84` | `0x00405A90` | begin cinematic letterbox |
| `0x85` | `0x00405AB0` | end cinematic letterbox |

---

# 148. Important global addresses

```text
0x004C012C
    event/debug/status byte touched by EndEvent

0x004C0130
    special area/opcode execution gate

0x004C013C
    current music-track state

0x004C0140
    RuntimeScenarioOpcodeDescriptor table

0x004E61E8
    first context-registry pointer

0x004E6268
    end of 32-pointer registry

0x004E6D94
    shared scenario/START state pointer

0x006A05E0
    probe/dry-run side-effect suppression flag
```

Names are descriptive unless established by diagnostics.

---

# 149. Current OpenNomad source locations

VM metadata/runtime:

```text
src/core/Core/Script/AreaScriptOpcode.hpp
src/core/Core/Script/AreaScriptRuntime.hpp
src/core/Core/Script/AreaScriptRuntime.cpp
```

AREA parser:

```text
src/core/Core/Omikron/IamArea.hpp
src/core/Core/Omikron/IamArea.cpp
```

Scenario orchestration:

```text
src/core/Core/Scenario/ScenarioEngine.*
src/core/Core/Scenario/ScenarioManager.*
src/core/Core/Scenario/ScenarioRuntime.*
src/core/Core/Scenario/ScenarioStartupController.*
```

Structured script bridge:

```text
src/core/Core/Script/ScriptRuntime.*
src/core/Core/Omikron/SCX.*
```

Tests:

```text
src/core/Tests/IamArea.spec.cpp
src/core/Tests/AreaScriptRuntime.spec.cpp
src/core/Tests/ScenarioRuntime.spec.cpp
```

---

# 150. High-value next reverse-engineering targets

## 150.1 Complete opcode semantic map

The static table gives handler addresses for every opcode through `0x98`.

This should now be treated as a finite audit.

## 150.2 Resolve opcode-descriptor auxiliary word

Find any editor/debug/runtime consumer outside the central dispatcher.

Do not call it operand size until its true use is established.

## 150.3 State 3/5/8/9/10/11

Recover exact lifecycle names and completion paths.

## 150.4 Zone event 1/2/3 semantics

Trace trigger geometry state changes and determine the exact event mapping.

## 150.5 Parameter block producers

Map all paths that allocate/write `context +0x24`.

Determine:

- selector meaning;
- true maximum parameter count;
- which event/table types use remapping.

## 150.6 Context flag bits

Map every read/write of:

```text
+0x28
```

and assign names only after call-path evidence.

## 150.7 Probe scanners

Identify why Runtime scans event bytecode in suppressed-side-effect mode and
what decisions are made from flags/opcode stops.

## 150.8 Generic state-4 completion

Map the common completion mechanism used by:

```text
0x2E
0x3A
0x3C
```

rather than modeling it as only a character-script wait.

---

# 151. Documentation correction required in `iam-area.md`

The VM pass adds one concrete correction to the AREA document.

Table 2 currently needs to be described as:

```text
+0x00  event1Offset
+0x04  event2Offset
+0x08  event3Offset
```

rather than treating only `+0x00` as an event entry.

Runtime AREA loading relocates all three nonzero dwords, and zone-context
creation passes them as the three scenario-context event pointers.

The semantic gameplay names remain unknown.

---

# 152. Documentation correction required in `script-opcodes.md`

The dedicated VM audit also supersedes two earlier compact-VM assumptions:

```text
0x39:
    is non-tracked StartScxScript

0x3A:
    is the tracked/state-4 generic variant
```

and:

```text
state 4:
    is not character-script-specific
```

`script-opcodes.md` should eventually be updated to point to this dedicated
document and remove duplicated stale AREA-VM details.

---

# 153. Compact Runtime ABI reference

```text
RuntimeScenarioContext
======================

+00  event1Entry*
+04  event2Entry*
+08  event3Entry*
+0C  instructionPtr*

+10  int32 stack*
+14  u16 stackDepth
+16  u16 state

+18  u8 queuedEvents[4]
+1C  u16 queuedEventCount

+1E  u8 registrySlot
+1F  u8 ownerAreaSlot

+20  u32 activeEvent
+24  parameterBlock*

+28  u16 flags
+2A  i16 externalToken

size = 0x2C
```

Stack:

```text
16 dwords
0x40 bytes
```

Global context registry:

```text
32 contexts
```

---

# 154. Compact event reference

```text
event 1 -> context +00
event 2 -> context +04
event 3 -> context +08
event 4 -> destroy/remove context
```

Queue:

```text
4 x u8
FIFO
event 2 specifically deduplicated
```

On activation:

```text
activeEvent = event
flags = 0
IP = selected entry
state = 1
```

---

# 155. Compact interpreter reference

```text
while state == 1:

    opcode = *IP
    IP++

    handler =
        table[opcode].handler

    handler(context)
        reads its own operands
        advances IP
        mutates stack/global/native state

    if state != 1:
        stop
```

Opcode table:

```text
base      = 0x004C0140
entry     = 8 bytes
valid     = 0x00..0x98
sentinel  = 0x99
```

---

# 156. Compact Scalar16 reference

```text
raw == 0xFFFF:
    literal -1

else if raw & 0x4000:
    index = raw & ~0x4000
    value =
        *(i16*)(parameterBlock + 2 + index*2)

else:
    literal signed 16-bit value
```

Not every 16-bit operand uses this decoder.

---

# 157. Compact stack/global primitive reference

```text
02 NoOp

03 EndEvent

04 JumpRelative
05 BranchIfTrue
06 BranchIfFalse

07 PushInt8
08 PushScalar16
09 PushInt32
0A PushGlobalVariable
0B Pop

0C SetGlobalVariableZero
0D SetGlobalVariableOne
0E SetGlobalVariableInt8
0F SetGlobalVariableInt16
10 SetGlobalVariableInt32
11 CopyGlobalVariable
12 SetGlobalVariableFromStack
13 AddStackToGlobalVariable
14 SubtractStackFromGlobalVariable
15 MultiplyGlobalVariableByStack
16 DivideGlobalVariableByStack
17 AndGlobalVariableWithStack
18 OrGlobalVariableWithStack

19..29 expression operators

2A..2C compare-top-and-branch operators
```

---

# 158. Compact SCX bridge reference

```text
39 StartScxScript
    generic
    fire-and-forget

3A StartScxScriptTracked
    generic
    state 4

3B StartCharacterScript
    explicit character
    fire-and-forget

3C StartCharacterScriptTracked
    explicit character
    state 4
```

This pair structure is authoritative for the currently analyzed Runtime build.

---

# 159. Boundary of current knowledge

The fundamental VM architecture is now strongly recovered.

We know:

```text
context size/layout
stack allocation/capacity
event queue storage/capacity
event 1/2/3 entrypoints
event 4 destruction
global registry size
central dispatch loop
static opcode table extent
handler-owned operand decoding
Scalar16 0x4000 remapping
shared global-variable backing
core stack/expression/global opcode families
generic/character SCX launch pairs
major native wait states
probe/dry-run execution mode
```

The largest remaining gaps are now:

```text
semantic names for many native opcodes
exact meaning of several context states/flags
zone event 1/2/3 gameplay labels
parameter-block producers/selector semantics
the opcode-table auxiliary dword
```

The key architectural takeaway is:

> The IAM scenario VM is a small but fully fledged event machine: fixed contexts
> execute one of three event entrypoints over shared AREA bytecode, use a
> sixteen-dword expression stack and shared scenario globals, and suspend by
> changing native context state while Runtime subsystems complete asynchronous
> work.
