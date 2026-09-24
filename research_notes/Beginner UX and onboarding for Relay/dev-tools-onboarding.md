# Onboarding & first-run research: how developer tools handle the blank-slate moment

Research for Relay's first-run design. Sources are official docs, changelogs, blogs, HN/GitHub threads, fetched 2026-01/02. Facts are quoted where the exact copy matters.

---

## 1. Warp

**(a) First 60 seconds.** Launch Warp and you land in a live terminal session — no wizard, no modal. The current quickstart doc states it flatly: "When you launch Warp, you'll see a terminal session ready for input. You can optionally sign up for an account (top right), or skip and start using immediately," with optional onboarding steps available afterwards (https://docs.warp.dev/getting-started/quickstart). The structural concept you need to learn — commands and their output are grouped into editable "blocks" — is taught through interaction hints: up/down moves between blocks, ⌘↩ runs input, and "default keybindings maximize compatibility with common terminal shortcuts." A January 2026 stable release finally shipped a dedicated "onboarding flow for new users" (v2026.01.14), and the 2026.01.07 build added contextual "agent tips under the warping indicator when there is a relevance match" — tips triggered by behavior, not by a tour (https://docs.warp.dev/getting-started/changelog).

**(b) No-credentials state.** This is Warp's saga. At the April 2022 public beta launch, login was mandatory: "Currently, we do so to enable team features (e.g., shared command configuration), to maintain a consistent user experience across all your devices, and to maintain app stability and security" (https://www.warp.dev/blog/lifting-login-requirement). The backlash was immediate — on the Show HN (946 points, 726 comments): "The ui is beautiful but a login for a terminal is insane"; "This feels like a product designed to eventually sell me something"; "Rust tooling deserves better than an Electron-like wrapper with an account requirement" (https://news.ycombinator.com/item?id=30921231). In December 2022 they made telemetry opt-out ("Telemetry is now optional in Warp", https://news.ycombinator.com/item?id=33910992), and in November 2024 — days after Ghostty 1.0 was announced — they made login optional: "For a long time, the answer to 'should we make login optional?' is a resounding yes... This was not a small portion of the community; it was most of it." The logged-out mode is explicitly not "offline mode": the app works fully with "some AI on us" as preview usage, and login is only re-prompted when a feature needs it; they reserve the right to require accounts later for team features. HN's response: "This is the sequel to a paid terminal, which was the sequel to an Electron terminal that asked you to login," and "Ghostty has taught me a terminal can be both fast and free of enshittification" (https://news.ycombinator.com/item?id=42247583).

**(c) Shortcuts/hidden power.** A searchable Command Palette is the escape hatch; the AI surface is taught with a literal type-this token: typing `#` in the input editor switches it into AI/natural-language mode ("What can I do with the AI panel?" is the offered starter prompt), with the AI Panel on ⌘⌥A and Agent Mode on ⌘↩ (https://docs.warp.dev/knowledge-base/terminal, https://docs.warp.dev). Warp Drive (⌘⇧D) ships pre-populated Workflows, Notebooks, environment variables and rules that double as context for agents.

**(d) Experts vs novices.** Experts get a genuine migration path: the palette action "Import External Settings" detects iTerm2 profiles, previews the settings that will change, and offers a choice between the Warp prompt and your existing PS1 — meeting experts where their dotfiles live (https://docs.warp.dev/getting-started/migrate-to-warp). Novices get the modern-editor look, blocks, and the `#` natural-language affordance.

**(e) Praised/criticised.** Praised: blocks, speed, Drive, the login reversal. Criticised: the original login wall (the single most-cited terminal onboarding mistake on HN), default-on telemetry until Dec 2022, and for years the login-vs-features ambiguity ("features may be gated later" keeps the trust question alive).

**(f) Outcomes.** Warp's own framing: the login requirement persisted because they "didn't listen closely enough to the community"; the reversal posts are rare public examples of a tool publicly reversing an onboarding decision under user pressure.

## 2. Ghostty

**(a) First 60 seconds.** Nothing happens — deliberately. Launch Ghostty and you get a working terminal with "sane defaults," full stop. The docs state the philosophy verbatim: "Zero Configuration Philosophy: Ghostty is designed to 'just work' out of the box with zero configuration" (https://ghostty.org/docs/config). Mitchell Hashimoto's pre-launch essay set the bar: "Ghostty is a fast, feature-packed, cross-platform terminal that 'just works'... arguably the best drop-in replacement" for your existing terminal (https://mitchellh.com/writing/ghostty-is-coming).

**(b) No-credentials state.** None exists. No account, no login, no AI, no telemetry prompts — which is precisely what HN's Warp threads held up as the alternative.

**(c) Shortcuts/hidden power.** "The config file is the UI": keybinds are `keybind = key_sequence=action` strings in `config.ghostty` (renamed from `config` in v1.2.3; XDG path plus a macOS-specific path). Discovery is CLI-native: `ghostty +show-config --default --docs` prints every option with its documentation even when unset, and `+show-config` annotates values that are defaulting; the keybind docs point to `ghostty +list-actions` as "the best way to browse all of Ghostty's available actions" and `+list-keybinds` for the active set (https://ghostty.org/docs/config). Power is surfaced by inspection, not by UI.

**(d) Experts vs novices.** Unashamedly expert-first: the novice story is "don't configure anything"; the expert story is self-documenting config. There is no novice-specific surface.

**(e) Praised/criticised.** Praised: defaults, speed, native UI, no account. Criticised: discovery depends on reading docs/source; the v1.2.3 config-filename rename confused upgraders; there is no in-app teaching layer at all.

**(f) Outcomes.** No metrics published; the cultural outcome is that Ghostty 1.0 (Dec 2024) became the benchmark that pressured Warp's login reversal.

## 3. Claude Code (CLI + desktop/VS Code)

**(a) First 60 seconds.** The official quickstart is an 8-step guided ladder: Install → Log in → Start your first session → Ask your first question → Make your first code change → Use Git with Claude Code → Fix a bug or add a feature → Test out other workflows — followed by "Essential commands" and "Pro tips for beginners" sections (https://code.claude.com/docs/en/quickstart). On first use you're prompted to log in (Claude subscription account or Anthropic Console); Console orgs get a "Claude Code" workspace auto-created on first login. Setting `ANTHROPIC_API_KEY` skips login entirely and instead prompts you to approve the key; `/login` switches accounts later. First session in a folder shows the trust dialog; after that you land at an empty prompt with a `/`-triggered command menu.

**(b) No-credentials state.** Hard gate: "Claude Code requires an account to use" — but with two routes (subscription vs. usage-billed Console/API key), which became the template Codex copied.

**(c) Shortcuts/hidden power.** Pressing `?` shows the shortcut list; `/` opens the command menu with a right column that can expand to include skills, plugins and MCP. Notably, the docs admit some slash commands "are hidden by design" to avoid overwhelming the menu (https://code.claude.com/docs/en/interactive-mode). Multiline entry is taught with honest terminal reality: native Shift+Enter works only in iTerm2, WezTerm, Ghostty, Kitty, Warp, Apple Terminal and Windows Terminal; `\`+Enter works everywhere — readline-style emacs bindings (Ctrl+K, Ctrl+U…) are documented as muscle-memory transfer.

**(d) Experts vs novices.** Explicitly two-speed: the setup doc links "If you've never used a terminal before, see the terminal guide" — a dedicated novice doc (https://code.claude.com/docs/en/setup). Approval autonomy is also tiered: on Pro/Max plans the default is now "auto" mode where "a classifier reviews actions instead of you... Claude edits most files and runs most commands without asking," while other plans start in manual (quickstart).

**(e) Praised/criticised.** Praised: the quickstart ladder and `/init` (writes a CLAUDE.md project memory) make the second session better than the first. Criticised: the trust dialog's UX. Issue #90582 asks that "Yes, I trust this folder" be restored as the first/default option, warning the current order trains unsafe muscle memory; trust decisions persist in `~/.claude.json` (`hasTrustDialogAccepted`) per-directory, except the home directory, which is deliberately never persisted and re-prompts (https://github.com/anthropics/claude-code/issues/90582). Desktop app and VS Code extension share the CLI's first-run (trust/approval settings apply to all instances, per the setup doc).

**(f) Outcomes.** No published funnel metrics; observable outcome is that "trust the folder" and plan/auto approval tiers are now the genre's standard pattern.

## 4. OpenAI Codex CLI

**(a) First 60 seconds.** Install → `cd` into a project → run `codex` → choose a sign-in method → get a first task suggested ("ask it to explain the codebase"-style orientation prompt) (https://developers.openai.com/codex/cli).

**(b) No-credentials state.** The README is blunt: "Run codex and select Sign in with ChatGPT. We recommend signing into your ChatGPT account to use Codex as part of your Plus, Pro, Business, Edu, or Enterprise plan... You can also use Codex with an API key, but this requires additional setup" (https://github.com/openai/codex). The auth doc frames the two methods as subscription (with admin controls) vs. usage-based API key (https://developers.openai.com/codex/auth).

**(c)/(d) Shortcuts & audience.** Power is surfaced as permission presets, not keybindings. The June 2025 README taught a three-level autonomy ladder — **Suggest** (default: read-only + approve everything), **Auto Edit** (read/write files, network disabled), **Full Auto** (execute commands, network disabled, confined to working dir) (https://web.archive.org/web/20250601000000/https://github.com/openai/codex). Current docs ship the same idea as a sandbox (OS-enforced, network off by default, typically the workspace) plus an approval policy, with a `/permissions` editor and an "Auto" preset = workspace-write + on-request approvals; read-only mode is recommended when you "just want to chat about files" (https://learn.chatgpt.com/docs/agent-approvals-security).

**(e) Praised/criticised.** Praised: sandbox-on-by-default makes the first run safe to experiment with. Criticised (historically, in HN threads about the April 2025 launch): approval fatigue — being asked to bless every command trains users to auto-accept, which is why both Codex and Claude Code moved to classifier/allowlist-based auto modes.

**(f) Outcomes.** The ChatGPT-sign-in framing ("your subscription includes the agent") measurably lowered the credential barrier vs. API-key-first CLIs.

## 5. Cursor

**(a) First 60 seconds.** Historically the hook was zero-friction migration: the 2023 docs promise "You can effortlessly import all your VS Code extensions, settings, and keybindings with our built-in VS Code Import. This can be found under Cursor Settings > General > Account" (https://web.archive.org/web/20230801000000/https://docs.cursor.com/). Today's quickstart is a task ladder instead of a tour: sign in → open agent chat (⌘I) → as your first task, "ask Cursor to explain the codebase" → review the diff → learn Shift+Tab for Plan Mode (https://cursor.com/docs/get-started/quickstart).

**(b) No-credentials state.** Sign-in is the first screen; the product is account-first (trial included), unlike Codex/Claude Code's API-key escape hatch framing.

**(c) Shortcuts/hidden power.** The three keystrokes are taught as a mantra repeated across docs and changelog: Tab (autocomplete), ⌘K (inline edit), ⌘I/⌘L (agent/chat). The quickstart's "explain the codebase" first task is a forcing function: it makes the agent read your project before you write a prompt, and produces an immediately reviewable artifact.

**(d) Experts vs novices.** Experts: import everything, keep your keybindings and extensions, done. Novices: a familiar VS Code shell plus one chat box. The changelog's framing of "Projects" — persistent context so "you shouldn't have to onboard an agent every time you start a task" (https://cursor.com/changelog) — shows Cursor treats *repeated* onboarding as the real enemy.

**(e) Praised/criticised.** Praised: the VS Code import ("it just felt like my editor, but with AI") is the most-cited reason for Cursor's early adoption. Criticised: pricing/trial surprises and sign-in-first gating in community threads.

**(f) Outcomes.** No onboarding metrics published; the import-first strategy is credited in reviews and HN threads with Cursor's fast early growth against Copilot-in-VS-Code.

## 6. VS Code

**(a) First 60 seconds.** The Welcome page: a Start column (New File, Open Folder, Clone Repository), a Recents list, and Walkthroughs. Since v1.57 (June 2021) walkthroughs are first-class: core and extensions contribute guided, step-by-step tours with checkmarked steps; "once you've completed (or dismissed) all walkthroughs, VS Code rearranges the contents to double the number of recent workspaces" — the page literally retires itself. `workbench.startupEditor` defaults to `gettingStarted`, and extensions can opt out of auto-opening their walkthrough on install via `workbench.welcomePage.walkthroughs.openOnInstall` (https://code.visualstudio.com/updates/v1_57).

**(b) No-credentials state.** Fully functional with no account. Settings Sync is an explicit, reversible act: "Backup and Sync Settings..." from the Manage gear or Accounts menu → sign in with Microsoft or GitHub → local and cloud data merge automatically, with conflict prompts if needed (https://code.visualstudio.com/docs/configure/settings-sync).

**(c) Shortcuts/hidden power.** The official advice: "The best way of exploring VS Code hands-on is to open the Welcome page and then pick a Walkthrough for a self-guided tour" (https://code.visualstudio.com/docs/getstarted/tips-and-tricks), plus the command palette as the universal action index and printable keyboard-shortcut reference sheets. The Walkthrough API (https://code.visualstudio.com/api/extension-guides/walkthrough) lets extensions ship the same checklist pattern with media, buttons and completion events — onboarding is a platform feature, not a one-off.

**(d) Experts vs novices.** Same surface, different entry: novices take "Get Started"/"Learn the Fundamentals" walkthroughs; experts disable the welcome page with one setting and live in the palette. Copilot folds in gently: first sign-in enrolls eligible accounts in Copilot Free, and the Copilot docs route newcomers to "complete your first coding task with an AI agent" (https://code.visualstudio.com/docs/copilot/setup).

**(e) Praised/criticised.** Praised: the self-retiring welcome page and extension-level walkthroughs. Criticised: welcome-page persistence annoyed users for years (hence the startup setting), and the post-2023 surface keeps accruing entries (Copilot, chat) that crowd the blank state.

**(f) Outcomes.** The v1.57 notes record the walkthrough system as a direct response to "empty editor" abandonment; recents-doubling is a measurable built-in A/B of "learned enough → show me my work."

## 7. Zed

**(a)/(b) First 60 seconds & credentials.** Launch shows a Welcome page with quick actions (open folder, clone repo, view docs) — and it "disappears once you open a folder," lives only in the center pane, and can be reopened from the command palette (https://zed.dev/docs/getting-started). The editor works fully signed-out; sign-in is demanded by AI features: Zeta edit prediction requires a Zed account (one HN comment notes the docs promise "2,000 free predictions" on sign-in), which produced backlash including a fork ("Rouge") explicitly removing the "refusing to allow the Zed account/sign-in features to be disabled" behavior (HN search: https://hn.algolia.com/?query=zed%20sign%20in).

**(c) Shortcuts/hidden power.** The docs call the command palette "the gateway to every action in Zed" and front-load an essentials table (⌘⇧P palette, ⌘K ⌘T theme switcher, ⌘, settings, ⌥⌘L/⌘⇧A agent panel). Zed also ships "agentic vs classic" layout presets and vim/helix modes.

**(d) Experts vs novices.** Strong expert-concierge pattern: dedicated migration guides for VS Code, IntelliJ, PyCharm, WebStorm and RustRover users (zed.dev/docs/getting-started). Novices get the welcome page and defaults.

**(e) Praised/criticised.** Praised: speed, migration guides, palette-first design. Criticised: AI-features-require-sign-in in a tool marketed as native/local; the fork is the visible cost.

## 8. iTerm2 / Windows Terminal (first run, briefly)

**iTerm2** does essentially no onboarding: it ships a "Tip of the Day" window (documented in the official one-page docs, https://iterm2.com/documentation-one-page.html), an optional Shell Integration install (documented as the power feature that enables command marks, status, etc.), and a preferences wall. Discovery is menu/docs-driven; the audience is assumed expert.

**Windows Terminal** opens a PowerShell tab on first launch ("when you install Windows Terminal, the default profile is set to PowerShell", https://learn.microsoft.com/en-us/windows/terminal/install). Its first-run decision is OS-level: making it the default terminal application (Startup settings; the flow differs on Windows 11 vs. Windows 10 22H2+KB). Startup behavior is config-first (`firstWindowPreference: defaultProfile | persistedWindowLayout`, 120×30 default size, https://learn.microsoft.com/en-us/windows/terminal/customize-settings/startup). No tour, no account — Microsoft's answer to onboarding is "it opens and works."

## 9. Fish

**(a) First 60 seconds.** Fish teaches through the prompt itself. The default greeting — "Welcome to fish, the friendly interactive shell. Type help for instructions on how to use fish" — sits above a default prompt that already demos syntax highlighting and shows host/cwd (https://fishshell.com/docs/current/tutorial.html). As you type, gray ghost-text autosuggestions appear asynchronously from history — the single most imitated "learn by watching the tool" pattern in shells.

**(b)/(c)** No credentials; hidden power is discoverable: `help` opens docs, `fish_config` opens a web-based config UI, and tab-completion teaches flags in context.

**(d) Experts vs novices.** The design doc is explicit: "Configurability is the root of all evil... Every configuration option is a place where the program is too stupid to figure out for itself what the user really wants," and autosuggestions must be asynchronous "so that typing is never delayed" (https://fishshell.com/docs/current/design.html). Great defaults for novices; escape hatches for experts.

**(e)/(f)** Praised endlessly for "works out of the box"; criticised by experts for POSIX incompatibility — the price of opinionated defaults.

## 10. Raycast

**(a) First 60 seconds.** Raycast's quickstart is a 7-step behavioral ladder rather than a settings tour: 1) search your system from Root Search via your hotkey, 2) open the Action Panel (⌘K) "to discover every action available for the selected item," 3) create a Quicklink, 4) set your own hotkey (tip: ⌥+first letter of the app), 5) chat with AI (Tab from Root Search = Quick AI), 6) install a Store extension ("try something fun like GIPHY"), 7) "over the next few days, try" five everyday essentials — Clipboard History, Calculator, Emoji, Snippets, Window Management (https://manual.raycast.com/quickstart).

**(b)/(c)** No account needed for core use. The Action Panel is the discovery engine — the same "every action is searchable and shows its shortcut" trick as VS Code/Zed/Warp, applied to the OS.

**(d)–(f)** Often cited as best-in-class onboarding because it sequences *habits over days* ("Practice these basics for a few days and you'll start to feel Raycast becoming part of how you move around your computer") instead of front-loading features. The design lesson: onboarding is a curriculum spread across the first week, ending with `@manual` in AI chat as the perpetual help surface.

## 11. Nushell

Nushell's first run shows a banner that links directly into the configuration docs — the book notes a section "linked directly from the banner message" explains how to remove it (`config nu` → set `$env.config.show_banner = false`) (https://www.nushell.sh/book/configuration.html). Discovery is structural: typing `$env.config` prints every setting; `config nu --doc | nu-highlight` pages abbreviated per-setting docs *inside the shell*. Same pattern as Ghostty's `+show-config --default --docs` — config-as-self-documenting-UI — executed inside the REPL.

---

## Synthesis: transferable patterns (with the best-in-class example)

1. **Usable before login.** Gate credentials at the moment of need, not at launch. Best: Warp post-2024 ("skip and start using immediately"); Codex (login inside the tool on first `codex`). 
2. **Offer the BYO-key escape hatch alongside the account.** Best: Codex/Claude Code dual auth (ChatGPT sign-in *or* API key; "requires additional setup" honesty).
3. **The empty state is a menu, not a void.** Best: Zed's welcome page (quick actions, self-dismisses after first folder) and VS Code's Welcome page that retires itself.
4. **A task ladder beats a tour.** Best: Claude Code's 8-step quickstart (install → first question → first change → git) and Raycast's 7 habits over days.
5. **Seed the first prompt.** Best: Cursor's "explain the codebase" first task; Warp's "What can I do with the AI panel?" starter; Claude Code's "ask a question about your code."
6. **A literal token to switch modes.** Best: Warp's `#` for natural-language-in-terminal; Raycast's Tab-from-Root-Search for Quick AI.
7. **The command palette as shortcut tutor** — every action searchable, shortcut displayed next to it. Best: Zed ("gateway to every action"), Raycast's Action Panel (⌘K).
8. **Teach through interaction, not modals.** Best: fish's gray autosuggestions and live syntax highlighting; Warp's blocks navigable by arrow keys; Warp's behavior-triggered tips under the warping indicator.
9. **Zero-config defaults + self-documenting config for experts.** Best: Ghostty (`ghostty +show-config --default --docs`) and Nushell (`$env.config`, `config nu --doc`).
10. **Import the expert's existing life.** Best: Cursor's VS Code import (extensions/settings/keybindings); Warp's iTerm2 profile importer with PS1-vs-Warp-prompt choice; Zed's per-editor migration guides and vim/helix modes.
11. **Make trust an explicit, memorable event.** One clear "do you trust this folder?" + visible autonomy tiers (read-only → workspace-write → full access). Best: Codex's Suggest/Auto Edit/Full Auto ladder and sandboxed default; Claude Code's plan/auto/manual modes with classifier.
12. **Onboarding is a platform, not a page.** Best: VS Code's Walkthrough API — extensions ship checklists with completion tracking; completion reconfigures the home surface (recents ×2).
13. **Machine-readable docs as the new onboarding surface.** Warp, Claude Code, OpenAI and Zed docs all publish `.md` versions and `llms.txt` indexes — agents now onboard *other agents' users*, so docs are part of the first-run funnel.
14. **Tips that know when to stop.** Contextual, behavior-triggered hints (Warp's agent tips; Claude Code's `?` hint) over always-on tip windows (iTerm2's Tip of the Day is the dated version of this).

## Anti-patterns (with who got burned)

1. **A login wall at the blank-slate moment.** Warp 2022 — the single most-cited onboarding failure in the genre; took two public reversals (telemetry opt-out Dec 2022, optional login Nov 2024) and a competitor (Ghostty) to fix. HN: "a login for a terminal is insane."
2. **Default-on telemetry in a terminal.** Warp, same era; "terminal sends your data" is a trust-destroyer that overshadows every feature.
3. **Account requirements for local, offline-capable features.** Zed's Zeta-sign-in provoked a literal fork that strips the sign-in gate. Rule: if the feature works offline, don't gate it.
4. **Nag dialogs that train unsafe muscle memory.** Claude Code's trust-dialog option-order change drew issue #90582 precisely because re-prompting every session (or reversing defaults) teaches users to hit Enter reflexively. Persist decisions; put the safe action first; never re-ask without new information.
5. **Approval fatigue as the default mode.** Early Codex/Claude Code manual-approve-everything flows produced click-through blindness; both had to add classifier/allowlist "auto" tiers. If your safety model depends on users reading 50 prompts a day, it isn't one.
6. **Overwhelming the command surface.** Claude Code hides slash commands "by design"; VS Code walkthroughs cap steps. An onboarding UI that dumps every feature is a spec sheet, not teaching.
7. **Silent breaking changes to the config contract.** Ghostty's v1.2.3 `config`→`config.ghostty` rename was minor, but it's the pattern to avoid: experts forgive missing features, not moved furniture.
8. **Assuming your user already knows the terminal.** Claude Code needed a dedicated "never used a terminal" guide; the blank-slate moment is where novices churn. If your docs start at `cd`, your onboarding starts too late.

**Bottom line for Relay:** the winning composite is Ghostty's zero-config launch + Zed's self-dismissing welcome page with quick actions + Warp's `#`-token AI affordance and behavior-triggered tips + Claude Code's trust-once dialog and BYO-key login + Cursor/Zed's import-the-expert's-life, all delivered as a checkable walkthrough (VS Code/Raycast) that retires itself when done.
