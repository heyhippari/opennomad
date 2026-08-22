# Runtime FMV / startup movie pipeline

> **Status:** work-in-progress reverse-engineering documentation for OpenNomad  
> **Last updated:** 2026-08-22
>
> This document describes the Windows retail Runtime's full-motion-video
> pipeline, with particular focus on the three startup MPEG files:
>
> ```text
> FLIS\EIDOS.mpg
> FLIS\QUANTIC.mpg
> FLIS\GAME.mpg
> ```
>
> The most important architectural result is that Omikron does **not** contain
> an in-house MPEG decoder. It constructs a **DirectShow filter graph** using
> the system Filter Graph Manager, installs a Quantic Dream custom video
> renderer filter, and lets DirectShow build the rest of the playback graph.
>
> The custom renderer receives decoded 16-bit RGB video samples, performs
> optional pixel-format remapping, copies the result to a DirectDraw movie
> surface, blits that surface into the game's back buffer, and presents by
> either `Blt` or `Flip`.
>
> OpenNomad replaces the complete DirectShow/DirectDraw stack with FFmpeg,
> SDL3 audio, and an OpenGL textured-quad presenter. This document separates
> those modern choices from recovered Runtime behavior.

Related documentation:

- [`startup-sequence.md`](startup-sequence.md) — exact placement of the movie
  phase in process startup;
- [`runtime-main-loop.md`](runtime-main-loop.md) — the normal recurring game
  loop, which begins only after the startup movies and splash phase;
- [`renderer.md`](renderer.md) — DirectDraw/Direct3D display architecture and
  native pixel-format conversion;
- [`audio.md`](audio.md) — normal ADP music and SCX sound systems; movie audio
  is a separate DirectShow/FFmpeg path;
- [`runtime-globals.md`](runtime-globals.md) — process-global FMV and graphics
  state;
- [`original-toolchain.md`](original-toolchain.md) — Win32/DirectX/COM
  environment.

---

# 1. Evidence model

Source precedence:

1. **direct disassembly of the supplied retail `Runtime.exe`;**
2. **COM GUIDs and legacy DirectShow/DirectDraw ABI behavior;**
3. **retail MPEG files and observed retail playback;**
4. **current OpenNomad FFmpeg implementation;**
5. visual comparison against external players.

Confidence labels:

- **Confirmed — Runtime:** direct executable behavior.
- **Confirmed — data:** direct retail-file observation.
- **Confirmed — API:** established by legacy Microsoft interface ABI/API.
- **Corroborated:** independent evidence agrees.
- **Strongly reconstructed:** architecture/role is clear, original symbol name
  is unavailable.
- **Provisional:** plausible working interpretation requiring more tracing.
- **OpenNomad-only:** deliberate modern policy, not original Runtime behavior.

Reference executable:

```text
SHA-256:
55f7120bfea7891b048c64e3682f3259cdbf2719a43fa24e42254b753c95d2ef

image base:
0x00400000
```

---

# 2. Executive architecture

Recovered Runtime pipeline:

```text
Runtime startup
    |
    v
movie requested
    |
    v
DirectShow Filter Graph Manager
CLSID_FilterGraph
IGraphBuilder
    |
    +-- system file/source/splitter/decoder filters
    +-- system audio renderer
    |
    +-- Quantic custom video renderer filter
            |
            v
      decoded 16-bit RGB sample
            |
            +-- optional 65536-entry
            |   pixel-format conversion LUT
            |
            v
      CPU staging memory
            |
            v
      movie DirectDraw surface
            |
            v
      game back buffer
            |
            +-- windowed:
            |      primary.Blt(client rectangle)
            |
            +-- fullscreen:
                   primary.Flip(backbuffer)
```

OpenNomad:

```text
FFmpeg demux/decoder
    |
    +-- video:
    |      YUV -> RGBA8 through libswscale
    |      -> OpenGL texture
    |      -> fullscreen quad
    |
    +-- audio:
           libavcodec/libswresample
           -> SDL3 audio stream
```

The architectural boundary is therefore unusually clear.

---

# 3. Startup movie order

Top-level lifetime wrapper:

```text
0x00439470
```

runs the movie phase after core engine initialization and before:

```text
aventure.scx
OMIKRON.BMP splash
ordinary main loop
```

Normal order:

```text
1. FLIS\EIDOS.mpg
2. FLIS\QUANTIC.mpg
3. FLIS\GAME.mpg
```

Then:

```text
SCPTDATA\aventure.scx
IMAGES\OMIKRON.BMP
0x00439310 ordinary frame/message loop
```

This ordering is Runtime-confirmed.

---

# 4. Movie filename strings

Static Runtime strings:

```text
0x004C5F9C  "FLIS\EIDOS.mpg"
0x004C5FAC  "FLIS\QUANTIC.mpg"
0x004C5FC0  "FLIS\GAME.mpg"
```

The code copies each path into a local buffer before calling the generic movie
wrapper:

```text
0x0043B4E0
```

The three files are not hard-coded into the lower-level DirectShow player.

---

# 5. Global FMV suppression — `0x009103CD`

Working name:

```c
uint8_t g_noFmv;
```

The startup movie block is skipped entirely when:

```text
g_noFmv != 0
```

This flag is set by at least two independent paths.

---

# 6. `NOFMV` command-line token

Runtime's command-line parser recognizes:

```text
NOFMV
```

and sets:

```text
0x009103CD = 1
```

This is direct evidence that startup-video suppression was an intended
developer/user mode in the original executable.

OpenNomad may expose an equivalent configuration option without reproducing the
old command-line parser exactly.

---

# 7. Failed early display probe also disables FMV

WinMain initializes:

```text
g_noFmv = 0
```

then calls an early graphics/movie compatibility probe around:

```text
0x0043B300
```

If that probe fails, Runtime sets:

```text
g_noFmv = 1
```

Thus startup movies are considered optional presentation:

```text
movie display unsupported
    ->
skip FMV
    ->
continue game startup
```

This is useful modern behavior to preserve.

---

# 8. Early movie/display probe — `0x0043B300`

The probe:

1. obtains a DirectDraw interface;
2. queries:
   ```text
   IID_IDirectDraw4
   ```
3. creates a small DirectDraw surface;
4. queries that surface's pixel format;
5. stores RGB channel masks;
6. counts bits present in each channel mask;
7. releases temporary COM objects.

Failure returns false and suppresses FMV playback.

---

# 9. `IID_IDirectDraw4`

GUID stored at:

```text
0x004BD358
```

decodes as:

```text
9C59509A-39BD-11D1-8C4A-00C04FD930C5
```

which is:

```text
IID_IDirectDraw4
```

This establishes the DirectDraw interface generation directly rather than by
vtable guess alone.

---

# 10. Probe surface

`0x0043B300` builds a `DDSURFACEDESC2`-sized structure:

```text
dwSize = 0x7C
```

with dimensions:

```text
256 × 256
```

and caps value:

```text
0x40
```

in the relevant capability slot.

It then calls:

```text
IDirectDraw4::CreateSurface
```

and:

```text
surface->GetSurfaceDesc(...)
```

to retrieve the actual pixel format.

The exact purpose is compatibility/pixel-format discovery rather than visible
presentation.

---

# 11. Stored source/reference RGB masks

The probe records three 16-bit-relevant channel masks at:

```text
0x0052DDB0
0x0052DDAC
0x0052DDA8
```

and counts set bits into:

```text
0x0052DDBC
0x0052DDB8
0x0052DDB4
```

respectively.

A conservative interpretation is:

```text
reference/source 16-bit RGB masks
+
their channel precision
```

used by the later movie conversion LUT.

Do not assume a hard-coded:

```text
RGB565
```

or:

```text
RGB555
```

for every machine.

Runtime derives the actual format from DirectDraw.

---

# 12. Movie scratch initializer — `0x0043B4A0`

