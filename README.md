# SpyHunter native reconstruction solution

## Shared configuration

`Configuration` is a C++20 static-library project shared by the main reconstruction and focused test applications. It currently loads one required setting:

```text
ExtractedBinariesPath=C:\path\to\Extracted Binaries
```

Copy `SpyHunter.config.example` to `SpyHunter.config` and adjust the path for the current machine. The real file is ignored by Git because it is machine-specific. Relative values are resolved from the configuration file's directory.

Copy `SpyHunter.local.props.example` to `SpyHunter.local.props` and set `SDL3Root` to the local SDL3 distribution directory before building. The real properties file is ignored by Git. The project derives its include, library, and runtime DLL paths from that property so no developer-specific dependency location is committed.

Build the `x64` solution platform. When building from a terminal, use the x64 Visual Studio developer environment or the x64 MSBuild executable. The 32-bit MSBuild host can fail at link time with an incompatible `MSPDB140.DLL` even though compilation succeeds.

`Tools/SHViewer` accepts an explicit configuration path as its first command-line argument. Without one, it searches for `SpyHunter.config` from its working directory upward. **SH Viewer** opens at 2560 × 1440 with a 2× UI scale, remains resizable, and launches without a console window. It recursively lists every regular file beneath `ExtractedBinariesPath`; selecting `.VKI` displays the decoded still and selecting `.PSS` plays the cutscene. Other files remain visible in the browser and are marked unsupported. Click a row or select it with Up/Down and press Enter. Escape stops movie playback; press Escape again or close the window to exit.

SH Viewer creates an ignored `local-assets/M_LOGO.mpg` working copy because Windows' MPEG source resolver requires a recognized container suffix. The source PSS and everything under the extracted/original trees remain unchanged.

Utility and focused executable projects live under `Tools`; reusable libraries and the future main game remain separate. SH Viewer uses the public `GameRuntime` cutscene-session API for PSS playback and its local VKI decoder for stills. See [Tools](Tools/README.md) and [GameRuntime](GameRuntime/README.md) for their ownership boundaries.
