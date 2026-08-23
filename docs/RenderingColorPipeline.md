# Rendering color pipeline

OpenNomad preserves Omikron's retail composition math inside an explicit
legacy compatibility boundary, then exposes a modern linear-light scene target.

## Frame flow

`WorldScene` renders at drawable pixel resolution in this order:

```text
retail RGBA8 textures (legacy encoded, bilinear)
    -> GL_RGBA16 legacy encoded target
       world -> white fade -> letterbox -> I2D
    -> exact standard sRGB EOTF
    -> GL_RGBA16F linear scene target
       -> OpenNomad developer overlays
    -> exact standard sRGB OETF
    -> ordinary default framebuffer
    -> ImGui/debug UI
```

The legacy target has `GL_DEPTH24_STENCIL8`. Its normalized color storage
clamps blend results to `[0,1]`, preserving saturation semantics while using
more precision than Runtime's display target. Runtime dithering is
intentionally omitted because the high-precision target makes it obsolete.
OpenGL dithering is explicitly disabled rather than left at its default state.
The linear target retains a matching depth/stencil attachment solely so the
existing depth-aware geometry wireframe can remain a modern developer overlay
after legacy decode. Depth/stencil is copied at the boundary; retail particles
and layers are never recomposed in linear light.

`GL_FRAMEBUFFER_SRGB` remains disabled throughout these passes. The first
fullscreen shader converts only RGB from encoded to linear with the standard
piecewise sRGB EOTF. The second converts only RGB back with the standard
piecewise sRGB OETF. Alpha passes through unchanged, and manual output encoding
is never combined with automatic framebuffer encoding.

## Texture semantics

`TextureColorEncoding` distinguishes three meanings:

- `k_srgb`: sRGB bytes stored as `GL_SRGB8_ALPHA8`; sampling automatically
  decodes RGB.
- `k_linear`: linear numeric data stored in a non-sRGB format.
- `k_legacy_encoded`: display/sRGB-like numbers stored in `GL_RGBA8` so
  sampling does not decode them.

Retail 3DO/3DT, character, embedded sprite, and I2D bitmap textures use
`k_legacy_encoded`. Retail 3D and sprite textures use `GL_LINEAR` minification
and magnification, so interpolation occurs directly on encoded RGB exactly as
it did in Runtime's fixed-function renderer.

"Linear format" and "linear-light values" are deliberately separate concepts.
`GL_RGBA16` is a non-sRGB format whose numbers are semantically legacy encoded;
`GL_RGBA16F` is the canonical modern linear-light scene representation.

## Confidence boundary

Runtime's lack of sRGB transfer states, bilinear filtering, encoded-domain
blend behavior, sprite bucket traversal, diffuse alpha, and dithering are
recovered Runtime facts. Treating authored RGB as sRGB-like at the boundary,
the two high-precision target formats, exact standard sRGB transfers, and
omitting dithering are modern OpenNomad reconstruction choices.

Startup FMV playback, the splash presenter, and ImGui stay outside the legacy
target. The splash shader performs its own explicit display encoding; decoded
video remains a display-referred passthrough.
