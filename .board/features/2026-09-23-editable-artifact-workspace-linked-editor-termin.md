---
id: P2W8
type: work
status: planned
labels: [feature, panes, files, agent-ui, switchboard, plugins]
assignee: agent
implemented_by: anthropic/claude-fable-5-1 via claude-code
blocked_by: [2FQ9, R660, E85D, F8R7, C0Q8]
rank: zzzzzzzzzzzzzzzzy
created: '2026-09-23'
verify: {artifact: code, primary: person, also: [script], human: required, criteria: 'every slice card (#Y2BA #S976 #E8V1 #6FDD #2FQ9 #83YV #PBZ4 #R660 #3B1B #6BY7) reaches needs-verification with its own evidence and the pane model still reads as one agent class across console/artifact/system panes; the owner closes the umbrella when the last slice closes', sign_off: none, effort: high, stakes: rework, blast: capability}
source: Owner in a Relay pane, 2026-09-23; research in reports/Editable workspaces for Relay.md (commit d1249048)
links: {plans: [], commits: [], evidence: [], related: [MDA7, 2GV0], github: null}
---
# Editable artifact workspace: linked editor, terminal/agent and preview panes, with human and agent editing the same file

## Issue
do you know the canvas feature that chatgpt had? does the chatgpt app, claude app, other harnesses, have that for local work? also research overleaf. i want to set up relay that i can have an editable document in one pane and a linked terminal pane next to it, i type requests in the paen and it edits the document, and i can also edit manually at the same time. 

are there any AI co-work systems lke this in a terminal? 

another interesting modality would be 3 panes with the terminal, TEX, and PDF. it could be 1:1:1 or 2:1 with the TEX and terminal on the left, with terminal beneath it. 

do deep research on related features to see what we can build for relay. 

i guess this works for working on scripts as well. artwork / CAD as well. err on the side of broadening this feature.

## Discussion points
Research is in `reports/Editable workspaces for Relay.md` (commit d1249048) with the per-topic notes under `research_notes/Editable workspaces for Relay/`. Short version: ChatGPT's side-by-side Canvas is retired for current models; Claude Docs is the closest to "keep typing while the agent edits"; neither links the document to a local file. Overleaf is the reference for source + compile + PDF + SyncTeX. Terminal agents (Codex, Claude Code, Aider, Zed, Cursor) edit project files but expose uneven change records and no shared editor buffer.

**What Relay has today** (`docs/ARCHITECTURE.md` section 10): `relay::FilePreview` edits local text and Markdown (atomic save through `QSaveFile`, `●` for unsaved), edits SSH-hosted text with an Overwrite/Reload choice, renders Markdown, images and PDF (`QPdfView`, only when Qt PDF is built in — the `.deb` and AUR packages leave it off). `RelayWindow::openPath` places a preview beside the anchor pane and reuses one already open; tool panes split, close and restore like terminal panes and the layout is saved. `relay open PATH`, the explorer and clickable `file:line` output all land in the same preview.

