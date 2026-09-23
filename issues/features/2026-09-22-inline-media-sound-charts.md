---
id: MDA7
type: work
status: needs-verification
labels: [feature, terminal, agent-ui]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: 8161d006-7718-485d-ab39-17c42cf9bf55
rank: m
created: '2026-09-22'
source: Claude Code in a Relay pane, 2026-09-22
links: {plans: [], commits: [b5e8fd28, 2dcdd1b8, 4f577c43, e33cf68f, c916d46a, db252aec, ff8a58b5, ab83fbd8, da47dcbe, 17939eea, 4b99849e, 65c99fd7, fa46cfdb, b9a0a3b2, 9e63714c, 56ef8416, 6b9ecff4, ef40e101, 306804bf, a7a3ca3a, 971df555, bb2912b2, b67049da, e2331a6f, 2b93624f], evidence: [docs/qa_evidence/2026-09-23-inline-media/], related: [1MGS, NY7Z], github: null}
---
# Inline media after images: playable sound, interactive charts, and the rest

## Issue
when done, allow relay to show in-line playable sound widgets the same way. and think if there are other multimodal functions we need. showing jupyter/flask style interactive charts etc would be really great, if its doable.

## Discussion points
Starts after #1MGS lands. Everything here reuses its layer: link-anchored rows in the grid that the
view paints over (`engine/core/InlineImage.h`), so an object scrolls, clears and restores with its
text in both cores.

What this machine has (checked 2026-09-22): Qt Svg and an optional Qt Pdf. There is **no
QtWebEngine and no QtMultimedia**, and Relay has avoided QtWebEngine on purpose (`src/ProfilePane.h`,
`docs/PROFILING.md` section 4). Voice already records through the desktop's own CLI tools
(`src/Voice.h`: pw-record, parecord, arecord, ffmpeg), with no audio library.

Proposed, in order:

1. **Sound player row.** A `relay-media:` sibling of the image URI; the view paints a one-row
   player: play/pause, a waveform thumbnail, position/duration, and click-to-seek. Playback goes
   through the mirror of Voice's tools (pw-play, paplay, aplay, ffplay), so there is no new
   dependency. One clip plays at a time, and it stops when its pane closes. Sources: an agent reply
   linking a local audio file, a prompt attachment, and programs through a `relay-show` helper (below).
2. **`relay-show <file|->`**, a small CLI in the shell's PATH that works like `imgcat` for every kind:
   it sniffs the type and emits Relay's escape. That gives programs and agents one way to show
   anything, since there is no standard terminal escape for audio or HTML.
3. **More still images for free**: SVG (Qt Svg, which covers mermaid/graphviz output), animated
   GIF/WebP that plays inline, PDF pages as images when Qt Pdf is built, and `MPLBACKEND` set to
   a kitty backend so matplotlib's `plt.show()` draws inline with no setup.
4. **Interactive charts** (plotly, bokeh, altair/vega, folium, and localhost Flask/Dash apps).
   There are three ways to do this; see question 1.
5. Later, if wanted: LaTeX math in agent replies (rendered to an image row), rich tables for
   CSV/dataframes (sortable, drawn natively), video as a poster frame that opens in the system
   player, and reading a reply aloud with TTS (the reverse of voice input).

