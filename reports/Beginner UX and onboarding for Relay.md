# Beginner UX and onboarding for Relay

*Research synthesis and design proposal, 24 September 2026. Card `#9HS0` is filed with this report; the four research notes it draws on are in `research_notes/Beginner UX and onboarding for Relay/`.*

**Executive answer.** Relay's first run today is a live shell, a Models pane open on the providers tab, and two dialogs that arrive at 400 and 800 milliseconds; after that, an empty box that says "Shell commands or agent prompts…". Nothing tells a newcomer what to do, and nothing gets out of an expert's way either, since both get the same two interruptions. The research is unusually consistent about the fix. Every product that onboards well now teaches by having the person do **one real piece of work in the product's own surface**, with an escape hatch on the same screen for people who have done this before, and with keyboard shortcuts taught at the moment of use and then retired. The products that got burned did the opposite: a login wall (Warp 2022), an uninvited assistant (Clippy), a long story before first value (Arc), or an empty shell with tooltips on top (what Slack rebuilt its onboarding to replace). The proposal below replaces the two dialogs with one **Start pane** whose body is the owner's list of starter tasks, folds the approvals choice into it so the explicit pick survives, makes the prompt box say what it is for, labels Relay Free from the first turn, and turns the existing shortcut-hints machinery into **behaviour-triggered tips**. Experts get a one-line skip that never returns, and an import of the keys and instruction files they already have.

## 1. What a first run is today

The ground truth is in `relay-first-run-audit.md`; the load-bearing claims were re-read in the tree on 24 September.

- **Layout.** With no saved layout and `instructions/onboarded` unset, `WindowManager::newWindowAt` opens one terminal pane on the left and the Models pane on the right, on its providers tab, because "a fresh profile has no key, so … the first thing to do is add one" (`src/WindowManagerImpl.h:301-318`). That premise is no longer true: since card `#HG7K` a fresh install runs on Relay Free with no key at all.
- **Two interruptions in the first second.** `Pane::onSessionConfigured` fires the instructions dialog at 400 ms and the approvals screen at 800 ms (`src/Pane.h:5798-5812`). The approvals screen is the owner's explicit design from `#K2FV`: three sentences, two buttons, and it must be picked rather than dismissed. On a machine with no instruction files the instructions dialog is skipped and a starter `relay.md` is written silently (`src/Pane.h:8305-8340`, from `#ZYRB`).
- **The prompt box.** The placeholder is "Shell commands or agent prompts…      ?  for help" (`src/Pane.h:620-622`). Routing is automatic and invisible: `ls` runs locally, "list the files" spends a Relay Free turn, and nothing on screen says which will happen before Enter.
- **Tips exist and are good, but generic.** `showIdleTip` rotates six tips after an idle agent turn (@ attaches a file, plan mode, Esc Esc rewinds, ↓ selects subagents, the Actions palette, the `!`/`*` prefixes), plus a Board tip when the tab is attached to a project (`src/Pane.h:10182-10210`). Each shows at most three times with a 30-minute cooldown (`src/Hints.h`). They are triggered by idleness, not by what the person was just doing.
- **Relay Free is unlabelled until it runs out.** The provider is one row like any other in the Models pane; the quota surfaces as a chip and the exhaustion message arrives mid-task with a detour to add a key.
- **Discovery is behind three chords.** Actions (Ctrl+Shift+A), Options (Ctrl+Shift+O) and the Board (Ctrl+Shift+S) are documented only in the README key table and the `idle.palette` tip.
- **Existing cards.** `#MH58` (instructions onboarding), `#ZYRB` (no dialog when there are no files), `#HG7K` (Relay Free), `#K2FV` (first-launch approvals choice), `#8E4Q` (the switchboard aesthetic names "First-run / onboarding" as a surface that may carry the metaphor because it is seen once). None of them asks for a first-run experience as a whole.

## 2. What the research says

Four notes, about 14,000 words, are reduced here to the findings that change the design. Each links to the note that holds the sources.

**Nobody teaches features any more; they teach one piece of real work.** Claude Code's quickstart is an eight-step ladder from install to first code change to first git commit; Cursor's first task is "ask Cursor to explain the codebase"; Raycast's is seven habits practised over days; Linear's activation event is a *resolved* issue, not a created one; GitHub's Hello World is repo, branch, pull request. ([dev-tools](../research_notes/Beginner%20UX%20and%20onboarding%20for%20Relay/dev-tools-onboarding.md), [mass-market](../research_notes/Beginner%20UX%20and%20onboarding%20for%20Relay/mass-market-onboarding.md)). The literature agrees: Carroll's minimalist instruction puts the learner on a meaningful task at once and makes error recovery the lesson, and NN/g's summary is that "tutorials interrupt users, don't necessarily improve task performance, and are quickly forgotten" ([design literature](../research_notes/Beginner%20UX%20and%20onboarding%20for%20Relay/design-literature.md), §2.2, §6.3).