**What is missing, in dependency order.**
1. *Shared-file safety.* `FilePanes` has no `QFileSystemWatcher`; a local save does not compare the disk file with the revision it loaded; the agent's `edit_file`/`write_file` and a shell's `sed -i` write the disk file while the buffer may be dirty, so someone loses edits. Needed: watch the file, three-way merge an external change into a dirty buffer (base = loaded revision), revision-aware save, and apply the agent's own edits to the *open buffer* as revision-aware patches (cursor and undo preserved, each patch a marked undo step) rather than through the disk. This is the foundation for "I type requests in one pane and edit manually at the same time".
2. *Linked pane group.* A workspace id shared by an editor pane, a console (terminal/agent) pane and a preview pane, so they open, close, restore and move together, `edit_file` on the open file lands in that editor, and the two requested layouts exist as named presets: **1:1:1** (terminal | TEX | PDF) and **2:1** (TEX above terminal on the left, PDF on the right).
3. *Artifact runner.* source → command → output with debounce on save, incremental rebuild, a diagnostics parser into clickable `file:line` rows (`OutputLinks` already resolves those), the preview reloading in place and showing **live / building / stale / failed** plus the source revision it was built from. TeX first (latexmk), then Typst and Quarto/pandoc as the same runner with a different command.
4. *SyncTeX both ways.* Editor line → PDF page/position through `synctex view`, PDF click → editor line through `synctex edit`; `QPdfView`'s page navigator can jump to a page and location.
5. *Typed previews beyond PDF.* Script → plot/table/console output (with #MDA7's inline images and tables), diagram source → SVG, artwork file → canvas, CAD source → rendered view with an explicit Regenerate step. Each preview declares what it renders and which representation is authoritative.
6. *Remote.* For a project on an SSH host, run the build beside the source over the pane's own connection and fetch the PDF, SyncTeX map and log as one generation (the `RemoteFile` path already exists for text).

The per-task packaging of 2–5 (TeX workspace, Python/Stata kernel IDE, and the others) is scoped in the design card on task plugins; this card owns 1 and the pane-group/layout work everything else sits on.

### Decisions to settle
1. Agent edits: applied live into the open buffer as marked undo steps (Claude Docs style), or shown as a patch to accept first (Cursor/Zed style)? Recommendation: live by default, with a per-workspace "review before apply" toggle.
2. When the agent and the user change the same lines: three-way merge with the conflict shown inline under a chip (recommended), or last writer wins?
3. Qt PDF in the `.deb` and AUR packages? It is off today, so those builds have no in-Relay PDF pane.
4. The console pane in a group: a full terminal pane (recommended, so `latexmk`, `git` and the REPLs work there) or an agent console without a shell?
5. Start with phase 1 (shared-file safety) before any TeX or kernel work? Recommended: yes, everything else loses edits without it.

### Cards as artifact panes (folded in from #C7RW, owner 2026-09-25)

Owner: *"replace the switchboard split view (list and cards) with card panes. it would often be convenient to have multiple card threads open. a design risk to discuss, if that just becomes the same as a regular pane. and we should instead have cards attachable to panes and more of the card features integrated into standard panes."* and *"a card can be a linked artifact with a pane, similar to the feature where we have a linked canvas document."*

**Where the limit is today.** One Board pane per tab (`RelayWindow::openBoardCard` finds "the one the tab already has"), one `CardDetail` per `BoardView`, so one open card page per tab. The backend already runs one conversation and one `TurnSupervisor` per open card (#DR4K, #CTRN, key `<tab>/card:<ID>`), so several card threads at once is a GUI limit only. #Y2BA asks for the same thing.

**A terminal pane is already card-aware**: the header chip (#C7PF/#0FBB) names the turn's card and every claimed card, the `#` picker attaches cards to a prompt, Run/Verify hand a card to a pane (`startBoardTask`), `board_claim` writes the pane token into the card. The chip's click only opens the Board.

**Options weighed.** (A) Card panes: lift `CardDetail` + `CardContext` into a `ToolPane::Kind::Card`, one leaf per open card; Board becomes the list. (A0) Cheapest form: allow N Board panes per tab, the second opened solo on a card with the list hidden for good. (B) Card features into the terminal pane: a drawer with body/Plan/Tasks/stage and Plan/Verify/Done actions. B cannot replace the card page: #CTRN made the thread the settled record written by the worker with provenance, and piping a terminal pane's turns into it would make the thread a transcript. (C) Card as a linked artifact of a pane: the #E85D group model with roles `card`/`runner`, which Run already creates implicitly and then forgets. (D) tabs inside one Board pane and (E) `o` opens the card file: rejected, no side-by-side / loses the structure.

**The "same as a regular pane" risk, answered.** Since #AGNT a card page's console *is* a `Pane` (no shell, `CardContext`), so a card pane is a console by construction. What must stay different and be legible in the chrome: no pty (Run opens a shell pane beside it); role `switchboard`, console tool scope, Plan writes only `## Plan`; the record is the thread with `model=`/`turn=` provenance; a card pane is *about* the card, a shell pane *claims* it. The owner's second message resolves this into the general design below: a card is one kind of **artifact pane**, and "pop out" is the linked shell pane.

### One pane model: consoles, artifacts, system panes (owner, 2026-09-25)

Owner: *"there are no longer terminal agents vs helper agents. there are just agents. a new pane is a shell pane, it starts with an agent that's attached to the shell. we could even replace the name 'pane' with 'shell'. so ctrl+E is new shell. a shell pane is the main version of a console pane. but you could have others, eg ipython ... it auto-detects python commands. it could also have special tools like compile / run / etc. there would be a plug-in spec defining that. i would want to prioritize building a python console and a stata console. we can build python first. then there are artifact panes ... where the content is an editable object you are interacting with, rather than a console. a central one would be cards ... but this is also more general, like scripts, jupyter notebooks, documents, charts, artwork, etc. in an artifact pane, you have the agent system prompt docked at the bottom, same as a console pane. it would have special tools in it, for example the planner we currently have for cards. it could also have commands that can be slashed or auto-detected ... further, you can pop out the artifact agent and it goes into a side-by-side separate connected shell pane. so that agent is working on the artifact but has access to the shell as well."* and *"the third type of pane is system pane or options pane. that's like our board or models panes. you have an agent docked there as well, same as with artifacts, but that agent helps you manage the options."*

#### The model

Every pane is **content + one docked agent**. The agent is one class everywhere (#AGNT: one composer, queue, transcript, model box, worker connection); what varies is its `relay::agent::Context`. The three kinds differ in what the content is and what the **record** of the agent's work is:

| Kind | Content | Composer routes typed text to | Agent's extra tools | Record of a turn |
|---|---|---|---|---|
| **Console** (Shell, Python, Stata, …) | a running program in a pty | the program when it parses as that language, else the agent | the plugin's runtime tools (`py_run_cell`, `stata_run`) | the pane's conversation + the program's blocks |
| **Artifact** (card, script, notebook, .tex, chart, artwork) | an editable object | the agent (slash commands are the object's actions) | the plugin's object tools (`tex_build`; the card planner) | the object: a card's thread with `model=`/`turn=` provenance, a file's revision with the agent's patches as undo steps (#F8R7) |
| **System** (Board list, Models, Options, Actions, Sessions, Sharing, Tests) | a Relay surface | the agent | the surface's app tools (`option:`, `session:`, board tools) | the tab's one helper conversation |

The Board today straddles two kinds: its **list** is a system pane and an **open card** is an artifact pane. That is the clean answer to the card-panes question above: split them.

#### What is already true in code (explorers, 2026-09-25, read-only)

- **"Just agents" is done at the architecture level.** `TerminalContext` (`src/Pane.h:595`) and the console contexts are the same `Pane`; scopes are `pane` and `console` only (`agent_context.py:49`), both get the executor, console scope merely keeps the app/session/test groups un-deferred (`agent.py:1125`, `:1465`). What remains is **labels**: "Helper Agent (Alt+Q)", the role name `switchboard`, "terminal agent" in docs.
- **System panes exist as described**: Board list (`BoardContext`), Options/Actions (`OptionsContext`), Sessions (`SessionsContext`), Models (`ModelsContext`) each embed a console through `wireConsoleHost` (`src/RelayWindow.h:1180`, `:1331`, `:1457`, `:5419`). No console yet on Explorer, Preview, Plan, Diff, Sharing, Internals, TestSuites, Profile, Review, Info (`src/PaneChrome.h:406`).
- **Artifact panes exist without the docked agent.** `FilePreview` edits text/Markdown/SSH text with revision-checked `QSaveFile` saves (`src/FilePanes.cpp:1830-1884`), `PlanEditor`, `DocxEditor`, `DiffView`; none has a Context. The **watcher, conflict merge and agent patch-into-open-buffer path of #F8R7 is largely present** (`src/FilePanes.cpp:1166-1175`, `:2331-2485`, `src/PaneEvents.cpp:145`). `ArtifactWorkspace` already models a persisted group id, plugin kind, roles Editor/Console/Preview/Variables, `1:1:1`/`2:1` presets and output generations (`src/ArtifactWorkspace.h:37-65`, `:104-142`, `src/RelayWindowWorkspace.cpp`); its Console role is a full shell `Pane`.
- **A card page is nearly an artifact pane.** `CardContext` (`src/BoardPane.cpp:4589`) is a Context with actions Plan/Refine/Run/Run in pane/Verify (`:3314-3445`); `CardDetail` (`:1830`) needs a controller for what it borrows from `BoardView` (model lookups, selection follow, worker requests, hash-checked `board_update`, `:5049-5084`, `:8689-8875`).
- **Plugin manifest v1 is read and validated** (`task_plugins.py:97`, `:417`: activation, router, runner, tools, skills, panes, preview, requires; project packages need explicit enable, `:837`). The **Python kernel runtime exists** (`workspace_plugins.py:136`, `py_kernel.py:835`, jupyter_client or subprocess fallback) and `py_*` tools are offered lazily when a workspace is active (`agent.py:1531`, guest bridge `:160`). **Stata** has a manifest and classifier only (`workspace_plugins.py:490`).
- **Routing is wired in the worker, not the GUI.** `route` goes through `WorkspaceManager.route` → `lang_router.detect_repl`/`classify_line` (`worker.py:254`, `lang_router.py:604`, `:644`) and can answer `program`/`incomplete`; the GUI sends no `foreground_program` (`src/PaneRuntime.cpp:1203`) and `dispatch` has no `program` or `incomplete` branch (`:1253`). The pty always starts Bash (`src/PaneRuntime.cpp:880-956`); `LineTarget::Program` covers canonical-mode readers only (`src/InputPolicy.cpp:14-64`). Completion is shell-only (`src/Completion.cpp:78`); OSC 133 blocks come from the Bash integration only (`shell/relay-integration.bash:19`). The manifest has **no slash-command field** and a Context **cannot add slash commands** (`src/AgentContext.h:261-322`, `/` menu at `src/PaneRuntime.cpp:470-525`).
- **The docked console shares the tab's worker and never starts a pty** (`sharesWorker()`, `src/Pane.h:10348`); a shell pane starts its own worker, persist scope `pane` keyed by `scrollbackId()` (`:600`), sessions under `relay/sessions/<ws>/`; a console persists under scope `helper`, key `<tab>/card:<ID>`, a deterministic session id under `relay/helper-sessions/` (`agent_context.py:94`, `worker.py:343`). The agent's own shell commands run in the worker's executor, **not** in the pane's pty — so "shell access for the agent" and "a pty for the human" are separable.

#### Design proposals

**1. Names.** Keep **pane** for the rectangle; every product surveyed (Warp, iTerm2, tmux, Positron, Zed) uses *shell* for the program and *pane* for the region, and "new shell" would mislead once a pane can be a Python console or a card. So: the Ctrl+E action is labelled **New shell**, the palette also offers **New Python console** / **New Stata console** / **New card**, and the console kind shows in the header (`Shell`, `Python · ipython`). Drop "helper" and "terminal agent" from every label; the row is just the agent.

**2. A console kind is a plugin.** `relay.shell` becomes a bundled manifest like `relay.python` (runner: Bash + integration script; router: bash; completion: shell; blocks: OSC 133). That makes the contract honest (#C0Q8: "the second implementation should show whether the contract is reusable") and a console pane is then *pty running the plugin's program + the plugin's router, completion, block marks, tools and slash commands*. Manifest v2 adds `console.program` (what the pty runs), `completion` (a provider or static table), `blocks` (how statement boundaries are marked) and `commands` (slash actions).

**3. Python console: IPython in the pty, attached to Relay's kernel.** The owner wants it to "look like a shell pane". The kernel runtime already exists but is separate from any pty, so `py_run_cell` would not share state with a plain `ipython` the user types into. Recommendation: `KernelRuntime` starts the kernel; the pane's pty runs `jupyter console --existing <connection_file>` (the JupyterLab "code console attached to a kernel" pattern, and Positron's "one active session scopes Variables and the assistant"); the agent's `py_*` tools hit the same kernel over `jupyter_client`. Human and agent share one namespace by construction. Fallback with no `jupyter_console`: plain `ipython` in the pty and tools that type into it. Statement blocks: an IPython `Prompts` subclass in Relay's startup config emits the OSC 133 marks. Stata second, same shape (`stata -q` in the pty; tools through the validated bridge, #33G0).

**4. Routing is one rule for every console.** The composer's destinations are *program* and *agent*; for the Shell console, program = the shell. The GUI half of #S976/#33G0 is what is missing: send `foreground_program`, act on `program`/`incomplete`, a `Program` destination with its own ink and chip text, per-plugin completion. Three plugin surfaces stay separate (the research memo's strongest point): **auto-detected input**, **slash actions** (visible, context-gated, may expand to a prompt, a tool call or a program line), and **agent tools** (never inferred from a visible command).

**5. Artifact panes = a Context on the editor.** A generic `ArtifactContext` (path, plugin id, the plugin's actions as the action row, `screen` = cursor/selection/cell/open card, `turnFinished` = refresh) gives `FilePreview`/`PlanEditor` the docked agent that Board pages already have; `ContextSpec` fits it now, a `file`/`plugin` field goes on the wire for structured identity. The agent edits the *buffer* (the #F8R7 path), never the disk behind it. **Cards** are the first artifact: `CardContext` is already the Context, the work is the `CardDetail` controller and a `ToolPane::Kind::Card` (A0 → A above). The card's "planner" is simply its Plan action; a `.tex` artifact's are Build/Diagnostics/Jump; a notebook's are Run cell/Restart.

**6. Pop-out = the same console, promoted to a linked leaf, plus a pty.** Three mechanisms were sized: (a) a new shell pane whose own worker resumes the card conversation by key (8–12 files; loses the card linkage unless a handoff is built, and two workers would contend for one conversation file); (b) the tab's helper worker grows a pty (6–10; one shell for a worker serving every console of the tab); (c) the docked console `Pane` itself is moved out of the artifact frame into the splitter and starts a pty (5–8; keeps worker, key, `surface: card:<ID>` routing and thread provenance intact). **Recommend (c).** The artifact pane and its popped-out console form an `ArtifactWorkspace` group with roles `artifact` and `console`, both headers show the link, "dock back" is the reverse move. This is distinct from **Run**, which stays: Run starts a *fresh* agent on the brief in a new shell pane; pop-out *continues* this agent with a shell. The one asymmetry left is worker topology (a shell pane owns a worker, a console shares the tab's); (c) lives with it and is the experiment that says whether it should go.

**7. System panes.** Built as described; decide which of the console-less kinds get one. Recommendation: Tests and Sharing yes; Explorer, Diff, Turn, Subagent, Info no (they serve another pane's agent).

#### Order

0. Label sweep (agent, New shell, console kind in header). Small.
1. Cards as artifact panes: A0 (N solo Board panes per tab), then `Kind::Card` with the `CardDetail` controller. Unblocks #Y2BA.
2. Pop-out / dock-back for the card console via the workspace group (mechanism c).
3. Python console: the GUI routing half (#S976 steps 1–5), `console.program`, jupyter-console-on-Relay's-kernel, Python completion, prompt marks; `relay.shell` manifest alongside as the proof. Independent of 1–2 (different files), so it can run in parallel, and it is the owner's stated priority.
4. `ArtifactContext` on file panes (TeX, scripts, Markdown), reusing 2's pop-out.
5. Stata console on 3's shape; manifest v2 fields as 3–4 need them.

Related: #C0Q8 (manifest), #33G0 (kernel, routing), #S976 (program destination), #E85D (groups, presets), #F8R7 (buffer safety), #Y2BA, #AGNT, #CTRN. Research memo sources: Warp Agent Mode/Blocks, Positron interpreters and assistant context, JupyterLab code consoles, Zed REPL, VS Code Interactive Window, Claude Docs, Canvas, Overleaf Error Assist, VS Code contribution points, Jupyter kernelspec, Claude Code and Codex plugin manifests.

## Done means
- A file open in an editor pane is watched: an external change (agent `edit_file`, `sed -i`, `git checkout`) merges into a dirty buffer three-way and never overwrites unsaved typing; a save whose disk revision moved offers merge, overwrite or reload instead of silently winning.
- The agent's edits to a file that is open land in that buffer as marked undo steps while the user keeps typing, with a change view listing what each turn changed.
- An editor, a console (terminal/agent) pane and a preview pane can be linked into one group that opens, closes, restores and moves together, with the **1:1:1** and **2:1** layouts as named presets.
- The same works for a file on an SSH host through the pane's own connection.
- Focused tests: watcher and merge cases, save races, layout save and restore; a QA run of the TeX flow once #MEPR's plugin 1 lands.

## Decisions
Owner, 2026-09-25, on the five "Decisions to settle" above, verbatim: *"i want to go big and build the whole thing. for the decisions: 1) agree 2) agree 3) if small/smooth, include; otherwise plugin 4) its a full terminal pane 5) yes, then delegate to subagents. also note and build the case of multiple linked panes -- eg shell -> TEX -> PDF."*

- **D1** Agent edits land live in the open buffer as marked undo steps, with a per-workspace "review before apply" toggle.
- **D2** Same-line conflicts are three-way merged with the conflict shown inline under a chip.
- **D3** Qt PDF ships in the `.deb`/AUR builds if the packaging is small and smooth; otherwise the PDF preview is a plugin-provided adapter with the external-viewer fallback.
- **D4** The console pane of a workspace group is a full terminal pane (shell + agent), never a shell-less console.
- **D5** Phase 1 (shared-file safety) first, then the rest, delegated to subagents.
- **D6** Linked pane chains are a first-class case: a group may have more than two members and a direction (shell → TeX editor → PDF preview), each member can open the next, and the chain restores, closes and moves as one.

The six questions of the unified pane model (thread, 2026-09-25) were not answered separately; "go big and build the whole thing" adopts their recommendations as working defaults, each overturnable by a comment: **U1** keep "pane", label Ctrl+E "New shell", drop "helper" from labels; **U2** the Python console is IPython in the pty attached to Relay's kernel (`jupyter console --existing`), plain `ipython` as fallback; **U3** pop-out promotes the docked console itself to a linked leaf with a pty, sharing the tab's worker, and Run stays a fresh agent on the brief; **U4** Tests and Sharing get a docked agent, Explorer/Diff/Turn/Subagent/Info do not; **U5** a card's record is its thread, a file's record is the buffer's undo steps plus a per-turn change list; **U6** cards and the Python console proceed in parallel, then file artifacts, then Stata.
- 2026-09-26 — Owner: “yes to all.” Close this umbrella after its slice cards and one full live run pass; #WYGY and #33G0 own the later TeX and Python/Stata depth. A clear Stata-unavailable message suffices on this host. Use a separate verification session for finished slices.

## Plan
**Goal.** Build the one pane model end to end: consoles (Shell, Python, Stata), artifact panes (cards first, then files), system panes, pop-out, and linked chains, each slice on its own card. *(Refreshed 2026-09-26: the hold is gone, the foundation cluster and eight of ten slices are on main in `needs-verification`, #2FQ9 is being built by a live session, #R660's code landed without its card being updated. What is left is finishing #2FQ9, syncing #R660, one verifying pass, and the owner's answer on what this umbrella waits for.)*

**Findings (2026-09-26, against main at 6514be0c).**
- *The foundation is on main.* The owner lifted the hold on 2026-09-25 ("lift the whole on #E85D/#F8R7/#C0Q8 cluster , you can build those"). The thread's hashes were rewritten: 503933ca → `13909113` (worker side of #C0Q8/#F8R7 plus the #6FDD remainder), b8de6172 → `7e4e0c45` (#F8R7 open buffers), 6c4b363b → `4ad5fe87` (#E85D groups and presets). The 2026-09-25 findings about untracked files and a hidden `land.py who` are obsolete: #BHJZ gave land.py one shared root, and `who` now lists every session.
- *Slice cards and where they are:*

| Slice | Card | State | On main |
|---|---|---|---|
| Cards as artifact panes | #Y2BA | needs-verification, 3/3 | `103e2f60` `64c7788d` `9a13cfe2` `0fec7afd` and follow-ups |
| Program destination | #S976 | needs-verification, 8/8 | `98913976` and earlier |
| Labels | #E8V1 | needs-verification, 4/4 | `e0ad8f8b` `784e91ea` `e6febb9f` |
| Manifest v2, relay.shell | #6FDD | needs-verification | `b51072ff` `483ed807` `03c70448` `13909113` |
| Python console | #83YV | needs-verification, 5/6; Stata t:ja deferred (no Stata on this host) | `5b9ce8cd` … `dd484d8a` |
| Docked agent on file editors | #PBZ4 | needs-verification, 5/5 | `ab196217` `b70c33bf` `7cea51a7` `4a5a4267` |
| Tests and Sharing agents | #3B1B | needs-verification, 3/3 | `488ff0c8` |
| Card drawer in a terminal pane | #6BY7 | needs-verification | `f7dff8f4` `6822c9a5` `c92cdc3f` |
| Pop-out / dock-back | #2FQ9 | executing, **live session** (`land.py who`: `2fq9`, idle minutes, uncommitted `src/LinkedAgent.h`, `src/PaneLinkedShell.cpp`, `src/RelayWindowLinkedAgent.cpp`, FilePanes ⤴ Shell button) | none yet |
| Linked chains | #R660 | planned, 0/5 **stale**: its code landed in salvage `def2cf0b` (+431 lines in `src/RelayWindowWorkspace.cpp`), evidence `docs/qa_evidence/2026-09-25-tex-chains/` (`b52a33ad`) | `def2cf0b` |

- *Found while checking:* `def2cf0b`'s chain registers `main.pdf` with the source's adapter (`text`/`editable`, `src/RelayWindowWorkspace.cpp:714-715`). The fix is on #E85D (t:q7). #10RD (`ef296c8d`) opens other group sources as unlinked panes, which is #E85D owner question 1.
- *The original "what is missing" list (Discussion points 1–6), by owner:* 1 shared-file safety → #F8R7 (done except an SSH pane test); 2 linked group and presets → #E85D (done except t:q7/t:m3 and live checks) and #R660; 3 artifact runner with latexmk, diagnostics and live/stale → #WYGY (engine `01e4e216`; `tex_build` wiring in its t:qk); 4 SyncTeX both ways → #WYGY t:j2; 5 typed previews beyond PDF → #33G0 t:9h (plots/tables) and later plugins; 6 remote builds → #WYGY t:en. Packaged Qt PDF (D3) → #9Y7X (executing); the viewer install button → #7WGJ. **This umbrella owns none of 3–6 directly.** Owner question 1 asks whether its closure waits for them.

**Remaining steps.** 1. #2FQ9 lands under its live session (not this card's to touch), then its group registration (#2FQ9 t:rp) on #E85D's model. 2. #R660's owner or the next session syncs that card: tasks 9s/qr/fy/vq checked against `def2cf0b`, the adapter defect handed to #E85D t:q7, and `## Execution Summary` pointing at the tex-chains evidence. 3. One separate verifying session (another model family recommended) works through the needs-verification slices against their own `## Done means` and runs the program-level scenarios below into `docs/qa_evidence/<date>-one-pane-model/`. 4. The owner closes the umbrella (`human: required`) when the slices close, with scope set by question 1.

**Risks.** #2FQ9's hunks share `src/FilePanes.cpp`, `src/RelayWindow.h`, `src/RelayWindowCore.cpp` and `src/Pane.h` with #E85D t:q7/t:m3, so land #2FQ9 first or claim with `--dry-run`. Two workers per tab remain until #2FQ9 shows whether they should merge. Verification needs a Qt-PDF build for the PDF half of scenario (d), which waits on #9Y7X.

**Verify.** Per slice on its card. Program level, live under Xvfb with an isolated profile: (a) two card panes planning at once; (b) a Python console where `x = 1` typed in the pty and `py_run_cell("x")` from the agent agree; (c) a card agent and a file agent popped out, running `git status`, and docked back with the transcript intact; (d) a shell → TeX → PDF chain restored after a restart, with the preview rendering the PDF (needs #9Y7X); (e) a person typing in a file while its docked agent edits it (#F8R7/#PBZ4). The owner looks at the result (`human: required`).
**2026-09-26 scope update.** This umbrella closes on its slice cards and one full program-level run. #WYGY owns deeper TeX work; #33G0 owns Python/Stata depth. A clear Stata-unavailable state is sufficient here, and a separate session checks the finished slices.

## Tasks

- [x] Wave 1: cards as artifact panes (#Y2BA) — landed, in needs-verification <!-- t:kc card=Y2BA -->
- [x] Wave 1: Program destination in the GUI (#S976) — landed (`98913976`), in needs-verification <!-- t:wr card=S976 -->
- [x] Wave 1: labels for the one pane model (#E8V1) — landed, in needs-verification <!-- t:fy card=E8V1 -->
- [x] Wave 1: manifest v2 and relay.shell (#6FDD) — remainder landed in `13909113`, in needs-verification <!-- t:nb card=6FDD -->
- [x] Owner lifts the hold; the #E85D/#F8R7/#C0Q8 cluster lands — `13909113`, `7e4e0c45`, `4ad5fe87` (2026-09-25) <!-- t:ft -->
- [ ] Wave 2: pop-out / dock-back (#2FQ9) — live session building it, nothing on main yet <!-- t:yf s=in-progress card=2FQ9 blocked_by=ft,kc -->
- [x] Wave 2: Python console, then Stata (#83YV) — Python landed, in needs-verification; Stata t:ja deferred until a host with Stata <!-- t:4q card=83YV blocked_by=ft,wr,nb -->
- [x] Wave 2: artifact panes on file editors (#PBZ4) — landed, in needs-verification <!-- t:vf card=PBZ4 blocked_by=ft,nb -->
- [ ] Wave 2: linked chains shell → TeX → PDF (#R660) — code in `def2cf0b`, evidence in `docs/qa_evidence/2026-09-25-tex-chains/`; card not synced, adapter defect is #E85D t:q7 <!-- t:gf card=R660 blocked_by=ft,vf -->
- [x] Wave 2: docked agent on Tests and Sharing (#3B1B) — landed (`488ff0c8`), in needs-verification <!-- t:mx card=3B1B blocked_by=ft -->
- [x] Wave 2: card drawer in a terminal pane (#6BY7) — landed, in needs-verification <!-- t:mj card=6BY7 blocked_by=ft,kc -->
- [x] Owner settled umbrella closure scope, Stata fallback and separate verification order on 2026-09-26 <!-- t:q1 -->
- [ ] One separate session verifies the finished slice cards <!-- t:v9 blocked_by=yf,gf -->
- [ ] Program-level live verification and evidence <!-- t:7w blocked_by=yf,4q,vf,gf,mx,mj,v9 -->
