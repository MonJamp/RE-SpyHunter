# SpyHunter tools

These small GUI applications help inspect game files and develop the SpyHunter reconstruction.

## SH Viewer

`SHViewer` is the single game-file browser and media viewer. It recursively lists every regular file below the configured `ExtractedBinariesPath`, sorted by relative path. It displays standalone `.VKI` stills and plays `.PSS` cutscenes. Clicking a `.PCK` expands a subtree under that row with its member names; selecting a supported `.vki` member displays it in the main area. Other member types are listed as unsupported. Clicking `DATA/AUDIO.MPK` expands its 359 `.vag` and `.mc3` audio entries; selecting one plays it at the sample rate and channel count stored in its own header. The first four MPK entries are IOP support modules and are not shown in the audio list.

The viewer uses the shared configuration and GUI libraries, its VKI decoder, and the `GameRuntime` cutscene, PCK, MPK and audio APIs. It opens at 2560 × 1440 with a 2× UI scale and stays resizable. The browser has clickable **All**, **VKI**, **PSS**, **PCK**, and **MPK** filters (keyboard shortcuts 1–5). Click a file or use Up/Down and Enter to view or play it; the mouse wheel and Page Up/Down move through longer lists. Click an expanded archive again to collapse it. Escape stops video or audio playback, then exits when playback is inactive. Selecting another item stops the current sound.

MPK playback is a raw asset audition at header rate and unity gain. In-game effect pitch, volume, pan, bus and priority depend on the individual sound call. For example, the title accept sound uses 0.8 pitch and 0.5 volume in-game; SH Viewer intentionally plays the archived VAG at its native 44.1 kHz for inspection. MPC3 decoding uses the step-size table adapted from [vgmstream](https://github.com/vgmstream/vgmstream/blob/master/src/coding/mpc3_decoder.c) under the notice in `GameRuntime/VgmstreamLicense.txt`.

Pass a `SpyHunter.config` path as the first argument, or let the viewer search upward from its working directory. PSS playback uses a file-backed Media Foundation byte stream and incremental PCM reading, without copying the movie. Input game files remain read-only. Set `Log.SHViewer` and `Log.GameRuntime` independently in the config to inspect navigation and playback in the tab-delimited `LogFilePath` file. See [building and configuration](../BUILDING.md) for setup and log levels.

## Shared project dependencies

- [`GameRuntime`](../GameRuntime/README.md) provides PSS playback, PCK and MPK indexing/bounded entry reads, and VAG/MPC3 audition playback.
- [`Configuration`](../Configuration/Configuration.vcxproj) loads the extracted-binaries location.
- [`GUI`](../GUI/README.md) provides SDL3 presentation primitives and UI scaling.

These libraries live beside `Tools` under the SpyHunter solution because the main game and other focused applications may share them.
