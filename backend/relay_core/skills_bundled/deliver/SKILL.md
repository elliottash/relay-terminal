---
name: deliver
description: Work a request through the Board: check it is not done, claim its card, plan, execute, verify. "/deliver", "deliver this", "work this card".
short: 'Deliver a request through the Board: claim a card, plan, execute, verify.'
profile: |
  artifact: code
  primary: script
  human: none
  effort: medium
  stakes: rework
  blast: capability
  regularity: routine
  executable: yes
  rot: low
  confidential: no
  money: no
---

# Deliver a request through the Board

You are a terminal-pane agent in a project that has a board, so you have the `board_*` tools
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

**Whether or not it needs a plan, write `## Done means` before you write code** — two to five
lines: the outcome someone could check, and how failure would be recognised. It is what a separate
verifying session checks the work against, so it has to be chosen before the implementation can
shape it. Run on a card without it says so on the board and goes on.

**Beside it, propose the `verify` block** — `board_update_card {fields: {verify: {…}}}` — by
walking the QA ladder in order and stopping at the first oracle that can gate: `script` (a test,
a diff, a hash), `probe` (the live artifact answers), `metric` (a calibrated threshold),
`ai-text` (a model reads the output), `ai-visual` (a model reads a picture), `level` (a rubric),
`pairwise` (old against new), `person` (someone looks, listens or plays), `world` (an experiment
or a client). That rung is `primary`; the others that still help go in `also`. Say what the
`artifact` is (code, text, number, visual, audio, system, physical, decision); whether a person
must look — `human: none|optional|required`, with `criteria` (one line: what they check and what
passing looks like) whenever it is not `none`; `sign_off` when a rule demands one (money,
publish, send, delete, legal, clinical); and `effort: low|medium|high` from stakes × novelty
(`stakes`, `blast` when they matter). If nobody here can run the check at all, say so honestly:
`deferred: "until <what has to happen, and who owns it>"`. Example, a worker change proved by a
unit test and read once by a person: `{artifact: code, primary: script, also: [ai-text], human:
optional, criteria: "the refusal reads as one sentence", effort: low}`. The claim result reminds
you when the card has none; the user corrects the proposal, and their correction stands.

## 5. Run

- **One section per stage** (`relay_core.board.CARD_SECTIONS`): the body records what each stage
  produced. Planning leaves `## Done means` and `## Plan`; the work keeps `## Tasks` live; landing
  writes `## Execution Summary` (what was built, links to the outputs) beside `## Tests`; the
  verifying session writes `## QA checklist` and `## Verdict`. Invent no other headings — the list
  is complete, and `relay-board.py check` warns on anything outside it.
- `#ID` in every commit message, and each commit hash into the card's `links.commits`.
- A `progress` comment at a real milestone — the plan is settled, a hard part works, you are
  blocked — not a running commentary.
- A fault you find on the way that is not this card's: a new card in the bugs tab with the
  measured evidence, never a silent fix and never a detour.
- The tests that prove the card go in its `## Tests` section, one invocation per line
  (`` `ctest -R panelayout` ``, `` `tests/test_board.py::CardTests::test_roundtrip` ``,
  `manual: <evidence path>`). Run `tests_check` on the card before you move it to
  `needs-verification`, and fix what it names.
- **A signal your own run opened is yours.** `tests_run`'s result lists them as "signals this run
  opened" (#AQ6X): claim one with `board_signals {action: "claim", key: …}`, fix it in this turn
  before you report, and run the test again so it resolves — two consecutive passes of that key,
  and nothing else, close it. If you cannot fix it, `board_signals {action: "release", reason:
  "gave-up"}`, which files it as a bug card. Leave it unclaimed and Relay starts its own agent
  thread on it after the next fold.
- When it lands, by tier (policy rule 5), in the same commit as the change:
  - **Medium:** `board_move_card` to `done` with a one-line reason naming the test that proves it,
    the commits in `links.commits`, and the test's path or command as the evidence line. No QA
    checklist, no verifier: the user can reopen it.
  - **Large:** `board_move_card` to `needs-verification` with the evidence path, and
    `## Execution Summary` and `## Tests` in the body. **Write no `## QA checklist`**: that
    section is the verifying session's record of what it checked against your `## Done means`,
    and a checklist written by the pane that did the work is a list of criteria the work already
    meets. A separate session verifies — recommended on a different model family, which is a
    recommendation and not a rule — by running the tests *and* staging and playing the situation
    the card describes, into `docs/qa_evidence/<date>-verify-<ID>/`; it then moves the card on to
    a QA lane or back a stage. Closing a
    card that sits in a QA lane needs the verifier's verdict in the body — any pane may flip it
    once that is there — and the card's `qa` block still names the best verifier. A card whose
    `## Human QA` holds a question with no `Answer:` under it is not yours to close at all: that
    judgement is the user's.
  Relay stamps `implemented_by` with your provider/model and `verified_by` on whoever closes the
  card, so never type either — and never type `session`.
- **Verified means the whole `verify` block is met** (`relay_core.board.verified`): the primary
  evidence is on the card — a `## Verdict`, or a passing `### Check` under `## Tests` — *and* the
  person's `Answer:` sits under a `## Human QA` question when `human: required` (offer
  `needs-qa-human`, never `done`), *and* a line beginning `Receipt:` in `## Verdict` or
  `## Execution Summary` records the sign-off when one is required, *and* `deferred` is not set.
  `board_move_card` refuses `done` (and, while deferred, the QA lanes) until all of that holds;
  a deferred card shows "unverified until …" on its row, and clearing `deferred` through
  `fields.verify` — recorded in the thread under your name — is the only way on.
- A question for the user goes on the card as a `question` comment with your recommendation, and
  the card goes to `discussing` with `waiting_on: owner`.
- **Show it working, in a picture.** Relay draws images inline (#1MGS), so a change a person can
  see is proved by one: a screenshot of the real thing, taken after the change, from an isolated
  profile. Embed it as Markdown, `![what it shows](<evidence dir>/01-….png)`, in
  `## Execution Summary` and in your reply, one image per claim and the caption saying what the
  image proves. A failure is shown the same way. A test result proves logic; only the capture
  proves what the person will see, and a claim with no picture behind it is a claim to check.

## 6. Reply

Name the card as `#ID`, then one line per thing you did, and where the evidence is. The card holds
the detail; do not repeat it in the terminal.
When the change is something a person can see, end with its screenshot as a Markdown image, so
the proof is in front of them without opening a file.
