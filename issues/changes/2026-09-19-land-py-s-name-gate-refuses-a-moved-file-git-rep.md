---
id: BTYE
type: work
status: ready
labels: [bug, tooling]
component: [worker]
rank: zzzzzzzi
created: '2026-09-19'
acceptance: a commit that deletes one claimed path and adds another lands without a workaround, and a test in the script's own suite moves a file
source: 'found by the #T71W session (Claude Fable 5.1 in Claude Code), 2026-09-19, landing 3b857ef'
links: {plans: [], commits: [], evidence: [], related: [T71W], github: null}
---
# land.py's name gate refuses a moved file: git reports a rename as one path

## Issue
scripts/land.py refuses a commit that moves a file: its name gate compares `git diff --name-only TIP NEW` with the session's paths, and git's rename detection reports a moved file as its new path only, so the old path is "missing" and nothing lands. Seen 2026-09-19 moving card #T71W from issues/features/ to issues/features/needs_qa_llm/ (every card's move to a QA lane is this shape):

land.py: name gate failed: the commit would touch ['docs/VALIDATION.md', 'issues/features/needs_qa_llm/…', 'issues/threads/T71W.md'] but this session's paths are [… the same three plus the old path …]. Nothing was landed.

Workaround that landed it (3b857ef) without touching the script: GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=diff.renames GIT_CONFIG_VALUE_0=false python3 scripts/land.py commit <me> -m …  The fix is one flag: pass --no-renames to that `git diff --name-only` (and to any other name comparison in the script).
