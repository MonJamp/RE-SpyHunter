# Building the native solution

Open `SpyHunter.slnx` in Visual Studio and build the `x64` solution platform. To run the main game starter, set `RE-SpyHunter` as the startup project. It opens a resizable 1280 × 720 window and exits with Escape or window close.

Copy `SpyHunter.local.props.example` to `SpyHunter.local.props` and set `SDL3Root` to the local SDL3 distribution directory. The local properties file is ignored by Git; the project derives include, library, and DLL paths from it.

Copy `SpyHunter.config.example` to `SpyHunter.config` and set `ExtractedBinariesPath` to the extracted game files. The local config is ignored by Git. Relative values are resolved from the configuration file's directory. The main game starter does not yet load this config; SH Viewer does.

When building from a terminal, use the x64 Visual Studio developer environment or x64 MSBuild. The 32-bit MSBuild host has failed at link time with an incompatible `MSPDB140.DLL` in this environment.

`SpyHunter.config` also controls logging: `LogFilePath` selects an append-only, tab-delimited file; `Log.Default` sets the fallback level; and `Log.SHViewer`, `Log.GameRuntime`, `Log.GUI`, and `Log.Configuration` override it by module. Levels are `off`, `error`, `warning`, `info`, `debug`, and `trace`. The example writes to `local-assets/SpyHunter.log` with UTC epoch milliseconds in each line.
