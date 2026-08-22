# Runtime interface and I2D system

> **Status:** work-in-progress reverse-engineering documentation for OpenNomad  
> **Intended repository path:** `docs/reverse-engineering/interface.md`  
> **Last updated:** 2026-08-22
>
> This document describes the Windows retail Runtime's generic interface
> subsystem and the I2D presentation system used by menus and other
> full-screen interfaces.
>
> It covers:
>
> - the static interface descriptor table;
> - the fixed runtime interface-instance pool;
> - AREA opcode `0x46` and its wait/completion contract;
> - interface resource loading;
> - I2D state graphs and element data;
> - interface 29, the main/start menu;
> - the Runtime-authored quit flow;
> - the `gfxint.bmp` artwork and colour-keyed blit;
> - the `CLOUD.BMP` bump background;
> - and the distinction between recovered Runtime presentation and intentional
>   OpenNomad modernization.
>
> This document is authoritative for the **generic interface architecture**.
> The compact scenario VM that opens interfaces is documented in
> [`iam-scenario-vm.md`](iam-scenario-vm.md).

Related documentation:

- [`iam-scenario-vm.md`](iam-scenario-vm.md) — AREA opcode `0x46`, state 6,
  shared globals and event resumption;
- [`iam-area.md`](iam-area.md) — AREA 118 startup bytecode;
- [`startup-sequence.md`](startup-sequence.md) — path from executable startup
  to interface 29;
- [`runtime-globals.md`](runtime-globals.md) — interface-instance pool and
  ScenarioEngine globals;
- [`original-toolchain.md`](original-toolchain.md) — original I2D source paths
  and Win32/DirectDraw environment.

---

# 1. Evidence model

Sources are ranked:

1. **`Runtime.exe` behavior and static data**;
2. **retail IAM/I2D assets**;
3. **AREA 118 startup bytecode**;
4. **OpenNomad's current `Core/Interface` implementation**;
5. visual comparison against the retail game.

Confidence terms:

- **Confirmed — Runtime:** direct executable behavior/static-data evidence.
- **Confirmed — data:** direct retail asset evidence.
- **Corroborated:** independent Runtime/data evidence agrees.
- **Strongly reconstructed:** architecture is clear but original symbol name is
  unavailable.
- **Provisional:** plausible interpretation requiring more tracing.
- **OpenNomad-only:** deliberate modern behavior, not attributed to Runtime.

Reference executable:

```text
SHA-256:
55f7120bfea7891b048c64e3682f3259cdbf2719a43fa24e42254b753c95d2ef

image base:
0x00400000
```

---

# 2. High-level architecture

Runtime has a reusable interface manager rather than one hard-coded main-menu
loop.

Conceptually:

```text
AREA scenario VM
    |
    +-- opcode 0x46 OpenInterface
            |
            v
    static interface descriptor table
            |
            +-- interface ID
            +-- resource names
            +-- callbacks
            +-- flags/configuration
            |
            v
    Runtime interface instance
            |
            +-- loaded bitmap/string/font resources
            +-- interface-specific init
            +-- I2D state graph
            +-- current state
            |
            v
    user interaction
            |
            v
    interface completion result
            |
            v
    shared scenario/global variable
            |
            v
    AREA context resumes
```

The main menu is interface:

```text
29
```

inside this generic architecture.

---

# 3. Static interface descriptor table

Runtime keeps a static table beginning around:

```text
0x004CB640
```

Each descriptor is:

```text
0x5C bytes
```

The table contains metadata and callbacks for multiple interfaces.

Known neighboring interface names/IDs include:

```text
28  DIVERS
29  OMK START MENU
30  SAVE GAME
31  PAUSE GAME
35  OPTIONS
36  HIGH-SCORE
```

Not every descriptor has been semantically mapped.

---

# 4. Interface 29 descriptor address

Interface 29's descriptor begins at:

```text
0x004CC0AC
```

The next descriptor, interface 30, begins at:

```text
0x004CC108
```

Difference:

```text
0x5C
```

confirming the descriptor stride.

