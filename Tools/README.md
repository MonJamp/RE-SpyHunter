# SpyHunter tools

These small GUI applications help inspect game files and develop the SpyHunter reconstruction.

## SH Viewer

`SHViewer` is the single game-file browser and media viewer. It recursively lists every regular file below the configured `ExtractedBinariesPath`, sorted by relative path. It currently displays standalone `.VKI` stills and plays `.PSS` cutscenes. Other file types are listed and labeled unsupported; packed images and other formats are not decoded.

The viewer uses the shared configuration and GUI libraries, its validated VKI planar byte-RLE decoder, and the `GameRuntime` non-blocking cutscene playback API. It opens at 2560 × 1440 with a 2× UI scale and stays resizable. The browser has clickable **All**, **VKI**, and **PSS** filters (keyboard shortcuts 1, 2, and 3). Click a file or use Up/Down and Enter to view it; the mouse wheel and Page Up/Down move through longer lists. Escape stops a movie, then exits when playback is inactive.

Pass a `SpyHunter.config` path as the first argument, or let the viewer search upward from its working directory. Movie playback writes the Media Foundation `.mpg` working copy under the configuration directory's ignored `local-assets` folder; input game files remain read-only.

## Shared project dependencies

- [`GameRuntime`](../GameRuntime/README.md) provides PSS playback and its incremental session API.
- [`Configuration`](../Configuration/Configuration.vcxproj) loads the extracted-binaries location.
- [`GUI`](../GUI/README.md) provides SDL3 presentation primitives and UI scaling.

These libraries live beside `Tools` under the SpyHunter solution because the future main game and other focused applications may share them.
