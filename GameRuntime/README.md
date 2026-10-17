# GameRuntime

`GameRuntime` is the shared native reconstruction library. Executables such as
`Tools/CutscenePlayer` and the future main game own application startup, configuration,
the SDL window, and top-level state selection. This library owns reusable game
presentation and runtime services.

## Current API

`CutscenePlayer.h` exposes `playCutscene(renderer, options)`. It accepts an
existing SDL renderer, a source PSS path, and a writable working directory. It
returns `completed`, `skipped`, or `failed` with a diagnostic. The implementation
contains the target-specific PSS demultiplexing, Windows Media Foundation video
decode, SDL audio playback, timing, aspect-fit rendering, and playback event loop.

## Intended additions

- `StillImagePlayer`: move the now-validated VKI decoder/presentation path from `Tools/StillViewer` into this shared library when the main runtime needs it.
- A non-blocking cutscene session API for integration into the main game loop.
- Startup/frontend state coordination built from named states and transition
  results. Menu transitions should depend on state IDs and actions, not on media
  decoder details.

Keep configuration-file discovery and executable-specific UI out of this
library. Add a service only when at least one executable has a concrete use for
it and its asset behavior is supported by evidence.
