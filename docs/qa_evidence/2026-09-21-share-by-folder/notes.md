# Share beside folder — SHP7

`scripts/relay-build --target relay` passed, build 2026-09-21.19H.01.

Isolated Xvfb GUI with separate XDG config/data/runtime and a fixture worker; no sharing connection or model call was made. `prompt.png` confirms the share icon immediately follows the folder, and the top-right pane controls occupy one row. Existing shareChipPressed behavior and RemoteShare state/guest tooltip updates are preserved by inspection.

`git diff --check -- src/Pane.h src/PaneChrome.h` passed. Board format check has no findings on this card/thread (other cards have existing findings).
