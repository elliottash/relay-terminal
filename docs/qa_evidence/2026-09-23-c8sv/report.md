# C8SV Keep-link crash investigation

The only matching GUI crash in the available rotated logs remains the
2026-09-23 16:44:18.932 UTC SIGSEGV, 1 ms after
`globals_suggestion_accepted`. Its three saved frames do not identify an
application function. No later `gui_crash` or matching acceptance appears in
the available logs.

At detached revision `3dad4ea0cdb054c4db39062bc9ab6dc876072e9e`, I applied
[`combined_probe.patch`](combined_probe.patch) only in
`/tmp/relay-c8sv-review-WFkST5`. It extends the existing transcript memory test
to show a real `GlobalsPane`, route the transcript Keep link through
`Pane::openOutputTarget`, deliver the accepted worker reply, refresh the visible
Globals pane through the decision callback, and deliver the resulting
suggestions/list replies. `scripts/relay-build --target relay-consolemode-tests`
passed, followed by `scripts/relay-qa-run ctest --test-dir build -R
'^consolemode$' --output-on-failure` (passed, 3.00 s).

This probe covers the combined callback flow that the prior separate tests
missed. It did not reproduce the crash. The probe uses direct link routing,
not a pointer click in a live `RelayWindow`, and has no symbolized fault stack.
It therefore does not identify a safe production fix. The next diagnostic is
an isolated live Keep/No click under `scripts/relay-debug` with Globals visible,
then a pane close before a pending reply; preserve any backtrace before making
a GUI lifetime or reentrancy change.
