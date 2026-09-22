# Editor click and wrap evidence

Cards: #CEW2, #WWB2. Implementer evidence, 2026-09-22.

- `scripts/relay-build --target relay relay-filepanes-tests relay-consolemode-tests`: passed, build 2026-09-22.13H.07.
- `xvfb-run -a env XDG_CONFIG_HOME=/tmp/editwrap-test-config ctest --test-dir build -R '^(consolemode|filepanes)$' --output-on-failure`: 2/2 passed (4.14 seconds).
- Console regression exercises actual Pane routing: Ctrl+click edits the file with its line number, bypasses context preview, plain click retains context routing, Shift opens externally, directories are not edited.
- File editor regression clicks Word wrap, verifies multiple visual lines, unchanged document text and clean state, Markdown view switching and disabling wrapping.
- Live FilePreview under Xvfb with isolated config: [wrapped](wrapped.png), [unwrapped](unwrapped.png). Inspected wrapped screenshot: top-bar button is checked and paragraph wraps. Harness links the built FilePreview library and clicks the button in both directions.
- `git diff --check`: passed.
- Board format check reports existing issues elsewhere; neither new card has a finding. The bridge does not expose tests_check.

Try it in the new Relay build: Ctrl+click a text/Markdown file path in terminal output; the file opens editable. Click Word wrap in its top bar to toggle wrapping. Existing running instances need a restart to load the rebuilt executable.
