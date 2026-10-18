# PS2Compatibility

Independent static library for host reconstructions of PS2 SDK APIs and hardware-facing services. These implementations belong here, not in GameRuntime. It currently depends on SDL3 only; it has no GameRuntime, Configuration or main-game dependency. Executables link the libraries they need directly.

First service: `spyhunter::ps2::DevConsoleFont`, extracted from the former GameRuntime `Ps2DevConsoleFont`. It reconstructs the observed Sony developer-console bitmap drawing using a caller-supplied 2,048-byte `sceFont8` table. The caller supplies color, cell scale and screen position. Game-specific ELF offsets and capture-derived mapping remain in `IntroSequence`. Texture ownership is noncopyable; destroy before the renderer/SDL.

This is a narrow SDL-backed reconstruction of observed console glyph behavior, not a complete `sceDevCons` API/ABI, GS emulator, or general PS2 font guarantee. Future SDK/hardware services should document supported calls, arguments/state/timing, original evidence, host mapping and explicit unsupported behavior here. Do not silently stub unknown hardware behavior or introduce memory-card emulation into the current game scope.

GameRuntime continues to own game asset formats, media playback and reusable game services. A PS2-specific file format alone does not make a decoder a hardware/SDK compatibility implementation.
