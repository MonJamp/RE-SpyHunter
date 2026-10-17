# SpyHunter native reconstruction solution

## Shared configuration

`Configuration` is a C++20 static-library project shared by the main reconstruction and focused test applications. It currently loads one required setting:

```text
ExtractedBinariesPath=C:\path\to\Extracted Binaries
```

Copy `SpyHunter.config.example` to `SpyHunter.config` and adjust the path for the current machine. The real file is ignored by Git because it is machine-specific. Relative values are resolved from the configuration file's directory.

Set the `SPYHUNTER_SDL3_ROOT` environment variable to the local SDL3 distribution directory before building. The project derives its include, library, and runtime DLL paths from that variable so no developer-specific dependency location is committed.

`StartupPlayer` accepts an explicit configuration path as its first command-line argument. Without one, it searches for `SpyHunter.config` from its working directory upward. It currently resolves and validates `MOVIES/M_LOGO.PSS`, the first logo movie selected for playback integration; actual PSS decoding is not implemented yet.
