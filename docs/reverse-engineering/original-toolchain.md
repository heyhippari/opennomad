# Original Omikron PC development and build toolchain

> **Status:** work-in-progress reverse-engineering documentation for OpenNomad  
> **Last updated:** 2026-08-22
>
> This document reconstructs the development environment and content-production
> pipeline used for the Windows retail build of *Omikron: The Nomad Soul*.
>
> “Toolchain” is used here in two distinct senses:
>
> 1. the **native Windows executable build toolchain** — compiler, linker, CRT,
>    Win32, DirectX, resource compiler, and binary layout; and
> 2. Quantic Dream's **proprietary content-authoring pipeline** — internal
>    editors such as IAM, GEM, GALE and RAM-related tooling, plus the export
>    formats eventually consumed by `Runtime.exe`.
>
> These must not be conflated. A retail `.3DO`, `.3DA`, `.SCX`, or IAM archive
> is evidence of the **runtime asset pipeline**, but is not automatically the
> native project format of an original editor.
>
> This document deliberately distinguishes:
>
> - facts encoded directly in `Runtime.exe`;
> - facts encoded in retail assets;
> - surviving source-path/diagnostic strings;
> - period credits and historical material;
> - and best-fit reconstruction where the exact original build software has
>   not survived.

Related documentation:

- [`runtime-globals.md`](runtime-globals.md) — process-global Runtime state;
- [`startup-sequence.md`](startup-sequence.md) — Win32/CRT/engine startup;
- [`runtime-coordinate-math.md`](runtime-coordinate-math.md) — old x87/runtime
  math behavior relevant to compiler fidelity;
- [`3do.md`](3do.md), [`3dt.md`](3dt.md), [`3da.md`](3da.md),
  [`3dp.md`](3dp.md) — recovered runtime asset formats;
- [`scx.md`](scx.md) — SCX v5 runtime container;
- [`iam-area.md`](iam-area.md) — IAM AREA archive/record format;
- [`iam-scenario-vm.md`](iam-scenario-vm.md) — compact IAM scenario VM;
- [`script-opcodes.md`](script-opcodes.md) and
  [`iam-script-functions.md`](iam-script-functions.md) — structured scripting.

---

# 1. Evidence hierarchy

For native build-toolchain questions, sources are ranked:

1. **PE/COFF fields in the supplied retail `Runtime.exe`**;
2. **machine code and static imports**;
3. **embedded source paths, diagnostics and resource strings**;
4. **known Microsoft tool-version history**;
5. **historical/period material**;
6. **best-fit reconstruction**.

For content-tool questions:

1. **retail binary/data behavior**;
2. **explicit editor-versus-game strings embedded in Runtime**;
3. **period game credits/manual**;
4. **archival screenshots of Quantic Dream tools**;
5. **contemporary development diaries / interviews / retrospective material**;
6. **inference from runtime formats**.

Confidence labels used throughout:

- **Confirmed — Runtime:** directly demonstrated by `Runtime.exe`.
- **Confirmed — PE:** directly encoded in PE/COFF metadata.
- **Confirmed — data:** directly demonstrated by retail data.
- **Confirmed — period credit:** stated in original retail credits/manual.
- **Corroborated:** independent evidence agrees.
- **Strongly reconstructed:** evidence strongly favors the interpretation, but
  the exact source artifact/tool version is unavailable.
- **Provisional:** plausible and useful, but should not become an asserted fact.
- **Unknown:** the relevant artifact/detail has not been recovered.

---

# 2. Reference executable

All binary-toolchain statements refer to:

```text
File:
    Runtime.exe

Supplied analysis file:
    Runtime(1).exe

Architecture:
    PE32 / Intel i386

Image base:
    0x00400000

Entry point:
    0x00411640

PE linker timestamp:
    1999-10-04 20:31:50

SHA-256:
    55f7120bfea7891b048c64e3682f3259cdbf2719a43fa24e42254b753c95d2ef
```

This is one Windows retail executable.

Do not assume that:

- another localization;
- a demo build;
- a patched executable;
- the Dreamcast version;
- the abandoned PlayStation build;
- or a later digital re-release

has identical toolchain fingerprints or code addresses.

---

# 3. Executive reconstruction

The strongest current build-environment model is:

```text
Quantic Dream C/C++ source tree
C:\Omikron\Sources\...
        |
        v
Microsoft Visual C/C++-era compiler
exact CL.EXE version unresolved
        |
        +-- substantial C source
        +-- some C++ classes/runtime support
        +-- C++ exceptions / SEH translation
        |
        v
Microsoft LINK.EXE 5.x
PE header reports 5.0
        |
        +-- Win32 GUI subsystem
        +-- dynamic MSVCRT.dll
        +-- raw Win32 USER/GDI APIs
        +-- DirectX 6.1-era SDK/API surface
        +-- Quantic/PATCH DirectDraw shim
        |
        v
Runtime.exe
fixed-base 32-bit x86 retail executable
```

The strongest current content-production model is:

```text
artists / animators / game builders / designers
        |
        +-- GEM
        +-- GALE
        +-- IAM
        +-- RAM-related tooling/workflow
        +-- DCC / motion-capture processing
        |
        v
internal authoring/project data
formats not fully recovered
        |
        v
conversion / export / platform processing
        |
        +-- 3DO / 3DT
        +-- 3DA / 3DP
        +-- ANI / CTL / OPT / APT / ...
        +-- SCX
        +-- IAM archives
        +-- WAV -> runtime audio path including ADP
        |
        v
retail data tree
        |
        v
Runtime.exe
```

Question marks remain on many exact editor-to-file arrows.

---

# 4. PE/COFF fingerprint

The supplied executable reports:

```text
PE magic:
    0x10B
    PE32

Machine:
    i386

MajorLinkerVersion:
    5

MinorLinkerVersion:
    0

OperatingSystemVersion:
    4.0

SubsystemVersion:
    4.0

Subsystem:
    Windows GUI

ImageBase:
    0x00400000

SectionAlignment:
    0x1000

FileAlignment:
    0x0200
```

Additional characteristics:

```text
relocations stripped
line numbers stripped
COFF symbols stripped
32-bit executable
```

There is no PE base-relocation directory.

This is a classic fixed-base late-1990s Win32 executable rather than a modern
ASLR-capable image.

---

# 5. Linker version: what is actually proven

The PE Optional Header contains:

```text
MajorLinkerVersion = 5
MinorLinkerVersion = 0
```

This is the version stamp written by the linker.

It is strong evidence for a:

```text
Microsoft LINK 5.x-era output
```

and is highly consistent with the Visual C++ 5.0 / Visual Studio 97 generation.

It does **not**, by itself, prove:

```text
CL.EXE was exactly Visual C++ 5.0
```

because a project can in principle combine object files or frontend/compiler
versions with a different linker/import-library set.

Recommended wording:

> `Runtime.exe` was linked by a Microsoft LINK 5.0-era linker. Visual C++ 5.0
> is the best-fit baseline for a historical reconstruction, while the exact
> compiler frontend remains unresolved.

---

# 6. Do not infer `_MSC_VER` from the PE linker stamp

Microsoft compiler-version macros distinguish:

```text
Visual C++ 5.0:
    _MSC_VER = 1100

Visual C++ 6.0:
    _MSC_VER = 1200
```

No surviving `_MSC_VER` value has been recovered from this executable.

Therefore the older OpenNomad/Ghidra convenience definition:

```c
#define _MSC_VER 1200
```

must not be cited as evidence that Quantic Dream compiled this Runtime with
Visual C++ 6.0.

It is a header-compatibility choice unless separate binary evidence proves it.

---

# 7. VC5 versus VC6: current conclusion

Evidence favoring the VC5 generation:

```text
PE linker major/minor:
    5.0

Win32 4.0 subsystem target

late-1990s CRT/startup conventions

October 1999 binary timestamp
```

Evidence that would be needed to prove the exact compiler frontend:

```text
Rich header product/build IDs
PDB signature/path
compiler-generated version string
surviving .obj/.lib files
original .dsp/.dsw workspace
original build logs
exact compiler binary
```

None of those has been recovered from the supplied retail Runtime.

Current confidence:

```text
Microsoft compiler family:
    confirmed

LINK 5.0:
    confirmed PE

Visual C++ 5.0-era linker/tool environment:
    strong

CL.EXE exactly VC5:
    unresolved

CL.EXE VC6:
    possible but not evidenced

_MSC_VER = 1200:
    not established
```

---

# 8. No Rich header

The executable contains no:

```text
"Rich"
```

signature in the DOS-stub/header area or elsewhere as a valid Rich structure.

That removes one of the most useful modern ways to recover:

- compiler product IDs;
- linker build IDs;
- object counts by tool;
- resource-compiler provenance.

Absence can result from:

- linker/tool generation;
- stripping;
- post-processing;
- or a build pipeline that never emitted the marker.

Do not manufacture Rich-header metadata for this binary.

---

# 9. No PE Debug Directory

PE Data Directory entry 6 is:

```text
RVA  = 0
size = 0
```

No embedded CodeView marker was found:

```text
RSDS
NB10
```

and no `.pdb` pathname survives.

Therefore:

```text
exact PDB name:
    unknown

PDB GUID/signature:
    unavailable

compiler build from CodeView:
    unavailable
```

