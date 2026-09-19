# The switchboard aesthetic inside Relay (proposal, 2026-09-17)

Card `#8E4Q`. **Mostly still a proposal.** Two parts of it are built: the Switchboard's typography
(intervention 3, 2026-09-17) and, since 2026-09-19, the board's **materials** — the `[board]` theme
tokens of §3.4, the face and hardware they paint, and the empty board of intervention 5 (owner, on
review item D7: "yeah build that out"; `docs/qa_evidence/2026-09-19-switchboard-materials/`). Each
built part carries a note where it is proposed. Everything else here — the pane-header jack strip,
the lamp, remote pairing, the icon — is still design, and no other source file has been touched.
Source references updated 2026-09-18, when `src/main.cpp` was split into one header per unit: they
now name the function and its file, because the original line numbers had already drifted.
Companions: `docs/SWITCHBOARD-DESIGN.md` (the tracker), `site/index.html` + `site/style.css` (the
hero patch panel, shipped), `issues/features/2026-09-17-color-themes.md` (`#0JA7`, themes),
`issues/features/2026-09-17-remote-phone-and-multiplayer.md` (`#W5N2`, phone pairing).

## 1. The claim

The metaphor is not a coat of paint we are considering. It is already the product's data model:
`relay::input::LineTarget` is a destination, `applyDestinationColor()` (`src/Pane.h`) paints
the caret cyan or violet by where the line is patched, and the tracker is called the Switchboard.
The website now says this literally — a bakelite face, brass jacks, a cord that re-patches
(`site/style.css:230-275`).

And yet the word is entirely unclaimed in the GUI vocabulary: `cord`, `jack`, `lamp`, `patch` and
`switchboard` do not appear once in `src/` or `engine/` (the only `operator` is
`ShellHighlighter.cpp:19`, a shell `|`). Every naming decision here is still free.

So the question is not *whether* the metaphor belongs in the app. It is where it is allowed to stop
being a word and become a **material**: brass, bakelite, enamel, cord, lamp. My answer: materials
live in **chrome and in rare moments**; they never touch the working surface. A terminal is looked
at for six hours; a charming texture is a tax paid 10,000 times.

## 2. The line between metaphor and costume

Three tests. A candidate has to pass all three.

1. **The all-day test.** Will the user see this more than ~50 times an hour? If yes it may carry a
   *word*, a *glyph* and a *colour* — never a *material*. Materials are for things seen a few times
   a session.
2. **The information test.** Does the ornament carry state that is not otherwise visible? A lamp
   that means "a turn is running" earns its pixels. A brass screw head does not. Decoration that
   encodes nothing is costume by definition.