---

# 5. Interface 29 descriptor — recovered dwords

Current sparse map:

```text
+0x00  0x004CC734 -> "OMK START MENU"
+0x04  29
+0x08  0xFFFFFFFF
+0x0C  0x004CC744 -> "gfxint.bmp"
+0x10  0x004CC750 -> "Menu"

+0x14  0x00479D10
+0x18  0x0042A0F0
+0x1C  0x00479F30
+0x20  0x00475A50

+0x24  1
+0x28  2
+0x2C  0

+0x30..+0x50
       mostly 0xFFFFFFFF in this descriptor

+0x54  0x20000400
+0x58  0
```

Known callback roles:

```text
+0x14:
    interface-29 initializer
    0x00479D10

+0x1C:
    interface-29 teardown
    0x00479F30
```

The callbacks at `+0x18/+0x20` participate in generic interface behavior but
their exact original source-level roles are not yet named.

---

# 6. Documentation-safe descriptor type

Do not replace the unresolved dwords with invented semantics.

A safe partial representation is:

```c
struct RuntimeInterfaceDescriptor {
    const char *name;               // +0x00
    int32_t interfaceId;            // +0x04

    int32_t field08;                // +0x08

    const char *bitmapName;         // +0x0C
    const char *stringTableName;    // +0x10

    void (*init)(void *instance);   // +0x14
    void *callback18;               // +0x18
    void (*destroy)(void *instance);// +0x1C
    void *callback20;               // +0x20

    uint32_t field24;               // +0x24
    uint32_t field28;               // +0x28
    uint32_t field2C;               // +0x2C

    uint32_t unresolved30[9];       // +0x30..+0x50

    uint32_t flags;                 // +0x54
    uint32_t field58;               // +0x58
}; // 0x5C
```

Callback prototypes above are schematic.

Their exact calling conventions/parameters should be taken from each callsite.

---

# 7. Descriptor flags

Interface 29 stores:

```text
0x20000400
```

at descriptor:

```text
+0x54
```

The complete bit meanings are not recovered.

OpenNomad therefore preserves:

```text
runtime_flags = 0x20000400
```

without assigning speculative symbolic names.

This is the correct policy.

---

# 8. Runtime interface-instance pool

Runtime owns a fixed pool beginning at:

```text
0x004E9818
```

Geometry:

```text
stride = 0x7C
count  = 3
```

Thus Runtime supports:

```text
3 resident interface-instance records
```

in this fixed pool.

This is distinct from the much larger static descriptor table.

---

# 9. Descriptor versus instance

Keep these concepts separate:

```text
RuntimeInterfaceDescriptor
    static metadata
    process lifetime
    one per interface type

RuntimeInterfaceInstance
    mutable state
    one per active/resident open
    fixed pool of 3
```

OpenNomad similarly separates:

```text
InterfaceDescriptor
InterfaceInstance
```

but uses safe dynamic ownership rather than reproducing a raw 3-record global
array.

---

# 10. Multiple resident interfaces

Runtime architecture permits one interface to remain resident while another is
opened.

Interface 29 has a relationship with:

```text
interface 35 "OPTIONS"
```

Current OpenNomad models this as:

```text
companion_interface = 35
```

while keeping residency distinct from focus.

This is architecturally preferable to the old assumption:

```text
opening any interface destroys the previous one
```

---

# 11. AREA opcode `0x46`

Compact scenario opcode:

```text
0x46
```

is the generic interface-opening operation.

Runtime handler:

```text
0x00403860
```

It consumes three `Scalar16` operands.

High-level contract:

```text
OpenInterface(
    interfaceId,
    argument,
    resultGlobal)
```

The exact author-facing names of operands 1 and 2 remain partially unresolved.

---

# 12. Interface wait state

After opening the interface Runtime sets the compact scenario context to:

```text
state 6
```

The instruction pointer has already advanced past the `0x46` instruction.

Execution therefore becomes:

```text
AREA instruction
    |
    v
OpenInterface
    |
    +-- create/open interface instance
    +-- retain concrete interface context/handle
    |
    v
scenario state = 6
    |
    v
interpreter returns
```

