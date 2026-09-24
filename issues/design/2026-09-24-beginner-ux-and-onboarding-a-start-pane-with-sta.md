---
id: 9HS0
type: work
status: discussing
labels: [feature, design, gui, onboarding]
component: [gui, worker]
waiting_on: owner
rank: zzzzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
source: Claude Fable session in Relay, 2026-09-24
links: {plans: [], commits: [], evidence: [], related: [K2FV, HG7K, MH58, ZYRB, 8E4Q], github: null}
---
# Beginner UX and onboarding: a Start pane with starter tasks, a prompt box that says what it is for, and behaviour-triggered tips

## Issue
i want to work on the beginner UX and onboarding with relay. how do we make the first experience as powerful and smooth and inspiring as possible?

look at how warp / claude / codex / ghost ty / MSword / vscode / cursor / github / etc deal with this problem, and also more about mass design, to try to make relay immediately powerful and flexible and impressive for sophisticated users, while welcoming and obvious and helpful for people who have never used a terminal

something to note -- i think when it opens, it could propose some tasks

start coding

start analyzing data

triage my email

organize my folders

set up model providers

organize multiple subscriptions

start a painless SSH session

generate some art

develop a game

etc

i guess we should have some tip notifications as well about functionality, maybe targeted based on what people are doing.

## Discussion points
The research and the proposal are in `reports/Beginner UX and onboarding for Relay.md`, drawing on four notes in `research_notes/Beginner UX and onboarding for Relay/` (a file:line audit of today's first run; Warp, Ghostty, Claude Code, Codex, Cursor, VS Code, Zed, Fish, Raycast, Nushell; Word, GitHub, Notion, Figma, Slack, Superhuman, Arc, Linear, Apple, Duolingo, Canva, ChatGPT, games; and the HCI and onboarding literature).

**Today's first run**: one terminal pane, the Models pane on the providers tab (premise predates Relay Free), the instructions dialog at 400 ms and the approvals screen at 800 ms (`src/Pane.h:5798-5812`), then an empty box saying "Shell commands or agent prompts…". Six idle tips exist (`src/Pane.h:10182-10210`), triggered by idleness, not by what the person did.

**Proposal in one paragraph**: replace the two dialogs with one Start pane at the right, whose body is the owner's starter-task list (start coding, analyze data, write a paper, triage email, organize folders, set up providers, subscriptions, SSH, art, game, just a terminal). Each task is a recipe: layout, a first prompt staged in the composer but not sent, a one-line needs check, and the tips it arms. The #K2FV approvals choice sits on the same pane as its last row, still an explicit pick. Esc closes it for good; `/start` reopens. The placeholder says "Type a command, or say what you want done"; the routing chip is visible while typing; `?` answers "what can you do here?" accurately; Relay Free is named on the first reply and warned at 20%. Tips become behaviour-triggered (a failed command, a first agent edit, an `rm` under allow-everything, `ssh`, a folder with a TODO.md), name their key, and retire once the key is used. Delivery is three cards: Start pane and recipes (large, UI), prompt box and Relay Free labelling (medium), targeted tips and wildcard preview (medium).

**Decisions needed** (report §4): (1) approvals choice on the Start pane's last row, or kept as its own screen before it; (2) which starter tasks ship bundled, given email triage and subscriptions rest on skills that are not bundled; (3) whether picking a starter task files it as the first Board card, which means asking the project-init question on the first run; (4) whether the Start pane carries the switchboard material (#8E4Q lists first-run as a permitted surface).

## Decisions
- 2026-09-24, owner: "back to the /start page -- i agree with all the recs and the overall design." The four questions in `## Discussion points` are settled as recommended: the approvals choice is the last row of the Start pane and still an explicit pick; all eleven starter tasks ship, with email triage and subscriptions saying "needs a skill" until a bundled skill exists (#SZ1H); a starter task files a Board card only when the folder already has a Board; typography and the two destination colours only, no switchboard materials, until #8E4Q is decided.
- 2026-09-24, by the same agreement: the Models pane no longer greets a fresh profile, which reverses the first-run layout of #MDL1 t:a11 (owner, 2026-09-21). "Set up model providers" on the Start pane is how that pane is reached on a first run.
- Open: questions 5 to 19 in the thread (Esc versus the approvals pick, task-before-approvals order, the recipe's folder, art on Relay Free, a keys row, no persona question, tip budget, the Security tip, the Relay Free note, the import row, a health check, local-only measurement, platforms, the name, and filing the three delivery cards).
