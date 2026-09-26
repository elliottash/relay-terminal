---
id: 4DS8
type: work
status: needs-verification
assignee: agent
implemented_by: glm/glm-5.3
session: 645455b4-de7d-4037-8864-21df8fd45542
rank: zzzzzzzzzzzzzzzr
created: '2026-09-20'
links: {commits: [355c1c27d031aec132fb98ce45329821ef8666d7, 85020a3060d9c208c612b5c1d1b294e81ed4e058], evidence: [docs/qa_evidence/2026-09-25-voice-microphone-dropdown/, docs/qa_evidence/2026-09-25-tryit-4DS8/], github: null, plans: [], related: []}
---
# in options, make "microphone" a dropdown from available microphones, rather than…

## Issue
in options, make "microphone" a dropdown from available microphones, rather than a text field.

## Done means
Options › Voice › Microphone is a dropdown listing the capture sources this machine actually has (plus a "Desktop default" entry that keeps today's empty-value behaviour), and choosing one stores it in `voice/device` so the next recording uses that source. Failure shows as: the row still renders as a free-text box; the dropdown is empty or lists devices of a tool that is not the one capture would use; or a chosen device is written but the recording still comes from the default source.

## Plan
**Goal.** Replace the free-text **Microphone** row on Options › Voice with a dropdown of the machine's real capture sources, keeping an explicit "Desktop default" choice that stores today's empty value.

**Findings** (re-checked 2026-09-25: still accurate; only file locations moved, because the `settingsSections()` bodies were split out of the header into `.cpp` files, card #243T).
- The row is `textRow("voice/device", "Microphone", …)` in the Voice section of `settingsSections()`, **`src/RelayWindowSettings.cpp:906`**. The section already runs capture-tool detection at build time (`option:voice_recorder` info row, lines 911–919, using `relay::voice::chooseTool(QSettings().value("voice/tool"), relay::voice::toolOnPath)`).
- The stored string flows verbatim into `relay::voice::Options::device` (`src/Voice.h:87`) and becomes a per-tool flag in `captureArguments()` (`src/Voice.cpp:79`): `--target=` for pw-record, `--device=` for parecord, `-D` for arecord, `-i` for ffmpeg `-f pulse`. So the dropdown must offer names in the *current* tool's namespace.
- `SettingRow::Choice` (`src/SettingsPane.h:72`) is the control: `options` + `optionLabels` + `current` + `onChoose`. Choice-row precedent in the same section: the Voice key row (`option:voice_hold_key`, `src/RelayWindowSettings.cpp:885`) builds ids/labels from `relay::voice::holdKeys()`/`holdKeyLabel()` and sets `reset`/`changed` by hand.
- `src/Voice.cpp` keeps every rule as a pure, testable function; `tests/voice_test.cpp` covers `captureArguments`, `chooseTool` and the hold keys without audio hardware (confirmed present). Enumeration must follow that pattern: pure parsers for tool listings, one impure runner.

**Steps.**
1. `src/Voice.h` / `src/Voice.cpp` — add pure parsers, one per listing format:
   - `QList<QPair<QString,QString>> sourcesFromPactl(const QString &text)` — parse `pactl list sources` blocks, returning (source name → `Description:`). Use the **full** listing, not `pactl list short sources`: the short format has no Description column. Covers pw-record, parecord and ffmpeg (`-f pulse` is the PulseAudio namespace on both PulseAudio and PipeWire). Skip sources whose name ends in `.monitor` — those are desktop outputs, not microphones.
   - `QList<QPair<QString,QString>> devicesFromArecord(const QString &text)` — parse `arecord -L` (`name` + indented description lines).
2. `src/Voice.h` / `src/Voice.cpp` — add one impure runner `QList<QPair<QString,QString>> captureDevices(const QString &tool)` that picks the right listing command for `tool` (`pactl list sources` for pw-record/parecord/ffmpeg, `arecord -L` for arecord), runs it via `QProcess` with a short timeout (~2 s), Linux-only (empty list on Windows/macOS, matching `toolOnPath`), and feeds the output to the step-1 parsers. Missing binary or timeout → empty list.
3. `src/RelayWindowSettings.cpp` Voice section (~line 906) — replace the `textRow` with a Choice row `option:voice_device`:
   - Determine the tool the way the info row below already does (`chooseTool("voice/tool", toolOnPath)`), then `captureDevices(tool)`.
   - First option: value `""`, label "Desktop default". Then each enumerated (name, description).
   - If the stored `voice/device` is non-empty but absent from the list, append it as a choice labelled e.g. "<name> (not currently available)" so a choice control never silently drops the saved value.
   - `onChoose`: write `voice/device`, or `QSettings().remove` for the default (mirrors the hold-key row's empty-value handling); `reset` removes the key; `changed = QSettings().contains("voice/device")`.
   - Keep the `option:voice_recorder` info row as is.
4. `tests/voice_test.cpp` — add parser cases: a realistic `pactl list sources` blob (several sources, one with a missing Description, one `.monitor` source that must be skipped), a realistic `arecord -L` blob, and empty/garbage input → empty list.

**Risks.**
- Enumeration runs a subprocess when the Voice page is (re)built. The ~2 s timeout bounds it, and precedent exists (the Local models page probes ports at build; this page already runs `chooseTool`), but keep the runner synchronous and cheap — no caching layer.
- A source unplugged after the page was built leaves a stale entry until the pane rebuilds; acceptable — SettingsWatch already redraws, and the "(not currently available)" entry covers the persisted case.
- `.monitor` sources are excluded so the list is microphones, not every desktop output. If transcribing desktop audio ever matters, dropping that one filter line is a trivial follow-up — say so and it is done.
- No free-text escape hatch remains for exotic setups (e.g. a remote Pulse server typed by hand). "Desktop default" covers the common case; if the owner wants manual entry back, that is a follow-up card, not this one.

**Verify.**
- `ctest --test-dir build -R voice` after `scripts/relay-build` (new parser tests included).
- Live: open Options › Voice on a Linux desktop — the Microphone row is a dropdown whose entries match the non-`.monitor` names of `pactl list sources` (or `arecord -L` on an ALSA-only box); pick a non-default source, start a voice recording, and confirm the tool runs with the matching `--target=`/`--device=`/`-D` flag.

## Execution Summary
Landed in `355c1c27d031aec132fb98ce45329821ef8666d7` on `main` (land.py session `relay-4ds8`; the verify slot built the exact tree).

- `src/Voice.{h,cpp}`: pure parsers `sourcesFromPactl` (full `pactl list sources` blocks, `.monitor` skipped, lower-case property look-alikes ignored), `devicesFromArecord` (`arecord -L`, margin names + indented descriptions joined; margin lines containing spaces are log noise, not PCM names), `sourcesFromPwDump` (`pw-dump Node` JSON, `media.class = Audio/Source`, `device.class = monitor`/`.monitor` skipped), and one impure runner `captureDevices(tool)`: pactl for pw-record/parecord/ffmpeg, `arecord -L` for arecord, **pw-dump fallback for pw-record when pactl is missing or fails** (this machine has no pactl), ~2 s start/finish timeouts, empty list on any failure, non-Linux → empty.
- `src/RelayWindowSettings.cpp`: the Microphone `textRow` is now Choice row `option:voice_device` — "Desktop default" (stores the old empty value; choosing it removes `voice/device`), then `name · description` per source, then `<stored> · not currently available` if the saved source is not enumerated; `reset` removes the key, `changed = contains(voice/device)`; the `option:voice_recorder` info row untouched.
- `tests/voice_test.cpp`: four new cases — realistic pactl blob (missing Description, `.monitor` skipped, property look-alikes), realistic `arecord -L` blob, `pw-dump Node` blob captured from this machine plus a monitor and a V4L2 node, and empty/garbage input answering nothing (including `captureDevices` for a non-tool).
- Evidence: `docs/qa_evidence/2026-09-25-voice-microphone-dropdown/` — `implementer-voice-tests.txt` (16 passed, `ctest -R voice` green) and `implementer-capture-devices.txt` (runner end-to-end here + `pw-record --target=<name>` produces a valid WAV).

Not done here, on purpose (per the plan's Risks): no manual free-text entry for exotic setups; desktop-audio capture stays excluded with the microphones-only filter.

## Tests
- `ctest --test-dir build -R voice` — Passed (1/1). `relay-voice-tests`: 16 passed, 0 failed, including the four new parser cases; output in `implementer-voice-tests.txt`.
- Full `relay` target built through `scripts/relay-build` and the land.py verify slot built the exact landed tree (`relay` target) before the commit was swapped in.
- Live on this machine (PipeWire, `pw-record`, no `pactl`): the one-off runner binary returns the webcam source, and `pw-record --target=<that name>` produced a valid 2 s WAV (deleted after); see `implementer-capture-devices.txt`.
- Not covered by a machine here: the `pactl` branch and the `arecord -L` branch run only on machines with those binaries — both are exercised through their pure parsers' fixtures; a verifying session with pactl installed can re-run the dropdown against `pactl list sources` live.

## Try it
**The one task.** Open **Options › Voice** in your Relay and look at the **Microphone** row: it should be a dropdown offering `Desktop default` and `alsa_input.usb-046d_HD_Pro_Webcam_C920_B38639BF-02.analog-stereo · C920 PRO HD Webcam Analog Stereo` — pick the webcam, then make one voice-key recording and check it comes from the webcam. (Or run `docs/qa_evidence/2026-09-25-tryit-4DS8/stage.sh`, which does the pick under a sandbox and leaves the dropdown open, the choice taken, and the `device=` value it wrote; `staging-notes.md` explains the staging and `expected.md` seals what a pass looks like.)

## Human QA
1. The dropdown lists microphones only — desktop-audio/`.monitor` sources and manual free-text entry are excluded (the plan's call). Is that the list you want, or should the row also offer desktop audio / a free-text escape hatch?

   Answer:
