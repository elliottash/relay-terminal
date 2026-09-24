<!-- relay:entry 20260919T022500Z-v8 author=agent kind=note -->
### Agent · 2026-09-19 02:25
Filed from a conversation; findings from reading the current implementation (card #XM0T, `src/PaneStatus.{h,cpp}`, `src/PaneChrome.h`, `docs/ARCHITECTURE.md` "Pane types, pane states and remote sessions").

**What says "running" today — all of it static:**

- The pane header carries one 14×14 glyph before the title (`PaneChrome::PaneStateGlyph` → `paintStateGlyph`): ring = idle, triangle = command running, four-point star = agent working, star + two dots = subagents. Shape and colour only; nothing moves. 14 px inside a 26 px header band.
- The tab icon is a 16 px static picture of the most urgent state among the tab's panes (`tabIcon`), repainted only when the state *changes* (`RelayWindow::refreshPaneStatus()`, 400 ms poll).
- The only animated element is the prompt-box waiting line ("waiting for 2 subagents . . ." with growing dots) — but only when the main agent is *blocked* on background work, and it lives in the prompt box, not the header or tab.
- So the live states (Running, Working, Subagents) have no time dimension at all: a star reads no more "happening now" than a ring unless you already know the code, and a background tab's icon is a still picture that could be stale. Done/Failed/NeedsYou pull the eye by being news, not by being live.

**Directions (owner to pick; can combine):**

1. **Animate the live glyph.** Pulse or phase the Running/Working/Subagents glyph — motion is the universal "in progress" signal, needs no colour (consistent with #XM0T's "each has its own shape, so none depends on colour"), and matches the waiting-dots language already in the app. Needs a repaint tick only while a live state is up (the waiting line already ticks off `QApplication::cursorFlashTime`), and should respect a reduced-motion setting.
2. **Strengthen the mark.** A bigger glyph, or the state's word beside the title ("Relaying…", "Command running" — the strings exist in `stateLabel()`).
3. **Carry liveness to the tab.** A busy mark on the tab icon (pulsing dot or spinner overlay) so a background tab reads busy without being opened — the tab is where "is something running over there?" is actually asked.

<!-- relay:entry 20260919T024200Z-d4 author=owner kind=decision -->
### Elliott Ash · 2026-09-19 02:42
i want 1, 2, 3, i think, and the icons / anims should use blue for terminal work happening and violet for agent work happening

<!-- relay:entry 20260919T024300Z-e2 author=agent kind=event -->
- ✦ agent moved Inbox → In progress · implementing all three directions; `stateInk` already maps Running to `shell` (blue) and Working/Subagents to `agent` (violet), confirmed as the decision

<!-- relay:entry 20260919T030500Z-f1 author=agent kind=note -->
### Agent · 2026-09-19 03:05
All three directions implemented, blue/violet per the decision:

- **Rules** (`src/PaneStatus.{h,cpp}`): `isLive()` (Running/Working/Subagents), `liveMarker()` for a tab's mark (Working wins over Running — agent work outranks a command when both run), `pulseScale()` — four breath steps `{1.0, 0.90, 0.82, 0.90}`, a scale never an opacity so the ink keeps its contrast — and `stateText()`, the state's ink lifted to 4.5:1 for the header word.
- **Pane header** (`src/PaneChrome.h`): `PaneStateGlyph` is 16×16 and breathes while its state is live, on the waiting dots' own 600 ms clock, still at full size when `cursorFlashTime()` is 0 (the app's reduce-motion signal). A painted `PaneStateWord` beside the title says "Command running" / "Relaying…" / "Subagents working" in the state's colour; the title re-elides around it.
- **Tab** (`src/PaneChrome.h` `tabIcon`, `src/RelayWindow.h` `refreshPaneStatus`): the tab icon breathes when it *is* the live state, and otherwise carries a breathing corner dot in the live state's colour (blue command, violet agent) — so a news icon (done, failed, needs you) never hides that a sibling pane is busy. The dot takes the corner across from the ssh mark. The 400 ms poll drives the phase; repaint only while something is live.
- **Tests**: `tests/panestatus_test.cpp` — `liveStatesAndTheirMarks` (which states are live, the tab's mark and its precedence, the breath's shape, rest-not-smaller) and `theStateWordIsLegibleOnEveryGround` (4.5:1 on the pane background and the ssh band, every shipped theme).
- **Docs**: `docs/ARCHITECTURE.md` "Live states" paragraph after the States paragraph; `src/PaneStatus.h` header comment.

Validation: `cmake --build build` clean; full `ctest` 51/51 (307 s); `./scripts/test.sh` OK (2217 tests, 284 s).

<!-- relay:entry 20260919T033000Z-f2 author=agent kind=note -->
### Agent · 2026-09-19 03:30
Evidence in `docs/qa_evidence/2026-09-19-pane-live-state/` (Xvfb, isolated HOME/XDG, stub provider on 127.0.0.1:8803; scenes idle / running / relaying / mix). The first run reported **relaying: tab-bar frames differ in 0 pixels** while running and mix showed the pulse — diagnosed rather than re-rolled:

- `analyze.py` (pixel families + OCR) proved the relaying scene *was* in Working: violet star glyph + "Relaying…" word in the header, violet star on the tab, waiting line animating at the bottom of the window. The marks were painted; only the motion was missing from the two captured frames.
- `probe.cpp` (built by `build-probe.sh`, output in `probe-output.txt`) painted the real glyphs at every pulse phase offscreen: **every adjacent step moves 30–63 strongly-differing pixels at dpr 1** (more at 2) for every shape including the star — and **phases 1 and 3 paint identically**, because the breath's way out and way back both pass through 0.90.
- So the 0 was sampling aliasing: the harness's two frames were actually ~1.54 s apart (a 0.8 s settle sleep ran before each import, not just between them) — within 60 ms of the tab pulse's full 1.6 s period, and two steps apart lands on the repeated 0.90. The marks animate; the cadence could not see it.

Root-cause fixes (no downstream patching):

- `drive.sh` `pair()` now shoots **six frames 0.5 s apart** (spanning the longest period, 2.4 s) and reports the min/max pairwise difference across all pairs — periodic sampling can no longer alias a live pulse into "static". Regenerated notes: running 0..480, relaying 160..456, mix 0..444 px (a 0 minimum is the documented phase-1≡phase-3 pair; the max is the "it moves").
- New rendering test `tests/pulsepaint_test.cpp` (+ `pulsepaint_paint.cpp`, which keeps `PaneChrome.h` out of the moc'd TU — `Keymap.h`'s raw string literal breaks moc) pins the property the aliasing hid: every *adjacent* pulse step visibly moves the painted tab icon, corner dot and header glyph, at dpr 1 and 2. Registered as `pulsepaint` in CMake (offscreen); passes.

Full build clean; `ctest -R pulsepaint` passes; `analyze.py` on the regenerated frames confirms violet star/word and 336 px (bar) / 176 px (head) movement between adjacent relaying frames.

<!-- relay:entry 20260919T200544Z-nc author=agent kind=event model=glm-5.3 pane=switchboard turn=e17598d07151419d86ddfea89de60412/c1c5a69cae214f50bedb9384a2802d81 -->
- ✦ agent updated this card · labels: ["change"] → ["change", "bug"]