Before the startup sequence, Runtime allocates two movie work buffers.

First:

```text
calloc(2, 0x10000)
```

total:

```text
0x20000 bytes
=
131072 bytes
```

stored at:

```text
0x0052DDC0
```

This is exactly large enough for:

```text
65536 × uint16_t
```

and is later used as a complete 16-bit pixel conversion lookup table.

Recommended:

```c
uint16_t *g_moviePixelConversionLut;
```

---

# 13. Movie frame staging buffer

Second allocation:

```text
calloc(2, 0x25800)
```

total allocation:

```text
0x4B000 bytes
=
307200 bytes
```

stored at:

```text
0x0052DDC4
```

This is a CPU staging buffer used by the custom renderer when copying decoded
16-bit frames to a DirectDraw surface.

Recommended neutral name:

```c
void *g_movieFrameStaging;
```

Do **not** call it a `640×480 framebuffer`.

Its allocation size alone does not establish one unique intended geometry.

For reference:

```text
320 × 240 × 2 bytes = 153600 = 0x25800
```

so the allocation is exactly twice that quantity.

The reason for the two-frame-equivalent capacity has not yet been recovered.

---

# 14. Movie scratch cleanup — `0x0043B7F0`

After the startup movie sequence Runtime frees both buffers and clears their
global pointers/state.

Movie scratch therefore has a deliberately narrow lifetime:

```text
core initialization
    ->
movie scratch allocate
    ->
up to three startup videos
    ->
movie scratch free
    ->
aventure.scx
```

OpenNomad similarly scopes its startup `VideoPlayer`/`VideoScene` before
permanent scenario loading.

---

# 15. Generic movie wrapper — `0x0043B4E0`

The startup caller passes eight arguments.

Current reconstructed conceptual signature:

```c
bool PlayMovie(
    const char *path,
    uint32_t pixelConversionEnabled,
    HWND gameWindow,
    IDirectDraw4 *directDraw,
    IDirectDrawSurface4 *backBuffer,
    IDirectDrawSurface4 *primarySurface,
    bool windowed,
    MovieCallback callback);
```

The exact C source signature/order/name is not recovered.

Several object names above are based on decisive method use rather than
original symbols.

---

# 16. `0x0043B4E0` output dimensions

The wrapper sets:

```text
0x00907A30 = 640
0x00907A34 = 480
```

before starting playback.

These are game/movie presentation dimensions, not the decoded MPEG dimensions.

The custom filter separately stores decoded movie width/height.

This distinction is important:

```text
decoded video dimensions
    !=
game output surface dimensions
```

---

# 17. Windowed-mode argument

Startup passes:

```text
BYTE [0x0091030A]
```

to `0x0043B4E0`.

This field is the accepted/active alternate windowed DirectDraw mode already
documented as:

```text
g_windowedModeActive
```

Movie presentation later branches on this value between:

```text
windowed Blt
```

and:

```text
fullscreen Flip
```

---

# 18. Movie callback

Startup passes:

```text
0x00439730
```

as the per-loop movie callback.

The lower-level movie loop stores it at:

```text
0x00907B70
```

and marks callback use through:

```text
0x00907B6C
```

The callback is invoked while synchronous DirectShow playback is active.

This is how normal game input can interrupt startup movies even though the
ordinary `0x00439310` main loop has not started yet.

---

# 19. Persistent startup movie state — `0x0052DD54`

Working interpretation:

```c
uint32_t g_startupMovieSkipState;
```

At each movie boundary, `0x00439470` checks:

```text
0x0052DD54 == 0
```

before starting the next movie.

Therefore once the field becomes nonzero:

```text
remaining startup movies are skipped
```

for that startup sequence.

This is more specific than the older generic label:

```text
movie state
```

---

# 20. Startup skip is sequence-persistent

The top-level logic is:

```text
if movieSkipState == 0:
    play Eidos

if movieSkipState == 0:
    play Quantic

if movieSkipState == 0:
    play Game intro
```

Thus the original semantic is **not** merely:

```text
skip current video
```

A skip condition can terminate the current movie and suppress subsequent
startup movies as well.

---

# 21. Startup input callback — `0x00439730`

High-level behavior:

```text
if g_startupMovieSkipState != 0:
    return

poll normal input subsystem

if relevant input condition:
    abort/close current movie

if secondary input/result value == 2:
    g_startupMovieSkipState = 1
```

The function always returns a nonzero status to its lower-level caller.

The exact meaning of the two low-level input outputs should remain documented
separately from the proven movie-state effect.

---

# 22. Input subsystem reuse

The callback calls:

```text
0x0043E0D0
```

which is the same lower-level input polling/snapshot subsystem used by the
ordinary frame driver.

This confirms:

```text
startup FMV skip input
```

is integrated into normal game input infrastructure rather than being a
DirectShow keyboard hook.

---

# 23. Important OpenNomad skip difference

Current OpenNomad invokes the three `StartupVideoSequence` slots separately and
its stop predicate checks the per-frame:

```text
k_skip_video
```

input action.

After one slot returns, the next slot is still attempted.

Unless a separate persistent state is introduced, this behaves as:

```text
Escape -> skip current movie only
```

whereas Runtime has a sequence-persistent skip state.

This is a concrete current fidelity gap.

Recommended OpenNomad behavior:

```text
once the Runtime-equivalent startup skip state is asserted:
    stop current movie
    mark remaining startup video slots skipped by user
```

---

# 24. DirectShow identification

Four GUIDs embedded in Runtime identify the movie subsystem decisively.

```text
0x004BEAC8
E436EBB3-524F-11CE-9F53-0020AF0BA770
    CLSID_FilterGraph

0x004BEAD8
56A868A9-0AD4-11CE-B03A-0020AF0BA770
    IID_IGraphBuilder

0x004BEAE8
56A868B6-0AD4-11CE-B03A-0020AF0BA770
    IID_IMediaEvent

0x004BEAF8
56A868B1-0AD4-11CE-B03A-0020AF0BA770
    IID_IMediaControl
```

This is direct binary evidence for Microsoft's DirectShow Filter Graph
Manager.

---

# 25. DirectShow terminology

Microsoft's DirectShow architecture uses a:

```text
Filter Graph Manager
```

to connect source, splitter/parser, decoder, renderer and audio filters.

The manager exposes interfaces including:

```text
IGraphBuilder
IMediaControl
IMediaEvent
```

`IGraphBuilder::RenderFile` constructs a playback graph for a media file.

Runtime follows exactly this architecture.

---

# 26. Filter graph creation — `0x0048F520`

Runtime performs conceptually:

```c
CoCreateInstance(
    CLSID_FilterGraph,
    NULL,
    CLSCTX_INPROC_SERVER,
    IID_IGraphBuilder,
    &g_filterGraph);
```

Global:

```text
0x00660B70
```

holds the resulting graph-builder pointer.

Recommended:

```c
IGraphBuilder *g_movieGraph;
```

---

# 27. Custom renderer allocation

After creating the graph, Runtime allocates:

```text
0x778 bytes
```

and constructs a custom COM/filter object around:

```text
0x004B4190
```

The resulting object is stored at:

```text
0x00660B74
```

Recommended:

```text
g_movieRendererFilter
```

with its exact original class name unresolved.

---

# 28. The custom object is added to the graph

`0x0048F520` obtains the filter-facing subobject/interface and calls:

```text
IFilterGraph::AddFilter(customRenderer, NULL)
```

before the movie file is rendered.

This is critical.

Runtime is **not** relying on DirectShow's default video renderer/window.

It deliberately inserts Quantic's own renderer into the graph so decoded video
frames can be copied into the game's DirectDraw presentation chain.

---

# 29. Why this matters

Without the custom filter, a default DirectShow playback graph could create its
own video window/surface.

Omikron instead needs video to:

- appear inside its existing fullscreen/windowed DirectDraw mode;
- use the game's front/back surfaces;
- respect the game's pixel format;
- integrate with its skip/input callback;
- avoid a separate native video window.

The custom renderer provides that boundary.

---

# 30. Media-event setup — `0x0048F600`

Runtime queries the graph for:

```text
IID_IMediaEvent
```

then calls a method corresponding to:

```text
GetEventHandle
```

storing the event handle at:

```text
0x00660B78
```

It also disables DirectShow's default handling for event code:

```text
1
```

through the appropriate event-interface call.

Event code 1 is:

```text
EC_COMPLETE
```

in DirectShow.

This lets Runtime own completion handling in its synchronous movie loop.

---

# 31. Graph-building wrapper — `0x0048F650`

This helper performs:

```text
create graph/custom renderer
    ->
set up media event
```

and is used by the file-rendering path.

It is a useful Ghidra grouping boundary.

---

# 32. Movie file build — `0x0048F660`

This function:

1. creates/initializes the graph;
2. switches the cursor to the wait cursor;
3. converts the ANSI file path to UTF-16 with:
   ```text
   MultiByteToWideChar
   ```
4. calls the graph-builder method at vtable `+0x34`;
5. restores the arrow cursor.

That method is:

```text
IGraphBuilder::RenderFile
```

for the DirectShow interface ABI.

---

# 33. `RenderFile` consequence

`RenderFile` asks the Filter Graph Manager to build the playback graph for the
specified movie.

For MPEG files this means Windows' installed DirectShow components provide the
appropriate:

```text
source filter
MPEG parser/splitter
video decoder
audio decoder
audio renderer
```

while Runtime's pre-added custom filter can satisfy the video-renderer role.

Therefore:

> The codec bitstream decoder is not implemented in Runtime's game code.

This is the key distinction when mapping the original pipeline to FFmpeg.

---

# 34. DirectShow dependency implications

Retail playback quality/compatibility could vary with:

- installed DirectShow filters;
- Windows version;
- MPEG decoder implementation;
- graphics/display format;
- audio renderer/device.

The game code owns final video-surface presentation, but not every decode-stage
implementation detail.

A modern reimplementation must choose one deterministic decoder stack instead.

OpenNomad chooses FFmpeg.

---

# 35. IMediaControl Run — `0x0048F6F0`

Runtime queries:

```text
IID_IMediaControl
```

then calls the method corresponding to:

```text
Run()
```

and marks:

```text
0x00660B7C = 1
```

as movie/graph running state.

Recommended:

```c
bool g_movieGraphRunning;
```

---

# 36. IMediaControl Stop — `0x0048F730`

Parallel helper:

```text
QueryInterface(IID_IMediaControl)
    ->
Stop()
```

then:

```text
0x00660B7C = 0
```

This is the ordinary graph-stop path.

---

# 37. Pause/resume helper — `0x0048F9F0`

This helper queries:

```text
IID_IMediaControl
```

and chooses:

```text
argument != 0:
    Pause()

argument == 0:
    Run()
```

It is exposed through the wrapper around:

```text
0x0043B7B0
```

which also stores movie pause/state information at:

```text
0x00907B64
```

---

# 38. Media-event polling — `0x0048F770`

Runtime queries:

```text
IID_IMediaEvent
```

then calls the non-blocking:

```text
GetEvent(..., timeout = 0)
```

path.

Playback is stopped for terminal event codes including:

```text
1
2
3
```

corresponding to the ordinary DirectShow completion/user-abort/error-abort
family.

The exact enum names should be tied to the DirectShow header version if a
Ghidra header is created.

---

# 39. Synchronous movie message loop — `0x0048F7D0`

Movie playback has its own message/event loop.

It runs **before** the ordinary game loop at:

```text
0x00439310
```

The movie loop:

- waits on DirectShow/media and Windows message activity;
- pumps/distributes Windows messages;
- polls DirectShow terminal events;
- invokes the supplied Omikron callback while playback remains active.

Runtime therefore remains responsive to:

```text
window messages
input/skip
DirectShow completion/error
```

during synchronous startup video playback.

---

# 40. `MsgWaitForMultipleObjects`

The movie loop uses a Win32 wait capable of observing:

```text
DirectShow event handle
+
window message queue
```

rather than continuously spinning.

This is a period-appropriate synchronous playback architecture.

OpenNomad's current loop instead decodes/paces frames explicitly and pumps SDL
events through its `should_stop` callback.

---

# 41. Movie active/pause global — `0x00907B64`

The synchronous loop checks:

```text
0x00907B64
```

and can exit/alter behavior when it becomes nonzero.

The wrapper around:

```text
0x0043B7B0
```

stores the requested pause state there and forwards it to
`IMediaControl::Pause`/`Run`.

Recommended:

```text
g_moviePausedOrBlocked
```

until all window-message setters are fully classified.

---

# 42. COM lifetime

Top-level playback:

```text
0x0048F8E0
```

performs:

```text
CoInitialize(NULL)

build graph
create movie surface
Run graph
movie event/message loop
Stop graph
release movie resources

CoUninitialize()
```

The DirectShow graph is therefore scoped to one movie playback, not retained
for all three startup files.

Movie scratch memory is retained across the sequence, but the COM playback
graph is rebuilt per file.

---

# 43. Alt+Tab hotkey registration

`0x0048F8E0` calls the imported Win32 hotkey-registration function with:

```text
window = NULL
id     = 100
modifier = 1
key      = 9
```

which corresponds to an:

```text
Alt + Tab
```

combination.

This is direct behavior.

The precise intended policy is likely to control Alt+Tab while the movie owns
fullscreen presentation, but the full registration/unregistration lifecycle
requires additional tracing before assigning a stronger semantic statement.

---

# 44. Custom movie surface

After `RenderFile` has negotiated the custom filter's media type, Runtime reads
decoded dimensions from the filter at:

```text
+0x758  width
+0x75C  height
```

and creates a DirectDraw surface with exactly those dimensions.

Global:

```text
0x00907B84
```

holds this decoded-video surface.

Recommended:

```c
IDirectDrawSurface4 *g_movieSurface;
```

---

# 45. Decoded dimensions are filter state

The custom renderer object owns:

```text
width
height
```

derived during media-type negotiation.

This means Runtime does not hard-code:

```text
320×240
```

inside the generic DirectShow player.

The actual startup assets may have that geometry, but the renderer is built to
receive arbitrary negotiated dimensions within its scratch/resource limits.

---

# 46. Custom video sample callback — `0x004B46B0`

The most important custom-renderer method begins around:

```text
0x004B46B0
```

It is called for each decoded DirectShow video sample.

High-level behavior:

```text
get IMediaSample data pointer

lock movie DirectDraw surface

read decoded width/height

copy/convert 16-bit source pixels
    into CPU staging buffer

copy staging rows
    into locked movie surface

unlock movie surface

Blt movie surface
    into game back buffer

present back buffer
    via Blt or Flip
```

This is the heart of the original FMV presentation pipeline.

---

# 47. DirectShow gives Runtime 16-bit RGB samples

The sample callback reads source pixels as:

```text
uint16_t
```

and processes:

```text
width × height
```

words.

There is no MPEG YUV inverse transform or entropy decoder here.

By the time the sample reaches Quantic's custom renderer it is already decoded
into a 16-bit RGB-like format negotiated by DirectShow.

This establishes an important division:

```text
DirectShow decoder:
    MPEG/YUV -> RGB sample

Quantic Runtime:
    RGB16 format adaptation + DirectDraw presentation
```

---

# 48. Vertical row handling

The sample-copy code starts from an end-relative source position and walks rows
in the opposite direction while producing the staging image.

This is consistent with the Windows bitmap/video convention where RGB media
can use bottom-up scanline ordering.

OpenNomad independently flips FFmpeg's top-down RGBA result to match its own
texture-upload convention.

