---
id: ST1T
type: work
status: inbox
labels: [bug, qa, evidence, workspaces, docs]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-26'
source: 'Owner in a Relay pane, 2026-09-26, after pane b65a84fc hit it while delivering #WYGY and #ZA3S'
links: {plans: [], commits: [], evidence: [], related: [NA0H, WYGY, ZA3S], github: null}
---
# Queue workspaces hide docs/qa_evidence and its privacy rules, while every instruction still says to write evidence there

## Issue
The instructions agents follow and the workspaces they get disagree about QA evidence.

**What the instructions say.** RELAY.md, `.board/POLICY.md` and card plans (#WYGY's, for one) all say to put implementer evidence in `docs/qa_evidence/<date>-<slug>/`.

**What a queue workspace does.** It sparse-excludes that folder (`exclude = [".board", "docs/qa_evidence"]`, docs/TREES-AND-LANDING.md). So `docs/qa_evidence/README.md` and `.gitignore` are invisible from a workspace. Those two files hold the rules that matter:
- the evidence is **public**;
- screenshots and text must be reviewed for personal paths, accounts and tokens before committing;
- raw logs, traces and archives belong in the private relay-internal repo (#PVT9);
- `*.log` and `*.jsonl` are ignored.

Nothing in RELAY.md, CLAUDE.md or POLICY.md mentions the exclusion, how to include a card's own folder, or the public/private split.

**How it slowed down #WYGY and #ZA3S (pane b65a84fc, 2026-09-26).**
1. `ls docs/qa_evidence` failed in the workspace. Finding out why took several reads: `git ls-tree`, `git sparse-checkout list`, then TREES-AND-LANDING.md. The fix is `git sparse-checkout add /docs/qa_evidence/<folder>/` plus `git add --sparse`.
2. Since the README was invisible, I wrote and committed screenshots and state JSON first. They showed the owner's home path and shell prompt (user@host), the Models pane with both account emails, and `/home/...` scratch paths. I found the privacy rules only by reading `git show HEAD:docs/qa_evidence/README.md` while investigating something else.
3. The cleanup cost:
   - a UI fix and a script change (work dir under `/tmp`, screenshots cropped to the relevant panes);
   - a full rerun of the 9-step TeX live check (about 5 minutes);
   - reshooting the MCP sign-in screenshots;
   - rebuilding two unlanded commits, so the unredacted images never reach public history.

   Had the images landed, they would have been permanent in the public repo. About 30–40 minutes of a long delivery turn went here.
4. A related false lead: the public-repo `ls` failed while the tree at HEAD listed the folder, which briefly looked like a broken checkout.

**Fix, in outline.**
- (a) State the rule where agents read it: RELAY.md's Tests/Issues bullet and POLICY's evidence step. It covers public vs private, what to review, where raw logs go, and how to include your own folder (`git sparse-checkout add` + `git add --sparse`), or a `relay-land` helper that does both.
- (b) Have workspace creation include the README and `.gitignore` of `docs/qa_evidence` even when the rest is excluded (non-cone patterns can re-include them).
- (c) Optionally, a pre-commit or `relay-land` check that refuses images and text in `docs/qa_evidence/` that contain the user's home path, username or known account emails.

> and file a card for the qa_evidence issue and how it slowed you down
> — elliott · [session:45c2ee31a8d34d5184a33977aa649f5f](relay://session/45c2ee31a8d34d5184a33977aa649f5f) · 2026-09-26
