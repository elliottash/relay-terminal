---
id: KBD7
type: work
status: needs-verification
labels: [bug, remote]
assignee: claude-code
implemented_by: anthropic/claude-opus-5 via claude-code
rank: zkbd7
created: '2026-09-22'
source: Measured by Claude Code driving the phone app at 390x844, 2026-09-22
links: {plans: [], commits: [f919f14b, d6116335, 7d313776], evidence: [docs/qa_evidence/2026-09-22-phone-ux-drive/, docs/qa_evidence/2026-09-22-streamA-pane/], related: [PH0N, 7JD1], github: null}
---
# The phone's Send button puts the keyboard straight back up, and the blur it undoes breaks Enter

## Issue
`f919f14b` blurs the prompt box on send so the on-screen keyboard comes down. In the pane view —
the surface a real desktop mounts — the Send **button** undoes it on the next statement
(`app/pane.js:1114-1118`):

```js
on(sendButton, 'click', () => {
  const text = box.value;
  if (text.trim()) compose(text, busy() ? 'queue' : 'now');
  box.focus();                    // undoes the box.blur() compose() just did
});
```

Measured at 390×844 (`docs/qa_evidence/2026-09-22-phone-ux-drive/`, finding 3): after tapping Send
`document.activeElement` is still `rp-input`; after Enter it is the body. The tap is the gesture a
phone uses, so the feature does not hold where it was asked for. The older client composer
(`app/app.js:1807`) does blur correctly — verified in the same drive.

Two more faults on the same path, found reading it:

**The unconditional blur takes three keyboard paths with it.** `enter()` is reachable only from the
box's `keydown` listener (`app/pane.js:1126`; the `composer` `submit` listener never fires — a
textarea does no implicit submission and there is no submit button). After `compose()` blurs:
the three-step Enter escalation that turns a queued prompt into a steer and then sends it now
(`app/pane.js:718-747`, a 15-second window) is unreachable; `queue_resume` on an empty box — the
phone's only way to lift a Stop-paused queue (#7JD1) — is unreachable; and ArrowUp row selection
(`app/pane.js:1136`) is unreachable. The comment at `:712` ("a physical keyboard's blur is
invisible, so this is unconditional") is the wrong premise: the blur is not invisible to those
paths, it removes the focus every one of them needs. The view already knows the device
(`data-device="phone"`), so blurring only for touch would keep both.

**Answering the agent's ask leaves the answer in the box.** The ask branch of `compose()`
(`app/pane.js:692-702`) does `answerAsk(text, when); editRow = ''; box.blur(); return;` — it never
clears `box.value`, calls `fitBox()` or `renderSendState()`, which the fall-through path does. So
after answering question 1 of 3 the typed `yes` is still in the box, blurred, with Send enabled: a
tap sends it as the answer to question 2, and if the ask has closed it goes to the agent as a
fresh prompt. Pre-existing, but the new blur hides the evidence.

## Done means
On a touch device, sending from the pane's prompt takes the on-screen keyboard down and
leaves it down — by the Send button as much as by the return key. On a device with a physical
keyboard the box keeps the focus, so Enter's three-step escalation, `queue_resume` on an empty box
and ArrowUp row selection all still work. Answering the agent's ask empties the box the way an
ordinary send does. It fails if a tap on Send leaves the box focused, or if a second Enter after a
send does nothing on a laptop.

## Execution Summary
One `keyboardDown()` now blurs the prompt only when `root.dataset.input === 'touch'`, and the Send
button asks for the focus back only when it is not. So the tap a phone actually uses takes the
on-screen keyboard down and leaves it down, while a laptop keeps the focus and with it Enter's
three-step escalation, `queue_resume` on an empty box (#7JD1) and ArrowUp row selection — every one
of which is reachable only from the box's own keydown listener, and so would have died with an
unconditional blur. The comment claiming a physical keyboard's blur is invisible is replaced by
what is actually true.

Third fault on the same path: the ask branch of `compose()` returned without clearing `box.value`,
`fitBox()` or `renderSendState()`, so the typed answer to question 1 stayed in the box with Send
enabled and the next tap sent it as the answer to question 2. It now empties the box the way the
ordinary path does.

The decision behind this — touch-only rather than unconditional — is recorded in
`docs/qa_evidence/2026-09-22-phone-ux-drive/WORKSTREAM.md` and in this card's `## Decisions`.

## Decisions
The blur is **touch-only**, decided by the orchestrating session rather than asked, because the
work would otherwise have stalled on it and the alternative is knowably worse. `f919f14b` blurred
unconditionally on the premise that "a physical keyboard's blur is invisible". It is not: `enter()`
is reachable only from the prompt's own `keydown`, so a blur takes Enter's three-step steer
escalation, `queue_resume` on an empty box and ArrowUp row selection with it. Blurring only where
there is an on-screen keyboard to dismiss keeps both — the phone gets its screen back, the laptop
keeps its keys — and the view already knows which it is drawing for. Reversible in one place
(`keyboardDown()`) if the owner would rather it always blurred.

## Tests
`RELAY_KEYRING=off python3 -m unittest tests.test_pane_view` — 36 tests, OK, 16 s. Re-run by the
orchestrating session after the landing.

- `tests/test_pane_view.py::PaneViewTests::test_send_takes_the_keyboard_down_on_touch_and_leaves_the_focus_on_a_keyboard`
- `tests/test_pane_view.py::PaneViewTests::test_answering_the_agents_ask_empties_the_box`
- `manual: docs/qa_evidence/2026-09-22-streamA-pane/` — `KBD7-send-touch.png`,
  `KBD7-send-mouse.png`; the log records `focus=demo-bare box=""` for touch and
  `focus=rp-input box=""` for mouse.
- `manual: docs/qa_evidence/2026-09-22-phone-ux-drive/` — the probe this card was filed from,
  re-run unchanged against the landed tree on a touch mount: **focus after Send is now the body**
  where it was `rp-input`, the box is empty, and the `compose` still went. Enter behaves the same
  as before.