Do not confuse the two flips:

- Runtime is adapting negotiated DirectShow RGB sample row orientation;
- OpenNomad is adapting FFmpeg output to OpenNomad texture-coordinate
  convention.

---

# 49. Optional 16-bit conversion LUT

Global:

```text
0x00907A40
```

controls whether each source 16-bit pixel is remapped.

When disabled:

```text
destinationPixel = sourcePixel
```

When enabled:

```text
destinationPixel =
    g_moviePixelConversionLut[sourcePixel]
```

This is why Runtime allocates all:

```text
65536
```

possible `uint16_t` LUT entries.

---

# 50. Conversion-control argument

Startup passes:

```text
low byte of 0x0090E724
```

to the generic movie wrapper.

That argument determines whether the 16-bit conversion table is constructed
and used.

The field's broader original graphics/configuration name remains unresolved.

Use:

```text
movie pixel conversion enabled
```

only as a local semantic description.

---

# 51. Destination pixel-format query

When conversion is requested, `0x0043B4E0` queries the game presentation/back
surface's:

```text
DDSURFACEDESC2
```

and extracts its RGB masks.

It then compares the destination channel widths with the reference/source
channel widths recovered by the early display probe.

The code computes shifts required to move channel values between those layouts.

---

# 52. 65536-entry conversion construction

For every possible source 16-bit pixel:

```text
0x0000 .. 0xFFFF
```

Runtime:

1. masks source R/G/B;
2. shifts components into normalized/appropriate bit positions;
3. accounts for channel precision differences;
4. positions the components according to destination RGB masks;
5. writes one converted `uint16_t` value to:
   ```text
   g_moviePixelConversionLut[pixel]
   ```

The per-frame sample path then becomes a single indexed lookup per pixel.

---

# 53. What Runtime's game code does **not** do

The game-side movie code does not contain a visible:

```text
Y'CbCr -> RGB
BT.601 matrix
BT.709 matrix
limited-range expansion
MPEG decoder
```

for startup video.

Those operations belong to the system DirectShow decoder/filter graph.

The explicit Runtime colour operation is:

```text
16-bit RGB mask/precision remapping
```

to the selected DirectDraw surface format.

---

# 54. Implication for FFmpeg mapping

A modern replacement must reconstruct both logical halves:

```text
1. MPEG decode and YUV -> display RGB
2. final game presentation without accidental second gamma transfer
```

FFmpeg naturally handles the first.

OpenGL/SDL presentation must avoid altering the already display-referred RGB
codes incorrectly in the second.

---

# 55. Current OpenNomad YUV->RGB choice

Current `VideoPlayer` configures `libswscale` explicitly with:

```text
SWS_CS_ITU601
source range = limited
destination range = full
brightness = neutral
contrast   = neutral
saturation = neutral
```

and converts to:

```text
AV_PIX_FMT_RGBA
```

This is a sensible modern approximation for standard-definition MPEG video.

However:

> It is **not** a matrix recovered from Omikron's Runtime code.

It is an explicit choice replacing whatever system DirectShow MPEG decoder was
used on the original Windows installation.

---

# 56. Color metadata policy

A stronger long-term FFmpeg policy should prefer:

```text
stream/frame color metadata
```

when trustworthy, with an Omikron-compatible SD MPEG fallback such as BT.601
when metadata is absent.

The retail assets should be measured/inventoried before making one hard-coded
matrix a universal game-format rule.

---

# 57. Current gamma fix: framebuffer sRGB

OpenNomad's general renderer enables:

```text
GL_FRAMEBUFFER_SRGB
```

for ordinary linear-light 3D rendering.

`VideoScene` temporarily disables it while blitting decoded movie RGB.

That is architecturally appropriate.

FFmpeg's RGBA values are already display-referred code values.

Passing them through an additional framebuffer sRGB encoding operation can
brighten/distort the movie.

---

# 58. Runtime presentation is display-referred

Original DirectDraw movie presentation performs essentially:

```text
decoded RGB16 code value
    ->
possible RGB16 mask remap
    ->
DirectDraw surface
    ->
Blt/Flip
```

There is no evidence that the game linearizes the movie into a lighting space
and later re-encodes it.

Thus:

```text
plain display-referred blit
```

is the right conceptual modern model.

---

# 59. Runtime 15/16-bit quantization

The original movie ultimately targets a native 15/16-bit DirectDraw surface
configuration.

OpenNomad converts to:

```text
RGBA8
```

and displays on a modern higher-precision framebuffer.

This intentionally removes original output quantization.

For ordinary use this is desirable.

A strict pixel-reference/debug mode could optionally emulate the original
channel masks/quantization after FFmpeg colour conversion.

---

# 60. Movie surface -> game back buffer

After copying the decoded sample to:

```text
g_movieSurface
```

Runtime calls the game back-buffer surface's:

```text
Blt
```

using:

```text
source = g_movieSurface
flags  = DDBLT_WAIT
```

with null source/destination rectangles in the first movie-to-backbuffer blit.

The key consequence is that movie presentation uses ordinary DirectDraw
surface scaling/copy semantics rather than custom polygon rendering.

---

# 61. Back-buffer identity

Global:

```text
0x00907B74
```

is the movie wrapper's stored game presentation surface.

Its use as:

- destination of the movie-surface `Blt`;
- source for the final windowed primary-surface `Blt`;
- explicit surface argument to fullscreen `Flip`;

strongly identifies it as the movie/game back-buffer-like surface.

Recommended working name:

```text
g_movieBackBuffer
```

with original ownership residing in the wider renderer/window subsystem.

---

# 62. Primary/front surface identity

Global:

```text
0x00907B7C
```

is used for final presentation.

Windowed:

```text
primary/front.Blt(clientDestination, backBuffer, ...)
```

Fullscreen:

```text
primary/front.Flip(backBuffer, ...)
```

This makes a primary/front-surface interpretation strong.

Recommended:

```text
g_moviePrimarySurface
```

---

# 63. Fullscreen presentation

When:

```text
windowed == false
```

the custom renderer presents by calling:

```text
primary/front surface -> Flip(backBuffer, DDFLIP_WAIT)
```

This is a normal DirectDraw page-flip path.

The movie renderer is therefore integrated into the game's fullscreen surface
chain.

---

# 64. Windowed presentation

When:

```text
windowed != false
```

Runtime:

1. calls `GetClientRect(gameWindow)`;
2. converts the client coordinates to screen coordinates;
3. calls:
   ```text
   primary/front.Blt(destinationClientRect, backBuffer, ...)
   ```

This is necessary because primary DirectDraw surface coordinates in windowed
mode are screen-space.

---

# 65. Output fill/stretch behavior

The custom renderer first blits:

```text
decoded-size movie surface
```

onto:

```text
game back buffer
```

with no recovered contain-fit calculation in the custom video callback.

The wrapper explicitly establishes:

```text
640 × 480
```

movie/game presentation dimensions.

Current evidence therefore strongly supports:

```text
decoded video is scaled to fill the game back-buffer presentation area
```

rather than:

```text
preserve source aspect by adding new pillarbox/letterbox bars
```

inside the Runtime movie layer.

---

# 66. OpenNomad contain-fit is not Runtime-confirmed

Current `VideoScene::compute_contain_scale()` preserves decoded-frame aspect
ratio and adds black space whenever viewport aspect differs.

That is a modern presentation policy.

The Runtime path recovered above does not contain an equivalent aspect-fit
calculation.

Therefore current OpenNomad:

```text
contain-fit
```

should not be documented as original movie behavior.

---

# 67. Widescreen policy

On the 1999 target, the game presentation is fundamentally:

```text
640×480 / 4:3
```

A modern widescreen implementation has a design choice.

For fidelity, a strong policy is:

```text
first reproduce Runtime's 640×480 movie composition
then scale that 4:3 output to the modern viewport
```

