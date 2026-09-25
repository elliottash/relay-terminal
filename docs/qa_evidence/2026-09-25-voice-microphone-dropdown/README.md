# Evidence — Microphone dropdown in Options › Voice (`#4DS8`)

Implementer evidence for
[`.board/features/2026-09-20-in-options-make-microphone-a-dropdown-from-avail.md`](../../../.board/features/2026-09-20-in-options-make-microphone-a-dropdown-from-avail.md)
(card `#4DS8`). **Not a QA verdict.** Files prefixed `implementer-` were produced by the
implementing session on 2026-09-25.

## What changed

The **Microphone** row on Options › Voice was a free-text box whose value had to be typed in the
capture tool's own spelling. It is now a dropdown (`option:voice_device`) listing the sources this
machine actually has, in the namespace of the tool that would record:

- `captureDevices(tool)` in `src/Voice.cpp` runs the tool's own listing — `pactl list sources` for
  pw-record, parecord and ffmpeg, `arecord -L` for arecord — with a ~2 s timeout, and answers an
  empty list on any failure, missing binary, or non-Linux platform.
- **pw-dump fallback (beyond the plan's letter, within its Done means):** this machine runs
  PipeWire with `pw-record` and **no `pactl`** (pipewire-tools without pulseaudio-utils), so the
  plan's pactl-only enumeration offered nothing here. When pactl is missing and pw-record is the
  tool, `pw-dump Node` is parsed instead (`sourcesFromPwDump`) — the same node names
  `pw-record --target=` takes. Monitors (`device.class = monitor` / `.monitor` names) are skipped,
  as in the pactl parser.
- The dropdown's first entry is **Desktop default**, which stores the old empty value (the key is
  removed, so the tool picks the source). A stored source that is not currently enumerated stays
  listed as `<name> · not currently available`, so the control never silently drops the saved
  value.

| File | What it shows |
|---|---|
| `implementer-voice-tests.txt` | `relay-voice-tests` (16 passed, 0 failed) and `ctest -R voice` — the three new parser tests cover a realistic `pactl list sources` blob (a source with no Description, a `.monitor` that must be skipped, lower-case look-alike properties that must not shadow the fields), a realistic `arecord -L` blob, a `pw-dump Node` blob captured from this machine plus a monitor and a video source, and empty/garbage input answering nothing. |
| `implementer-capture-devices.txt` | The runner end to end on this machine: `pactl` absent, `pw-record`/`pw-dump` present; a one-off binary linked against `build/librelay-voice.a` calls the same `captureDevices(chooseTool(...))` the settings row calls and gets the webcam source; a 2 s `pw-record --target=<that name>` capture produces a valid WAV (then deleted), proving the offered name steers the recorder. |

## What is left for a verifying session

Open Options › Voice in the built binary on a desktop session: the Microphone row should be a
dropdown whose entries are "Desktop default" plus the machine's sources (here: the C920 webcam
mic). Picking a source, recording with the voice key, and putting the row back to Desktop default
should each store/remove `voice/device` and change what `pw-record` runs with.
