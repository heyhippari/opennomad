# Omikron `IAM/GLOBAL` camera subset

`IAM/GLOBAL` contains several independent session-wide sections. Only its
camera-relevant subset has been recovered and implemented; the sections
referenced by header fields `+0x08`, `+0x0C`, and `+0x10` remain intentionally
unnamed.

## Camera header fields

The serialized header is at least `0x20` bytes:

| Offset | Type | Meaning |
|---:|---|---|
| `+0x14` | `uint32` | file-relative camera-table offset |
| `+0x1E` | `int16` | signed camera count |

The native loader at `Runtime.exe 0x0040DE60` relocates the dwords at `+0x08`,
`+0x0C`, `+0x10`, and `+0x14`. OpenNomad does not reproduce that in-place
pointer relocation: it validates the relative camera span and owns immutable
parsed records.

A negative camera count is malformed. A zero count permits an empty table.
For a positive count, the complete table must fit in the file, but it need not
end at EOF.

## Camera records

GLOBAL uses the same `0x2C` `IamCameraRecord` ABI as AREA and SCENE table 6.
The signed camera ID is at record `+0x18`; vectors, type, roll, horizontal FOV,
attachment selectors, and tail fields retain their ordinary shared meanings.
Duplicate IDs are not deduplicated: linear lookup returns the first serialized
record.

The supplied retail positive control is `0x1A68` bytes, with four cameras at
`0x19B8` (IDs `0`, `6`, `11`, and `35`). These values describe that sample,
not parser constants. Camera 11 uses selector 1, whose live attachment behavior
remains outside the implemented selector subset.

## Compact camera definition namespace

The native resolver at `Runtime.exe 0x0040B220` searches exactly:

1. resident slot 0 AREA cameras;
2. resident slot 0 attached SCENE cameras;
3. resident slot 1 AREA cameras;
4. resident slot 1 attached SCENE cameras;
5. `IAM/GLOBAL` cameras.

The first matching ID wins. Neither the calling compact context nor the active
resident slot reorders this namespace. The caller still owns presentation:
its world scene/generation, AREA ID, operation generation, captured camera
participants, lifetime, and tracked state-7 completion are retained even when
the immutable definition comes from another resident or GLOBAL.

GLOBAL is therefore a definition fallback, not a separate camera command or
controller family. AREA/SCENE records intentionally shadow matching GLOBAL
IDs according to the fixed order above.
