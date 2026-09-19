# Relay Free: handoff of the three open items

For the next implementer (written 2026-09-19 for a Kimi session; any agent can take it). Card
`#HG7K`, design in [RELAY-FREE.md](RELAY-FREE.md). Relay Free is live: the gateway runs at
`https://api.relay-terminal.ai`, the desktop lands on it with no key, and v0.1.0-beta.2 ships it.
Three things were left deliberately. Each is independent; do them in any order.

## Before you touch anything

- Read `CLAUDE.md` first. Several agent sessions edit this one checkout at once and commit to
  `main`. The commit procedure there is not optional: a private index, only your own hunks, build
  and test the exact tree, compare-and-swap onto `main` with the base read once, reset the shared
  index afterwards. Do not create a branch or a worktree, and never commit or revert a file you did
  not write.
- Run `git log --oneline -15` and `git status` before starting. The pane-state and remote files in
  item 1 are busy: `src/PaneState.h`, `app/pane.js`, `src/RemotePane.cpp`, `remote/` belong to the
  remote-sharing work (card `#W5N2`). Make small, surgical edits there and say so on the card.
- Tests: `./scripts/test.sh` for Python, `ctest --test-dir build` for everything. Python is the
  standard library only; nothing is pip-installed.
- Never print, log or commit a key. The gateway's provider key lives only in
  `/etc/relay-gateway/env` on the server.

---

## 1. The allowance chip in the phone view and the Relay-to-Relay pane

**What is missing.** On the desktop, a pane on Relay Free shows "Free · 73% left" beside the
context chip (`src/Pane.h`: `setHostedQuota`, `updateQuotaLabel`, members `m_quotaLimit`,
`m_quotaUsed`, `m_quotaResets`, shown only while `onHostedPreset()` is true). The two remote views
of a pane do not show it: the web view (`app/pane.js`, used by phones and guests) and the native
Relay-to-Relay pane (`src/RemotePane.cpp`). Both draw what the desktop publishes as `pane_state`
and publish nothing of their own, so the fix is one new field, published once and drawn twice.

**Where things are.**

- The published model: `src/PaneState.h`, struct `Inputs` (the `// context` block has
  `contextLabel` and `percentLeft`) and the function that turns `Inputs` into JSON. The wire shape
  is documented in `docs/REMOTE-PROTOCOL.md` section 16, where context is
  `"context":{"label":"96% left","percent_left":96}`.
- The desktop fills `Inputs` in `Pane::remoteState()` (`src/Pane.h`, search for
  `relay::panestate::Inputs remoteState() const`).
- The web view draws context in `app/pane.js` (`contextChip`, and the block that reads
  `state.context`). The native view does the same in `src/RemotePane.cpp` (`m_context`, and the
  block that reads `m_state.value("context")`).

**What to build.**

