# Retail FNT bitmap fonts and Runtime text-control grammar

> **Status:** reverse-engineering reference for OpenNomad  
> **Last updated:** 2026-08-27
>
> This document describes both the retail `.FNT` bitmap-font format and the
> inline text-control language interpreted by the Windows retail Runtime. The
> latter is important because authored strings such as IAM/OBJECT subtitles are
> not necessarily plain display text: braces and square brackets can carry
> formatting and selection semantics which must be consumed by the text system
> rather than rendered as glyphs.

## 1. Evidence model

Sources are used in this order:

1. **`Runtime.exe` behavior** — authoritative for FNT selection, glyph metrics,
   inline command dispatch, mutable text state, selectable spans, line layout,
   and rendering effects.
2. **Retail `.FNT` files** — authoritative for bitmap serialization and glyph
   coverage data.
3. **Retail `IAM/OBJECT`** — authoritative corpus evidence for which inline
   commands are actually authored in object descriptions/voice-over subtitles.
4. **Current OpenNomad implementation** — useful for documenting the modern
   decoder/renderer, but subordinate when it differs from Runtime.

Analyzed Runtime build:

```text
File:             Runtime.exe
Supplied file:    Runtime(1).exe
Architecture:     PE32 / i386
Image base:       0x00400000
Linker timestamp: 1999-10-04 20:31:50
SHA-256:          55f7120bfea7891b048c64e3682f3259cdbf2719a43fa24e42254b753c95d2ef
```

Analyzed retail IAM/OBJECT corpus:

```text
Supplied file:    OBJECT(2)
File size:        0x1F5000 bytes
Fixed slots:      1002 * 0x800 bytes
SHA-256:          8f25aca8dc61081781c4f58aec71e26ff0cea39ef5f7ba91d21d3f0edd360965
```

Confidence labels used below:

- **Confirmed — Runtime:** directly established from executable behavior.
- **Confirmed — data:** directly established from retail bytes.
- **Corroborated:** Runtime behavior and authored retail data agree.
- **Strongly reconstructed:** behavior is clear but the original source-level
  name is unavailable.
- **Unknown:** storage/behavior exists but its intended source-level meaning is
  not recoverable from the current evidence.

---

## 2. Retail `.FNT` serialization

Omikron retail `.FNT` files are headerless, byte-indexed bitmap fonts. The
first `0x800` bytes are a table of 256 eight-byte little-endian descriptors:

```text
+0x00  uint16 data_block
+0x02  int16  vertical_offset
+0x04  uint16 width
+0x06  uint16 height
```

For a non-empty descriptor, the bitmap begins at:

```text
data_block * 8
```

and contains:

```text
width * height
```

bytes in top-row-first row-major order.

Each bitmap byte is a five-bit coverage value in:

```text
0 .. 31
```

It is **not** a palette index. The next bitmap begins on an eight-byte boundary.
Empty descriptors carry no bitmap payload.

### OpenNomad decoder behavior

OpenNomad validates:

- the complete 256-entry descriptor table;
- checked dimension and offset arithmetic;
- each bitmap range;
- descriptor-table overlap;
- and every coverage byte.

The GPU atlas stores white RGB with:

```text
alpha = coverage / 31
```

It preserves the top-down source orientation in glyph UVs, inserts one
transparent pixel between glyphs, and uses nearest filtering. This keeps the
five-bit samples exact and prevents inter-glyph bleed.

---

## 3. Runtime font registry

The recovered Runtime registry associates a one-byte font key with a resource
and three logical metrics:

| Key | Resource | Letter spacing | Blank width | Line height |
| --- | --- | ---: | ---: | ---: |
| `I` | `MENUINTR` | +2 | 15 | 36 |
| `M` | `MENUSAVE` | +1 | 8 | 17 |
| `D` | `DIALOGUE` | +1 | 6 | 17 |
| `R` | `DIALSELE` | -1 | 6 | 17 |
| `P` | `PARCHEMI` | +1 | 6 | 17 |
| `C` | `COMPUTER` | 0 | 6 | 14 |
| `S` | `SNEAK` | +1 | 6 | 20 |
| `J` | `JOURNAL` | +1 | 6 | 17 |
| `V` | `VOIXOFF` | +1 | 6 | 23 |
| `1` | `GENERIC1` | +2 | 6 | 12 |
| `2` | `GENERIC2` | +3 | 6 | 18 |
| `3` | `GENERIC3` | +3 | 6 | 24 |
| `L` | `SMALL` | 0 | 6 | 12 |

