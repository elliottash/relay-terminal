# Staging notes — #DVV2 Try-it

**What is staged.** One agent's scratch life through the `relay-scratch` CLI in a sandbox
(`stage.sh` wipes and recreates `sandbox/` on every run; the real ledger, the real scratch homes
and the real `$HOME` are never touched — every command runs with `HOME`, `RELAY_LEDGER`,
`RELAY_SCRATCH_HOME`, `RELAY_TOOLS_HOME`, `RELAY_STATE_HOME` pinned into the sandbox). The
transcript in `transcript.txt` is a captured run.

**How it differs from real use.**
- The TMPDIR row is written by the script the way the backend's worker writes it
  (`scratch_tmpdir`, keyed on the pane token). The pane's shell and guest CLIs get their TMPDIR
  from `PaneRuntime.cpp` / `GuestBridge.h` — that part needs the app running and is the
  verifier's call, not staged here.
- The post-turn sweep is shown via its scanner (`unledgered_created_since`); inside a session it
  is wired to the turn's completion reminder and names the entries to the agent as a note, once.
- Step 9 demonstrates the 6 h gc floor in the library; the CLI prints the same refusal.
- The rogue dir `/tmp/dvv2-tryit-rogue` is created on the **real** `/tmp` on purpose: it is the
  thing the sweep would name to the agent, and the person's decision to make (delete it, or
  `relay-scratch adopt` it). Delete it after, or leave it for `adopt --apply` to list.

**Rerun:** `docs/qa_evidence/2026-09-25-tryit-DVV2/stage.sh` (from anywhere; ~2 s, no network).