## Decisions
- 2026-09-22, the owner: "yes to all of these ideas -- tables, video, read-aloud, and latex math -- definitely, add that now. for charts, (b) for now. for sound, if qt is better let me know".
  - Scope: the sound player, `relay-show`, SVG/GIF/PDF/matplotlib, tables, video, read-aloud and LaTeX math.
  - Charts: (b). A static snapshot inline; a click opens the live HTML or localhost URL in the browser.
  - Sound: QtMultimedia as an optional component, because it is better where it is built (FFmpeg backend in Qt 6.8, the installers' Qt: real pause, seek and position on all three platforms). The CLI tools are the fallback for builds without it (Qt5 Linux, like this machine). Read-aloud does the same with QtTextToSpeech, falling back to spd-say/espeak-ng/say.

## Tasks

- [x] Sound player row (QtMultimedia optional, CLI fallback): play/pause, waveform, position, seek <!-- t:9s -->
- [x] relay-show CLI: any file or stdin, type-sniffed, emits Relay's escape <!-- t:8a -->
- [x] SVG, animated GIF/WebP, PDF pages, matplotlib inline backend <!-- t:vj -->
- [x] Charts (b): HTML/plotly/bokeh/altair/localhost as an inline snapshot, click opens live in the browser <!-- t:q1 -->
- [x] Tables: CSV/TSV/dataframe output and Markdown tables as a native sortable table <!-- t:ym -->
- [x] Video: poster frame inline, click plays in the system player <!-- t:ms -->
- [x] Read-aloud: speak a reply (QtTextToSpeech optional, CLI fallback) <!-- t:cy -->
- [x] LaTeX math in agent replies ($...$, $$...$$) rendered as image rows <!-- t:sq -->

## Plan
**Goal.** Extend the image-row layer with typed media rows and local rendering helpers, preserving ordinary terminal history and link behavior.

**Findings.** `engine/core/InlineImage.*` defines row anchors; `engine/view/TerminalView.*` paints and hit-tests them; `src/MarkdownAnsi.*` renders agent prose; `src/Pane.h` handles prompt attachments. The current Qt 6.8 install has SVG but lacks Multimedia, Speech, and PDF. Read-aloud is already landed in `src/Speech.*`.

**Steps.** 1. Add bounded media contracts and a `relay-show` helper for files/stdin. 2. Add the audio player, optional QtMultimedia playback, and CLI fallback. 3. Add still/animated/PDF/video preview handling and matplotlib integration. 4. Add chart snapshots with click-through, sortable native tables, and math rendering. 5. Verify with targeted tests and an isolated live drive, record evidence, and move #MDA7 to needs-verification.

**Risks.** Other sessions share `CMakeLists.txt` and pane sources; claim paths just before editing and review contested hunks. The Qt Multimedia, Speech, and PDF paths need separate builds when those modules are installed.

**Verify.** Focused engine/view/Markdown tests, `relay-show` CLI tests, a targeted `scripts/relay-build`, and screenshots of actual widgets and click actions under Xvfb.

## Done means
1. A local audio file linked in an agent reply, attached to a prompt, or emitted by `relay-show` draws a one-row player with play/pause, waveform, elapsed/duration, and seek. Playback stops when its pane closes.
2. `relay-show` accepts a path or stdin, detects supported media from its contents, and emits a bounded inline representation. SVG, animated GIF/WebP, PDF pages, and video render useful previews; unsupported or corrupt input yields a readable error without damaging subsequent terminal text.
3. A local or localhost interactive chart shows an inline static snapshot and opens its live page on click. CSV/TSV/dataframe and Markdown tables open in a native sortable table; LaTeX math in replies renders inline.
4. The above works with optional Qt modules where present and practical fallbacks where absent. Targeted tests and live isolated-profile screenshots show the behavior; clear, scroll, and restart follow the image layer.

## Execution Summary
Built one `relay-media:` row layer for audio, table, and preview widgets. `relay-show` accepts local files, stdin, and local chart URLs; the view draws playable audio controls, sortable CSV/TSV and Markdown tables, HTML chart snapshots, video posters, SVG/PDF previews, and animated GIF/WebP. Agent replies and prompt attachments use the same rows. `plt.show()` in a new local pane uses the Relay Matplotlib backend. Agent `$...$` and `$$...$$` render as images; read-aloud was already landed. Media links survive saved scrollback and a restart while source files exist. Usage: `docs/INLINE_MEDIA.md`.

![Audio controls and CSV table row](docs/qa_evidence/2026-09-23-inline-media/01-sound-table.png)

![Native sortable table opened from the CSV row](docs/qa_evidence/2026-09-23-inline-media/07-table-open.png)

![Chart snapshot and video poster](docs/qa_evidence/2026-09-23-inline-media/02-chart-video.png)

![Agent audio, LaTeX math, and Markdown table](docs/qa_evidence/2026-09-23-inline-media/03-agent-math.png)

![Media restored after restarting Relay](docs/qa_evidence/2026-09-23-inline-media/04-restored.png)

![SVG, PDF, and animated GIF](docs/qa_evidence/2026-09-23-inline-media/05-svg-pdf-animation.png)

The full drive and additional GIF/WebP frames are in `docs/qa_evidence/2026-09-23-inline-media/`. The GIF's sampled pixel changed between captures. This machine's Qt 6.8 install lacks Multimedia, Speech and PDF, so their optional Qt code paths remain uncompiled here; CLI audio, speech and PDF fallbacks ran. GhosttyCore is also unavailable on this machine.

## Tests
- `ctest -R relay-engine-tests` — engine/tests/ViewTest.cpp
- `ctest -R markdown` — tests/markdownansi_test.cpp
- `ctest -R speech` — tests/speech_test.cpp
- manual: docs/qa_evidence/2026-09-23-inline-media/

Direct checks also passed: `scripts/relay-build --target relay relay-engine-tests relay-markdown-tests relay-speech-tests`; focused ViewTest 74/74, MarkdownAnsiTest 33/33, SpeechTests 13/13; `python3 -m unittest tests.relay_show_test tests.relay_render_math_test` (10/10); and `/tmp/relay-mda7-mpl-venv/bin/python -m unittest tests.relay_mpl_backend_test` (1/1 with Matplotlib). The live GIF pixel changed from (136, 68, 221) to (255, 136, 68).

### Check 2026-09-23 19:31
- passed · ctest:relay-engine-tests — ctest -R relay-engine-tests passed for this revision on spark-dcc9, 2026-09-23T23:31:27Z
- passed · ctest:markdown — ctest -R markdown passed for this revision on spark-dcc9, 2026-09-23T23:31:27Z
- passed · ctest:speech — ctest -R speech passed for this revision on spark-dcc9, 2026-09-23T23:31:27Z
- not-applicable · manual:docs/qa_evidence/2026-09-23-inline-media/ — manual evidence, recorded by hand: docs/qa_evidence/2026-09-23-inline-media/
- notice · ctest:relay-engine-tests — ctest -R relay-engine-tests is slow: p95 31.60 s, p50 31.60 s
- notice · ctest:speech — ctest -R speech is slow: p95 2.98 s, p50 2.98 s
history: thread
