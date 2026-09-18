# Colour themes: design, contrast, and the Dark Copper / IBM Beige audition

Card `#0JA7`. The theme **system** — the TOML format, the loader, live switching, the generated
Konsole schemes, the Settings › Appearance picker — is documented in `docs/ARCHITECTURE.md` §14
and lives in `src/ThemeFile.{h,cpp}` and `src/Theme.{h,cpp}`. This document is the **design**
side: the rules a theme must follow, how contrast is measured, what the four incumbents measure,
and the two themes the owner asked to audition as candidates for the preferred pair:

> "lets keep the 4 generic themes but add my two themes and see if they are good / distinctive
> enough to be the preferred themes." — owner, 2026-09-18

| file | role |
|---|---|
| `data/theme/themes/dark-copper.toml` | new — the dark candidate |
| `data/theme/themes/ibm-beige.toml` | new — the light candidate |
| `relay-dark`, `relay-light`, `gruvbox-dark`, `solarized-dark` | incumbents, **unchanged**; `relay-dark` stays the default |
| `docs/qa_evidence/2026-09-18-copper-and-beige-themes/contrast.py` | the measuring tool; every number here comes from it |
| `tests/theme_test.cpp` | the assertions (the existing theme test, extended) |

Reproduce every number: `python3 docs/qa_evidence/2026-09-18-copper-and-beige-themes/contrast.py
check` (or `table <id>`, or `compare`).

---

## 1. The three rules

**1. Cyan and violet are not a theme — they are the destination pair.** `shell` means *the
shell*, `agent` means *the agent*, in every theme. A theme retunes them for its ground; it never
repurposes them. In the incumbents `accent` equals `shell` (the accent *means* shell in Relay's
language). The two new themes deliberately separate them — the accent is chrome (copper, navy), so
cyan and violet only ever mean "where this line goes".

**2. Ground and chrome may have personality; the text surface stays neutral and the meaning
colours stay constant.** Hue carries information in a terminal — amber flags, red errors, green
strings — so a theme must not put its personality in the same hue as a warning. Character lives in
`background`, `surface_raised`, `border`, `border_strong` and `accent`. `surface` stays near-neutral.

**3. Colour is the fourth channel.** Meaning is carried by the word (`TERMINAL` / `AGENT`), the
glyph and fixed position first; colour reinforces it (`SWITCHBOARD-AESTHETIC.md` §6). This is what
makes it safe for IBM Beige to grey its destination pair, and why no surface may carry the
destination in colour alone.

---

## 2. What is measured, and the rule each pair follows

The pairs are the ones the app actually paints, with the colours it actually uses — including the
"ink on a filled chip" colours `src/Theme.cpp` derives with `inkOn()` and the `caution` blend, which
the tool replicates rather than guesses.

| rule | threshold | used for |
|---|---|---|
| **AA** | 4.5:1 | text a person reads |
| **UI** | 3:1 (WCAG 1.4.11) | an edge or glyph a person must find: the focused pane's outline, the cursor, ANSI 8 |
| **deco** | none | carries no information alone: the resting hairline `border` |
| **distinct** | CIELAB ΔE76 ≥ 20 | two colours that must not be mistaken for each other (below) |

The 59 graded pairs per theme:

- `text` and `text_muted` on `background`, `surface` and `surface_raised` — **AA**
- `accent` on `background` (the route label, the running queue use it as text) — **AA**
- `accent_text` on `accent` (a primary button) — **AA**
- `shell`, `agent` on `background` and on `surface_raised` (the mode chip) — **AA**
- ink on `shell`, `agent`, `warning`, `caution`, `selection` (the prefix, plan and secret chips,
  selected text) — **AA**
- `success`, `warning`, `error` on `surface_raised` (chips) **and on `background`** (the dot on a
  notification row, which is painted `@bg`) — **AA**
- every `[syntax]` colour on `surface` (idle composer) **and on `surface_raised`** — the *focused*
  composer is painted `@raised` (`QFrame#composer[relayActive="true"]`), so that is the ground
  syntax is read against while you type — **AA**