3. **The hairline test.** Turn every brass fill into a 1px `@border` circle and every bakelite
   panel into `@surface`. Is the widget still fully legible and fully usable? If not, the metaphor
   was load-bearing for *comprehension*, which is exactly what it must never be (see §6, "the
   under-40 problem"). This test is also literally how the plain/light themes are implemented (§4.4).

### 2.1 Surfaces that may carry it

| Surface | Why it qualifies |
|---|---|
| Pane header line (`src/Pane.h`, today a bare `m_cwdLabel`) | Seen constantly but *read* rarely; it is the one place that states this pane's destination |
| The Switchboard pane's column headers and empty state (`BoardPane::buildColumn`, `rebuild`) | A board, opened deliberately, closed again |
| Remote pairing and connect/disconnect (`#W5N2`) | A phone pairing with a desktop **is** a call being patched through. Rare, ceremonial, currently unbuilt |
| First-run / onboarding | Seen once |
| The app icon and the `.desktop` entry (`data/icons/org.relayterminal.Relay.svg`) | Seen at 32px, outside the app |
| An About box (there is none today — `relay --version` is the only version surface) | Seen once, by choice |
| Sound | Opt-in only, default off |
| The bell / notification centre (`NotificationsPopup`, `src/WindowChrome.h`) | Rare, and already a telecom noun |

### 2.2 Surfaces that must stay plain — a hard list

- **The terminal grid.** `engine/view/TerminalView.cpp:521` `paintEvent`, `:536` `paintRow`,
  `:696` `paintCursor`. No texture, no vignette, no scanline, no phosphor bloom, no bakelite grain,
  no faux CRT curvature, ever. Every pixel here is the user's own output.
- **The prompt box.** `QPlainTextEdit#composerEditor` (`src/Theme.cpp:146`) and the composer frame.
  Its only decoration is the destination caret it already has.
- **Syntax colouring.** `src/ShellHighlighter.cpp` and `InputHighlighter::colorFor`. Brass never
  appears within 8px of syntax-coloured text — two unrelated colour systems adjacent to each other
  read as noise.
- **Card body and thread text** in the Switchboard (`BoardPane` `CardDetail`, both reusing
  `#filePreviewMarkdown`), the turn log (`#turnLog`), the plan editor (`#planText`), file preview.
- **The tab bar.** `src/Theme.cpp:295-301`. It is a title bar in a frameless window; it is hit with
  the mouse dozens of times an hour and must stay a flat row of words.

The rule behind the list: **anything whose pixels are the user's own text, or that the mouse aims
at repeatedly, stays on Relay's neutral surface and its semantic colours.**

## 3. The visual language

Four materials, and only four. Anything not on this list is out of scope by construction.

### 3.1 Materials

- **Bakelite** — the board face. `linear-gradient(#17140f → #0f0d0b)` with a 1px top edge of
  `rgba(200,164,92,0.16)`. Already exactly `site/style.css:237-244`. In Qt: a `QFrame` with
  `qlineargradient` in QSS; no image, no tiling.
- **Brass** — jack rings, lamp bezels, the rule under an engraved label. Highlight `#c8a45c`,
  mid `#7d6738`, shadow `#6b5637`. Permitted only on **circles ≤ 22px and 1px rules**. Never a
  fill larger than a chip.
- **Enamel label** — the one new text treatment: a `#131519` strip, 1px `@border`, text in mono,
  uppercase, `letter-spacing: 1px`, `@muted`. The Switchboard's column headers and the pane's
  destination word wear this. Cheap: pure QSS.
- **Cord** — a 2.4px stroke, `stroke-linecap: round`, a single cubic curve. Drab `#6b5a3c` when
  idle, the destination colour when live. **At most two cords visible anywhere in the app.** The
  moment there is a bundle, it is a wallpaper.

### 3.2 The palette that results

Relay already runs three colour axes: neutral chrome (`Background`/`Surface`/`Border`/`Text`,
`src/Theme.h:11-19`), semantic destination (`#3ec5f0` shell / `#b48ef7` agent,
`src/Theme.cpp:206-207`), and status (`#7ec88c` ok, `#e5c07b` warn, `#e06c75` error, `#f7768e`
recording). Brass is a **fourth axis and it is non-semantic**: brass means "this is the physical
board", never "this is a state". That single rule is the whole discipline. Three tokens in
`src/Theme.h`, beside the existing ones:

```cpp
inline const QColor Brass{0xc8, 0xa4, 0x5c};      // jack ring, lamp bezel, engraved rule
inline const QColor BrassDim{0x6b, 0x56, 0x37};   // unlit ring, idle cord
inline const QColor Bakelite{0x17, 0x14, 0x0f};   // board face
```

**As built (2026-09-19)** these are *variables*, not constants, because a colour picked for
near-black chrome cannot survive a light theme (§3.3): `relay::theme::BoardMetal`, `BoardMetalDim`
and `BoardFace`, assigned per theme from its `[board]` table. The three values above are exactly
what Relay Dark's file names, so the arithmetic in this section still holds for that theme.

Contrast on the dark theme (`#0f1115`): brass **8.0:1**, shell cyan 9.4:1, agent violet 7.3:1,
`@muted` 6.0:1. Brass on bakelite is 7.8:1. All comfortably past AA; brass is legible as *text*,
which is what makes the enamel label honest rather than a texture.

**Rule of two.** Brass is structure; cyan/violet is state. A jack ring is brass; the halo *around*
it is cyan or violet. Exactly one element in a view carries a destination colour at a time — which
is already true of the status strip today (only `#stripChip[dest=…]` does).

### 3.3 Light theme (`#0JA7`)

Brass on white is **2.35:1**. It does not survive inversion, so it must not be inverted — it must
be **oxidised**: `#6b5637` on a phenolic cream `#f3efe7` is **6.1:1**, and on white 7.0:1. The
board face becomes cream, the metal becomes dark bronze. This is one token swap, not a second
design.

A separate finding for `#0JA7` while we are here: **the destination colours themselves fail in
light.** `#3ec5f0` on white is 2.01:1 and `#b48ef7` is 2.58:1 — unreadable as chip text. The light
theme needs its own pair; `#0a6183` (6.6:1) and `#5c3f9e` (7.6:1) hold the same hue relationship
and both pass AAA-for-large. This is true with or without any of this proposal.

### 3.4 The materials as theme data, and degrading to a plain theme

> **Built, 2026-09-19** (owner on review item D7: "yeah build that out"; commits `5755a30` and
> `a634e71`, evidence in `docs/qa_evidence/2026-09-19-switchboard-materials/`). What this section
> proposed is now the `[board]` table in every shipped theme, read by `src/ThemeFile.cpp` into
> `ThemeSpec::board` and adopted into `relay::theme::BoardFace` / `BoardMetal` / `BoardMetalDim`
> (`src/Theme.h`). The spellings that shipped are `face`, `metal`, `metal_dim` and the flag is
> `[flags] board_material`, default **true** for every theme rather than for Relay Dark alone —
> which is safe because a theme that names no `[board]` table has all three *derived from its own
> chrome* (`docs/THEMES.md` §6) and every derivation is contrast-measured against the same rules as
> a hand-picked one. Nothing is ever borrowed from Relay Dark here: its bakelite is any dark
> window's own colour again, and its brass is 2.35:1 on white.
>
> **What wears them today.** The board pane's ground is the face — one stylesheet rule on
> `#boardView`, with the list pane, the header and the card detail transparent so the sheet runs
> behind the pane unbroken. The engraved rule over each section name and the two rules that frame
> the list are `metal_dim`, and the rule of the section under the pointer is `metal`, so exactly one
> piece of hardware in the pane is ever lit. A row's hover band and the drag image are mixed from
> the face. The empty board (intervention 5) draws one unlit jack per section. Card and thread text
> stay on `@surface` and `@text`: §2.2 is unchanged, and the face is measured as a text ground in
> `tests/theme_test.cpp`, exactly like `background` and `surface`.
>
> **Not built, deliberately:** the pane-header jack strip and its lamp (interventions 1 and 2, still
> the "build first" item, and a different surface), remote pairing jacks (4, waiting on `#W5N2`),
> and the cord — the board has none, and the drag indicator stays the accent because it is live
> state, not hardware.

Themes ship as data (`#0JA7`). Add a `board` group with three colour tokens (`board.face`,
`board.metal`, `board.metalDim`) and one boolean `board.material`, default `true` on Relay Dark.
`board.material: false` gives:

| Material | Plain fallback |
|---|---|
| Bakelite face | `@surface` |
| Brass ring | a 1px `@border` circle, `@accent` when lit |
| Lamp | a filled 6px dot in the status colour |
| Enamel label | `@muted` uppercase mono, no strip |
| Cord | a 1px `@border` line, destination-coloured when live |

Nothing moves, nothing resizes, no layout changes — only fills. Every "do" in §4 is drawn so that
this substitution is lossless. A design that cannot be turned off this cheaply is not shippable in
a themeable app.

### 3.5 One coherence debt to name

The site is Space Grotesk + IBM Plex Mono; the app picks Hack / JetBrains Mono / DejaVu
(`src/Theme.cpp:33`); the remote web client (`app/style.css`) uses a *third* palette — `#7aa2f7`
accent and `12px` radius, matching neither. Before adding brass anywhere, `app/` should be moved
onto Relay's tokens. Ornament on top of three inconsistent palettes is just more inconsistency.

## 4. Ranked interventions

Each: what, where, Qt cost, wear risk, verdict.

### 1. The pane's line indicator — a lit jack in the pane header · **DO**

The pane header is `m_cwdLabel` (`src/Pane.h`), a bare path label. Replace it with a strip
that states what this pane's line is patched to.

```
┌─────────────────────────────────────────────────────────────┐
│ ◉─────╮   ~/repos/relay-terminal                    ⠿ ◫+ ⬓+ │
│       ╰──◉ AGENT · glm-5.3                                  │
├─────────────────────────────────────────────────────────────┤
│  $ git status --short                                       │
│   M src/greet.c                                             │
```

Left ring = your line (always brass). Right ring = the destination, brass with a cyan or violet
halo; the cord between them is one cubic curve. The word `AGENT` / `TERMINAL` is in enamel type
beside it, so the colour is never the only channel. **Cost:** one custom `QWidget` of ~80 lines
(`paintEvent` with two `drawEllipse` and one `QPainterPath`) plus a QSS block; the state already
exists — `m_modeChip`'s `dest` property is set in `src/Pane.h`. **Wear risk: low.** It
replaces a label with a label that says more; there is no motion and it is in peripheral vision.
**Passes the hairline test** — grey rings and a grey line still read.

### 2. The busy lamp · **DO**

A 10px brass-bezelled lamp on that same strip, lit amber while a turn runs, dark otherwise. The
plumbing exists: `m_turnClock` already ticks once a second (`src/Pane.h`), and
`#requestsChip[state=…]` already encodes running/done/attention (`src/Theme.cpp:167-171`). Today
"the agent is thinking" is read from the `✦ thinking…` fold line in the terminal's own grid (issue
T8CN; the floating `#thinkingOverlay` this section argued against is gone); a lamp says the same
thing from the pane header, where it is still visible once the grid has scrolled on. **Cost:** ~25 lines inside the same widget. **Wear risk:
low if it does not blink** — a steady fill, or at most a 1 Hz opacity breath, and *nothing* under
reduced motion (§6). **Verdict: do, immediately after #1 — they are one widget.**