The AREA event does not continue until the correct interface completes.

---

# 13. Completion identity matters

Runtime does not resume merely because:

```text
some interface finished
```

The wait is associated with the concrete opened interface/context.

A faithful modern implementation should therefore track:

```text
interface ID
+
instance generation/identity
```

rather than one global Boolean:

```text
interfaceFinished
```

OpenNomad's `InterfaceHandle` is a safe modern realization of this principle.

---

# 14. Completion result

On matching interface completion:

```text
result
    ->
shared START/scenario global variable
```

specified by the opening opcode.

The scenario context then resumes from the instruction after `0x46`.

This creates the general pattern:

```text
AREA asks UI question
UI returns integer result
AREA branches on result
```

---

# 15. Startup instruction for interface 29

AREA 118 contains:

```text
46 1D 00 FF FF 13 00
```

Decoded:

```text
interfaceId  = 29
argument     = -1
resultGlobal = 19
```

Thus the main menu is opened from ordinary AREA bytecode.

---

# 16. Special startup interface-context global

Runtime saves a scenario/interface-related context specially at:

```text
0x004E6C7C
```

for the interface-29 startup path.

This is documented in `runtime-globals.md` as:

```text
g_specialScenarioScriptContext
```

The behavior is confirmed.

Do not generalize it to:

```text
the current interface pointer
```

for every interface/mode without further evidence.

---

# 17. Interface 29 resources

The descriptor selects:

```text
bitmap:
    I2D/bitmaps/gfxint.bmp

string table:
    IAM/Menu
```

The initializer additionally uses:

```text
IMAGES/CLOUD.BMP
```

for the animated background.

Fonts include:

```text
'I' -> MENUINTR
'S' -> SNEAK.FNT
```

The exact resource registration/cache plumbing belongs to the wider I2D
subsystem.

---

# 18. `IAM/Menu`

`IAM/Menu` is a NUL-separated string table.

For the start-menu paths currently recovered:

```text
index 0  New Game
index 1  Load Game
index 4  Options
index 5  Quit
index 6  Yes
index 7  No
```

The string table supplies text only.

It does **not** encode the menu's rectangles/layout.

Those live in Runtime's static I2D structures.

---

# 19. Interface 29 initializer

Runtime:

```text
0x00479D10
```

Working name:

```text
StartMenu_Initialize
```

The function establishes interface-specific I2D state and resources after the
generic interface-opening machinery has created the instance.

Current evidence includes:

- root I2D state selection;
- bump-background initialization;
- menu text/button structures;
- bitmap/logo element;
- quit-state relationships;
- resource/font setup.

---

# 20. Interface 29 root state

Recovered static I2D root state:

```text
0x004CF218
```

The interface initializer stores this state in the live interface instance.

The exact instance field map is not yet complete enough to publish a full
`0x7C` structure.

Use:

```text
root/current I2D state pointer
```

rather than naming every nearby dword from one callsite.

---

# 21. Root menu text elements

Recovered static elements:

```text
0x004CE6F0 -> IAM/Menu[0]
0x004CE738 -> IAM/Menu[1]
0x004CE780 -> IAM/Menu[4]
0x004CE7C8 -> IAM/Menu[5]
```

Selection order:

```text
New Game
Load Game
Options
Quit
```

---

# 22. Runtime root-menu geometry

Recovered 640×480 logical-canvas rectangles:

```text
New Game:
    x=0 y=120 w=640 h=40

Load Game:
    x=0 y=200 w=640 h=40

Options:
    x=0 y=280 w=640 h=40

Quit:
    x=0 y=360 w=640 h=40
```

Font:

```text
'I'
```

for all four.

The 640-pixel-wide rectangles and observed presentation indicate centered
labels.

---

# 23. Root text-group flags

Recovered raw group flags:

```text
0x80000010
```

The complete symbolic bit meanings are not established.

OpenNomad should preserve the raw value for RE correlation while modeling the
observed behavior directly.

---

