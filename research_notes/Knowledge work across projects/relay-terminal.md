# relay-terminal — dossier

## What it is and what we were trying to do

Relay is "a cross-platform workspace for terminal and agentic coding" (`README.md`): a C++/Qt terminal with its own engine (`engine/`), a Python agent worker per pane (`backend/worker.py`), and a file-based Board in `issues/` that is both the project's tracker and a product feature. Domain: software development, for the owner first and a public beta second (`README.md` "Status: public beta", packaged releases in `.github/workflows/release.yml`). State: very active.

Size (verified): 2,595 commits, all by one git author, dated 2026-09-17 to 2026-09-23, i.e. seven days at 227–568 commits a day; 1,667 commit messages carry a `Co-Authored-By` line, so most commits were landed by AI sessions. 9,440 tracked files, of which 7,225 are under `docs/qa_evidence/` (438 MB); 221 in `src/`, 137 in `backend/`, 308 entries in `tests/` (103 C++ suites, 187 Python modules, 13 `.mjs` peers). `docs/ARCHITECTURE.md` alone is 3,763 lines. I infer from the seven-day window and `docs/COMMIT-MAP-2026-09-19.txt` (a history rewrite to drop a 710 MB `build1/` tree) that history was reset or rewritten and the project is older than the log shows.

## How the work was actually done

Evidence of process is unusually explicit because the process kept failing and each failure was written down:

- **Instruction files**: `WARP.md` (rules), `AGENTS.md` (imports `CLAUDE.md`), `CLAUDE.md` (310 lines, mostly incident narrative: "On 2026-09-18 alone, five commits silently undid other sessions' work"; "The hand-run recipe that replaced it then failed four more ways in one evening on 2026-09-19").
- **Board**: `issues/` with 615 cards (`issues/BOARD.md`), one thread per card under `issues/threads/`, policy generated from `backend/relay_core/board_policy.md` into `issues/POLICY.md`. Card evidence goes under `docs/qa_evidence/<date>-<slug>/`, 543 directories, e.g. `docs/qa_evidence/2026-09-22-1MGS-relay-images/` holds `drive.sh`, a stub provider, and screenshots.
- **AI tools**: several Claude Code sessions in parallel in one checkout, plus Codex and Kimi/GLM as reviewers (`#4QM4` "Measure the author x reviewer matrix"). Session-level state lives in `.relay/` and in `scripts/land.py` snapshots under `/tmp/claude-1000/land/<me>/`, not in git.
- **Scripted vs manual**: committing, building, board indexing and policy generation are programs (`scripts/land.py`, `scripts/relay-build`, `scripts/relay-board.py`). The choice of what to do next is manual: cards move by hand and by agents, `issues/bug_intake.txt` and `feature_intake.txt` are "the owner's inboxes".
- **State between sessions** is the card thread plus `git log --oneline -15` and `git status` at session start (`CLAUDE.md`). `tmp/board-columns-patch.py` shows a session writing a patch script because "edit_file cannot open this file (it is over the 128 KiB preview limit)", a tool limit worked around by hand.

## Cases and servers

Recurring cases and what serves them:

- *Land a commit without reverting another session* — served by a program, `scripts/land.py`, built after two days of hand-run recipes that each looked like success. This is the clearest card-built-a-server in the repo.
- *Build without stale objects* — program, `scripts/relay-build` (flock, timestamp reset), built after "a green build of code nobody had written".
- *Deliver a request through the Board* — a skill, `backend/relay_core/skills_bundled/deliver/SKILL.md`, embedded verbatim in `issues/POLICY.md`, followed by AI sessions.
- *Verify a landed card* — mixed: a human or a non-Claude model runs the card's QA checklist; `docs/VALIDATION.md` describes the lane.
- *Evaluate prompt rules* — program plus model, `scripts/eval-requests.py`, "NOT run by ./scripts/test.sh or CI".
- *Regenerate docs from truth* — `issues/BOARD.md` and `issues/POLICY.md` are generated; `docs/ARCHITECTURE.md` and `docs/VALIDATION.md` are hand-kept and already say so ("Current as of 2026-09-19", "Last updated 2026-09-17").

Redone ad hoc: `CLAUDE.md` records the model dropdown and the unknown `/command` each "implemented independently on the same day"; `WARP.md` "Words" records that "card had grown eight meanings" before the vocabulary was pinned.

