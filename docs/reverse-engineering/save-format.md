# Omikron Save System and Persistent Game State

> **Status:** work-in-progress reverse-engineering documentation for OpenNomad.  
> **Authority:** the retail Windows `Runtime.exe` is authoritative. Asset files, real save files, UI screenshots, and fan-developed tools are corroborating evidence only.

This document records the currently understood save/persistence architecture of **Omikron: The Nomad Soul**, with particular emphasis on `IAM/GAMES`, the `OMK_SAVE` block, the `IAM/START`-derived per-playthrough state, and the serialized current-character record.

The goal is to preserve enough exact detail that OpenNomad can:

1. initialize a new game from the same canonical state as the retail engine;
2. implement a semantic, maintainable `GameState` instead of cloning Runtime's global-memory layout;
3. import and upgrade original retail saves;
4. eventually write OpenNomad-native saves without tying the native format to the retail binary layout; and
5. avoid losing reverse-engineering detail as work moves between sessions and contributors.

---

## 1. Reference executable and confidence vocabulary

The Runtime addresses in this document apply to the retail executable currently used by the OpenNomad reverse-engineering effort:

```text
SHA-256
55f7120bfea7891b048c64e3682f3259cdbf2719a43fa24e42254b753c95d2ef

PE image base
0x00400000
```

### Confidence vocabulary

- **Confirmed — Runtime:** directly demonstrated by Runtime reads/writes, loop bounds, file offsets, size calculations, dispatch behavior, or matching serializer/deserializer behavior.
- **Corroborated:** Runtime behavior agrees with retail assets, real save data, game UI, or an independent tool.
- **Strong:** structure/role is highly constrained, but the original source-level name or a narrow semantic detail remains unknown.
- **Tentative:** useful working interpretation that must not yet become a permanent API/field name.
- **Unknown:** layout is known to contain persistent state, but semantics are not yet recovered.

When evidence conflicts, `Runtime.exe` wins.

---

# 2. High-level persistence architecture

The retail engine has two distinct persistence layers inside `IAM/GAMES`:

```text
IAM/GAMES
│
├── +0x000000 : 0x0DA8-byte OMK_SAVE block
│               process/global configuration and persistent preferences
│
└── +0x000DA8 : 256 × 0x8028-byte save slots
                individual playthroughs
```

The important architectural distinction is:

- `OMK_SAVE` is **not** the main per-playthrough game-progress state.
- The actual persistent playthrough state is the `0x2000` state snapshot inside each `0x8028` slot.
- That `0x2000` snapshot is derived directly from `IAM/START`.

The supplied retail installation does **not** contain `IAM/GAMES`. A downloaded completed-game `GAMES` file has the exact fully allocated size expected by Runtime. This strongly suggests Runtime creates the file lazily when persistence is first needed, probably on the first save, but the file-creation path has not yet been traced far enough to mark this confirmed.

---

# 3. Startup ordering: defaults before persisted `OMK_SAVE`

The early startup order is confirmed in `WinMain` (`0x00410950`).

Relevant sequence:

```text
WinMain
  |
  |-- early display / DirectDraw / process setup
  |-- command-line parsing
  |
  |-- 0x00413120
  |     early startup/path diagnostics
  |
  |-- 0x0041F4C0
  |     construct default OMK_SAVE in memory
  |     initialize additional runtime globals
  |
  |-- CONFIG-only argument handling
  |
  |-- 0x00409200
  |     load persisted OMK_SAVE header from IAM\GAMES if available
  |
  |-- process WINDOW / NOFMV and related options
  |-- create windows
  |
  `-- later engine/core initialization
        FMVs
        aventure.scx
        splash
        GRID
        main menu
```

The relevant calls occur in this order:

```asm
00410A27  call 00413120
00410A2F  call 0041F4C0
...
00410A66  call 00409200
```

This proves the intended pattern:

```cpp
initialize_default_preferences();

if (configuration_only_mode) {
    run_configuration_dialog();
    return;
}

load_persisted_preferences_if_available();
initialize_engine();
```

If `IAM/GAMES` is missing or invalid, the defaults produced by `0x0041F4C0` remain live.

This occurs long before the main menu and explains why `OMK_SAVE` fields are referenced very early during engine initialization.

---

# 4. `OMK_SAVE`: global configuration/persistence header

## 4.1 Base, size, and initialization

Live Runtime base:

```text
0x0090E180
```

Exact size:

```text
0x0DA8 bytes
```

`0x0041F4C0` begins by clearing exactly that extent:

```asm
mov ecx, 0x36A
mov edi, 0x0090E180
xor eax, eax
rep stosd
```

because:

```text
0x36A dwords × 4 = 0x0DA8 bytes
```

The persistence loader at `0x00409200` reads/copies the same extent.

## 4.2 Known early fields

| Offset | Runtime address | Type | Default | Meaning / status |
|---:|---:|---|---:|---|
| `+0x000` | `0x0090E180` | `char[8]` | `OMK_SAVE` | signature — confirmed |
| `+0x008` | `0x0090E188` | `u32` | `0x00010001` | format version — confirmed |
| `+0x00C` | `0x0090E18C` | `u16` | `640` | display width — confirmed |
| `+0x00E` | `0x0090E18E` | `u16` | `480` | display height — confirmed |
| `+0x010` | `0x0090E190` | `u8` | `1` | `displaySky` — confirmed by preference string |
| `+0x011` | `0x0090E191` | `u8` | `1` | `displayShadows` — confirmed by preference string |
| `+0x012` |  | `u8[2]` | 0 | unknown/padding |
| `+0x014` | `0x0090E194` | `i32` | `50` | `clipDistance` — confirmed by preference string |
| `+0x018` | `0x0090E198` | `i32` | `0` | dialog attenuation — confirmed |
| `+0x01C` | `0x0090E19C` | `i32` | `0` | ambient attenuation — confirmed |
| `+0x020` | `0x0090E1A0` | `i32` | `0` | FX attenuation — confirmed |
| `+0x024` | `0x0090E1A4` | `u8` | `1` | unknown option |
| `+0x025` | `0x0090E1A5` | `u8` | `1` | unknown option |
| `+0x026` | `0x0090E1A6` | `u16` | `1` | unknown option |
| `+0x028` | `0x0090E1A8` | `u16` | `1` | unknown option |
| `+0x02A` | `0x0090E1AA` | `u8` | `1` | `tekkenCam` — confirmed by preference string |
| `+0x02B` |  | `u8` | 0 | unknown/padding |
| `+0x02C` | `0x0090E1AC` | `u16` | `20` | mouse sensitivity X — confirmed |
| `+0x02E` | `0x0090E1AE` | `u16` | `15` | mouse sensitivity Y — confirmed |
| `+0x030` | `0x0090E1B0` | `u8` | `0` | unknown option |
| `+0x031` | `0x0090E1B1` | `u8` | `0` | unknown option |
| `+0x032` |  | `u8[2]` | 0 | unknown/padding |

The attenuation defaults are zero; because the fields are explicitly attenuation values, zero should not automatically be interpreted as muted volume.

## 4.3 Three persistent input/control tables

`0x0041F4C0` copies three static `0xE0`-byte tables into `OMK_SAVE`:

```text
0x004C8F90 -> OMK_SAVE + 0x034
0x004C9070 -> OMK_SAVE + 0x114
0x004C9150 -> OMK_SAVE + 0x1F4
```

Each table is:

```text
0xE0 bytes
= 56 dwords
= 4 × 14 entries
```

The first table begins with DirectInput DIK values:

```text
0xCB  Left
0xCD  Right
0xC8  Up
0xD0  Down
```

and contains other recognizable keyboard scan codes. The group is therefore confirmed to be persistent control/input configuration, although the exact semantic identity of all three tables should remain provisional until all consumers are traced.

Working serialized shape:

```cpp
std::uint32_t inputTable0[4][14];  // +0x034 .. +0x113
std::uint32_t inputTable1[4][14];  // +0x114 .. +0x1F3
std::uint32_t inputTable2[4][14];  // +0x1F4 .. +0x2D3
```

Do not prematurely name all three `keyboard`, `joystick`, etc. without tracing the runtime readers.

## 4.4 High-score table at `+0x2D4`

Runtime function `0x00409370` computes:

```text
categoryBase = OMK_SAVE + 0x2D4 + category * 0xB4
```

Each category contains five `0x24`-byte records. The function compares/inserts by the dword at entry `+0x20`, shifts lower records, copies a 32-byte name, and writes the score.

Confirmed shape:

```cpp
struct OmkHighScoreEntry {
    char name[32];       // +0x00
    std::int32_t score;  // +0x20
}; // 0x24

