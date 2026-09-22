---
id: KBD7
type: work
status: inbox
labels: [bug, remote]
assignee: null
rank: zkbd7
created: '2026-09-22'
source: 'Measured by Claude Code driving the phone app at 390x844, 2026-09-22'
links: {plans: [], commits: [f919f14b], evidence: ['docs/qa_evidence/2026-09-22-phone-ux-drive/'], related: [PH0N, 7JD1], github: null}
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