**The blank canvas is the enemy, and templates are the fix, but blank must stay one click away.** Canva's teardown: "there is no blank canvas." Word 2013 found that over half of sessions start blank and responded by making *blank* better ("Single spaced (blank)") rather than hiding it. Figma seeds real example files because "blank-canvas intimidation" is its named top risk. Notion's Getting Started page is a real page you complete and then delete. ([mass-market](../research_notes/Beginner%20UX%20and%20onboarding%20for%20Relay/mass-market-onboarding.md) §1, §3, §4, §9.)

**Experts and novices share one screen when the screen carries a skip.** GitHub's empty repository page is titled "Quick setup — if you've done this kind of thing before": experts self-identify and are gone, novices get copy buttons. Apple's rule is skippable, never re-shown, always findable. VS Code's Welcome page retires itself once the walkthroughs are done and doubles the recents list. Zed's welcome page disappears when a folder is opened. ([mass-market](../research_notes/Beginner%20UX%20and%20onboarding%20for%20Relay/mass-market-onboarding.md) §2, §8; [dev-tools](../research_notes/Beginner%20UX%20and%20onboarding%20for%20Relay/dev-tools-onboarding.md) §6, §7.)

**Shortcuts are taught at the moment of use, then retired.** Superhuman's ⌘K shows the shortcut "for next time" on every action; Cooper's rule is "don't weld on training wheels"; Carroll and Carrithers built literal training wheels at IBM that come off. Relay's `ShortcutHints` is already this pattern; the research says to extend it, not replace it. ([design literature](../research_notes/Beginner%20UX%20and%20onboarding%20for%20Relay/design-literature.md) §1.2, §3.3, §6.2.)

**Credentials after value, never before.** Warp's mandatory login at the 2022 launch is the most-cited onboarding mistake in the terminal category and took two public reversals to fix; Zed's sign-in for edit prediction produced a fork; Arc's account wall at the end of a charming intro was the complaint on Hacker News. Codex and Claude Code offer two routes (subscription or key) inside the tool at the moment of first need. Relay Free already removes the wall; the remaining work is to say so. ([dev-tools](../research_notes/Beginner%20UX%20and%20onboarding%20for%20Relay/dev-tools-onboarding.md) §1, §3, §4, §7.)

**Assistants must not interrupt, must know their own capabilities, and must be honest about scope.** Swartz's Stanford study of Clippy: it "ignores social conventions of when to disturb someone, it does not learn from its mistakes." NN/g's study of new AI users found that three of six had never used an AI tool, that they open with "can you…?" questions, and that a bot that misdescribes its own features damages the person's model of the product. Slackbot is the counter-example: an assistant that onboards by doing things with you in the product's own medium, closable. ([design literature](../research_notes/Beginner%20UX%20and%20onboarding%20for%20Relay/design-literature.md) §5.)

**Terminals frighten people because they have no signifiers and no undo.** Julia Evans's survey of people who stopped being scared of the command line credits risk reduction above all: `rm` aliased to something safe, tab completion showing what a wildcard will hit, git and backups, a prompt that says where you are. Norman's framing is that a prompt has capabilities but no signifiers; Tognazzini's explorable interfaces and protect-the-user's-work are the two principles a bare terminal violates most. ([design literature](../research_notes/Beginner%20UX%20and%20onboarding%20for%20Relay/design-literature.md) §1.3, §1.4, §4.1.)

**Behaviour-triggered tips beat a tip window.** Warp's January 2026 builds added "agent tips under the warping indicator when there is a relevance match"; iTerm2's Tip of the Day is the dated form. Apple's guideline is "a collection of context-specific tips instead of a single onboarding flow." ([dev-tools](../research_notes/Beginner%20UX%20and%20onboarding%20for%20Relay/dev-tools-onboarding.md) §1, §8; [mass-market](../research_notes/Beginner%20UX%20and%20onboarding%20for%20Relay/mass-market-onboarding.md) §8.)

## 3. The proposal: Relay's first ten minutes

### 3.1 The contract with each audience

For the person who has never used a terminal: never a blank screen, one real task completed in the first session with the agent doing the typing, nothing destructive without a word of warning, and every hint in plain words. For the expert: nothing in the way that a single keystroke cannot remove for good, their existing keys and instruction files imported rather than retyped, their dotfiles and prompt honoured (Relay already skips nothing in `~/.bashrc` unless asked), and density they can turn up. Both get the same Relay; the Start pane and the tips are the only surfaces that differ, and both retire themselves.

