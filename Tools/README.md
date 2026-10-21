# SpyHunter tools

These small GUI applications help inspect game files and develop the SpyHunter reconstruction.

## FrontendCapture

[`FrontendCapture`](FrontendCapture/README.md) is a small command-line renderer for AI-assisted frontend inspection. It captures the profile, name-entry, or main menu and can save just a header, table, selected row, footer, or exact pixel rectangle. It uses temporary profile state and the same `FrontendMenu` implementation as the game. The behavior checks remain in the separate `FrontendMenuSmoke` project.

## SH Viewer

`SHViewer` is the game-file browser and media viewer. It recursively lists every regular file below the configured `ExtractedBinariesPath`, sorted by relative path. It displays standalone `.VKI` stills and serialized `.RGBA`/`.RGB` textures, and plays `.PSS` cutscenes. Clicking a `.PCK` expands its members; select a `.vki` image or `vkDMATexture` to display it. Texture previews show dimensions, PS2 format, palette/mip counts, and the first palette/base mip. Press **C** to switch the transparency background between checkerboard and black, and **A** to inspect alpha as grayscale. This exposes assets such as the main-menu stencil that otherwise appear solid white when an image program discards alpha. Raw texture display does not apply original game framebuffer masks, material tint, UV animation or GS TEXA state. The integrated `ModelViewerControl` also previews the identified PG1 `vkTrack` member (BSP v7, default leaf 74 / batch 5) in a diagnostic host view. It reports the source member range, topology, raw coordinates, UVs and colors; **W**, **C**, arrows, **+/-**, and Home control wireframe, vertex colors, orbit, zoom and reset while a 3D resource is selected. **F3** toggles detailed bounds/UV/degenerate/camera statistics, **F4** toggles projected bounds, and **V** toggles vertex points. Track depth uses a painter-order approximation; texture resolution and original game presentation remain open. Ordinary `.vk` model entries are identified in the same control but their hierarchy/drawable parser is not implemented yet. Clicking `DATA/AUDIO.MPK` expands its 359 `.vag` and `.mc3` audio entries; selecting one plays it at the sample rate and channel count stored in its own header. The first four MPK entries are IOP support modules and are not shown in the audio list.

The viewer uses the shared configuration and GUI libraries, its VKI decoder, and the `GameRuntime` cutscene, PCK, MPK and audio APIs. It opens at 2560 × 1440 with a 2× UI scale and stays resizable. The browser has clickable **All**, **VKI**, **PSS**, **PCK**, and **MPK** filters (keyboard shortcuts 1–5). Click a file or use Up/Down and Enter to view or play it; the mouse wheel and Page Up/Down move through longer lists. Click an expanded archive again to collapse it. Escape stops video or audio playback, then exits when playback is inactive. Selecting another item stops the current sound.

MPK playback is a raw asset audition at header rate and unity gain. In-game effect pitch, volume, pan, bus and priority depend on the individual sound call. For example, the title accept sound uses 0.8 pitch and 0.5 volume in-game; SH Viewer intentionally plays the archived VAG at its native 44.1 kHz for inspection. MPC3 decoding uses the step-size table adapted from [vgmstream](https://github.com/vgmstream/vgmstream/blob/master/src/coding/mpc3_decoder.c) under the notice in `GameRuntime/VgmstreamLicense.txt`.

Pass a `SpyHunter.config` path as the optional positional argument, or let the viewer search upward from its working directory. CLI11 validates the command line; use `--help` for the generated usage. PSS playback uses a file-backed Media Foundation byte stream and incremental PCM reading, without copying the movie. Input game files remain read-only. Set `Log.SHViewer` and `Log.GameRuntime` independently in the config to inspect navigation and playback in the tab-delimited `LogFilePath` file. See [building and configuration](../BUILDING.md) for setup and log levels.

For repeatable texture inspection, `--open` selects a file relative to the configured root and `--member` selects a PCK resource by full name or basename. `--export-image` writes a native-size decoded BMP and exits; add `--alpha` to export the alpha channel. `--capture-view` instead saves one complete viewer frame and exits. Export/capture runs keep their window hidden; their output directories must already exist. A BMP reader that ignores transparency may show every RGB pixel even when alpha is zero, so use alpha view or the viewer checkerboard to inspect masks.

For repeatable track analysis, `--leaf <index>` and `--batch <index>` select another PG1 BSP batch (defaults 74 and 5). `--dump-geometry-json <path>` writes the selected member/batch offsets, raw position/RGBA/UV values, strip lengths, and winding-corrected triangle indices, then exits. The JSON labels unknown source units and the host painter-order depth approximation. Output directories must already exist. Configure `Log.SHViewer=debug` to record selected member ranges, parser outcomes and decoded counts.

```powershell
.\x64\Debug\SHViewer.exe --open DATA/MENU.PCK --member menu_main_stencil.rgba
.\x64\Debug\SHViewer.exe --open DATA/MENU.PCK --member menu_main_stencil.rgba --alpha --export-image stencil-alpha.bmp
.\x64\Debug\SHViewer.exe --open DATA/MENU.PCK --member menu_main_wheel.rgba --capture-view wheel-view.bmp
.\x64\Debug\SHViewer.exe --open PG1/PG1.PCK --member new_provinggrounds.trk --leaf 74 --batch 5 --dump-geometry-json pg1-leaf74-batch5.json
```

## Shared project dependencies

- [`GameRuntime`](../GameRuntime/README.md) provides PSS playback, PCK and MPK indexing/bounded entry reads, and VAG/MPC3 audition playback.
- [`Configuration`](../Configuration/Configuration.vcxproj) loads the extracted-binaries location.
- [`GUI`](../GUI/README.md) provides SDL3 presentation primitives and UI scaling.

These libraries live beside `Tools` under the SpyHunter solution because the main game and other focused applications may share them.

When a PCK is expanded, `vkTrack` and `vkModel` entries can be selected directly in the same browser. `vkTrack` is decoded by `GameRuntime/TrackGeometry`; `.vk` models show version, member extent and the explicit unsupported-parser reason in the embedded control. `--open` plus `--member` selects a PCK member on launch. The preview camera and geometry are inspection aids, not original-game fidelity evidence.