The shipped executable was stripped of ordinary debugger metadata.

---

# 10. Symbol and relocation stripping

PE Characteristics include:

```text
IMAGE_FILE_RELOCS_STRIPPED
IMAGE_FILE_LINE_NUMS_STRIPPED
IMAGE_FILE_LOCAL_SYMS_STRIPPED
```

Consequences:

- no COFF line-number mapping;
- no COFF symbol table;
- no relocation table for easy absolute-reference analysis;
- fixed preferred base expected.

This is consistent with a production/release executable.

It is also why embedded diagnostics and source-path strings are unusually
valuable reverse-engineering evidence.

---

# 11. Section layout

The executable has five sections:

| Section | RVA | Virtual size | Raw size | File offset |
|---|---:|---:|---:|---:|
| `.text` | `0x001000` | `0xBAFA2` | `0xBB000` | `0x000400` |
| `.rdata` | `0x0BC000` | `0x3708` | `0x3800` | `0xBB400` |
| `.data` | `0x0C0000` | `0x470838` | `0x26200` | `0xBEC00` |
| `.idata` | `0x531000` | `0x14DE` | `0x1600` | `0xE4E00` |
| `.rsrc` | `0x533000` | `0xA168` | `0xA200` | `0xE6400` |

The enormous difference between:

```text
.data raw size:
    0x26200

.data virtual size:
    0x470838
```

shows that a very large amount of zero-initialized/BSS-style global storage is
merged into `.data`.

This matches the Runtime's extensive fixed global pools.

---

# 12. Why `.data` matters for decompilation

Old Microsoft toolchains often leave an executable whose global architecture is
visible as one enormous flat address region.

Omikron exemplifies this strongly:

```text
0x004Cxxxx
    initialized/static data

0x004Exxxx
    small engine-global structures

0x005xxxxx
0x006xxxxx
0x008xxxxx
0x009xxxxx
    large BSS/global arrays/pools
```

Do not infer that all nearby absolute addresses represent fields of one source
object.

Many are independent translation-unit globals laid out by the linker.

See `runtime-globals.md`.

---

# 13. No modern PE hardening flags

`DllCharacteristics` is:

```text
0
```

There is no modern:

- ASLR flag;
- NX compatibility flag;
- CFG;
- high-entropy VA;
- dynamic-base metadata.

This is normal for the era.

A modern OpenNomad binary should of course use modern platform hardening; this
field is historical provenance, not a reimplementation target.

---

# 14. Runtime target: 32-bit Win32

The ABI assumptions visible throughout Runtime are:

```text
pointer size:
    4 bytes

long/int:
    32-bit Win32 conventions

WORD/short:
    16-bit

COM interface pointer:
    one 32-bit pointer

cdecl/stdcall/WinAPI calling conventions:
    32-bit x86

x87 floating point:
    extensively used
```

Serialized/runtime structures frequently contain pointer-sized 4-byte slots.

When translating those structures to modern 64-bit C++:

> Never replace a serialized/runtime `u32` slot with host `void*` merely because
> Runtime later used the field as a pointer.

---

# 15. CRT linkage

`Runtime.exe` dynamically imports:

```text
MSVCRT.dll
```

with:

```text
62 imported symbols
```

This is not a statically linked CRT executable.

Strong project-setting reconstruction:

```text
release dynamic Microsoft CRT
approximately the historical /MD family
```

The exact project switch and CRT library revision cannot be proven from the PE
alone.

---

# 16. CRT startup fingerprints

Imported startup/runtime functions include:

```text
__set_app_type
__p__fmode
__p__commode
_adjust_fdiv
__setusermatherr
_initterm
__getmainargs
_acmdln
_XcptFilter
_except_handler3
_controlfp
_ftol
```

These are characteristic of the old Microsoft Win32 CRT startup and numeric
runtime.

They are useful when separating compiler-generated startup code from Omikron
game code around the PE entrypoint.

---

# 17. C++ runtime support is present

Although many recovered source files are `.c`, the executable also imports
Microsoft C++ runtime features:

```text
operator new
operator delete
_purecall
__CxxFrameHandler
_set_se_translator
```

Therefore:

```text
"Omikron PC was pure C"
```

would be incorrect.

The better description is:

> The engine is heavily C-style and several graphics libraries demonstrably
> came from `.c` translation units, while the complete executable also uses C++
> object/runtime and exception facilities.

---

# 18. C++ exception / SEH integration

Imports include:

```text
__CxxFrameHandler
_set_se_translator
_except_handler3
_XcptFilter
```

This proves that Microsoft C++ exception machinery and Win32 structured
exception handling are present somewhere in the linked image.

It does not prove one global compiler switch for every translation unit.

In particular, do not conclude:

```text
every source file used /GX
```

merely because C++ exception support appears in the final executable.

---

# 19. RTTI evidence

The import table does not show the familiar dynamic-RTTI helpers such as:

```text
__RTDynamicCast
__RTtypeid
```

No useful RTTI name corpus has been identified in the current pass.

Safe conclusion:

```text
no material Runtime RTTI use has yet been recovered
```

Unsafe conclusion:

```text
the project definitely compiled with /GR-
```

The latter requires stronger evidence.

---

# 20. Release optimization evidence

Large functions such as the WinMain-equivalent use optimized x86 code patterns:

```text
ESP-relative locals
large direct stack allocation
callee-saved register use
no conventional EBP frame chain
```

This strongly indicates:

```text
optimized release code
frame-pointer omission in at least many functions
```

Do not over-specify:

```text
/O2
/Ox
/Oy
/Gy
```

unless a precise code-generation study distinguishes them.

The useful Ghidra implication is simply:

> Do not expect a reliable EBP frame chain in Runtime code.

---

# 21. x87 floating-point behavior

Runtime uses x87 floating point extensively.

Important consequences already observed in format/coordinate work include:

```text
_ftol-based float-to-integer conversion
x87 intermediate precision
old Microsoft CRT math helpers
row-vector matrix code
```

A byte-identical or numerically exact historical rebuild would need the exact
compiler/x87 control behavior, not merely mathematically equivalent modern C++.

OpenNomad normally wants **behavioral fidelity**, not old-codegen identity.

---

# 22. Static import overview

Imported DLL/function counts:

| DLL | Imported functions |
|---|---:|
| `KERNEL32.dll` | 43 |
| `USER32.dll` | 54 |
| `GDI32.dll` | 17 |
| `MSVCRT.dll` | 62 |
| `ole32.dll` | 6 |
| `PATCH.dll` | 2 |
| `DSOUND.dll` | 2 |
| `WINMM.dll` | 21 |
| `DINPUT.dll` | 1 |

This is a very direct snapshot of the PC platform layer.

---

# 23. Raw Win32 application framework

The executable directly imports extensive:

```text
USER32
GDI32
KERNEL32
```

functionality.

Examples:

```text
RegisterClassA
CreateWindowExA
DefWindowProcA
PeekMessageA
DispatchMessageA
DialogBoxParamA
CreateDialogParamA
MessageBoxA
FindWindowA
LoadIconA
LoadCursorA

CreateDIBSection
CreateCompatibleDC
StretchBlt
TextOutA
```

There is no MFC DLL import.

The original PC shell is therefore best described as:

```text
custom/raw Win32 application code
```

rather than an MFC application.

---

# 24. ANSI Win32 API

The import table overwhelmingly uses explicit ANSI functions:

```text
CreateWindowExA
GetMessageA
DispatchMessageA
MessageBoxA
GetPrivateProfileStringA
LoadLibraryA
```

This is important for:

- old file/path handling;
- localization assumptions;
- fixed-size `char` strings in runtime structures;
- Ghidra function signatures.

Do not retrofit `wchar_t` into original Runtime structure declarations unless a
specific path uses Unicode conversion.

---

# 25. Limited Unicode bridge

`KERNEL32` imports:

```text
MultiByteToWideChar
```

and OLE/COM facilities are present.

This means Unicode conversion occurs in some subsystem, but the game-wide
native string model remains overwhelmingly narrow/ANSI.

---

# 26. COM/OLE use

Static OLE imports include:

```text
CoInitialize
CoUninitialize
CoCreateInstance
CoFreeUnusedLibraries
CoTaskMemAlloc
CoTaskMemFree
```

COM is also central to old DirectX interfaces.

A string for:

```text
OleAut32.dll
```

exists even though it is not a static import, suggesting at least one
dynamically loaded OLE Automation path.

The exact owning feature remains separate RE work.

---

# 27. DirectX minimum version

`Runtime.exe` contains the literal diagnostic:

```text
DirectX error : This game needs DirectX 6.1 or higher.
```

Thus:

```text
DirectX 6.1 minimum:
    confirmed — Runtime
```

Microsoft publicly shipped the DirectX 6.1 SDK on:

```text
1999-02-03
```

which is chronologically compatible with this October 1999 build.

---

# 28. DirectX subsystem split

Static imports show:

```text
DirectDraw:
    through PATCH.dll

DirectSound:
    DSOUND.dll

DirectInput:
    DINPUT.dll

multimedia/timers/audio helpers:
    WINMM.dll

Direct3D:
    COM/vtable interface calls reached through DirectDraw/Direct3D objects
```

This is a DirectX 6.1-era application architecture, not a modern D3D9/11/12
link model.

