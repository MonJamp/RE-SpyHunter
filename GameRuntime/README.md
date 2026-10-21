# GameRuntime

`VkfTextureFont` reads the observed `vkTextureFont`/PSMT8 entries from a PCK archive, converts their indexed atlas and glyph metrics to an SDL texture, and draws text using the game's bottom-origin coordinates. `drawTextScaled` preserves independent horizontal and vertical text scales. It supports the 3.0.87 and 3.0.79 256 × 256 layouts with 16-bit glyph records used by `euroStile14.vkf` and `euroStile12.vkf`, plus the compact 3.0.56 128 × 128 layout with 32-bit glyph records used by `Eurobet.vkf`. All three observed variants contain four control and 95 printable ASCII records. Other VKF layouts require separate decoding before use.

`GameRuntime` is the shared native reconstruction library. Executables such as
`Tools/SHViewer` and `RE-SpyHunter` own application startup, configuration,
the SDL window, and top-level state selection. This library owns reusable game
presentation and runtime services.

## Current API

`StillTexture` owns an SDL still texture, uploads validated RGBA data, loads a standalone VKI or a member from an existing PCK index, and aspect-fits it into a caller-supplied rectangle with linear filtering. Failed replacements retain the prior texture. Whole-image/member decode is deliberate for stills; it never loads the whole archive. Destroy textures before the renderer.

`GamepadSet` owns controller handles, discovers existing pads and handles hotplug. Game-specific button mappings remain in the application/sequence; destroy the set before SDL shutdown.

`CutscenePlayer.h` exposes `CutsceneSession` for non-blocking playback and
`playCutscene(renderer, options)` as a blocking wrapper. Options contain a source
PSS path and the Escape behavior; no writable movie directory is needed. Video
uses a file-backed Media Foundation byte stream with MPEG content type, while
`PssAudioReader` reads private-stream PCM packets through a bounded input buffer.
The target's 512-byte left and right channel blocks are converted to alternating
stereo samples before being queued to SDL. The SDL audio queue is refilled incrementally during `update()`. The session
handles timing and aspect-fit rendering. `Log.GameRuntime=debug` enables open,
queue, completion, and error diagnostics; `trace` adds refill and frame details.
Silent, extension-resolved host video and looping are optional; FrontendMenu uses
that path for the local MBACK conversion without opening an audio device.

`PckArchive.h` exposes a read-only archive index (`open`, `entries`) and a
bounded `readEntry` method for one serialized resource. It scans PCK metadata
in 64 KiB blocks without loading every resource body. Entry boundaries are
inferred from the next validated header. Indexed records retain the serialized
type version as well as source name, offset, and inferred size. `VkiImage.h` decodes standalone VKIs
and supported VKI members read from a PCK.

`TrackGeometry.h` exposes a bounded PG1 track fixture reader for the observed
PCK member `new_provinggrounds.trk` (outer 3.0.85, internal BSP v7). It retains
source XYZ, RGBA bytes, UVs, strip boundaries, local texture reference, and
archive, batch-header, and vertex-stream offsets for one selected leaf/batch.
The selected stream's observed 24-byte vertex stride is exposed for source
correlation. Counts, stream extents,
texture references, finite floats, strip sums, collision extents, and decoded
memory are checked before geometry is returned. It intentionally rejects
other track versions and does not decode `.vk` models. The current viewer uses
this as an inspection tool; no original camera, material, BSP visibility, or
texture presentation parity is claimed.

`RgbaImage.h` decodes serialized `vkDMATexture` `.rgba`/`.rgb` resources from
bytes or a standalone file. It follows the original version-dependent reader
for palettes and mip data, supporting PSMT4, PSMT8, PSMCT32 and RGB5A1
PSMCT16/16S previews. It returns the first palette/base mip as RGBA32, validates
all stored mip extents, reverses raster rows for the frontend convention,
reorders the PSMT8 CSM1 palette, and maps PS2 0..128 alpha to 0..255.
PSMT4 starts with the high nibble; this was checked against the main-menu
stencil's edge. File/member input is capped at 32 MiB, dimensions at 1024²;
whole-resource reads are deliberate for these bounded still textures.
External texture references and absent CLUTs produce explicit errors.
16-bit alpha is a raw RGB5A1 preview; original GS TEXA/blend/mask state belongs
to the consumer. This format is not an arbitrary headerless RGBA pixel dump.

`PackedTexture` uses that same decoder for game textures, reads only the selected
archive member, and uploads its base mip. Placement and meaning stay with the
caller. `drawRounded` crops scanlines to a caller-supplied capsule bound;
`drawTinted` draws an unmodified texture rectangle with caller-owned RGB/alpha
modulation and an optional source crop. The main menu uses the latter with the original SYSTEM.PCK lightbar
and source color vectors, preserving its baked rounded gradient/border.
The profile screen uses the separate MENU.PCK long lightbar with its own source
size, row anchors and light-blue tint. Asset-specific geometry stays in the game.
See the target's [RGBA evidence](../../../docs/rgba-textures.md) for validation
coverage, source addresses, reference checks, and remaining rendering gaps.

`AudioMpk.h` reads the `MPAK` directory and sequential name table without
loading the 145 MB package. `AudioMpkPlayer.h` opens selected PS2 VAG ADPCM
and Paradigm MPC3 ADPCM entries by offset, decodes bounded chunks during
`update()`, and maintains a short SDL PCM queue at the embedded sample rate and
channel count. Call `update()` each frame; `stop()` closes the stream immediately.
It is an asset audition service; callers must separately
apply the game's per-sound pitch, volume, pan and bus properties. The MPC3 step
table is adapted from vgmstream under `VgmstreamLicense.txt`.

## Boundaries

Startup/frontend coordination lives in the main game's `IntroSequence` and `FrontendMenu`. PS2 SDK/hardware compatibility lives in the separate [PS2Compatibility](../PS2Compatibility/README.md) library; developer-console font rendering moved there. Asset formats and movie decoding remain here because they are game services, not SDK emulation.

Keep configuration-file discovery and executable-specific UI out of this
library. Add a service only when at least one executable has a concrete use for
it and its asset behavior is supported by evidence.
