# Session manager, completed (card #SM4R) — implementer's run, 2026-09-18

Xvfb on a display nobody else was on, every XDG directory and TMPDIR isolated, no provider key,
`build/relay --clean-shell --fresh --workspace …/work`. Eight sessions were seeded by script into two
workspaces (summaries, checkpoints with written files, open todos, one unfinished, one pinned) and
the index reconciled them on first use. Opened with `/resume`.

| File | What it shows |
|---|---|
| `a-list-summaries-badges-continue.png` | Every row: the title on its own line, then tags (pinned, unfinished, edits · N files, the branch when it is not main) and the summary — or the first prompt when there is none. "Continue" heads the list with what this project was in the middle of. Model, branch, time, group and sort menus; the operator hint in the box. |
| `b-quick-look-unfolded.png` | → on a row: the quick look built from `overview` — Summarise (no summary yet), First:, the last turns as You:/Agent:, Files (1). The side preview shows the full conversation. |
| `c-operator-chips-and-no-match.png` | `file:parser -pelican index`: one chip per operator (`not: pelican` for the exclusion), the sort switched to Best match by itself, and the no-match state offering "Search all projects". |
| `d-project-operator-widens-scope.png` | `project:other`: the scope menu follows the worker's effective scope ("All projects") and the other workspace's three sessions are listed. |
| `e-grouped-by-date.png` | Group by: Date — Continue, Today, Yesterday, This month, Older. |
| `f-more-menu-filters.png` | The More menu: Has edits, Unfinished, Pinned, Has a summary, Open tasks, and Summarise all…. |
| `g-summarise-all-estimate.png` | Summarise all…: "3 conversations in this project have no summary. Summarising them costs about 1.5k input and 330 output tokens on the chores model. Nothing is summarised unless you press Start." Start was not pressed (no key in this profile). |