A non-empty glyph advances by:

```text
width + letter_spacing
```

An empty glyph, including space, advances by:

```text
blank_width + letter_spacing
```

For a logical line beginning at `line_y`:

```text
baseline     = line_y + line_height
glyph_top    = baseline + vertical_offset - height
glyph_bottom = glyph_top + height
```

The dialog keys resolve through the normal case-insensitive game-data path as:

```text
FONTS/DIALOGUE.FNT
FONTS/DIALSELE.FNT
```

Valid retail data is preferred. A missing or rejected file produces a warning
before the `OMIKRON.TTF` fallback is attempted.

### Low-resolution font override

The inline `f` command can request any registry key, but Runtime has a special
low-resolution behavior. In the text-format initialization path around
`0x0043F36C`, if either framebuffer dimension is below:

```text
640 x 480
```

Runtime forces the active font key to:

```text
L = SMALL
```

The `f` command still consumes its one-byte operand, but below that threshold
its requested font does not replace the forced `SMALL` selection.

---

## 4. Retail text is byte-indexed, not UTF-8

Retail strings select:

```text
glyph[unsigned_byte]
```

directly.

A byte such as:

```text
0xE9
```

is one glyph index. It must **not** be decoded as UTF-8 before FNT lookup.

This distinction also applies to the inline text grammar documented below: its
commands and operands operate on authored bytes, not Unicode code points.

The optional TTF fallback retains its own UTF-8 decoder because it is a
separate modern backend.

---

# Runtime inline text-control grammar

## 5. Parser architecture

The central retail text formatter/layout routine begins at:

```text
0x0043F3E0
```

Its surrounding setup routine begins around:

```text
0x0043F180
```

The formatter is not a plain string-to-glyph loop. Outside literal/raw mode it
recognizes two independent pieces of authored control syntax:

```text
{ ... }    inline formatting commands
[ ... ]    selectable/highlighted spans
```

The important model is:

> **brace commands mutate persistent text state; `}` ends command parsing but
> does not restore the previous style.**

For example:

```text
{fD}Hello
```

means:

```text
select font D
render "Hello" using that font until another command changes it
```

It does **not** mean that font D is scoped only to the contents of the braces.

Likewise:

```text
{I255000000}Warning
```

sets the current color to red and leaves that color active afterward.

Multiple commands may be concatenated inside one block:

```text
{HDfR}DATA MEMORIZED
```

Runtime parses `H`, then `D`, then `fR` before returning to ordinary text.

---

## 6. High-level grammar

A useful reconstructed grammar is:

```text
Text :=
    (Glyph | Space | CarriageReturn | Newline | CommandBlock | SelectableSpan)*

CommandBlock :=
    '{' CommandByte* '}'

Command :=
      'B'
    | 'C'
    | 'D'
    | 'E' Byte
    | 'F'
    | 'G'
    | 'H'
    | 'I' Digit Digit Digit Digit Digit Digit Digit Digit Digit
    | 'L'
    | 'M'
    | 'X' Digit Digit Digit Digit Digit Digit
    | 'f' Byte
    | 'g'
    | UnknownByte

SelectableSpan :=
    '[' Text ']'
```

This grammar is descriptive rather than a modern validation contract. Retail
Runtime trusts authored data and does not robustly validate operand length or
that the `I`/`X` operands are actually decimal digits.

Unknown bytes inside a brace block are consumed and otherwise ignored.

---

## 7. Brace command dispatcher

While brace-command mode is active, Runtime dispatches command bytes through a
compact lookup table. The relevant tables are at:

```text
command -> handler-index table: 0x0043FE0C
handler jump table:            0x0043FDD0
```

