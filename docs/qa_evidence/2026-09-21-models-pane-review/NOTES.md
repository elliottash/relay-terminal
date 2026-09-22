# The owner's review of the models pane — card #MDL1, 2026-09-21

Implementer evidence for the five notes the owner left after reading the three tabs. `drive.sh`
is the whole run: Xvfb on a free display in 800–830, an isolated `HOME`/`XDG_*`/`TMPDIR` under a
short path, `RELAY_KEYRING=off`, fake keys for kimi code, z.ai, anthropic and gemini, and no
network turn — every step is a pane draw, a tab or a key.

The profile is **seeded with a lite list that names `gemini-3.5-flash-lite`**, because that is the
owner's report: membership of the lite list used to pin a model available, so the one model the
built-in list names could never be un-ticked.

## Commits

| sha | what |
| --- | --- |
| `7bef1f17` | only a terminal class pins a model available; `relay-lite` is drawn nowhere |
| `f6e0d1f5` | available: no recent section, providers alphabetically |
| `d6e5d2b6` | priorities: one page of sections, no class tabs, no lite |
| `cdc4b9bb` | providers: dividers, and the advanced dialog retired |

## What each shot shows

| shot | the note it answers |
| --- | --- |
| `a-providers-dividers.png` | **"tab 1: add horizontal line dividers between providers."** A rule in the theme's `@border` between kimi code, z.ai, openrouter, anthropic and google — each of which is a key row, a "models… (N of M available)" link and, for a guest, a permissions row. |
| `b-add-provider-and-warp-import.png` | the bottom of the same group: **+ add provider** (where a custom endpoint is added — the retired dialog's Base URL and Model ID) and **keys from warp** (its "Import keys from Warp" button), then the profiles. |
| `c-key-box-consent.png` | **the consent sentence, moved to where a key is entered.** "It is saved to the desktop keyring and sent only to this provider — never to Relay's server, and never written to a settings file." and "Your prompts and tool results go to this provider when the agent runs. The agent runs tools without asking: shell commands are not sandboxed and have your own permissions, and file tools are held to the pane's directory." |
| `d-options-no-advanced-row.png` | **"check the advanced provider settings."** Options › Models, searched for it: "Nothing matches “advanced provider”." |
| `d1-options-defaults-still-has-the-token-limit.png` | …and the one field of it that was already a row on the page is still there: **Output token limit**, under defaults. |
| `e-available-alphabetical-no-recent.png` | **"for available, remove the recent section. i would order the sections alphabetically."** anthropic (claude) · claude code · codex · google (gemini) · kimi · relay · z.ai (glm), in that order, with no "recent" block — and under **relay**, `relay-main` and `relay-flash` and **no `relay-lite`**. |
| `e1-gemini-flash-lite-found.png` | `gemini-3.5-flash-lite` on the available tab, ticked, with its full name drawn rather than elided. |
| `f-gemini-flash-lite-unticked.png` | **"it seems like i cant disable gemini flash lite."** The tick cleared, with the lite list still naming it. |
| `f1-available-after-the-untick.png` | the same, back in its provider's section. |
| `g-priorities-sections.png` | **"tab 3: … they should just be in divided sections. remove the lite section."** One page: a banded header per class — high (`/high, and plan mode`), main (`new panes start on rank 1`), flash (`/flash, and quick jobs`) — each with its own "in box" tick over that class's cutoff ticks. No second row of tabs and no lite section. |
| `h-each-section-offers-what-it-does-not-list.png` | typing `opus`: every section shows its own matches and then **"not in \<class\> — ctrl+enter adds it here"**. claude-opus-5 is offered under high (through claude code), main and flash; the guest row is offered under high and main and not flash, which is `addableToTier`. |
| `i0-highlight-in-the-flash-section.png` | the highlight on the row under the **flash** header, before the key. |
| `i-ctrl-enter-added-into-flash.png` | after Ctrl+Enter: claude-opus-5 is rank 2 of **flash**, and main and high are untouched. |

## The two assertions a shot cannot make

`lists-after.txt` (cut out of `conf-after.txt`, the profile as Relay left it):

```
tier\flash=glm-coding|glm-5.3-flash|, anthropic|claude-opus-5|     <- ctrl+enter landed in flash
tier\high=anthropic|claude-opus-5|                                  <- untouched
tier\lite=gemini|gemini-3.5-flash-lite|                             <- the lite list still names it
tier\main=glm-coding|glm-5.3|, kimi-code|k3|                        <- untouched

available=… (43 keys) …                                             <- and gemini|gemini-3.5-flash-lite
                                                                       is NOT among them
```

So the un-tick stuck while the lite list went on naming the model — which is the whole of the
rule: the chores read `models/tier/lite` straight (`Pane::tiersObject`) and never `shown()`, so
taking a lite model out of the terminal's filter leaves them exactly where they were.

`relay-free|relay-lite` **is** in `available`, and that is correct: the first un-tick writes down
today's default as a snapshot, and the snapshot is of the setting, not of what is drawn.
`models::liteOnlyRole` filters at read time, which is why `relay` has two rows and not three in
`e-available-alphabetical-no-recent.png`.

## What the first runs found, and what was changed for it

Four runs, and the shots of the first three were wrong in ways the code was wrong in:

1. **The model's name was elided** — "claude-opu…", "gemini-3.5-…", "kimi-for-cod…" — while
   "anthropic (claude) · pay-as-you-go" had room to spare. `via` was sized to its contents and the
   model column took what was left, which at pane width is about 145 px. Both stretch now, so the
   provider elides first: it is the column whose text repeats down the page and whose tooltip says
   it in full. (Rule 1 is that every surface prints the model's name.)
2. **A class header's note was elided too** — "main · new pane…" — because it was written into the
   same narrow column. The class goes in the model column and the note in **via**, which is wide.
3. **The notes were too long** even there: "what /high runs on, and a plan turn" became
   "/high, and plan mode".
4. **Rank 1 of main said "new panes start here" twice** once the section header said it, and the
   two together cost the model column a third of its width. The row says it only on a single-class
   page, which has no header; the tooltip says it either way.
5. **The sections did not look divided.** Each class header now carries a band — a step off the
   list's own `QPalette::Base` rather than a colour of its own, so it follows a theme change.

Two of the runs also failed for reasons that are the script's and not the app's, and `drive.sh`
says so where it happens: a modal hands the keyboard back to the *window* rather than to the pane,
so the tab after the key box is reached with the mouse and then Alt+2 again to put the caret in the
filter; and pressing Ctrl+Shift+M while the focus is already in the models pane closes it, so the
Options steps come last and from the terminal pane.

## Tests

`ctest --test-dir build -R "modelspane|modelpicker|modelcatalog|settings|appcommands|filterpopup"`
— green. `PYTHONPATH=backend python3 -m unittest tests.test_keybindings` — 25, green.
