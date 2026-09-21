# Guest context meter — C8WX

Relay ignored `context.guest_context` and displayed its 128,000-token transcript fallback.
The meter, tooltip, `/context` and remote pane state now use the running guest's reading.
An attached guest with no reading says `context unknown`. Compaction is described as owned
by Codex or Claude Code; Relay's transcript limit is not presented as their threshold.

Related fixes from the trace:
- Claude's recorded two-request bash fixture summed 43,585 prompt tokens. Its final request
  used 22,252 tokens; that is now its occupancy. Cumulative usage accounting is unchanged.
- Claude chooses the final parent model's window rather than the first model in modelUsage.
- Usage reports refresh the meter during a turn. Model changes clear the reading, and late
  usage from the old turn cannot repopulate it. A detached guest restores native accounting.
- Codex reports a measured zero-token context as zero rather than dropping the reading.

Validation:
- `scripts/test.sh --junit /tmp/context-c8wx-tests.xml tests.test_guest_context_meter tests.test_guest_harness_claude tests.test_guest_harness_codex tests.test_guest_harness_provider.TurnTests tests.test_guest_harness_provider.AgentWiringTests tests.test_guest_harness_provider.WorkerProtocolTests`
- `ctest --test-dir build -R '^contextmeter$' --output-on-failure`
- `scripts/relay-build --target relay-contextmeter-tests relay`
- `python3 docs/qa_evidence/2026-09-21-guest-context-meter/drive.py`

The GUI drive launches the real binary under Xvfb, with isolated XDG directories and a scripted
worker; no provider calls. Screenshots and OCR assertions show Codex at 25,840 / 258,400,
Claude at 20,000 / 200,000 (both 90% left), and unknown usage with no fabricated percentage.
The `/context` output also says which guest manages compaction. These are protocol fixtures,
not claims about the user's current live session size. See `gui.txt` and the three PNGs.

The initial broader provider-suite run also found an unrelated Luna model-level expectation
mismatch in CatalogueTests, in files undergoing concurrent model-ranking work. The focused
context, adapter, wiring and worker tests above pass; model catalogue changes are outside this fix.

Final landing: `f94f669d64ba057b35f68ad99213c9511f87fbe3`. The exact commit tree passed
land.py's build gate. The final six context regression cases and contextmeter CTest are recorded
as passing in the Switchboard test store; tests_check returned no findings, failures or blocks.
The transient catalogue failure above passed on recheck after the concurrent changes landed.
Global board check reported existing format errors elsewhere; none concern C8WX.
