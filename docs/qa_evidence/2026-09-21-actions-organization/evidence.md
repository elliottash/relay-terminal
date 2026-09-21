# Actions organization — #A9QR

Built relay using scripts/relay-build. Built relay-settings-tests and ran
`ctest --test-dir build -R '^settings$' --output-on-failure`: passed.

Inspected the full action catalog and the rendered UI under Xvfb with isolated XDG settings:
- Everyday agent commands now lead the menu (top.png).
- Separate Conversations, Models, Files and projects, Remote and sharing, and Appearance groups replace mixed catch-all lists.
- New tabs/windows and splits lead Panes and tabs; layout moves, closing and reopening follow.
- Maintenance commands sit in Relay at the end; one File explorer row replaces two identical callbacks.
- Searching Remote and sharing finds the section. Enter clears search and scrolls to it; SSH and sharing commands appear together, followed by Appearance (sections.png).
- Existing shortcuts, slash hints and callbacks are retained. Unknown/new action keys are appended, never discarded.