- `terminal.foreground` on `terminal.background` — **AA**; the cursor — **UI**
- ANSI 1–7 and 9–15 on the terminal ground — **AA**. ANSI 0 is exempt (a background colour in
  practice). ANSI 8 is **UI**: it is the colour programs pick *in order to* be dim, and forcing it
  to 4.5:1 would make it indistinguishable from ANSI 7.
- `border_strong` on `background` — **UI**

A theme may shade the grid instead of painting it flat: `[terminal] background_end` is the colour
the ground fades to at the bottom of the pane, `background` stays the colour at the top, and the
fade is a plain vertical gradient across the whole view. It is opt-in — no key, no gradient, and
no inheriting one from the fallback theme. Every rule above is then measured **at both ends**, not
just at the top (`theAuditionedThemesMeetTheirContrastContract` walks both), because text lands on
the bottom edge as often as the top. Only the default ground is shaded: a cell carrying its own
background colour still paints solid, so `\e[41m` is the same red everywhere on the page. The
gradient is the engine's own, drawn by `TerminalView::paintEvent`.

A theme may also declare a **chrome material**: `[flags] metal = true` (Dark Copper) or
`[flags] plastic = true` (IBM Beige), with a `[material]` table (`light`, `mid`, `dark`, `edge`,
`chrome_light`, `chrome_dark`). Raised chrome — buttons, chips, menus, the pane header — is then
painted as a face of that material rather than a flat fill, and the window behind it as the
chassis or the case it is set into:

| | metal | plastic |
|---|---|---|
| highlight | a specular line along the top edge | a broad soft band across the upper half |
| range | wide: it reflects | narrow: it scatters |
| pressed | the light turns round | the same, gentler |
| edge | the theme's own border | the two-tone `[bevel]` moulding |

It is for a theme whose chrome is a material, and it never touches the grid or a surface text is
typed on. Both ends of a face are grounds people read on, so the rules above are measured on
`material.light` and `material.dark` — and on `material.chrome_light` / `material.chrome_dark` —
as well as on `surface_raised`.

A tiled grain (brushed streaks, moulded stipple) was built for both and cut on sight: Qt paints a
stylesheet `background-image` on some widget classes and not others, so it landed on the chips
and never on the tab row, and an inconsistent grain reads as dirt rather than as a material.

**Distinctness** (the rule WCAG does not have): `accent` and `border_strong` against `warning` and
`error`; `surface_raised` against `warning`; `border` against `error`; and `shell` against
`agent`. The first four are the copper trap (§4); the last keeps the destination pair two colours
when a theme greys it (§5).

---

## 3. The incumbents, measured

Not changed — the owner asked for them to stay — but measured with the same contract, because an
audition needs a baseline.

| theme | failing pairs of 59 | worst pair | notes |
|---|---|---|---|
| `relay-dark` | **1** | `border_strong` on `background` **2.40:1** (UI, needs 3) | otherwise clean; tightest AA is `operator` on the focused composer, 4.57 |
| `relay-light` | **14** | `border_strong` **2.54:1**; `shell`/`command`/`token` on the focused composer **3.85:1** | the destination colour itself fails in the status strip and while typing |
| `gruvbox-dark` | 17 | ANSI 1 **2.69:1** | faithful to upstream Gruvbox, which is not an AA palette |
| `solarized-dark` | 42 | ANSI 8 **1.00:1** (it *is* the background) | faithful to upstream Solarized, which is low-contrast by design |

Two cheap fixes worth knowing about, **not applied** (out of scope for an audition):
`relay-dark` `border_strong = "#5d6675"` takes the focus outline to 3.26:1 and stays grey; and
`relay-light`'s shell colour needs to be about one stop darker to clear AA on its own chips —
`#086584` measures 5.45:1 on its `surface_raised` and 5.91:1 on its `surface`. Gruvbox and Solarized fail because they are faithful ports; making them pass would make
them not Gruvbox and Solarized.

