# Runtime 3DM dialogue performances

`MORPH/<basename>.3dm` is a synchronized dialogue-performance package. A
single stream contains object rotations, root-local translation, face source
vertices and compressed mono speech. IAM/DIALOG supplies the basename; it is
not a `VOICE/*.ADP` reference.

## Header and physical records

All integers and floats are little-endian:

```text
+00 u32 packed audio/mode   high 8 bits mode, low 24 audio bytes/record
+04 u32 morph vertex count maximum 200
+08 u32 neutral field      observed values describe neither physical stream
+0c u32 object count       maximum 30
+10 u32 object_ids[count]
```

Only mode zero is supported. A mode-zero motion record is
`object_count * 16 + 12 + morph_count * 24` bytes. A full record adds the
packed audio-byte count. Physical records are discovered from EOF: zero
remainder means all full records; a remainder exactly equal to the motion size
is one valid final motion-only record. Any other remainder is malformed.
`field_08` is retained for diagnostics and never used as a frame count.

## Motion serialization and binding

Each authored object slot stores a WXYZ quaternion. The root slot additionally stores XYZ immediately *before* its quaternion. The slot is not fixed: resolve the target 3DO root mesh, take its `MeshDescriptor::script_id`, and require one exact match in the 3DM object-ID array. Every other object ID likewise requires one exact model `script_id` match. Mesh ordinal and `mesh_id` are not fallbacks.

Quaternions must be finite and nonzero and are normalized before use. Root XYZ is converted to a per-frame delta from frame zero and applied only to the temporary model-local root object; it never changes the character's AREA/world transform.

Each morph record is position XYZ followed by normal XYZ. The target is the unique 3DO mesh carrying `MeshFlags::k_face_morph`, and its vertex count must equal the 3DM morph count. Face-local index `i` overrides global source vertex
`face.vertex_base + i` before object transforms and polygon expansion. Colour,
UV-related corner data and all immutable shared model data remain unchanged.
Missing, ambiguous, or mismatched face geometry disables only facial morphing; object motion and voice can continue.

## Embedded dialogue codec

Mode-zero audio is one continuous custom IMA-family stream. Decoder state starts once per clip at predictor 0 and index 0 and remains continuous across record chunks. Each byte is decoded high nibble then low nibble. The standard 89-entry IMA step table and index deltas are used, but Runtime's difference is:

```text
diff = ((bit2 ? step*4 : 0) +
        (bit1 ? step*2 : 0) +
        (bit0 ? step   : 0)) >> 2
```

There is no unconditional `step / 8` baseline. Bit 3 selects subtraction;
predictor clamps to signed 16-bit and index clamps to 0..88. The mono predictor sample is duplicated to stereo signed-16 PCM at 22080 Hz. Thus an observed 368-byte chunk produces 736 stereo sample frames, 1472 `int16_t` values, or 2944 bytes. A final motion-only record contributes no audio.

## Runtime composition and timing

Frame zero and the continuous voice start together. Visual samples run at 30 Hz using `floor(elapsed_seconds * 30)` and direct frame selection, so dropped render frames skip stale authored samples instead of stretching playback. A final motion-only sample remains visible for its full 1/30-second interval.

Ordinary SCX body animation advances first. While 3DM playback is active, the current 3DM sample is composed afterward over that instance-local pose.

Runtime's 3DM stop routine does not restore the character's object-animation matrices. It restores only the backed-up facial vertex array. Consequently the last streamed body/root pose remains held after media EOF until another performance or body operation overwrites it. OpenNomad mirrors this without mutating its underlying SCX pose by retaining and reapplying the final body-only 3DM sample after ordinary body animation updates.
automatic-player lines, choices, session reset, world replacement, and early skip remove the visual overlay and stop the dedicated nonspatial dialogue lane.
Media completion does not acknowledge or advance the dialog graph.
