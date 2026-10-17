# RE::SpyHunter

RE::SpyHunter is a native reconstruction of the PlayStation 2 game SpyHunter (2001). The solution keeps the main game, reusable runtime code, and inspection tools in separate projects so behavior recovered from the original can be implemented and checked in small steps.

## Solution layout

- [`RE-SpyHunter`](RE-SpyHunter/RE-SpyHunter.vcxproj) is the main game executable. Its first intro slice follows the recovered boot order through the Press Start image and the `MLOAD.VKI` pre-menu loading screen. Press X during a movie to skip it while iterating; Press Start with Enter, Space, S, or a mouse click.
- [`GameRuntime`](GameRuntime/README.md) holds reusable playback and runtime services. It plays PSS cutscenes and indexes PCK archives for bounded member reads.
- [`Configuration`](Configuration/Configuration.vcxproj) loads local paths and logging settings shared by the executables.
- [`GUI`](GUI/README.md) provides shared SDL3 presentation helpers.
- [`Tools`](Tools/README.md) contains focused applications for inspecting game files, including SH Viewer. Tool controls and behavior are documented there.

The solution uses C++20, Visual Studio, and SDL3. Build and local configuration instructions are in [BUILDING.md](BUILDING.md).

The autosave notice styling is based on the user-supplied PCSX2 capture at [`reference-captures/autosave-enabled-2026-10-17.png`](../../reference-captures/autosave-enabled-2026-10-17.png). The native font is a small in-source approximation; exact original font data and pixel matching remain open.
