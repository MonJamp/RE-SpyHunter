# Main game structure

The source-derived [frontend menu requirements](../../../docs/frontend-menu-requirements.md)
document the original hierarchy, navigation and selection commands. The current
native hierarchy now follows that baseline; the implementation-status section
records remaining gameplay/media, progress and presentation boundaries.

- `main.cpp`: SDL entry point only.
- `Application`: configuration, SDL/window/renderer lifetime, controller discovery, event pump and frame clock. Releases game resources before renderer/window/SDL, including failure exits.
- `IntroSequence`: game-specific startup assets, input policy, state transitions and presentation. `load`, `handleEvent`, `update(deltaSeconds)`, `render` separate lifecycle phases. It retains the recovered constants and existing host approximations through `readyForMenu()` at MLOAD.
- `FrontendMenu`: local three-slot profile selection/name creation and concrete game menu state. Escape or controller Triangle returns to the preceding screen while restoring the parent selection. It loops the converted original `MBACK.IPU` background when `local-assets/MBACK.mp4` is present. Profiles are stored beside the selected configuration under `local-assets/profiles.txt`; no PS2 memory-card image is read or written.
- Menu expansion: Operations directly opens the 14-mission selector and briefing tabs; System Options includes Settings, Controller and Extras; Dossier is a read-only mission table. Extras exposes 19 music entries, 24 movies and 16 rewards. Settings and controller schemes accept/cancel and persist per profile (optional `OPTIONS_V1` tail, old three-name files still load); they do not yet drive the audio/vibration services. Gameplay/media playback and model previews remain labeled boundaries. Square or keyboard S opens profile deletion confirmation (No by default); arrows/D-pad navigate, Left/Right adjust/carousel, Cross/Enter accepts, Triangle/Escape cancels. Held left-stick repeat lives in GameUI; title-specific routing/catalogues remain here. Two Player requires two gamepads, and Controller 2 returns on disconnect.

Application transfers input/rendering to FrontendMenu after showing MLOAD. Generic menu cursor movement and selector/panel drawing live in GameUI, mirroring the original split between shared `ShMenu` facilities and concrete menu classes. Keep generic media/asset services in GameRuntime, and PS2 SDK/hardware compatibility in the independent PS2Compatibility library. Avoid putting menu decisions in decoders or adding a general state framework before another flow needs it.

The intro borrows a renderer, owns its resources, and must be destroyed before that renderer and SDL. Construct/load one instance per run. The application supplies elapsed time; the intro preserves the original clamping only for idle/fade updates. Movie decoding still uses media/host timestamps. Input mappings, font positions/colors, image fitting and playback properties are unchanged by the organization work.

The two packed intro stills share one PCK index. A still deliberately decodes one whole image/member to create its texture; video/audio remain incremental. Game-specific ELF offsets and screen placement stay here; the compatibility library receives the console glyph bytes, not a game executable path.

## Debug profile launch

From the solution directory, launch directly into Settings with:

```powershell
& .\x64\Debug\RE-SpyHunter.exe .\SpyHunter.config --debug-profile --start-screen settings
```

To open Mission Select directly, use `--start-screen mission-select` instead of `settings`.

`--debug-profile` uses the isolated `local-assets/debug-profiles.txt` store and seeds profile 1 as `DEBUG`; it never writes normal `profiles.txt`. `--start-screen` requires that flag, skips the intro and profile picker, and accepts `mission-select`, `dossier`, `settings`, `controller`, `extras`, `music`, `movies`, or `cheats`. Mission Select and Dossier return to Main; other screens return to their matching System Options or Extras parent. If `--debug-profile` is used alone, the normal intro and profile picker remain, with the isolated DEBUG profile available there. Changes are saved to the debug store when accepted.

## Smoke check

Build `Tests/IntroSequenceSmoke.vcxproj` with x64 MSBuild (`/m:1 /nr:false /p:Configuration=Debug /p:Platform=x64`) and run `Tests/x64/Debug/IntroSequenceSmoke.exe "<extracted-binaries-root>"`. The CLI uses shared CLI11 validation; `--help` prints its generated usage. The project is deliberately separate from the normal solution build and needs the same local SDL props. It uses dummy video/audio and a software renderer, so no desktop or physical audio device is used.

Checks: packed/standalone still loading and failed replacement, invalid console glyph input, missing assets, both autosave branches, repeated-input suppression, fade/idle clamps, legal duration, all three movie skips, attract return, Start and MLOAD endpoint. Time is injected for stills; movie skip fades run in real time. This is an integration smoke check with local assets, not pixel/audio parity, physical gamepad testing, or all-movie completion validation.

Run `prepare-menu-background.ps1` from the solution directory once to convert the original `MOVIES/MBACK.IPU` into ignored `local-assets/MBACK.mp4`; the script defaults to the documented shared FFmpeg installation and prints both hashes. This keeps derived video out of Git and leaves the extracted source unchanged. The native submenu tree is documented as code/string evidence in the [frontend property audit](../../../docs/intro-property-audit.md#main-menu-0).

Build `Tests/FrontendMenuSmoke.vcxproj` the same way and run `Tests/x64/Debug/FrontendMenuSmoke.exe "<extracted-binaries-root>" --background "<local-assets/MBACK.mp4>"`. It verifies profile behavior, all implemented menu branches and parent Back navigation, software rendering, and that two background frames differ. It deletes its temporary profile file after the run. Use `--help` for the shared CLI11-generated usage, or use the separate [FrontendCapture](../Tools/FrontendCapture/README.md) project to save full or cropped UI images.