### 3.2 Launch: one Start pane instead of two dialogs

On a fresh profile the window opens as today's terminal pane at the left, but the right-hand pane is a **Start pane**, not the Models pane. It is a pane, not a floating dialog, per the owner's standing preference recorded on `#K2FV`, and it holds, top to bottom:

1. One line of orientation: *Type a command and it runs. Type a request and the agent answers, in the same box.*
2. **What would you like to do?** The starter tasks of §3.3, as a list the arrow keys walk and Enter picks.
3. The approvals choice from `#K2FV`, unchanged in its three sentences and two buttons, as the last row. The owner's rule that allow-everything must be explicitly picked survives; what changes is that the pick happens on the one screen a newcomer is already reading, not a second screen that lands on top of the first at 800 ms. Until it is answered the cautious set stays in force exactly as now.
4. A footer: *Done this before? Esc closes this and it will not come back. `/start` reopens it.*

The instructions dialog stops firing at 400 ms on a fresh machine. `#ZYRB` already made the empty-list case silent; the Start pane makes the non-empty case a starter task instead ("Import my settings from Claude Code / Codex / Warp", which lists the instruction files and keys it found, every box unchecked, exactly like the project-import question in `docs/PROJECT-INIT-AND-IMPORT.md`).

The Models pane no longer greets a fresh profile. A person with no key is on Relay Free and can work; "Set up model providers" is a starter task that opens the Models pane on the providers tab, which is what `#MDL1` t:a11 wanted, now reached by choice.

### 3.3 Starter tasks (the owner's list)

Each starter task is a **recipe**: a folder choice, a pane layout, a first prompt placed in the composer but **not sent** (the person presses Enter, so the first agent turn is theirs), the things it needs checked before it starts, and the tips that are armed by choosing it. The list is the owner's, from 24 September; the right-hand columns say what each rests on today.

