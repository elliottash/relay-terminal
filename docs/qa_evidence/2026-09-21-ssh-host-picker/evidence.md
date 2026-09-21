# SSH hosts in a modal

- `scripts/relay-build --target relay relay-sshconfig-tests`: passed.
- `ctest --test-dir build -R '^sshconfig$' --output-on-failure`: passed (SSH config parsing, recent hosts, typed targets and command quoting).
- Full Relay GUI exercised under Xvfb with isolated XDG configuration/data/cache/runtime paths and fake recent hosts qa-alpha and qa-beta. Actions search for ssh shows one Connect to SSH entry and no host rows. Enter opens the modal with saved and recent hosts.
- Filtering qa-beta leaves that recent host selected. Typing demo@new-host.invalid offers one explicit new-host row. Invalid text containing spaces leaves no matching rows and disables Connect. Escape dismisses the modal. No SSH connection was made.
- Acceptance calls the existing connectToHost path, which opens a new tab, queues the quoted SSH command, and remembers the target. Network connections were not exercised.

Screenshots: actions.png, filtered.png, new-host.png. The unfiltered picker screenshot is not checked in because it contains the user's real SSH host list.
