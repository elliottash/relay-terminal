---
id: MDA7
type: work
status: discussing
waiting_on: owner
labels: [feature, terminal, agent-ui]
assignee: claude-code
rank: m
created: '2026-09-22'
source: 'Claude Code in a Relay pane, 2026-09-22'
links: {plans: [], commits: [], evidence: [], related: [1MGS, NY7Z], github: null}
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
