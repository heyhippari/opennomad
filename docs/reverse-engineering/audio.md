# Runtime audio and music system

> **Status:** work-in-progress reverse-engineering documentation for OpenNomad  
> **Last updated:** 2026-08-22
>
> This document describes the Windows retail Runtime's currently recovered
> audio architecture and the corresponding OpenNomad compatibility model.
>
> Three distinct domains must remain separate:
>
> 1. **numbered music tracks** stored as `TRACKS/<id>.ADP`;
> 2. **scenario/SFX resources** stored as RIFF/WAVE payloads inside SCX
>    `DEAD0003`;
> 3. **voice/spatial playback state**, originally implemented over DirectSound
>    and associated software/native calculations.
>
> FMV audio belongs to the movie playback pipeline and should not be conflated
> with the ADP music decoder merely because both ultimately reach the output
> device.

Related documentation:

- [`scx.md`](scx.md) — `DEAD0003` sound descriptors and embedded WAVE payloads;
- [`iam-scenario-vm.md`](iam-scenario-vm.md) — AREA music opcode `0x67`;
- [`iam-script-functions.md`](iam-script-functions.md) — structured
  `PlaySound`/`StopSound`/related functions;
- [`runtime-coordinate-math.md`](runtime-coordinate-math.md) — native inches
  and the audio metres boundary;
- [`runtime-globals.md`](runtime-globals.md) — current music-track global;
- [`original-toolchain.md`](original-toolchain.md) — DirectSound/WINMM and the
  original WAV/ADP production vocabulary.

---

# 1. Evidence model

Source precedence:

1. **`Runtime.exe` behavior/imports/diagnostics**;
2. **retail ADP and SCX WAVE data**;
3. **SCX script handler behavior**;
4. **current OpenNomad implementation/tests**;
5. inferred mapping to SDL3_mixer.

Confidence labels:

- **Confirmed — Runtime**
- **Confirmed — data**
- **Corroborated**
- **Strongly reconstructed**
- **Provisional**
- **OpenNomad-only**

The spatialization section deliberately labels approximate modern formulas
where exact Runtime parity has not yet been achieved.

---

# 2. Original PC audio APIs

Runtime statically imports:

```text
DSOUND.dll
    DirectSoundCreate
    DirectSoundEnumerateA

WINMM.dll
    multimedia timer/mixer/mmio/mci families
```

This is a classic late-1990s DirectSound-based PC audio architecture.

DirectSound behavior must be inferred from:

- native calls/vtables;
- Runtime's software paths;
- resource/playback structures;
- audible retail behavior.

Modern SDL3_mixer implementation details are not historical evidence.

---

# 3. Runtime file vocabulary

The executable contains original file-selector descriptions:

```text
ADP Adpcm Sound File
*.ADP

WAV Sound File
*.WAV

SFX File
*.SFX
```

Runtime also contains paths/formats such as:

```text
TRACKS\%d.ADP
SOUNDS
i2d\sounds\%s.wav
```

This directly demonstrates multiple audio asset classes.

---

# 4. Audio domains

A useful recovered partition is:

```text
Music
    numbered TRACKS/*.ADP
    controlled from scenario VM

Scenario SFX
    named SCX DEAD0003 resources
    RIFF/WAVE payloads
    controlled by structured Script_* functions

SFX definition sound IDs are scenario-local DEAD0003 `ScxSoundRecord::h_id`
values, not indices into the sound table. OpenNomad resolves the first matching
hID, loads its parallel embedded WAVE through the normal sound-resource cache,
and submits a one-shot spatial request. Runtime-native SFX emitter distances
are minimum `78` inches and maximum `585` inches; conversion to metres remains
at the ScenarioRuntime audio boundary.

CTL animation audio markers (states with `animation_mode & 0x0008`) use the
same DEAD0003 **hID** namespace through Runtime's `0x0048CC80` lookup — never
a table index. The ordinary locomotion markers are one-shot per state
execution at an authored animation phase (footsteps); their spatial origin is
the live character position. No CTL-specific attenuation constants are
recovered for this path, so OpenNomad uses the general scenario spatial-sound
defaults rather than the SFX-specific 78/585 pair. See [ctl.md](ctl.md) §4.8.

I2D/UI sounds
    WAV-like named resources under I2D paths

Movie audio
    handled by movie/FMV subsystem
```

Do not force all of these through one original file-format model.

---

# 5. Numbered music controller

