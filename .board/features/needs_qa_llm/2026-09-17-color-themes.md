---
id: 0JA7
type: work
status: needs-qa-llm
labels: [feature]
component: [gui, theme]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (Claude Code, color-themes worktree), 2026-09-17
rank: zz0j
created: '2026-09-17'
acceptance: switching theme in Settings restyles the app, the terminal and the composer colours without a restart
source: '`issues/feature_intake.txt`, 2026-09-17: "allow different color themes."'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-17-color-themes/, docs/qa_evidence/2026-09-18-copper-and-beige-themes/], related: [8E4Q], github: null}
---
# Colour themes

Ship a few built-in themes (the current dark, a light one, and one or two popular palettes) selectable in
Settings, applied to the app chrome, the terminal colour scheme and the composer's syntax colours together.
Themes are plain files so people can add their own; the engine and KonsolePart must use the same one.

## Format (owner approved, 2026-09-17)

One file per theme, in TOML, carrying both the 16-colour ANSI terminal palette and the UI tokens. Built-in
themes in `data/theme/themes/`, user themes in `~/.config/relay/themes/`. The Konsole/engine `.colorscheme`
is generated from the theme at runtime rather than being a second source of truth.

## Implemented (2026-09-17)

Four built-in themes — **Relay Dark** (the current colours, unchanged), **Relay Light**, **Solarized Dark**
and **Gruvbox Dark** — pick-able in **Settings › Appearance**, applying at once to the chrome, both terminal
engines and the prompt box.

**The format** — `data/theme/themes/<id>.toml`:

```toml
[theme]     name, variant ("dark" | "light"), description
[ui]        background, surface, surface_raised, border, border_strong, text, text_muted,
            accent, accent_text, accent_hover, selection, disabled,
            success, warning, error, shell, agent
[syntax]    command, unknown, flag, string, path, operator, variable, agent, token
[terminal]  background, foreground, cursor, background_intense, foreground_intense,
            palette = [ 16 colours: the 8 ANSI colours then the 8 bright ones ]
[flags]     per-theme booleans
```

Every key is optional except `[theme].name`: anything a file leaves out falls back to Relay Dark, so a user
theme that only sets an accent still renders (proved in the live run). Unknown tables, keys and flags are
kept verbatim in `ThemeSpec::extra` / `ThemeSpec::flags` rather than rejected, so the three material tokens,
the light "oxidised bronze" variant and the `board.material` switch proposed in
[`docs/SWITCHBOARD-AESTHETIC.md`](../../../docs/SWITCHBOARD-AESTHETIC.md) (issue `#8E4Q`, still
`discussing`) can be added later with no format change and no change to the reader: a new `[ui]` token only
has to be named in `uiTokenNames()` and given a value in `builtinDark()`, and a per-theme boolean is already
readable through `spec.flag("board_material", false)`.

**The library** — `src/ThemeFile.{h,cpp}` (`relay-theme`, ctest target `theme`) is plain Qt Gui: a small
TOML reader (comments, tables, bare and quoted keys, strings/booleans/integers, arrays on one line or
several), the token contract (`uiTokenNames()`, `syntaxTokenNames()`, `isComplete()`), discovery
(`themeSearchDirs()`, `discoverThemeFiles()` — the user folder shadows the built-in one), and the generated
Konsole files (`konsoleSchemeText()`, `konsoleProfileText()`, `konsoleNameFor()`). No widgets, so every rule
is testable without a window.

**Runtime tokens** — `src/Theme.h`'s design tokens were compile-time `const QColor`s substituted into the
stylesheet *and* read directly by painting code. They are now **variables** that `setActiveTheme()` assigns,
so `ChromeButton::paintEvent`, `SubagentsPanel`, `RequestsPanel`, `TurnTranscript` and `InputHighlighter`
follow a switch without a restart or a new widget. Five semantic tokens were added (`Success`, `Warning`,
`Error`, `Shell`, `Agent`) plus the nine `Syntax*` colours, and the colours those call sites had hard-coded
now come from them. `setActiveTheme()` rebuilds the `QPalette` and the stylesheet, regenerates the terminal
schemes, re-polishes every top-level widget and emits `theme::notifier()->themeChanged()`.

The two per-widget stylesheets that cached a colour (`RequestsPanel`, `SubagentTranscript`) moved into the
application stylesheet, and the stylesheet's hard-coded state colours became tokens (`@success`, `@warning`,
`@error`, `@shell`, `@agent`, `@caution`, and `@onWarning` / `@onShell` / `@onAgent` / `@onCaution`, the ink
that stays legible on a filled chip in either direction). Rules that had just landed — `paneTitle`,
`paneCwd`, `paneAuto`, `paneTitleEdit`, `stripChip[flash=…]`, `QWidget#pane[relayActive]` with
`@borderStrong` and `QFrame#composer[relayActive]` — are unchanged apart from taking their colours from
tokens.

