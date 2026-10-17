# SpyHunter tools

These small GUI applications help inspect game files and develop the SpyHunter reconstruction.

## SH Viewer

`SHViewer` is the single game-file browser and media viewer. It recursively lists every regular file below the configured `ExtractedBinariesPath`, sorted by relative path. It displays standalone `.VKI` stills and plays `.PSS` cutscenes. Clicking a `.PCK` expands a subtree under that row with its member names; selecting a supported `.vki` member displays it in the main area. Other member types are listed as unsupported.

The viewer uses the shared configuration and GUI libraries, its validated VKI planar byte-RLE decoder, and the `GameRuntime` cutscene and PCK APIs. It opens at 2560 × 1440 with a 2× UI scale and stays resizable. The browser has clickable **All**, **VKI**, **PSS**, and **PCK** filters (keyboard shortcuts 1–4). Click a file or use Up/Down and Enter to view it; the mouse wheel and Page Up/Down move through longer browser lists and expanded PCK subtrees. Click an expanded PCK again to collapse it. Escape stops a movie, then exits when playback is inactive.

Pass a `SpyHunter.config` path as the first argument, or let the viewer search upward from its working directory. PSS playback uses a file-backed Media Foundation byte stream and incremental PCM reading, without copying the movie. Input game files remain read-only. Set `Log.SHViewer` and `Log.GameRuntime` independently in the config to inspect navigation and playback in the tab-delimited `LogFilePath` file. See [building and configuration](../BUILDING.md) for setup and log levels.

## Shared project dependencies

- [`GameRuntime`](../GameRuntime/README.md) provides PSS playback and PCK indexing/bounded entry reads.
- [`Configuration`](../Configuration/Configuration.vcxproj) loads the extracted-binaries location.
- [`GUI`](../GUI/README.md) provides SDL3 presentation primitives and UI scaling.

These libraries live beside `Tools` under the SpyHunter solution because the main game and other focused applications may share them.