struct OmkHighScoreCategory {
    OmkHighScoreEntry entries[5];
}; // 0xB4

OmkHighScoreCategory highScores[4];
// +0x2D4 .. +0x5A3, total 0x2D0 bytes
```

This explains why initialization clears `0xB4` **dwords** at `0x0090E454`: that is `0x2D0` bytes, exactly the whole four-category high-score array.

## 4.5 Renderer/options bytes at `+0x5A4`

Defaults:

```text
+0x5A4 = 0
+0x5A5 = 0
+0x5A6 = 3
+0x5A7 = 1
```

The first two are consumed very early by core renderer initialization:

```text
OMK_SAVE +0x5A4 -> 0x0045EF20
OMK_SAVE +0x5A5 -> 0x0045EF60
```

Their precise user-facing settings names remain unresolved, but they are clearly renderer/configuration inputs.

## 4.6 Version compatibility

`0x00409200` accepts at least:

```text
0x00010000
0x00010001
```

The older `0x00010000` compatibility path clears:

```text
+0x5A4
+0x5A5
```

then normalizes the live version to:

```text
0x00010001
```

This is important precedent: the retail game already treats its persistence format as versioned and performs migration rather than assuming one immutable binary representation.

## 4.7 Trailing `0x800` bytes

Exactly `0x800` bytes remain after the known `+0x5A4..+0x5A7` bytes:

```text
+0x5A8 .. +0xDA7
```

No sufficiently strong semantic model exists yet. Do **not** call this padding or reserved space without further evidence; indexed/indirect Runtime accesses could still target it.

## 4.8 Working `OMK_SAVE` structure

```cpp
struct OmkSaveStateV10001 {
    char signature[8];                       // +0000
    std::uint32_t version;                   // +0008

    std::uint16_t screenWidth;               // +000C
    std::uint16_t screenHeight;              // +000E
    std::uint8_t displaySky;                  // +0010
    std::uint8_t displayShadows;              // +0011
    std::uint8_t unknown0012[2];
    std::int32_t clipDistance;                // +0014
    std::int32_t dialogAttenuation;           // +0018
    std::int32_t ambientAttenuation;          // +001C
    std::int32_t fxAttenuation;               // +0020
    std::uint8_t unknown0024;                 // +0024
    std::uint8_t unknown0025;                 // +0025
    std::uint16_t unknown0026;                // +0026
    std::uint16_t unknown0028;                // +0028
    std::uint8_t tekkenCam;                   // +002A
    std::uint8_t unknown002B;
    std::uint16_t mouseSensitivityX;          // +002C
    std::uint16_t mouseSensitivityY;          // +002E
    std::uint8_t unknown0030;                 // +0030
    std::uint8_t unknown0031;                 // +0031
    std::uint8_t unknown0032[2];

    std::uint32_t inputTable0[4][14];         // +0034
    std::uint32_t inputTable1[4][14];         // +0114
    std::uint32_t inputTable2[4][14];         // +01F4

    OmkHighScoreEntry highScores[4][5];       // +02D4

    std::uint8_t rendererOption0;             // +05A4
    std::uint8_t rendererOption1;             // +05A5
    std::int8_t unknown05A6;                  // +05A6, default 3
    std::int8_t unknown05A7;                  // +05A7, default 1

    std::uint8_t unknown05A8[0x800];          // +05A8 .. +0DA7
};
```

Any OpenNomad parser should use explicit checked little-endian reads rather than casting disk bytes directly to this illustrative structure.

---

# 5. `IAM/GAMES` physical layout

A real downloaded completed-game file has exact size:

```text
0x8035A8 bytes
```

This decomposes perfectly as:

```text
0x0DA8 + 256 × 0x8028 = 0x8035A8
```

Therefore the physical format is:

```text
+0x000000  OMK_SAVE header                         0x0DA8
+0x000DA8  save slot 0                            0x8028
+0x008E00  save slot 1                            0x8028
...         ...
            save slot 255
