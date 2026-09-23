# Ctrl+Enter on a deferred guest pane

Tested in the shared checkout on 2026-09-23 with an intercepted worker wire. The
test starts no Codex or Claude process.

`scripts/relay-build --target relay-consolemode-tests` — passed.

`ctest --test-dir build -R '^(consolemode|continueturn)$' --output-on-failure`
— 2/2 passed.

`tests/consolemode_test.cpp::ctrlEnterStartsADeferredGuestOnItsFirstPrompt`
constructs a fresh pane with `guest:codex` selected and the harness deferred.
For typed text and an empty composer, it checks that Ctrl+Enter sends only
`configure` first; the prompt stays queued; after `configured`, exactly one
`ask` carries the typed text or `Continue`, respectively.

The existing `continueturn` test checks that an idle empty box means
`Continue`, while text and a busy agent do not take that path. Visual and
real guest process behavior remain for separate verification.