**The terminal** — `data/theme/konsole/RelayDark.colorscheme` is **deleted**; there is no checked-in colour
scheme any more. Before `QApplication` starts, `exposeKonsoleProfile()` writes one `.colorscheme` and one
`.profile` per known theme into `$XDG_CACHE_HOME/relay/theme/konsole/`, plus a `relayrc` naming the chosen
one, and prepends that directory *and* `data/theme` to `XDG_CONFIG_DIRS` / `XDG_DATA_DIRS`.
`data/theme/konsole/Relay.profile` is the base those are built from, so the font, margins, scrollback and
link settings stay in one place. Every theme is written up front because Konsole builds its profile list
once; on a switch `KonsoleBackend::applyTheme()` only names the right profile through the Session object's
scriptable `setProfile()`, and **the running KonsolePart pane recolours in place**.
`EngineBackend::applyThemeColors()` reads the active `ThemeSpec` straight into the view, so Relay-engine
panes recolour in place too. Both XDG variables are still restored before each shell starts.

**The picker** — Settings › Appearance: the theme list (user themes marked "(yours)"), "Reload themes" and
"Your themes folder" (creates and opens `~/.config/relay/themes`). The actions palette renders the same rows,
so the picker is reachable from Ctrl+Shift+A without any new shortcut — and therefore owes no new
shortcut hint under the WARP.md rule (opening Settings already hints).

**Tests.** `tests/theme_test.cpp` (17 slots): the TOML subset including a multi-line array and an error that
does not lose the rest of the file; the built-in dark theme complete; **every shipped theme complete**, with
`relay-dark.toml` matching the compiled-in fallback colour for colour so the app still looks exactly as
designed; the light theme actually light; missing tokens filled from the fallback; a bad colour reported and
fallen back; unknown tables and flags surviving; a short palette refused; the user folder shadowing the
built-in; Konsole names, the generated scheme's 30 colour entries, the faint rule, and the generated profile
keeping everything but the scheme and the name. `./scripts/test.sh` **609 passed**; `ctest` **19/19**, the new
target included. No worker event was added, so `remote/wire.py` is untouched and `tests/test_remote_wire.py`
still passes inside `backend-and-bash`.

**Verified live** (Xvfb 1600x1000, isolated `XDG_CONFIG_HOME` / `XDG_DATA_HOME` / `XDG_CACHE_HOME`):

* `drive.sh` — **one** Relay process for the whole run, with `ls --color` output in the terminal and a
  half-typed shell line in the composer so every frame carries the ANSI palette and the syntax colours.
  `implementer-01` Relay Dark, then Gruvbox Dark, Relay Light, Solarized Dark and back to Relay Dark
  (`05`–`08`), then the light theme on every surface: main window, actions palette (filtered), a second tab
  and a split pane, the notification centre, a toast with the Settings window, the terminal's context menu,
  the keyboard-shortcuts dialog and a file explorer pane (`10`–`17`), and back to dark (`18`, `19`). The
  script prints `relay pid … never restarted` at the end.
* `drive-engine-and-user-theme.sh` — `RELAY_ENGINE=relay`, so every pane is Relay's own engine: the picker
  shows a user theme written into `~/.config/relay/themes/qa-hot-pink.toml` beside the four built-ins
  (`e02`), the engine pane recolours to Relay Light in place (`e03`) and then to the user theme (`e04`,
  `e05`), whose file sets only nine `[ui]` tokens, one `[syntax]` colour and two `[terminal]` colours — the
  rest falls back to Relay Dark, visibly.

* `drive-persists.sh` — Relay started with `theme/name=solarized-dark` already in `relay.conf`, which
  is what the picker writes: the chrome, the terminal and the generated `relayrc` all come up on Solarized
  Dark, so the choice survives a restart (`implementer-p01`).

All three stderr logs are clean.

## QA checklist

1. **The picker.** Settings (Ctrl+,) › Appearance lists Relay Dark, Relay Light, Solarized Dark and Gruvbox
   Dark, with Relay Dark selected on a fresh profile. The same row is reachable from the actions palette
   (Ctrl+Shift+A, type "theme").
2. **No restart.** With a terminal pane showing coloured output (`ls --color=always -la /usr/share`) and a
   shell command half-typed in the prompt box, switch to Relay Light. Without touching anything else: the
   chrome, the already-open Settings window, the pane frame, the tab bar, the running terminal's background
   and its ANSI colours, and the prompt box's syntax colours must all change at once. Relay must not restart
   and no new pane may be needed.
