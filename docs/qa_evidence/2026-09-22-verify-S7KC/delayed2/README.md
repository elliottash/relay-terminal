# Delayed remote completion verification

Pass. An ssh shim activated after an ordinary SSH connection was ready delayed only calls containing compgen by two seconds. It then exec'd /usr/bin/ssh with unchanged arguments, retaining the shared socket and destination. delayed-calls.txt records all three delayed calls. Drive exited 0.

- 01-command-completion.png: `printen` becomes `printenv` after the delay, confirming remote command completion through the shim works.
- 02-stale-edit.png: query `cat REMOTE_ON`, then replace draft after 0.3 seconds with UNCHANGED_DRAFT; after three seconds the edited draft remains unchanged and no stale popup appears.
- 03-stale-disconnect.png: query `cat REMOTE_ON`, then native-control exit disconnects SSH before the reply; after three seconds the composer remains `cat REMOTE_ON`, local cwd and badge are restored, and no old remote filename or popup is inserted.

The separate interactive-PATH extension failure in build6 remains: completion runs in a new remote noninteractive bash whose environment does not inherit the visible shell's export. Standard remote command completion passes here.

Real localhost SSH on one machine, deterministic worker stub, no paid model. This fixture delays process dispatch rather than network packets. Initial failed fixture is retained in ../delayed/ with its invalid-result note. No implementation edits.
