---
id: 1CXD
type: work
status: planned
labels: [feature, switchboard, docs]
component: [gui, worker]
rank: zzzzzzzzzzzzzzzzd
created: '2026-09-21'
source: 'owner, 2026-09-21'
links: {plans: [], commits: [], evidence: [], related: [YZ8G], github: null}
---
# "Switchboard" becomes "Board" in the product; the icon and aesthetic stay; the folder becomes `board/`

## Issue
also im sold to change switchboard to board, lets just use the switchboard icon / aesthetic. i would change .switchboard to /board. you can deploy a subagent to scope that

## Decisions
- 2026-09-21, owner: the product word is **Board**; the switchboard icon and aesthetic are kept; the board folder in a project changes from `.switchboard/` to `board/`.

## Done means
- No string a user reads in the GUI, the phone client, the website, the worker's briefs or the
  README says "Switchboard" any more, except the busy strip's "Switchboarding" and the aesthetic's
  own vocabulary; `Glyph::Switchboard` and the switchboard icon are byte-for-byte unchanged.
- A project initialized after the change gets `board/`, and a checkout with `.switchboard/`,
  `switchboard/` or `issues/` opens, reads and writes exactly as it does today, with this
  repository's own `issues/` untouched.
- Every test named in the Steps passes, and `rg -i switchboard` over `src/ backend/ app/ remote/
  tests/ docs/ scripts/ site/` returns only the aesthetic, the frozen identifiers listed in
  Findings §2, and the three redirect stubs.
- **Failure looks like:** a board that stops being found after the lookup list changes; a pointer
  block duplicated in a project's `CLAUDE.md` because the marker bytes moved; or the word surviving
  in one surface only — the palette row, say — so the product calls itself two things at once.

## Plan

**Goal.** Every place a user reads the word reads **Board**; the switchboard icon, the brass/bakelite
materials and the `SWITCHBOARD-AESTHETIC.md` vocabulary stay exactly as they are; a new project's
board folder is `board/`, and every board that exists today keeps working untouched. Nothing about
the card format, the protocol message names or the `board_*` tools changes.

### Findings

**Size.** 222 live files, 1,944 occurrences of either spelling (excluding `.git/`, `issues/`,
`docs/qa_evidence/` and two stray scratch files): 1,147 capital `Switchboard`, 646 lowercase
`switchboard`. By area: `src/` 62 files / 516, `tests/` 63 / 510, `docs/` 22 / 460,
`backend/` 41 / 324, `data/` (theme comments) 5 / 25, `app/` 8 / 24, `remote/` 5 / 21,
`scripts/` 5 / 18, root `*.md` 5 / 21, `site/` 1 / 8, `engine/` 2 / 4, `packaging/` 1 / 1.
Most of it is prose in comments and docstrings, not strings a user sees.

**1. Product-visible strings.** In `src/` only 130 of the 363 `Switchboard` lines sit inside a C++
string literal; the rest are comments. The user-visible set, by file:

- Pane title and header band — `src/PaneStatus.cpp:287` `{"board", "Switchboard", "tools", Glyph::Switchboard}`,
  `src/PaneStatus.cpp:403-404` (`"Switchboard: cards, threads and plans"`, `"Close the Switchboard"`,
  `"the Switchboard"`), `src/PaneStatus.h:172,202`. The list header itself is
  `src/BoardPane.cpp:5770-5771` — `"Switchboard"` / `"Switchboard · %1 open"`.
