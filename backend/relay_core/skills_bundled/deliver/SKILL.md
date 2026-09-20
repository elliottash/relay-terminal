---
name: deliver
description: Work a request through the Switchboard: check it is not done, claim its card, plan, execute, verify. "/deliver", "deliver this", "work this card".
short: 'Deliver a request through the Switchboard: claim a card, plan, execute, verify.'
---

# Deliver a request through the Switchboard

You are a terminal-pane agent in a project that has a Switchboard, so you have the `board_*` tools
and the policy in your system prompt. This is the procedure that policy rule 1 points at: it turns
a request into a card you hold, work that is visible while it runs, and a card in
`needs-verification` with the evidence. `/deliver <request>` runs it even when the automatic rule
would have skipped the card.

Do not run it for a question or a one-command ask ("what does X do?", "run the tests"). A turn
that already carries a card block is already yours to work: start at step 3.

## 0. Which tier?

Decide before anything else, and say the tier in one word in your reply when it is not obvious.

| Tier | It is | What happens |
|---|---|---|
| **Small** | finished in this turn, verified by you (built, a test run, or seen working), no design choice, no question for the user | no card; the commit is the record; steps 1 and 5 still apply (do not redo done work, `#ID` only if a card already exists) |
| **Medium** | more than one turn or more than two files, but no decision needed and a test proves it | steps 1–3, then work; at landing **you** move it to `done` (step 5) |
| **Large** | needs a plan, a decision from the user, or changes UI (needs eyes) | all six steps; lands in `needs-verification` for a verifier |

`/deliver <request>` makes it large whatever its size. "Just do it" or "no card" from the user
makes it small. When in doubt between small and medium, small: a card nobody needed is noise,
and the commit message still says what changed.

## 1. Is it already done?

Before anything else. Cards go stale; the code is the record.

- Read the code the request is about (`search_files`, `read_file`). Working code is the best evidence.
- `git log --grep "<keyword>"`, and `git log --oneline -20` for what landed today.
- `board_list` with a query, including the closed lanes (`status: done`, `dropped`) and the QA
  lanes — the work may be landed and waiting on a verifier.

If it is done: say so with the evidence (the file and function, or the commit), name the card if
there is one, and **stop**. Do not re-implement it unless the user says to do it anyway.

## 2. Which card?

`board_list` with a query from the request's own words, then `board_read` the candidates in full —
a title match is not a match.

- **Same ask** → that is the card. Claim it (step 3). If the request adds something, put the new
  words on the card (`board_update_card`) so the card still holds what was asked.
- **Related but different** → create the card (`board_create_card`, the user's words verbatim in
  `request`) and put the neighbour in `links.related`. Never claim a card that asks for something
  else.
- **Nothing** → create the card, then claim it.

You label the card; the user never has to, and you say nothing about labelling in your reply:
exactly one of `bug` (something built behaves wrongly) or `feature` (something new or changed is
asked for), from your reading of the request, plus the obvious area labels (`voice`, `remote`,
`switchboard`, …).

You may rewrite the user's own text — a request, a title, an intake note — when they ask or when it
is plainly wrong. The old and the new text are recorded in the card's thread, so the change is
visible and reversible; say in your reply that you did it.

## 3. Claim it

```
board_claim {id: "K7Q2", note: "what you are about to do, in a line"}
```

One call sets `assignee: agent`, moves the card to Executing, writes this pane's `session` onto it
and links the claim to this pane in the thread. The result hands back the whole card, so you do not
read it again. Claim **before** you change any code.

Refused with `board_claimed_elsewhere`? Another pane holds it. `board_read` the card and its thread
to see what that session is doing, post a `progress` comment saying what you are doing instead (or
that you are standing off), and ask the user before you repeat the call with `force: true`.

## 4. Plan, if it needs one

More than a few steps, or more than two files: write the plan onto the card first —
`board_update_card` with `replace_section: {heading: "Plan", text: …}`, in the shape a Plan turn
uses: **Goal**, **Findings** (exact paths), **Steps** (numbered, each one checkable change),
**Risks** (including anything the user has to decide), **Verify** (the tests, and how to see it
working). Anything smaller: go straight to work.

## 5. Execute

- `#ID` in every commit message, and each commit hash into the card's `links.commits`.
- A `progress` comment at a real milestone — the plan is settled, a hard part works, you are
  blocked — not a running commentary.
- A fault you find on the way that is not this card's: a new card in the bugs tab with the
  measured evidence, never a silent fix and never a detour.
- The tests that prove the card go in its `## Tests` section, one invocation per line
  (`` `ctest -R panelayout` ``, `` `tests/test_board.py::CardTests::test_roundtrip` ``,
  `manual: <evidence path>`). Run `tests_check` on the card before you move it to
  `needs-verification`, and fix what it names.
- When it lands, by tier (policy rule 5), in the same commit as the change:
  - **Medium:** `board_move_card` to `done` with a one-line reason naming the test that proves it,
    the commits in `links.commits`, and the test's path or command as the evidence line. No QA
    checklist, no verifier: the user can reopen it.
  - **Large:** `board_move_card` to `needs-verification` with the evidence path, and a
    `## QA checklist` section in the body; the verifier then moves it on to a QA lane, or back to
    an earlier stage. Closing a card that sits in a QA lane needs the verifier's verdict in the
    body — any pane may flip it once that is there — and the card's `qa` block still names the
    best verifier.
  Relay stamps `implemented_by` with your provider/model and `verified_by` on whoever closes the
  card, so never type either — and never type `session`.
- A question for the user goes on the card as a `question` comment with your recommendation, and
  the card goes to `discussing` with `waiting_on: owner`.

## 6. Reply

Name the card as `#ID`, then one line per thing you did, and where the evidence is. The card holds
the detail; do not repeat it in the terminal.