---

# 29. `PATCH.dll` is architecturally important

Unlike a conventional game that statically imports:

```text
DDRAW.dll!DirectDrawCreate
DDRAW.dll!DirectDrawEnumerateA
```

this executable imports those two names from:

```text
PATCH.dll
```

Specifically:

```text
DirectDrawEnumerateA
DirectDrawCreate
```

This means PATCH.dll sits on the game's DirectDraw creation boundary.

Do not ignore it when reconstructing the original rendering initialization.

---

# 30. What PATCH.dll may represent

Possible roles include:

- compatibility wrapper;
- vendor/platform shim;
- Eidos/Quantic interception layer;
- patched DirectDraw loader;
- CD/distribution-specific compatibility component.

Without the exact matching DLL's source or full behavioral trace, the semantic
reason remains unresolved.

What is proven is narrower:

> The analyzed Runtime is linked against `PATCH.dll` for DirectDraw creation
> and enumeration.

---

# 31. DirectSound

Imports:

```text
DirectSoundCreate
DirectSoundEnumerateA
```

confirm old DirectSound use.

Runtime contains corresponding diagnostics and audio initialization paths.

This coexists with:

```text
WINMM
```

which supplies lower-level multimedia functions and timers.

---

# 32. DirectInput

The executable imports:

```text
DirectInputCreateA
```

from:

```text
DINPUT.dll
```

and contains DirectInput HRESULT/error strings such as:

```text
DIERR_BETADIRECTINPUTVERSION
DIERR_OLDDIRECTINPUTVERSION
```

This is direct evidence for old DirectInput SDK declarations being compiled
into the PC codebase.

---

# 33. WINMM use

Imports include families for:

```text
timeGetTime
timeBeginPeriod
timeEndPeriod
timeSetEvent
timeKillEvent

mmio*
mixer*
mci*
```

This fits a late-1990s multimedia game that combines:

- high-resolution-ish multimedia timing;
- sound/audio helpers;
- movie/media playback support;
- Windows mixer integration.

The exact ownership of each call must be traced per subsystem.

---

# 34. Direct3D: use vtable evidence, not modern import expectations

Old Direct3D interfaces are COM objects.

Therefore the absence of:

```text
D3D9.dll
```

or a simple `Direct3DCreate*` import is not surprising.

Recovered Runtime state includes a Direct3D-device-like COM pointer around:

```text
0x0080B068
```

whose vtable calls match legacy render-state/device behavior.

Exact interface-generation naming should be proven from vtable/IID/layout
evidence before permanently choosing:

```text
IDirect3DDevice
IDirect3DDevice2
IDirect3DDevice3
```

in reverse-engineering headers.

---

# 35. DirectX header policy for Ghidra

Recommended practice:

1. establish which historical COM interface revision Runtime actually uses;
2. recreate only the required DirectX structures/vtables in a Ghidra-safe C
   header;
3. preserve 32-bit pointer sizes and old struct packing;
4. avoid importing a modern Windows SDK header wholesale;
5. annotate uncertain interface revision explicitly.

A modern SDK can contain renamed/extended structures whose layout differs from
the 1999 declaration expected by Runtime.

---

# 36. Runtime diagnostics preserve SDK symbol names

The executable contains many symbolic DirectDraw/Direct3D errors:

```text
DDERR_...
D3DERR_...
DIERR_...
```

These were almost certainly compiled from custom error-decoding code using old
DirectX symbolic constants.

This is useful for matching:

- HRESULT values;
- header-era enum names;
- device setup behavior.

It does not prove the exact installed SDK build number beyond the explicit
DirectX 6.1 requirement.

---

# 37. Windows target level

PE fields report:

```text
MajorOperatingSystemVersion = 4
MinorOperatingSystemVersion = 0

MajorSubsystemVersion = 4
MinorSubsystemVersion = 0
```

This is a classic Win32 4.0-era target.

For historical rebuild experimentation, a Windows NT 4/Windows 9x-compatible
environment is therefore a much closer baseline than a modern Windows SDK
configuration.

OpenNomad itself should not inherit those OS restrictions.

---

# 38. Resource compiler evidence

The executable has a normal:

```text
.rsrc
```

section with dialogs/bitmaps/icons/string resources.

The resource directory uses French/France language metadata in the supplied
build:

```text
LANGID 0x040C
```

This is consistent with Quantic Dream's French development environment.

The exact `RC.EXE` version and original `.rc` file have not been recovered.

---

# 39. No byte-identical toolchain claim

At present we cannot produce a defensible statement such as:

```text
Runtime.exe was built with:
Visual C++ 5.0 SP3
CL.EXE build X
LINK.EXE build Y
DirectX SDK 6.1 build Z
these exact command-line switches
```

The available evidence is not that specific.

This document should remain explicit about that boundary.

---

# 40. Embedded original source root

Despite stripped symbols, the executable preserves absolute source paths.

The common root is:

```text
C:\Omikron\Sources\
```

with some strings using lowercase:

```text
C:\Omikron\sources\
```

The Windows filesystem's case-insensitive behavior made those variants
equivalent in the original environment.

This is direct evidence for the original development tree.

---

# 41. Surviving source paths

Unique source/header paths recovered from Runtime include:

```text
C:\Omikron\Sources\libpoly2d\..\libdirect3d\include\acc3d.h

C:\Omikron\Sources\libdirect3d\bw.c
C:\Omikron\Sources\libdirect3d\include\acc3D.h
C:\Omikron\Sources\libdirect3d\InitCartes.c

C:\Omikron\sources\libdirect3d\acc3d.c
C:\Omikron\sources\libdirect3d\include\acc3d.h
C:\Omikron\sources\libdirect3d\acc3Dprivate.h

C:\Omikron\Sources\libscreen\libscreen.c

C:\Omikron\Sources\LIBI2D\libi2dpc.c

C:\Omikron\sources\3de\..\libdirect3d\include\acc3d.h

C:\Omikron\Sources\libpoly2d\gereaff.c

C:\Omikron\Sources\omikron\I2D_Bump.c
```

There is also:

```text
C:\OMIKRON\DATA\BUG.TXT
```

in a runtime/debug reporting path.

---

# 42. Reconstructed source modules

Those strings directly establish source-tree components:

```text
libdirect3d
libpoly2d
libscreen
LIBI2D
3de
omikron
```

These are not names invented during OpenNomad refactoring.

They are original source-directory/library boundaries.

This is useful when naming Ghidra functions:

```text
Acc3D_...
Poly2D_...
Screen_...
I2D_...
```

where behavior and diagnostic xrefs support the association.

---

# 43. Graphics code was substantially C

The surviving paths are:

```text
*.c
*.h
```

not merely C++ `.cpp` files.

Examples:

```text
bw.c
InitCartes.c
acc3d.c
libscreen.c
libi2dpc.c
gereaff.c
I2D_Bump.c
```

Thus substantial graphics/rendering/platform code was genuinely C source.

This is stronger evidence than decompiler style alone.

---

# 44. Naming/casing clues

Original path strings mix:

```text
Sources
sources

acc3D.h
acc3d.h
acc3Dprivate.h

LIBI2D
libdirect3d
```

Do not normalize these variations and then infer separate projects from casing.

On the original Windows development filesystem, the spellings can refer to the
same path.

---

# 45. `libdirect3d`

This original source component contains at least:

```text
bw.c
InitCartes.c
acc3d.c
include\acc3d.h
acc3Dprivate.h
```

The name predates our reverse-engineering terminology and is direct evidence
that Quantic Dream isolated accelerated/Direct3D renderer functionality behind
an internal library/module.

---

# 46. `InitCartes.c`

French:

```text
cartes
```

in this context naturally refers to graphics/video cards.

Runtime strings around this code include hardware/device setup diagnostics.

This file is therefore a high-confidence owner for part of:

```text
video-card enumeration/configuration
DirectX graphics initialization
```

A Ghidra source-module namespace such as:

```text
libdirect3d::InitCartes
```

is justified when xrefs agree.

---

# 47. `acc3d.c`

The `acc3d` naming strongly aligns with:

```text
accelerated 3D
```

and appears in the Direct3D portion of the executable.

It should be distinguished from:

```text
software renderer
generic 3DE/world representation
```

unless individual functions prove overlap.

The exact original expansion of “acc3d” is not present as a string.

---

# 48. `bw.c`

`bw.c` is repeatedly cited by diagnostics/asserts from the Direct3D library.

Do not expand `bw` without evidence.

A short source filename is insufficient to assign a semantic owner beyond
`libdirect3d`.

---

# 49. `libscreen`

Original path:

```text
C:\Omikron\Sources\libscreen\libscreen.c
```

appears repeatedly around screen/DirectDraw error reporting.

This strongly identifies a dedicated screen/display abstraction layer.

Likely responsibilities visible in Runtime include:

```text
display mode
primary/back surfaces
cooperative level
surface creation
text/debug surface helpers
```

Exact function boundaries should continue to be established from xrefs.

---

# 50. `libpoly2d`

Original path:

```text
C:\Omikron\Sources\libpoly2d\gereaff.c
```

and cross-include path:

```text
libpoly2d\..\libdirect3d\include\acc3d.h
```

show that the 2D polygon/display layer directly included accelerated-3D
interfaces.