Compact scenario opcode:

```text
0x67
```

drives numbered music.

Runtime handler:

```text
0x00404FB0
```

The instruction consumes three 16-bit scenario values.

Current high-level model:

```text
trackId
operandB
operandC
```

Only the first operand's identity is completely firm.

---

# 6. Music path resolution

Runtime constructs numbered music paths equivalent to:

```text
TRACKS\<trackId>.ADP
```

Current OpenNomad uses:

```text
TRACKS/<trackId>.ADP
```

through its case-insensitive game-data resolver.

This is a platform-path spelling difference, not an asset-model difference.

---

# 7. Current music-track global

Runtime global:

```text
0x004C013C
```

stores numeric music-track state.

`0x00404FB0` compares a requested track against it before starting/restarting
music.

Recovered behavior:

```text
if requested numeric track == current active track:
    do not restart from the beginning
```

This is important for scenario scripts that may issue repeated music commands.

---

# 8. Opcode `0x67` example

AREA 118 startup contains:

```text
67 6D 00 01 00 01 00
```

giving:

```text
trackId   = 109
operandB  = 1
operandC  = 1
```

Retail observation confirms:

```text
109.ADP
```

is the main-menu music.

---

# 9. Music operand B

Current OpenNomad interprets:

```text
operandB != 0
```

as infinite looping.

This interpretation agrees with observed use and current recovered behavior,
but documentation should distinguish it from the track-ID fact.

Use:

```text
loop-like / looping flag
```

until every handler branch and retail callsite has been inventoried.

---

# 10. Music operand C

The third operand is preserved in OpenNomad as:

```text
mode_flag
```

Its exact original semantics remain unresolved.

Do **not** rename it speculatively to:

```text
fade
volume
priority
channel
```

without stronger Runtime evidence.

---

# 11. QD ADP container

Retail numbered music uses a compact Quantic Dream ADP container.

Recovered layout:

```c
struct QdAdpHeader {
    uint8_t payloadSize24[3]; // +0x00, little-endian u24
    uint8_t stereoFlag;       // +0x03, 0 mono / 1 stereo
    uint8_t reserved[12];     // +0x04..+0x0F, zero
}; // 0x10

uint8_t compressedPayload[payloadSize];
```

No RIFF/WAVE header wraps this stream.

---

# 12. ADP payload size

Header:

```text
+0x00..+0x02
```

stores a little-endian 24-bit compressed payload size:

```text
size =
    byte0
    | byte1 << 8
    | byte2 << 16
```

Total file size:

```text
0x10 + payloadSize
```

OpenNomad currently requires that this exactly matches the file size.

---

# 13. ADP channels

Header:

```text
+0x03
```

values:

```text
0 -> mono
1 -> stereo
```

No other flag value is accepted by the current recovered parser.

Channel count:

```text
channels =
    stereoFlag ? 2 : 1
```

---

# 14. ADP reserved bytes

Header:

```text
+0x04..+0x0F
```

is zero in the recovered format.

Current OpenNomad treats non-zero bytes as malformed.

A broader retail corpus should be retained as the final authority if an unusual
asset ever contradicts this.

---

# 15. ADP sample rate

Recovered QD ADP music sample rate:

```text
22050 Hz
```

This is fixed for the current decoder model.

The file does not carry a separate sample-rate field in its 16-byte header.

---

# 16. ADP frame count

Each compressed byte contains two 4-bit ADPCM codes.

Therefore:

```text
decoded samples total =
    payloadSize * 2

frames per channel =
    payloadSize * 2 / channels
```

For mono:

```text
one compressed byte -> two successive mono samples
```

For stereo:

```text
one compressed byte -> one stereo frame
```

---

# 17. QD IMA decoder state

Each channel begins with:

```text
predictor = 0
stepIndex = 0
```

and uses the standard 89-entry IMA step table plus 16-entry index adjustment
table.

Rewind restores:

```text
compressed read position = 0
decoded frame count = 0
predictor = 0
step index = 0
```

for every channel.

---

# 18. ADP nibble decode

For one 4-bit code:

```text
magnitude = nibble & 7
step      = stepTable[stepIndex]
```

Delta is assembled from the step contributions and then shifted:

```text
if magnitude bit 2: delta += step * 4
if magnitude bit 1: delta += step * 2
if magnitude bit 0: delta += step

delta >>= 2
```

If sign bit:

```text
nibble & 8
```