| Starter task | First prompt in the composer (editable) | Layout and what it rests on | Needs |
|---|---|---|---|
| Start coding | "Explain this project to me, then suggest three things I could do next." (Cursor's first task; on an empty folder: "Set up a new <language> project here with a README and a first test.") | Terminal pane; offers to init a Board (`docs/PROJECT-INIT-AND-IMPORT.md`) | A folder |
| Analyze some data | "Look at the data files in this folder, tell me what is in them, and plot the most interesting column." | Python or Stata task plugin (`docs/TASK-PLUGINS.md`), source beside a preview | Python present; the `requires` check the plugin already does |
| Write a paper | "Set up a LaTeX paper skeleton with a bibliography and build it." | TeX task plugin, source beside PDF | A TeX distribution, checked by the plugin |
| Triage my email | "Show me what is in my inbox and sort it into needs-reply, waiting, and archive." | Terminal pane; needs a mail skill | A skill that exposes mail; the recipe says so and offers the skills dialog when none is installed |
| Organize my folders | "Show me the largest and oldest things in ~/Downloads and propose a way to sort them. Do not move anything yet." | Terminal pane with the file preview | Nothing; the prompt itself is the safety rule (propose, then act) |
| Set up model providers | (none) | Models pane on the providers tab; Import from Warp / Claude Code / Codex when those are on the machine, hidden otherwise | Nothing |
| Organize multiple subscriptions | "Which subscriptions and keys do I have, and which should each kind of work run on?" | Models pane, profiles section; `reports/Multiple subscription routing in Relay.md` | At least one key or harness login |
| Start a painless SSH session | "Connect me to <host>." with the host field focused | Terminal pane; Relay's ssh wrapping from `#S5SH` (the agent keeps working on the host) | A host name |
| Generate some art | "Make me an image of …" | Terminal pane with inline media (`docs/INLINE_MEDIA.md`), the `media_*` tools | A provider that can generate images, or Relay Free if the gateway serves one |
| Develop a game | "Start a small playable game in this folder in <engine or plain HTML>, and open it when it runs." | Terminal pane beside a preview pane | A folder |
| Just give me a terminal | (none) | Bare pane; sets `instructions/onboarded`; the Start pane never returns | Nothing |

Rules that apply to every recipe:

- **Nothing is sent, nothing is written, until the person presses Enter.** The recipe stages; the person acts. This is the one-safe-Goomba rule from Mario 1-1 and Apple's "safely test an action".
- **A recipe that needs something it cannot find says so in one line and offers the fix**, in the pane, not a dialog: *Triage my email needs a mail skill. Install one… · Pick another task.* Never a task that fails on its first Enter (NN/g: a suggested prompt must never promise what fails).
- **The list is extensible.** A bundled recipe is a small manifest beside the bundled skills (`backend/relay_core/skills_bundled/`), and an installed skill or task plugin can declare a starter task of its own. The owner's skill catalogue already holds email triage, subscription management and a game-level pipeline; on a machine that has them, the list shows them.
- **A recipe can become the first card.** When the folder has a Board, or the person says yes to creating one, the chosen task is filed as a card and the first agent turn is its work. That is Linear's activation event, a resolved issue, in Relay's own vocabulary.

### 3.4 The prompt box says what it is for

- Placeholder on a fresh pane: **"Type a command, or say what you want done      ?  help   /  commands"**, with the rungs "Command or request…", "Command…" for narrow panes (the ladder in `src/AgentContext.cpp:46-64` splits on commas, so the copy is written comma-first). After the first successful agent turn the placeholder returns to the terse one; the long form is a training wheel.
- **Show the routing before Enter.** The mode chip that `applyChipFlash` already flashes becomes a steady, visible "terminal" or "agent" label while the line is typed, in the destination colours the switchboard design already assigns (cyan and violet, `docs/SWITCHBOARD-AESTHETIC.md` §1). Raskin's rule: a mode that is invisible is a mode that bites. Once the person has used `!` or `*` twice, or has turned hints off, the chip goes back to its quiet form.
- **`?` on an empty box answers "what can you do here?"** in the agent's own words and accurately: the tools in this pane, the Board if there is one, the model it is on and what it costs. NN/g found new AI users open with exactly that question.
- **Ghost suggestions from the actual folder.** The existing "Suggested prompt (AI) · Tab accepts" channel is used for the second and third suggestions of a recipe, so the box teaches itself the way fish's autosuggestions teach the shell.

### 3.5 Targeted tips: behaviour-triggered, named key, retire on use

The `ShortcutHints` machinery stays; what changes is the trigger. Today a tip fires on idleness. The proposal adds **triggers on what the person just did**, so the tip is Warp's "relevance match" rather than a rotation. Each trigger names the tip's key, shows at most three times (the existing limit), and is retired the first time the person uses the shortcut it names, which the hints system can already count. The global 20-second gap and the toast queue are unchanged, so a tip never lands on top of a running turn.

| What the person just did | Tip |
|---|---|
| Typed a sentence that ran in the shell and failed ("command not found") | *Ctrl+Shift+Enter asks the agent to fix a failing command. Or just say what you wanted.* |
| Typed a sentence that went to the agent when a command was meant | *Start a line with ! to force the terminal.* |
| Ran the same command three times | *↑ recalls it; → accepts the dim suggestion.* |
| Reached for the mouse to split, or opened the third tab | *Ctrl+E splits to the right; ← ↑ ↓ places it.* (already a hint id) |
| The agent edited a file for the first time | *Esc Esc rewinds the chat; /rewind-code restores the files it changed.* |
| The agent ran `rm`, `mv` or `git push` under allow-everything | *Options › Security can ask before deletions, moves and pushes.* (once, ever) |
| Typed `cd` into a folder that has a `.board/` or a `TODO.md` | *This folder has work in it. Ctrl+Shift+S opens the Board; # names a card.* |
| Typed `ssh` | *The agent keeps working on the host you connect to.* |
| Pasted a long error | *@ attaches a file instead of pasting it.* |
| Relay Free quota reached 20% | *About a fifth of today's free turns left. Add a key in Options › Models, or Ctrl+Shift+M.* |
| Fifth agent turn on Relay Free | *Relay Free is the included model. Your own key runs bigger models and never touches Relay's server.* |
| Opened the Actions palette | (no tip: the palette shows the key beside every row, which is the Superhuman lesson; check that it does) |
| Hints turned off, or Esc pressed on three tips in a row | Stop. Cooper: don't weld on the training wheels. |

Every tip is a sentence, names a key or a command, and never asks a question. None appears while a turn is running. The "reset shortcut hints" row in Options already exists for a person who wants them back.

### 3.6 Relay Free is labelled from the first turn

The first agent reply on a fresh install carries one note line, in the same `Ink::Note` the starter `relay.md` message uses: *Answered on Relay Free, the included model · N turns today · your own key: Options › Models.* The quota chip is visible from the first turn rather than from exhaustion, and the 20% tip above fires before the day dies. The exhaustion message keeps its detour, but the person has been told twice by then. This is Warp's "some AI on us" preview done with the honesty Warp lacked in 2022.

### 3.7 Safety that a newcomer can feel

Julia Evans's list becomes product rules, and most are already partly present:

- The approvals checklist from `#K2FV` covers `delete_or_move`, `network` and `read_outside`. The Start pane's one-line explanation of the cautious set should say what those words mean to a person who has never typed `rm`.
- **Preview what a wildcard hits.** Before the shell runs a line with `*` and `rm`, `mv` or `chmod -R`, the pane prints the expansion under the box, the way fish's tab completion does. This is a small router change, not an agent feature.
- **Undo is the recovery story.** `/rewind-code` exists; the tip in §3.5 makes it known at the moment the agent first edits a file, which is when a newcomer first wonders whether they can take it back.

### 3.8 Experts: out of the way, and their life imported

- Esc on the Start pane sets `instructions/onboarded` and it never returns; `/start` and Options › General reopen it. VS Code's setting and Apple's rule, in one key.
- "Import my settings" finds keys and instruction files from Claude Code, Codex and Warp when those are on the machine, and is hidden otherwise (the audit's friction point 9).
- Shortcut hints off is one toggle, already in Options › General. A "compact" density is a product decision for `#8E4Q`, not this card.
- The prompt honours the person's shell configuration already; the Start pane says so in its footer for anyone who wonders.

### 3.9 Measuring it without telemetry

Relay's privacy stance (keys never touch Relay's server; Warp's default-on telemetry is an anti-pattern in the dev-tools note) rules out a funnel. The activation event can still be defined and observed locally. Proposed definition, in Slack's "2,000 messages" spirit: **a first agent turn that ran a tool, followed by a second session on another day.** Relay records both already (sessions and turn records); a local, private counter and the QA drive under Xvfb that `#K2FV` established (`docs/qa_evidence/2026-09-19-ask-before-risky-things/`) are enough to judge each change on a fresh `XDG_CONFIG_HOME`. Every step of the Start pane is judged by one question: does removing it lose anyone? If not, it goes.

## 4. Decisions for the owner

1. **Approvals inside the Start pane.** `#K2FV` says "one screen, before anything else, that cannot be dismissed without choosing." The proposal keeps the pick explicit but puts it on the Start pane's last row, so a newcomer reads one screen, not two. If the owner wants the approvals screen to stay separate, the Start pane opens after it instead, and the rest stands.
2. **Which starter tasks ship bundled.** The eleven above are the owner's list plus "Write a paper" and "Just give me a terminal". Email triage and subscriptions rest on skills that are the owner's rather than bundled; the recipe can ship with a "needs a skill" line, or wait until a bundled skill exists.
3. **Whether a recipe files a card.** Filing the first task as a Board card makes the Board visible from minute one, which the switchboard design wants, but it means a Board is created in the person's folder on their first run. The project-init rule ("nothing creates `.board/` before that yes") stands either way; the question is whether the Start pane asks it.
4. **The Start pane's look.** `#8E4Q` lists first-run as a surface that may carry the switchboard material, since it is seen once. That is the owner's call under the three tests in that document.

## 5. Delivery

Three cards, in order, each landable alone:

1. **Start pane and starter tasks** (large, UI): the pane, the recipe manifest, the eleven bundled recipes, `/start`, the Esc skip, the Models pane no longer greeting a fresh profile, the instructions dialog folded into "Import my settings". Verified on a fresh `XDG_CONFIG_HOME` under Xvfb, with a screenshot per recipe and a drive that presses Esc and relaunches.
2. **Prompt box and Relay Free labelling** (medium): the placeholder ladder, the visible routing chip while typing, the `?` capability answer, the first-turn note line and the 20% tip. Proved by the composer and presets tests plus one Xvfb screenshot.
3. **Targeted tips and wildcard preview** (medium): the trigger table as hint ids, the retire-on-use rule, the expansion preview for destructive wildcards. Proved by `ShortcutHints` unit tests for each trigger and the router test for the preview.

Sources: the four notes in `research_notes/Beginner UX and onboarding for Relay/`: `relay-first-run-audit.md` (this tree, with file:line), `dev-tools-onboarding.md` (Warp, Ghostty, Claude Code, Codex, Cursor, VS Code, Zed, iTerm2, Windows Terminal, Fish, Raycast, Nushell), `mass-market-onboarding.md` (Word, GitHub, Notion, Figma, Slack, Superhuman, Arc, Linear, Apple, Duolingo, Canva, ChatGPT, Claude, games) and `design-literature.md` (Nielsen, Cooper, Norman, Tognazzini, Raskin, Victor, Shneiderman, Hulick, NN/g, Appcues, Superhuman, Julia Evans, Swartz on Clippy, Carroll, Sierra, Eyal, JTBD).