## Verification

Modes present: script (56 ctest targets, 3,350 unittest cases per `docs/VALIDATION.md`; CI in `.github/workflows/ci.yml`, `macos.yml`, `windows.yml`); AI-on-text and AI-on-visual (evidence READMEs describe screenshots taken under Xvfb, e.g. `docs/qa_evidence/2026-09-22-1MGS-relay-images/README.md`); a build gate in `land.py` that compiles the exact tree to be landed.

What the counts say (verified from `issues/BOARD.md`): 243 cards `needs-qa-llm`, 231 `needs-verification`, 45 `done`. 342 card files have a `## QA checklist`, 21 have `## Verdict`, 1 has `## Human QA`. `docs/VALIDATION.md` still says "No issue has passed this lane yet" and points at a `needs_qa_llm/` folder that no longer exists. So implementer evidence is abundant and independent verdicts are rare: roughly 3 in 4 cards sit in a lane that waits for a verifier. The cost when this was skipped is on record: `CLAUDE.md` "It had skipped `--dry-run` the second time, and there was then no way to say 'take back the hunks'"; `main` "stopped compiling".

## Strengths

- Every incident became a rule with a date and a mechanism, and most rules became tools that refuse the bad action (`land.py` "What it refuses to do, and why each refusal is an incident").
- The board is git-native and searchable, and the policy is one text for humans and agents (`issues/POLICY.md`).
- Evidence is a first-class artifact with a fixed home (`docs/qa_evidence/`), and the test inventory is discovered rather than hand-kept (`relay_core.test_probe`, `docs/VALIDATION.md`).
- Throughput is extraordinary for one owner: several hundred landed commits a day.

## Weaknesses and limitations of the ad-hoc workflow

- **Verification backlog**: 474 cards waiting versus 45 done; the QA lane has one recorded human verdict. Evidence proves the implementer saw it work, not that anyone else did.
- **Status vocabulary drift**: `issues/board.yaml` names the column `needs-qa`, 242 cards carry `status: needs-qa-llm`.
- **Stale hand-kept docs**: `docs/VALIDATION.md` header dated 2026-09-17 with sections edited through 2026-09-21; `docs/ARCHITECTURE.md` pinned to a commit four days and about a thousand commits old.
- **Five build trees** (`build/` 2.7 GB, `build-clean/`, `build-engine/`, `build-fast/`, `build1/` 724 MB), one of which had to be rewritten out of history (`docs/COMMIT-MAP-2026-09-19.txt`).
- **Strays at the root**: `10.tsv` (an OCR word table), `tmp/` with 48 patch scripts and dry-run logs, 116 entries in `git status` at the time of reading, including 20 untracked source files another session is mid-way through.
- **Session state outside git**: land.py snapshots under `/tmp`, contests between sessions resolved by digests nobody can review after the fact.
- **Duplicate implementation** of the same feature by parallel sessions, admitted in `CLAUDE.md`.
- I found no credentials in tracked files (two `sk-` pattern hits are in test fixtures, `tests/test_memory_import.py`, `tests/test_logs.py`; inferred to be fake).

## What Relay would have to support here

- A **verifier lane with a real owner**: cards in `needs-verification` need a scheduled non-author session (or human) that writes `## Verdict`; today 474 cards wait and the doc that describes the lane is stale.
- **Evidence as a typed artifact**: screenshot plus script plus stub provider (`docs/qa_evidence/…/drive.sh`) is the recurring shape; the pane should know how to re-run it and diff the image, not just store it.
- **Servers with their incident ledger attached**: `land.py` and `relay-build` are programs whose *why* lives in `CLAUDE.md`; the card that built a server should stay linked to the server so the refusal messages and the rules do not drift apart.
- **Generated-versus-hand-kept docs** marked as such, with a staleness check for the hand-kept ones (`ARCHITECTURE.md`, `VALIDATION.md`).
- **Multi-session concurrency as a first-class object**: who claims which paths, contested hunks, and stale snapshots are currently visible only through `land.py who`.
- **Vocabulary as a server**: `WARP.md` "Words" and `board.yaml` statuses need one source so `needs-qa` and `needs-qa-llm` cannot coexist.
