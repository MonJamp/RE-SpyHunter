# RE::SpyHunter

RE::SpyHunter is a native reconstruction of the PlayStation 2 game SpyHunter (2001). The solution keeps the main game, reusable runtime code, and inspection tools in separate projects so behavior recovered from the original can be implemented and checked in small steps.

## Solution layout

- [`RE-SpyHunter`](RE-SpyHunter/RE-SpyHunter.vcxproj) is the main game executable. It currently opens an SDL3 window and runs an exitable event/render loop; gameplay and asset loading are still to come.
- [`GameRuntime`](GameRuntime/README.md) holds reusable playback and runtime services. It plays PSS cutscenes and indexes PCK archives for bounded member reads.
- [`Configuration`](Configuration/Configuration.vcxproj) loads local paths and logging settings shared by the executables.
- [`GUI`](GUI/README.md) provides shared SDL3 presentation helpers.
- [`Tools`](Tools/README.md) contains focused applications for inspecting game files, including SH Viewer. Tool controls and behavior are documented there.

The solution uses C++20, Visual Studio, and SDL3. Build and local configuration instructions are in [BUILDING.md](BUILDING.md).
