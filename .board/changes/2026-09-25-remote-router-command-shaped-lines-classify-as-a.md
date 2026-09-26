---
id: VJX7
type: work
status: needs-verification
labels: [bug, router, remote]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: a5ccfd18-92ab-4215-b969-c40fdfb108b3
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzw
created: '2026-09-25'
links: {commits: [b0e017e4e676, de31a0136f2a, 2f3834c1e73f], evidence: [docs/qa_evidence/2026-09-25-vjx7-sentence-rule-remote/], github: null, plans: [], related: []}
---
# Remote router: command-shaped lines classify as agent (test_command_shaped_lines_are_typed_on_the_host fails)

## Issue
tests/test_ssh_remote.py::RemoteRouterTests::test_command_shaped_lines_are_typed_on_the_host fails on current main: remote-mode lines "cp a b" and "kubectl get pods in the namespace" now classify as `agent` instead of `shell`. Reproduced independently of #EB4A's in-flight fix; the last router change was c4955c52 (#1ZNS, 2026-09-23, "catch sentences that start with a non-English command name"), which plausibly started reading "kubectl get pods in the namespace" as a sentence. Needs a product call: does the #1ZNS sentence rule outrank typing command-shaped lines on the remote host, or should remote mode keep the stricter command reading?

## Done means
`PYTHONPATH=backend python3 -m unittest tests.test_ssh_remote tests.test_router` passes on main with the sentence rule applied remotely: "cp a b" and "kubectl get pods in the namespace" now route to `agent` with `needs_assist` at a remote prompt — they ask first, nothing is silently sent — pinned by `test_sentence_shaped_lines_ask_instead_of_running`; "kubectl get pods" and "git status" still route to `shell`; #1ZNS's remote sentence ("claude has usage rests now, so we should track them") routes to `agent`; `NonEnglishCommandNameTests` still passes. Failure looks like: an unpunctuated command-shaped remote line running on the host shell and dying with "command not found", or a sentence being executed there instead of asked about.

## Plan
**Goal** — Restore the remote router's pre-#1ZNS rule: at an ssh prompt, a command-shaped line is typed on the host unless its shape is unmistakably a sentence, so "cp a b" and "kubectl get pods in the namespace" route to `shell` again.

**Findings** (all in `backend/relay_core/router.py`):
- `_classify_remote` (line ~1092) calls `assist_signals(trimmed, local_files=False)` and, when the score reaches `ASSIST_THRESHOLD` (2), returns `Decision("agent", needs_assist=True)` for a runnable line (line ~1115).
- Since c4955c52 (#1ZNS), `assist_signals` no longer zeroes the score when the first word is not an English command: `_clears_non_english_bar` (line ~641) passes at score ≥ `NON_ENGLISH_THRESHOLD` (4) given one weight-2 signal (article, pronoun, question word, or punctuation).
- "cp a b": article "a" (+2) and lead-in "a" (+2) = 4 → clears the bar → agent. Before #1ZNS a non-English first word always scored 0, so it was shell.
- "kubectl get pods in the namespace": "in" (+1), "the" (+2), six plain words with no pathlike arg (+1) = 4 → clears via "the" → agent. ("kubectl get pods" stays shell: only 3 words, no signals.)
- #1ZNS's motivating case — "claude has usage rests now, so we should …" ran in the shell — carries sentence punctuation (the "punctuation" reason), which no invocation has.
- Local path (`classify`, line ~1210) applies the same assist gate to runnable lines; it is #1ZNS's intended behaviour and out of scope here.

**Steps**
1. In the remote assist path, tighten the #1ZNS gate for non-English first words: at a remote prompt the assist fires only on a signal no invocation has — sentence punctuation or a trailing "?" (the `"punctuation"` / `"trailing ?"` reasons in `assist_signals`) — never on articles, pronouns, lead-ins or word count alone. Concretely, either pass a `remote=True` flag from `_classify_remote` into `assist_signals`/`_clears_non_english_bar`, or have `_classify_remote` require `{"punctuation", "trailing ?"} & set(signals)` before applying the non-English assist. Whichever shape is chosen, the English-command assist ("find the big logs" → `needs_assist`) and the `_remote_prose` fallback are untouched.
2. With that filter, "cp a b" and "kubectl get pods in the namespace" score below the assist bar and fall through to `shell` (their `_remote_prose` result is already ""); a remote "claude has usage rests now, so we should …" still assists via its comma.
3. Do not touch the local `classify` assist path or `NonEnglishCommandNameTests` expectations.
4. Land via `python3 scripts/land.py begin <me> backend/relay_core/router.py` / `commit`; the message names `#VJX7`.

**Risks**
- **Product call for the owner** (the one the issue names): should remote mode keep the stricter command reading? This plan says yes — remotely nothing can check whether "a", "b" or "pods" name files on the host, and a command silently sent to the agent is a no-op the user has to notice — with punctuation/question evidence preserved so real sentences still go to the agent. If the owner wants #1ZNS to outrank, the fix is instead to update the remote test's expectations; flag that and stop.
- Over-tightening sends genuine unpunctuated remote sentences ("kubectl get pods in the namespace please") to the host shell, where they fail with "command not found" — accepted as the lesser evil per the product call, and the note under the line explains why.

**Verify**
- `python3 -m unittest tests.test_ssh_remote.RemoteRouterTests` — all four tests pass.
- `python3 -m unittest tests.test_router` — unchanged, proves #1ZNS local behaviour and the other router rules hold.
- Manual check: `classify("kubectl get pods in the namespace", remote_mode=True)` → route `shell`; `classify("claude has usage rests now, so we should track them", remote_mode=True)` → route `agent` with `needs_assist`.

## Tests
- `PYTHONPATH=backend python3 -m unittest tests.test_ssh_remote tests.test_router` — 97 passed at 2f3834c1e73f.
- New `test_sentence_shaped_lines_are_typed...` companion: `test_sentence_shaped_lines_ask_instead_of_running` pins "cp a b" and "kubectl get pods in the namespace" → `agent` + `needs_assist` remotely (the owner-decision rule); `test_command_shaped_lines_are_typed_on_the_host` keeps the unambiguous commands.
- `NonEnglishCommandNameTests` unchanged and passing; local `classify` untouched.
- Evidence: `docs/qa_evidence/2026-09-25-vjx7-sentence-rule-remote/` (`tests.txt`, `remote-classify.txt`, README). Supersedes `docs/qa_evidence/2026-09-25-remote-router-command-lines/` from the pre-decision implementation.