This is useful context for the menu/interface rendering code, which mixes 2D
presentation with shared graphics backends.

---

# 51. `LIBI2D`

Original PC-specific file:

```text
C:\Omikron\Sources\LIBI2D\libi2dpc.c
```

indicates an internal I2D library with a PC-specific implementation layer.

The main-menu/UI reverse engineering has already exposed extensive I2D
terminology.

This path confirms I2D is original Quantic Dream terminology rather than an
OpenNomad invention.

---

# 52. `omikron\I2D_Bump.c`

Original path:

```text
C:\Omikron\Sources\omikron\I2D_Bump.c
```

directly identifies the specialized I2D bump/background effect code as
Omikron-specific source.

This is particularly relevant to the reconstructed main-menu bump/cloud
background.

It also demonstrates that some I2D features lived outside the reusable LIBI2D
module.

---

# 53. `3de`

The path:

```text
C:\Omikron\sources\3de\..\libdirect3d\include\acc3d.h
```

establishes an original `3de` source-tree component.

Its exact expansion is not present in the executable.

Given its dependency on `acc3d.h`, it belongs to the 3D engine/rendering
architecture, but do not silently expand it to a guessed phrase.

---

# 54. Why source paths survived

The paths likely originate from:

- assertions;
- internal diagnostics;
- source-file macros;
- error reporters;
- debug-support code compiled into release.

They are not a substitute for symbols, but they can anchor functions to original
translation units when a diagnostic is xref'd from one function family.

This is a high-value Ghidra technique.

---

# 55. `BUG.TXT`

Runtime includes:

```text
C:\OMIKRON\DATA\BUG.TXT
```

This suggests a hard-coded development/debug bug-reporting or trace path survived
in the retail executable.

Its existence reinforces that some internal diagnostic infrastructure was not
fully compiled away for release.

Do not assume retail installations normally write useful logs there without
tracing the path.

---

# 56. Original data/source root distinction

Source code strings use:

```text
C:\Omikron\Sources\
```

whereas one debug data path uses:

```text
C:\OMIKRON\DATA\
```

This suggests at least a conceptual tree such as:

```text
C:\Omikron\
    Sources\
    Data\
```

but the complete original workspace/directory tree is not recovered.

Do not invent sibling directories not evidenced by strings/assets.

---

# 57. Retail runtime directories

Runtime contains resource-directory names including:

```text
MESHES\DECORS
MESHES\MISC
MESHES\PERSOS
MESHES\OBJETS

ANIMS
TRAJECTOIRES
MAP2D
MORPH
BNK_ANIM_PERSO
SOUNDS
SCPTDATA
```

These are retail/runtime data-tree concepts.

Some use French naming:

```text
PERSOS
OBJETS
TRAJECTOIRES
```

which is consistent with the internal French production environment.

---

# 58. Embedded file-selector descriptions

Runtime contains a striking list of type descriptions and wildcard filters:

```text
3DO Scene File
*.3DO

3DO Sky File
*.3DO

3DO Perso File
*.3DO

CTL Bank List File
*.CTL

APT All PathGroup
*.APT

OPT Optimizes Path
*.OPT

MAP 2d map level
*.MAP

3DM Face Motion File
*.3DM

ADP Adpcm Sound File
*.ADP

3DO Object File
*.3DO

3DA Animation File
*.3DA

ANI Animation Bank File For Game
*.ANI

TAN Animation Bank File For Editor
*.TAN

WAV Sound File
*.WAV

SCX Script File
*.SCX

SFX File
*.SFX
```

This is one of the best direct snapshots of the original asset ecosystem.

---

# 59. Why editor-like file selectors are in Runtime

Possibilities include:

- shared utility/library code linked into both game and tools;
- developer/debug functionality left in retail Runtime;
- configuration/viewer modes;
- resource-selection code used by internal builds but still linked.

The presence of these strings does **not** mean normal retail gameplay exposes
a full content editor.

It does mean the executable shares substantial vocabulary with the production
asset pipeline.

---

# 60. Proven editor/game format boundary: TAN versus ANI

The most explicit authoring-pipeline distinction in Runtime is:

```text
ANI Animation Bank File For Game
TAN Animation Bank File For Editor
```

This proves that at least one animation-bank workflow distinguished:

```text
editor representation:
    TAN

game/runtime representation:
    ANI
```

Therefore a general principle for OpenNomad documentation is justified:

> Retail runtime formats are not automatically the original editable source
> formats.

---

# 61. Implication for `.3DT`

The same caution applies to `.3DT`.

Current reverse engineering establishes `.3DT` as a runtime indexed
palette/pixel payload interpreted through 3DO material metadata.

There is no evidence that artists edited raw `.3DT` files directly in GEM or
another DCC/editor.

Treat `.3DT` as a:

```text
retail/build/runtime payload
```

unless an original editor/project format proves otherwise.

---

# 62. WAV and ADP

Runtime explicitly names:

```text
WAV Sound File
ADP Adpcm Sound File
```

The retail data uses ADP heavily.

A plausible pipeline is:

```text
WAV source/editor audio
    ->
ADPCM conversion
    ->
ADP runtime asset
```

but the selector strings alone do not prove the exact converter, parameters, or
whether WAV could also be consumed directly in some modes.

Use:

```text
strongly suggested authoring/runtime distinction
```

rather than claiming a fully recovered encoder pipeline.

---

# 63. 3DM facial motion

Runtime calls:

```text
3DM Face Motion File
```

This aligns with Omikron's extensive facial motion-capture/dialogue system.

The format's exact production route is not yet documented here.

Do not confuse:

```text
3DM facial motion
```

with:

```text
3DA skeletal/body animation
```

merely because both carry “3D animation-like” data.

---

# 64. CTL

Runtime calls `.CTL`:

```text
CTL Bank List File
```

Retail files include names such as:

```text
H1Avnt.CTL
H1Cmbt.CTL
F1Avnt.CTL
F1Cmbt.CTL
```

AREA character definitions reference corresponding control/profile names.

This establishes CTL as part of the character control/animation production
pipeline.

Its complete binary format belongs in a dedicated future document.

---

# 65. APT and OPT

Runtime labels:

```text
APT All PathGroup
OPT Optimizes Path
```

The unusual English is likely an original internal description.

These files appear to belong to authored path/navigation/optimized world data.

Do not “clean up” the original labels into more specific semantics unless their
loaders establish those semantics.

---

# 66. MAP

Runtime labels:

```text
MAP 2d map level
```

and contains retail directory:

```text
MAP2D
```

This makes the role of `.MAP` in a 2D map-level representation fairly direct.

Do not confuse it with the MPT field observed in IAM AREA dependency records;
those are distinct names/extensions until their relationship is proven.

---

# 67. SCX

Runtime itself labels:

```text
SCX Script File
```

Our current reverse engineering shows SCX is broader than “just scripts”:

- structured script templates;
- path resources;
- animations;
- models;
- audio;
- scenes;
- other scenario data.

The historical file-selector label likely reflects the authoring perspective:
SCX is the script/scenario package generated for Runtime.

---

# 68. Quantic Dream internal content tools

Historical evidence and supplied archival screenshots establish several
proprietary tools/workflows associated with Omikron:

```text
IAM
GEM
GALE
RAM
```

Their exact source versions, executables and project formats have not been
recovered in the current dataset.

The roles below are therefore carefully provenance-labeled.

---

# 69. IAM — strongest historical provenance

`IAM` is exceptionally well supported.

Period retail credits explicitly list:

```text
IAM scripting
```

with team members including:

```text
Sophie Buhl/Ruhl
Nathalie Chody
Audrey Leprince
```

Historical development material describes IAM as the scripting part of the
game.

Our binary/data work independently recovers:

```text
IAM/START
IAM/AREA
IAM/DIALOG...
```

and a sophisticated scenario/event architecture.

Therefore IAM is original production terminology, not a modern fan label.

---

# 70. IAM screenshot evidence

The supplied archival IAM screenshot visibly shows a Windows authoring
application with areas such as:

```text
Area
Character
Object
Scene

Script:
    Constant
    Variable
    Expression
    Trigger
    Action
    Dialogue

Position:
    Character
    Object
    Zone
    Address
    Player
```

A function/action list visibly contains names aligning with recovered runtime
operations, including camera/object/script-related commands.

A real-time 3D world preview is embedded in the editor.

This is highly consistent with the serialized IAM AREA/VM architecture recovered
from retail data.

---

# 71. IAM editor versus runtime IAM data

We can safely infer:

```text
IAM authoring concepts
    ->
runtime IAM data
```

but should not yet claim one direct save operation:

```text
IAM editor project file == retail IAM/AREA
```

The editor may have used richer source/project files and exported flattened
runtime archives.

That distinction remains important.

---

# 72. IAM and SCX

The IAM screenshot exposes high-level actions corresponding to the wider
runtime scripting ecosystem.

Retail architecture contains two script layers:

```text
compact IAM scenario/event VM
structured SCX Script_* actions
```

It is very plausible that both are products of the same IAM/game-building
workflow.

However, the exact compiler/export stage:

```text
IAM authoring graph
    ->
AREA bytecode

IAM action timeline
    ->
SCX DEAD0002
```

has not been recovered as an original converter executable.

Document the runtime products; keep the authoring compiler boundary provisional.

---

# 73. GEM

The supplied archival screenshot visibly shows:

```text
G.E.M. Mega EDITOR
```

with orthographic views such as:

```text
XZ (Top)
ZY (Left)
XY (Front)
Camera
```

and a textured 3D preview.

The UI visibly supports geometry/camera/view editing.

Historical secondary material describes GEM as a Quantic Dream real-time 3D
model/level editing tool.

Confidence:

```text
GEM was an original 3D content editor:
    strong/corroborated

exact acronym expansion:
    unresolved

exact native project file format:
    unresolved
```

---

# 74. GEM and `.3DO`

It is tempting to write:

```text
GEM saves .3DO
```

but current evidence does not prove that exact arrow.

Safer model:

```text
GEM-authored 3D content
    ->
unknown native/project representation
    ->
export/build processing
    ->
runtime 3DO/3DT/etc.
```

A direct `.3DO` export remains possible, but should be proven by original tool
or file evidence.

---

# 75. GALE

The supplied archival GALE screenshot shows a node/tree-oriented authoring
interface with an animation/model preview.

It visually fits:

- animation logic;
- character state/control graphs;
- reusable behavior/animation connections.

Historical secondary material likewise associates GALE with animation/control
authoring.

Confidence:

```text
GALE is an original Quantic Dream animation/control authoring tool:
    strong

exact acronym expansion:
    unresolved

exact output formats:
    unresolved
```

---

# 76. GALE, CTL, TAN and ANI

A plausible relationship is:

```text
GALE
    |
    +-- animation/control authoring
    |
    +-- CTL / TAN / ANI pipeline
```

but the exact ownership of each file has not been established.

What *is* proven independently:

```text
CTL is a runtime-recognized bank-list/control format

TAN is explicitly labeled "For Editor"

ANI is explicitly labeled "For Game"
```

Future recovery of the GALE executable/project files could close these arrows.

---

# 77. RAM

`RAM` is also original production terminology in period credits:

```text
RAM Scripting
```

appears in at least the Dreamcast credits.

Historical secondary material describes a RAM-side workflow concerned with
runtime world elements such as resident/inactive entities and related scenario
state.

However:

- no definitive `RAM` executable has been recovered here;
- no retail file named simply as a RAM project format has been proven;
- “RAM scripting” may describe a tool, subsystem, workflow or all three.

Use the name without forcing an expansion.

---

# 78. Do not expand proprietary acronyms casually

Until original documentation is found, avoid asserting expansions such as:

```text
GEM = <invented phrase>
GALE = <invented phrase>
RAM = <invented phrase>
```

even when a plausible English backronym fits.

For IAM, historical material supplies “Intelligent Adventurer Manager” /
“gio” variants, but even there source wording varies.

The internal short name:

```text
IAM
```

is safer and is directly corroborated by the retail data directory.

---

# 79. Period team credits

The retail manual credits explicitly separate roles such as:

```text
Programming
Animations
IAM scripting
Cameras
Dialogue
Sound Effects
```

This division is useful architectural evidence.

In particular, a dedicated IAM-scripting credit supports the idea that the
internal tools were designed to let non-engine programmers/game builders author
substantial game behavior.

---

# 80. Historical design goal of internal tools

Contemporary/retrospective material consistently describes Quantic Dream
building in-house tools to reduce the need for designers to ask programmers to
hard-code every interaction.

This matches what the binary shows:

```text
IAM scenario VM
large native opcode table
data-driven areas/zones/characters/objects/cameras
SCX action functions
generic interface system
```

The data architecture and historical accounts reinforce each other.

---

# 81. Development-diary evidence for asset processing

Archived development material from 1998 explicitly discusses:

```text
processing the data
```

and notes that platform data could be processed differently, including
PlayStation-versus-PC differences.

This is direct historical support for a build pipeline that transformed
authoring content into platform-specific runtime data.

Therefore the conceptual layer:

```text
authoring data
    ->
processing/export
    ->
platform runtime data
```

is not merely a reverse-engineering convenience.

---

# 82. Platform-specific pipeline

Omikron development targeted multiple platforms during production.

Historical material references:

- PC;
- PlayStation development;
- later Dreamcast.

The Windows retail files we reverse engineer are therefore one platform
projection of a broader content base.

This matters when asking whether a field is:

```text
generic authoring data
```

or:

```text
PC-specific runtime allocation/cache data
```

For example, texture-page/cache fields in PC 3DO material records may reflect
the Windows renderer rather than a universal authoring representation.

---

# 83. Motion capture pipeline

Period/current Quantic material confirms Omikron used extensive:

```text
body motion capture
facial motion capture
```

The original credits identify external capture contributors.

Runtime/file-selector vocabulary includes:

```text
3DA Animation File
3DM Face Motion File
```

The exact converter chain from capture sessions to these runtime assets is not
yet recovered.

Do not assume raw capture data was stored directly as 3DA/3DM.

---

# 84. Animation pipeline layers

Current evidence suggests several distinct representations:

```text
captured/keyframed source animation
        |
        v
editor animation/control data
        |
        +-- TAN "For Editor"
        |
        v
game animation bank
        |
        +-- ANI "For Game"
        |
        +-- CTL bank/control data
        |
        +-- individual 3DA animation resources
        |
        v
Runtime
```

The relationships between all four are not yet fully mapped.

This diagram is intentionally a working pipeline, not a claim that one converter
directly transforms every left-hand file into every right-hand file.

---

# 85. Geometry pipeline layers

Current cautious model:

```text
3D artist / GEM / other DCC
        |
        v
editable scene/model data
        |
        v
build/export processing
        |
        +-- 3DO geometry/material metadata
        +-- 3DT indexed texture payloads
        +-- path/optimization/map adjunct data
        |
        v
Runtime / SCX embedding
```

Again:

```text
retail 3DO != proven GEM native project format
```

---

# 86. Sound pipeline layers

Current cautious model:

```text
recorded/edited source audio
        |
        +-- WAV source/editor representation
        |
        v
build/encode processing
        |
        +-- ADP ADPCM runtime tracks
        +-- RIFF/WAVE resources in some SCX sound payloads
        +-- other SFX/audio banks
        |
        v
Runtime audio subsystems
```

Omikron uses more than one runtime sound representation.

---

# 87. Scenario pipeline layers

Current model:

```text
IAM game-building authoring
        |
        +-- areas / characters / objects / zones / addresses / cameras
        +-- variables / expressions / triggers / actions / dialogue
        |
        v
authoring compiler/export
        |
        +-- IAM/START
        +-- IAM/AREA
        +-- IAM/DIALOG...
        +-- TAG metadata
        +-- SCX structured scripts/resources
        |
        v
Runtime scenario systems
```

The exact boundaries between IAM export and other build tools remain to be
recovered.

---

# 88. TAG files as production metadata

Retail data includes external TAG registries such as:

```text
AREAS.TAG
OBJECTS.TAG
CAMERAS.TAG
ZONES.TAG
ADDRESSES.TAG
VARIABLES.TAG
SCENES.TAG
DIALOGS.TAG
```

These preserve symbolic/editor-facing names for numeric runtime IDs.

Their presence is a strong signature of an authoring pipeline that:

1. works with human-readable names;
2. compiles/serializes numeric IDs;
3. preserves a name registry for runtime/debug/editor utility.

They are especially valuable for reconstructing the lost editor model.

---

# 89. Runtime debug/editor DNA

The shipping executable contains many elements that feel closer to an internal
engine build than to a completely sanitized consumer executable:

```text
absolute source paths
assert/diagnostic strings
DirectX symbolic error names
file picker type labels
French "Sélectionnez..." prompts
BUG.TXT path
resource/viewer-mode code
```

This does not mean the retail EXE is a debug build.

It means Quantic Dream's release build retained a substantial diagnostics/tool
support layer.

---

# 90. Why this helps reverse engineering

When a function references:

```text
C:\Omikron\Sources\libscreen\libscreen.c
```

or a distinctive diagnostic message, we can often place it into an original
source module even without symbols.

Recommended Ghidra workflow:

```text
string xref
    ->
owning function
    ->
neighboring functions / shared globals
    ->
module namespace
```

This is much more reliable than naming every function only from a local
decompilation guess.

---

# 91. Historical compiler recreation baseline

If the goal is to experiment with code generation similar to Runtime, the
current best baseline is:

```text
32-bit x86 Windows
Visual C++ 5.0 / Visual Studio 97-generation tools
Microsoft LINK 5.x
dynamic MSVCRT
DirectX 6.1 SDK-era headers/libs
Win32 4.0 target conventions
```

This is a **historical approximation environment**, not a proven byte-identical
build recipe.

---

# 92. Why VC6 should also be retained for comparison

The game shipped in late 1999, when VC6 was available.

A possible historical setup could have involved:

- older VC5 linker/import libraries;
- newer compiler frontend;
- mixed static libraries built at different times.

Because there is no Rich/PDB signature, a useful experimental RE setup could
compile small comparison cases with:

```text
VC5
VC6
```

and compare emitted patterns to characteristic Runtime functions.

That can improve confidence, but pattern similarity alone is still not absolute
proof.

---

# 93. Compiler fingerprinting experiments worth doing

Candidate micro-patterns:

```text
switch lowering
signed/unsigned division
float-to-int conversion
exception prologues
new/delete failure behavior
member-function calling convention
large stack-frame prologues
memcpy/memset inlining thresholds
small struct returns
x87 comparison sequences
```

Build the same minimal source under period toolchains and compare disassembly.

Use many independent patterns before promoting a compiler identification.

---

# 94. Linker fingerprinting experiments worth doing

Compare:

```text
section ordering
section characteristics
default alignments
import layout
CRT startup placement
resource placement
exception tables
BSS merging
default image base
PE version fields
```

against known VC5/VC6 sample programs.

The observed:

```text
MajorLinkerVersion 5.0
```

should remain the strongest direct linker clue.

---

# 95. Exact service pack remains unknown

Even if VC5 is established, possible variants include different:

- service packs;
- CRT DLL/import-library revisions;
- SDK updates;
- linker hotfixes.

Binary-reproducibility work should record the exact hashes of any historical
tool binaries used in experiments.

Do not simply label a VM:

```text
"VC5"
```

and assume all installations emit identical code.

---

# 96. DirectX SDK revision remains only partly bounded

Runtime explicitly requires:

```text
DirectX 6.1 or higher
```

This establishes a minimum runtime/API expectation.

It does not prove:

```text
the project was compiled against the exact February 1999 public SDK package
```

A later compatible SDK could expose the same APIs.

The exact historical headers/libs must be inferred from:

- interface revisions;
- structure sizes;
- GUID/IID usage;
- constants;
- import libraries;
- surviving project files.

---

# 97. `PATCH.dll` blocks a fully clean historical relink

Even with source and Microsoft SDK tools, a historically equivalent link would
also need the exact import library/component matching:

```text
PATCH.dll
```

because Runtime's imports are wired through it.

Until PATCH.dll provenance and export ABI are documented, it is a significant
unknown in the PC graphics build environment.

---

# 98. Resource-compiler inputs are missing

Byte-identical reconstruction also requires:

- `.rc` source;
- dialog templates;
- string tables;
- icons/cursors/bitmaps;
- resource include files;
- exact resource compiler version;
- resource ordering/language settings.

The final `.rsrc` section alone is not enough to reproduce the original resource
object byte-for-byte.

---

# 99. Build-order and library-order uncertainty

MS LINK output can be affected by:

```text
object order
library order
COMDAT selection
incremental-link settings
function/data ordering
static library member order
```

No original `.dsp`, `.dsw`, makefile, response file, or linker command line has
been recovered.

Therefore function addresses cannot currently be explained as an exact original
link map.

---

# 100. Incremental linking

No assertion is currently made about:

```text
/INCREMENTAL
```

or a specific incremental-link state.

Do not infer it solely from address gaps or section layout.

A stripped production executable is compatible with several linker workflows.

---

# 101. Debug information policy

The final EXE has no PE debug directory, yet release diagnostics survive.

This suggests a build policy roughly like:

```text
optimized executable
no shipped symbolic debug record
internal diagnostic/error strings retained
```

Whether separate private PDB/MAP files existed at Quantic Dream is unknown and
quite plausible.

Finding one would be extraordinarily valuable.

---

# 102. Potential `.map` linker file versus game `.MAP`

Be careful with terminology:

```text
Microsoft linker MAP file:
    textual symbol/address output

Omikron *.MAP:
    "MAP 2d map level"
```

These are unrelated.

The `.MAP` wildcard embedded in Runtime refers to a game asset, not proof that a
linker map survived.

---

# 103. Original project-system files to search for

High-value lost artifacts include:

```text
*.dsw
*.dsp
*.mak
*.ncb

*.rc
resource.h

*.def

*.lib
*.obj

*.pdb
*.map   // linker map, context-dependent

build batch files
response files
asset conversion scripts
```

Any one of these could sharply improve toolchain identification.

---

# 104. Tool executables to search for

Potentially valuable original proprietary binaries:

```text
IAM editor
GEM / G.E.M. Mega EDITOR
GALE
RAM-related tool
animation-bank converter
TAN -> ANI exporter
3DO exporter/compiler
3DT texture packer
WAV/ADP encoder
SCX compiler/packer
TAG generator
```

A tool executable may preserve a richer Rich/PDB/source-path fingerprint than
the retail Runtime.

---

# 105. Why editor binaries might be even more valuable than Runtime

Internal tools often ship or survive with:

- less aggressive stripping;
- assertions;
- menu command IDs;
- dialog resources;
- format import/export strings;
- source-control paths;
- project names;
- file-version resources.

Recovering one could answer:

```text
which file was native vs exported?
which compiler version was used?
which converter wrote a given header?
```

far more directly than runtime-only analysis.

---

# 106. Historical screenshots as evidence

Current project assets include archival screenshots identified as:

```text
iam_editor_up.png
gem_editor_up.png
gale_editor_up.png
```

They should be preserved with provenance if eventually added to repository
documentation.

Recommended metadata:

```text
source/publication
original date if known
capture date
whether image is original scan or upscale/restoration
what UI text is directly readable
```

Do not treat AI/upscaled pixels as stronger evidence than the source image.

---

# 107. IAM screenshot and runtime nomenclature correlation

The IAM screenshot visibly contains action names closely related to recovered
Runtime operations.

This is potentially valuable for recovering **original author-facing names**
for currently provisional scenario opcodes.

Recommended future workflow:

1. transcribe every visible IAM operation name;
2. compare to Runtime strings;
3. compare to scenario VM native handlers;
4. compare to SCX `Script_*` names;
5. only then replace provisional OpenNomad opcode names.

This may recover source terminology without original code.

---

# 108. GEM screenshot and format correlation

The GEM screenshot visibly exposes:

- orthographic editing views;
- camera view;
- textured environment geometry;
- material/view controls.

Potential correlations to investigate:

```text
3DO scene/decor object hierarchy
camera definitions
material names
MAP/MPT data
path/zone geometry
```

Do not assert a binary mapping based on visual resemblance alone.

---

# 109. GALE screenshot and format correlation

The GALE screenshot visibly uses a graph/tree of named animation/control nodes
and a character-preview window.

Potential correlations to investigate:

```text
CTL bank data
TAN editor animation bank
ANI runtime animation bank
3DA individual tracks
body animation names
control-state transitions
```

A recovered GALE project would be the strongest way to distinguish those
layers.

---

# 110. Period credit: IAM scripting is not marketing-only terminology

The original retail manual credits a dedicated:

```text
IAM scripting
```

team.

That matters because “IAM” also survives as the retail data directory.

Together these establish a direct production-to-runtime naming chain:

```text
production role:
    IAM scripting

retail data:
    IAM/...

Runtime:
    IAM-derived scenario systems
```

This is stronger than a later interview alone.

---

# 111. Dreamcast credit distinction: RAM scripting

Dreamcast credits additionally separate:

```text
IAM Scripting
RAM Scripting
```

This suggests RAM was a genuine production discipline/system distinct enough to
credit separately.

The Windows Runtime's relationship to RAM still needs direct binary mapping.

Do not assume that every Dreamcast-specific credit corresponds to an identical
PC tool binary or data layout.

---

# 112. Tool philosophy and runtime architecture

Historical descriptions characterize IAM as enabling game builders to assemble
complex gameplay without programming each case manually.

Runtime directly supports that philosophy:

```text
153 native scenario VM opcodes
shared globals
areas
zones
characters
objects
addresses
cameras
interfaces
music
SCX script launching
presentation effects
```

This is one of the strongest cases where historical tool descriptions and
binary architecture independently converge.

---

# 113. Native operation catalogue as an authoring API

The scenario VM opcode table at:

```text
0x004C0140
```

can be interpreted conceptually as the Runtime side of the IAM authoring API.

Likewise the SCX `Script_*` dispatch table exposes higher-level actions.

A likely toolchain relationship is:

```text
editor command/function
    ->
stable numeric serialized ID/opcode
    ->
Runtime native handler
```

This explains why many action names can survive across:

- editor screenshots;
- TAG metadata;
- Runtime diagnostics;
- serialized numeric IDs.

---

# 114. Source-level modularity versus final global architecture

The source tree clearly had reusable libraries:

```text
libscreen
libdirect3d
libpoly2d
LIBI2D
```

yet the final executable relies heavily on process-global state.

Do not mistake the latter for lack of source modularity.

Late-1990s C library modules commonly exposed globals and function APIs rather
than modern object ownership.

OpenNomad should preserve semantic module boundaries without copying global
memory architecture.

---

# 115. Static libraries are likely but not directly proven

Directory names beginning with:

```text
lib...
```

strongly suggest separately maintained libraries/modules.

However the retail EXE does not expose the original linker inputs.

Thus:

```text
libdirect3d.lib definitely existed
```

is not yet proven.

The source module may have been built as:

- a static `.lib`;
- directly included project source;
- a makefile object group.

Use “library/module” unless the original `.lib` is found.

---

# 116. Build-language mixture

A plausible original solution/workspace therefore contained:

```text
C graphics/platform libraries
+
C++ engine/application code
+
Windows resources
+
DirectX/COM declarations
```

This mixture is consistent with the import and source-path evidence.

It also explains why Ghidra encounters both:

- plain C-style data/function architecture;
- C++ exceptions/virtual calls/classes.

