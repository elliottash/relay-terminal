# The approval path, against the real guests (GT7X, protocol 29.1/29.3)

Implementer evidence, 2026-09-19. Driver: `approval-drive.py` (run log `approval-drive-run.log`).

Everything else in Tier A runs under `permissions: "bypass"` — the owner's rule, and the only thing
the model picker sends — so the approval path had shipped untried against a live guest. This is the
one run that exercises it. For each guest the driver starts the real harness with
`permissions="ask"` in a scratch directory, asks it to write one small file, and then does what the
pane would do: it takes the `approval` event the adapter raised, builds the section 27 card the
provider would draw, answers with **Allow**, and checks that the guest went on and the file is
there.

    PYTHONPATH=backend python3 approval-drive.py [claude|codex|both]

It does not stand up a worker or a pane: the `question` / `question_answer` round trip is
unit-tested, and this is about the live wire. It uses the real HOME, because both CLIs need their
login, and writes only under a temp directory. One turn per guest.

## What happened

**Claude Code 2.1.278** — one turn, 5 s. It raised the approval as its `can_use_tool` control
request before the Write, the adapter reported it as `patch` with the detail
`Write: <path>/approval-probe.txt`, the card came out as **Apply edit** with the four choices, the
Allow reached claude, and the file was written. Events in order: `started`, three `delta`,
`tool_started`, `approval`, `tool_result`, six more `delta`, `usage`, `done`.

**Codex 0.155.1** — one turn, 13 s. Its sandbox refused the patch and it asked to retry outside it,
which arrives as `item/fileChange/requestApproval`; the card came out as **Apply edit** with the
same four choices, the Allow reached codex, and the file was written. Events: `started`, fourteen
`delta`, `tool_started`, `approval`, `tool_result`, twelve more `delta`, `usage`.

Both: `stop_reason=end`, the probe file present, the four options exactly
`Allow / Allow for session / Deny / Deny and stop`.

## What the run found

The first Codex run's card read **"Apply edit · command failed; retry without sandbox?"** — codex's
own reason, and not one word about which file was about to be written outside the sandbox. That is
the one thing the person answering needs. The cause is a protocol change: v1's `applyPatchApproval`
carried a `changes` map, but v2's `item/fileChange/requestApproval` carries only `itemId`, `reason`
and `grantRoot` (confirmed against `codex app-server generate-json-schema` for 0.155.1), so a card
built from the request alone can never name the file.

Fixed by reading the paths from the item the adapter is already following for the diff. The second
Codex run's card reads **"edit …/approval-probe.txt (command failed; retry without sandbox?)"**:
what, and why, in that order. Covered by `ApprovalDetail` in `tests/test_guest_harness_codex.py`.

## All four choices, both guests (2026-09-19)

The first pass covered only `Allow`. The rest were run afterwards, one turn each:

| choice | claude | codex | what was checked |
|---|---|---|---|
| Allow | pass | pass | the file is written, the turn ends normally |
| Allow for session | pass | pass | the word is accepted and the work completes (see below) |
| Deny | pass | pass | nothing is written, and the guest carries on and says so — claude: "The write was declined, so I did not create the file"; codex: "Couldn't create `approval-probe.txt` because the file-write request was denied" |
| Deny and stop | pass | pass | nothing is written, `stop_reason=interrupted`, and it does not ask again |

**"Allow for session" cannot be proved by a driver, and the run is honest about it.** Codex's own
schema defines `acceptForSession` as "future changes to **the same files** should run without
prompting" for a file change, and "future prompts in the same session-scoped approval cache" for a
command; claude's session rule is written as narrowly as the thing that was asked about. Whether a
later call is skipped is therefore the guest's judgement of sameness, and the guest picks its own
tools. Four runs of that one choice bore this out: it edited a different file, then reached for a
shell command, then honoured the scope and asked once, then skipped its own second step. The check
is what the harness guarantees — the decision is accepted, the turn completes — and the ask count
and the file's final contents are logged as observations.

## Reachable from Options

Until this run the path could not be reached from the interface at all: the picker always sent
`permissions: "bypass"`. Options › Claude Code and Codex now has a third row per guest, **"When it
wants to use a tool"** — *Just run it* (the default, and the owner's rule), *Ask me*, *Refuse it* —
stored as `guests/<guest>/permissions` and read by the same code that carries the model and the
effort. It reaches the picker's route, where Relay is the guest's editor and can draw the question.
A guest running as a program in the terminal asks there, in its own words, and the row says so.

## What this still does not cover

- The pane drawing the card. The four options are a section 27 card like any other, and the shapes
  are unit-tested, but no screenshot of one has been taken in a real pane.
- `permissions: "deny"`, which refuses for the user without asking. Unit-tested only.