Full six-way table: `docs/qa_evidence/2026-09-18-copper-and-beige-themes/contrast-all-six.md`.

---

## 4. Dark Copper

`data/theme/themes/dark-copper.toml`. **59/59 pairs pass. Worst pair 3.32:1 (ANSI 8, UI). Worst
AA pair 5.24:1** (`error` on a chip).

### 4.1 Where it comes from

The owner's brief was "rusty metal", then "rather than oxide, it could be more charcoal", then
"dark copper", with a reference: a 1950s operator headset on a manual switchboard
(`ref-switchboard.jpg` in the evidence). The photograph is lit by tungsten, which drags every hue
into 0–38°; discounting that, it says three things:

| sampled | measured | what it became |
|---|---|---|
| board metal, jack field | `#48322a`, hue 16°, value 28% | the chrome is **copper**, not brass — redder and much darker than the `#c8a45c` brass `SWITCHBOARD-AESTHETIC.md` assumed |
| board face | `#321919`, value 20% | the warm tier's direction (`surface_raised`, `border`) |
| the operator's headband | nickel — the only cool metal in the scene | `border_strong` |
| — | there is no cyan anywhere in the photograph | the ground stays cool charcoal, so the destination pair still sings |

### 4.2 Tokens

| token | value | measured |
|---|---|---|
| `background` | `#0e0f12` | cool charcoal |
| `surface` | `#15161a` | near-neutral: syntax sits on it |
| `surface_raised` | `#241c18` | copper-tinted; 1.62:1 against the ground |
| `border` | `#3a2822` | copper rule; 1.37:1 |
| `border_strong` | `#666f7a` | **nickel**, the focus ring: **3.76:1** (UI) |
| `text` / `text_muted` | `#ece6e0` / `#a2968c` | 15.5 / 6.6 on the ground; muted **5.80** on chips |
| `accent` | `#c07a4a` | aged copper: **5.59:1** as text; ΔE **28.5** from amber, **31.2** from red |
| `accent_text` | `#1a0f08` | **5.49:1** on the accent |
| `selection` | `#5a3a26` | copper-brown; its ink measures above AA |
| `shell` | `#45c8ee` | **9.8:1** on the ground, **8.57** on chips |
| `agent` | `#ab97f7` | **7.76:1** on the ground; about 8° bluer than Relay Dark's violet (below) |
| `success` / `warning` / `error` | `#7ec88c` / `#e5c07b` / `#e06c75` | **Relay Dark's, unchanged**; error on a chip 5.24 |
| `[syntax]` | as Relay Dark, command/token = shell, variable/agent = agent, `operator = #9a938a` | tightest: operator on the focused composer **5.51** (Relay Dark: 4.57) |
| `[terminal]` | ground `#0e0f12`, fg `#dad4ce`, ANSI as Relay Dark with 5/7/8/13/15 warmed | ANSI 8 **3.32** (UI) |
| `[board]` | face `#1a1210`, metal `#c08556`, metal_dim `#6b4a33` | metal on face 5.91 (enamel text) |

### 4.3 The trap: copper between amber and red

Copper (hue ≈ 22°) sits between `warning` (≈ 39°) and `error` (≈ 355°). A theme that put copper in
the chrome at the same brightness as those two would make every rule look like a warning. It is
kept apart two ways, both asserted by `copperStaysClearOfAmberAndRed()`:

| | on the ground | ΔE76 from `warning` | ΔE76 from `error` |
|---|---|---|---|
| `border` (copper rule) | **1.37:1** | — | 57.6 |
| `surface_raised` (copper face) | **1.62:1** | 77.0 | — |
| `accent` (the one bright copper) | 5.59:1 | **28.5** | **31.2** |
| `error` | 6.00:1 | | |
| `warning` | 11.10:1 | | |

The structural copper is always a dim *material* (under half of `warning`'s contrast, by
assertion); the one bright copper is a clearly different *colour* (ΔE ≥ 20, by assertion). The
meaning colours are Relay Dark's, unchanged — asserted too.

### 4.4 Violet on this ground

Yes, it needed shifting. On a copper-chromed ground the eye adapts warm, and a red-leaning violet
drifts toward brown and into the chrome. `agent` moves from `#b48ef7` to `#ab97f7`, about 8° toward
blue, which keeps it clear of both copper and `error` and gains contrast (7.76:1 against 7.44:1).
Cyan needs no hue move — it is already the farthest thing on the wheel from copper.

### 4.5 One deliberate deviation from the brief

The coordinator's brief put copper in `border_strong` too. It is **nickel** here instead, straight
off the reference: the focus ring then has a hue that appears nowhere else in the theme, which is
the strongest separation available for the one edge a person must always be able to find, and it
sidesteps the amber/red trap for that edge entirely (ΔE 57.6 / 54.3). If the owner prefers an
all-copper frame, `border_strong = "#8a6448"` measures 3.65:1 on the ground and stays ΔE 38.9 / 37.5 from amber and red; it is one line.

---

## 5. IBM Beige

`data/theme/themes/ibm-beige.toml`. **59/59 pairs pass. Worst pair 4.11:1 (`border_strong`, UI).
Worst AA pair 4.65:1** (`warning` on the ground).

### 5.1 Where it comes from

The owner's brief: "1992 PC beige", then "windows 1995 PC beige can be the light theme", then
"IBM beige", with a reference: a beige CRT and keyboard shot on a white studio background
(`ref-beige.jpg`). That background measures `#ffffff`, so its white point is neutral and its hues
can be taken literally. The whole case is one family — **hue 33–37°, saturation 12–39%**:

| sampled | measured | what it became |
|---|---|---|
| lit top surface | `#f0e4d4`, sat 12%, value 94% | `[bevel] light` |
| deck face | `#c5b296`, sat 24% | `background` (lifted to `#cdc0a8`) |
| keycaps | `#8e785d`, sat 35% | `[bevel] dark` |
| bezel recess | `#655244`, sat 33% | `border_strong` |
| **screen glass** | `#817e7d`, **sat 3%** | the reason `surface` is neutral |

That last row is the finding worth keeping: **on a real beige machine the only unsaturated surface
in the whole object is the screen.** The reference enforces Relay's own rule 2 for free.

### 5.2 Tokens

| token | value | measured |
|---|---|---|
| `background` | `#cdc0a8` | the case |
| `surface` | `#f3f2f0` | the screen: 2% saturation. Terminal, idle composer, editors |
| `surface_raised` | `#ded3bf` | a raised plastic face: chips, buttons, the focused composer |
| `border` / `border_strong` | `#9c8d74` / `#655244` | outline **4.11:1** (UI) |
| `text` / `text_muted` | `#1f1c16` / `#4d463a` | 9.47 on the ground; muted **5.19** (raised from 4.59 for margin: it is the status strip) |
| `accent` | `#1a3070` | Windows 95 navy: **6.88:1** as text, **11.04** under its label; also `selection` |
| `accent_text` | `#f3f2f0` | |
| `shell` | `#324d5c` | dark grey-blue: **4.98** on the ground, **6.03** on chips |
| `agent` | `#4f4163` | dark grey-violet: **5.16** on the ground, **6.25** on chips |
| `success` / `warning` / `error` | `#135427` / `#684800` / `#931c17` | **5.03 / 4.65 / 4.84** on the ground |
| `[syntax]` | command/token = shell, variable/agent = agent, flag/string/unknown = the status colours, `path #0f5f5a`, `operator #5a554a` | all ≥ 5.00 on the focused composer |
| `[terminal]` | ground `#f3f2f0`, fg `#1f1c16`, cursor `#23211a`, **ramp inverted** | ANSI 7 **8.44**, ANSI 8 **4.96** |
| `[bevel]` | light `#f0e4d4`, dark `#8e785d` | two-tone edges (§5.4) |
| `[flags]` | `bevel = true`, `square = true` | |
| `[board]` | face `#e0d6bd`, metal `#63492b` | brass oxidised to bronze (brass is 2.35:1 on white) |

