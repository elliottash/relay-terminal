# Expected — Q8TM Try it (sealed until the person answers)

## BEFORE (main before this card)
- Harness starts **fresh**: `resume=None`.
- Pane status: "Handed the conversation so far to Codex."
- The prompt repeats the **whole** conversation, including the turn the guest itself already ran:
  `Start on codex.` **and** its own `ok` reply, then the GLM turn, then the question.

## AFTER (this card, commit 11847c)
- Harness starts **resumed**: `resume='codex-1'` — the pane's own earlier Codex session.
- Pane status: "Resumed Codex session codex-1; sent the 2 messages that ran while it was away."
- The prompt carries **only what ran while the pane was away**:
  - present: `Remember the codeword PELICAN-42 for later.` and GLM's answer,
    inside a `[Relay context]` block that opens "You ran this conversation earlier in Relay as
    Codex session codex-1; that session has been resumed…" and ends with the actual question;
  - absent: `Start on codex.` and the guest's own `ok` — the resumed session already holds them,
    and re-sending them is exactly the wasted context this card removes.

A good answer says the AFTER catch-up is complete and trustworthy: nothing the guest needs is
missing, and the note makes clear why its own earlier turn is not repeated.
