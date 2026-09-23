<!-- relay:entry 20260922T023000Z-ls author=codex kind=evidence -->
### Release investigation
Filed measured #R6BS failure and deterministic reproduction of leaked worker catalog callbacks
writing presets JSON into a later guest-hook stdout capture. This is test fixture interference,
not evidence that a stale permission answer was accepted. Root has the bounded fix recommendation;
no source/test change was made during this read-only investigation.

<!-- relay:entry 20260922T020859Z-ls author=codex kind=evidence -->
### Codex · 2026-09-22 02:08
Authorized fixture correction restores all four catalog listeners on leaving run_worker and
prevents its unrelated background refreshes. New regression proves pre-existing callbacks are
restored and later stdout stays uncontaminated. The original failing permission assertion is
unchanged. The bounded reproducer now reports no leaked listener and passes; all 102 tests in
test_guest_harness_provider and test_guest_hook pass. Product code and frozen beta4 source are
unchanged. Marked this fixture bug done.