### 3. Engraved column headers and card chips in the Switchboard · **DO**

> **Built, and then the layout changed.** The columns became one sectioned list of rows
> (SWITCHBOARD-DESIGN 4.6, 2026-09-18). The engraved label survives as the section header,
> painted by `RowDelegate` from the same tokens with a hairline rule above it; the QSS names
> are now `#boardList`, `#boardListPane` and `#boardCount`.

`BoardPane::buildColumn` (`src/BoardPane.cpp:732`) named `#boardColumnHeader`, and `CardDetail`
names `#boardCardTitle`, `#boardCardMeta`, `#boardTasks`, `#boardProblems` — **none of which
`src/Theme.cpp` styles at all today.** The pane currently inherits generic widget chrome. So this
is not "add ornament", it is "the pane is unstyled and here is the style": enamel headers, a
hairline rule under each, columns as flat `@surface` strips. **Cost: QSS only**, ~12 lines appended
after `src/Theme.cpp:318`. **Wear risk: none** — it is typography. **Do it whether or not the rest
of this document is approved.**

**Status (2026-09-17, UX pass):** done in plain materials only: enamel-style headers (uppercase mono,
letter-spaced, `@muted`) on flat `@surface` column strips, cards painted from the theme tokens, the
`board*` rules at the end of the stylesheet in `src/Theme.cpp`. No brass, so nothing here depends on
the `board.*` theme group of section 3.4. See `docs/SWITCHBOARD-DESIGN.md` section 4.5.

