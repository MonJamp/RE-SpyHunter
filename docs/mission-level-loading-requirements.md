# Mission level loading requirements

Mission 1 loading slice, updated 2026-10-20 from the target's static source-contract refinement and preserved loading capture. The evidence applies to USA `SLUS_200.56` (SHA-256 `EFC55119F17E35F6AEE95335499F5845D5FAD4274434E803449C6E14CBFA73AB`); runtime order remains unconfirmed in PCSX2.

Source record: [Mission 1 loading investigation](../../../docs/level-one-loading.md). This native work covers package discovery, bounded reads and a visible loading phase. It does not claim to deserialize proprietary scene formats, create a rendered level, or implement gameplay.

## ML-01 — Index every serialized PCK resource

Keep the internal resource name used by existing consumers and also retain the length-prefixed load path immediately preceding each serialized record. When the serialized header has no extension-bearing name, derive the display/member basename from that load path. Preserve offset and inferred extent behavior and keep the archive read-only.

**Acceptance:** `PckEntry` exposes both names; records without an extension-bearing internal name remain indexable; existing archive consumers can continue looking up their current names. The Mission 1 census contains 287 static and 117 dynamic candidate records, subject to the static research caveat that this is header-based coverage, not type-specific parsing.

**Status:** implemented in `GameRuntime/PckArchive`; the updated index has not yet been run against PG1 archives.

## ML-02 — Resolve and check the Mission 1 package

For the evidence-backed first mission only, locate `PG1/PG1.PCK` and `PG1/PG1D.PCK`, verify the required track, water group, environment and eight external-model load paths, and require `PG1.AI`, `PG1.WTR`, `OBJECTS.TXT` and `PFX.TXT` in the mission directory. Fail with the missing path/resource in the error.

**Acceptance:** package inspection succeeds for the extracted USA data set and rejects a missing archive, required archive resource, or loose companion with a specific diagnostic. Do not infer support for other missions from this Mission 1 manifest.

**Status:** connected to Mission Data START for Mission 1; package inspection success/failure has not been exercised at runtime.

## ML-03 — Read package data in recovered order with bounded progress

Inspect static archive records first, then dynamic archive records, followed by loose mission companions. Read in chunks no larger than 64 KiB and report stage, current item, completed bytes and item total through the caller callback. Stop on the first short read or I/O error.

**Acceptance:** the callback reports monotonically increasing byte counts per item/stage; no operation buffers an entire PCK or companion file; the sequence is static archive → loose water file → dynamic archive → AI → object definitions → effect counts. Progress is a host byte-read indicator and must not be described as the original game's item-based 405-count bar.

**Status:** implemented as a cancellable worker with live progress display. Static package-read callback order and extracted-package runtime behavior have not been exercised; original archive deserialization and effect/object initialization remain future work.

## ML-04 — Show the recovered loading view and stop at the first unsupported boundary

On Mission Data START, show the 640×448 `DATA/LOADING.VKI` background with Texas/8:00, the recorded intelligence/tip strings, stage/current-item feedback and a clearly labeled host byte-read bar. Escape/Triangle cancels the inspection and returns to the briefing. After package reads, identify the track fixture import stage; once its bounded geometry is ready, hand off to a visibly diagnostic track view. Never report that the complete level or gameplay is ready while deserialization, materials, camera and rendering remain unsupported.

**Status:** implemented for Mission 1. The clone uses exact HUD font glyph metrics (full-size title; glyph records pre-scaled once to 0.53 for all other original text), exact English copy/spacing and recorded baseline anchors/tints. The real still is drawn 1:1 over the logical canvas; the background's circular/grid/ruler geometry is baked, with no duplicate overlays. Five bar components use `messagebar.rgb` with recovered positions, GS fixed-point UV conversion, source rectangles, mirroring, byte-read fill and >99% cap condition. Sprite vertex colors are now mapped from the source's 7-bit GS RGBA representation to SDL tint; SDL source-over applies the decoded texture alpha once. Host stage/item/failure/completion detail remains in the existing diagnostic status field, not as competing original-screen labels; the visibly labeled `PACKAGE READ n%` text is the only added loading-copy. Escape/Triangle remains the host cancel action. See the target [asset-use audit](../../../docs/intro-property-audit.md#mission-loading-screen-2026-10-20) for visual capture, texture/alpha and timing differences.

**Visual review:** original reference `reference-captures/2026-10-17-level-loading-screen-user-capture.png` (1053×697 with emulator chrome; source report estimates viewport `(112,51)..(940,671)`) was inspected against deterministic 640×448 SDL software crops in `analysis/bar-fixed-mid.bmp` and `analysis/bar-fixed-complete.bmp`. Mid-read and complete captures now visibly show the source-tinted left fill and right cap, instead of the previous nearly invisible fill; the five-part source layout and transparent texture padding remain intact. Exact GS blend/filter and framebuffer parity are unresolved; the worker's byte fraction intentionally differs from the 405-item fill.

## ML-05 — Reach the first in-game level draw

The next vertical slice must cover the original level's resource deserialization/setup and the `ShStartLevelState::draw` call into the level-owned draw virtual. That is the first supported visual boundary after loading; the source trace shows start-state overlays drawn afterward. Do not substitute a texture or static card for the 3D scene. Track/model formats, transforms, camera, materials and draw traversal need their own evidence and implementation before this item can pass.

**Status:** first perspective/depth geometry glimpse implemented on `task/mission1-first-level-draw`: after the package-read worker completes, the bounded PG1 importer retains leaf 74/batch 5 and a fixed inspection camera feeds a CPU rasterizer with near/far clipping, perspective-correct source vertex colors and a Z-buffer. SDL presents the completed software image through one texture on the existing renderer. Back returns to briefing. The importer reproduces the recorded full-track census and exact boundary, but skips BSP node contents without checking child references. This diagnostic camera is not recovered from the game. It does not resolve `road_asphalt.rgb`, sample UVs, execute original BSP visibility/material passes, or deserialize proprietary level setup. This remains one diagnostic batch, not a complete or playable level. No fresh interactive capture has been made. T1/T2/T3/T4/T6 are partial; T5 remains open. The task branch also merges locally available master `bbc40bc` including SH Viewer changes; the bounded loading parser remains shared by both consumers. See target [rendering investigation](../../../docs/level-one-rendering.md).