rather than independently fitting the raw MPEG to the host screen.

This preserves the original relationship between:

```text
movie frame
game backbuffer
```

while still allowing the overall game canvas to letterbox on widescreen.

---

# 68. `GAME.MPG` baked letterbox bars

Retail `GAME.MPG` is:

```text
320 × 240
```

and observed decoding shows approximately:

```text
34 black rows at top
34 black rows at bottom
```

around the active intro picture.

Current OpenNomad crops:

```text
x      = 0
y      = 34
width  = 320
height = 172
```

before RGBA conversion.

---

# 69. Critical crop provenance

No corresponding fixed crop has been recovered in Runtime's DirectShow custom
renderer.

The sample callback copies the negotiated:

```text
width × height
```

frame.

No direct:

```text
skip 34 top rows
reduce height by 68
```

branch is visible in the recovered presentation path.

Therefore:

```text
GAME.MPG 34px crop
```

is currently:

```text
OpenNomad-only presentation policy
```

not Runtime-confirmed behavior.

---

# 70. Why the crop can still be useful

The modern crop was introduced because the active image looks better when the
baked-in bars are removed before modern high-resolution presentation.

That can be a legitimate modernization.

But documentation/code should say:

```text
retail-asset-specific presentation adjustment
```

rather than:

```text
recovered original crop
```

until evidence appears.

---

# 71. Possible reason retail did not look “double letterboxed”

Runtime fills a 4:3 game surface with a 4:3 MPEG.

The baked bars are therefore simply part of the authored intro frame.

It does not add an additional 4:3-preservation layer inside the 640×480
surface.

A modern contain-fit presentation of the cropped `320×172` active image can
produce a different composition from both:

```text
raw retail MPEG
```

and:

```text
original Runtime full-frame stretch
```

This should be evaluated deliberately with retail captures.

---

# 72. Recommended crop policy separation

Represent crop as explicit per-title/asset presentation metadata:

```cpp
VideoPresentationPolicy {
    optional source crop;
    game-canvas scaling mode;
}
```

Then document:

```text
Runtime reference:
    full negotiated frame

OpenNomad enhanced:
    GAME.MPG crop 320×172+0+34
```

This permits both exact-reference and improved presentation modes later.

---

# 73. DirectShow audio ownership

Runtime never manually pulls/decompresses MPEG audio samples in the movie code
recovered here.

`IGraphBuilder::RenderFile` builds the whole playback graph.

Since Omikron supplies only a custom **video** renderer, the graph can choose
the normal DirectShow audio decoder/renderer chain.

Thus movie audio is architecturally separate from:

```text
TRACKS/*.ADP music
SCX WAVE effects
```

---

# 74. DirectShow synchronization

The Filter Graph Manager coordinates graph streaming and uses DirectShow's
reference-clock architecture to keep streams synchronized.

Runtime starts/stops/pauses the graph through:

```text
IMediaControl
```

and observes completion through:

```text
IMediaEvent
```

The game does not manually schedule each decoded video frame from MPEG PTS in
the custom renderer callback.

---

# 75. OpenNomad movie audio

Current OpenNomad:

```text
FFmpeg decodes audio
    ->
libswresample
    ->
float stereo
    ->
SDL3 audio stream
```

Audio stream setup is independent from the normal `AudioSystem` ADP/SFX mixer.

That separation correctly reflects the original architectural distinction,
even though the backend differs.

---

# 76. OpenNomad playback clock

Current `VideoPlayer::clock_seconds()` uses:

```text
external wall clock
anchored to the first video frame's PTS
```

rather than the DirectShow graph reference clock.

This is an implementation choice designed to avoid a single-threaded
decode/present loop stalling while waiting on audio it cannot feed.

It should not be presented as Runtime timing behavior.

---

# 77. Stale current header comment

At the time of this documentation pass, `VideoPlayer.hpp` still contains a
class-level comment saying approximately:

```text
the audio stream is the playback master clock;
wall clock is used when no audio
```

but the actual `clock_seconds()` implementation and its method comment use an
external wall clock.

The implementation/documentation should be reconciled.

This is not an RE issue, but it is worth fixing to avoid future confusion.

---

# 78. OpenNomad frame pacing

Current startup sequence:

```text
decode one frame
calculate:
    delay = frame.pts - externalClock

if delay > 0:
    wait in 5ms chunks
    pump stop/skip input

present frame
```

This explicitly reproduces real-time playback pacing.

Original Runtime leaves media sample scheduling to DirectShow's graph/clock.

---

# 79. Decode-error policy

Runtime's broader startup behavior treats FMV support as optional:

```text
failed compatibility probe -> no FMVs
```

Current OpenNomad similarly treats missing/undecodable startup movies as:

```text
SkippedUnavailable
```

rather than fatal startup errors.

This is a good semantic match.

---

# 80. Window deactivation during FMV

Runtime window/message handling checks whether the movie system is active.

Recovered behavior indicates different paths depending on presentation mode.

In windowed mode, an activation-related path can request:

```text
movie pause
```

through the `0x0043B7B0 -> IMediaControl::Pause` path.

In fullscreen, a corresponding path can:

```text
abort/close movie
set persistent startup movie skip state
```

The exact originating Win32 message/case should be fully mapped before turning
this into a broad “all focus loss does X” rule.

---

# 81. Why activation deserves separate testing

DirectDraw fullscreen surface loss and ordinary window focus loss were
materially different concerns on the original platform.

Modern SDL does not need to mimic DirectDraw surface-loss mechanics.

It should reproduce user-visible policy:

```text
what happens to startup video
when the app loses focus / is minimized / resumes
```

after the exact Runtime window-message path is fully classified.

---

# 82. Movie system active flag

Wrapper/helper around:

```text
0x0043B7A0
```

reports movie-system state associated with:

```text
0x00907B80
```

Other wrappers pause or destroy the current movie.

Recommended grouping:

```text
Movie_IsActive
Movie_SetPaused
Movie_Abort
```

as reconstructed labels, not original symbols.

---

# 83. Custom renderer object layout

The custom DirectShow filter is:

```text
0x778 bytes
```

Important recovered tail fields include:

```text
+0x758 decoded width
+0x75C decoded height
+0x760..+0x770 renderer/media flags/modes
```

The tail-mode fields are configured by helper:

```text
0x004B4610
```

from a bitfield value.

Their exact author/source-level semantics remain unresolved.

---

# 84. DirectShow renderer class implementation

The custom object has its own vtables and COM/filter implementation around:

```text
0x004B41xx..0x004B65xx
```

This region appears to implement the DirectShow renderer/filter interface
machinery needed for:

- media-type negotiation;
- pins;
- sample delivery;
- filter state;
- allocator/sample handling;
- frame renderer callback.

A dedicated DirectShow-filter ABI document is unnecessary unless deeper
playback bugs require it.

For `fmv.md`, the important boundary is the delivered sample at `0x004B46B0`.

---

# 85. Movie frame copy safety assumptions

Runtime assumes the decoded frame dimensions and its fixed staging allocation
are compatible.

Modern OpenNomad should keep explicit bounds validation around:

```text
decoded width
decoded height
crop
RGBA allocation
```

even when retail files are trusted.

Memory-safety parity is not a fidelity requirement.

---

# 86. OpenNomad fixed crop validation

Current `VideoPlayer` validates:

```text
crop origin >= 0
crop width/height > 0
crop contained within decoded frame
```

then applies the crop to the FFmpeg frame before swscale conversion.

This is good modern safety behavior.

It should remain separate from claims about Runtime's custom renderer.

---

# 87. Crop before colorspace conversion

OpenNomad currently applies:

```text
av_frame_apply_cropping
```

before:

```text
sws_scale
```

Thus only the active selected source rectangle is converted to RGBA.

For the fixed `GAME.MPG` modernization this avoids wasting conversion on black
bars.

