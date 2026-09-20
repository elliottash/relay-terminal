# #BRD3 — the Switchboard pane's model selector (implementer evidence)

Implemented 2026-09-20 by the Relay pane agent (`glm/glm-5.3`). Screenshots are
`implementer-` prefixed: they show what the implementer saw, and are not a QA verdict.

## What was verified live (Xvfb :99, isolated `XDG_CONFIG_HOME=/tmp/brd3-xdg`, `build/relay`)

The data directory was **not** isolated, so the worker used this machine's real keystore
(no key material was read or copied by the implementer).

- `implementer-popup-rows.png` — the box in the Switchboard's tools row, popup open, rows:
  `Follow Main — kimi-k3`, `Flash — kimi-k2.7-code-highspeed`, `Lite — google/gemini-3.8-flash`,
  then the usable providers (`Relay Free`, `Kimi`, `GLM`, `DeepSeek`) and
  `⚙ Model roles…` (`implementer-popup-with-gear.png`).
- Pick **Flash** → `$XDG_CONFIG_HOME/RelayTerminal/relay.conf` gained exactly
  `[roles]\nswitchboard\tier=flash`, the board workers reconfigured, and a freshly opened
  Switchboard showed `Flash — kimi-k2.7-code-highspeed`
  (`implementer-flash-picked-row.png`) — the worker resolved the role onto the Flash tier's
  live model, not merely the pane's default.
- Pick **Follow Main** → the `[roles]` section was removed entirely (empty section dropped)
  and the box showed `Follow Main — kimi-k3` (`implementer-follow-main-row.png`).
- `scripts/relay-build` green after the change.

One fix came out of this run: a tiered role's `Resolved` carries the preset its tier landed
on, so the current-row logic first tried the provider row; the tier now wins
(`rebuildModelBox` in `src/BoardPane.cpp`).

## Placement follow-up (#8YQ9 t:6m, after the runs above)

The page agent's panel landed while this was being executed, and its composer row is where
the box belongs (composer parity with the main panes): after the verification runs above, the
box was reparented into `BoardChatPanel`'s composer row (`implementer-composer-row.png` —
"Ask about the board — Enter sends | Follow Main — kimi-k3"), dropped out of
`layoutListTools()`'s width math, and the `presets` branch now feeds the panel's microphone.
App boots green with the move; the pick paths are unchanged by it.

## Not verified by the implementer (left for QA)

- The box disabled while a Discuss/Plan/cleanup turn runs (the `syncModelBoxEnabled` path).
- The `⚙ Model roles…` row opening the roles dialog (`agent.modelRoles`).
- A keyless provider pick falling back to Main with the warning in the tooltip.
- Two-way agreement with the roles modal's "Switchboard card threads" Advanced row.
- No new unit tests, per the owner's instruction at execute time.