- The running-turn strip — `src/BoardPane.cpp:2945-2946` `"✦ Switchboarding · planning…"` /
  `"… · discussing…"` (owner's own coinage, 2026-09-19, quoted at `BoardPane.cpp:2938-2939`).
- Empty and error states — `src/BoardPane.cpp:4174,6761` `"Loading the Switchboard…"`,
  `6310` `"The Switchboard agent is busy."`, `6423`/`5012`/`6461` "the Switchboard agent…",
  `4214,4378,7362,7404,7421,7864` the six `reason` strings (`"edited in the Switchboard"`,
  `"changed…"`, `"moved…"`, `"marked done…"`, `"deleted…"`) that are written into card threads,
  `7603` `"Execute · handed to a new terminal pane beside the Switchboard…"`.
- Helper panel header and placeholder — `src/BoardPane.cpp:3668` `spec.briefTitle = "Switchboard agent"`,
  `3772` `"Ask the Switchboard agent — Enter sends, a second prompt queues"`; the title is carried in
  `src/AgentContext.h:188,192` and asserted in `tests/test_agent_context.py:17,55` and
  `tests/agentcontext_test.cpp:219-222`.
- Worker status lines — `src/BoardWorker.cpp:30,45,58`.
- Keybinding action labels — `src/Keymap.h:278` `add("board.open", "pane", "Switchboard: cards, threads and plans (again to close it)", Ctrl+Shift+S)`
  and `:283` `tests.open` ("…beside the Switchboard").
- Palette rows and aliases — `src/RelayWindow.h:4431-4434` label `"Switchboard"`, detail
  `"Cards, threads, plans and project memory"`, aliases `"board issues cards todo trello kanban scratchpad tickets tracker"`;
  `:4442` Test suites detail; `:4450` `"Switchboard HQ: global memories, aliases and instructions"`;
  `:4458` detach detail and `:4459` its aliases (`"project switchboard attach unattach board"`).
- Options › Agent — `src/RelayWindow.h:3493` `headingRow("Switchboard")`, `:3502` the
  `"Hidden Switchboard folder"` toggle, `:3505` its aliases (`"switchboard board folder dotfile hide show dotswitchboard"`),
  `:3414` the Agent page blurb, `:3528` its notice.
- Globals pane — `src/GlobalsPane.cpp:19` `"Switchboard HQ · memories, aliases and instructions across projects"`,
  `:111` `"Globals / Switchboard HQ…"` (the accessible summary).
- Projects pane / picker — `src/ProjectsPane.cpp:63` the `"Switchboard"` button, `:157`
  `"Switchboard available"` / `"No Switchboard yet"`; `src/ProjectPicker.cpp:60`
  `"opened its Switchboard"`, `:132` the init tooltip.
- The init question, all of it — `src/ProjectInit.cpp:142` `"Initialize a project and create a Switchboard here?"`,
  `:120` `"%1 already has a Switchboard."`, `:264` `"Switchboard created in %1/"`, `:272`, `:278`;
  `src/ProjectInitBlock.h:92` the "Not now" tooltip.
- Slash commands — `src/Pane.h:9123-9125`: `/switchboard` ("Open the Switchboard: …"), `/card`
  ("Add a card to the Switchboard inbox, verbatim"), `/init` ("…create its Switchboard").
- Terminal hints and chips — `src/Pane.h:10359,10361,12302,15581,15628`, `5068`
  (`"Switchboard card being executed"`, an accessible name), `18664` (`"Switchboard · Execute #%1"`).
- Card-attachment prompt text the agent reads — `src/BoardModel.cpp:367,384,411,625`
  (`"The Switchboard card %1 is attached"`, "…as the Switchboard rules say…").
- Phone/desktop bridge notices — `src/BoardRemote.cpp:521,531,608`, `src/RelayWindow.h:7216,7640-7641,5443,5575,6548-6550,1539`.
- Backend user-facing text — `backend/relay_core/board_protocol.py:95` `NO_BOARD_CHAT_ERROR`
  ("This tab has no Switchboard, so there is nothing for the Switchboard…"); the five briefs
  (`board_chat_brief.md:1,4` "You are the Switchboard agent for this board", `board_cleanup_brief.md:11,70`,
  `board_plan_brief.md:1,3`, `board_discuss_brief.md:1`, `board_policy.md:1,25`); `board_tools.py`
  (62 hits, incl. the `[Switchboard card #…]` prompt block and `"Your Switchboard session: …"`);
  `agent_context.py:150,157-160`; `board.py:1028` `CONFIG_TEXT` header comment.
- Web/phone client — `app/board.js:249` inbox title `'Switchboard'`, `:266` bar name, `:396`
  `'This project has no Switchboard yet.'`, `:606` `'Loading the Switchboard…'`, `:608`
  `'This project has no Switchboard yet. Create one on the desktop: the Switchboard pane offers it.'`;
  `remote/wire.py:268` refusal text, `remote/host.py:3000,3016,3020` three refusals,
  `remote/board_state.py:245`.
- Website and packaging — `site/index.html:153,161,165,203-204,359-361`;
  `packaging/org.relayterminal.Relay.metainfo.xml:73`.
- README/docs prose — `README.md:104,134,140,301,424,437,444` (including the
  `### Switchboard (Ctrl+Shift+S)` heading and `/switchboard`); `docs/README.md:21,28,41,42,43,49,50`;
  `CONTRIBUTING.md:11-12`; `WARP.md:44`; `CLAUDE.md:244` and the generated block at `:291-305`;
  `AGENTS.md:10-24`.
- The `switchboard` model role label — `backend/relay_core/roles.py:76` (`ROLES`), `:81`
  (`HELPER_ROLE = "switchboard"`), `:92` (model-box group `"helpers"`), `:123`
  (`("switchboard", "Helper agent", "the Switchboard's agent, and the helper in Options, …")`),
  `:142` (tier `main`); GUI mirror at `src/ModelRows.cpp:28,48` and `src/ModelRows.h:137-138,220-222`.
  The *label* already says "Helper agent" (#BRD3/#MDL1, owner 2026-09-21 "i also dont like how it
  says switchboard in the picker") — only the third element, the description, names the Switchboard.

**The design doc already recommended this, on 2026-09-17.** `docs/SWITCHBOARD-DESIGN.md:8-13`, §1
"Name": *"Switchboard fits Relay's telecom theme (it routes work between owner, agents and
collaborators) but is long in UI, tool and command names, reads like a settings screen, and the
owner suspects it is too cute. … **Recommendation: "Board"** in product, code, tools and docs
(`BoardPane`, `board_*`, `/board`): short, obvious (Trello, GitHub Projects). "Switchboard" only as
a website nickname."* The code half of that recommendation was taken at the time (`BoardPane`,
`board_*`, `board.py`); only the product word was not.

