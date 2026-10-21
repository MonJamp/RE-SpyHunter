# RE::SpyHunter

RE::SpyHunter is a native reconstruction of the PlayStation 2 game SpyHunter (2001). The solution keeps the main game, reusable runtime code, and inspection tools in separate projects so behavior recovered from the original can be implemented and checked in small steps.

## Solution layout

- [`RE-SpyHunter`](RE-SpyHunter/RE-SpyHunter.vcxproj) is the main game executable. Its first intro slice follows the recovered boot order through the Press Start image and the `MLOAD.VKI` pre-menu loading screen. Press X or a controller's south face button during a movie to fade it out and skip it; press Start/Options on a controller, Enter, Space, S, or click the title to advance. Controller south also shortens the autosave notice. SDL gamepad mapping supports recognized controllers, including directly connected DualSense pads.
- [`GameRuntime`](GameRuntime/README.md) holds reusable playback and runtime services. It plays PSS cutscenes and indexes PCK archives for bounded member reads.
- [`GameUI`](GameUI/README.md) holds reusable game-menu navigation and drawing primitives. Concrete SpyHunter screens and their recovered coordinates stay in the main game.
- [`PS2Compatibility`](PS2Compatibility/README.md) is the separate library for reconstructed PS2 SDK/hardware-facing services, initially developer-console glyph rendering. It does not depend on GameRuntime.
- [`Configuration`](Configuration/Configuration.vcxproj) loads local paths and logging settings shared by the executables.
- [`GUI`](GUI/README.md) provides shared SDL3 presentation helpers.
- [`Tools`](Tools/README.md) contains focused applications for inspecting game files, including SH Viewer with an embedded `ModelViewerControl`. Tool controls and behavior are documented there.

The solution uses C++20, Visual Studio, and SDL3. Build and local configuration instructions are in [BUILDING.md](BUILDING.md).

See [main game structure](RE-SpyHunter/README.md) for Application/IntroSequence responsibilities, the future menu handoff, and the hidden integration smoke check.

The autosave notice uses the original `euroStile14.vkf` glyph atlas and metrics from `DATA/INTRO.PCK` through `GameRuntime`. The intro applies the original 0.8 font scale, letter-spaced strings, and draw coordinates.