Again, the operation is OpenNomad-specific.

---

# 88. FFmpeg source dimensions

`configure_video_output()` begins with:

```text
outputWidth  = decoder width
outputHeight = decoder height
```

and changes them only when a crop option is supplied.

There is no generic dynamic `cropdetect`.

This avoids a startup flash caused by detecting bars only after several frames
have already been shown.

---

# 89. Why no runtime `cropdetect`

Runtime has no concept analogous to FFmpeg's content-analysis `cropdetect`
filter in the recovered path.

It receives the negotiated frame and presents it.

Therefore a deterministic fixed asset rule is preferable to runtime
content-detection if OpenNomad chooses to modernize `GAME.MPG`.

---

# 90. Current OpenNomad texture transfer

Decoded movie frames are uploaded as:

```text
RGBA8
```

to a normal 2D GL texture.

The texture is created with:

```text
sRGB storage disabled
```

for the video path.

Then `GL_FRAMEBUFFER_SRGB` is disabled while rendering it.

This produces a plain code-value blit rather than the 3D renderer's normal
linear/sRGB pipeline.

---

# 91. OpenNomad fullscreen quad

`VideoScene` uses an unlit textured quad with:

```text
no depth test
no culling
no tint
no lighting
```

and a black framebuffer clear.

This is an appropriate modern equivalent to a DirectDraw surface blit.

No 3D world renderer should be involved in FMV presentation.

---

# 92. Current contain scale

The presenter currently calculates:

```text
frameAspect
viewportAspect
```

and scales the quad so the complete frame is visible.

As established above, this is:

```text
OpenNomad presentation behavior
```

rather than a recovered equivalent of Runtime's 640×480 back-buffer fill.

This should be reconsidered if exact retail composition is the target.

---

# 93. Recommended modern presentation model

For the closest architectural mapping:

```text
FFmpeg decoded frame
    |
    +-- optional enhancement crop policy
    |
    v
convert to display-referred RGB
    |
    v
compose into a logical 640×480 Runtime movie canvas
    |
    v
scale the completed 4:3 canvas to host viewport
```

This mirrors:

```text
decoded DirectShow movie surface
    ->
640×480 DirectDraw game backbuffer
    ->
actual display
```

more closely than fitting the raw frame directly to the host viewport.

---

# 94. Reference versus enhanced modes

A future debug option could make the distinction explicit.

Reference:

```text
no GAME crop
Runtime-style fill into 640×480
optional 16-bit quantization
```

Enhanced:

```text
fixed GAME crop
preserve active-picture aspect as desired
modern high-precision output
```

This would let screenshots be compared against retail without sacrificing an
improved normal presentation mode.

---

# 95. Pixel conversion strict-reference option

A strict presentation path could accept:

```text
source RGB8
destination simulated mask:
    R mask
    G mask
    B mask
```

then:

```text
quantize to original channel precision
re-expand to RGB8 for modern display
```

This would reproduce original 15/16-bit color loss without actually requiring
a 16-bit host framebuffer.

It is optional and should be driven by visual-reference needs.

---

# 96. Do not emulate arbitrary historical decoder bugs by default

Because DirectShow selected system filters, exact decoder rounding/color
behavior could vary between machines.

OpenNomad should target:

```text
correct retail image appearance
```

rather than an unspecified bug in one 1999 MPEG decoder.

Runtime's **own** presentation transformations should be reproduced where
known.

---

# 97. Movie audio is not subject to ADP rules

Do not apply:

```text
22050 Hz QD ADP assumptions
IMA nibble decoder
TRACKS numeric IDs
music loop mode
```

to FMV audio.

DirectShow owns the original movie's audio media graph.

FFmpeg owns it in OpenNomad.

---

# 98. Movie skip is not Escape game-command dispatch

During normal gameplay, held Escape goes through the ordinary frame-loop game
command path documented in `runtime-main-loop.md`.

During startup movie playback:

```text
ordinary game loop has not started
```

and `0x00439730` polls input directly to stop the movie.

These are separate contexts even if the physical Escape key is involved.

---

# 99. Missing-movie behavior

The direct Runtime failure semantics of every `RenderFile` failure case still
need a complete top-level return-value audit.

However the startup architecture already demonstrates that:

```text
FMV capability can fail without aborting the game
```

and OpenNomad's:

```text
missing/undecodable -> skipped unavailable
```

policy is consistent with that robust intent.

---

# 100. Movie callback timing

The callback is invoked by the movie loop after Windows-message processing
while the DirectShow graph remains active.

It is not tied to one callback per decoded frame in the same explicit way as
OpenNomad's current `should_stop()` checks.

Therefore do not use callback frequency as a recovered movie frame rate.

---

# 101. FMV frame rate

Frame rate belongs to each MPEG stream and DirectShow's negotiated media
timing.

Runtime does not convert movie playback into the game's:

```text
30 Hz Omikron simulation delta
```

The video graph has its own media timing.

OpenNomad correctly uses decoded PTS/media time rather than script ticks.

---

# 102. Movie playback versus game frame timing

Startup FMV occurs before the ordinary timed-frame function:

```text
0x0041F740
```

is running in its normal long-lived loop.

Therefore:

```text
g_frameTimeScale
g_scriptFrameDelta
```

are not the movie scheduler.

The movie graph/event loop owns its own progression.

---

# 103. DirectShow event handle global

Runtime stores the DirectShow media-event handle at:

```text
0x00660B78
```

Recommended:

```c
HANDLE g_movieEventHandle;
```

The Filter Graph Manager owns the underlying event lifetime.

The game uses it for waiting/polling during synchronous playback.

---

# 104. DirectShow graph globals

Recommended working labels:

```text
0x00660B70
    g_movieGraph
    IGraphBuilder*

0x00660B74
    g_movieRendererFilter
    custom filter/object

0x00660B78
    g_movieEventHandle

0x00660B7C
    g_movieGraphRunning

0x00660B80
    custom movie/filter state, unresolved
```

These are distinct from the outer wrapper globals under `0x00907Axx/0x00907Bxx`.

---

# 105. Outer movie presentation globals

Recommended map:

```text
0x00907A30  movie output width = 640
0x00907A34  movie output height = 480

0x00907A38  game HWND
0x00907A3C  IDirectDraw4-like object
0x00907A40  16-bit pixel conversion enabled

0x00907B64  pause/block/abort state
0x00907B68  surface-lock/reentrancy state
0x00907B6C  callback enabled
0x00907B70  callback pointer

0x00907B74  game/backbuffer-like surface
0x00907B78  windowed mode
0x00907B7C  primary/front-surface-like object
0x00907B80  movie system active/initialized
0x00907B84  decoded-size movie DirectDraw surface
```

Names containing `-like` remain reconstructed, though surface behavior is
strong.

---

# 106. Scratch/color globals

```text
0x0052DDC0
    65536-entry uint16 conversion LUT

0x0052DDC4
    movie frame staging allocation

0x0052DDB0
0x0052DDAC
0x0052DDA8
    reference/source RGB masks

0x0052DDBC
0x0052DDB8
0x0052DDB4
    reference/source RGB mask bit counts

0x0052DD54
    persistent startup movie skip/state
```

Destination-mask temporary values are built on the movie wrapper's stack.

---

# 107. Recommended Ghidra labels

High-confidence reconstructed labels:

```text
0043B300  Movie_ProbeDirectDrawPixelFormat
0043B4A0  Movie_AllocateScratch
0043B4E0  Movie_Play
0043B7A0  Movie_IsActive
0043B7B0  Movie_SetPaused
0043B7D0  Movie_Abort
0043B7F0  Movie_FreeScratch

00439730  StartupMovie_InputCallback

0048F520  DirectShow_CreateGraphAndRenderer
0048F600  DirectShow_SetupMediaEvent
0048F650  DirectShow_InitializeGraph
0048F660  DirectShow_RenderFile
0048F6F0  DirectShow_Run
0048F730  DirectShow_Stop
0048F770  DirectShow_PollEvent
0048F7D0  DirectShow_PlaybackLoop
0048F8A0  DirectShow_ReleaseMovieSurface
0048F8E0  DirectShow_PlayMovie
0048F9F0  DirectShow_SetPaused

004B46B0  MovieRenderer_RenderSample
```

