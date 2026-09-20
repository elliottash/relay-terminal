<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# A scroll as a shift: before and after — #3H5T item 1

The fix is `79cae3ef` (`remote: a scroll reaches the phone as a shift, not a new screen`) plus the
one-line guard below. The finding it answers is section 3.1 of
[`../phone/RESULTS.md`](../phone/RESULTS.md): a line of streamed output at the bottom of the screen
scrolls the viewport, and that was sent to the phone as every row — 166 `screen_snapshot` of
7 747 B against 86 `screen_diff` of 375 B for a 20 000-character reply, 1.50 MB on the air and
82 % of one core in Chrome.

Two measurements, because the finding has two halves. The bytes are measured on spark against
`relay-screen-bridge` built from the commit and from its parent — the same serializer
(`engine/tools/ScreenJson.h`) a GUI pane share uses, so this is the screen stream itself, not a
proxy. The phone's cost is measured on the owner's Pixel 8, running the real `app/screen.js` over
the same two streams.

## 1. Bytes on the screen stream — `relay-screen-bridge`, `79cae3ef^` against `79cae3ef`

`scripts/bridgebytes.py` starts the bridge on a `/bin/sh`, types one of the scenario scripts beside
it, and counts the JSON it writes, by message. Raw output:
[`evidence/bridge-bytes.txt`](evidence/bridge-bytes.txt).

| 24 s of a streamed reply (`scripts/reply.sh`: 200 lines, styled, ten a second) | before | after |
| --- | --- | --- |
| screen stream | **679 798 B** | **108 088 B** — **−84.1 %** |
| whole-screen `snapshot` | ×190, 674 390 B, avg 3 549 B | — |
| `diff` carrying `scroll` | — | ×190, 102 680 B, **avg 540 B** |
| ordinary `diff` | ×11, 5 408 B | ×11, 5 408 B |

| the same, unstyled (`scripts/prose.sh`) | before | after |
| --- | --- | --- |
| screen stream | 534 898 B | 94 288 B — −82.4 % |

| 50 MB through the terminal (`scripts/firehose.sh`) | before | after |
| --- | --- | --- |
| screen stream | 75 427 B in 27 snapshots | 72 636 B in 26 snapshots — unchanged per frame |

The firehose is unchanged **on purpose**, and it is the reason for the guard in `frameOf()`: a
frame that scrolls by more rows than the screen holds has nothing to carry over, so the shift saves
nothing and the 35 bytes of the `scroll` object would make it 3 B/frame *worse*. The scroll form is
now used only when fewer rows changed than the screen has. `../phone/RESULTS.md` already found the
firehose protected by the desktop's frame coalescing — "it is the *slow* stream that costs".

## 2. The phone — Google Pixel 8, Android 17, Chrome 153

`scripts/bench.html` is the real `app/screen.js` in the phone's Chrome with the desktop replaced by
the page: it builds both streams as the exact wire text up front, then, ten frames a second for
20 s, `JSON.parse`s one and `apply()`s it — what `app/rrp.js` does after decryption. Served over
plain HTTP on the LAN address only (`192.168.1.192:8931`), driven over
`adb forward` + CDP. No Relay, no share, no pairing, no key of any kind.

| 200 frames, 20 s | whole-screen snapshots | the shift |
| --- | --- | --- |
| bytes the page parsed | **730 890 B** | **61 690 B** — **−91.6 %** |
| Chrome CPU over the window (`/proc`, all its processes) | 7.31 s = 32.4 % of one core | 6.69 s = 29.7 % of one core |
| non-idle JS self time | **1 098 ms** | **810 ms** — −26 % |
| `span` + `replaceChildren` + `paintRow` (`app/screen.js`) | 85 ms | **43 ms** |
| the last row painted | `5223 relay ay the frame carries rows of style runs so the client needs no s` | **identical** |

Raw: [`evidence/phone-js-selftime.txt`](evidence/phone-js-selftime.txt),
[`evidence/phone-chrome-cpu-snapshot.txt`](evidence/phone-chrome-cpu-snapshot.txt),
[`evidence/phone-chrome-cpu-scroll.txt`](evidence/phone-chrome-cpu-scroll.txt).

**−91.6 % on the air is the headline and it matches the card's estimate** (1.50 MB → ~0.14 MB,
−91 %). **The CPU figure is smaller than the card predicted (−8 %, not the 82 %→low figure a reader
might infer), and the honest reason is where the remaining time goes**: `toBottom()`
(`app/screen.js:311`) is now the top frame of the profile at 133 ms, and the rest is layout, paint
and compositing of a screen that changes every frame either way. The card's 82 % of a core was a
*live* share, which also carries the Noise decrypt and the WebSocket stack over those 730 kB —
costs this bench does not have and the fix removes in proportion to the bytes. A live before/after
on a real share is the orchestrator's sphinxpad re-measurement, not this.

Both streams paint the same last row, which is the point of the browser test that landed with the
fix (`tests/test_remote_browser.py`, five shifts of 1 to 13 rows then the same screen sent whole:
the painted rows are identical).

## What was changed on the phone, and put back

| Change | Restored? |
| --- | --- |
| `adb forward tcp:9222` | **Yes** — removed, `adb forward --list` is empty |
| One Chrome tab on `http://192.168.1.192:8931/bench.html` | **Yes** — navigated to `about:blank` and closed over CDP; the tab that was open before is untouched |
| `settings put global stay_on_while_plugged_in 15` (the screen slept mid-window) | **Yes** — back to `0`, which is what `../phone/RESULTS.md` recorded as its value |
| Nothing installed, no `adb root`, no reboot, no page but the bench one touched | — |

## Reproducing

```
git archive <sha>   | tar -x -C <dir>          # and <sha>^ for the before
cmake -S <dir> -B <build> -DRELAY_BUILD_ENGINE=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build <build> --target relay-screen-bridge
QT_QPA_PLATFORM=offscreen python3 scripts/bridgebytes.py <build>/engine/relay-screen-bridge reply 24

# the phone: serve scripts/bench.html beside a copy of app/screen.js and app/style.css, then
adb -s <serial> forward tcp:9222 localabstract:chrome_devtools_remote
python3 ../phone/scripts/cdp.py eval "window.bench('snapshot', 200, 100).then(r => JSON.stringify(r))" bench.html
bash ../phone/scripts/chromecpu.sh <label> 22
```