# 24. `gfxint.bmp` top artwork

Interface 29 uses:

```text
I2D/bitmaps/gfxint.bmp
```

Recovered element approximately:

```text
0x004CF1A8
```

Source rectangle:

```text
0, 0, 640, 150
```

Destination rectangle:

```text
0, 0, 640, 150
```

Raw element flags:

```text
0x40000100
```

Recovered blit mode:

```text
0x03
```

---

# 25. `gfxint.bmp` colour-key behavior

Blit-mode bits:

```text
bit 0 / 0x01:
    DDBLT_KEYSRC
    source colour key

bit 1 / 0x02:
    DDBLT_KEYDEST
    destination colour key
```

The source-key behavior explains why the retail menu logo does not show an
opaque black rectangular background.

The exact destination colour-key value/path remains unresolved.

---

# 26. Important logo-rendering lesson

Treating `gfxint.bmp` as an ordinary opaque RGBA image gives the wrong result.

The original element is a DirectDraw colour-keyed blit.

Modern rendering should reproduce the visible keyed result, even if its
internal implementation uses alpha instead of a DirectDraw destination
surface.

Do not bake the black background into the intended artwork.

---

# 27. Runtime versus modern logo layout

Recovered Runtime rectangle:

```text
0,0,640,150
```

Current OpenNomad intentionally applies presentation-only adjustments including:

```text
top-centre anchoring
small top margin
scale ≈ 0.84
viewport width clamping
```

Those are modernization choices.

The recovered Runtime rectangle must remain available/documented separately.

---

# 28. Runtime versus modern menu spacing

Runtime Y positions:

```text
120
200
280
360
```

Current OpenNomad modernized positions:

```text
168
218
268
318
```

and a smaller modern text height.

Again:

```text
Runtime geometry != OpenNomad presentation policy
```

Do not replace the recovered coordinates in RE structures with modern layout
values.

---

# 29. New Game result

Runtime New Game callback:

```text
0x0047A2B0
```

eventually causes interface completion result:

```text
3
```

For the startup invocation, that result is written to:

```text
global variable 19
```

because AREA 118's `0x46` specified global 19.

AREA 118 subsequently branches on this result and enters the Kay'l intro path.

---

# 30. OpenNomad New Game transition

Current OpenNomad adds a presentation-only:

```text
short commit hold
white fade
```

before delivering result 3.

This is explicitly:

```text
OpenNomad-only modernization
```

not Runtime evidence.

The important preserved semantic contract is:

```text
result 3 reaches AREA only after UI completion
```

---

# 31. Quit state

Runtime Quit-state initializer:

```text
0x0047BBB0
```

Recovered writes:

```text
WORD [0x004CEF12] = 1
WORD [0x004CF004] = 5
```

Interpretation:

```text
selected choice = 1
title string = IAM/Menu[5] = "Quit"
```

Thus the default/safe choice is:

```text
No
```

---

# 32. Quit confirmation geometry

Title:

```text
IAM/Menu[5] = "Quit"
font 'I'
x=0 y=40 w=640 h=40
```

Choices:

```text
IAM/Menu[6] = "Yes"
IAM/Menu[7] = "No"

font 'S'
x=0 y=330 w=640 h=40
```

Both choice elements occupy the same rectangle.

Runtime behaves as an exclusive selector rather than displaying two side-by-side
labels.

---

# 33. Quit Yes callback

Runtime:

```text
0x0047BC10
```

ultimately performs:

```c
PostQuitMessage(0);
```

This is the original quit behavior.

OpenNomad maps it to:

```text
SDL_EVENT_QUIT
```

and allows normal application teardown to happen through the event loop.

That is a correct modern platform mapping.

---

# 34. Quit No behavior

Selecting:

```text
No
```

returns to the root menu state.

The confirmation is therefore an I2D child state, not a native Win32 message
box.

---

# 35. I2D state graph

Interface 29 demonstrates the larger interface architecture:

```text
root state
    |
    +-- New Game state/action
    +-- Load Game state
    +-- Options state
    +-- Quit state
            |
            +-- Yes action
            +-- No -> parent/root
```

