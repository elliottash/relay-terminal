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

## What this does not cover

- The pane drawing the card. The approval is unreachable from the GUI today, because the picker
  always sends `permissions: "bypass"`; if a permissions control is ever added, the card's four
  options want an eye on them in a real pane.
- `Allow for session`, `Deny` and `Deny and stop` against a live guest. Their wire words are
  covered by unit tests against the recorded protocol; only `Allow` was spent here, at one turn
  per guest.
- `permissions: "deny"`, which refuses for the user and is unit-tested only.