The table covers byte values through `0x67` (`g`). Every unrecognized value in
that range maps to the default no-effect path.

Exactly thirteen command bytes have dedicated entries:

| Command | Handler | Runtime behavior | IAM/OBJECT use |
| --- | ---: | --- | ---: |
| `B` | `0x0043F8F0` | toggle timed normal-color/red flash | 8 |
| `C` | `0x0043F704` | horizontal center mode | 9 |
| `D` | `0x0043F6F1` | horizontal right mode | 17 |
| `E` | `0x0043F9D8` | consume/store one auxiliary style byte | 0 |
| `F` | `0x0043F715` | horizontal mode `0x10`, left-origin placement, immediate style-resolution path | 0 |
| `G` | `0x0043F6DE` | horizontal left mode | 2 |
| `H` | `0x0043F728` | vertical top mode | 3 |
| `I` | `0x0043F823` | set RGB color from 9 decimal characters | 158 |
| `L` | `0x0043F747` | vertical bottom mode | 0 |
| `M` | `0x0043F7AD` | vertical middle mode | 0 |
| `X` | `0x0043F90A` | set absolute screen position from percentages | 133 |
| `f` | `0x0043F8AF` | select one-byte font key | 381 |
| `g` | `0x0043FDAB` | recognized no-op | 0 |

`IAM/OBJECT use` is the number of command invocations found in the supplied
retail `IAM/OBJECT` archive, not a claim about every resource in the game.

---

## 8. `{f<key>}` — select bitmap font

**Status:** Confirmed — Runtime; Corroborated by retail IAM/OBJECT.

Handler:

```text
0x0043F8AF
```

Syntax:

```text
{fD}
{fR}
{f1}
```

Lowercase `f` consumes exactly one following byte and, at 640x480 or above,
stores it as the current Runtime font key.

For example:

```text
{fD}
```

selects:

```text
DIALOGUE.FNT
```

### IMPASSE worked example

IAM/OBJECT slot 141 contains:

```text
audio stem: ZVOM010
subtitle:   {fD}You have been the victim of a violent attack. Go home, eat and
            rehydrate yourself. If you experience mental disturbances, consult
            a Psytech. The Omikron Police thank you for your cooperation.
```

Therefore the visible subtitle begins with:

```text
You have been the victim ...
```

and `{fD}` is presentation markup, not display text.

OpenNomad must not render `{`, `f`, `D`, or `}` as glyphs.

### Authored inline font switching

IAM/OBJECT also uses font changes within ordinary prose, for example:

```text
A Pass to enter the {fS}Jaunpur{fJ} district
```

This demonstrates that font changes are persistent state changes embedded in
one logical string rather than metadata that applies only to the entire text
object.

---

## 9. `{Irrrgggbbb}` — set RGB text color

**Status:** Confirmed — Runtime; Corroborated by retail IAM/OBJECT.

Handler:

```text
0x0043F823
```

Syntax:

```text
{Irrrgggbbb}
```

where each component is written as three ASCII decimal characters.

Examples:

```text
{I255255255}   white
{I255000000}   red
{I000255000}   green
{I000000255}   blue
{I128128128}   medium gray
```

The handler manually decodes three characters per channel and stores the low
byte of each result as the active R, G and B values.

Runtime does not perform modern bounds or syntax validation here. OpenNomad
should preserve valid retail semantics while rejecting/truncating malformed
input safely rather than reproducing unsafe reads.

Retail IAM/OBJECT contains many examples combining color and font selection,
including police records, notes, prescriptions, and other readable documents.

---

## 10. Horizontal alignment modes: `G`, `D`, `C`, `F`

Runtime stores horizontal layout as mutually exclusive bits within the text
style flags:

```text
G -> 0x02
D -> 0x04
C -> 0x08
F -> 0x10
```

The final line-placement switch around `0x0043FBF5` treats them as follows:

```text
G / 0x02:
    line_x = left_origin

D / 0x04:
    line_x = right_bound - line_width

C / 0x08:
    line_x = left_bound + (right_bound - left_bound - line_width) / 2

F / 0x10:
    line_x = left_origin
```

