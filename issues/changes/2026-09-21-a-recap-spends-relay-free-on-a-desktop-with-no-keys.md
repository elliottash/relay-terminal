---
id: RCPF
type: work
status: discussing
labels: [feature, remote]
component: [worker]
waiting_on: owner
rank: m
created: '2026-09-21'
source: 'Claude Code session on #PH0N, 2026-09-21: finding 8 of the hosted drive, docs/qa_evidence/2026-09-21-ph0n-hosted-drive/'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-21-ph0n-hosted-drive/], related: [PH0N, HG7K, WMXN]}
---
# A recap on a desktop with no keys is written by Relay Free, unasked

## Issue

Found by #PH0N's hosted drive. A recap's side call asks for a cheap model
(`_start_recap` → `agent.side_provider(cheap=True, role="summaries")`,
`backend/relay_core/session_protocol.py:481`). On a profile with no Flash or Lite model
configured that falls through to Relay Free, so run 3 of the drive printed a recap written by a
hosted model on a machine started with `RELAY_KEYRING=off` and a pane on a local model. The same
shape hits any fresh profile, which is why QA drives keep reaching the network by accident
(`docs/qa_evidence/2026-09-20-perf-profile` saw it too).

Two of the three recap reasons are automatic (`away`, `resume`), so this can spend the allowance
with nobody asking for anything.

## Discussion points

The question is the owner's, because Relay Free exists precisely so that a keyless install still
works (#HG7K), and a recap is exactly the kind of small chore it was meant for.

Options, with a recommendation:

1. **Leave it.** Relay Free is the fallback for every model call; a recap is not special.
2. **Recommended: a manual recap may use Relay Free; an automatic one (`away`, `resume`) does
   not.** The person asked for the first and not the second, and an away recap that silently
   spends quota is the case that surprises. An automatic recap with no local or BYOK cheap model
   is skipped, and says so in the pane in one line.
3. **Never for side calls.** Titles, summaries and recaps stay on a configured model or do not
   happen. Cleanest for spend, worst for a fresh install, where the pane would fill with
   "no model for this".

Whichever is chosen, a test profile should be able to say "no hosted fallback" in one setting, so
a drive cannot reach the network by forgetting a tier list.
