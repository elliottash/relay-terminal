# #MDA7 live QA evidence

`drive.sh` launches a real Relay window under Xvfb with an isolated HOME, a
loopback stub model, and libvterm. It generates its own media files, runs
`relay-show`, takes the screenshots below, quits and relaunches Relay, then
checks that saved scrollback still contains media links. Run it after building:

```sh
scripts/relay-build --target relay
docs/qa_evidence/2026-09-23-inline-media/drive.sh
```

| Screenshot | Observed behavior |
| --- | --- |
| `01-sound-table.png` | Audio player with waveform and duration, CSV sortable-table row. |
| `07-table-open.png` | Clicking the CSV row opens the native table. |
| `02-chart-video.png` | Local HTML chart snapshot and video poster inline. |
| `03-agent-math.png` | Agent-linked audio, rendered math, and a Markdown table row. |
| `04-restored.png` | Those media rows return after an app restart. |
| `05-svg-pdf-animation.png` | SVG, PDF preview, and animated GIF inline. |
| `06-animation-frame.png` | The GIF changes colour between captures; the drive checks a pixel. |
| `08-webp.png` | Animated WebP inline. |

The screenshots are taken from Relay's actual window, not a mockup. The drive
uses command-line fallbacks because this machine's Qt 6 install lacks Multimedia,
Speech, and PDF. Qt Multimedia and Qt Speech paths remain to be built on a
machine with those modules.
