# Qt 6 build, and Relay installed on `sphinxpad` (card `#WV4V`)

Implementer evidence, 2026-09-18. The owner asked for Relay on `sphinxpad` — their laptop,
Ubuntu 26.04.1 x86_64, 192.168.1.153 — and then for the Qt 6 compile errors to be fixed rather
than worked around.

Ubuntu 26.04 has no Qt 5 branch in `packaging/deb/build-deb.sh`: it builds Qt 6, and Qt 6 did
not compile. Five source fixes later it does, and the `.deb` installs and runs there.

| File | What it is |
|---|---|
| `qt6-errors-before.txt` | The four `qsizetype` errors, identical on aarch64 and on sphinxpad's x86_64 + KF6 |
| `sphinxpad-qt6-ctest.log` | `ctest` on the Qt 6 build on sphinxpad: 44/45 |
| `spark-qt5-ctest.log` | `ctest` on a Qt 5 build of the same tree on this desktop: 44/45, unchanged by the fix |
| `sphinxpad-smoke-installed.log` | `packaging/smoke-installed.sh` against the installed `.deb` |
| `sphinxpad-package.txt` | `dpkg -s relay` on sphinxpad |
| `relay-on-sphinxpad.png` | Relay running there, with a typed command and its output |

## What was run

```bash
# on sphinxpad, from a clean `git archive` of main plus the five fixes
cmake -S relay-src -B deb -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr -DRELAY_QT_MAJOR=6 -DBUILD_TESTING=ON
cmake --build deb --parallel 12
ctest --test-dir deb --output-on-failure -R backend-and-bash
cpack -G DEB                       # relay_0.1.0-1~ubuntu26.04_amd64.deb
sudo apt-get install -y ./relay_0.1.0-1~ubuntu26.04_amd64.deb
RELAY_SMOKE_SECONDS=20 packaging/smoke-installed.sh
```

The screenshot was taken under `xvfb-run` with an isolated `HOME`, `XDG_RUNTIME_DIR` and
`TMPDIR`, driving the real window with `xdotool`: `uname -srm && echo relay-on-sphinxpad-works`
runs in the terminal pane, prints `Linux 7.0.0-31-generic x86_64`, and the pane shows
`Shell ready · exit 0`.

## The 45th test

`backend-and-bash` fails on `main` with or without this change — `test_roles`
(`test_a_tier_falls_back_to_main_when_nothing_else_has_a_key`,
`test_unknown_main_provider_keeps_every_role_on_main`, both about the `local` role) and
`test_tools` (`test_absolute_and_parent_paths_allowed_inside_workspace`). Verified against a
pristine `git archive` of `main`; they belong to another session's in-flight work.

## Two things that are sphinxpad's environment, not the code

- **`ctest` picks up a uv-managed Python.** CMake's `find_package(Python3)` found
  `~/.local/bin/python3.12`, which has no `cryptography`, so all twelve `test_remote_*` modules
  failed to import. The system `python3` (3.14.4) has it. `Python3_EXECUTABLE` is only used for
  the `backend-and-bash` test command, never compiled into the app, so the `.deb` is unaffected.
  Configure with `-DPython3_EXECUTABLE=/usr/bin/python3` to test there.
- **`test_router` and `test_ssh_shell` read the machine.** `route -n` is expected to be a real
  command (no `net-tools` on sphinxpad), the mistyped-command cases depend on what is installed,
  and `test_bash` asserts the `bind -X` output format, which Ubuntu 26.04's newer Bash prints
  without the `:` separator. These pass on this desktop and are about the host, not Relay.
