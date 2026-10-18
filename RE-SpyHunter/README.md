# Main game structure

- `main.cpp`: SDL entry point only.
- `Application`: configuration, SDL/window/renderer lifetime, controller discovery, event pump and frame clock. Releases game resources before renderer/window/SDL, including failure exits.
- `IntroSequence`: game-specific startup assets, input policy, state transitions and presentation. `load`, `handleEvent`, `update(deltaSeconds)`, `render` separate lifecycle phases. It retains the recovered constants and existing host approximations through `readyForMenu()` at MLOAD.
- `FrontendMenu`: local four-slot profile selection and name creation followed by the interactive four-item main menu. It loops the converted original `MBACK.IPU` background when `local-assets/MBACK.mp4` is present. Profiles are stored beside the selected configuration under `local-assets/profiles.txt`; no PS2 memory-card image is read or written.

Application transfers input/rendering to FrontendMenu after showing MLOAD. Keep generic media/asset services in GameRuntime, and PS2 SDK/hardware compatibility in the independent PS2Compatibility library. Avoid putting menu decisions in decoders or adding a general state framework before another flow needs it.

The intro borrows a renderer, owns its resources, and must be destroyed before that renderer and SDL. Construct/load one instance per run. The application supplies elapsed time; the intro preserves the original clamping only for idle/fade updates. Movie decoding still uses media/host timestamps. Input mappings, font positions/colors, image fitting and playback properties are unchanged by the organization work.

The two packed intro stills share one PCK index. A still deliberately decodes one whole image/member to create its texture; video/audio remain incremental. Game-specific ELF offsets and screen placement stay here; the compatibility library receives the console glyph bytes, not a game executable path.

## Smoke check

Build `Tests/IntroSequenceSmoke.vcxproj` with x64 MSBuild (`/m:1 /nr:false /p:Configuration=Debug /p:Platform=x64`) and run `Tests/x64/Debug/IntroSequenceSmoke.exe "<extracted-binaries-root>"`. The project is deliberately separate from the normal solution build and needs the same local SDL props. It uses dummy video/audio and a software renderer, so no desktop or physical audio device is used.

Checks: packed/standalone still loading and failed replacement, invalid console glyph input, missing assets, both autosave branches, repeated-input suppression, fade/idle clamps, legal duration, all three movie skips, attract return, Start and MLOAD endpoint. Time is injected for stills; movie skip fades run in real time. This is an integration smoke check with local assets, not pixel/audio parity, physical gamepad testing, or all-movie completion validation.

Run `prepare-menu-background.ps1` from the solution directory once to convert the original `MOVIES/MBACK.IPU` into ignored `local-assets/MBACK.mp4`; the script defaults to the documented shared FFmpeg installation and prints both hashes. This keeps derived video out of Git and leaves the extracted source unchanged.

Build `Tests/FrontendMenuSmoke.vcxproj` the same way and run `Tests/x64/Debug/FrontendMenuSmoke.exe "<extracted-binaries-root>" "<local-assets/MBACK.mp4>"`. It verifies profile behavior, main-menu Back/re-entry, software rendering, and that two background frames differ. Optional third and fourth output arguments save the profile and main-menu frames as BMPs. It deletes its temporary profile file after the run.