is set:

```text
delta = -delta
```

Predictor:

```text
predictor =
    clamp_s16(predictor + delta)
```

Step index is adjusted through the normal IMA index table and clamped:

```text
0..88
```

---

# 19. ADP nibble/channel order

Mono:

```text
byte high nibble -> next mono sample
byte low nibble  -> following mono sample
```

Stereo:

```text
byte high nibble -> channel 0
byte low nibble  -> channel 1
```

This ordering is required for correct playback.

---

# 20. OpenNomad music decoding policy

Current OpenNomad:

```text
reads entire ADP
decodes entire track to S16LE PCM
hands PCM to SDL3_mixer
```

This is a modern first implementation.

It should not be documented as the original Runtime's buffering/streaming
architecture.

The codec/output content can be faithful while playback ownership is modern.

---

# 21. Music player separation

OpenNomad correctly separates:

```text
Omikron music controller
    numeric track ID
    loop/mode fields
    ADP path resolution

generic MusicPlayer
    PCM/stream ownership
    SDL3_mixer track
    gain/pause/stop
```

This prevents speculative scenario-opcode semantics from leaking into a generic
audio player.

---

# 22. SCX sound table

SCX `DEAD0003` stores named scenario sounds.

Serialized record size:

```text
0x1A
```

Current recovered record:

```c
struct SerializedScxSoundRecord {
    char name[22];        // +0x00
    uint16_t field16;     // +0x16
    uint16_t hId;         // +0x18
}; // 0x1A
```

Runtime mutates the live `+0x16` slot during loading.

---

# 23. SCX sound live field `+0x16`

The original Runtime replaces the `+0x16` word with a loaded sound
handle/resource ID.

On load failure it writes:

```text
0xFFFF
```

Thus this slot is best treated as:

```text
serialized/runtime sound-handle slot
```

rather than a stable authored semantic ID.

OpenNomad names its preserved value:

```text
runtime_sound_id
```

with an explicit runtime-placeholder interpretation.

---

# 24. SCX sound `hId`

Record:

```text
+0x18
```

is a separate 16-bit field used by Runtime lookup paths.

Current name:

```text
h_id
```

The exact original expansion/authoring meaning of “hID” remains unresolved.

Do not merge it with the live sound-resource handle at `+0x16`.

---

# 25. Embedded SCX WAVE resources

Each `DEAD0003` descriptor corresponds to one appended:

```text
RIFF/WAVE
```

resource.

The SCX parser keeps descriptors and WAVE spans in parallel arrays.

Examples in `Grid.SCX`:

```text
INTRO01.WAV
INTRO02.WAV
INTRO03.WAV
INTRO04.WAV
INTRO05.WAV
INTRO06.WAV
INTRO07.WAV
```

`aventure.SCX` contains:

```text
53
```

sound descriptors/resources.

---

# 26. Music ADP versus SCX WAVE

These formats solve different runtime roles:

```text
ADP
    numbered long-form music tracks
    TRACKS/<id>.ADP

WAVE in SCX
    scenario-local named sound effects
    descriptor table + runtime sound handle
```

Do not decode an SCX WAVE as QD ADP based on the word “sound.”

---

# 27. Structured sound functions

The SCX `Script_*` system contains sound operations including the recovered
families:

```text
PlaySound
PlaySyncSound
StopSound
```

and associated reinitialization/native handlers.

These are structured-script native functions, not compact AREA opcodes.

The detailed numeric function-ID catalogue belongs in
`iam-script-functions.md`.

---

# 28. Sound ownership

Runtime sound playback can be associated with a scenario/world object.

This matters for:

```text
spatial position updates
StopSound lookup
scenario unload cleanup
```

Current OpenNomad replaces raw runtime pointers with:

```text
AudioOwnerToken
```

containing safe scenario identity, object index and generation.

That token is an OpenNomad safety abstraction, not a Runtime serialized type.

---

# 29. SFX voice pool

Current reverse-engineering compatibility model establishes:

```text
16 simultaneous SFX voice slots
```

with:

```text
first free slot allocation
no voice stealing
```

OpenNomad models this as:

```text
VoicePool::k_voice_count = 16
```

with generation-counted handles.

The generation counter is modern safety state.

---

# 30. Voice exhaustion

When all original-compatible SFX slots are occupied, the correct policy is:

```text
play request fails / cannot allocate another voice
```

rather than:

```text
steal oldest
steal quietest
grow indefinitely
```

OpenNomad preserves the no-stealing behavior.

---

# 31. Sound-resource compatibility bound

OpenNomad preserves a recovered resource-table compatibility capacity of:

```text
160 sound resources
```

The modern table deduplicates by canonical resource identity and reports
exhaustion safely.

This is an implementation-compatible representation of a fixed original
resource limit rather than a requirement to reproduce old raw arrays.

---

# 32. Invalid sound sentinel

Sound-resource handle:

```text
0xFFFF
```

represents an invalid/unloaded sound in recovered Runtime paths.

This aligns with SCX loader failure behavior at descriptor `+0x16`.

OpenNomad preserves this sentinel in `SoundResourceId`.

---

# 33. Play flags

Current recovered script/play request flags include:

```text
bit 0 / 0x01:
    infinite loop
    confirmed

bit 3 / 0x08:
    observed in cleanup/runtime logic
    semantic meaning unresolved
```

OpenNomad deliberately keeps bit `0x08` as:

```text
unknown_flag
```

instead of inventing a behavior.

---

# 34. StopSound matching

Current recovered behavior is compatible with:

```text
find first active voice
matching:
    sound resource
    owner/context
```

and stop that one.

If no matching voice exists:

```text
no-op
```

is preferable to treating it as a fatal script error.

---

# 35. Scenario unload

Voices tied to a scenario/object owner must not survive indefinitely after the
owner's world state disappears.

OpenNomad provides:

```text
stop_owned_by(owner)
```

using safe owner tokens.

This reproduces ownership cleanup without dereferencing stale Runtime pointers.

---

# 36. Non-spatial playback

Some sounds are played without a spatial emitter.

Current OpenNomad interprets this as:

```text
full gain
centered stereo
normal pitch
```

This is a reasonable compatibility boundary.

Do not infer a world position such as `(0,0,0)` for a truly non-spatial sound.

---

# 37. Native coordinate boundary

Gameplay/world positions are Runtime-native:

```text
inches
+X right
+Y down
+Z forward
```

The modern audio backend uses metres.

Conversion at the audio boundary:

```text
metres =
    inches * 0.0254
```

This is the only scale conversion required.

The audio system should not adopt OpenGL renderer-space axis flips as gameplay
truth.

---

# 38. Listener and emitter state

A spatial voice conceptually requires:

```text
listener:
    position
    velocity
    orientation/basis

emitter:
    position
    velocity
    minimum distance
    maximum distance
```

The original Runtime uses DirectSound/native calculations around equivalent
concepts.

Modern ownership of these values is not required to mirror old DirectSound
buffer structures.

---

# 39. Exact spatialization status

The current OpenNomad `LegacySpatializer` contains a mixture of:

```text
recovered constants/architecture
+
provisional modern formulas
```

It must **not** be documented as an exact reimplementation of all DirectSound
3D calculations.

This confidence split is intentional in the code comments.

---

# 40. Current attenuation approximation

OpenNomad currently uses:

```text
gain = 1
    inside minimum distance

linear falloff
    minimum -> maximum

gain = 0
    at/outside maximum
```

This is currently a compatibility approximation.

Exact Runtime/DirectSound attenuation parity remains to be reconstructed.

---

# 41. Current panning approximation

OpenNomad currently derives:

```text
pan =
    dot(
        normalized(listener -> source),
        listenerRight
    )
```

and maps that to constant-power stereo gains.

This is:

```text
modern approximation
```

unless/until the retail DirectSound/software panning law is proven.

---

# 42. Current stereo law

Current formula:

```text
angle =
    (pan + 1) * pi / 4

left  = cos(angle)
right = sin(angle)
```

This is a conventional constant-power pan law.

Do not cite it as recovered Runtime math.

---

# 43. Doppler evidence

A Runtime-derived/provisional constant near:

```text
429.0
```

is currently isolated as:

```text
k_doppler_speed_of_sound
```

in OpenNomad.

Its precise physical units/original semantic derivation are still under review.

Current software Doppler implementation therefore remains partly provisional.

---

# 44. Current Doppler approximation

OpenNomad currently computes a frequency ratio based on radial velocities:

```text
(speed - listenerRadial)
/
(speed - sourceRadial)
```

with numerical guards/clamps.

This is useful compatibility behavior.

It should not be promoted to an exact Runtime formula until the original
DirectSound/software fallback path has been completely reconstructed.