### `{G}` — horizontal left

**Status:** Corroborated.

Handler:

```text
0x0043F6DE
```

`G` selects left-origin placement.

The likely mnemonic is French `gauche`, but that source-level naming is not
proven by machine code and should not be encoded as an authoritative symbol
name.

Retail credits provide a useful authored example where `{G}` explicitly
returns text to the left side after right-oriented content.

### `{D}` — horizontal right

**Status:** Corroborated.

Handler:

```text
0x0043F6F1
```

`D` right-aligns each resolved line against the current right boundary.

The likely mnemonic is French `droite`, but, again, that spelling is an
interpretation rather than a recovered source symbol.

Retail credits use this repeatedly, for example:

```text
{X090058}{f1}{D}Programming Directors
{X080065}{f3}{D}Olivier NALLET
{X075075}{f3}{D}Fabien FESSARD
```

### `{C}` — horizontal center

**Status:** Corroborated.

Handler:

```text
0x0043F704
```

`C` centers a resolved line in the current horizontal region.

Retail pickup notifications use patterns such as:

```text
{fRC}LARGE MEDIKIT PICKED UP: LIFE + 100
```

which means:

```text
fR -> select DIALSELE.FNT
C  -> horizontal center
```

### `{F}` — fourth horizontal mode

**Status:** Confirmed — Runtime behavior; source-level meaning unknown.

Handler:

```text
0x0043F715
```

`F` selects horizontal mode bit:

```text
0x10
```

The final line-position switch places mode `0x10` at the same left origin as
`G`; it is **not** full justification in this retail Runtime.

There is nevertheless one control-flow distinction: unlike the `G`, `D`, and
`C` handlers, `F` branches immediately to the style-change/line-resolution path
at `0x0043FB40` instead of directly to the normal continuation at
`0x0043FDAB`. If pending buffered text exists, this gives `F` an immediate
format-boundary/flush behavior that the other horizontal commands do not have
at the point the command is parsed.

A safe descriptive name is therefore:

```text
fourth horizontal mode / left-origin mode with immediate style resolution
```

Do **not** call it `justify` unless new evidence establishes that meaning.

The retail IAM/OBJECT corpus contains zero `{F...}` uses.

---

## 11. Vertical alignment modes: `H`, `L`, `M`

Runtime uses a separate set of mutually exclusive vertical style bits:

```text
H -> 0x0400
L -> 0x0800
M -> 0x1000
```

### `{H}` — vertical top

**Status:** Corroborated.

Handler:

```text
0x0043F728
```

`H` selects the top/high vertical mode and resets the current vertical origin
to the top of the active layout region.

Retail IAM/OBJECT uses it three times, including:

```text
{HDfR}DATA MEMORIZED
```

### `{L}` — vertical bottom

**Status:** Confirmed — Runtime.

Handler:

```text
0x0043F747
```

`L` selects the bottom/low vertical mode. Runtime positions the starting line
from the lower bound while accounting for the current font's line height.

No invocation occurs in the supplied IAM/OBJECT corpus.

### `{M}` — vertical middle

**Status:** Confirmed — Runtime.

Handler:

```text
0x0043F7AD
```

`M` centers the initial line vertically between the active top and bottom
bounds.

No invocation occurs in the supplied IAM/OBJECT corpus, although Runtime itself
contains internally formatted strings combining center and middle modes.

---

## 12. `{Xxxxyyy}` — absolute screen position

**Status:** Corroborated.

Handler:

```text
0x0043F90A
```

Syntax:

```text
{Xxxxyyy}
```

`xxx` and `yyy` are each parsed from three ASCII decimal characters. Runtime
then computes the position as a percentage of the complete framebuffer:

```text
x = xxx * framebuffer_width  / 100
y = yyy * framebuffer_height / 100
```

Conceptually:

```text
{X000000}   top-left
{X050050}   center
{X100100}   bottom-right
```

The parsed values are narrowed to bytes before the multiply/divide path, and
Runtime does not robustly validate malformed values. Valid authored data uses
the intended percentage-like form.