### 5.3 The destination pair, which is the hard part

Relay Dark's pair is unusable on any light ground: `#3ec5f0` measures **2.01:1** on white and
`#b48ef7` **2.58:1**. The accessible pair proposed in `SWITCHBOARD-AESTHETIC.md` §3.3,
`#0a6183` / `#5c3f9e`, holds on white (6.88 / 7.90) but lands at **3.84 / 4.40** on this beige —
the ground this theme actually has.

The owner then asked for this theme specifically: **"terminal is dark gray-blue and agent is dark
gray-violet"** — a slate and a pewter, not a teal and a purple. On warm beige that is the right
instinct: a saturated pair reads as a modern web app dropped into a 1992 case. But the first cut of
that idea, `#354a59` / `#4c4460`, measured **ΔE 14.3 apart at identical lightness** (luminance ratio
1.01:1) — two greys a person would struggle to tell apart. A search over grey-blues and
grey-violets for the pair closest to the owner's that still clears ΔE 20, AA on both grounds and
matched lightness found `#324d5c` / `#4f4163`: **ΔE 20.3**, each within ΔE ~3 of the owner's
colours. ΔE 20 is about the ceiling for a grey pair of matched lightness; this is the pair that
**only just passes**, and the one to watch.

### 5.4 Bevels, not hairlines — the one `src/Theme.cpp` change

A 1px rule on paper reads as a stray mark; a two-tone edge reads as moulded plastic, and Windows 95
drew every control that way. The format could already *carry* this (`[flags]` and an unknown
`[bevel]` table land in `ThemeSpec::flags`/`extra` without a reader change), but the stylesheet
could not *draw* it. So `src/Theme.cpp` gains two flag-gated additions, and nothing else:

- `[flags] bevel = true` appends a block giving buttons, chips, menus and popups a raised edge
  (`[bevel] light` top/left, `[bevel] dark` bottom/right) and inputs, the composer and the pane a
  **sunken** one (the edges swap), so a text field reads as a well, not a button.
- `[flags] square = true` flattens every `border-radius` in the finished sheet in one pass.

Both are **off unless a theme sets them**, so every incumbent's stylesheet is byte-for-byte
unchanged — asserted by `theChromeFlagsAndBevelColoursAreRead()`.

### 5.5 Everything else re-derived, not inverted

Every status, syntax and ANSI colour keeps its hue and drops in luminance until it clears 4.5:1 on
the **darkest** ground it is painted on — which for status colours is the case beige behind a
notification row, not the lighter chip face (the first cut missed this: warning measured 3.78 there).
Amber has to become ochre to survive, and it is the tightest pair in the theme at **4.65:1**: this
is where a light theme is weakest, because a darker amber stops reading as "warning" and starts
reading as brown.

The ANSI ramp **inverts**: 7 and 15 are the darkest entries and "bright" means more contrast, not
more light, or `\e[37m` text vanishes on the page. Asserted by `theBeigeTerminalInvertsTheAnsiRamp()`.

---

## 6. The Switchboard materials in each new theme

`SWITCHBOARD-AESTHETIC.md` defines bakelite, brass, enamel and cord, and one rule — brass is
structure, never state. Both new themes carry a `[board]` table (face, metal, metal_dim) and a
`[flags] board_material` switch. **Nothing paints them yet** (the board widgets, card `#8E4Q`, are
unbuilt); they ride in `ThemeSpec::extra` so the widgets can read them when they exist.

| theme | material | lands as |
|---|---|---|
| Dark Copper | copper `#c08556` on `#1a1210` | brass becomes copper, off the reference; 5.91:1, legible as enamel text. The one theme where app chrome and board are the same material, so the polished metal is what separates them — and the "at most two brass circles in a window" guard loses force, because the whole window is warm. Check the board widgets against this theme first. |
| IBM Beige | bronze `#63492b` on cream `#e0d6bd` | brass does not survive a light ground (2.35:1 on white); it is oxidised, not inverted |
| incumbents | none | no `[board]` table: the widgets should fall back to the hairline form (`SWITCHBOARD-AESTHETIC.md` §3.4) |