### 4. Remote pairing as a call being patched through · **DO (restrained)**

`#W5N2` is the one place where the metaphor is literally true: a phone asks for a line, the desktop
operator confirms, the call is connected. `docs/REMOTE-AND-MULTIPLAYER-DESIGN.md:210` already
describes the QR + five-digit confirm, and `:259` asks for a persistent "2 remote devices
connected" indicator. Give that indicator a jack strip in the window chrome (beside the bell,
`windowChromeRight` in `src/RelayWindow.h`): one brass ring per connected device, lit; a connect draws the cord in over
180ms, a disconnect drops it. **Cost:** a small painted widget plus a QSS block; the pairing screen
itself is new work regardless. **Wear risk: low** — a pairing happens a handful of times a year.
**Do**, with the hard constraint that the confirm dialog stays a plain dialog: the five-digit code
is a security decision and must not be decorated.

### 5. The empty Switchboard — an unpatched board · **DO**

`BoardPane::rebuild` (`src/BoardPane.cpp:785-789`) currently shows one centred sentence,
"No cards yet. Press n to add one." An empty state is seen twice, is not read all day, and is the
cheapest place in any app to have a point of view. Relay has almost none — the notification popup
(`NotificationsPopup`, `src/WindowChrome.h`) and the Tasks panel (`src/RequestsPanel.cpp:190`) are the only two written
with any care, and a brand-new pane has no welcome at all.

```
        ◉      ◉      ◉      ◉      ◉      ◉
      INBOX  DISCUSS  READY  DOING  WAIT   QA

              nothing patched through yet

              press  n  to open a card
```

Brass rings over enamel column names, all unlit. **Cost:** one `QWidget` in `BoardPane`, ~50 lines,
or a single SVG plus a label. **Wear risk: none** — it disappears the moment there is a card.