Retail credits rely heavily on `X`, which is why the IAM/OBJECT corpus contains
133 uses across 44 object records.

Example:

```text
{X010030}{f1}Written & Directed by
{X020035}{f3}David CAGE
```

`X` is absolute with respect to the framebuffer, not relative to the current
text box.

---

## 13. `{B}` — timed normal-color/red flash toggle

**Status:** Corroborated.

Handler:

```text
0x0043F8F0
```

`B` toggles style bit:

```text
0x4000
```

During glyph-run construction, Runtime checks that bit around:

```text
0x0043FADB
```

If active, it queries Runtime timer slot 1 through:

```text
0x0042B5E0(1)
```

and examines the timer's state at `+0x18`.

The static timer table begins at:

```text
0x004C3EA0
```

with a `0x28`-byte stride. Timer slot 1 has a period field of:

```text
500
```

When the timer state is false/zero, the configured text RGB is used. When the
state is nonzero, Runtime substitutes:

```text
255, 0, 0
```

Therefore `{B}` is not a visibility blink. It is a timed **normal-color ↔ red flash**.

Because `B` XORs the flag, a second `{B}` turns the effect back off.

Retail IAM/OBJECT confirms the toggle interpretation. One police memo contains:

```text
{B}{I225120045}Attention: {B}{I225225225} these individuals are dangerous ...
```

The first `B` enables the flash for `Attention:`; the second disables it before
the following text.

---

## 14. `{E<byte>}` — auxiliary/legacy style byte

**Status:** Confirmed — Runtime storage behavior; presentation meaning absent in
this retail build.

Handler:

```text
0x0043F9D8
```

Syntax:

```text
{E<byte>}
```

`E` consumes exactly one following raw byte and stores it in Runtime text state:

```text
0x00907A28
```

This is real style state rather than an accidental parser artifact:

- the text formatter initializes it to zero;
- the outer style descriptor can initialize it from descriptor `+0x14`;
- entering the selected `[...]` span saves it;
- the selected-span alternate style can replace it from descriptor `+0x3C`;
- leaving the selected span restores the prior value;
- `{E<byte>}` explicitly updates it.

However, exhaustive direct references to `0x00907A28` in this Runtime are
confined to those initialization/save/restore/write operations inside the text
formatter. No rendering, measurement, font, color, positioning, wrapping, or
glyph-raster path consumes the value.

Accordingly, the best current model is:

```text
legacy / auxiliary one-byte text-style state
```

whose original intended feature is absent or disabled in the retail executable.

The supplied retail IAM/OBJECT archive contains:

```text
0 uses of E
```

OpenNomad should still consume the operand and preserve the state if it models
the grammar faithfully; it should not invent a visual effect.

---

## 15. `{g}` — recognized no-op

**Status:** Confirmed — Runtime.

Dispatch target:

```text
0x0043FDAB
```

Lowercase `g` has a dedicated dispatcher entry but performs no state change and
consumes no operand beyond the command byte itself.

This distinguishes it from an arbitrary unknown byte at the table level even
though both are visually inert in the retail build.

The supplied IAM/OBJECT corpus contains zero uses.

---

## 16. Unknown bytes inside `{...}`

Bytes without dedicated command entries are not displayed while command mode
is active. They are consumed and routed through the default no-effect path.

Runtime does not report an error.

This permissive behavior matters because retail authored data contains at
least one sequence that appears malformed when interpreted according to the
recovered grammar.

IAM/OBJECT slot 423 begins:

```text
{fI225120045}
```

The actual parser reads this as:

```text
fI           select font key I
2            unknown command byte -> ignored
2            ignored
5            ignored
1            ignored
2            ignored
0            ignored
0            ignored
4            ignored
5            ignored
```

There is **no** `I225120045` color command because the `I` byte was consumed as
`f`'s font-key operand.

This strongly resembles an authoring typo for something like:

```text
{fI}{I225120045}
```

but Runtime tolerates it by swallowing the extra bytes.

A compatibility parser should therefore distinguish:

```text
valid-but-unknown command byte -> consume silently
unsafe/truncated operand       -> handle safely in OpenNomad
```

rather than turning the language into a strict modern markup validator.

---

# Selectable spans

## 17. `[...]` is a second control mechanism

Square brackets are handled outside brace-command mode and have separate
semantics.

Runtime keeps:

```text
selected_span_index
current_span_counter
```

The current span counter starts at:

```text
-1
```

Each `[` increments it:

```text
first '['  -> span 0
second '[' -> span 1
third '['  -> span 2
...
```

If the new counter equals the externally supplied selected-span index, Runtime:

1. saves the current RGB, font key, style flags, and auxiliary `E` state;
2. installs an alternate/highlight style supplied by the caller's style
   descriptor;
3. formats the span using that alternate state.

At the matching `]` for the selected span, Runtime restores the saved primary
style.

Neither bracket is rendered as a glyph.

### Flat selection model, not general nesting

`[` increments the span ordinal, but `]` does not decrement it. The mechanism
is therefore designed for a flat sequence of selectable spans rather than a
general recursively nested markup language.

Conceptually:

```text
[First choice][Second choice][Third choice]
```

can select one ordinal and apply the alternate style only to that span.

### Reconstructed alternate-style fields

When selectable-span support is enabled in the outer text-style descriptor,
Runtime reads:

```text
+0x2C  selected span index
+0x30  alternate R byte
+0x31  alternate G byte
+0x32  alternate B byte
+0x34  alternate font key / font selector value
+0x38  alternate style flags
+0x3C  alternate E/auxiliary style value
```

The exact complete source structure and field names remain reconstructed, but
the selected-span save/swap/restore behavior is direct Runtime evidence.

---

# Literal/raw mode and ordinary characters

## 18. Raw/literal text mode

The outer style descriptor has flag:

```text
0x00010000
```

When set, the setup routine passes a nonzero literal/raw-mode argument to the
formatter.

In that mode, the special recognition of:

```text
{ }
[ ]
```

is disabled and those bytes proceed through ordinary text handling.

This is Runtime's whole-call escape mechanism. There is no evidence for an
inline backslash-style `\{` escape grammar.

---

## 19. Ordinary control bytes

Outside brace-command mode and outside literal/raw mode:

```text
0x00  NUL  terminates the string
0x0D  CR   ignored as a rendering character
0x0A  LF   explicit line break
0x20  SP   ordinary blank advance and a word-wrap opportunity
```

Other bytes are treated as byte-indexed glyph values when they survive the
control-syntax checks.

Line wrapping uses glyph advances and remembers space boundaries so a pending
word can be moved to the next line rather than splitting whenever possible.

---

# Text-style state

## 20. Confirmed mutable presentation state

The inline parser mutates the same presentation state that can be initialized
by the caller's text-style descriptor. Confirmed state includes:

```text
RGB color
font key
horizontal mode
vertical mode
blink/flash flag
absolute/current positioning state
selected-span state
auxiliary E byte
```

This is why inline commands must be parsed before or during layout rather than
removed by a preprocessing function that returns only plain text.

For example, this string:

```text
A Pass to enter the {fS}Jaunpur{fJ} district
```

requires at least three styled runs if represented in a modern retained model:

```text
run 0: current font  -> "A Pass to enter the "
run 1: font S        -> "Jaunpur"
run 2: font J        -> " district"
```

Simply stripping `{fS}` and `{fJ}` would preserve the visible characters but
lose the authored presentation.

---

# Retail IAM/OBJECT corpus

## 21. Archive properties relevant to text

The supplied retail IAM/OBJECT archive contains:

```text
1002 fixed slots
slot stride:          0x800
serialized record:    0x518
subtitle/text offset: +0x118
```

Scanning all NUL-terminated text fields at `+0x118` produced:

```text
non-empty text fields:              940
objects containing brace markup:    233
well-formed {...} blocks:            618
unterminated brace blocks:             0
```

### Command frequency

Parsing those 618 blocks with Runtime's operand widths gives:

| Command | Invocations | Objects containing command |
| --- | ---: | ---: |
| `f` | 381 | 229 |
| `I` | 158 | 88 |
| `X` | 133 | 44 |
| `D` | 17 | 13 |
| `C` | 9 | 9 |
| `B` | 8 | 6 |
| `H` | 3 | 3 |
| `G` | 2 | 2 |
| `E` | 0 | 0 |
| `F` | 0 | 0 |
| `L` | 0 | 0 |
| `M` | 0 | 0 |
| `g` | 0 | 0 |

This distinguishes two different facts which should not be conflated:

1. Runtime **supports** thirteen dedicated command bytes.
2. Retail IAM/OBJECT actually **authors** only eight of them.

In particular, the absence of `E` and `F` from this corpus is strong evidence
against inventing gameplay-specific meanings for them merely to explain
OBJECT subtitles.

It does **not** prove that no other retail resource ever uses those commands.

---

## 22. Worked retail examples

### IMPASSE voice-over subtitle

```text
object ID: 141
audio:     ZVOM010
text:      {fD}You have been the victim of a violent attack. ...
```

Meaning:

```text
fD -> DIALOGUE.FNT
```

The markup must not be visible.

### Centered pickup notification

```text
{fRC}LARGE MEDIKIT PICKED UP: LIFE + 100
```

Meaning:

```text
fR -> DIALSELE.FNT
C  -> horizontal center
```

### Top/right notification

```text
{HDfR}DATA MEMORIZED
```

Meaning:

```text
H  -> vertical top
D  -> horizontal right
fR -> DIALSELE.FNT
```

### Credits positioning and right alignment

```text
{X090058}{f1}{D}Programming Directors
{X080065}{f3}{D}Olivier NALLET
{X075075}{f3}{D}Fabien FESSARD
```

This independently corroborates both percentage-like `X` positioning and
right alignment for `D`.

### Explicit return to left alignment

One credits object contains both:

```text
{G}Text Advisor: Paul Glancey
...
{D}Additional designs, Ideas and Unlimited Support:
```

which corroborates `G` as the left-oriented counterpart to `D`.

### Timed flashing warning

```text
{B}{I225120045}Attention: {B}{I225225225} these individuals are dangerous ...
```

The paired `B` commands corroborate toggle semantics rather than a one-way
"enable" command.

---

# OpenNomad implementation requirements

## 23. Do not strip markup in IAM parsers

IAM resource parsers should preserve the authored byte string.

For IAM/OBJECT specifically, `IamObjectRecord` should continue to expose the
NUL-terminated field beginning at `+0x118` without deleting `{...}` sequences.
The markup belongs to the text presentation layer, not to archive parsing.

This matters because the same string can contain multiple font/color/layout
changes and because malformed-but-tolerated retail markup must remain
observable to the compatibility parser.

---

## 24. OpenNomad content and presentation representation

OpenNomad parses authored bytes into `RuntimeTextDocument`. The document owns
the original byte string and a presentation-neutral event stream containing
visible byte ranges, explicit line breaks, persistent style changes, absolute
positions, format boundaries, and selectable-span boundaries. Plain visible
bytes can be derived without destroying the authored document.

```text
authored retail bytes
    |
    v
RuntimeTextDocument + events
    |
    +------------------------------+
    |                              |
    v                              v
RuntimeTextLayout              future modern processing
    |                       (derived text/cues only)
    v
faithful FNT presenter
```

`RuntimeTextLayout` is CPU-only and walks the events with mutable
`RuntimeTextStyle`. Font metrics are injected by key and byte, which permits
mixed-font lines and tests without OpenGL. `I2DRenderer` supplies metrics and
glyph textures from the existing `FontManager` registry and resolves the
500 ms flash phase from explicit presentation time.

The important Runtime property remains:

```text
commands mutate persistent state
```

The parser should support, at minimum:

```text
{B}
{C}
{D}
{E<byte>}
{F}
{G}
{H}
{I#########}
{L}
{M}
{X######}
{f<byte>}
{g}
unknown brace bytes
[...]
raw/literal mode
CR/LF/space behavior
```