3. **Relay Dark is unchanged.** Switch back to Relay Dark and compare with a screenshot from before this
   change (`docs/qa_evidence/2026-09-17-bugfix-batch1/*.png` are from the old build). Nothing should have
   moved or changed colour.
4. **Every light surface.** On Relay Light, check: the actions palette, a menu (right click the terminal),
   the keyboard-shortcuts dialog, a file explorer pane and a file preview, a toast (Settings › General ›
   Reset shortcut hints), the notification centre behind the bell, the tab bar with two tabs, two panes side
   by side (the focused one's outline), the model and effort chips, the `!`/`*` prefix chips, the mode chip
   and its wrong-mode flash, the "auto" badge on a pane title, and a disabled button. Nothing may be light
   text on a light surface or a leftover dark rectangle.
5. **Both engines.** Repeat step 2 with `RELAY_ENGINE=relay` (or the palette's "New pane (Relay engine)").
   The engine pane must recolour in place as well.
6. **A pane opened after a switch.** Switch theme, then open a new pane (Ctrl+E) and a new tab (Ctrl+T).
   Both start on the chosen theme, not on Relay Dark.
7. **A user theme.** Copy `data/theme/themes/relay-dark.toml` to `~/.config/relay/themes/mine.toml`, change
   `[theme].name` and `[ui].accent`, then press "Reload themes". "Mine (yours)" appears and applies. Delete
   most of the file, leaving only `[theme].name` and one `[ui]` token: it must still be selectable and
   render, with everything else falling back to Relay Dark rather than going blank or black.
8. **A broken theme.** Put a syntax error, and then a nonsense colour (`accent = "not-a-colour"`), in that
   file. Relay must warn on stderr and keep rendering; it must not crash, and a theme it cannot read at all
   must fall back to Relay Dark rather than to no theme.
9. **A theme file with a name clash.** Name a user theme `relay-dark.toml`. It replaces the built-in one in
   the picker (one entry, not two), marked "(yours)".
10. **Restart.** Choose a theme, quit and start again: the same theme comes back, including in panes
    restored by "Reopen windows on start".
11. **Nothing leaks into Konsole's own config.** After a run, `~/.config/konsolerc`, `~/.local/share/konsole`
    and the user's own Konsole profiles must be untouched; the generated files must all be under
    `$XDG_CACHE_HOME/relay/theme/`. Open a shell in a Relay pane and check `echo $XDG_DATA_DIRS` — it must be
    the user's own value, with none of Relay's directories in it.
12. **Privacy and cost.** Nothing about a theme leaves the machine, and no model call is involved.

## Known gaps

- **A theme file added while Relay runs reaches new KonsolePart panes only after a restart.** Konsole builds
  its profile list once, at startup, so Relay writes every known theme's profile before `QApplication`
  starts. "Reload themes" restyles the app and re-reads the file for the app and for Relay-engine panes, and
  says so in its toast; KonsolePart panes opened after it still use the profile set that existed at startup.
  Editing a theme that already existed at startup is fine after a "Reload themes" for everything except a
  running KonsolePart pane's own colours, which follow on the next switch.
- The live switch of a running KonsolePart pane goes through the Session object's scriptable
  `setProfile()`. It is guarded (`indexOfMethod("setProfile(QString)")`), so a Konsole build without it
  simply leaves running panes on the previous colours until a new pane; verified working on
  `konsole-kpart` 23.08.5 / KF5.
- Terminal *scrollback that has already been drawn* keeps the escape-sequence colours it was written with;
  those are palette indices, so they re-map, but a program that emitted 24-bit colour keeps it. That is how
  terminals work, not a Relay bug.
- The generated `.colorscheme` derives Konsole's "faint" colours (the colour mixed a third of the way into
  the background) rather than letting a theme pin them. No built-in theme wants different ones.
- `[ui].selection` is used for both the Qt palette highlight and the stylesheet's
  `selection-background-color`; the text drawn on it is computed, not themeable.
- The three material tokens, the light bronze variant and `board.material` from `#8E4Q` are deliberately
  **not** implemented here; the format carries them without a change when that issue is decided.
- No keyboard shortcut cycles themes, so no new shortcut hint was added. If a "next theme" key is wanted
  later it needs a hint under the standing WARP.md rule.

## Audition: Dark Copper and IBM Beige (2026-09-18)

Owner, 2026-09-18: *"lets keep the 4 generic themes but add my two themes and see if they are good /
distinctive enough to be the preferred themes."* Implemented by Claude Opus 5 (1M context) in the
`agent-a6680b33734cf1eb4` worktree, on the theme system above. No second loader. The four incumbents are
unchanged and `relay-dark` stays the default.

**Added**
- `data/theme/themes/dark-copper.toml`: a charcoal ground with aged-copper chrome (`accent #c07a4a`, a
  copper-tinted `surface_raised`, copper rules) and a **nickel** focus outline, taken from the owner's
  switchboard reference. `shell`/`agent` stay cyan and violet, with the violet about 8° bluer. `warning`
  and `error` are Relay Dark's, unchanged.
- `data/theme/themes/ibm-beige.toml`: a PS/2 case ground sampled from the owner's beige-CRT reference, a
  neutral "screen" `surface`, Windows 95 navy as `accent`/`selection`, the owner's grey-blue/grey-violet
  destination pair (retuned to ΔE 20.3 apart), and every status, syntax and ANSI colour re-derived for
  the light ground, with the ramp inverted.
- `src/Theme.cpp` (+39 lines): two flag-gated additions. `[flags] bevel` adds two-tone bevels from a
  `[bevel]` table, and `[flags] square` flattens every border-radius. Both are off unless a theme sets
  them, so the incumbents' stylesheets are byte-for-byte unchanged (this is asserted).
- `tests/theme_test.cpp` (+7 cases): the contrast contract for both new themes, the copper/amber/red
  separation, destination-pair distinctness, the inverted ANSI ramp, and the flags. The existing
  `everyShippedThemeIsComplete` test now covers the two new files as well.
- `docs/THEMES.md`: the design rules, the measurement method, the incumbents measured, both themes'
  token tables with measured numbers, and the verdict.
- Evidence: `docs/qa_evidence/2026-09-18-copper-and-beige-themes/`. It contains all six themes in the
  same scene, the contact sheets, `contrast.py`, and the measured tables.

**Measured** (59 graded pairs per theme; `contrast.py check`)

| theme | failing | worst pair | worst AA pair |
|---|---|---|---|
| dark-copper (new) | **0** | 3.32 ANSI 8 (UI) | 5.24 `error` on a chip |
| relay-dark | 1 | 2.40 focus outline (UI, needs 3) | 4.57 operator, focused composer |
| ibm-beige (new) | **0** | 4.11 focus outline (UI) | **4.65** `warning` on the case beige |
| relay-light | 14 | 2.54 focus outline | 3.85 shell/command on the focused composer |

**Verdict** (`docs/THEMES.md` §10)
- **Dark Copper: keep as an option.** It is not yet distinctive enough to be the preferred theme: side
  by side it clearly differs, but on its own it reads as Relay Dark with a warm composer. The one change
  that would help most is `background = "#18120e"`, which puts warm charcoal on the frame and keeps the
  terminal cool. Measured, every pair still passes. It is not applied.
- **IBM Beige: the preferred light theme once the agent-text bug below is fixed.** It is unmistakably
  distinctive and fails 0 pairs where Relay Light fails 14. It only just passes on warning ochre
  (4.65:1) and on the grey destination pair (ΔE 20.3).

**Found, not fixed (outside this change)**
- **Agent-turn text ignores the theme.** `src/main.cpp:5693-5704` (`Ink::*`) and the `MarkdownAnsi` at
  `:7588` hard-code 24-bit colours chosen for a dark ground. The agent's answer measures **1.13:1** on
  IBM Beige and **1.22:1** on Relay Light, which is effectively invisible. This blocks any light theme
  from being preferred. The fix is to emit palette-indexed SGR, or to build the `Ink` strings from the
  live tokens on `themeChanged()`.
- `main` at `c0ac7f7` does not build: `main.cpp` includes the uncommitted `MarkdownAnsi.h`. The audition
  binary was built outside the tree with a CMake overlay (see the evidence README).
- `test_remote_wire.test_every_worker_event_is_classified` fails on `main` (`subagent_model` is
  unclassified). `relay-board.py check` flagged `2026-09-17-wrong-mode-hints.md` id `94U5` (fixed to `94V5`): `U` is not
  Crockford base32. Both were already on `main`, and this change touches neither.

### QA checklist (audition)

1. Settings › Appearance lists six themes. Both Dark Copper and IBM Beige apply live, with no restart.
2. Open `contact-all-six-session.png` and the two `head-to-head-*.png` sheets. Judge whether each new
   theme is distinctive against its incumbent.
3. Dark Copper: no copper rule or chip should read as an amber warning or a red error. The focus outline
   (nickel) should be findable. The terminal grid should carry no warm tint.
4. IBM Beige: controls should be bevelled and square, with a sunken composer and a navy focus ring. The
   destination chip should say TERMINAL or AGENT legibly. Check that the grey pair is still two colours
   (Ctrl+I).
5. Every other theme's chrome should be unchanged, with rounded corners and hairlines as before.
6. Run `python3 docs/qa_evidence/2026-09-18-copper-and-beige-themes/contrast.py check` and confirm that
   both new themes report 0 failures.
7. Run `ctest -R theme` and confirm that all theme cases pass.
