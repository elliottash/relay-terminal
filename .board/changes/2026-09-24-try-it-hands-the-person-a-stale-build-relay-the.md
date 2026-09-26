---
id: J6MF
type: work
status: needs-verification
labels: [bug, build, qa, try-it]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: 6b643199-f90f-42f0-9d2c-29a59f627ad0
rank: zzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [probe], human: none, criteria: relay-qa-run and the Try-it stager both run the check and fail on a stale binary; a test builds a fake stale binary and expects the failure, sign_off: none, effort: low, blast: capability}
links: {related: [SZHQ, XQ8F], commits: [6b8f4feadf3a], evidence: [docs/qa_evidence/2026-09-24-j6mf/]}
---
# Try-it hands the person a stale build/relay: the binary predates the landed change, so a working fix looks missing

## Issue
can you look at the time spent and how many tokens were spent and on what tasks, to see if there are any lessons for the app. because what we do here is going to be happening on all the users' machines by default.

## ## Issue
In session 3f4a20ad (the #234Z Alt+Esc work) the fix landed on `main`, the session ran `./build/relay` for the Try-it hand-off, and the binary predated the change: it had been linked before the edit was compiled. The feature looked missing, so the session spent its next phase debugging code that was never in the binary it ran.

`scripts/relay-build` already exists for exactly this: it holds one build at a time and back-dates every object it produces so a mid-compile header edit is always rebuilt, and `--check "literal"` fails unless the binary holds a marker string. Nothing requires the hand-off paths to use it:

- the Try-it staging and `relay-qa-run` run `./build/relay` directly, so a stale binary is presented to a person or to QA as the change itself;
- nothing compares the binary's build id (or a marker) against the commit being verified, so staleness is invisible until a human says "it doesn't work".

**How to address.** Make the hand-off prove what it runs: (1) `relay-qa-run` and the Try-it stager build through `scripts/relay-build` (never `cmake --build` or a bare binary), (2) before handing over, run `relay-build --check` with a marker from the landed change (for #234Z, a string the new code adds) and fail loudly with "binary predates the change" when it misses, (3) `scripts/build-id.py` already exists: compare the binary's id with the commit being verified and print both on mismatch. A stale binary must never reach a person's hands silently.

## ## Done means
- A Try-it run and a `relay-qa-run` probe hand over only a binary proven to hold the landed change: both build through `scripts/relay-build` and pass `relay-build --check`/`--check-only` with a marker from the change before a person or QA sees anything.
- A stale binary produces the named failure `binary predates the change`, with the binary's build id and the commit, and the hand-off stops there — never a silently wrong result presented as the change.
- A test builds a stale binary on purpose (fake `relay.build-id`, marker absent) and asserts that named failure in both `relay-build` and `relay-qa-run`; `tests/test_tryit_protocol.py` asserts the Try-it prompt carries the rule.
- Recognising failure: a person says "the feature is missing" or a QA probe contradicts the landed change, and `build/relay.build-id` turns out to predate the commit — exactly what these checks must make impossible to miss.

## Plan
**Goal.** Neither hand-off path (Try-it staging, `relay-qa-run`) may present a `build/relay` that predates the landed change: both build through `scripts/relay-build`, prove the binary holds a marker from the change, and fail with the named error `binary predates the change` (build id + commit) instead of a silent wrong result.

**Findings (verified in the tree).**

- `scripts/relay-qa-run` (29 lines) sets a disposable XDG profile and runs `"$@"`. It never builds and never checks anything: a probe sees `build/relay` exactly as stale as it happens to be. There is no repo-root variable.
- `scripts/relay-build` already does one-build-at-a-time, back-dating, and `--check <literal>` (repeatable) / `--check-binary PATH` with exit 5 (`EXIT_CHECK`) and the message `--check failed: relay does not contain 'X' -- the build did not pick up your change`. The check only runs *after* a build — there is no check-only mode — and the failure names neither the binary's build id nor the commit.
- `backend/relay_core/tryit_protocol.py` — `tryit_prompt()` heads the turn with the card's binary from `_app_binary(repo)` ("not built here — build it, or say so and stop" when missing) and embeds `card_brief("tryit")`. `backend/relay_core/board_tryit_brief.md` ("Try it brief v1", pinned by evals) step 3 says "name the binary … otherwise `build/relay` of this checkout"; step 7 lists honest reasons not to stage. Neither the prompt nor the brief requires building through the wrapper or proving the binary holds the change.
- `scripts/build-id.py`, wired at `CMakeLists.txt:707-719`, writes `relay.build-id` beside the binary at link time (a `date.hour.count` id the app shows on start). The id exists but nothing compares it at hand-off.

**Steps.**

1. `scripts/relay-build` — add `--check-only`: skip the lock, configure and build; run only the `--check` list against `--check-binary` (or `build/relay`). Upgrade the check-failure message to the named form, for both `--check` and `--check-only`: `binary predates the change: <binary> does not contain '<marker>' (build id <relay.build-id contents or 'none'>; HEAD <git short sha + date> or 'unknown'); the binary is stale, or the marker is not a literal this change adds`. Keep exit 5.
2. `scripts/relay-qa-run` — add `--check <literal>` (repeatable) and `--check-binary <path>`; compute the repo root from the script's own path. Before running the command: if any `--check` was given, or the command resolves (`readlink -f`) to `<repo>/build/relay`, run `scripts/relay-build` first (never a bare binary), then `scripts/relay-build --check-only` with the markers against the resolved binary. On check failure print the named failure and exit with its code without running the command. `--check-binary` pointing outside the repo skips the build (test seam; packaged binaries). A plain `relay-qa-run python …` with no flags keeps today's behaviour exactly (`tests/test_event_report.py:106` depends on it).
3. `backend/relay_core/tryit_protocol.py` — in `tryit_prompt()`, next to the binary line, add the rule: build through `scripts/relay-build`, prove the binary holds the change with `relay-build --check "<marker>"` where the marker is a string literal the landed change adds, and on exit 5 record `could not be staged: binary predates the change (build id …, commit …)` instead of staging.
4. `backend/relay_core/board_tryit_brief.md` — extend step 3 with the same build-and-prove requirement (including `stage.sh`), add "the binary predates the change" to step 7's honest-stop reasons, bump the brief marker v1 → v2 with a dated note, and update any eval that pins the old brief text (grep `evals/` for `board_tryit_brief` / pinned phrases).
5. Tests:
   - `tests/test_relay_build.py`: new test — first build with `MARKER-ONE`, `git init` the throwaway root, write `build/relay.build-id`, then `--check-only --check MARKER-ABSENT` exits 5 with `binary predates the change`, the build id and the HEAD sha, and no build ran. Update the pinned message in `test_runs_from_any_cwd_and_checks_the_binary`.
   - new `tests/test_relay_qa_run.py`, using the copy-the-script trick from `test_relay_build.py` with a stub `relay-build`: stale path — stub exits 5 with the named failure, so `relay-qa-run` exits non-zero, prints it, and the command (`touch ran`) never ran; fresh path — stub exits 0 and the command runs; no-flag path — `relay-qa-run python -c …` never invokes `relay-build`.
   - `tests/test_tryit_protocol.py`: new test asserting `tryit_prompt()` contains `scripts/relay-build` and `binary predates the change`.

**Risks.**
- A marker that is not really a literal the change adds makes a fresh build fail the check too; the message says both possibilities, and the Try-it agent picks the marker from the change's own strings (for #234Z, text the new code adds). No owner decision needed.
- `--check-only` reads the binary without the build lock, so a link in flight can make a fresh binary read stale; both hand-off paths run it right after a wrapper build, which holds the lock.
- POSIX only: these hand-offs are the Linux dev checkout; a Windows Try-it build path is out of scope (owner decision only if Windows hand-offs matter now).
- `relay-qa-run` gets slower when the command is the app binary (it builds first) — that is the card's point; backend probes are untouched.

**Verify.** `python3 -m pytest tests/test_relay_build.py tests/test_relay_qa_run.py tests/test_tryit_protocol.py tests/test_event_report.py`. By hand: `scripts/relay-build --check-only --check 'a literal only the new code has'` passes, and `--check 'definitely-absent-marker'` prints the named failure with build id and commit; `scripts/relay-qa-run --check 'definitely-absent-marker' ./build/relay --version` exits before running the binary.

Commit through `scripts/land.py` (`begin` before editing), evidence under `docs/qa_evidence/2026-09-24-j6mf/`, then `needs-verification`.

## Tests
- `tests/test_relay_build.py::RelayBuildTest::test_check_only_names_a_stale_binary_with_its_build_id_and_head`: real throwaway CMake build, fake `relay.build-id`, git HEAD. `--check-only` with an absent marker exits 5 with `binary predates the change`, the build id and the HEAD sha, and builds nothing. `test_runs_from_any_cwd_and_checks_the_binary` now pins the named message.
- `tests/test_relay_qa_run.py` (new, 6 cases, stub `relay-build`): a stale binary is named and never run; a fresh one runs; a drive script with `--check` checks `build/relay`; `build/relay` is always built first; a binary outside the checkout is checked but not built; a plain probe never builds.
- `tests/test_tryit_protocol.py::ReuseTests::test_the_prompt_requires_proving_the_binary_holds_the_change`, plus added asserts in `test_with_a_landed_commit_the_binary_comes_from_land_py_try` (the slot binary gets `--check-only --check-binary <slot>`).
- Run: `python3 -m pytest tests/test_relay_build.py tests/test_relay_qa_run.py tests/test_tryit_protocol.py tests/test_event_report.py` gives 68 passed.
