# SpyHunter native reconstruction solution

## Shared configuration

`Configuration` is a C++20 static-library project shared by the main reconstruction and focused test applications. It currently loads one required setting:

```text
ExtractedBinariesPath=C:\path\to\Extracted Binaries
```

Copy `SpyHunter.config.example` to `SpyHunter.config` and adjust the path for the current machine. The real file is ignored by Git because it is machine-specific. Relative values are resolved from the configuration file's directory.

Copy `SpyHunter.local.props.example` to `SpyHunter.local.props` and set `SDL3Root` to the local SDL3 distribution directory before building. The real properties file is ignored by Git. The project derives its include, library, and runtime DLL paths from that property so no developer-specific dependency location is committed.

Build the `x64` solution platform. When building from a terminal, use the x64 Visual Studio developer environment or the x64 MSBuild executable. The 32-bit MSBuild host can fail at link time with an incompatible `MSPDB140.DLL` even though compilation succeeds.

`StartupPlayer` accepts an explicit configuration path as its first command-line argument. Without one, it searches for `SpyHunter.config` from its working directory upward. It plays `MOVIES/M_LOGO.PSS` with MPEG-2 video decoded by Windows Media Foundation and the PSS Sony PCM stream played through SDL3. Escape or closing the window stops playback.

The player creates an ignored `local-assets/M_LOGO.mpg` working copy because Windows' MPEG source resolver requires a recognized container suffix. The source PSS and everything under the extracted/original trees remain unchanged.

Reusable runtime behavior lives in the `GameRuntime` static library. `StartupPlayer` is intentionally a small client of its public `playCutscene` API; the future main game project can link the same library. See [GameRuntime](GameRuntime/README.md) for the ownership boundary and planned still-image/state additions.