Runtime may encode some element callbacks directly where OpenNomad uses a tiny
synthetic action-state object.

That implementation difference should not be mistaken for a recovered Runtime
state node.

---

# 36. OpenNomad synthetic action state

Current OpenNomad represents Quit/Yes using:

```text
synthetic child state with on_enter callback
```

because its I2D abstraction routes actions through state transitions.

Runtime stores the Yes action directly on the corresponding text element/callback
path.

The synthetic state is therefore:

```text
safe modern representation
```

not serialized/recovered Runtime data.

---

# 37. Navigation state

I2D states contain a current selected element/index.

Known example:

```text
Quit confirmation selected index = 1
```

at state entry.

The generic input layer moves selection and activates the target/action of the
selected element.

Exact original keyboard/controller mapping belongs to input RE, not this
document.

---

# 38. Interface 29 background

The animated background source is:

```text
IMAGES/CLOUD.BMP
```

It is not an ordinary looping movie or a set of pre-rendered cloud frames.

Runtime executes a procedural I2D bump/warp effect.

---

# 39. Original source-file provenance

`Runtime.exe` preserves:

```text
C:\Omikron\Sources\omikron\I2D_Bump.c
```

This directly identifies the effect as original Omikron-specific I2D code.

Recovered function regions:

```text
~0x004B19C0  initialization
~0x004B1B00  per-tick/effect pass
~0x004B1F40  warp-table generation
~0x004B2220  CLOUD loading / colour-ramp setup
```

Addresses are working labels around the recovered functions.

---

# 40. `CLOUD.BMP` format/use

Validated source:

```text
256 × 256
8-bit indexed BMP
```

Runtime uses raw index values as a:

```text
height field
```

rather than treating the BMP palette as the final displayed cloud colours.

This is the foundation of the bump-lighting effect.

---

# 41. Bump effect pipeline

At each authentic logical effect tick Runtime approximately performs:

```text
256×256 height indices
    |
    v
signed 8-bit X/Y height gradients
    |
    +-- moving light position
    |
    v
0..63 intensity index
    |
    v
recovered 64-entry colour ramp
    |
    v
256×256 lit field
    |
    +-- 480-entry animated row warp
    +-- 640-entry animated column warp
    |
    v
640×480 final background
```

The warp tables are consumed in reverse order and use 8-bit wrapped source
coordinates.

---

# 42. Bump-light animation

The moving light follows a circular/periodic trajectory around approximately:

```text
(128,128)
```

while four phase accumulators animate the row/column cosine warps.

The exact scalar update formulas are implemented in OpenNomad's CPU reference
effect and tested against the recovered Runtime sequence.

---

# 43. Signed-gradient semantics

Runtime's neighboring height differences wrap as signed 8-bit values.

Conceptually:

```text
gradient =
    int8_t(uint8_t(heightDifference))
```

This wrap behavior matters.

Using an unrestricted integer difference gives different lighting at steep
height discontinuities.

---

# 44. Original logical refresh

The bump effect updates at:

```text
30 Hz
```

matching the game's logical tick cadence.

That does not require the modern display to present visible 30 Hz stepping.

---

# 45. OpenNomad GPU presentation

Current production OpenNomad:

- uploads `CLOUD.BMP` height data once;
- generates the static signed-gradient field on GPU;
- keeps two authentic 30 Hz endpoint states, tick N and N+1;
- generates tiny row/column warp endpoint textures;
- interpolates effect state at host/display refresh;
- renders one full-screen fragment-shader pass.

This is:

```text
modern GPU implementation of recovered Runtime math
```

rather than a claim that Runtime used shaders.

---

# 46. Stepped versus interpolated mode

OpenNomad can expose:

```text
stepped:
    authentic visible 30 Hz updates

interpolated:
    same 30 Hz endpoint timing
    presentation interpolated at display FPS
```

The interpolated mode intentionally changes only presentation smoothness.

The authored/effect clock remains Runtime-compatible.

---

