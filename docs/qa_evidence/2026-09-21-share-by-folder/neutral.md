# Neutral share button

Removed the share-specific `dest=agent` property. Theme.cpp uses that property to select the purple border; sharing now uses the same default stripChip rules as adjacent controls regardless of sharing state. Guest counts and sharing status remain in the tooltip.

Build: `scripts/relay-build --target relay` passed (2026-09-21.19H.06). Isolated Xvfb GUI inspected in `neutral.png`; no actual sharing session was started. The active-sharing styling is verified by the absence of a state-specific property in updateShareChip.