**Which of these are read by tests.** Assertions on the literal word: C++ —
`boardmodel_test.cpp` (7), `projects_test.cpp` (4), `projectinit_test.cpp` (4, incl. `:271`
the whole title line and `:375` `"Switchboard created in …/.switchboard/"`), `settingspane_test.cpp`
(3, incl. `:855` which greps `RelayWindow.h` for `headingRow(QStringLiteral("Switchboard"))`),
`panestatus_test.cpp` (2, `:379` `QCOMPARE(board.label, "Switchboard")`), `projectpicker_test.cpp`,
`conversations_test.cpp`, `agentcontext_test.cpp` (1 each), plus `boardfilter_test.cpp:185,201`
(worker status text) and `boardworkspace_test.cpp:80` / `boardmodel_test.cpp:640` (they iterate
`boardFolders()`). Python — `test_board.py` (34), `test_board_protocol.py` (31),
`test_board_tools.py` (13), `test_board_chat.py` (9), `test_system_prompt.py` (4),
`test_project_probe.py` (3), `test_roles.py` (2), `test_prompt_profiles.py` (2),
`test_guest_harness_provider.py` (2), and one each in `test_tests_protocol.py`,
`test_test_history.py`, `test_signal_threads.py`, `test_queue.py`, `test_board_view.py`.

**2. Identifiers.**

*Already "board" — nothing to do:* `BoardPane`, `BoardModel`, `BoardWorker`, `BoardWorkspace`,
`BoardRemote`, `BoardSections`, `BoardSignals`, `board.py`, `board_tools.py`, `board_protocol.py`,
`board_policy.md`, every `board_*` tool and protocol message, `board.open` / `board/hidden_folder` /
`board/default_project` settings keys, `[board]` theme table, `app/board.js`, `app/board.css`,
`scripts/relay-board.py`. The 2026-09-17 recommendation was already applied here.

*Carrying "switchboard" — with a recommendation for each:*

| Identifier | Where | Recommendation |
|---|---|---|
| `Glyph::Switchboard` | `src/PaneStatus.h:161`, `src/PaneChrome.h:120`, `PaneStatus.cpp:287,303` | **rename never.** It names the *icon*, which the owner is keeping. Renaming it would be the one change that contradicts the decision. |
| `relay::projects::kReasonSwitchboard = "switchboard"` | `src/Projects.h:92`, `ProjectInit.cpp:56`, `RelayWindow.h:6573,7215`, `ProjectPicker.cpp:60` | **rename never.** Its *value* is written into the project registry on disk and read back by `projects::isReason()`; changing it invalidates every remembered project. Only the rendered text ("opened its Switchboard") changes. |
| `projectinit::Trigger::Switchboard` | `src/ProjectInit.h:30`, `ProjectInit.cpp:56,66,72`, `Pane.h:15653,15886`, `RelayWindow.h:6052`, asserted by `projectinit_test.cpp:433` | **rename later.** Purely internal, but its value is `kReasonSwitchboard`, so it is cheapest to move with that identifier if it ever moves. |
| role id `"switchboard"` | `backend/relay_core/roles.py:76,81,92,123,142`, `src/ModelRows.cpp:28,48`, `src/ModelRows.h`, `src/AgentContext.h:163` | **rename never** (question 3). It is a protocol name on the wire *and* a QSettings key (`configured.roles.switchboard`, `models/role/switchboard`) in every existing profile; renaming needs a settings migration like `migrateFastRoleSettings()` for no user-visible gain — the picker already shows "Helper agent". |
| agent-context name `"switchboard"` | `backend/relay_core/agent_context.py:33,145`, `src/AgentContext.h:154` | **rename never.** Same wire-name argument; `_FILE_BRIEFS["switchboard"] → board_chat_brief.md` is keyed on it. |
| `RELAY_GLOBAL_SWITCHBOARD` + `$XDG_CONFIG_HOME/relay/switchboard` | `backend/relay_core/aliases.py:446,450`, `agent_context.py:159-160`, used by 5 test files | **rename later, on its own card.** It is a real directory on users' disks; moving it needs a fallback read of the old path. Out of scope here; the *label* "Switchboard HQ" changes to "Board HQ" with the rest. |
| pointer markers `<!-- relay:switchboard-policy start/end -->` | `backend/relay_core/board.py:2094-2095`, present in this repo's `CLAUDE.md:291,305` and `AGENTS.md:10,24` and in every scaffolded project | **rename never.** `_with_pointer()` finds an existing block by these exact bytes; changing them orphans the old block in every project that has one, leaving two. |
| card label `switchboard` | `board_cleanup_brief.md:41`, this card's own front matter, ~40 existing cards | **rename never.** It is data in cards that are never edited; a new `board` label would split the history in two. |
| `dotswitchboard` (palette alias) | `src/RelayWindow.h:3505` | **rename now** — it is search text, and it should gain `dotboard`. |
| `Switchboarding` (the verb in the busy strip) | `src/BoardPane.cpp:2938-2946` | **owner's own word**; see question 2. |