These are working names, not recovered PDB symbols.

---

# 108. DirectShow GUID reference

```text
004BEAC8
    CLSID_FilterGraph
    E436EBB3-524F-11CE-9F53-0020AF0BA770

004BEAD8
    IID_IGraphBuilder
    56A868A9-0AD4-11CE-B03A-0020AF0BA770

004BEAE8
    IID_IMediaEvent
    56A868B6-0AD4-11CE-B03A-0020AF0BA770

004BEAF8
    IID_IMediaControl
    56A868B1-0AD4-11CE-B03A-0020AF0BA770

004BD358
    IID_IDirectDraw4
    9C59509A-39BD-11D1-8C4A-00C04FD930C5
```

These GUID identities are a strong anchor for typing the COM calls.

---

# 109. Current OpenNomad implementation map

```text
Runtime DirectShow graph
    ->
VideoPlayer / FFmpeg contexts

DirectShow system video decoder
    ->
libavcodec video decoder

DirectShow RGB conversion
    ->
libswscale YUV -> RGBA8

DirectShow audio graph
    ->
libavcodec + libswresample + SDL audio stream

custom DirectShow renderer
    ->
VideoScene

DirectDraw movie surface
    ->
OpenGL Texture2D

DirectDraw Blt/Flip
    ->
fullscreen quad + SDL_GL_SwapWindow

movie Win32 message loop
    ->
StartupVideoSequence + Application::poll_events()

startup input callback
    ->
InputManager k_skip_video
```

This is a strong modern conceptual mapping.

---

# 110. Current OpenNomad differences table

| Behavior | Runtime | Current OpenNomad |
|---|---|---|
| MPEG decoder | system DirectShow filters | FFmpeg |
| video sample delivered as | 16-bit RGB | decoded source -> RGBA8 |
| output color adaptation | 16-bit channel-mask LUT when needed | BT.601 limited->full + RGBA8 |
| final transfer | DirectDraw display-referred blit | GL blit with framebuffer sRGB disabled |
| decoded surface | DirectDraw decoded-size surface | RGBA CPU frame / GL texture |
| game movie canvas | 640×480 | direct viewport contain-fit |
| aspect policy | fills game backbuffer | preserves raw/cropped frame aspect |
| GAME crop | no recovered crop | fixed 34px top/bottom crop |
| movie clock | DirectShow graph/reference clock | external wall clock |
| audio | DirectShow graph/audio renderer | FFmpeg + SDL |
| skip | can persist across remaining startup movies | currently current-slot edge behavior |
| unavailable movie | FMV subsystem can be bypassed | skipped, nonfatal |

---

# 111. Highest-priority OpenNomad correction: persistent skip

The clearest behavioral mismatch is the startup-sequence skip state.

Recommended change:

```text
StartupVideoSequence owns:
    bool skip_remaining_movies

when Runtime-equivalent skip action is asserted:
    abort current slot
    set skip_remaining_movies = true

later slots:
    do not open/decode
    report skipped-by-user
```

The exact distinction between temporary movie abort and persistent sequence
skip can be refined after the two low-level input outputs from `0x00439730`
are fully named.

---

# 112. High-priority presentation review: 640×480 canvas

Current contain-fit code should be revisited against retail captures.

Runtime evidence suggests:

```text
decoded frame
    ->
fill 640×480 backbuffer
```

A closer modern model is:

```text
compose Runtime 4:3 canvas first
    ->
then fit that canvas to host viewport
```

This also keeps all startup presentation conceptually aligned with the game's
native 640×480 UI space.

---

# 113. High-priority crop provenance fix

Even if the fixed `GAME.MPG` crop remains enabled, code comments/tests/docs
should state:

```text
OpenNomad enhancement
```

rather than implying:

```text
Runtime crops the encoded bars
```

The current test:

```text
"only the game intro has a fixed letterbox crop"
```

tests OpenNomad policy, not an original binary rule.

A naming change such as:

```text
enhanced_startup_video_options
```

or a policy enum could make this distinction impossible to miss.

---

# 114. High-priority clock comment cleanup

`VideoPlayer.hpp` and implementation currently disagree about clock ownership.

The code's actual behavior:

```text
external wall clock for playback pacing
```

should be the documented current behavior.

If audio-master synchronization is desired later, it should be implemented
explicitly rather than retained only in a stale comment.

---

# 115. Recommended FFmpeg color policy

For current architecture:

```text
decode MPEG
    ->
choose source colorspace/range from reliable metadata
    ->
fallback to SD BT.601 limited range when absent
    ->
convert to full-range display RGB/RGBA
    ->
do not apply framebuffer sRGB encoding again
```

This is conceptually closest to:

```text
DirectShow decoder supplies display RGB
    ->
DirectDraw blit
```

without pretending Runtime itself contains the BT.601 matrix.

---

# 116. Recommended color-reference tests

Use known decoded frames and compare:

1. FFmpeg default automatic conversion;
2. explicit BT.601 limited->full;
3. external player with correct SD MPEG handling;
4. retail Runtime capture from the same machine/display mode if possible.

Check:

```text
near-black
skin tones
saturated red/blue
white logo levels
shadow detail
```

Avoid tuning gamma from one screenshot alone.

---

# 117. Recommended optional 16-bit reference test

Take an FFmpeg-converted RGB frame.

Quantize it according to a simulated original DirectDraw mask, for example:

```text
RGB565
```

then expand back to RGB8 and compare against:

```text
unquantized modern output
retail capture
```

This can determine whether original 16-bit display precision materially
explains remaining color differences.

Do not make RGB565 universal without reading the actual Runtime-selected masks
from the target capture/configuration.

---

# 118. Recommended presentation tests

Runtime-reference tests:

- [ ] startup order Eidos -> Quantic -> Game;
- [ ] `NOFMV` skips all three;
- [ ] failed FMV probe skips all three;
- [ ] persistent startup skip suppresses later slots;
- [ ] unavailable video is nonfatal;
- [ ] movie graph/resources are closed before `aventure.scx`;
- [ ] decoded movie is composed through a 640×480 reference canvas;
- [ ] reference mode does not invent a GAME crop;
- [ ] windowed/fullsreen presentation policy represented separately.

Modern enhancement tests:

- [ ] optional GAME crop is exactly `320×172+0+34`;
- [ ] crop applied before RGB conversion;
- [ ] no first-frame `cropdetect` flash;
- [ ] host widescreen preserves chosen game-canvas policy.

---

# 119. Recommended playback tests

- [ ] decoded PTS drives real-time presentation;
- [ ] skip/quit is polled while waiting for a future frame;
- [ ] audio absence still allows video playback;
- [ ] audio decode failure can fall back to video-only when desired;
- [ ] EOF flushes buffered decoder frames;
- [ ] movie resources release on skip/error/EOF;
- [ ] no normal game-frame timing state is required during startup video.

---

# 120. Recommended color/gamma tests

- [ ] movie GL texture is not treated as linear-light source data;
- [ ] `GL_FRAMEBUFFER_SRGB` is disabled for display-referred video blit;
- [ ] state is restored after presentation;
- [ ] YUV range expansion does not crush blacks or lift black bars;
- [ ] RGB channel order is correct;
- [ ] bottom-up/top-down row conversion is applied exactly once.

---

# 121. Recommended DirectShow/Ghidra typing

A Ghidra-safe historical header should include minimal declarations for:

```text
IGraphBuilder
IMediaControl
IMediaEvent
IDirectDraw4
IDirectDrawSurface4
DDSURFACEDESC2
DDPIXELFORMAT
```

and the five GUIDs listed above.

This will turn many opaque:

```text
call [eax + constant]
```

operations into meaningful interface methods.

Avoid importing a modern DirectShow/DirectDraw header wholesale if it makes
Ghidra parsing/packing unreliable.

---

# 122. DirectShow references

Useful Microsoft references:

```text
Filter Graph Manager
https://learn.microsoft.com/en-us/windows/win32/directshow/filter-graph-manager

Overview of Graph Building
https://learn.microsoft.com/en-us/windows/win32/directshow/overview-of-graph-building

IGraphBuilder::RenderFile
https://learn.microsoft.com/en-us/windows/win32/api/strmif/nf-strmif-igraphbuilder-renderfile

Learning When an Event Occurs
https://learn.microsoft.com/en-us/windows/win32/directshow/learning-when-an-event-occurs
```

Modern Microsoft documentation marks DirectShow as a legacy API, but the
interface architecture remains the relevant historical reference for
`Runtime.exe`.

---

# 123. High-value remaining RE: custom media type

Trace the custom filter's connection/media-type negotiation to identify
exactly:

```text
accepted video subtype
bit count
RGB masks
stride/orientation
VIDEOINFOHEADER / VIDEOINFOHEADER2 layout
```

This would tell us whether Quantic explicitly requests:

```text
RGB555
RGB565
```

or accepts whichever 16-bit RGB format the graph offers.

It would also settle the precise source-mask interpretation.

---

# 124. High-value remaining RE: crop/source rectangle

Inspect the negotiated:

```text
VIDEOINFOHEADER rcSource
rcTarget
BITMAPINFOHEADER
```

handling in the custom filter.

No crop is visible in the sample renderer itself.

If Runtime ever removes `GAME.MPG` bars, media-type source rectangles are the
most plausible remaining place for that behavior to live.

Until that is proven:

```text
native GAME crop = unconfirmed
```

---

# 125. High-value remaining RE: exact startup input outputs

Fully type the two outputs passed to:

```text
0x0043E0D0
```

by `0x00439730`.

We currently know:

```text
one nonzero value aborts current playback
one value == 2 persists skip state
```

Recovering their source-level roles will tell us whether Runtime distinguishes:

```text
temporary abort
explicit skip-all
focus/device abort
mouse/keyboard classes
```

or another policy.

---

# 126. High-value remaining RE: activation messages

Map the exact WindowProc cases that invoke:

```text
Movie_SetPaused
Movie_Abort
```

and set:

```text
0x0052DD54
```

This is necessary before OpenNomad can reproduce retail Alt-Tab/minimize
behavior precisely.

---

# 127. High-value remaining RE: hotkey lifetime

Determine why movie playback calls the hotkey-registration API for:

```text
Alt+Tab
```

with ID:

```text
100
```

and establish when/how that registration is removed or superseded.

Do not assume permanent system hotkey capture from this one callsite without
following the complete import/wrapper lifecycle.

---

# 128. High-value remaining RE: movie audio graph

DirectShow owns audio, but graph inspection or deeper COM tracing could still
establish:

```text
MPEG audio subtype
decoder selected on reference Windows install
sample rate/channel layout
audio renderer
volume path
```

This is lower priority because FFmpeg can decode the authored audio
deterministically.

---

# 129. High-value remaining RE: pixel conversion mode field

Recover the wider meaning of:

```text
low byte [0x0090E724]
```

that controls movie 16-bit conversion.

It likely belongs to broader screen/pixel-format capability/configuration
state.

Mapping its setters may explain when Runtime expects source and target movie
surface formats to differ.

---

# 130. Compact startup reference

```text
00439470 startup lifetime

if [009103CD] == 0:
    0043B4A0 Movie_AllocateScratch

    if [0052DD54] == 0:
        0043B4E0("FLIS\\EIDOS.mpg", ...)

    if [0052DD54] == 0:
        0043B4E0("FLIS\\QUANTIC.mpg", ...)

    if [0052DD54] == 0:
        0043B4E0("FLIS\\GAME.mpg", ...)

    0043B7F0 Movie_FreeScratch
```

Then:

```text
aventure.scx
OMIKRON.BMP
ordinary main loop
```

---

# 131. Compact DirectShow reference

```text
0048F520
    CoCreateInstance(CLSID_FilterGraph, IID_IGraphBuilder)
    allocate/add custom renderer filter

0048F600
    IID_IMediaEvent
    event handle / completion handling

0048F660
    MultiByteToWideChar
    IGraphBuilder::RenderFile

0048F6F0
    IMediaControl::Run

0048F730
    IMediaControl::Stop

0048F770
    IMediaEvent::GetEvent

0048F7D0
    synchronous event/message loop

0048F9F0
    IMediaControl::Pause / Run
```

---

# 132. Compact video renderer reference

```text
custom filter:
    size 0x778

+758 decoded width
+75C decoded height

per-sample renderer:
    004B46B0
```

Flow:

```text
IMediaSample 16-bit RGB
    ->
optional 65536-entry LUT
    ->
0052DDC4 staging
    ->
00907B84 movie surface
    ->
00907B74 game backbuffer
    ->
00907B7C primary/front

windowed:
    Blt

fullscreen:
    Flip
```

---

# 133. Compact color reference

Runtime game-side color work:

```text
probe/reference RGB16 masks
destination RGB16 masks
count channel bits
build 65536-entry remap table
lookup every pixel if conversion enabled
DirectDraw blit
```

OpenNomad replacement:

```text
FFmpeg/libswscale:
    MPEG YUV -> RGBA8

current fallback:
    BT.601
    limited -> full

VideoScene:
    no framebuffer sRGB encode
    plain display RGB blit
```

---

# 134. Compact crop/aspect reference

Runtime-confirmed:

```text
movie wrapper output canvas:
    640 × 480

decoded movie surface:
    decoded width × decoded height

no explicit fixed GAME crop recovered

movie surface -> backbuffer:
    DirectDraw Blt
```

Current OpenNomad:

```text
GAME.MPG:
    crop 320×172+0+34

presentation:
    contain-fit raw/cropped frame to host viewport
```

Therefore both crop and contain-fit are currently modern policy.

---

# 135. Boundary of current knowledge

Strongly recovered:

```text
three startup filenames/order
NOFMV gate
failed display probe -> no FMV
persistent startup skip state
startup input callback
DirectShow Filter Graph Manager
IGraphBuilder / IMediaControl / IMediaEvent
custom Quantic video renderer filter
RenderFile graph construction
decoded 16-bit RGB sample path
DirectDraw movie surface
65536-entry RGB16 format LUT
windowed Blt versus fullscreen Flip
640×480 game movie canvas
movie graph per-file COM lifetime
```

Still incomplete:

```text
custom filter original class name
exact negotiated DirectShow video subtype
whether media-type source rectangles ever crop active picture
exact semantics of low byte 0x0090E724
exact activation/minimize policy per WindowProc message
Alt+Tab hotkey lifetime/intention
precise DirectShow audio filters on a reference machine
whether any Runtime path outside the recovered sample renderer modifies
GAME.MPG's authored bars
```

The central architectural takeaway is:

> Omikron delegates MPEG decoding and A/V synchronization to Windows
> DirectShow, but does not delegate final video presentation. Quantic inserts
> a custom DirectShow renderer that receives decoded 16-bit RGB samples,
> adapts them to the game's DirectDraw pixel format when necessary, and
> presents them through the same 640×480 DirectDraw surface chain used by the
> game. OpenNomad's clean modern equivalent is FFmpeg decoding into
> display-referred RGB plus a simple unlit video blit — but crop, aspect,
> color-matrix, clock, and skip policies must remain explicitly separated from
> what Runtime actually proves.
