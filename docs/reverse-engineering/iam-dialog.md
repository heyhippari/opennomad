# IAM/DIALOG archive and dialog records

This page records the confirmed Windows retail `IAM/DIALOG` serialization used
by OpenNomad's Phase D3 parser and CPU dialog runtime. It describes authored
data, not inferred speech, facial-animation, or camera-transition behavior.

## Indexed archive lookup

Dialog IDs are unsigned 16-bit resource IDs. `IAM/DIALOG` uses the same paged
index as `IAM/AREA`: each `0x800`-byte page contains 256 little-endian
`{uint32 offset, uint32 size}` entries. For ID `id`, the entry is at:

```text
(id >> 8) * 0x800 + (id & 0xff) * 8
```

Offsets are absolute archive offsets. OpenNomad validates the entry, rejects a
zero-sized record, validates the entire record range, and copies the selected
record into immutable owning storage.

## Record header and fixed tables

```text
+0x00  int16 character_id
+0x02  int16 node_count
+0x04  int16 camera_count
+0x06  int16 camera_count_mirror
+0x08  DialogNode[node_count]       stride 0x40
        IamCameraRecord[camera_count] stride 0x2c
```

The two camera counts match across all 420 retail records and are validated as
a serialized duplicate. Counts and table arithmetic are checked before any
fixed record is read.

## Dialog node (`0x40` bytes)

```text
+0x00  uint32 condition_script_offset[4]
+0x10  uint32 action_script_offset[4]
+0x20  uint32 strings_offset
+0x24  int16  target_node_id[4]
+0x2c  int16  node_id
+0x2e  char   face_motion_base[10]
+0x38  int16  response_camera_a
+0x3a  int16  response_camera_b
+0x3c  int16  line_camera_a
+0x3e  int16  line_camera_b
```

The first nine dwords are record-relative offsets. Zero condition/action
offset means no program; `strings_offset` is required. Node IDs are exactly
`0..node_count-1`. Nonnegative targets and camera IDs must resolve inside the
same dialog. `-1` camera IDs mean no authored camera.

The face field is a fixed-width NUL-padded basename. Runtime requests the
corresponding `basename.3dm`; D3 exposes this request but does not fabricate
facial playback.

## Six consecutive strings

`strings_offset` points to exactly six checked NUL-terminated strings:

1. NPC/main line;
2. response slot 0;
3. response slot 1;
4. response slot 2;
5. response slot 3;
6. automatic player-side presentation line.

An empty response is not offered. Its condition and action are not evaluated
or executed. This matters for editor-remnant data attached to invisible slots.

## Conditions, actions, and progression

Nonzero condition/action offsets point to compact IAM/AREA scenario bytecode,
not to a second dialog language. D3 preserves program spans and delegates them
through narrow evaluator/executor callbacks. A missing callback produces an
explicit unsupported error. It does not expand or guess VM semantics.

A dialog enters node 0 and presents its main line. Acknowledgement presents
string 5 when nonempty, then exposes filtered choices. Selecting a choice runs
only that visible choice's action and follows its exact target; a negative
target completes. A node with no choices completes only after its line (and
automatic line, if present) has been acknowledged.

## Embedded cameras

The `0x2c` camera ABI is shared with IAM/AREA table 6: two raw signed position
vectors, camera ID/type, roll and horizontal-FOV Runtime units, two unresolved
signed fields, and four unresolved tail words. D3 preserves both camera IDs in
each node pair and resolves their raw records. It does not invent an
interpolation duration. Coordinate and angle conversions remain the existing
`RuntimeMath` presentation responsibility.

## Worked example: dialog 272

Dialog 272 (`Kay'l / Intro`) is archive range `[0x55800, 0x55eab)`. Its header
is character 310, three nodes, and five cameras. Visible graph:

```text
node 0: "I accept."        -> node 1
node 1: "OK. I understand." -> node 2
node 2: no visible response -> complete after acknowledgement
```

Face basenames are `125338`, `125339`, and `12533A`. Line camera pairs are
`2159 -> 2165`, `2165 -> 2160`, and `2167 -> 2166`; node 1's response pair is
`2160 -> 2167`. Node 2 slot 0 contains action bytes `8a 36 01 03`, but its
response text is empty, so normal terminal progression does not execute it.

## AREA opcode distinction and implementation status

AREA opcode `0x3D` consumes one 16-bit dialog ID, advances the AREA instruction
pointer, starts dialog mode, and explicitly yields the dispatcher. It is not a
typed `AreaWaitKind` wait. Phase D4 implements the mapping as:

```text
AREA 0x3D StartDialog
  -> ScenarioManager::start_dialog(dialog_id)
  -> session DialogRuntime becomes active
  -> AREA dispatcher yields at the already-advanced instruction pointer
  -> ScenarioStartupController gates later normal AREA ticks
  -> DialogRuntime::take_completion() removes the gate
  -> the same running AREA context resumes from that instruction pointer
```

Literal Scalar16 IDs are supported. Parameter-indirected operands carrying bit
`0x4000` fail explicitly until OpenNomad models the scenario parameter block;
they are never misinterpreted as literal dialog IDs.

This compact opcode is also unrelated to the 32-bit SCX `ScenarioControl`
command range `0x34..0x3f`; the two opcode namespaces must not be conflated.
Runtime's live numeric dialog-takeover state 3 is likewise separate from
OpenNomad's one-shot `ScenarioMode::k_teardown` startup operation. Starting a
dialog never invokes that teardown mode.
