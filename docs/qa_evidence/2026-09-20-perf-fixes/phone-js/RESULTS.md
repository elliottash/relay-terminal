<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# #3H5T items 3 and 4, measured on the owner's Pixel 8: the `history_get` storm and `fit()`

The card's findings 3 and 4 (`docs/qa_evidence/2026-09-20-perf-fixes/phone/RESULTS.md`, sections
3 and 4): a streamed reply made the phone send **188 `history_get` in about five seconds**, 121
refused `rate_limited` and the refusal drawn in red under the composer; and `fit()` forced a style
recalculation and a layout flush on every frame, making it the top JS function of the whole
profile.

Fixed in `77b49d59` (`app/screen.js` + the bench and its tests), `df2fca6e` (`app/app.js`,
`app/guest.js`), `4536acce` (`docs/REMOTE-PROTOCOL.md` §10) and `c6e89ef5` (the bench's counters
behind `?count=1`).

## What was measured, and how

The same Pixel 8 (Android 17, Chrome 153.0.8010.52) over `adb`, in its own Chrome, against the
real `ScreenView` — `tests/screen_harness.html`, the bench that ships with the tests, with this
page playing the desktop. The page is served from spark over `adb reverse`, so the phone loads it
from **its own `127.0.0.1`**: a secure context for WebCrypto, no LAN listener, no share, no link.

A "frame" is one line of output at the live end: the live block moves down a row and every row on
screen changes, which is what the desktop sends today. The reader is where a reader watching an
agent type is — at the bottom, following the output.

* `scripts/phone.py <url> <label> [frames] [every_ms]` — opens a tab of its own over CDP, runs the
  scenario, reports the counts, the V8 sampling profile reduced to self time per function, and
  Chrome's CPU (utime+stime over every `com.android.chrome` process, the way
  `../phone/scripts/chromecpu.sh` does it) over the streaming window alone. It closes the tab it
  opened and touches nothing else.
* `scripts/measure.py <served tree>` — the same scenario in headless Chrome on spark, for a check
  that does not need the phone.
* **before** is the working tree as the card was filed, i.e. `app/*.js` at `1a057ac8`; **after** is
  a clean `git archive` of the tip with the four commits above in it. Raw output in `raw/`.

## 1. The storm

160 frames at 185 ms — the rate the card measured (5.4 screen snapshots a second over a
30-second reply), which is scenario (a)'s shape:

| over 30 s of streamed output | before | after |
| --- | --- | --- |
| `history_get` sent | **160** | **30** |
| over the desktop's budget (`(120, 60)`, `remote/host.py:58`) | **40 refused** | **0** |
| `fit` self time | **691.8 ms** | **0.3 ms** |
| `fit` share of non-idle JS | **55.8 %** | **0.0 %** |
| non-idle JS | 1 240.6 ms | **724.1 ms** (−42 %) |
| Chrome CPU over the window | 38.0 % of one core | **31.9 %** (−16 %) |

The 30 that remain are the live cadence: while the reader is at the live end the seam is closed
once a second rather than once a frame (`HISTORY_LIVE_INTERVAL`), which is half the budget. Moving
off the bottom drops to 250 ms (`HISTORY_INTERVAL`), because that is when the seam is in front of
them. Section 6.5 still holds — the column is never painted with a hole in it, and
`tests/test_remote_browser.py`'s `test_scrollback_pages_in_when_you_drag_the_terminal_down` is the
test of that, unchanged and passing.

Two harder rates, for the shape of it:

| 200 frames at 10 ms (2 s) | before | after |
| --- | --- | --- |
| `history_get` | 200 | **2** |
| `getComputedStyle(#screen-wrap)` | 401 | **0** |
| `clientWidth(#screen-wrap)` | 401 | **0** |
| `fit` | 416.8 ms = 55.3 % of non-idle JS | **0.3 ms = 0.1 %** |
| non-idle JS | 754.4 ms | 428.8 ms |

| 2 000 frames at 10 ms | before | after |
| --- | --- | --- |
| `history_get` | **1 722** | **26** |
| over budget | 1 602 refused | **0** |
| `fit` | 15 040 ms = 70.8 % of non-idle JS | **15 ms = 0.1 %** |
| non-idle JS | 21 251 ms | 14 215 ms |
| wall for the same 2 000 frames | 29.6 s | **23.3 s** — the phone keeps up better |
| Chrome CPU per frame | 30.1 ms | **23.8 ms** (−21 %) |

The count columns are taken with `?count=1`, which installs wrappers on `getComputedStyle` and the
container's `clientWidth`; the profile columns are taken without, because the wrappers are JS
frames of their own and collect the layout read's cost under `get clientWidth` instead of under
`fit`. Same page, separate runs (`c6e89ef5`).

## 2. The refusal

`rate_limited` on a `history_get` now goes to the console and the view backs off for two seconds;
it never reaches `thread-note`. `tests/test_web_screen.py`'s
`test_a_refused_page_is_never_the_readers_problem` is the end-to-end proof: a real host with the
budget turned down to `(1, 60)` so every page after the first is refused, a real browser, a reader
up in the scrollback, 200 frames of output. It asserts the composer's note is empty, that the
refusal was logged, that fewer than twelve requests were even attempted — and then, with the
budget restored, that the column joins up again, because a rate limit is not the end of the
desktop's scrollback and no longer clears `more`.

Against the pre-fix client the same test reports **200 `history_get` for 200 frames** and never
reaches the recovery step.

## 3. What is left, and it is not this card's

`toBottom()` is now the per-frame layout cost: `this.root.scrollTop = this.root.scrollHeight` is a
read and a write of scroll geometry on every frame, and with `fit()` out of the way it carries the
whole flush — 294.9 ms of the 428.8 ms of non-idle JS in the 200-frame run, 498.4 ms of 724.1 ms
at the live rate. Removing it is not a tweak: following the live end without measuring the
document means a bottom-anchored layout (`overflow-anchor`, or a reversed flex column), which
changes how scrollback is laid out and belongs with whoever owns the frame path. It is worth a
card of its own; it is not worth doing as a side effect of this one.

## The phone, and what was changed on it

| Change | Restored? |
| --- | --- |
| `adb reverse tcp:8097/8098/8099` (the bench served from spark to the phone's own localhost) | **Yes** — `adb reverse --remove-all`, list empty |
| Three Chrome tabs on `http://127.0.0.1:809x/tests/screen_harness.html`, one per run | **Yes** — each closed by `Target.closeTarget` on the id this script created; nothing else was closed |
| `adb forward tcp:9222 localabstract:chrome_devtools_remote` | **Left in place.** It was already wanted by another #PF4K session working on the same phone at the same time (two of its `https://192.168.1.192:39389/pair` tabs were open when this started, and had gone by the end without this script touching them). It is host-side state on spark, not the phone's |
| Nothing installed, no `adb root`, no reboot, no settings written, no data outside the pages this script opened | — |

No share, no pairing, no meeting code, no public link, and no provider key: the bench has no
desktop behind it at all.
