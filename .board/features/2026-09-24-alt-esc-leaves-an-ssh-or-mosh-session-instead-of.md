---
id: 234Z
type: work
status: needs-verification
labels: [feature, keyboard, remote]
assignee: agent
implemented_by: glm/glm-5.3
session: 5c8138b9-4c6b-4280-abe8-5583cfdef77e
rank: zzzzzzzzzzzzzzzzzzzr
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [person], human: optional, criteria: 'inside a real ssh session, Alt+Esc returns the pane to its local shell prompt and toasts the exit; plain programs keep the two-press rule', sign_off: none, effort: low, stakes: rework, blast: capability}
source: pane 1, 2026-09-25
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-25-234Z/], related: [H2KQ, XCXD, VD2M], github: null}
---
# Alt+Esc leaves an ssh or mosh session instead of forwarding Ctrl+C to the far side

## Issue
alt esc needs to exit ssh/mosh sessions

## Done means
- With an interactive ssh or mosh session as the pane's foreground program, Alt+Esc **leaves the session**: the session client's process group is terminated, the pane returns to its local shell prompt, and a toast names what was exited (program, and host when known).
- That works from both input modes: composer focus and native/terminal focus inside the session pane.
- Programs that are not remote session clients keep #H2KQ semantics — first press Ctrl+C, next press a beat later SIGTERM — proven by the existing h2kq console cases still passing.
- The pane's Relaying line while a session runs names the exit key.
- The failure this removes: Alt+Esc inside ssh merely forwarding ^C to the far side (composer focus) or being swallowed by the client (native focus).

## Plan
**Goal.** Alt+Esc, pressed while a pane's foreground program is a remote session client (ssh, mosh, mosh-client, telnet, autossh), exits that session here and returns the pane to its local shell.