1. Add an optional `allowance` object to `pane_state`, a sibling of `context`:
   `{"label": "Free · 73% left", "percent_left": 73, "warn": false, "detail": "182,400 of 250,000 tokens today · resets at 02:00"}`.
   Omit it entirely when the pane is not on a hosted preset or no quota has been seen yet. The
   desktop writes the words; the views never compose them (that is section 16's rule).
2. Fill it in `remoteState()` from the same values `updateQuotaLabel()` uses, so the desktop chip
   and the published label can never disagree. Publish when `hosted_quota` arrives
   (`setHostedQuota` should trigger the same publish path the context chip uses).
3. Draw it in `app/pane.js` as a chip next to the context chip, with the warn style the context
   chip already has, and `detail` as its title. Draw it in `src/RemotePane.cpp` as a label next to
   `m_context` with the same rule.
4. It is read-only: no new client message, nothing in `remote/wire.py`'s client types. If
   `remote/wire.py` or the host filters `pane_state` by field, make sure `allowance` passes for
   every client level; it holds no secret.
5. Document the field in `docs/REMOTE-PROTOCOL.md` section 16 and tick the item on the card.

**Tests.** Extend `tests/panestate_test.cpp` (published when hosted and quota known, absent
otherwise, label equals the desktop chip's text), `tests/remotepane_test.cpp` (the label shows and
hides), and `tests/test_pane_view.py` with a fixture in `app/pane-demo.js` that carries
`allowance` (the chip renders, the warn class applies at 10 % and below). Note the browser tests
must wait with `document.body && ...`, never a bare `document.body.dataset`.

**Done when** a phone or a second Relay looking at a pane on Relay Free shows the same allowance
text as the desktop, it disappears when the pane switches to a provider with a key, and the three
test files cover it.

---

## 2. Tune the `relay-lite` output cap down from 512

**Why it is 512.** The owner's spec asked for 8 to 32 output tokens on Lite. It started at 512
because Relay's Lite chores are more than routing: pane titles and tab labels
(`backend/relay_core/titles.py`, `MAX_TOKENS = 1024`), 320-character session summaries
(`SUMMARY_MAX_TOKENS = 1024`), the request audit (`backend/relay_core/requests.py`,
`AUDIT_MAX_TOKENS = 1024`) and route assist (`backend/relay_core/route_assist.py`,
`MAX_TOKENS = 256`). The client asks for those numbers; the gateway clamps to the role's cap and
never refuses. A cap that is too low truncates a title or a summary mid-word, so measure first.

**How to measure.** The gateway logs one line per call with `role=`, `out=` (output tokens) and
`truncated=`. On the server (the owner runs this, or gives you the output; it contains no content):

```bash
ssh root@138.201.189.28 "journalctl -u relay-gateway --no-pager | grep 'role=relay-lite'" \
  | grep -o 'out=[0-9]*' | cut -d= -f2 | sort -n \
  | awk '{a[NR]=$1} END {print "n="NR, "p50="a[int(NR*.5)], "p95="a[int(NR*.95)], "max="a[NR]}'
```

Wait for a few hundred Lite calls across real use (titles, summaries and routing all appear as
`relay-lite`), not the handful that exist today. Gemini Flash-Lite at minimal reasoning spends
almost nothing on thinking, so `out` is close to the visible reply.

**The decision rule.** Set the cap to the smallest round number at or above 1.5 times the observed
maximum of legitimate replies, and not below 128 while summaries run on Lite. Expect something
like 192 or 256. Reaching the spec's 32 needs the second change below, not just a number.

**Changing it.** It is server configuration, no desktop release:
`/etc/relay-gateway/gateway.json`, `roles."relay-lite".max_output_tokens`, then
`systemctl restart relay-gateway` and `curl -s https://api.relay-terminal.ai/v1/health`. Mirror the
number in `gateway/gateway.example.json` and the sentence about 512 in `gateway/README.md` and
`docs/RELAY-FREE.md`, run `python3 -m unittest tests.test_gateway`, and commit.

**The optional second change** (an owner decision, recorded on the card): session summaries need
judgment more than speed, and the spec says not to put all summaries on Lite. In
`backend/relay_core/roles.py`, `ROLE_TIERS` maps the `chores` role to `lite`; summaries reach it
through `session_protocol._chores_provider`. Splitting summaries onto the existing `summaries` role
(Flash) would leave Lite with titles, labels, audit and routing, and only then is a cap near 64
realistic. This changes behaviour for every provider, not only Relay Free, so ask before doing it.

**Done when** the cap is set from measured data, the example config and both documents say the new
number and how it was derived, and a day of logs shows no `truncated=1` on `relay-lite`.

---

## 3. Move Lite to Gemini direct

**Why it is not done.** Lite runs Gemini 3.5 Flash-Lite through OpenRouter. Going direct to Google
should cut latency, which is the whole point of Lite, but the owner's spec forbids sending user
content through a tier whose terms allow training on it. That has to be confirmed before any
traffic moves, and it is the owner's account, so parts of this are the owner's.

**Steps.**

1. **Terms (owner confirms).** Read Google's current Gemini API additional terms and the pricing
   page. The expected answer is that a project with billing enabled ("paid services") is not used
   to improve Google's models while the free tier may be. Record the URL, the date read and the
   sentence relied on in `docs/RELAY-FREE.md` under "Privacy". If paid traffic is not excluded from
   training, stop here and leave Lite on OpenRouter.
2. **Key (owner).** Create an API key in a Google Cloud project with billing enabled, and add
   `GATEWAY_GEMINI_KEY=...` to `/etc/relay-gateway/env` on the server (mode 0600, never anywhere
   else).
3. **Check the reasoning knob before switching.** The `google` provider in the config uses
   `effort_style: "reasoning_effort"`, and the `relay-lite` role's default effort is `minimal`.
   `backend/relay_core/presets.py` records that Google's OpenAI-compatible endpoint rejects
   `minimal` on `gemini-3.8-flash`. Test `gemini-3.5-flash-lite` directly with
   `reasoning_effort: "minimal"`; if it is refused, set the role's `effort` to `low` when Google is
   the first upstream, or give the `google` provider `effort_style: "none"`.
4. **Config.** In `gateway.json`, give `providers.google.price_per_mtok` an entry for
   `gemini-3.5-flash-lite` with Google's list prices (the gateway refuses to start if a served model
   has no price), and make the role's upstreams an ordered pair so OpenRouter stays as the fallback:

   ```json
   "upstreams": [
     {"provider": "google", "model": "gemini-3.5-flash-lite", "extra": {"temperature": 0}},
     {"provider": "openrouter", "model": "google/gemini-3.5-flash-lite", "extra": {"temperature": 0}}
   ]
   ```

   Failover happens only before the first byte, on a 429, a 5xx or a connect timeout. Add
   `"google": <usd>` under `limits.per_provider_per_day_usd`.
5. **Restart and verify.** `systemctl restart relay-gateway`; the config check in
   `gateway/README.md` catches a missing price or key first. Then make one Lite call through the
   live gateway with the desktop's client (the snippet in the deployment note on card `#HG7K`'s
   thread does exactly this) and read the log line: `provider=google`, `status=200`, and a
   `ttft_ms` lower than the OpenRouter lines before it. Compare a dozen calls each way; if direct is
   not faster, revert the order and say so on the card.
6. **Repo.** Mirror the upstream order, the price and any effort change in
   `gateway/gateway.example.json`, add a test to `tests/test_gateway.py` if the effort handling
   changed, update the role table in `docs/RELAY-FREE.md`, and commit.

**Done when** the terms are recorded, Lite's log lines say `provider=google` with a lower time to
first token than before, OpenRouter remains the working fallback, and the example config matches
the server.

---

## Reporting back

Append to card `#HG7K`'s thread (`issues/features/2026-09-18-relay-free-hosted-inference.md`):
what you did, the commit, the evidence, and anything left with the reason it could not be done
here. Tick the matching line under "What is left" or turn it into a task with a checkbox. Evidence
for a GUI change goes under `docs/qa_evidence/<date>-<slug>/`. Then
`python3 scripts/relay-board.py check && python3 scripts/relay-board.py index`.