---

# 45. Real-time delta for audio

OpenNomad spatialization/update uses:

```text
real seconds
```

not the 30 Hz script-frame scalar.

This is appropriate because emitter velocities and host audio updates are
continuous-time backend concerns.

Script durations remain in their script/scenario domains.

---

# 46. Original versus modern output device

Original:

```text
DirectSound / WinMM
Windows audio device/mixer
```

OpenNomad:

```text
SDL3_mixer
negotiated host device format
```

There is no fidelity value in forcing SDL to mimic an old 1999 device format
when decoded samples and game-visible mixing semantics can be reproduced
independently.

---

# 47. OpenNomad output request

Current OpenNomad asks SDL3_mixer for a modern hint:

```text
2 channels
48000 Hz
float32
```

and accepts device negotiation/conversion.

This is explicitly modern backend policy.

It is unrelated to the original ADP sample rate:

```text
22050 Hz
```

which remains part of decoded source content.

---

# 48. Dedicated music track

OpenNomad uses one dedicated mixer track tagged:

```text
"music"
```

and 16 SFX tracks tagged:

```text
"sfx"
```

This is a clean modern mapping of the original conceptual music/SFX separation.

Exact DirectSound buffer-count architecture is not implied.

---

# 49. Music replacement

When a different numeric track is requested:

```text
current track -> new track
```

OpenNomad replaces the music source.

When the same track ID remains active:

```text
no restart
```

matching recovered Runtime behavior.

---

# 50. Music stop

OpenNomad exposes:

```text
stop_music(fadeOutMs)
```

because the generic modern player supports fades.

Do not infer from that API that every Runtime music stop used a millisecond
fade.

Runtime opcode-specific fade/mode semantics must be traced separately.

---

# 51. Gains

OpenNomad has separate modern gains:

```text
master
SFX
music
```

These are useful backend controls.

Do not automatically map them to specific `OMK_SAVE` preference fields until
those original configuration fields are recovered.

---

# 52. Resource caching

Original Runtime uses fixed sound-resource structures and loaded handles.

OpenNomad safely caches decoded `MIX_Audio` objects by canonical identity.

Important semantics to preserve:

```text
one loaded source can be referenced by multiple voices
resource identity is stable
resource limit can be enforced
unload waits until voice ownership is safe
```

The exact heap/cache object layout need not match Runtime.

---

# 53. FMV audio boundary

Startup movies:

```text
EIDOS.mpg
QUANTIC.mpg
GAME.mpg
```

have their own movie-decoding/audio path.

Current OpenNomad uses FFmpeg for FMV playback.

The recent color/crop work on `GAME.MPG` belongs in a future dedicated
`fmv.md` or startup-presentation section, not in the ADP codec.

---

# 54. ADP is not RIFF ADPCM

Despite:

```text
ADP
ADPCM
```

the numbered music file is not simply a WAV file with an ADPCM format tag.

It has the custom 16-byte QD header documented above and a raw nibble stream.

This distinction matters for external players and tooling.

---

# 55. External playback

Tools such as vgmstream can recognize/play Omikron audio assets in some
contexts, but external-tool support is not format authority.

OpenNomad's own parser/decoder should follow the retail bytes and Runtime
behavior.

---

# 56. Recommended modern architecture

```text
AudioSystem
    |
    +-- ScenarioSoundResourceCache
    |       |
    |       +-- decoded SCX WAV resources
    |
    +-- SfxVoicePool
    |       |
    |       +-- 16 compatibility slots
    |
    +-- MusicController
    |       |
    |       +-- numeric Omikron track ID
    |       +-- ADP path/decoder
    |       +-- generic mixer MusicPlayer
    |
    +-- Listener/Spatializer
            |
            +-- native-world -> metres boundary
            +-- compatibility spatial math
```

This is close to current OpenNomad.

---

# 57. Recommended confidence layering in code

For every spatial constant/formula, annotate one of:

```text
Runtime-confirmed
retail-data-confirmed
DirectSound semantic mapping
provisional compatibility approximation
OpenNomad-only safety/modernization
```

Avoid a generic class name like `LegacySpatializer` becoming implicit proof
that every formula inside is legacy-exact.

---

# 58. Recommended regression tests — ADP