# 47. CPU effect as RE oracle

`I2DBumpEffect` reproduces the original CPU algorithm closely and is kept for:

```text
tests
parity checks
debugging
reference frames
```

The production renderer does not need to execute the original per-pixel CPU
loop every tick.

This is a useful OpenNomad pattern:

> keep a small authoritative reference implementation while allowing a more
> efficient equivalent presentation implementation.

---

# 48. Unresolved bump scalar

Runtime also updates a second floating-point/double scalar which is:

```text
initially about 2.0
decremented by about 0.0815
```

in the same region.

Current tracing has not found a downstream reference affecting the displayed
background.

OpenNomad intentionally does not invent an effect for it.

---

# 49. Interface opening resource order

A faithful modern opener should conceptually follow:

```text
lookup descriptor
    |
    v
allocate/create runtime instance
    |
    v
load descriptor resources
    |
    +-- bitmap
    +-- IAM string table
    +-- other generic resources as recovered
    |
    v
call descriptor-specific initializer
    |
    v
establish root/current I2D state
```

Do not hide interface-specific state construction inside AREA opcode code.

---

# 50. Interface closing order

Likewise:

```text
interface-specific destroy callback
    |
    v
generic instance/resource release
```

OpenNomad uses RAII for the second stage.

Runtime uses explicit global/resource ownership.

Observable lifecycle should match while memory management remains modern.

---

# 51. Interface 29 teardown

Runtime callback:

```text
0x00479F30
```

is the start-menu destroy path.

Its detailed resource side effects are less important than preserving the
generic descriptor destroy contract.

OpenNomad currently has an explicit destroy callback even though RAII performs
most actual resource release.

---

# 52. Static versus mutable I2D data

Runtime contains large static I2D structures in the executable image:

```text
text elements
groups
state records
bitmap elements
callback pointers
default selected values
```

The live interface instance then points at and/or mutates relevant state.

OpenNomad converts this into owned runtime objects rather than mutating mapped
static executable data.

That is a deliberate architecture improvement.

---

# 53. 640×480 reference canvas

Interface 29's recovered geometry is authored around:

```text
640 × 480
```

This is the correct logical reference space for the original interface.

Modern high-resolution rendering should derive a presentation transform from
that canvas rather than rewriting every recovered coordinate.

---

# 54. Modern-resolution policy

Recommended layering:

```text
RecoveredI2DGeometry
    640×480 Runtime coordinates

I2DPresentationTransform
    viewport scale/aspect/anchor policy

GPU renderer
    physical pixels
```

This lets OpenNomad modernize spacing and high-resolution output while retaining
the original data for diagnostics and exact-reference mode.

---

# 55. Modernization must be explicit

Current intentional modernizations include:

```text
main-menu entry fade from black
New Game hold + white fade
compact root-menu vertical spacing
logo scale/top margin
host-refresh bump interpolation
high-resolution/widescreen presentation
```

None should be documented as Runtime behavior.

This separation is especially important because interface rendering is
subjective enough that presentation improvements can otherwise erase RE facts.

---

# 56. Current OpenNomad descriptor model

Current modern type:

```cpp
InterfaceDescriptor {
    id
    name

    bitmap_name
    string_table_name

    companion_interface

    init
    destroy

    runtime_flags

    presentation_hints // OpenNomad only
}
```

This intentionally does not pretend that all 0x5C Runtime descriptor fields
have been decoded.

That is the correct current design.

---

# 57. Current OpenNomad instance model

Current instance owns:

```text
descriptor pointer
generation-counted handle
original open request

IAM strings
optional bitmap
optional bump background

owned I2D states
root state
current state

OpenNomad-only presentation state
```

This cleanly separates:

```text
recovered interface state
```

from:

```text
modern transition overlays
```

---

# 58. Current implementation gap: descriptor coverage

Only interface 29 is currently substantially implemented.

Runtime has many more interface descriptors.

High-value future interfaces include:

```text
30 SAVE GAME
31 PAUSE GAME
35 OPTIONS
36 HIGH-SCORE
```

