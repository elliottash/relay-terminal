# Inline media in Relay

`relay-show` displays a local file in a terminal pane. It accepts images, SVG, PDF,
audio, video, HTML charts, and CSV/TSV tables. It also accepts stdin (`-`) and a
local or localhost chart URL. Run `relay-show --help` for `--kind`, `--snapshot`,
`--url`, and `--name` options.

```sh
relay-show plot.svg
relay-show recording.wav
relay-show results.csv
relay-show chart.html
relay-show http://127.0.0.1:8050/ --kind chart
```

Audio rows have play/pause, elapsed time, duration, waveform, and click-to-seek.
Tables open a sortable native view. Video shows a poster frame and opens in the
system player. Charts show a snapshot and open the live page when clicked.
Animated GIF and WebP advance in the pane. Media scrolls and clears with terminal
text; saved pane history restores it while its source files still exist.

Agent replies can link local audio, show Markdown tables, and use `$...$` or
`$$...$$` for math. Prompt audio attachments have the same player. New local
pane shells get a Relay matplotlib backend if `MPLBACKEND` was not already set:
`plt.show()` writes a persistent PNG into Relay's media cache and shows it inline.

Preview tools are used when available: `rsvg-convert` for SVG, `pdftoppm` for PDF,
`ffmpeg` for video, and Chrome or Chromium for HTML snapshots. For a chart or
other preview, `--snapshot` can name an existing PNG, JPEG, GIF, or WebP instead.
The math renderer needs `latex`, `dvisvgm`, and `rsvg-convert`.

Qt 6 Multimedia supplies audio pause and seek when that module is installed at
build time. Otherwise Relay uses an available local command-line player (`pw-play`,
`paplay`, `aplay`, or `ffplay`). On this development machine, the Qt 6.8 install
lacks Multimedia, Speech, and PDF, so the optional Qt playback and speech code
has not been compiled here. `relay-show` uses the external PDF preview tool.

`relay-show` resolves file paths on the computer displaying Relay. A path on a
remote SSH host is not directly readable by Relay; copy the file locally first.
Chart URLs are limited to local files and loopback HTTP(S) pages.
