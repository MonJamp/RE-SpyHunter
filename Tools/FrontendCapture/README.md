# FrontendCapture

This command-line inspection tool renders the real `FrontendMenu` with an SDL software renderer and saves only the requested pixels. It is separate from `FrontendMenuSmoke`, which checks behavior. The output is a BMP at native 640 × 448 logical resolution or a smaller crop; no image upscaling is applied. Cropping before saving makes visual inspection cheaper in image tokens and disk space.

Options are parsed by the shared, header-only [CLI11](https://github.com/CLIUtils/CLI11) v2.7.2 library. Its single header and MIT license are in [`ThirdParty/CLI11`](../../ThirdParty/README.md), so building this tool does not require package restore or a network connection. `--help` prints generated usage and option descriptions.

Build `FrontendCapture.vcxproj` with x64 MSBuild, or build the solution. From the solution directory:

```powershell
$capture = '.\Tools\FrontendCapture\x64\Debug\FrontendCapture.exe'
$assets = 'C:\Users\Filjo\Documents\Reverse Engineering\SpyHunter (2001)\Extracted Binaries'
& $capture --assets $assets --background '.\local-assets\MBACK.mp4' --screen profile --region selection --output '.\profile-selection.bmp'
& $capture --assets $assets --background '.\local-assets\MBACK.mp4' --screen profile --filled --region footer --output '.\profile-footer.bmp'
& $capture --assets $assets --background '.\local-assets\MBACK.mp4' --screen main --selection 2 --rect 225,225,210,54 --output '.\main-item.bmp'
```

Options:

- `--assets ROOT` and `--output FILE.bmp` are required. The output directory must already exist. Use `--help` for the generated option summary.
- `--background MBACK.mp4` uses the prepared animated background. Omit it to inspect UI against the fallback background.
- `--screen profile|name|main|mission-select` selects the screen; the default is `profile`.
- `--selection INDEX` moves the profile or menu cursor before rendering (0–2 for profile, 0–3 for main, 0–13 for Mission Select).
- With `--path`, `--selection` is checked against the destination (up to 23 for movies); briefing/grid choices use horizontal movement. `--path 0,0` reaches briefing, `2,2,0` music, `2,2,1` movies, `2,2,2` rewards, and `3` the read-only Dossier. Submenu selection crops use the table area rather than root-only coordinates.
- `--path 2,1,0` accepts zero-based choices from the main menu before applying `--selection`; this example reaches Controller 1 Options. Requires `--screen main`. Each index is checked against the current menu size; paths that do not transition screens (locked or unimplemented actions) fail.
- `--screen mission-select` creates the isolated capture profile, opens Operations, and renders the selector directly. Use `--mission-points`, `--completed-primary`, or `--all-levels` to select card states without persisting progress.
- Mission Select progress fixtures can be set with `--mission-points N`, `--completed-primary 1,3`, and `--all-levels`. These values are temporary capture inputs and do not write profile progress. Example: `--screen main --path 0 --selection 1 --mission-points 4 --completed-primary 1` renders mission 2 open; omit `--completed-primary 1` to show only its unmet predecessor condition.
- Mission Select cards settle to the initial targets before `--selection` is applied, so a nonzero selection captures the source-style scroll toward that mission. `--time-ms` controls the post-selection animation/background sample; use at least 700 ms for settled card positions, including the incoming card whose target is y=360.
- `--filled` creates a temporary `SPY1` profile so the profile menu shows its progress row and Delete legend. The main-menu screen creates this profile automatically.
- `--region full|header|table|selection|footer` chooses a useful crop. The default is `full`; `selection` follows the selected row. `--rect x,y,width,height` overrides the named region. Coordinates are pixels from the top-left of the 640 × 448 canvas and must fit inside it.
- `--time-ms 0..8000` chooses roughly when to sample the background after menu creation; default 100 ms. Media decoding and host timing mean this is a convenient sampling control, not a frame-exact PS2 timestamp.

Each invocation creates and removes an isolated temporary profile directory. It never reads or changes the game's `local-assets/profiles.txt`. It saves one image and prints the selected screen, crop coordinates, and output path. For a focused comparison, request the smallest region containing the UI element; use `full` only when layout context is needed. Captures are native reconstruction evidence, not proof of parity with PCSX2.