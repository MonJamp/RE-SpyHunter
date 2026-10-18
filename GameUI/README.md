# GameUI

`GameUI` contains reusable frontend behavior and drawing primitives. `MenuSelection`
provides bounded, wraparound menu navigation; `MenuDrawing` provides shared blended
panels and pill selectors on an SDL renderer.

The boundary follows the original game's organization: common `ShMenu` facilities
are represented here, while concrete screens such as the profile/memory-pack menu
remain in `RE-SpyHunter`. Asset decoding and media playback stay in `GameRuntime`,
and PS2 SDK or hardware compatibility stays in `PS2Compatibility`.

Keep labels, profile data, screen transitions, recovered SpyHunter coordinates, and
other title policy out of this library. Add a general UI concept only when a concrete
game screen uses it.