---

## 7. The website, and one family

`site/style.css` is Relay Dark under other names: `--bg #0f1115`, `--text #e6e8ec`, `--muted
#8b919c`, `--shell #3ec5f0`, `--agent #b48ef7`, `--flag #e5c07b`, `--string #7ec88c` are the
theme's `background`, `text`, `text_muted`, `shell`, `agent`, `syntax.flag`, `syntax.string`
exactly; `--raised #161920` and `--line #242832` are within a couple of values of `surface` and
`border` and should be reconciled on the next site edit.

The family rules shared with trace.law, zrh-ai-econ.com and recode.ink — zero border-radius,
hairline rules, mono as a structural face, restrained saturated accents — are only partly
expressible in the app today: `[flags] square` is the first way to say "zero radius", and IBM Beige
is the first theme to use it. The incumbents keep the app's 4–10px radii.

---

## 8. Adding a theme

1. Copy the closest file in `data/theme/themes/` (or put it in `~/.config/relay/themes/`). The
   file stem is the id.
2. Set `[theme] variant` correctly — it decides which way "lighter" means away from the ground.
3. Put personality in `background`, `surface_raised`, `border`, `border_strong`, `accent`. Keep
   `surface` near-neutral.
4. Retune `shell` and `agent` for the ground. Do not repurpose them.
5. Keep the meaning colours' hues; move luminance only as far as the **darkest** ground they are
   painted on requires.
6. Re-derive `[syntax]` against `surface` *and* `surface_raised`, and the ANSI 16 against the
   terminal ground; invert the ramp on a light ground.
7. Run `contrast.py check <id>` until it is clean.
8. Optional: `[flags] bevel` / `square`, `[bevel] light/dark`, `[board]`.

---

## 9. What is asserted, and where

All in `tests/theme_test.cpp` (the existing theme test, extended):

| assertion | test |
|---|---|
| every shipped theme parses cleanly and is complete (existing — now covers the two new files) | `everyShippedThemeIsComplete` |
| every text/background pair in Dark Copper and IBM Beige meets its rule | `theAuditionedThemesMeetTheirContrastContract` |
| copper is dim where structural and a different colour where bright; meaning colours unchanged | `copperStaysClearOfAmberAndRed` |
| the destination pair stays two colours (ΔE ≥ 20), shell the cooler | `theDestinationPairStaysTwoColours` |
| the light terminal inverts the ANSI ramp | `theBeigeTerminalInvertsTheAnsiRamp` |
| bevel/square flags and bevel colours are read; off for every other theme | `theChromeFlagsAndBevelColoursAreRead` |