**Findings.**
- `src/Pane.h` `forceInterruptShell()` (Alt+Esc, card #H2KQ) sends Ctrl+C first — ssh/mosh forward that byte to the far side, so the session never ends; only a second press a beat later SIGTERMs the client.
- `src/Pane.h` `remoteCommandLine()` already knows the foreground program is one of `remoteSessionProgram`/`relay::panestatus::isRemoteProgram`; `relay::panestatus::remoteHost()` extracts the destination for the toast.
- `src/RelayWindowCore.cpp` runs the keymap action `terminal.interrupt` (Alt+Esc in every preset) as `interruptShell()` — Ctrl+C only — and `src/Keymap.h` `actsInsidePrograms()` keeps Alt+Esc from acting when the terminal widget owns the keyboard, so a native-input session pane swallows the key entirely.
- The Relaying busy line for a live session (`m_login` branch in `updateTakeControl`) does not name an exit key.

**Steps.**
1. `forceInterruptShell()`: when `remoteCommandLine()` is non-empty, SIGTERM the client's process group immediately (same pgid logic as the kill stage), pause the command queue (#XCXD stop semantics), reset the load/prompt flags, toast `Exited ssh · host`, and return — skipping the Ctrl+C-first stage.
2. `Keymap::actsInsidePrograms()`: return true when the key matches the `terminal.interrupt` action, so Alt+Esc is Relay's key even with a program owning the terminal (mirrors its description "Alt+Esc anytime").
3. `RelayWindowCore.cpp`: `terminal.interrupt` → `pane->forceInterruptShell()` so the action path shares the exit/two-stage semantics.
4. Busy line: the `m_login` branch appends the live exit key to the detail line ("Running on host · Alt+Esc exits").
5. New `tests/234z_cases.h` console cases: an ssh-named foreground program dies on the first Alt+Esc with the exit toast; a plain `sleep` still gets Ctrl+C first; `actsInsidePrograms` accepts Alt+Esc. Registered in `tests/consolemode_test.cpp`.
6. Docs: `docs/ARCHITECTURE.md` §9.2 and `docs/SSH-AND-MOSH.md` get the exit rule.

**Risks.**
- Nested sessions (ssh inside zellij) still see only the outermost program; out of scope, the pane's foreground program is the session client.
- A program that legitimately binds Alt+Esc loses it while it is the pane's foreground program — that is already true from the composer, this makes it consistent.
- SIGTERM leaves the local pty modes as ssh set them; the local shell restores its own modes when it regains the prompt (same as closing a terminal window today).

**Verify.** `scripts/relay-build --target relay-consolemode-tests` then `ctest --test-dir build -R '234z|h2kq|xcxd'`; by hand: run `ssh <host>` in a pane, press Alt+Esc, expect the local prompt and the exit toast.

## Execution Summary
Landed in `3ea76671` (code and tests) and `1a70f980` (evidence), both on `main`.

- `Pane::forceInterruptShell()` recognises a remote session client (`remoteCommandLine()` non-empty — ssh, mosh, mosh-client, telnet, autossh) and terminates its process group on the first press, pauses the command queue like any stop (#XCXD), resets the load/prompt flags and toasts `Exited <program> · <host>`; a mosh server stays up on the host for the next attach.
- The kill resolves the group via a new `foregroundProcessGroup()`: the kernel's `tpgid` from the pane shell's `/proc` stat, falling back to the backend's foreground pid. #H2KQ's second press had signalled `kill(-foregroundPid())`, which misses when the foreground pid is not the group's leader (`bash -c "sleep 30"` has `sleep` in front) — fixed for both stages.
- `terminal.interrupt` (the action Alt+Esc runs when the terminal owns the keyboard) now calls `forceInterruptShell()`, and `Keymap::actsInsidePrograms()` lets that action act inside a program, so a native-input session pane can leave too. `program_keys: none` still hands the key to the program.
- The Relaying line names the exit key: the login branch (`Relaying · … · Alt+Esc exits`, tooltip `Running on <host> · <key> leaves the session.`) and the processBusy branch for a session client before the login flow appears. (The processBusy hunk was swept into the #6CSN commit `960c0e70` while uncommitted; the rest is `3ea76671`.)
- Docs: `docs/QUEUE-INTERRUPT.md` (stop controls) and `docs/SSH-AND-MOSH.md` §4 — the Plan had named `ARCHITECTURE.md` §9.2, which does not carry the stop semantics; QUEUE-INTERRUPT is their home.

## Tests
All on the landed tree (land.py's verify tree for `3ea76671`), 2026-09-25:

- `relay-consolemode-tests --234z-only` — `234z: all cases passed` ×3 (case 1: an ssh-named, SIGINT-ignoring foreground program dies on the first Alt+Esc, the busy line had said `… · Alt+Esc exits`, the toast says `Exited ssh`, the stop strip clears; case 2: the same program named `bash` survives the first press's Ctrl+C and dies to the press after the beat).
- `relay-consolemode-tests` — `consolemode: 21 cases, all passed`.
- `relay-consolemode-tests --xcxd-only` — `queuecontract: all cases passed`.
- `relay-consolemode-tests --h2kq-only` — `h2kq: all cases passed` (#H2KQ semantics intact).
- `relay-keymap-tests` — `7 passed, 0 failed` (new slot `stopKeyActsInsidePrograms`: Alt+Esc acts inside programs under `shift-only`, not under `none`).
- `ctest --test-dir build -R 'consolemode|queuecontract|keymap'` — 3/3 passed (shared tree).

`tests/test_keybindings.py` has two failures on the shared tree (`test_qwas_defaults` over `Ctrl+Shift+R`/`review.open`, and the warp preset doc mirror) — both from another session's uncommitted `src/Keymap.h` binding-table hunks that predate this card and are not part of what landed here.

## Try it
Rebuild first — the shared `build/relay` predates the landed busy-line strings: `scripts/relay-build --target relay`.

Then in any pane run your usual `ssh <host>` (or a mosh), and once the session is up:

1. The Relaying line should read `Relaying · ssh… · Alt+Esc exits` (tooltip: `Running on <host> · Alt+Esc leaves the session.`).
2. Press **Alt+Esc once**: the session client ends, the pane returns to your local shell prompt, the queue pauses if anything was waiting, and a toast says `Exited ssh · <host>` (mosh: `Exited mosh`; the mosh server stays up on the host for the next attach).
3. Plain **Esc** should still be just a Ctrl+C reaching the far side (it interrupts a remote command; it does not leave).

The automated pass (`docs/qa_evidence/2026-09-25-234Z/`) proved the behavior with a real pty; what only you can judge is whether the key, line and toast feel right on a real session.