> **Built, 2026-09-19** (`EmptyBoard` in `src/BoardPane.cpp`, ~90 lines including the layout): one
> jack per *section* — the columns became sections in 2026-09-18's layout — each a `metal_dim` ring
> with a small dark hole, over its name in enamel type, then the pane's own two lines of words. The
> names are drawn when they fit and the columns that do not fit are left off rather than elided to
> stubs; below three columns' room the rings stand alone. It is the widget carrying it disappearing
> the moment there is a card that lets it past the taste guard in §6.

### 6. The app icon — a jack, not a chevron · **MAYBE**

Today the icon is a cyan chevron plus a spark node (`data/icons/org.relayterminal.Relay.svg`),
which reads as a generic terminal. A brass jack ring with a cyan cord entering it would be
distinctive at 32px and would tie the icon to the site. Against: the chevron already matches the
`›` prompt mark used throughout the product and on the site's feature list, and icon changes are
expensive to unwind (three SVGs, eight rasterised PNG sizes, `.desktop`, metainfo, packaging, and
`setWindowIcon` in `main()`, `src/main.cpp`, which the window chrome reads back to draw the header mark).
**Verdict: maybe** — worth a
side-by-side at 24/32/48px, not worth a decision today. The small variant
(`org.relayterminal.Relay-small.svg`) is the real test: at 16px a jack ring becomes a dot.

### 7. Pane header / tab bar as a bakelite strip · **MAYBE for the header, NO for the tab bar**

The pane header strip from #1 may take a bakelite ground — it is a thin band, rarely clicked. The
**tab bar must not**: `src/Theme.cpp:295-301` is a title bar in a frameless window, it is a mouse
target dozens of times an hour, and a warm brown band across the top of a cool-grey app would fight
every screenshot. It also fails the information test outright.

### 8. Status strip chips as engraved brass plates · **NO**

`#stripChip` (`src/Theme.cpp:202-216`) is the most-looked-at chrome in the product: cwd, mode,
context, model, microphone, tasks, all in one row under the prompt, all re-rendered as you type
(`applyDestinationColor` runs on every router verdict). Six warm plates under the prompt box would
dominate the composer and fight the destination colour that actually means something. Fails tests 1
and 2. **The chips stay exactly as they are.**

### 9. Onboarding — one screen of the board · **MAYBE**

There is no onboarding today. If one is ever built, its first screen is the right place for the
full patch panel: one line in, two jacks out, click one. It is the site's hero, reused, and it
teaches the one thing a new user must understand. But building onboarding *in order to* have
somewhere to put the metaphor is the tail wagging the dog. **Maybe, contingent on onboarding
existing for its own reasons.**

### 10. Notifications as an annunciator drop · **NO**

`#notificationsPopup` / `#notificationRow` (`NotificationsPopup` in `src/WindowChrome.h`, styled at
`src/Theme.cpp:236-250`) already work and already carry coloured `#notificationDot` kinds. An
annunciator — the little flag that drops on an old board when a subscriber calls — is the single
most tempting idea here and the single worst fit: notifications arrive when you are busy with
something else, and a dropping flag is motion in the corner of the eye at the exact moment you must
not be distracted. Keep the bell (already a telecom noun), keep the dots. **No.**

### 11. A cord that animates between jacks on every mode change · **NO — refused**

See §5.

### 12. Sound · **NO by default, one opt-in**

A cord click on connect, a soft ring on a finished turn. There is no audio in Relay today
(`QApplication::beep()` at `src/RichEditor.cpp:220` is the whole of it), and every terminal user I
would trust has their terminal silent. If it exists at all: **one** sample, default off, in
Settings beside notifications, and never on mode changes or keystrokes. **Not worth building now.**

### 13. Bakelite grain, brass rails, screw heads, scanlines, a cord following the caret · **NO**

Listed so the answer is on record.

## 5. The first thing, and the refused thing

