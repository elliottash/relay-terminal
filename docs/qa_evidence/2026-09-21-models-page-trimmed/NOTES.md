# Options › Models keeps providers, keys and profiles — #MDL1 t:a10

Design `docs/MODEL-PICKING-DESIGN.md` §5.5, owner 2026-09-21 ("yes to all your recs"):

> The tier lists and the "models in the picker" checklist leave the page: one row, "models and
> priorities… (Ctrl+Alt+M)", opens the dialog on the main tab, and the defaults buttons move into
> the dialog as "fill from defaults". The checklist's setting (`models/shown`) retires; the
> dialog's `all` tab shows every usable model and keeps OpenRouter's long tail behind typing.

`drive.sh` is the whole run: Xvfb on a free display in 600..630, an isolated
`HOME`/`XDG_*`/`TMPDIR` under `/tmp/claude-1000/ct` (short, because `XDG_RUNTIME_DIR` holds a unix
socket), `RELAY_KEYRING=off`, and three fake provider keys. No turn is taken, so nothing calls a
provider. The OpenRouter long tail is the owner's **real** cached listing — 446 rows, copied into
the sandbox's `XDG_CACHE_HOME` with `RELAY_OPENROUTER_CATALOG=off` so nothing fetches.

Steps `d`–`g` run before `b`–`c` on purpose: the dialog button is the first row of the page, and
the first attempt at this run scrolled down to photograph the rest, scrolled back and clicked
"Fall over to a fallback model" instead. The page is now photographed after the dialog is done
with.

| shot | what it shows |
|---|---|
| `a-models-top.png` | The page as it opens. **models and priorities… (Ctrl+Alt+M)** is the first row, then **providers** (folded, as it always was once a provider is set up), **profiles**, **defaults**. No checklist, no tier lists, no "fill the lists". |
| `d-dialog-on-main.png` | The button pressed. The dialog is on the **main** tab — not on `high`, the first tab, and not on the pane's own mode: the page is not in a mode. The two defaults buttons are here now as **fill from defaults** / **…with openrouter**. |
| `e-all-tab.png` | The `all` tab with nothing typed: ~20 rows, every model these providers can run. OpenRouter's 446 live rows are **not** among them, and the footer says why — "type to search every model, openrouter's long tail included". |
| `f-tail-more-from-openrouter.png` | `muse-spark` typed. Five rows appear under the rule **more from openrouter** — `meta/muse-spark-1.3` and its siblings, none of which is in any list, none of which was reachable from this dialog before. Enter uses one; Ctrl+Enter on a tier tab adds it. |
| `g-add-a-model-by-id.png` | The end of the `all` tab: **+ add a model by id…**, highlighted. This is where the per-provider id box that used to sit under each open-ended provider on Options › Models went. "use" is greyed on it, because it is a row you press, not a model. |
| `b-models-providers.png` | **providers** opened: kimi code, z.ai, openrouter, claude code, codex — each with its key line, **replace key…** / **change login** and **test**. Under `claude code`, **when it wants to use a tool**: the guest permission row, which used to sit under that guest's models in the checklist and now sits under its own provider row. Nothing else is under a provider. |
| `c-models-bottom.png` | The same page from the bottom: **+ add provider**, **profiles** (profile, profiles file), **defaults** (failover, output token limit, advanced provider settings, per-job models) and **Reset to defaults**. No tier list anywhere on the page. |

`conf-before.txt` / `conf-after.txt` are the sandbox's `relay.conf`; the run changes nothing in it,
which is the point — every control photographed here either opens something or belongs to a
provider.

Covered headless rather than here:

- the page really has no tier row and no checkbox, and the button really opens on main —
  `tests/settingspane_test.cpp`, `theModelsPageHasNoTierListsAndNoChecklist` and
  `theModelsPageButtonOpensTheDialogOnMain` (they read `RelayWindow::modelsSection()` as text, the
  way the tests beside them do, because the page needs a whole window to build);
- `models/shown` in an existing settings file being ignored rather than migrated, and the exact
  rule `shown()` now applies — `tests/modelcatalog_test.cpp`, `aStoredModelsShownListIsIgnored`
  and `anOpenEndedProvidersLongTailIsBehindTyping`;
- "+ add a model by id…" storing an id the provider does not list — `tests/modelpicker_test.cpp`,
  `addAModelByIdIsTheLastRowOfTheAllTab` (the little provider/id dialog is modal, so the test
  presses `ModelPicker::addModelById` instead of driving it).

Commits: `ef5417d5` (the page), `b88e8282` (`models/shown` retired, the tail behind typing, the id
box's new home), `dd74b49a` (the box keeps the pane's own row when its class is switched off),
`f5cbf8f5` (README and ARCHITECTURE).