```

Generic slot address:

```text
slotOffset = 0xDA8 + slotIndex * 0x8028
```

The old fan save editor independently uses the same geometry (with a 1-based UI slot number), which corroborates Runtime's arithmetic.

The supplied `GAMES` contains **51 populated slots (0–50)** for player name `Jennedreng`. Those slots span many points in a completed playthrough and are therefore useful as a longitudinal corpus rather than merely a single endgame state.

---

# 6. Save slot layout (`0x8028` bytes)

Confirmed top-level layout:

```text
slot +0x0000  char playerName[32]                  0x0020
slot +0x0020  uint32 metadata0                     0x0004
slot +0x0024  uint32 metadata1                     0x0004
slot +0x0028  serialized START-derived state       0x2000
slot +0x2028  save thumbnail                       0x6000
                                                    ------
                                                    0x8028
```

Illustrative shape:

```cpp
struct OriginalGameSlot {
    char playerName[32];
    std::uint32_t metadata0;
    std::uint32_t metadata1;
    std::byte state[0x2000];
    std::uint16_t thumbnail[128 * 96];
};
```

The two metadata dwords are confirmed persisted values but their exact meanings are still unresolved.

## 6.1 Player name

The New Game interface edits a transient 32-byte name buffer at `0x0069BDA0`. On successful save/profile bookkeeping, Runtime copies the player name into the slot's first 32 bytes.

Player identity therefore belongs to the per-game slot, **not** the global `OMK_SAVE` configuration header.

## 6.2 No observed slot checksum

Runtime's load path has not exposed a slot checksum, and the old fan save editor modifies stat fields directly in `GAMES` without recomputing any integrity value. This strongly suggests the retail slot has no checksum/integrity field that must be updated after edits.

---

# 7. `0x00433090`: thumbnail buffer, not gameplay state

`0x00433090` is trivial:

```asm
00433090:
    mov eax, 0x00907FE0
    ret
```

It returns the save-thumbnail buffer.

Nearby capture/conversion code operates on exactly:

```text
0x3000 16-bit pixels
```

and:

```text
128 × 96 = 12288 = 0x3000 pixels
0x3000 × 2 = 0x6000 bytes
```

Therefore:

```text
slot +0x2028 .. +0x8027
```

is a **128×96 16-bit save screenshot/thumbnail**, not additional game state.

The capture/restore code converts between the active 16-bit display pixel masks and a normalized RGB555-like persisted representation.

This reduces the actual gameplay-state problem almost entirely to the `0x2000` block at slot `+0x28` plus the two metadata dwords.

---

# 8. The `0x2000` save state is `IAM/START` evolved at runtime

This is the central persistence discovery.

At ordinary new-session initialization, Runtime:

1. allocates/clears a fresh `0x2000` buffer;
2. loads `IAM\START`;
3. copies the file bytes into that zero-filled buffer;
4. frees the file allocation; and
5. calls `0x0040DB00` to relocate/activate the state.

The retail `IAM/START` file is:

```text
0x1636 bytes = 5686 bytes
```

so a fresh state is conceptually:

```text
IAM/START bytes 0x0000..0x1635
zero fill       0x1636..0x1FFF
```

The playthrough mutates this live state. Saving later serializes the whole live `0x2000` buffer into the slot.

Conceptual lifecycle:

```text
IAM/START
    |
    | copy into zero-filled 0x2000 state allocation
    v
0x0040DB00
    |
    | serialized offsets -> live pointers
    v
live persistent game state
    |
    | gameplay mutates it
    v
0x0040D950
    |
    | live pointers -> serialized offsets
    v
slot +0x28, 0x2000 bytes
```

This means OpenNomad should not create a new-game state by manually reproducing a large set of guessed defaults. `IAM/START` is the authoritative retail template for a fresh playthrough.

## 8.1 START global-variable region

The START header selects the authored signed-global array exactly:

```text
header +0x08  uint32 globalVariablesOffset  -> begin
header +0x0C  uint32 areaMappingOffset      -> end