**3. The folder.**

The definition lives in exactly two places and one test keeps them honest:
`backend/relay_core/board.py:1080-1088` (`HIDDEN_BOARD_FOLDER = ".switchboard"`,
`VISIBLE_BOARD_FOLDER = "switchboard"`, `LEGACY_BOARD_FOLDER = "issues"`,
`BOARD_FOLDERS = (HIDDEN, VISIBLE, LEGACY)`, `DEFAULT_BOARD_FOLDER = HIDDEN_BOARD_FOLDER`) and
`src/Projects.cpp:112-124` (`boardFolders()`, `newBoardFolder(bool)`), with
`tests/projects_test.cpp:247-277` parsing `board.py` for the tuple and comparing it element by
element against `boardFolders()`.

Everything that walks them: `board.board_folder()` (`board.py:1102-1116`),
`board_tools.py:56-59,1155,1190-1193` (`named_board_root`, the create path),
`project_probe.py:735-738`, `aliases.py:456-470` (`local_root`),
`test_history.py:197-209` and `signals.py:322-327` (`<board>/.private/…`),
`relay-board.py:27-35,247`, `scripts/relay-remote-tests:57`
(`for d in .switchboard switchboard issues; do …`), `src/Projects.cpp:53-60,190`,
`src/BoardWorkspace.cpp:59` and `BoardWorkspace.h:6,26-28` (the walk up to `/`, every spelling at
each level before going up), `src/BoardModel.cpp:1020-1024` (stripping the folder from a card path).

The rename action, protocol 19.17 (`docs/AGENT-SESSIONS-PROTOCOL.md:3644-3695`):
`board_folder {hidden}` → `board_folder_changed`, handled at
`backend/relay_core/board_protocol.py:917-950` and implemented by
`board.rename_board_folder()` (`board.py:1989-2060`) — `git mv` in a checkout, plain rename
otherwise; it rewrites the project's `.gitattributes` line, refuses an `issues/` board, refuses
when the target exists, and refuses when a card under the folder has uncommitted text. The GUI
half is `src/BoardSections.cpp:543-546,598-620`: the button reads "Show this board's folder" /
"Hide this board's folder" and compares `m_folder` against the literals `".switchboard"` and
`"switchboard"`.

