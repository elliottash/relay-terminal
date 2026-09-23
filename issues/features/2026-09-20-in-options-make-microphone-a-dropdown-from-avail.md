---
id: 4DS8
type: work
status: planned
rank: zzzzzzzzzzzzzzzr
created: '2026-09-20'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# in options, make "microphone" a dropdown from available microphones, rather than…

## Issue
in options, make "microphone" a dropdown from available microphones, rather than a text field.

## Done means
Options › Voice › Microphone is a dropdown listing the capture sources this machine actually has (plus a "Desktop default" entry that keeps today's empty-value behaviour), and choosing one stores it in `voice/device` so the next recording uses that source. Failure shows as: the row still renders as a free-text box; the dropdown is empty or lists devices of a tool that is not the one capture would use; or a chosen device is written but the recording still comes from the default source.

## Plan
**Goal.** Replace the free-text **Microphone** row on Options › Voice with a dropdown of the machine's real capture sources, keeping an explicit "Desktop default" choice that stores today's empty value.

**Findings.**
- The row is `textRow("voice/device", "Microphone", …)` in the Voice section of `settingsSections()`, `src/RelayWindow.h:3993`. The section already runs capture-tool detection at build time (`option:voice_recorder` info row, lines 3998–4006, using `relay::voice::chooseTool(QSettings().value("voice/tool"), relay::voice::toolOnPath)`).
- The stored string flows verbatim into `relay::voice::Options::device` (`src/Voice.h:86`) and becomes a per-tool flag in `captureArguments()` (`src/Voice.cpp:79`): `--target=` for pw-record, `--device=` for parecord, `-D` for arecord, `-i` for ffmpeg `-f pulse`. So the dropdown must offer names in the *current* tool's namespace.
- `SettingRow::Choice` (`src/SettingsPane.h:71`) is the control: `options` + `optionLabels` + `current` + `onChoose`. Choice-row precedent in the same section: the Voice key row (`src/RelayWindow.h:3976`) builds ids/labels from `relay::voice::holdKeys()`/`holdKeyLabel()` and sets `reset`/`changed` by hand.
- `src/Voice.cpp` keeps every rule as a pure, testable function; `tests/voice_test.cpp` covers them without audio hardware. Enumeration must follow that pattern: pure parsers for tool listings, one impure runner.

**Steps.**
1. `src/Voice.h` / `src/Voice.cpp` — add pure parsers, one per listing format:
   - `QList<QPair<QString,QString>> sourcesFromPactl(const QString &text)` — parse `pactl list sources` blocks (or `pactl list short sources`), returning (source name → `Description:`); covers pw-record, parecord and ffmpeg (`-f pulse` is the PulseAudio namespace on both PulseAudio and PipeWire).
   - `QList<QPair<QString,QString>> devicesFromArecord(const QString &text)` — parse `arecord -L` (`name` + indented description lines).
2. `src/Voice.h` / `src/Voice.cpp` — add one impure runner `QList<QPair<QString,QString>> captureDevices(const QString &tool)` that picks the right listing command for `tool` (`pactl …` for pw-record/parecord/ffmpeg, `arecord -L` for arecord), runs it via `QProcess` with a short timeout (~2 s), Linux-only (empty list on Windows/macOS, matching `toolOnPath`), and feeds the output to the step-1 parsers. Missing binary or timeout → empty list.
3. `src/RelayWindow.h` Voice section (~line 3993) — replace the `textRow` with a Choice row `option:voice_device`:
   - Determine the tool the way the info row below already does (`chooseTool("voice/tool", toolOnPath)`), then `captureDevices(tool)`.
   - First option: value `""`, label "Desktop default". Then each enumerated (name, description).
   - If the stored `voice/device` is non-empty but absent from the list, append it as a choice labelled e.g. "<name> (not currently available)" so a choice control never silently drops the saved value.
   - `onChoose`: write `voice/device`, or `QSettings().remove` for the default (mirrors the hold-key row's empty-value handling); `reset` removes the key; `changed = QSettings().contains("voice/device")`.
   - Keep the `option:voice_recorder` info row as is.
4. `tests/voice_test.cpp` — add parser cases: a realistic `pactl list sources` blob (several sources, one with a missing Description), a realistic `arecord -L` blob, and empty/garbage input → empty list.

**Risks.**
- Enumeration runs a subprocess when the Voice page is (re)built. The ~2 s timeout bounds it, and precedent exists (the Local models page probes ports at build; this page already runs `chooseTool`), but keep the runner synchronous and cheap — no caching layer.
- A source unplugged after the page was built leaves a stale entry until the pane rebuilds; acceptable — SettingsWatch already redraws, and the "(not currently available)" entry covers the persisted case.
- No free-text escape hatch remains for exotic setups (e.g. a remote Pulse server typed by hand). "Desktop default" covers the common case; if the owner wants manual entry back, that is a follow-up card, not this one.

**Verify.**
- `ctest --test-dir build -R voice` after `scripts/relay-build` (new parser tests included).
- Live: open Options › Voice on a Linux desktop — the Microphone row is a dropdown whose entries match `pactl list short sources` (or `arecord -L` on an ALSA-only box); pick a non-default source, start a voice recording, and confirm the tool runs with the matching `--target=`/`--device=`/`-D` flag.
