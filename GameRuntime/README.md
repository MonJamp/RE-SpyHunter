# GameRuntime

`VkfTextureFont` reads the observed `vkTextureFont`/PSMT8 entry from a PCK archive, converts its indexed atlas and glyph metrics to an SDL texture, and draws text using the game's bottom-origin coordinates. Its current format support is the 256 × 256 layout with four control and 95 printable ASCII records used by `euroStile14.vkf` in `INTRO.PCK`. Other VKF layouts require separate decoding before use.

`GameRuntime` is the shared native reconstruction library. Executables such as
`Tools/SHViewer` and the future main game own application startup, configuration,
the SDL window, and top-level state selection. This library owns reusable game
presentation and runtime services.

## Current API

`CutscenePlayer.h` exposes `CutsceneSession` for non-blocking playback and
`playCutscene(renderer, options)` as a blocking wrapper. Options contain a source
PSS path and the Escape behavior; no writable movie directory is needed. Video
uses a file-backed Media Foundation byte stream with MPEG content type, while
`PssAudioReader` reads private-stream PCM packets through a bounded input buffer.
The target's 512-byte left and right channel blocks are converted to alternating
stereo samples before being queued to SDL. The SDL audio queue is refilled incrementally during `update()`. The session
handles timing and aspect-fit rendering. `Log.GameRuntime=debug` enables open,
queue, completion, and error diagnostics; `trace` adds refill and frame details.

`PckArchive.h` exposes a read-only archive index (`open`, `entries`) and a
bounded `readEntry` method for one serialized resource. It scans PCK metadata
in 64 KiB blocks without loading every resource body. Entry boundaries are
inferred from the next validated header. `VkiImage.h` decodes standalone VKIs
and supported VKI members read from a PCK.

## Intended additions

- Startup/frontend state coordination built from named states and transition
  results. Menu transitions should depend on state IDs and actions, not on media
  decoder details.

Keep configuration-file discovery and executable-specific UI out of this
library. Add a service only when at least one executable has a concrete use for
it and its asset behavior is supported by evidence.
