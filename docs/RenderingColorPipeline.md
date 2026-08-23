# Rendering color pipeline

Gameplay rendering uses scene-linear sRGB/Rec.709-primary RGB in an HDR
`GL_RGBA16F` working buffer. Retail-compatible encoded arithmetic is confined
to transient blend-operator stages; there is no encoded whole-scene target.

```text
modern opaque/cutout world, characters, sprites
    -> scene A (GL_RGBA16F, linear HDR)
legacy blended source draws
    -> accumulator (GL_RGBA16, encoded operator state)
current scene + accumulator
    -> alternate scene B/A (GL_RGBA16F), then swap
world-space diagnostics
    -> current linear scene
clamp RGB to [0,1] + exact sRGB OETF
    -> default encoded SDR framebuffer
white fade -> letterbox -> I2D -> ImGui
```

Scene A, scene B, and the accumulator attach the same
`GL_DEPTH24_STENCIL8` renderbuffer. A compatibility stage therefore never
copies depth. It clears only the accumulator color attachment, draws its
sources with depth testing and no depth writes, then performs one portable
OpenGL 4.1 fullscreen composite into the alternate scene target. No
framebuffer fetch, image load/store, interlock, texture barrier, or scene copy
is required.

Only active stages are submitted. Alpha sources accumulate premultiplied
encoded RGB and coverage with `ONE, ONE_MINUS_SRC_ALPHA`; additive and
subtractive amounts accumulate with `ONE, ONE`; darken accumulates a factor
from a white clear with `ZERO, ONE_MINUS_SRC_COLOR`. Sprite stages follow
ascending Runtime bucket bits, including additive `0x2100` before darken
`0x2200`. Decor, character, and sprite streams retain their existing separate
ordering; they are not globally re-sorted.

For an HDR destination `D`, the compositor defines `base = clamp(D, 0, 1)` and
`excess = max(D - base, 0)`. It encodes only `base`. Alpha attenuates excess by
remaining transmittance, additive and subtractive preserve excess, and darken
scales excess by its accumulated factor. This makes SDR behavior match encoded
legacy arithmetic without applying transfer functions to negative values or
discarding HDR energy accidentally.

## Texture semantics

Retail color images have explicit upload policies:

- modern color: `GL_SRGB8_ALPHA8`, automatically decoded while sampling;
- legacy effect: ordinary `GL_RGBA8`, preserving encoded filtering and source
  modulation;
- linear data: ordinary linear storage for numeric data.

The game-color texture abstraction may own modern, legacy, or both GPU
representations made from the same decoded RGBA bytes. World material usage is
derived from opaque/cutout versus blended groups, so unused representations are
not allocated. Dynamic sprite modes may require both. I2D remains a legacy-only
display-space layer outside HDR scene processing.

Modern shaders convert authored vertex/tint RGB from its sRGB-like convention
to linear before modulation and perform lighting in linear light. Alpha is not
transferred. Legacy source shaders deliberately keep encoded texture filtering,
tint, grayscale, and modulation. Alpha stages premultiply RGB by source alpha;
additive and darken stages do not.

## Display boundary

`GL_FRAMEBUFFER_SRGB` and `GL_DITHER` remain disabled. The final display shader
clamps scene RGB to `[0,1]` and applies the exact piecewise sRGB OETF once;
alpha passes through. The encoded white fade follows that transform, so it does
not affect the opaque letterbox or I2D. Startup video and splash presentation
have their own unchanged color paths.

This is an SDR display transform, not a tone mapper. The pipeline does not add
bloom, HDR10 output, deferred rendering, or a G-buffer.