Implement these through the same generic descriptor/instance system rather than
adding new scene classes.

---

# 59. Current implementation gap: descriptor field map

Still unresolved:

```text
descriptor +0x08
callbacks +0x18/+0x20 exact roles
+0x24/+0x28/+0x2C
+0x30..+0x50
individual bits of +0x54 flags
+0x58
```

The best next step is cross-interface comparison plus callsite tracing.

---

# 60. Current implementation gap: full I2D element ABI

Interface 29 gives strong evidence for:

```text
text elements
bitmap elements
groups
states
selected member
callbacks
parent/child navigation
```

but the complete binary structures and flag definitions are not yet fully
documented.

A dedicated future `i2d.md` may eventually be useful once more interfaces are
traced.

For now this document describes only the fields/behavior needed by recovered
interfaces.

---

# 61. Recommended Ghidra labels

High-confidence working labels:

```text
0x00479D10  StartMenu_Initialize
0x00479F30  StartMenu_Destroy
0x0047A2B0  StartMenu_NewGame
0x0047BBB0  StartMenu_QuitStateInitialize
0x0047BC10  StartMenu_QuitYes

0x00403860  Scenario_OpenInterface

0x004B19C0  I2DBump_Initialize
0x004B1B00  I2DBump_Update
0x004B1F40  I2DBump_BuildWarpTables
```

Use the last three as reconstructed labels rather than original symbol claims.

---

# 62. Recommended regression tests

Interface architecture:

- [ ] descriptor 29 resolves to correct resources/callback metadata;
- [ ] opening creates a distinct instance identity;
- [ ] multiple resident interfaces do not alias instance identity;
- [ ] completion for a stale/wrong handle does not resume the waiter;
- [ ] state-6 completion resumes after opcode `0x46`;
- [ ] result writes to shared scenario globals.

Start menu:

- [ ] root string indices are `0,1,4,5`;
- [ ] recovered Runtime rectangles remain unchanged in reference data;
- [ ] New Game returns result 3;
- [ ] Quit defaults to No;
- [ ] Yes maps to application quit;
- [ ] No returns to root;
- [ ] keyed logo does not show black rectangle.

Bump effect:

- [ ] source is 256×256 indexed data;
- [ ] gradients use signed-byte wrap;
- [ ] palette has 64 recovered entries;
- [ ] row/column warp table sizes are 480/640;
- [ ] 30 Hz endpoint sequence matches CPU oracle;
- [ ] interpolated mode preserves endpoint timing.

---

# 63. Compact reference

```text
STATIC INTERFACE DESCRIPTORS
base ~004CB640
stride 0x5C

INTERFACE 29
descriptor 004CC0AC
name       "OMK START MENU"
id         29
bitmap     "gfxint.bmp"
strings    "Menu"
init       00479D10
destroy    00479F30
flags      20000400
```

Runtime instances:

```text
base   004E9818
stride 0x7C
count  3
```

AREA opening:

```text
opcode 46
handler 00403860
wait state 6
```

Startup:

```text
46 1D 00 FF FF 13 00

interface 29
argument -1
result global 19
```

Main-menu root:

```text
state 004CF218
strings 0,1,4,5
reference canvas 640×480
```

Background:

```text
IMAGES/CLOUD.BMP
256×256 indexed height map
procedural bump/warp
30 Hz logical effect
```

---

# 64. Boundary of current knowledge

Strongly recovered:

```text
generic descriptor-driven interface architecture
descriptor stride
fixed interface-instance pool
AREA state-6 open/wait/resume contract
interface-29 descriptor/resources
root menu layout
New Game result
Quit confirmation behavior
logo colour-key path
CLOUD procedural background
```

Still incomplete:

```text
full descriptor field meanings
full 0x7C interface-instance layout
all I2D structures/flag bits
interfaces other than 29
destination colour-key value
some generic callback semantics
```

The key architectural rule is:

> The main menu is not a bespoke scene. It is one instance of a generic
> descriptor-driven I2D interface system opened asynchronously by the IAM
> scenario VM.