Creation: `board.scaffold_files()` / `scaffold()` (`board.py:2533-2586`) writes
`<board>/.gitignore` (`GITIGNORE_TEXT`, `.private/`), `<board>/threads/.gitkeep`, the project's
`.gitattributes` line `<folder>/threads/*.md merge=union` (`gitattributes_line()`, `board.py:1117-1121`),
then `POLICY.md` and the pointer files. `board_init` is `board_protocol.py`'s `_init` path; the GUI
side is `src/ProjectInit.cpp:37` ("The folder a yes would create: `.switchboard`, or `switchboard`
when the 'Hidden Switchboard folder' option is off"). `pointer_text()` (`board.py:2411-2428`)
interpolates `board.root.name` into the `CLAUDE.md`/`AGENTS.md` block four times, and
`policy_text()` writes `POLICY.md` with the same substitution (`board.py:2148`, `2537`).
`<board>/.private/` is *inside* the board, so it moves with it and needs no separate work;
this repo's `issues/.private/` is never converted because `issues/` is never converted.

*The migration.*

- New boards go in `board/`. `BOARD_FOLDERS` becomes `("board", ".switchboard", "switchboard", "issues")`
  and `DEFAULT_BOARD_FOLDER = "board"`; `boardFolders()` matches, and `projects_test.cpp` is updated
  to the new four-element list (it re-derives the expectation from `board.py`, so only the first
  `QCOMPARE` is hand-written).
- Every existing board keeps working untouched: `.switchboard/`, `switchboard/` and `issues/` stay
  in the lookup list for ever and nothing migrates by itself — the rule that has held since
  2026-09-19. **This repository's own `issues/` is never converted**, and `rename_board_folder()`'s
  existing refusal for `issues/` stays exactly as it is.
- The explicit action becomes **"Move this board to `board/`"**: one button on the section editor's
  folder row, shown when `m_folder` is `.switchboard` or `switchboard`, hidden for `board` and
  `issues`. `board_folder {hidden}` becomes `board_folder {folder: "board"}` (accept the old
  `{hidden}` shape for one release, mapping true→`.switchboard`, false→`switchboard`, so a paired
  phone on an older build is not broken). The refusals and the `.gitattributes` rewrite are unchanged.
- The "Hidden Switchboard folder" option (`src/Projects.h:70-77`, `RelayWindow.h:3502`,
  `board/hidden_folder`, default on) has no meaning once the default is a visible `board/`.
  See question 1.

*The trade-off, stated plainly.* `docs/SWITCHBOARD-FORMAT.md:7-25` is explicit: the folder was
hidden on 2026-09-19 *"so the cards do not clutter the project's root listing"* and because
*"an agent working the codebase should not turn up a card on every unrelated search"* — ripgrep and
friends skip dotted directories, so a bare `rg` over a project misses the board. `board/` is
visible, so that reverses: **agents will see cards in ordinary code searches again.** On a board
with a few hundred cards a search for a common word can return more card text than code. What
improves is the opposite failure, which the same paragraph admits: an agent reaching for the cards
with a bare `rg` currently finds nothing and concludes there is no board.

*Mitigation — recommendation: no ignore file; teach the exclusion where agents already read.*
A `.gitattributes` attribute cannot do this (`linguist-*` affects GitHub's diff view, not any
search tool). The one thing that would work is a `.ignore` file — `board/.ignore` containing `*`,
which ripgrep, `fd` and `ag` honour and git does not — but it re-creates the discoverability bug it
just fixed, one level deeper and less well known than `--hidden`. Instead, add one sentence to the
two places every agent already reads before touching the project: the generated pointer block
(`board.py:pointer_text`, so it lands in `CLAUDE.md`/`AGENTS.md` of every project) and the top of
`POLICY.md` — *"Cards live in `board/`. Read them with the board tools or `board/BOARD.md`; exclude
them from code searches with `rg -g '!board/'`."* That is discoverable, costs nothing, and is
reversible. If the noise turns out to matter in practice, `board/.ignore` is a one-line follow-up
card; it is not worth shipping speculatively.

**4. Docs.**

*Renamed files (recommended), with one-line redirect stubs at the old names:*

| Old | New | Why |
|---|---|---|
| `docs/SWITCHBOARD-DESIGN.md` | `docs/BOARD-DESIGN.md` | 83 inbound references, 21 of them in live files |
| `docs/SWITCHBOARD-FORMAT.md` | `docs/BOARD-FORMAT.md` | 803 inbound references, 37 in live files — the single most-cited doc in the repo |
| `docs/SWITCHBOARD-TOOLING-RESEARCH.md` | `docs/BOARD-TOOLING-RESEARCH.md` | 14 inbound, 11 live |

*Not renamed:* `docs/SWITCHBOARD-AESTHETIC.md`. It is the document about the *switchboard
aesthetic* — bakelite, brass, jacks, cords, the lamp (`:1`, `:15-25`) — which the owner explicitly
kept. Its 29 live references include `data/theme/themes/*.toml`, `src/Theme.cpp`,
`src/ThemeFile.cpp` and `tests/theme_test.cpp`, all of which are talking about the aesthetic and
not the product noun. Renaming it would be the one edit that loses information.

*The stubs are worth it.* The three files are cited 900 times, and ~840 of those citations are in
text this plan must never edit: `docs/qa_evidence/`, `issues/threads/`, done cards, and commit
messages. A one-line stub (`# Moved → [BOARD-FORMAT.md](BOARD-FORMAT.md)`) keeps every one of
those links resolving, in the editor and on GitHub, for three lines of content. Without stubs the
repository's own history stops navigating.

*Text edits only* (no rename, no new file): `docs/AGENT-SESSIONS-PROTOCOL.md` (129 hits —
§19 throughout, §19.17 the folder action, §19.18 "The Switchboard is a context", §30.7 the helper
worker, §31 the tests hub, §3895/§7688 the global HQ paths), `docs/ARCHITECTURE.md` (51, incl.
`:1556` the folder sentence and `:3524` the `BoardWorkspace` row), `docs/TASKS-AND-MEMORY-DESIGN.md`
(25), `docs/PROJECT-INIT-AND-IMPORT.md` (24), `docs/THEMES.md` (13 — §6 "The Switchboard
materials"; keep the *aesthetic* references, change the product noun), `docs/VALIDATION.md` (13),
`docs/REMOTE-PROTOCOL.md` (7, §17), `docs/GITHUB-SYNC.md` (7), `docs/README.md` (7 — the index
rows, including the three renamed filenames), `README.md` (7, incl. the `### Switchboard
(Ctrl+Shift+S)` heading and `/switchboard`), `docs/SIGNALS-RESEARCH.md` (4),
`docs/MEMORY-AND-MULTI-REQUEST-RESEARCH.md` (5), `docs/PROFILING.md` (2),
`docs/GUEST-TOOLS.md` (1), `docs/research/qa-across-fields/*.md` (7 across three files),
`CONTRIBUTING.md`, `WARP.md`, `CLAUDE.md`, `AGENTS.md`, `site/index.html`,
`packaging/org.relayterminal.Relay.metainfo.xml`. `docs/QA-ACROSS-FIELDS-RESEARCH.md` has no hits.

*Never edited:* `docs/qa_evidence/**` (including `2026-09-20-switchboard-page-agent/`,
`2026-09-19-switchboard-materials/` and every screenshot filename), `issues/threads/**`, cards in
`done/` and in the QA lanes, `issues/BOARD.md` (regenerate it, do not hand-edit), and every commit
message. The word stands where it was written.

**5. Memory and skills — for the main session, not for this card.**
`~/.claude/projects/-home-elliott-repos-relay-terminal/memory/` mentions it in nine files:
`board-not-switchboard.md` (7 — already written today and already correct),
`switchboard-tooling-hub.md` (7 — the file name itself says Switchboard),
`project-attachment-model.md` (7), `MEMORY.md` (3),
`agents-separable-from-context.md` (3), `deliver-workflow-and-claims.md` (3),
`human-qa-is-a-staged-scenario.md` (2), `panes-not-overlays.md` (1),
`phone-remote-control.md` (1). `~/.warp/skills` has **no** mentions (checked all 20+ skills,
including `issue-tracking`), and neither does the rest of `~/.warp`. Nothing here is this card's
to edit.

### Steps

Six areas with disjoint file sets, landed in this order. **Nothing starts until #PR4Q, #WC3E and
#JNYN have landed** — #WC3E holds `backend/relay_core/board.py`, `board_tools.py`,
`board_protocol.py`, `board_policy.md`, `board_plan_brief.md`, `skills_bundled/deliver/SKILL.md`,
`docs/AGENT-SESSIONS-PROTOCOL.md`, `docs/SWITCHBOARD-FORMAT.md`, `issues/POLICY.md`,
`src/BoardModel.cpp`, `src/BoardPane.cpp`, `tests/test_board.py`, `tests/test_board_protocol.py`
and `tests/test_board_tools.py`; #PR4Q holds `backend/relay_core/test_history.py` and
`tests/test_test_history.py`; #JNYN adds `backend/relay_core/tryit_protocol.py` and touches
`BoardPane.cpp`'s CardDetail. **Every one of those is a file this rename touches.** Each area
claims its paths with `land.py begin` *at the moment it starts editing*, dry-runs before every
commit, and runs only its own tests.

1. **Area A — the folder, in the backend.** `backend/relay_core/board.py` (the four constants,
   `BOARD_FOLDERS`, `new_board_folder`, `board_folder`, `gitattributes_line`,
   `rename_board_folder`, `CONFIG_TEXT`, `GITIGNORE_TEXT`, `pointer_text`, `policy_text`,
   `scaffold_files`), `board_tools.py` (`named_board_root`, the create path, the lookup docstrings),
   `board_protocol.py` (`_folder`, the `configure` `folder` field, `NO_BOARD_CHAT_ERROR`),
   `project_probe.py`, `aliases.py:456-470`, `test_history.py:197-209`, `signals.py:322-327`,
   `scripts/relay-board.py`, `scripts/relay-remote-tests:57`.
   Adds the pointer/POLICY sentence about `rg -g '!board/'`.
   *Must test:* `tests/test_board.py`, `tests/test_board_tools.py`, `tests/test_board_protocol.py`,
   `tests/test_project_probe.py`, `tests/test_aliases.py`, `tests/test_test_history.py`,
   `tests/test_signal_threads.py`.
   *Breaks and must be fixed in the same commit:* `test_board.py:988-1197` (the whole
   `BOARD_FOLDERS` / scaffold / `rename_board_folder` / `policy_text` block — 34 assertions),
   `test_board_tools.py:2448-2504`, `test_board_protocol.py` (31), `test_project_probe.py` (3).
2. **Area B1 — the folder, in the GUI.** `src/Projects.h`, `src/Projects.cpp`,
   `src/BoardWorkspace.h`, `src/BoardWorkspace.cpp`, `src/BoardSections.h`, `src/BoardSections.cpp`
   (the folder row, the new "Move this board to `board/`" button), `src/ProjectInit.h`,
   `src/ProjectInit.cpp`, `src/BoardModel.cpp:1020-1024`, and the `board/hidden_folder` option's
   fate per question 1.
   *Must test:* `ctest -R 'projects|boardworkspace|boardsections|projectinit|boardmodel'`.
   *Breaks:* `tests/projects_test.cpp:247-300` (the pinned order and `boardDirOf`),
   `tests/boardworkspace_test.cpp:80`, `tests/boardmodel_test.cpp:640`,
   `tests/projectinit_test.cpp` (the folder the question names, `:115`, `:375`),
   `tests/boardsections_test.cpp`.
3. **Area B2 — the product word in the GUI.** `src/BoardPane.cpp`, `src/BoardPane.h`,
   `src/BoardWorker.cpp`, `src/BoardRemote.cpp`, `src/PaneStatus.cpp`, `src/PaneStatus.h`,
   `src/Pane.h`, `src/RelayWindow.h`, `src/Keymap.h`, `src/ProjectsPane.cpp`,
   `src/ProjectPicker.cpp`, `src/ProjectPicker.h`, `src/ProjectInitBlock.h`,
   `src/GlobalsPane.cpp`, `src/AppCommands.cpp`, `src/ModelRows.cpp`, `src/ModelRows.h`,
   `src/AgentContext.h`, `src/main.cpp`, and the comment-only files (`Theme.cpp`, `ThemeFile.cpp`,
   `PaneChrome.h`, `PaneLayout.h`, `WindowChrome.h`, `OutputLinks.h`, …) — but **not**
   `Glyph::Switchboard` and **not** `kReasonSwitchboard`, and **not** any
   `docs/SWITCHBOARD-AESTHETIC.md` reference.
   *Must test:* `ctest -R 'panestatus|boardpane|boardfilter|agentcontext|settingspane|projectpicker|conversations|modelrows|appcommands|panelayout|windowstate|outputlinks|consolemode|editor|signals|boardwatch|boardremote'`,
   plus `scripts/relay-build --check "Board: cards, threads and plans"`.
   *Breaks:* `panestatus_test.cpp:379`, `settingspane_test.cpp:855-858` (it greps `RelayWindow.h`
   for the literal `headingRow(QStringLiteral("Switchboard"))`), `agentcontext_test.cpp:219-222`,
   `boardfilter_test.cpp:185,201`, `projectpicker_test.cpp:81`, `boardmodel_test.cpp` (7),
   `conversations_test.cpp`, `modelrows_test.cpp`.
4. **Area C — worker text, briefs and the client.** `backend/relay_core/board_chat_brief.md`,
   `board_cleanup_brief.md`, `board_plan_brief.md`, `board_discuss_brief.md`, `board_policy.md`,
   `skills_bundled/deliver/SKILL.md`, `agent_context.py`, `roles.py:123` (the role *description*
   only), `guest_instructions.py:20`, `instructions.py:201`, `worker.py:440`,
   `app/board.js`, `app/app.js`, `app/index.html`, `app/board.css`, `app/boardmd.js`,
   `app/sw.js`, `app/outbox.js`, `app/notifykinds.js`, `remote/host.py`, `remote/wire.py`,
   `remote/board_state.py`, `remote/notify.py`, `remote/gui_host.py`, `scripts/eval-requests.py`.
   *Must test:* `tests/test_board_chat.py`, `tests/test_agent_context.py`, `tests/test_roles.py`,
   `tests/test_system_prompt.py`, `tests/test_skills.py`, `tests/test_board_view.py`,
   `tests/test_remote_board.py`, `tests/test_remote_wire.py`, `tests/test_prompt_profiles.py`,
   `tests/test_guest_harness_provider.py`, `tests/test_queue.py`.
   *Breaks:* `test_board_chat.py:193,389,478-479,530`, `test_agent_context.py:17,55`,
   `test_board_view.py:250-255`, `test_system_prompt.py` (4), `test_roles.py` (2),
   `test_prompt_profiles.py` (2), `test_guest_harness_provider.py` (2).
5. **Area D — docs and site prose.** Every file in Findings §4 under "Text edits only", plus
   `data/theme/themes/*.toml` comments and `engine/` comments. Keeps every
   `SWITCHBOARD-AESTHETIC.md` reference and every aesthetic word (bakelite, brass, jack, cord,
   patch panel, "switchboard metal"). Then `python3 scripts/relay-board.py policy` and
   `… index` to regenerate `issues/POLICY.md` and `issues/BOARD.md` from Area A's sources.
   *Must test:* `python3 scripts/relay-board.py check`, and re-read `docs/README.md`'s table.
6. **Area E — the three doc renames.** Last, because it is a mechanical path rewrite across files
   every other area has already landed. `git mv` the three files, write the three one-line stubs,
   update `docs/README.md`'s rows, and substitute the paths in the ~69 live inbound references
   (`docs/*.md`, `backend/relay_core/board.py`, `board_tools.py`, `app/boardmd.js`,
   `src/BoardPane.h`, `src/BoardModel.cpp`, `src/Pane.h`, `src/RelayWindow.h`,
   `src/ProjectInit.cpp`, `src/TestSuitesModel.h`, `src/ProfilePane.h`, `scripts/relay-profile`).
   *Must test:* `scripts/relay-build` (comments only, but the tree must still compile), and a
   `rg 'SWITCHBOARD-(DESIGN|FORMAT|TOOLING)' -g '!issues/**' -g '!docs/qa_evidence/**'` that
   returns only the three stubs.

### Questions for the owner

1. **`board/` or `.board/`, and does the "Hidden Switchboard folder" option survive?** Your words
   were `/board`, so the plan is a visible `board/`. That reverses the 2026-09-19 decision on
   purpose: `docs/SWITCHBOARD-FORMAT.md:19-25` hid the folder so a bare `rg` over a project would
   not return a card on every code search, and with `board/` it will again.
   **Recommendation:** ship visible `board/`, drop the "Hidden Switchboard folder" toggle (it has
   no meaning once the default is visible), lookup order `board`, `.switchboard`, `switchboard`,
   `issues`, and add one line to the generated `CLAUDE.md`/`AGENTS.md` pointer and to `POLICY.md`
   teaching `rg -g '!board/'`. No `.ignore` file — it would re-create the discoverability problem
   one level deeper. If you would rather keep the toggle, the hidden spelling becomes `.board/` and
   the lookup list grows to five.
2. **Does the helper panel header say "Board agent", and does "Switchboarding" survive?**
   `BoardPane.cpp:3668` says "Switchboard agent" and `:2945` says "✦ Switchboarding · planning…",
   which is your own coinage from 2026-09-19. **Recommendation:** "Board agent" for the header and
   the placeholder ("Ask the Board agent — Enter sends, a second prompt queues"), and **keep
   "Switchboarding"** — it is the aesthetic, it is a verb and not a noun, and there is no good
   replacement ("Boarding" reads as an aeroplane).
3. **Is the `switchboard` model role id renamed?** **Recommendation: no.** It is a protocol name
   on the wire and a QSettings key in every existing profile (`configured.roles.switchboard`), so
   renaming needs a settings migration, and the picker already shows "Helper agent" after #MDL1 —
   the user never sees the id. Only `roles.py:123`'s description changes. Same answer for the
   agent-context name `"switchboard"` and for `RELAY_GLOBAL_SWITCHBOARD` (a directory on disk;
   its own card if you want it).
4. **Are the doc files renamed?** **Recommendation: yes for three, no for the fourth.**
   `SWITCHBOARD-DESIGN.md` → `BOARD-DESIGN.md`, `SWITCHBOARD-FORMAT.md` → `BOARD-FORMAT.md`,
   `SWITCHBOARD-TOOLING-RESEARCH.md` → `BOARD-TOOLING-RESEARCH.md`, each with a one-line redirect
   stub at the old path because ~840 of their 900 citations are in evidence, threads and commit
   messages this plan must not touch. **`SWITCHBOARD-AESTHETIC.md` keeps its name**: it is the
   document about the switchboard aesthetic, which you are keeping, and it is what the theme files
   and `src/Theme.cpp` cite.
