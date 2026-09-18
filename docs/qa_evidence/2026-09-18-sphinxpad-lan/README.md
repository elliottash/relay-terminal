# Relay on sphinxpad, and what LAN testing needs (2026-09-18)

The owner's laptop now builds and runs Relay, and this is what the four devices need before the
phone client can be driven from them for real.

## The laptop

| | |
|---|---|
| Host | `sphinxpad`, 192.168.1.153 (`sphinxpad.local`), tailnet 100.116.24.28 |
| System | Ubuntu 26.04.1, x86_64, 12 cores, 30 GB; g++ 15.2, cmake 4.2, Qt 5.15.18 |
| Source | a `git archive` of `main` in `~/relay-terminal` (the commit is in `.commit`), not a checkout — the repo stays on spark |
| Installed by apt | `build-essential cmake ninja-build python3 libsecret-tools qtbase5-dev libkf5syntaxhighlighting-dev`, plus `xvfb x11-utils imagemagick` for the headless check. `qtpdf5-dev` (in README.md) does not exist on 26.04; the PDF preview is optional and the build simply does without it |
| Installed for the user | `cmake --install build --prefix ~/.local`: `~/.local/bin/relay`, the `.desktop` entry and the icons. Nothing system-wide beyond the apt packages, and nothing else in `~/repos` was touched |
| Start it | from the launcher ("Relay"), or `~/.local/bin/relay` |

`implementer-relay-on-sphinxpad.png` is the installed binary running under Xvfb there, with an
isolated profile: its own window, its own shell at `elliott@sphinxpad`.

**One real bug came out of it.** `ctest` on the laptop failed `markdown` where it passed on spark:
every numeric table cell rendered blank. `MarkdownAnsi::renderInline` built a cell with
`inner.feed(text) + inner.finish()`, and the order `+` evaluates its operands in is unspecified —
g++ 13 (aarch64) runs `feed` first, g++ 15 (x86_64) runs `finish` first, which flushes the renderer
before the text is fed. Fixed by sequencing both calls; the laptop now passes the same suites spark
does. A second toolchain earning its keep on the first run.

### The suites on the laptop, and two things the second machine exposed

`ctest` there: 44 of 45, with `backend-and-bash` the only failure, and nothing in it is a code bug:

- **`cryptography` is missing from the interpreter `ctest` picks up.** A non-interactive shell there
  finds the owner's uv-managed CPython 3.12 (`~/.local/share/uv/python/…`) before
  `/usr/bin/python3` 3.14, which does have it. Every `test_remote_*` module fails to import for that
  reason alone. Run with the system interpreter and they pass:
  `cd tests && PYTHONPATH=../backend:.. /usr/bin/python3 -m unittest test_remote_pane_state
  test_remote_wire test_remote_host test_queue` → 123 tests, OK. Nothing was installed into the
  owner's uv environment to make `ctest` happy; that is his to decide.
- **Four `test_router` cases depend on what is installed on the machine.** `test_table_routes_correctly`,
  the two `a_semicolon_in_a_sentence_is_not_a_command` cases and `sentence_punctuation_is_not_a_mistyped_command`
  use "Docker ps" and "ok; Docker ps", and pass on spark only because `docker` is on its PATH.
  sphinxpad has no `docker`, so the router reads the same sentence differently and they fail. The
  router's own resolution is injectable elsewhere (the unknown-`/command` work does this), so these
  cases should pass a command table in rather than read the machine's PATH — for the router's owner,
  not changed here. `test_ssh_shell.test_bash` is the same kind of thing: it needs a reachable sshd.

## What the phones and tablets need

The owner has an iPad, an iPhone, a Lenovo Android tablet, a Pixel and the laptop browser.

- **The app is served over https already.** The desktop mints a development certificate
  (`remote/devtls.py`) and serves the client on the chosen address; the phone accepts the warning
  once and rescans the QR (the fragment is lost across the interstitial). That is enough for
  WebCrypto, which is what pairing needs, and the owner has driven a real pane from the iPhone and
  iPad this way.
- **It is not enough for notifications.** A browser refuses to register a service worker behind a
  certificate warning, so Web Push cannot be tried on a real device until the certificate is real.
- **Plain `http://192.168.x.x` is not an option** and should not become one: `app/rrp.js` needs
  WebCrypto, which a browser only offers in a secure context.

### The recommendation: Tailscale, which is already on both machines

`tailscale status` shows spark (100.114.207.124) and sphinxpad (100.116.24.28) on the same tailnet,
and the iPhone is already a node on it. `tailscale cert` issues a real certificate for
`<machine>.<tailnet>.ts.net`, so:

1. `sudo tailscale set --operator=$USER` once on the desktop (`remote/devtls.py` already documents
   this as the path to a warning-free certificate);
2. share a pane and pick the tailnet address in the share dialog;
3. each device joins the tailnet (the iPhone is already on it) and opens the link with no warning,
   which also unblocks push and the microphone.

The alternatives, for the record: **mkcert** (not installed on either machine) means trusting a new
root on each of the four devices, including the Lenovo tablet, where installing a user CA is the
most awkward; **`app.relay-terminal.ai` + the `rv.` cloudflared ingress** is the owner's hosting
item, gives a real certificate to any device anywhere, and is the right answer once the feature
leaves the bench — it is server-side work on a box running a dozen sites, which `deploy.sh`
deliberately will not touch.

### Latency, for expectations

`ping` between spark and sphinxpad on the LAN: 3.9-5.1 ms, 4.6 ms average. The relay's own coalescing (one
`pane_state` per 100 ms, a 20 fps screen stream) dominates that by a wide margin.

## Relay-to-Relay is next, not done

The owner wants the laptop's **Relay app** to share sessions with the desktop's. The protocol for it
is here — the laptop pairs as the owner's own device with full control, not as a guest — and the
pane it would draw is the same `pane_state` this card publishes, plus the screen frames. Card #0VT4
holds that task; the session on card #W5N2 owns the transport it will use and asked for it to wait
until its wave 4 lands.