The ink-on-fill pairs (which need `Theme.cpp`'s `inkOn()`) are measured by the evidence script,
not the unit test, because the test binary links only the file reader.

---

## 10. The audition

Evidence: `docs/qa_evidence/2026-09-18-copper-and-beige-themes/`. All six themes, one fresh Relay
each, 1400×880, the same scene: a coloured `ls`, a failing `make`, an agent turn that runs `make`,
writes the fix (a real `write_file` diff) and answers (loopback stub provider, no network), a shell
line typed into the focused composer, and the status strip. Sheets: `contact-all-six-session.png`,
`head-to-head-dark.png`, `head-to-head-light.png`, `contact-composer-and-status-strip.png`.

### 10.1 A finding bigger than either theme: agent text ignores the theme

In both light themes, **the agent's answer is effectively invisible**: 1.13:1 on IBM Beige and
1.22:1 on Relay Light. The agent-turn lines are painted with 24-bit colours fixed for a dark ground
(then `src/main.cpp`, now `src/Pane.h`: `Ink::Agent` = `38;2;226;229;235`, and the `MarkdownAnsi`
instance); the prompt echo (Relay Dark's violet, 2.30:1 on beige), the model name (cyan, 1.80:1)
and the tool lines likewise do not follow the theme. It affects Relay Light exactly as much as IBM
Beige, so it does not decide the light head-to-head — but **no light theme can be the preferred
theme until it is fixed.** Cheapest fix: emit palette-indexed SGR (`\e[37m`, `\e[35m`, …) instead
of truecolor, so every theme's ANSI palette colours the agent turn; or build the `Ink` strings from
the live `Text`/`Shell`/`Agent`/`Warning`/`Success`/`Error`/`TextMuted` tokens on `themeChanged()`.
Out of scope here (`src/main.cpp`), not changed.

**Since fixed (2026-09-18):** `369725a` builds `MarkdownAnsi`'s eight colours from the live theme
tokens instead of a fixed dark palette, and `b0af7b3` writes the agent's Markdown in the terminal's
own colours, so every theme's palette now colours the agent turn. No hard-coded `38;2;226;229;235`
remains. Lines printed before a theme switch keep their colours: a terminal cannot recolour its
scrollback.

### 10.2 Dark Copper vs Relay Dark

- **Distinctive:** the composer's warm ground, the copper focus ring, tab underline and `auto`
  chip, the nickel pane outline. Side by side it reads as a different theme; alone, at a glance, it
  reads as *Relay Dark with a warm composer*. The terminal grid — most of the window — is plain on
  purpose, so the copper has little room.
- **Better than the incumbent on the numbers:** 0 of 59 pairs fail against Relay Dark's 1 (the
  focus outline, 2.40:1 → 3.76:1 here); worst AA 5.24 against 4.57 (the operator colour on the
  focused composer: 4.57 → 5.51).
- **Weak:** distinctiveness, above. Nothing only just passes; the tightest is ANSI 8 at 3.32 (UI).
- **Loses to Relay Dark:** in familiarity and in continuity with the website, which *is* Relay Dark.
- **Recommendation: keep as an option; not yet the preferred dark theme.** The one change that would
  most improve it: **`background = "#18120e"`** — a warm charcoal behind the tab row, the pane
  header and the frame, while `[terminal] background` stays cool `#0e0f12`. Measured: every pair
  still passes (worst: nickel outline 3.64:1), and the frame reads warm around a neutral grid, which
  is exactly rule 2. Not applied, so the owner judges the version in the screenshots.

### 10.3 IBM Beige vs Relay Light

- **Distinctive:** unmistakably. The beige case, square bevelled chips and buttons, a sunken
  composer with a navy focus ring, a neutral "screen" for the terminal. No other theme looks like it.
- **Much better than the incumbent on the numbers:** 0 of 59 pairs fail against Relay Light's 14.
  The destination colour in the status strip and while typing: 6.03:1 against Relay Light's 3.85.
  Focus outline 4.11 against 2.54.
- **Only just passes:** `warning` on the case beige, **4.65:1** (ochre is where a light theme is
  weakest — darker amber starts to read as brown); the destination pair's colour difference,
  **ΔE 20.3** (the owner's grey-blue/grey-violet at matched lightness: ΔE 20 is about the ceiling).
- **Weak:** the agent-text problem (§10.1), shared with Relay Light; the bevels are 2px and read as
  period detail rather than a heavy Win95 look — right for all-day use, but subtle in a thumbnail.
- **Recommendation: make it the preferred light theme once §10.1 is fixed; until then, keep it as an
  option** — so would Relay Light be, on the same evidence. The one change that would most improve
  it is not in its file: make the agent turn follow the theme.

### 10.4 What the harness could not show

The Switchboard pane opens in every theme but stayed on "Loading the Switchboard…" for the full 15
seconds in all six (a copied `issues/` tree in a sandbox with no git history), so the board shots
show only the pane frame and its filter field. The board widgets do not paint the `[board]`
materials yet anyway (§6).