---

## 25. Safety versus compatibility

Retail Runtime trusts authored strings and may read operand bytes without
modern bounds checks. OpenNomad should reproduce semantics for valid data but
must remain memory-safe.

Recommended behavior for malformed input:

```text
unknown command byte:
    consume silently, matching Runtime

truncated f/E operand:
    stop/ignore command safely; never read beyond the string

truncated I/X numeric operand:
    stop/ignore command safely; never read beyond the string

non-decimal I/X operand:
    retain a deliberate compatibility policy and diagnostic if useful;
    do not perform unchecked arithmetic on arbitrary bytes

unclosed '{':
    fail safely or treat remaining bytes according to a documented fallback;
    retail IAM/OBJECT contains no such case
```

Do not "repair" authored strings globally. Object 423 demonstrates that retail
content can contain odd-looking markup which Runtime tolerates predictably.

---

## 26. World-text presentation consequence

IAM/OBJECT text passes through the Runtime-compatible parser before FNT layout.
This facility is general world text rather than intrinsically a spoken
subtitle: retail uses it for positioned credits, notifications, documents,
and cinematic overlays as well as voice-over text.

The original bug that exposed this grammar was the IMPASSE subtitle:

```text
{fD}You have been the victim ...
```

The old path passed the raw field directly to a single-font renderer, causing
the markup bytes to appear as visible glyphs. The shared Runtime parser now
consumes `fD`, emits a font-state event, and leaves the visible text beginning
at `You`; the original `{fD}...` bytes remain in the document.

This also prevents future failures in object descriptions, pickup messages,
credits, police documents, and other strings which use `I`, `X`, alignment,
and `B` controls.

`WorldTextCommand` carries source kind, object ID, audio resource, semantic
role, and modernization policy beside the authored document. Current
IAM/OBJECT requests use an unknown role and faithful-only policy; audio
presence is not treated as proof of speech. This lets a future optional modern
presenter derive cues from the same intact document while conservatively
leaving unclassified credits and overlays faithful.

---

# Remaining unknowns

## 27. `F` source-level mnemonic

Runtime behavior is recovered:

```text
horizontal bit 0x10
left-origin final placement
immediate branch through style/line resolution
```

What the original letter `F` stood for is not known. It should remain
semantically neutral in documentation/code naming unless another source (IAM
editor documentation, debug string, symbol, or authored use) establishes the
original concept.

## 28. `E` intended feature

Runtime faithfully carries the `E` byte through style initialization,
selected-span save/alternate/restore, and inline updates, but this retail
executable contains no downstream consumer of the value.

Its intended historical/editor feature therefore remains unknown.

For compatibility purposes the important behavior is already sufficient:

```text
consume one byte
store it as auxiliary style state
produce no visual effect in this Runtime build
```

## 29. `g` historical purpose

Lowercase `g` is explicitly recognized but is a no-op in the retail executable.
Its historical purpose, if any, is unknown.

---

# Quick reference

| Syntax | Meaning in retail Runtime |
| --- | --- |
| `{B}` | toggle timer-driven current-color/red flash |
| `{C}` | horizontal center |
| `{D}` | horizontal right |
| `{E?}` | set one-byte auxiliary style state; no retail rendering consumer |
| `{F}` | horizontal mode `0x10`; left-origin placement + immediate style resolution |
| `{G}` | horizontal left |
| `{H}` | vertical top |
| `{Irrrgggbbb}` | set RGB color from three decimal characters per channel |
| `{L}` | vertical bottom |
| `{M}` | vertical middle |
| `{Xxxxyyy}` | set absolute X/Y as percentages of framebuffer dimensions |
| `{f?}` | select one-byte FNT registry key |
| `{g}` | recognized no-op |
| `{unknown}` | unknown command bytes are swallowed while command mode is active |
| `[text]` | selectable-span ordinal; selected span temporarily receives alternate style |
| style flag `0x10000` | disable brace/bracket interpretation for the whole text call |
| `CR` | ignored |
| `LF` | explicit line break |
| `NUL` | end of string |
