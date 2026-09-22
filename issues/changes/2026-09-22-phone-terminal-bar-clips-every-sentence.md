---
id: TBR2
type: work
status: needs-verification
labels: [bug, remote]
assignee: claude-code
implemented_by: anthropic/claude-opus-5 via claude-code
rank: ztbr2
created: '2026-09-22'
source: Measured by Claude Code driving the phone app at 390x844, 2026-09-22
links: {plans: [], commits: [f919f14b, ad25d4b0, 5f7432db], evidence: [docs/qa_evidence/2026-09-22-phone-ux-drive/, docs/qa_evidence/2026-09-22-streamD-shell/], related: [PH0N], github: null}
---
# The phone terminal bar clips every sentence it says, and an `agent` device is told it may only watch

## Issue
Two faults in the terminal bar, both visible in
`docs/qa_evidence/2026-09-22-phone-ux-drive/C-full-after-send.png` (findings 7 and 8).

**1. The sentence is cut.** `#term-note` shares its row with the "Watching" chip, the A−/A+ buttons
`f919f14b` added and "Take over" (`app/index.html:183-190`). Measured at 390×844:

| capability | what it says | `scrollWidth` | `clientWidth` |
|---|---|---|---|
| `full`  | "Agent running…" → **"Agent runnin…"** | 178 | 91 |
| `agent` | "This device is paired for viewing only." → **"This device is paired for viewi…"** | 226 | 195 |

That label is also where `sendPrompt`'s `fail()` puts the *failure* of a send
(`app/app.js:1803`), so a refused or not-permitted prompt is reported into 91 px and the reader
never learns why their line did not go.

**2. An `agent` device is told it may only watch.** `updateDriveUi()` sets
`allowed = capability === 'full'` and then, for anything below it, writes "This device is paired
for viewing only." (`app/app.js:1113`). But `canCompose` is `agent || full` on the very next
lines, so an `agent` device has a working composer on screen — in this drive it typed a prompt and
the desktop ran it — under a sentence saying it may only watch. `agent` needs its own words: it
may ask the agent, it may not take the keyboard.

## Done means
The terminal bar's sentence is readable on a 390 px phone — it is not cut mid-word, which
means measuring `scrollWidth` against `clientWidth` on the note and finding them equal. A failed
send says why somewhere the reader can read the whole of. A device paired `agent` is told what it
can do — ask the agent — and never that it may only watch while its composer is on screen. It fails
if `#term-note` still overflows at either capability.

## Execution Summary
`#term-note` shared its row with the "Watching" chip, A−/A+ and "Take over": 91 px at `full`,
195 px at `agent`, and every sentence cut mid-word — the reason a send did not go included, since
that label is where it is reported and nowhere else. The note is now last in the row and a full
line wide (`flex: 1 0 100%`, wrapping), so it sits under the controls with room for a second line,
and it is `hidden` while empty so the line only exists when it says something. Every writer in
`app/app.js` goes through one `termNote()` rather than eleven direct `textContent` writes.

And `updateDriveUi()` no longer tells an `agent` device "This device is paired for viewing only."
while `canCompose` is `agent || full` two lines later and its composer is on screen: `agent` is
told "You can ask the agent here. Taking the keyboard needs full access." Only `view` is told it is
watching.

## Tests
`RELAY_KEYRING=off python3 -m unittest tests.test_remote_browser` — 24 tests, OK. Real headless
Chrome at 390×844, paired over a real Noise session against a real host and rendezvous. Re-run by
the orchestrating session after the landing.

- `tests/test_remote_browser.py::TerminalBarTests::test_an_agent_device_is_told_it_may_ask_and_reads_the_whole_sentence`
  — the sentence, and that `#composer` really is on screen under it.
- `tests/test_remote_browser.py::TerminalBarTests::test_a_full_device_reads_the_whole_sentence_too`
  — "Agent running…", the 91 px case, plus the longest `modelLine` a model change can produce.
- `tests/test_remote_browser.py::TerminalBarTests::test_every_sentence_the_bar_can_say_fits_on_a_390px_screen`
  — every fixed sentence swept out of `app.js` (`termNote('…')`, `note('…')`, `voiceNote('…')`),
  plus the four the client builds, each measured in the real bar.

Each asserts `scrollWidth === clientWidth`, `scrollHeight === clientHeight`, that the note is below
A+ rather than beside it, and that it stays inside 390 px. With the old CSS put back all three fail
at the drive's own number: `AssertionError: 99 != 91 : 'Agent running…' is cut off (99 px of
sentence in 91 px)`.

- `manual: docs/qa_evidence/2026-09-22-streamD-shell/` — `term-bar-agent.png`,
  `term-bar-agent-model-change.png`, `term-bar-full-agent-running.png`,
  `term-bar-full-model-change.png`, `term-bar-longest-sentence.png`.
- `manual: docs/qa_evidence/2026-09-22-phone-ux-drive/` — the probe this card was filed from,
  re-run unchanged against the landed tree by the orchestrating session:
  `{"scrollW":366,"clientW":366,"clipped":false}` at **both** capabilities, where it was 178/91 at
  `full` and 226/195 at `agent`; and an `agent` device now reads "You can ask the agent here.
  Taking the keyboard needs full access." with its composer on screen.