---

# 117. No modern standard-library dependency

There is no separate:

```text
MSVCPxx.dll
```

import.

That means the executable does not rely on the later separately distributed
Microsoft C++ Standard Library DLL family.

Do not interpret this as proof that no templates/STL were used: old Microsoft
library/linking arrangements differ, and unused/inlined templates do not imply a
DLL import.

What is proven is simply the static import set.

---

# 118. Application entrypoint

The PE entrypoint:

```text
0x00411640
```

is Microsoft CRT startup.

It eventually calls the Omikron WinMain-equivalent around:

```text
0x00410950
```

This is classic GUI-subsystem Microsoft C runtime structure.

See `startup-sequence.md` for the exact flow.

---

# 119. WinMain calling environment

The CRT obtains:

```text
HINSTANCE
raw command line
STARTUPINFO/nCmdShow
```

then invokes the game WinMain path.

Runtime subsequently performs its own crude space-delimited command-line
tokenization.

That parser is game code, not an indication of compiler command-line behavior.

---

# 120. Build timestamp caveat

PE timestamp:

```text
1999-10-04 20:31:50
```

is useful provenance but should not be treated as cryptographically reliable
proof of the exact source build date.

PE timestamps can be altered by:

- relinking;
- patching;
- packaging;
- intentional modification.

Here it is historically plausible and there is no current evidence it is
fabricated.

---

# 121. Reproducing the old build safely

Historical Microsoft toolchains and SDKs are obsolete.

If experimentation is needed, use an isolated VM with:

```text
no privileged host access
no sensitive credentials
minimal networking
snapshots
hash-recorded installation media/tools
```

The goal is controlled compiler archaeology, not using unsupported software as
a daily development environment.

---

# 122. Do not commit proprietary tool binaries

OpenNomad should not redistribute:

- Visual C++ installers;
- Microsoft DirectX SDK binaries;
- original Quantic Dream editor executables;
- proprietary game DLLs/assets

unless their licensing explicitly permits redistribution.

Useful repository artifacts can instead include:

```text
hashes
version notes
derived structure declarations
reverse-engineering documentation
small independently authored test cases
```

---

# 123. Ghidra compiler model

For `Runtime.exe`, the analysis model should reflect:

```text
x86 32-bit little endian
Windows ABI
4-byte pointers
MSVC-era calling conventions
x87 floating point
old Win32/DirectX types
```

Function-by-function calling convention should come from machine code, not a
single project-wide guess.

---

# 124. Ghidra `_MSC_VER` policy

Do not globally use:

```c
#define _MSC_VER 1200
```

as a historical claim.

If a Ghidra-safe header requires compiler-feature branches, either:

1. define a synthetic compatibility macro with a comment that it is for parsing
   only; or
2. rewrite the header to avoid compiler-version branches.

Preferred comment:

```c
/* Parser compatibility only. Runtime compiler frontend is not proven. */
```

---

# 125. Ghidra Win32 version macros

Likewise:

```c
WINVER
_WIN32_WINNT
DIRECTDRAW_VERSION
DIRECTINPUT_VERSION
```

used to make headers parse are not automatically evidence of the original
project's preprocessor definitions.

Separate:

```text
header-emulation value
```

from:

```text
binary-proven target/API behavior
```

in all reverse-engineering headers.

---

# 126. Historical DirectX headers

A good Ghidra header should be based on the closest-era DirectX declarations,
because:

- COM vtable slot order matters;
- structure size matters;
- enum values matter;
- old interfaces differ from modern descendants.

But the final header should be minimized and independently documented.

Do not copy an enormous SDK tree into OpenNomad simply to type a handful of
vtable calls.

---

# 127. Compiler-generated CRT code should not be reimplemented

Functions such as the PE entrypoint's CRT initialization are valuable for
toolchain identification but are **not** OpenNomad behavior requirements.

OpenNomad needs to reproduce:

```text
game-visible initialization semantics
```

not:

```text
MSVC 1997 CRT internals
```

This distinction prevents toolchain archaeology from contaminating modern
architecture.

---

# 128. Old unsafe behavior is not a fidelity target

Examples:

```text
fixed command-line buffers
unchecked trusted-data pointer arithmetic
small fixed context stacks
raw malloc/free ownership
global arrays without modern bounds abstractions
```

Document them because they reveal Runtime behavior.

Do not deliberately recreate memory-unsafety where a checked modern
implementation can preserve observable semantics.

---

# 129. Historical build versus OpenNomad build

These are intentionally different:

```text
Original:
    Win32
    x86
    MSVC-era
    DirectX 6.1
    DirectDraw/Direct3D
    MSVCRT
    fixed globals

OpenNomad:
    modern C++
    CMake/vcpkg/modern toolchains
    SDL3
    modern graphics/audio abstractions
    safe ownership
```

The purpose of original-toolchain documentation is to improve RE accuracy, not
to make OpenNomad compile like a 1999 game.

---

# 130. Where historical compiler behavior does matter

Some behavior can leak into game semantics and deserves emulation:

```text
integer truncation
signed overflow assumptions in specific code paths
x87 float conversion
structure packing
bitfield/layout conventions
calling convention
32-bit pointer-sized serialized placeholders
row-vector math conventions
```

Those are semantic compatibility issues.

Compiler-specific startup code and binary section layout are not.

---

# 131. Where old DirectX behavior matters

Relevant fidelity areas include:

```text
color conversion
palette formats
surface pitch
texture capability constraints
blend/render-state semantics
viewport conventions
pixel-center/rasterization behavior
display gamma/color handling
FMV presentation
```

Modern rendering should emulate the **observable effect**, not necessarily use
the same API.

---

# 132. Where authoring-tool behavior matters

Original editor/compiler behavior can explain otherwise mysterious runtime
fields.

Examples:

```text
why IDs are stable across TAG files
why TAN and ANI coexist
why 3DA key 0 is a rest/reference entry
why AREA has multiple event entrypoints
why SCX has binding-name tables
why some runtime descriptors retain editor-facing names
```

Recovering the authoring pipeline can therefore resolve format semantics that
runtime tracing alone leaves ambiguous.

---

# 133. Provenance table: executable build

| Fact | Status |
|---|---|
| 32-bit x86 PE | Confirmed — PE |
| Win32 GUI subsystem | Confirmed — PE |
| image base `0x00400000` | Confirmed — PE |
| linker stamp `5.0` | Confirmed — PE |
| relocations stripped | Confirmed — PE |
| no Debug Directory | Confirmed — PE |
| no Rich signature found | Confirmed — binary scan |
| dynamic `MSVCRT.dll` | Confirmed — imports |
| Microsoft C++ runtime use | Confirmed — imports |
| raw Win32 USER/GDI shell | Confirmed — imports |
| DirectX 6.1 minimum | Confirmed — Runtime string |
| exact `CL.EXE` build | Unknown |
| exact VC service pack | Unknown |
| exact compiler options | Unknown |
| exact DirectX SDK package | Unknown |

---

# 134. Provenance table: source organization

| Evidence | Status |
|---|---|
| `C:\Omikron\Sources` source root | Confirmed — Runtime strings |
| `libdirect3d` module | Confirmed — source paths |
| `libscreen` module | Confirmed — source paths |
| `libpoly2d` module | Confirmed — source paths |
| `LIBI2D` module | Confirmed — source paths |
| `3de` module | Confirmed — source path |
| `omikron` game-specific source directory | Confirmed — source path |
| graphics modules contain `.c` source | Confirmed — source paths |
| exact VS workspace/project names | Unknown |
| exact static-library filenames | Unknown |

---

# 135. Provenance table: content tools

| Tool/workflow | Evidence | Current confidence |
|---|---|---|
| IAM | retail credits + IAM data + screenshot + Runtime architecture | Very strong |
| GEM / G.E.M. Mega EDITOR | archival screenshot + historical material | Strong |
| GALE | archival screenshot + historical material | Strong |
| RAM scripting/workflow | period credits + historical material | Strong existence, exact tool semantics unresolved |
| TAN editor animation bank | Runtime literal description | Confirmed |
| ANI game animation bank | Runtime literal description | Confirmed |
| exact GEM native format | none recovered | Unknown |
| exact GALE native format | none recovered | Unknown |
| exact IAM project format | none recovered | Unknown |

---

# 136. Provenance table: runtime asset vocabulary

| Extension | Runtime's embedded description |
|---|---|
| `.3DO` | Scene / Sky / Perso / Object file |
| `.CTL` | Bank List File |
| `.APT` | All PathGroup |
| `.OPT` | Optimizes Path |
| `.MAP` | 2d map level |
| `.3DM` | Face Motion File |
| `.ADP` | Adpcm Sound File |
| `.3DA` | Animation File |
| `.ANI` | Animation Bank File For Game |
| `.TAN` | Animation Bank File For Editor |
| `.WAV` | Sound File |
| `.SCX` | Script File |
| `.SFX` | SFX File |

Spelling/capitalization above intentionally follows the embedded strings.

---

# 137. What is not established

Do **not** state as fact:

```text
Runtime was definitely compiled by VC6
Runtime was definitely compiled by VC5 SP3
_MSC_VER was 1200
the original IDE was definitely Visual Studio 6
GEM directly saved retail .3DO
GALE directly saved retail .CTL
IAM directly saved the installed IAM/AREA archive
all .WAV files were converted to .ADP by one known encoder
PATCH.dll is definitely an Eidos wrapper
Direct3D interface revision X without vtable/IID proof
```

These are exactly the kinds of seemingly small assumptions that become
expensive later in reverse engineering.

---

# 138. Strongest current unknown: compiler frontend

The best next evidence would be another executable from the **same PC build
environment** that retained:

```text
Rich header
PDB path
file version
compiler signature
```

Good candidates:

- `CONFIG` utility;
- editor/viewer tools accidentally shipped;
- original demo EXE;
- installer helper built by Quantic rather than Eidos;
- matching PATCH.dll;
- any internal tool executable from the same archive.

Comparing several binaries could identify a shared compiler/linker revision.

---

# 139. Strongest current unknown: PATCH.dll

Highest-value questions:

1. exact supplied retail PATCH.dll hash/version;
2. exports and forwarding behavior;
3. whether it loads real `DDRAW.dll`;
4. any source/company/version resources;
5. whether other games used the same DLL;
6. whether debugging without it changes graphics initialization;
7. whether its import library explains linker behavior.

This DLL is part of the actual PC runtime boundary and deserves dedicated
documentation.

---

# 140. Strongest current unknown: authoring exporters

The most valuable content-tool discoveries would be:

```text
TAN -> ANI conversion
GALE/CTL export
GEM -> 3DO/3DT export
IAM -> AREA/SCX compilation
WAV -> ADP conversion
motion capture -> 3DA/3DM
```

Finding even one converter would expose source-level terminology and validation
rules that Runtime only implies indirectly.

---

# 141. Search targets inside historical media

When inspecting old development CDs, magazine cover CDs, backups or leaked
archives, search filenames/strings for:

```text
GEM
GALE
IAM
RAM

Omikron
Quantic

3DO
3DA
3DT
3DP
3DM
TAN
ANI
CTL
SCX
APT
OPT

convert
export
compile
build
pack
bank
```

Also search for original Windows development artifacts:

```text
.dsp
.dsw
.rc
.pdb
.map
.obj
.lib
```

---

# 142. Historical reference notes

External evidence used in this reconstruction includes:

1. **Microsoft Learn, `/msc_ver switch`** — documents compiler-version values:
   `1100` for Visual C++ 5.0 and `1200` for Visual C++ 6.0.
2. **Microsoft, “Microsoft Ships DirectX 6.1”, 1999-02-03** — dates the public
   DirectX 6.1 SDK release.
3. **Original Omikron retail manual credits** — explicitly credit an
   “IAM scripting” team.
4. **Archived Omikron development diaries / contemporary press mirrors** —
   describe platform-specific data processing and IAM development.
5. **Later developer retrospectives/interviews** — corroborate IAM and the
   proprietary content-tool workflow.
6. **Archival screenshots supplied to this project** — visually document IAM,
   G.E.M. Mega EDITOR and GALE interfaces.

External historical material is subordinate to Runtime/data evidence whenever
the two conflict.

---

# 143. Reconstructed historical development workstation

A **plausible**, not proven, workstation environment for the final PC phase is:

```text
Windows NT 4.0 and/or Windows 9x-generation development host

Microsoft Visual C++ / Visual Studio 97-generation toolchain
possibly mixed with later components

DirectX 6.1-era SDK

Quantic Dream proprietary:
    IAM
    GEM
    GALE
    RAM-related tools

external DCC / motion-capture software
exact packages not established

local project tree:
    C:\Omikron\Sources
    C:\Omikron\Data
```

Do not elevate the OS or IDE specifics above “plausible” without original
machine/project evidence.

---

# 144. Historical rebuild recipe — conservative version

For compiler archaeology only:

```text
1. Create isolated 32-bit-capable Windows VM.

2. Install a known, hash-recorded Visual C++ 5.0/VS97 environment.

3. Install period DirectX 6.1 SDK headers/libs.

4. Build tiny Win32/DirectX/C/C++ fingerprint programs.

5. Compare:
       PE linker fields
       CRT startup
       machine-code idioms
       exception frames
       section layout

6. Repeat selected tests with Visual C++ 6.0.

7. Record exact tool hashes and service packs.

8. Treat the result as evidence, not as proof, until multiple fingerprints
   converge.
```

Do not attempt to compile OpenNomad with this environment.

---

# 145. Reimplementation policy

The original toolchain should guide:

```text
reverse engineering
structure layout
math fidelity
API semantics
asset interpretation
source-module naming
```

It should **not** dictate:

```text
OpenNomad compiler
OpenNomad OS support
OpenNomad ownership patterns
unsafe buffers
fixed global memory
obsolete DirectX API use
```

Modernization and fidelity can coexist.

---

# 146. Recommended Ghidra labels from provenance

Where xrefs support them, useful module prefixes include:

```text
Direct3D_
Acc3D_
Screen_
Poly2D_
I2D_
Scenario_
Script_
```

Avoid applying a module prefix merely because a function is near another one in
address space.

Prefer:

```text
source-path diagnostic xref
+
shared globals
+
call graph
```

before assigning module ownership.

---

# 147. Recommended original-source annotation

For a function firmly tied to a source-path string, store a Ghidra plate comment
such as:

```text
Original source evidence:
    C:\Omikron\Sources\libscreen\libscreen.c

Evidence:
    diagnostic/assert string xref from this function

Confidence:
    confirmed source module; original function name unresolved
```

This separates original provenance from our reconstructed function name.

---

# 148. Recommended compiler annotation

At program level:

```text
Compiler family:
    Microsoft Visual C/C++, late 1990s

Linker:
    PE reports Microsoft-style linker version 5.0

Frontend:
    unresolved; VC5-era best fit

CRT:
    dynamic MSVCRT.dll

Target:
    Win32 x86 GUI, subsystem 4.0
```

Do not label Ghidra's compiler spec simply:

```text
Visual C++ 6.0
```

without a note describing why.

---

# 149. Recommended DirectX annotation

Program-level comment:

```text
Runtime explicitly requires DirectX 6.1+.

DirectDraw creation/enumeration imported via PATCH.dll.
DirectSound and DirectInput imported directly.
Direct3D uses legacy COM/vtable interfaces.
Exact Direct3D interface revisions should be established per vtable/IID.
```

This is both accurate and useful.

---

# 150. Compact reference

```text
Runtime.exe
===========

PE32 i386
GUI subsystem
image base 00400000

linker:
    5.0

timestamp:
    1999-10-04 20:31:50

relocations:
    stripped

debug directory:
    none

Rich:
    none found

CRT:
    MSVCRT.dll dynamic

language:
    C-heavy source tree + C++ runtime/EH

platform:
    raw Win32
    DirectX 6.1+

graphics:
    DirectDraw via PATCH.dll
    legacy Direct3D COM
    GDI/USER32

audio/input:
    DirectSound
    WINMM
    DirectInput
```

---

# 151. Compact original source-tree reference

```text
C:\Omikron\Sources\
    libdirect3d\
        bw.c
        InitCartes.c
        acc3d.c
        include\acc3d.h
        acc3Dprivate.h

    libscreen\
        libscreen.c

    libpoly2d\
        gereaff.c

    LIBI2D\
        libi2dpc.c

    3de\
        ...

    omikron\
        I2D_Bump.c
```

Also observed:

```text
C:\OMIKRON\DATA\BUG.TXT
```

---

# 152. Compact content-tool reference

```text
IAM
    scenario/game-building/scripting
    strongly corroborated by retail data and credits

GEM / G.E.M. Mega EDITOR
    3D environment/model editing
    strong historical/screenshot evidence

GALE
    animation/control graph authoring
    strong historical/screenshot evidence

RAM
    distinct credited scripting/workflow domain
    precise tool/data ownership still unresolved
```

---

# 153. Compact export-format reference

```text
3DO   scene/sky/person/object
CTL   bank list/control
APT   path groups
OPT   optimized path data
MAP   2D map level
3DM   facial motion
ADP   ADPCM sound
3DA   animation
ANI   animation bank for game
TAN   animation bank for editor
WAV   sound
SCX   script/scenario package
SFX   effects
```

Most important explicit pipeline clue:

```text
TAN = For Editor
ANI = For Game
```

---

# 154. Boundary of current knowledge

We can now reconstruct the **shape** of the original PC production environment
with high confidence:

```text
Microsoft 32-bit Win32 C/C++ build
Microsoft LINK 5.0-era output
dynamic MSVCRT
DirectX 6.1-era graphics/audio/input
Quantic internal C graphics libraries
Quantic proprietary game-building/content tools
platform-specific asset processing
retail runtime/export formats
```

We cannot yet reconstruct:

```text
exact CL.EXE version/service pack
exact compiler/link switches
exact DirectX SDK build
original VS workspace/makefiles
PATCH.dll provenance
native GEM/GALE/IAM project formats
exact asset-converter executable chain
```

The most important methodological rule is:

> **Do not turn a compatibility setting into historical evidence.**
>
> `_MSC_VER`, DirectX header-version macros, guessed editor-output arrows, and
> modern reconstructed struct names are useful tools, but the authoritative
> history must remain grounded in the PE, machine code, retail data, surviving
> source strings, and period production evidence.
