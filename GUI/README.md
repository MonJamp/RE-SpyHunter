# GUI

`GUI` is the solution's SDL3 presentation library. It holds renderer-scale setup, small drawing primitives, text truncation, and aspect-ratio fitting so tools and the future game share a consistent 4K-friendly interface foundation. It deliberately does not own a window, event loop, or product-specific screens.

New reusable controls and screen composition helpers belong here once at least two consumers need them. Asset decoding and game behavior stay in `GameRuntime`.
