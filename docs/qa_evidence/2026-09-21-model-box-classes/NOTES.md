# The model box as classes — card #MDL1, task t:a8

`drive.sh`, run under Xvfb on 2026-09-21 against `build/relay` at commit `dfed0146`, on display
:560 with an isolated `HOME`/`XDG_*`/`TMPDIR` under `/tmp/claude-1000/cx`, `RELAY_KEYRING=off`,
`isolation/enabled=false` and three fake provider keys. No turn is taken: every step is a model or
mode *switch*, which the worker answers without calling anybody.

The design is `docs/MODEL-PICKING-DESIGN.md` §5.3, and the four rulings on the card are what the
shots are here to show.

## The lists it was run against

```
main   kimi-code|k3, glm-coding|glm-5.3, kimi-code|k3-256k, kimi-code|kimi-for-coding
high   glm-coding|glm-5.3, openai|gpt-6-astra          (no OpenAI key on this machine)
flash  glm-coding|glm-5.3-flash, kimi-code|kimi-for-coding-highspeed
```

Four main entries, folding to four rows, against a cutoff of two: that is what makes the cutoff a
thing you can see. `openai|gpt-6-astra` has no key, which is the same `Group::spent` path an
exhausted subscription takes.

## What each shot shows

| shot | what it proves |
|---|---|
| `a-strip` | the collapsed chip on main: `kimi-k3`, the model alone |
| `b-altm-classes` | **high / main / flash** headers, two models under each, `kimi-k3` highlighted because it is this pane's model. `gpt-6-astra` is **absent** from high: no key. The `main` header carries `›` — it has more behind the cutoff; `high` and `flash` do not |
| `c1-down-over-the-header` | Down, Down from `kimi-k3`: `glm-5.3`, then **over the `flash` header** onto `glm-5.3-flash`. The highlight never lands on a header |
| `c2-down-again` | one more Down: `kimi-for-coding-highspeed` |
| `d-right-expands-main` | Right on a main row: main opens to all four, the header's mark turns to `⌄`, the highlight stays on the row it was on, and high and flash are untouched |
| `e-left-collapses-main` | Left: back to two, `›` again, highlight still on `glm-5.3` |
| `f-filter-across-classes` | `kimi` typed: `main`/`kimi-k3` and `flash`/`kimi-for-coding-highspeed`, each still under its own header, and the `high` header **gone** — a header lives and dies with its class's matches |
| `g1-flash-row-highlighted` | the flash row about to be picked |
| `g2-picked-flash` | Enter: "flash: glm-5.3-flash · conversation kept", and the collapsed chip reads **`glm-5.3-flash`** — the model alone, no `· flash` ("no need to show the model class in the pane header") |
| `h1-dialog` | Ctrl+Alt+M, the main tab: the **in box** column ticked on ranks 1–2, the **show this class in the box** switch, and the two **fill from defaults** buttons that were Options › Models' "fill the lists" row |
| `h2-dialog-row3-ticked` | rank 3 ticked: 1–3 are now ticked (it is a cutoff, not a per-row flag) |
| `h3-altm-three-in-main` | Alt+M: main draws three |
| `i1-flash-tab` / `i2-flash-switched-off` | the flash tab, then its class switch off: every tick in the class clears with it |
| `i3-altm-no-flash` | Alt+M: the flash class is **gone from the box** — no header, no rows |
| `stored-box-keys.txt` | what the two controls wrote: `box\main=3` and `box_off\flash=true`, beside the tier lists they belong to |

## What is not here, and why

**Exhaustion itself.** "exhausted models dont show up" is one rule with the keyless case — both are
`models::Group::spent`, and `modelrows::box` drops a spent group before it ever builds a row. The
keyless half is live in `b-altm-classes` (`gpt-6-astra` is ranked in high and is not drawn). The
exhausted half needs a provider to report a spent `limits` window, which a fake key cannot produce,
so it is stated in `tests/modelrows_test.cpp::spentAndKeylessModelsAreNotInTheBox` — a `rejected`
status on the pane's own preset, and the row is absent while the next rank moves up into its place.

**A class the pane is in, switched off.** `i3` is taken with the pane on flash, so after the switch
there is no row in the box for the model the pane is running, and the highlight falls to the first
selectable row while the chip still reads `glm-5.3-flash`. That is the switch doing what it was
asked: the cutoff spares the pane's own model, the class switch is "do not show me this class at
all". Worth a look if the owner wants the pane's own class exempted from the switch as well.

## Tests

`ctest --test-dir build -R "modelrows|filterpopup|modelpicker|modelcatalog|panestate"` — 5/5.

- `modelrows`: `classesWithTheirTopModelsUnderThem`, `headersAreLabelsAndNeverSelectable`,
  `theCutoffIsTwoByDefaultAndStored`, `expandingAClassShowsItsWholeList`,
  `aClassSwitchedOffLeavesTheBox`, `spentAndKeylessModelsAreNotInTheBox`,
  `thePanesOwnModelSurvivesTheCutoff`, `theCollapsedChipIsTheModelAlone`,
  `aConsoleBuildsTheSameRowsAsAPane`, `localClassAndGuestRows`, `fillCarriesEverythingIntoTheBox`.
- `filterpopup`: `upAndDownStepOverTheClassHeaders`, `rightExpandsAClassAndLeftCollapsesIt`,
  `aFilterKeepsTheHeadersWhoseClassStillMatches`,
  `enterAnswersWithTheRowsOwnDataAndEscapePicksNothing`,
  `theModelsAreIndentedUnderTheirClassHeader` (rendered and measured).
- `modelpicker`: `theShowInBoxColumnIsACutoffBothWays`, `theClassSwitchIsOnlyOnTheTabsTheBoxDraws`,
  `fillFromDefaultsPressesTheCallersAction`.
- `modelcatalog`: `theBoxCutoffIsStoredWithTheListsPerProfile`, and `profilesTravelAsJson` now
  carries the `box` object across an export/import round trip.
