# SpyHunter tools

This directory contains small executable applications that help develop and inspect the SpyHunter reconstruction. These tools are separate from the game so media and resource workflows can be exercised without launching the full game.

## CutscenePlayer

`CutscenePlayer` is a standalone cutscene playback and development utility. It opens at 2560 × 1440 and uses a 2× UI scale so the browser remains readable on a 4K development display. It is a Windows GUI application, so it opens without a console window. It recursively scans the configured extracted-binaries directory for `.PSS` files and presents the sorted relative paths in a browser pane beside the video preview. Click a row, or use Up/Down and Enter, to play a selected cutscene. Escape stops playback, then exits when no cutscene is active. It is intended to grow into a general tool for working with the game's cutscenes, including checking playback and iterating on cutscene support before integrating it into the game.

The executable reads `SpyHunter.config` to find extracted assets. Pass the config path as the first argument, or let the tool search upward from its working directory. The current playback implementation is provided by the shared [`GameRuntime` library](../GameRuntime/README.md), through its non-blocking `CutsceneSession` API. The tool owns its window, browser, SDL setup, and asset selection.

## StillViewer

`StillViewer` is the companion asset browser for static images. It uses the same configuration, 2560 × 1440 default window, 2× UI scale, and right-side browser layout as CutscenePlayer. It recursively inventories standalone `.VKI` containers and ordinary image files beneath the extracted-binaries root. Standalone SpyHunter `.VKI` files are decoded directly from their planar byte-RLE representation, converted from bottom-up storage to top-down display orientation, and aspect-fitted in the preview pane. Ordinary image files are inventoried but their pixel decoding and packed `INTRO.PCK` extraction remain pending behind the same viewer boundary.

## Shared project dependencies

The following solution projects provide reusable functionality to tools and the future main game:

- [`GameRuntime`](../GameRuntime/README.md) contains runtime-facing media functionality, currently cutscene playback. It is the shared API for playing a cutscene from an SDL renderer; future still-image loading and game state transitions can be added here as their interfaces are defined.
- [`Configuration`](../Configuration/Configuration.vcxproj) loads shared application settings, including the location of extracted binaries.
- [`GUI`](../GUI/README.md) supplies reusable SDL3 presentation primitives and 4K scaling for every tool.

These libraries live beside `Tools` under the SpyHunter solution because they are consumed by both standalone utilities and the main game.
