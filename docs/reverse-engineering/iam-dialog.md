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
vectors, camera ID/type, roll and horizontal-FOV Runtime units, target/eye
attachment selectors, and four unresolved tail words.

The recovered node accessors are:

```text
0x00401150 -> node +0x38   response camera A
0x00401190 -> node +0x3a   response camera B
0x004010D0 -> node +0x3c   main-line camera A
0x00401110 -> node +0x3e   main-line camera B
```

High-level dialogue presentation at `0x0046A200` uses `+0x3c/+0x3e` for the
main/NPC line and `+0x38/+0x3a` for player-response presentation. These roles
must not be swapped.

Runtime applies a dialog camera pair as a progression, not as two alternatives:

```text
camera A == -1
    return; camera B is not independently applied

camera A present
    snap immediately

camera B present
    transition from A over 160 Runtime camera units
```

This is native pair routine `0x004013B0`. The complete camera record and the
same participant pair are submitted in both operations, so eye, target, roll
and horizontal FOV all participate. Camera A is not awaited before camera B is
scheduled. Under OpenNomad's recovered 30 Hz camera timing, the second leg
lasts approximately 5.333 seconds while being sampled smoothly at display
refresh rate.

Main/NPC presentation uses the node's `+0x3c/+0x3e` line-camera pair.
Response-side presentation uses `+0x38/+0x3a`.

### Dialogue camera participants and selectors

The pair routine receives stable authored character IDs:

```text
participant A = current controlled/possessed character
participant B = IAM/DIALOG record character_id (the interlocutor)
```

Its recovered selector-to-participant binding is the same for target and eye:

| Selector | Primary | Secondary |
| ---: | --- | --- |
| 0, 1 | participant A | none |
| 2, 3 | participant B | none |
| 4, 5 | none | none |
| 6 | participant A | participant B |
| greater than 6 in this submission path | none | none |

Participant binding does not imply recovered transform semantics. The native
selector dispatcher is `0x00415A10`, with distinct resolver functions for each
selector. OpenNomad implements only absolute selector `-1`, participant-A
selector `0`, and two-participant selector `6`. Selectors `1..5` and `7..9`
remain on the absolute fallback path.

Selector 6 resolves through `0x00415540`: it anchors at the participants'
midpoint, derives Runtime-native yaw from
`atan2(normalize(A-B).z, normalize(A-B).x) * 180/pi + 90`, and subtracts the
rotated authored endpoint vector. Target and eye application at `0x00415D10`
and `0x00415E60` use the same anchor-minus-relative convention. The detailed
formula is also recorded in [`iam-area.md`](iam-area.md).

### IMPASSE dialogue evidence correction

Retail DIALOG 392 (character 57, `Démon/Impasse`) has the main line
`I've been waiting for you...`, response pair `{4,-1}`, and main-line pair
`{-1,-1}`. Camera 4 uses selector 6 for both target and eye, but the ordinary
main-line path does not select it. It must not be forced onto that line or used
as a reason to swap the pair fields.

DIALOG 393 contains the same demon line but no cameras. DIALOG 90 contains the
Mechagarde cooperation line but also has no cameras. Consequently the observed
demon and Mechagarde editorial closeups have an external scenario, scene, or
script camera source rather than the ordinary IAM/DIALOG main-line pair. This
evidence does not identify which external authored command supplies either
shot, and OpenNomad does not fabricate one.

## Worked example: dialog 272

Dialog 272 (`Kay'l / Intro`) is archive range `[0x55800, 0x55eab)`. Its header
is character 310, three nodes, and five cameras. Visible graph:

```text
node 0: "I accept."        -> node 1
node 1: "OK. I understand." -> node 2
node 2: no visible response -> complete after acknowledgement
```

Face basenames are `125338`, `125339`, and `12533A`. Line camera pairs are `2159 -> 2165`, `2165 -> 2160`, and `2167 -> 2166`; node 1's response pair is `2160 -> 2167`. Node 2 slot 0 contains action bytes `8a 36 01 03`, but its response text is empty, so normal terminal progression does not execute it.

The face basename selects a synchronized `MORPH/<basename>.3dm` package, not an external voice ADP. Its object, facial, and embedded-speech streams share one 30 Hz timeline; the format and binding rules are documented in
[`3dm.md`](3dm.md). The media layer observes dialog generations but never owns or auto-advances graph progression.

The opening node's first camera, 2159 (`cam DIA INTRO trav1`), is the
post-whiteout tilted close shot:

    eye       (-3206,-345,-1006)
    target    (-3165,-327,-239)
    roll      3928 units = 345.234375° = -14.765625° modulo 360
    HFOV      853 units  = 74.970703°
    type      12

Runtime snaps to 2159 and then travels to camera 2165 over 160 units.

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

## Retail text presentation

The gameplay dialog presenter uses the recovered 640x480 logical coordinate
space and the retail bitmap-font rules documented in [`fnt.md`](fnt.md). It
does not add a dimming overlay or text panels.

Main and automatic lines use key `D` (`DIALOGUE.FNT`) in a fixed viewport at
`x=32`, width `576`, effective top `412`, and effective bottom `476`. This is a
64-unit-high viewport with a four-unit presentation margin below it. Text is
left aligned and wraps at spaces or explicit newlines using exact FNT
advances. Content taller than the viewport scrolls vertically; dialog
confirmation remains progression input, independent of scroll position.
Ordinary main text is white. The automatic player-side line uses encoded tint
`#8080C0` and the same geometry.

Up to four visible responses are formatted independently at `x=32`, width
`576`, and at most 96 units high. Their actual formatted heights are stacked
in authored order and bottom-aligned to `y=448`, leaving a 32-unit bottom
margin. The selected response is white and the others are `#808080`. Selection
is represented only by colour: no marker or prefix is inserted, so authored
text and measurements remain unchanged.

All three text roles in this specific presenter—main, automatic, and
responses—use `DIALOGUE.FNT`. Registry key `R` still resolves to
`DIALSELE.FNT` for Runtime consumers that explicitly request it; its filename
does not make it the intro response font.