**Build first: the pane header line indicator with its lamp (#1 + #2).** It is one widget. Every
piece of state it needs already exists (`dest` and the turn clock, both in `src/Pane.h`).
It replaces the least considered surface in the app — a bare path label — with the only piece of
chrome that *is* the product's central idea, visible in every screenshot, and it makes the app and
the website recognisably the same thing. If nothing else here ships, this should.

**Refuse even if asked: an animated cord that swings between the two jacks on every mode change.**
Three reasons, in order of how fatal they are. (a) `refreshDestinationColor()`
(`src/Pane.h`) re-evaluates the destination on *every router verdict as you type* — an
auto-mode line can flip shell→agent→shell inside one sentence, so an animation is not an animation,
it is a strobe under the user's hands. (b) Destination feedback is the one signal in Relay that
must be instantaneous and pre-attentive; a 180ms transition makes a lie out of a fact that is
already true. (c) It is the purest costume in the whole list: it encodes nothing the instant colour
change does not already encode, and it fails all three tests at once. The cord in the pane header
is drawn, not animated; when the destination changes it is simply *already* in the other position
on the next paint.

## 6. Accessibility and taste

- **Contrast.** Numbers in §3.2/§3.3. Everything proposed clears 4.5:1 on dark; the light theme
  needs the oxidised bronze and a new destination pair before any of this lands. One caution: the
  site's idle cord `#6b5a3c` is **2.84:1** on Relay's background — acceptable for a decorative
  stroke, never for anything a user must find.
- **Colour blindness.** `#3ec5f0` and `#b48ef7` are *not* reliably distinguishable under deuter- or
  protanopia — both collapse toward a similar desaturated blue. They are the fourth channel, not
  the first. Meaning is carried by, in order: **the word** (`TERMINAL` / `AGENT` in enamel type),
  **the glyph** (`›` shell, `✦` agent — already the site's convention and already in
  `BoardPane::rowText`), **position** (shell jack left, agent jack right, fixed), and only then
  colour. Tested by turning the app greyscale: nothing becomes ambiguous.
- **Reduced motion.** There is no animation infrastructure in `src/` today — no `QPropertyAnimation`
  anywhere — which is a feature. Qt exposes no reduced-motion signal on Linux, so: a setting
  `appearance/animation` (`off` / `reduced` / `full`, default `reduced`), honouring
  `org.gnome.desktop.interface enable-animations` when gsettings is present. Under `off`, cords and
  lamps change on the next paint, full stop. Also flagged: the existing wrong-mode flash
  (`src/Theme.cpp:208-210`) is an unconditioned blinking fill; it should come under the same
  setting. Nothing proposed here blinks faster than 1 Hz under any setting.
- **The under-40 problem.** Most users have never touched a cord board, and a good number will read
  the rings as "power button" or "record". This is fine *provided the metaphor is never the
  explanation*. Every switchboard element is redundantly labelled in plain English; the jack strip
  says `AGENT`, not just a violet glow. The metaphor's job is memorability and coherence with the
  name and the site — not instruction. §2 test 3 is how we keep ourselves honest about that.
- **Taste, plainly.** The failure mode of this whole direction is a terminal that looks like a
  novelty theme. The guard is quantitative and belongs in review: **at most two brass circles and
  one cord visible in a default window**, and zero warm pixels inside the terminal grid or the
  prompt box. If a screenshot has more, the change is wrong regardless of how nice it looks.

## 7. Files this would touch, if approved

Rows marked **done** landed on 2026-09-19 (`5755a30`, `a634e71`).

| Change | File · line |
|---|---|
| Brass/bakelite tokens — **done**, as live per-theme variables | `src/Theme.h`, `BoardFace` / `BoardMetal` / `BoardMetalDim` / `BoardMaterial` |
| Board QSS — **done** (`@boardFace`, `@boardMetal`, `@boardMetalDim`); jack strip and lamp still to come | `src/Theme.cpp`, the Switchboard block and the token table |
| Theme token group `[board]`, `[flags] board_material` — **done** | `src/ThemeFile.cpp` (`boardTokenNames()`, the derivation), every file in `data/theme/themes/` |
| Light/plain variants | new, alongside whatever `#0JA7` lands |
| Pane header → jack strip | `src/Pane.h` (`m_cwdLabel`), new `LineStrip` widget |
| Destination feeds the strip | `src/Pane.h` (`applyDestinationColor`) |
| Lamp on/off | `src/Pane.h` (`m_turnClock` start/stop) |
| Switchboard column/card styling | `src/BoardPane.cpp:732` header, `:155-191` detail (QSS only) |
| Switchboard face and hardware — **done** | `src/BoardPane.cpp` `RowDelegate::paintSection`, `paintCard`, `RowList::startDrag` |
| Empty Switchboard — **done** | `src/BoardPane.cpp`, `EmptyBoard` |
| Remote device jacks | `src/RelayWindow.h` (`windowChromeRight`), plus `#W5N2` work |
| Icon (if #6 wins) | `data/icons/org.relayterminal.Relay*.svg`, `packaging/`, `.desktop` |
| **Not touched, by rule** | `engine/view/TerminalView.cpp`, `src/ShellHighlighter.cpp`, `#composerEditor`, `QTabBar` |