region = [begin, end)
element = little-endian int32_t
```

The parser validates ordered bounds, an end within the record, and a byte
count divisible by four. It does not hardcode a retail offset or count.

The studied retail START corroborates the layout independently:

```text
begin  0x058C
end    0x1064
bytes  0x0AD8
count  694 signed dwords
IDs    0..693 (matching VARIABLES.TAG's highest authored ID)
```

OpenNomad's `GameState::from_start()` copies these values verbatim, including
zero, positive, and negative initial values. All compact AREA/SCENE reads,
writes, and interface-result destinations access that single playthrough
store; the array is never initialized by assuming every value is zero.

---

# 9. `0x0040D950`: serialize live START state

`0x0040D950` serializes the current live state into a `0x2000` destination.

Before copying, it refreshes selected live fields, notably:

```text
START +0x586  current area
START +0x588  linked/secondary area
```

from current world-context state.

It also updates four values at:

```text
START +0x2C
START +0x30
START +0x34
START +0x38
```

from the currently controlled entity. The first three are spatial-coordinate-derived values; the fourth is orientation/heading-like and is reduced to a 12-bit angular domain. Exact axis naming remains to be confirmed.

The serializer then copies exactly:

```asm
mov ecx, 0x800
rep movsd
```

that is:

```text
0x800 dwords × 4 = 0x2000 bytes
```

Finally, it converts six internal live pointers in the serialized copy into offsets relative to the state base:

```text
START +0x08
START +0x0C
START +0x10
START +0x14
START +0x18
START +0x1C
```

This is a classic relocatable snapshot format.

---

# 10. `0x0040DB00`: activate/deserialize START state

`0x0040DB00` is the inverse of the serializer.

It sets the active state pointer and converts the six serialized offsets back into live pointers:

```cpp
if (serializedOffset != 0)
    livePointer = stateBase + serializedOffset;
```

It also reconstructs additional runtime-only pointers/views inside the fixed state region, including pointers to the two 256-byte current-character text buffers at:

```text
START +0x150
START +0x250
```

This has an important original-save compatibility consequence:

> Retail save files can contain stale absolute process addresses in fields that Runtime intentionally overwrites during load. An importer must not preserve such values as semantic state.

The clearest example is the current-character structure at `START +0x3C`: its first two fields are runtime pointers to the fixed strings at `+0x150` and `+0x250`. Saved pointer values are meaningless outside the process that produced the save and are rebuilt by Runtime.

---

# 11. Top-level serialized `IAM/START` / game-state layout

The first `0x20` bytes contain two header-like values followed by six serialized offsets:

```text
+0x00  u32  103
+0x04  u32  19991004
+0x08  u32  0x058C
+0x0C  u32  0x1064
+0x10  u32  0x126C
+0x14  u32  0x1314
+0x18  u32  0x1398
+0x1C  u32  0x13FC
```

The six regions are now strongly mapped:

```text
+0x058C  global variables
+0x1064  area mapping
+0x126C  packed 2-bit persistent states
+0x1314  CHARACTER flags
+0x1398  ADDRESS flags
+0x13FC  ZONE flags
```

Current working top-level layout:

```cpp
struct SerializedStartState {
    std::uint32_t formatRevision;              // +0000 = 103
    std::uint32_t buildDate;                   // +0004 = 19991004

    std::uint32_t variablesOffset;              // +0008 -> 058C
    std::uint32_t areaMapOffset;                // +000C -> 1064
    std::uint32_t packedStateOffset;            // +0010 -> 126C
    std::uint32_t characterFlagsOffset;         // +0014 -> 1314
    std::uint32_t addressFlagsOffset;           // +0018 -> 1398
    std::uint32_t zoneFlagsOffset;              // +001C -> 13FC

    std::byte unknown0020[0x0C];                // +0020 .. +002B

    std::int32_t savedPosition0;                // +002C
    std::int32_t savedPosition1;                // +0030
    std::int32_t savedPosition2;                // +0034
    std::int32_t savedOrientation;              // +0038

    RetailCurrentCharacter currentCharacter;    // +003C .. +014F

    char signs[0x100];                          // +0150
    char interests[0x100];                      // +0250

    std::int16_t objectList0[18];               // +0350
    std::int16_t objectList1[256];              // +0374
    std::int16_t objectList2[9];                // +0574

    std::int16_t currentArea;                    // +0586
    std::int16_t linkedArea;                     // +0588
    std::byte unknown058A[2];                    // +058A

    std::int32_t variables[694];                // +058C
    std::int16_t areaMap[260];                  // +1064

    std::uint8_t packedState2Bit[168];          // +126C
    std::uint8_t characterFlags[132];           // +1314
    std::uint8_t addressFlags[100];             // +1398
    std::uint8_t zoneFlags[570];                // +13FC
};
```

Serialized retail `IAM/START` ends at `0x1636`. The save snapshot always reserves/stores a full `0x2000` bytes.

The relocated ADDRESS region is selected by the header pair `START +0x18`
through `START +0x1C`, not by a hard-coded retail offset in consumers. Retail
uses `[0x1398, 0x13FC)`: 100 bytes, or 800 least-significant-bit-first flags.
For nonnegative ADDRESS ID `n`, Runtime uses byte `n / 8` and bit `n % 8`.
OpenNomad's semantic `GameState` copies these bytes during New Game/session
initialization and rejects IDs outside the copied capacity.

---

# 12. Global variables at `START +0x58C`

The first relocated region spans:

```text
0x1064 - 0x058C = 0x0AD8 bytes
0x0AD8 / 4 = 694 int32 values
```

`VARIABLES.TAG` uses IDs **0 through 693**.

Runtime's global-variable accessors simply index this table. Therefore:

```cpp
std::int32_t globalVariables[694];
```

is confirmed.

The studied pristine retail `START` happens to author zero in all 694 entries.
That corpus observation is not an initialization rule: loaders must copy every
signed dword from the header-selected region rather than synthesize zeros.

Known variable IDs include:

```text
19  Interface
20  Inventaire
22  Joueur
34  Vie
37  Argent
60  Anneaux
87  Mana
...
```

Real saves show these fields evolving as expected for gameplay script state.

This has an important OpenNomad architectural implication: script global-variable reads/writes should ultimately converge on the persistent `GameState` array. A separate AREA-runtime-only variable map is a temporary approximation and would break save semantics.

The already-reversed interface opcode `0x46` is a concrete example: its completion destination can be global variable `19` (`Interface`), and the value must persist in this canonical variable store.

---

# 13. Area mapping at `START +0x1064`

Region size:

```text
0x126C - 0x1064 = 0x208 bytes
0x208 / 2 = 260 int16 values
```

Runtime accesses this as a signed 16-bit array and updates entries such as:

```cpp
state->areaMap[currentArea] = linkedArea;
```

`AREAS.TAG` covers IDs up to the high 250s, fitting the 260-entry capacity.

Working shape:

```cpp
std::int16_t areaMap[260];
```

The default table is overwhelmingly `-1`.

---

# 14. Packed 2-bit persistent state at `START +0x126C`

Region size:

```text
0x1314 - 0x126C = 168 bytes
```

Runtime get/set helpers treat each logical entry as exactly two bits:

```cpp
value = (table[index / 4] >> ((index % 4) * 2)) & 3;
```

Capacity:

```text
168 × 4 = 672 logical entries
```

The exact resource category/semantic name remains unresolved, but the table is clearly persistent authored lifecycle/status state used during world/entity activation.

Progression across pristine START and the 51 supplied saves is highly structured. Approximate logical-state counts:

```text
             0      1      2      3
START        2     217      0     453
slot 0       2     216      2     452
slot 10      2     185     59     426
slot 20      2     156    120     394
slot 30      2     147    154     369
slot 40      3     143    210     316
slot 50      3     125    239     305
```

State `2` accumulates strongly as the game progresses, mainly at the expense of states `1` and `3`. This is consistent with a persistent authored-object/entity lifecycle machine but is not enough to assign a final name.

---

# 15. CHARACTER, ADDRESS, and ZONE flag bitsets

## 15.1 CHARACTER flags at `+0x1314`

```text
0x1398 - 0x1314 = 132 bytes = 1056 bits
```

AREA-script diagnostic/tag machinery explicitly identifies the associated resource category as:

```text
CHARACTERS
```

Runtime provides direct bit get/set operations. `CHARACTERS.TAG` is not present in either the retail installation or the investigated demo, so friendly authored names are unavailable from shipped TAG data.

The real saves show that this is not a simple monotonic "characters discovered" bitmap; bits both set and clear over a playthrough.

## 15.2 ADDRESS flags at `+0x1398`

```text
0x13FC - 0x1398 = 100 bytes = 800 bits
```

Runtime script handlers identify the resource category as:

```text
ADDRESSES
```

`ADDRESSES.TAG` contains IDs up to roughly 790, matching the 800-bit capacity closely.

Exact meaning of bit 0/1 still needs behavioral tracing.

## 15.3 ZONE flags at `+0x13FC`

The final serialized region occupies:

```text
0x13FC .. 0x1635
570 bytes = 4560 bits
```

Runtime performs bit get/set operations and masks incoming IDs with:

```cpp
id &= 0x7FFF;
```

The script/tag category is explicitly:

```text
ZONES
```

This agrees with high-bit modifiers observed in zone IDs.

---

# 16. Persistent object-ID collections at `START +0x350`

`0x0040DB00` establishes three persistent object-ID lists:

```text
START +0x350   18 × int16 object IDs
START +0x374  256 × int16 object IDs
START +0x574    9 × int16 object IDs
```

`-1` means unused/empty.

Pristine `START` contains examples including:

```text
6    Clé Appartement Kay'l
171  Notice Sneak
176  Notice Multiplan
163  Anneaux 5
```

Across real saves, the lists contain items such as weapons, ammunition, keys, documents, books, consumables, jewels, mission memos, etc. Duplicates are preserved, so these are not merely discovered-object bitsets.

Their exact gameplay roles remain to be named. The 18-entry list appears more constrained/active than the broad 256-entry collection, while the 9-entry list often contains mission-memo-like objects, but these interpretations should remain provisional until list-manipulation routines are fully traced.

The recovered insertion helper is newest-first: if a collection has an empty
slot, it shifts existing IDs toward the end and writes the new OBJECTS ID at
index zero. A full collection is unchanged. Compact opcode `0x32` suppresses
duplicate insertion only for kind 2 (and native kind 3, whose persistence is
not established); kinds 0 and 1 preserve duplicates. OpenNomad retains every
fixed-capacity slot, including `-1`, in semantic session state rather than
collapsing this layout into an unordered inventory abstraction.

## 16.1 `IAM/OBJECT` reconstruction

Runtime resolves each non-negative object ID through `IAM/OBJECT` when activating START state.

Current understanding of `IAM/OBJECT`:

```text
1002 fixed records
record stride: 0x800 bytes
Runtime reads: first 0x518 bytes
```

The loader builds a live `0x38`-byte object descriptor and rearranges record bytes approximately as:

```text
IAM/OBJECT +0x18..+0x37 -> live object +0x00..+0x1F
IAM/OBJECT +0x00..+0x17 -> live object +0x20..+0x37
```

The persistent START snapshot therefore stores object **IDs**, not complete object definitions; full definitions are reconstructed from retail asset data after load.

---

# 17. Current-character record at `START +0x3C`

The current possessed body is stored as a `0x114`-byte authored character record copied into the persistent START state.

Records of the same `0x114` shape are present in `IAM/AREA` and `IAM/SCENE`; possession/current-body handling copies an authored character record into START and then mutable fields evolve during play.

In `IAM/AREA`, table-0 character placement `+0x12` resolves against the authored definition ID at
table-4 record `+0x110`. The definition's fixed model field at `+0x090` supplies the character model
resource identity used by AREA opcode `0x4E`; this is distinct from hard-coding a model by the
table-0 character ID.

Real saves contain different current bodies such as:

```text
KAY'L 669
SYAO 471
ITZAM'A 420
JAYLI'N 814
BAHIMY'L 262
ZAO'R 940
IMAN 631
ENYA'D 843
NIOMA'Y 452
NEME'T 114
FODO
KUSHULAI'N
```

## 17.1 Identity/profile fields

The retail Identity UI, save bytes, Runtime accessors, and authored resources agree on the following layout:

```cpp
struct RetailCurrentCharacter {
    char* signs;                         // +0x000, runtime-only pointer
    char* interests;                     // +0x004, runtime-only pointer

    char name[32];                       // +0x008
    char job[32];                        // +0x028

    char adventureControlSet[18];        // +0x048, e.g. H1AVNT
    char combatControlSet[18];           // +0x05A, e.g. H1CMBT

    char sex[8];                         // +0x06C, e.g. "M"
    char eyes[8];                        // +0x074, e.g. "Green"
    char bloodType[4];                   // +0x07C, e.g. "K-"
    char height[8];                      // +0x080, ASCII e.g. "178"
    char weight[8];                      // +0x088, ASCII e.g. "80"
    char model[10];                      // +0x090, e.g. "HO1_FN"

    std::int16_t age;                    // +0x09A

    // characteristics/currencies follow...
};
```

For Kay'l, the actual retail Identity screen displays values matching the saved record:

```text
Name        KAY'L 669
Age         30
Sex         M
Blood Type  K-
Height      178
Weight      80
Eyes        Green
Job         Investigating Agent
```

### Runtime-only pointers

The two pointer fields at character `+0x00/+0x04` are rebuilt by `0x0040DB00` to point to:

```text
START +0x150  Signs text
START +0x250  Interests text
```

Retail save snapshots can therefore contain stale absolute process addresses in these fields. Original-save import must ignore the serialized pointer values and reconstruct semantic links instead.

## 17.2 Signs and Interests text

Confirmed fixed buffers:

```text
START +0x150  char signs[256]
START +0x250  char interests[256]
```

Kay'l's retail UI displays the strings directly under labels `Signs` and `Interests`.

## 17.3 Character control-set resources

Kay'l contains:

```text
H1AVNT
H1CMBT
```

These correspond to retail files such as:

```text
ANIMS/H1AVNT.CTL
ANIMS/H1CMBT.CTL
```

Female variants include:

```text
F1AVNT.CTL
F1CMBT.CTL
```

The CTL contents contain action/state names such as adventure movement/use/shooting states and combat guards/jumps/impacts. They appear to be character motion/control state sets rather than simple animation filenames.

`HO1_FN` is confirmed to identify Kay'l's model resource under `MESHES/PERSOS`, backed by the corresponding `.3DO`/`.3DT` assets.

---

# 18. Character characteristics and currencies (`+0x9A..+0xAF`)

The old fan save editor first pointed at these offsets, but Runtime accessors and the actual retail Characteristics UI independently confirm the mapping.

| Character offset | START offset | Type | Meaning | Kay'l UI |
|---:|---:|---|---|---:|
| `+0x09A` | `+0x0D6` | `i16` | Age | 30 |
| `+0x09C` | `+0x0D8` | `i16` | Mana | 10 |
| `+0x09E` | `+0x0DA` | `i16` | Speed | 70 |
| `+0x0A0` | `+0x0DC` | `i16` | Attack | 70 |
| `+0x0A2` | `+0x0DE` | `i16` | Body Resistance | 30 |
| `+0x0A4` | `+0x0E0` | `i16` | Dodge | 60 |
| `+0x0A6` | `+0x0E2` | `i16` | Fight Experience | displayed as `Initiate` |
| `+0x0A8` | `+0x0E4` | `i16` | unknown characteristic | not shown |
| `+0x0AA` | `+0x0E6` | `i16` | Energy | 10 |
| `+0x0AC` | `+0x0E8` | `u16` | Seteks | currency |
| `+0x0AE` | `+0x0EA` | `i16` | Rings | save-game currency/resource |

Runtime setters clamp most characteristics to `0..200`. Seteks uses a wider unsigned-like range and saturates at `0xFFFF` in relevant arithmetic.

### `+0xA8` unknown characteristic

Runtime exposes full getter/setter support for `character +0xA8` and treats it like the other `0..200` characteristics. However:

- the retail Characteristics UI does not display it;
- the fan editor does not expose it;
- pristine START and all 51 inspected saves leave it at zero.

Best working interpretation: a real engine characteristic that was unused/deprecated in the retail game. Preserve it in retail decoding but keep a neutral name such as `unknownCharacteristicA8`.

### Fight Experience presentation

Fight Experience is stored numerically, but the UI derives a rank string such as:

```text
Initiate
```

The rank mapping logic remains to be traced. The string should not be stored as the persistent value in OpenNomad.

### Currency correction

- **Seteks** are the game's general money/currency.
- **Rings** are consumed/used when saving a game.

Neither should be modeled as a generic character "stat" merely because they are adjacent to characteristics in the authored record.

---

# 19. Character type at `+0xB0`

`character +0xB0` is a confirmed 32-bit character-type enum. Runtime retains the associated strings:

| Value | Runtime string |
|---:|---|
| 0 | `No one` |
| 1 | `Man passer` |
| 2 | `Woman passer` |
| 3 | `Man enemy` |
| 4 | `Woman enemy` |
| 5 | `Mecagarde` |
| 6 | `Mecadog` |
| 7 | `X-Tech` |
| 8 | `Z-Tech` |
| 9 | `Incarnable` |
| 10 | `Gandhar` |
| 11 | `Zombie` |
| 12 | `Spectre` |
| 13 | `Astaroth` |

Possessable bodies such as Kay'l, Syao, Itzam'a, Jayli'n, etc. contain value `9` (`Incarnable`).

Working enum:

```cpp
enum class CharacterType : std::uint32_t {
    None = 0,
    MalePasser = 1,
    FemalePasser = 2,
    MaleEnemy = 3,
    FemaleEnemy = 4,
    Mecagarde = 5,
    Mecadog = 6,
    XTech = 7,
    ZTech = 8,
    Incarnable = 9,
    Gandhar = 10,
    Zombie = 11,
    Spectre = 12,
    Astaroth = 13,
};
```

The original Runtime strings should remain documented even if OpenNomad uses cleaner enum identifiers.

---

# 20. Character behaviour/AI region `+0xB4..+0xF9`

The physical layout is now known; the remaining problem is semantic naming.

## 20.1 Eleven signed behaviour parameters: `+0xB4..+0xC8`

There are eleven `int16` authored values:

```cpp
std::int16_t behaviorParam[11];
```

Runtime exposes them through generic CHARACTER/VALUES accessors. Getter operation IDs map non-linearly to offsets:

```text
VALUE 23 -> +0xC8
VALUE 24 -> +0xC6
VALUE 25 -> +0xC0
VALUE 26 -> +0xB4
VALUE 27 -> +0xBA
VALUE 28 -> +0xB8
VALUE 29 -> +0xB6
VALUE 30 -> +0xBC
VALUE 31 -> +0xBE
VALUE 32 -> +0xC4
VALUE 33 -> +0xC2
```

Values correlate strongly with character type and enemy archetype, indicating AI/combat-behaviour tuning such as ranges, probabilities, aggressiveness, perception, or attack parameters. Exact meanings must be traced through downstream consumers rather than guessed from values.

## 20.2 Four three-value tuples: `+0xCA..+0xE1`

Serialized physically as three parallel arrays:

```cpp
std::int16_t tupleA[4];  // +0xCA
std::int16_t tupleB[4];  // +0xD2
std::int16_t tupleC[4];  // +0xDA
```

Runtime conceptually accesses index `i` as:

```cpp
{ tupleA[i], tupleB[i], tupleC[i] }
```

so a semantic representation may be:

```cpp
struct CharacterBehaviorTuple {
    std::int16_t a;
    std::int16_t b;
    std::int16_t c;
};

CharacterBehaviorTuple tuples[4];
```

The exact tuple meaning remains unknown.

## 20.3 Four-entry keyed lookup: `+0xE2..+0xF9`

Three parallel arrays:

```cpp
std::int16_t lookupKeys[4];     // +0xE2
std::int16_t lookupValuesA[4];  // +0xEA
std::int16_t lookupValuesB[4];  // +0xF2
```

Runtime performs small linear key searches and returns corresponding values, with default fallback values when no key matches.

The table is meaningful for several enemy archetypes. Some `Incarnable` records contain repetitive large values that may be irrelevant/default data for that type; do not reinterpret those as pointers or IDs without evidence.

---

# 21. Weapon-family parameters and ammunition `+0xFA..+0x10D`

`IAM/GLOBAL`, `OBJECTS.TAG`, and Runtime weapon-selection logic provide a strong mapping.

`IAM/GLOBAL` contains object IDs corresponding to five normal firearm ammunition families and their weapons, including:

```text
Double-Waver
Octogun
Decagun
Megazooka
Hypra
```

## 21.1 Five authored weapon-family parameters: `+0xFA..+0x102`

Physical fields:

```text
+0xFA
+0xFC
+0xFE
+0x100
+0x102
```

Runtime exposes them through indexed/generic CHARACTER value operations. They are stable/authored for a body and do not behave like current ammunition quantities.

Strong working interpretation:

```cpp
std::int16_t weaponFamilyParameters[5];
```

Exact meaning is still unknown; possibilities include capacities, pickup quantities, proficiencies, or another family-specific tuning value. Trace weapon/ammo acquisition and consumption before assigning a narrower name.

## 21.2 Five current ammunition quantities: `+0x104..+0x10D`

Confirmed/very strong mapping:

```cpp
std::int16_t ammunition[5];
```

Order matches the five firearm families:

```text
0  Double-Waver
1  Octogun
2  Decagun
3  Megazooka
4  Hypra
```

Runtime's weapon-cycle/availability logic directly tests these quantities for corresponding weapon families, and generic CHARACTER setters can update them by index.

Real save values vary substantially across a playthrough, confirming mutable ammunition semantics.

---

# 22. `+0x10E`: associated object ID

`character +0x10E` is a signed 16-bit object ID.

Runtime routines:

- compare it against `OBJECTS` IDs;
- assign an object ID;
- reset it to `-1` in several interaction paths.

All 51 inspected save slots happen to store `-1` here, suggesting the association is relatively transient during ordinary gameplay.

Best provisional field name:

```cpp
std::int16_t linkedObjectId;
```

Potential eventual semantics include held object, attached prop, active interaction object, etc. It should not yet be called `weaponId` because weapon selection is represented elsewhere.

---

# 23. Current character ID at `+0x110`

`character +0x110` is a signed 16-bit current-body/character ID.

Examples observed in saves:

```text
Kay'l       49
Syao       138
Itzam'a    414
Jayli'n    291
Bahimy'l   364
Zao'r      330
Iman       433
Enya'd     307
Nioma'y    417
Neme't     416
Fodo       406
Kushulai'n 420
```

This frequently agrees with global variable `22` (`Joueur`) but can differ around body changes. Therefore:

- `currentCharacter.characterId` appears to be the authoritative identity of the copied current-body record;
- global `Joueur` is script-visible state and should not be assumed identical at every instant.

The final two bytes at character `+0x112` remain unresolved.

---

# 24. Working current-character structure

```cpp
struct RetailCurrentCharacter {
    char* signs;                            // +000 runtime-only
    char* interests;                        // +004 runtime-only

    char name[32];                          // +008
    char job[32];                           // +028
    char adventureControlSet[18];           // +048
    char combatControlSet[18];              // +05A
    char sex[8];                            // +06C
    char eyes[8];                           // +074
    char bloodType[4];                      // +07C
    char height[8];                         // +080
    char weight[8];                         // +088
    char model[10];                         // +090

    std::int16_t age;                       // +09A
    std::int16_t mana;                      // +09C
    std::int16_t speed;                     // +09E
    std::int16_t attack;                    // +0A0
    std::int16_t bodyResistance;            // +0A2
    std::int16_t dodge;                     // +0A4
    std::int16_t fightExperience;           // +0A6
    std::int16_t unknownCharacteristicA8;   // +0A8
    std::int16_t energy;                    // +0AA
    std::uint16_t seteks;                   // +0AC
    std::int16_t rings;                     // +0AE

    CharacterType characterType;            // +0B0

    std::int16_t behaviorParam[11];         // +0B4 .. +0C9

    std::int16_t tupleA[4];                 // +0CA
    std::int16_t tupleB[4];                 // +0D2
    std::int16_t tupleC[4];                 // +0DA

    std::int16_t lookupKeys[4];             // +0E2
    std::int16_t lookupValuesA[4];          // +0EA
    std::int16_t lookupValuesB[4];          // +0F2

    std::int16_t weaponFamilyParameters[5]; // +0FA
    std::int16_t ammunition[5];             // +104

    std::int16_t linkedObjectId;             // +10E
    std::int16_t characterId;                // +110
    std::int16_t unknown112;                 // +112
}; // 0x114
```

This is an illustrative semantic map, not a structure that should be memory-mapped directly over disk bytes.

---

# 25. Evidence from the old fan save editor

An old fan-developed save editor (`Savegame Editor v0.1a`) was inspected as corroborating evidence only. Its credits state that save offsets were provided by another contributor, reinforcing that it is an offset-based utility rather than an authoritative specification.

Useful corroboration:

- exact slot arithmetic `0xDA8 + slot * 0x8028`;
- 32-byte slot player name;
- current-character name location;
- stat offsets for Age, Mana, Speed, Attack, Resistance, Dodge, Fight Experience, Energy, Seteks, and Rings;
- direct in-place modification without checksum repair.

The editor deliberately omits the real Runtime-supported `character +0xA8` characteristic and does not understand the rest of the START/global-variable/world-state structure. It should therefore be treated as a narrow trainer-like editor, not a complete save format map.

Every field learned from it should remain secondary to Runtime evidence.

---

# 26. Real-save corpus observations

The supplied completed `IAM/GAMES` contains 51 populated saves for one player. This is valuable because fields can be compared longitudinally rather than only against pristine START.

Confirmed useful observations:

- current-character records change with possession;
- global variable values accumulate/change normally through progression;
- object-ID collections add/remove/duplicate items;
- ammunition changes dynamically;
- `character +0x10E` remains `-1` in all inspected checkpoints;
- the 2-bit state table evolves strongly and non-randomly;
- CHARACTER, ADDRESS, and ZONE bitsets all evolve;
- CHARACTER flags are not simply monotonic discovery flags;
- current character ID can temporarily differ from script global `Joueur`.

The corpus should continue to be used for hypothesis testing, but values in it must never be mistaken for pristine defaults.

---

# 27. Original-save import and migration requirements

OpenNomad should separate three concepts:

```text
Retail serialized representation
        |
        | decode + validate + migrate
        v
Semantic OpenNomad GameState
        ^
        |
        | decode + migrate
OpenNomad-native save versions
```

Recommended principles:

1. **Do not memory-map retail bytes into C++ structs.** Use checked explicit little-endian reads.
2. **Do not retain retail pointers.** Runtime-only pointer fields must be reconstructed semantically.
3. **Keep retail format versions separate from OpenNomad save versions.** Do not invent `OMK_SAVE 0x00010002` for native OpenNomad saves.
4. **Decode old retail versions through migration.** Runtime itself already demonstrates `0x00010000 -> 0x00010001` normalization.
5. **Preserve unknown fields where needed for compatibility/round-trip work**, but do not expose unknown raw memory as the primary gameplay API.
6. **Build New Game from `IAM/START`.** This is the retail canonical initial playthrough state.
7. **Use one semantic GameState after load.** A retail-imported game should not remain in a special legacy runtime mode.

Current OpenNomad New Game implementation copies the recovered ADDRESS bytes
and the three fixed-capacity persistent object-ID collections into that
session-owned state. Native save serialization remains a later step.

Potential architecture:

```text
Save/
  Original/
    OmkSaveHeaderReader
    OmkGameSlotReader
    StartStateReader
    RetailSaveMigration

  GameState
  SaveGame
  NativeSaveReader/Writer
  NativeSaveMigration
```

---

# 28. New Game implications

The correct conceptual New Game flow is now:

```text
main menu -> New Game
        |
        | player enters name
        v
load pristine IAM/START
        |
        | decode into semantic GameState
        v
attach player/save identity
        |
        | interface 29 returns result 1
        v
opcode 0x46 writes completion result to global variable destination
        |
        v
GRID script resumes
        |
        | Interface != 0 branch
        v
Kay'l intro sequence
```

New Game must **not** bypass the scenario script with a hard-coded `start_kayl_intro()` call.

The save/load UI can remain deferred while the persistence foundation is implemented.

---

# 29. Important Runtime functions / addresses

| Address | Role |
|---:|---|
| `0x00410950` | `WinMain`; confirms early persistence ordering |
| `0x0041F4C0` | construct default `OMK_SAVE`; also initializes unrelated runtime globals afterward |
| `0x00409200` | load/validate persisted `OMK_SAVE`; handles at least versions `0x10000` and `0x10001` |
| `0x00409370` | high-score insertion/update inside `OMK_SAVE` |
| `0x0040D950` | serialize live `0x2000` START-derived state into save form |
| `0x0040DB00` | activate/deserialize START-derived state; offsets -> pointers; rebuild runtime views |
| `0x0040E060` vicinity | fresh-session path loading `IAM/START` into a zeroed `0x2000` state buffer |
| `0x00433090` | return `0x00907FE0` save-thumbnail buffer |
| `0x0090E180` | live `OMK_SAVE` base |
| `0x004E6D94` | active START/game-state pointer used by serializer path |
| `0x00907FE0` | 128×96 save-thumbnail buffer |

Additional character/value/object helper addresses have been identified during RE but should be added here with stable names once the remaining behaviour fields are traced and Ghidra symbols are normalized.

---

# 30. Remaining unknowns / next RE targets

Highest-value unresolved items:

## `OMK_SAVE`

- exact meanings of early options `+0x24/+0x25/+0x26/+0x28/+0x30/+0x31`;
- exact identities of the three control tables;
- precise renderer-option meanings at `+0x5A4..+0x5A7`;
- semantics/use of trailing `+0x5A8..+0xDA7`;
- exact first-creation path for `IAM/GAMES`.

## Save-slot metadata

- semantic meaning of slot `+0x20` and `+0x24` dwords.

## START fixed/header state

- `START +0x20..+0x2B` constants;
- exact axis/order and scale for saved position at `+0x2C..+0x34`;
- exact orientation encoding at `+0x38`;
- final `currentCharacter +0x112` field.

## Persistent tables

- resource domain and exact meaning of the 672-entry 2-bit table at `+0x126C`;
- bit semantics for CHARACTER flags;
- bit semantics for ADDRESS flags;
- bit semantics for ZONE flags;
- final semantic names for the 18-, 256-, and 9-entry object collections.

## Current character

- exact semantics of behaviour/AI parameters `+0xB4..+0xC8`;
- exact semantics of the four behaviour tuples `+0xCA..+0xE1`;
- exact semantics of keyed table `+0xE2..+0xF9`;
- exact meaning of five weapon-family authored values `+0xFA..+0x102`;
- precise object relationship represented by `+0x10E`;
- Fight Experience numeric-to-rank mapping;
- whether `+0xA8` is truly abandoned/unused or used in a rare path.

The best next pass is to trace CHARACTER `VALUE` operations `23..33` into actual AI/combat consumers and trace weapon/ammo acquisition paths for `+0xFA..+0x102`.

---

# 31. Source material used so far

Authoritative and corroborating sources include:

- retail `Runtime.exe` — authoritative;
- retail `IAM/START`;
- retail `IAM/OBJECT`;
- retail `IAM/AREA`;
- retail `IAM/SCENE`;
- retail `IAM/DIALOG`;
- retail `IAM/GLOBAL`;
- `VARIABLES.TAG`;
- `OBJECTS.TAG`;
- `ADDRESSES.TAG`;
- `AREAS.TAG`;
- `SCENES.TAG`;
- `ZONES.TAG`;
- one downloaded completed `IAM/GAMES` containing 51 populated checkpoints;
- screenshots of the retail Identity and Characteristics screens;
- `H1AVNT.CTL`, `H1CMBT.CTL`, `F1AVNT.CTL`, `F1CMBT.CTL`;
- unpacked old fan save editor — corroboration only.

`CHARACTERS.TAG` is not present in either the investigated retail installation or demo assets. Runtime nevertheless retains diagnostic/tag-category strings for `CHARACTERS`, so its absence is not currently blocking structural reverse engineering.

The demo executable `Nomad.exe` may be useful later as an independent second implementation/source for structure comparison, but has not yet been required for the mappings recorded here.

---

# 32. Summary

The key recovered model is:

```text
PROCESS CONFIGURATION
=====================

0xDA8 OMK_SAVE
    preferences
    controls
    high scores
    renderer/config options


PER-PLAYTHROUGH STATE
=====================

IAM/START
    |
    | new-game template
    v
0x2000 live mutable GameState snapshot
    |
    +-- current possessed character
    +-- global variables[694]
    +-- area mapping[260]
    +-- persistent 2-bit lifecycle table[672]
    +-- CHARACTER flags
    +-- ADDRESS flags
    +-- ZONE flags
    +-- persistent object-ID collections
    +-- current/linked area
    +-- saved player transform
    |
    | 0x0040D950
    v
save slot state[0x2000]

save slot also contains:
    playerName[32]
    metadata[8]
    128x96x16bpp thumbnail[0x6000]
```

This structure is sufficient to begin designing a real OpenNomad `GameState` and retail-save importer without reproducing Runtime's unsafe pointer-heavy in-memory representation.