- [ ] reject files shorter than `0x10`;
- [ ] decode u24 payload size little-endian;
- [ ] mono flag 0;
- [ ] stereo flag 1;
- [ ] reject unsupported stereo flag;
- [ ] reserved bytes zero;
- [ ] exact payload-size/file-size relation;
- [ ] sample rate 22050;
- [ ] initial predictor/index zero;
- [ ] mono high-nibble then low-nibble ordering;
- [ ] stereo high-left/low-right ordering;
- [ ] predictor clamps to signed 16-bit;
- [ ] step index clamps 0..88;
- [ ] rewind reproduces identical first frames.

---

# 59. Recommended regression tests — music

- [ ] `TRACKS/<id>.ADP` lookup is case-insensitive;
- [ ] track 109 resolves for startup;
- [ ] same active numeric ID does not restart;
- [ ] different ID replaces active music;
- [ ] loop-like operand retained;
- [ ] third mode operand preserved without invented semantics;
- [ ] stop clears current numeric ID.

---

# 60. Recommended regression tests — SFX

- [ ] maximum 16 active compatibility voices;
- [ ] allocation scans first free;
- [ ] no voice stealing;
- [ ] 0xFFFF invalid resource rejected;
- [ ] loop bit 0 honored;
- [ ] unknown 0x08 preserved;
- [ ] StopSound matches first `(resource,owner)`;
- [ ] missing StopSound target is harmless;
- [ ] scenario-owner cleanup stops owned voices;
- [ ] same decoded resource can back several voices.

---

# 61. Recommended future Runtime work

Highest-value unresolved targets:

1. exact DirectSound voice/buffer runtime structure;
2. prove/rederive the 16-voice and 160-resource limits directly into this doc;
3. exact minimum/maximum distance units and defaults;
4. original attenuation law;
5. original panning/stereo law;
6. original Doppler constant/unit/formula;
7. bit `0x08` sound flag;
8. full `hId` meaning;
9. exact music operand B/C semantics;
10. UI/I2D WAV playback subsystem;
11. original music buffering/streaming strategy;
12. original master/music/SFX preference mapping.

---

# 62. Useful Runtime anchors

```text
00404FB0
    compact AREA music opcode 0x67

004C013C
    current numeric music-track state

DSOUND.dll imports
    DirectSoundCreate
    DirectSoundEnumerateA

SCX DEAD0003
    scenario sound descriptor table

SCX structured script handlers
    PlaySound / PlaySyncSound / StopSound family
```

Additional lower-level sound-function addresses should be added as they are
rechecked and named.

---

# 63. Compact reference

Music:

```text
TRACKS/<id>.ADP
opcode 67
handler 00404FB0
current track global 004C013C

ADP header:
+00 u24 payload size
+03 u8 stereo flag
+04..0F zero
+10 payload

sample rate 22050
QD/IMA nibble codec
```

Scenario SFX:

```text
SCX DEAD0003
record 0x1A

+00 char name[22]
+16 live sound-handle slot
+18 hId

parallel RIFF/WAVE payload
```

Compatibility limits:

```text
16 SFX voices
160 cached sound-resource slots
0xFFFF invalid sound handle
```

---

# 64. Boundary of current knowledge

Strongly recovered:

```text
ADP container/decoder
22050 Hz source rate
numbered music lookup
same-track no-restart behavior
SCX sound-table structure
SCX WAVE resource relationship
fixed compatibility voice/resource limits
sound ownership and loop flag
```

Still provisional/incomplete:

```text
exact DirectSound spatial math
distance defaults/units
Doppler interpretation
sound flag 0x08
hId source meaning
music operands B/C
original ADP streaming/buffering implementation
```

The key rule is:

> Keep original content/behavior separate from the modern mixer backend. ADP
> decoding, SCX sound identity and voice limits can be faithful even when the
> output device, threading and memory ownership are modern.

---

# 65. Dialogue performance voice

IAM/DIALOG face basenames resolve to synchronized `MORPH/<basename>.3dm`
packages. Their speech is embedded in each full 30 Hz record; it is not a
`VOICE/<basename>.ADP` lookup and does not use the music decoder. OpenNomad
decodes all audio chunks continuously with the Runtime 3DM ADPCM variant and
plays the resulting 22080 Hz stereo signed-16 stream on one dedicated,
nonspatial, one-shot mixer track. This track is outside both the 16-voice SFX
pool and the music lane, but follows master and SFX/dialog gain. See
[`3dm.md`](3dm.md) for the exact nibble math, state continuity, and synchronized
visual record layout.
