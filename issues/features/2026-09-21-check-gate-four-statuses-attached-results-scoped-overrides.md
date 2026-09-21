---
id: PR4Q
type: work
status: planned
labels: [feature, switchboard, tests]
component: [gui, worker]
parent: YZ8G
rank: zzzzzzzzzzzzzzzza
created: '2026-09-21'
source: 'owner, 2026-09-21: "i agree with all, go ahead with it" (#YZ8G plan)'
links: {plans: [], commits: [], evidence: [], related: [YZ8G, 7BM4], github: null}
---
# Check and the gate: four statuses, attached results, retired tests, scoped overrides, one status not a pile of blocks

## Issue
Step 1 of #YZ8G's plan, from Codex's review (docs/research/qa-across-fields/f-codex-skeptical-review.md §C): "Make the status distinguish passed, failed, missing evidence and not applicable. Block acceptance for explicitly required checks with applicable failures or missing evidence. Offer Run, Use this existing result, or Replace retired check. Keep incidental historical findings advisory … An override becomes a rubber stamp when the same known flake, unsupported runner or local-history gap prompts it repeatedly. Record an exception scoped to the check, relevant environment and revision or expiry … a card without ## Tests is ungated. That rewards omitting evidence." And §B: "Show the latest status; retain history behind a link"; "Update evidence status automatically after runs".

## Done means
- Check answers per listed test with one of: passed, failed, missing evidence, not applicable — never "never run here" when a run from any host or an attached result exists for this revision.
- An attached result (a JUnit file, a `relay-remote-tests` folder, a CI artefact) tied to a revision counts as evidence; the card page offers "Use this existing result".
- A listed test absent from discovery reads "retired" with the action "Replace retired check"; it does not by itself block.
- The gate blocks only on required checks with an applicable failure or missing evidence; an override is scoped to (check, revision) and expires; the same override is never asked twice for the same check and revision.
- A card without `## Tests` is gated on its `acceptance`/`## Done means`: the move to QA asks for the checks that prove it, once.
- The card body carries one current `### Check` status, replaced in place; history is one link to the thread.
- A finished run refreshes the strip and the findings without another Check click.
Failure would show as: an override prompt repeating for a known flake; "never run" on a test sphinxpad ran yesterday; a dated block pile; a card with no Tests section sailing through.
