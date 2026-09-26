---
id: R8Z5
type: work
status: discussing
labels: [feature, plugins, console]
waiting_on: owner
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzw
created: '2026-09-26'
source: conversation, 2026-09-26
links: {plans: [], commits: [], evidence: [], related: [33G0], github: null}
---
# Validate and connect the Stata workspace on a Stata host

## Issue
Select and test a Stata bridge against the owner’s actual Stata version and edition, then prove the shared console and agent execution scenario on that host.

> yes to all.
> — elliott · [session:54948a7060344df2abef877a514ad9bf](relay://session/54948a7060344df2abef877a514ad9bf) · 2026-09-26

## Done means
A Stata console opens on the owner’s Stata host, preserves shared execution state between human and agent cells, reports failures clearly, and passes a live restart check.

## Plan
Identify the owner’s Stata version, edition and host; choose `pystata` on Stata 17+, `stata_kernel` for older versions where viable, and `stata -q` as the console fallback. Test shared human/agent state, restart and failure behavior on that host.
