# GameRuntime

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
The SDL audio queue is refilled incrementally during `update()`. The session
handles timing and aspect-fit rendering. `Log.GameRuntime=debug` enables open,
queue, completion, and error diagnostics; `trace` adds refill and frame details.

## Intended additions

- A shared still-image service if the main runtime needs the VKI decoder currently owned by SH Viewer.
- Startup/frontend state coordination built from named states and transition
  results. Menu transitions should depend on state IDs and actions, not on media
  decoder details.

Keep configuration-file discovery and executable-specific UI out of this
library. Add a service only when at least one executable has a concrete use for
it and its asset behavior is supported by evidence.
