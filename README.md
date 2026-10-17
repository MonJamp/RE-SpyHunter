# SpyHunter native reconstruction solution

## Shared configuration

`Configuration` is a C++20 static-library project shared by the main reconstruction and focused test applications. It currently loads one required setting:

```text
ExtractedBinariesPath=C:\path\to\Extracted Binaries
```

Copy `SpyHunter.config.example` to `SpyHunter.config` and adjust the path for the current machine. The real file is ignored by Git because it is machine-specific. Relative values are resolved from the configuration file's directory.

Copy `SpyHunter.local.props.example` to `SpyHunter.local.props` and set `SDL3Root` to the local SDL3 distribution directory before building. The real properties file is ignored by Git. The project derives its include, library, and runtime DLL paths from that property so no developer-specific dependency location is committed.

Build the `x64` solution platform. When building from a terminal, use the x64 Visual Studio developer environment or the x64 MSBuild executable. The 32-bit MSBuild host can fail at link time with an incompatible `MSPDB140.DLL` even though compilation succeeds.

`Tools/SHViewer` accepts an explicit configuration path as its first command-line argument. Without one, it searches for `SpyHunter.config` from its working directory upward. **SH Viewer** opens at 2560 × 1440 with a 2× UI scale, remains resizable, and launches without a console window. It recursively lists every regular file beneath `ExtractedBinariesPath`; selecting `.VKI` displays the decoded still and selecting `.PSS` plays the cutscene. Other files remain visible in the **All** browser and are marked unsupported. Click **PSS** to show only cutscenes, **VKI** for stills, or **All** for every file; 3, 2, and 1 are the matching shortcuts. Click a row or select it with Up/Down and press Enter. The mouse wheel and Page Up/Down browse longer lists. Escape stops movie playback; press Escape again or close the window to exit.

SH Viewer opens `.PSS` files through a file-backed Media Foundation byte stream and reads their PCM audio incrementally. It does not copy the movie before playback. The source PSS and everything under the extracted/original trees remain unchanged.

Logging is configured in `SpyHunter.config`: `LogFilePath` selects an append-only, tab-delimited file, `Log.Default` sets the fallback level, and `Log.SHViewer`, `Log.GameRuntime`, `Log.GUI`, and `Log.Configuration` independently override it. Levels are `off`, `error`, `warning`, `info`, `debug`, and `trace`, from least to most detail. For example, `Log.GameRuntime=debug` records cutscene open and audio-buffer diagnostics while `Log.GUI=off` suppresses GUI messages. The example configuration writes to `local-assets/SpyHunter.log`; each line contains UTC epoch milliseconds, module, level, and message.

Utility and focused executable projects live under `Tools`; reusable libraries and the future main game remain separate. SH Viewer uses the public `GameRuntime` cutscene-session API for PSS playback and its local VKI decoder for stills. See [Tools](Tools/README.md) and [GameRuntime](GameRuntime/README.md) for their ownership boundaries.
