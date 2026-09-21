# Exporting and importing a model profile

Owner, 2026-09-21: **"allow exporting and importing profiles."**

A profile (the named snapshot of the five tier lists, f40cadf9) now travels as a JSON file, so one
can be mailed to a colleague, committed to a dotfiles repo, or carried to a second machine.

## What was added

- **Options › Models › profiles.** The current profile's row is now `rename… / export… / delete`.
  Below it a **profiles file** row carries **import…**, and **export all…** once there is more than
  one profile to mean. The import row is there with no profiles at all: importing is how the first
  profile arrives on a second machine.
- **The file** (`relay::models::curation::exportProfiles` / `readProfiles` / `writeProfile`):

  ```json
  {"relay": "model profiles", "version": 1, "exported": "2026-09-21T02:24:12Z",
   "profiles": [{"name": "AI work",
                 "lists": {"main": [{"preset": "glm-coding", "model": "glm-5.3", "effort": "high"}],
                           "high": [...], "flash": [...], "lite": [...], "local": []}}]}
  ```

  The entries are the same `{preset, model, effort}` objects the worker sends as
  `tier_list_defaults`, so a default it computed and a profile the user exported read the same.
  Every tier is written, empty ones included — a reader that saw `lite` missing could not tell "no
  lite models" from "this file predates the lite list". A lone `{"name", "lists"}` object is read
  too, and a `version` from a later Relay is read rather than refused.
- **A model this machine has no provider for is kept, not dropped.** The file may well arrive
  before the key does, and an entry nothing can run is skipped at failover time anyway.
- **Collisions ask.** An imported name this machine already uses offers *replace* / *keep both*
  (`AI work (2)`) / *skip*, one question per name, defaulting to "keep both".
- **Importing changes nothing that is running.** The profile is listed, not switched to — except
  when the import lands on the profile the live lists belong to, where the lists move with it,
  because the lists and the current profile are one thing.

## Evidence

`drive.sh` (Xvfb, an isolated `HOME`/XDG/`TMPDIR`, two literal non-key strings so two presets are
"stored", no network turn): `/models` → "fill the lists" → new profile "AI work" → **export…** →
**delete** → **import…** → switch onto it → **export…** again.

| shot | what it shows |
| --- | --- |
| `0-models-page.png` | Options › Models via `/models` |
| `0b-fill-the-lists.png` | the defaults that give the profile something to name |
| `1-profiles-search.png` | no profile yet: the choice row, and **profiles file** with **import…** only |
| `a-profiles-group.png` | "AI work" current: **rename… / export… / delete** |
| `b-export-dialog.png` | the save dialog |
| `c-deleted.png` | deleted — back to "no profile", no actions row |
| `d-imported.png` | **import…** read the file: *Imported "AI work".* |
| `e-listed.png` | listed in the choice box, and not switched to |
| `f-switched.png` | switched onto it |

`AI work.json` is the file the run wrote. Exporting again from the imported copy produced a file
identical to it apart from the `exported` timestamp — the round trip is lossless:

```
$ diff <(grep -v exported "AI work.json") <(grep -v exported "AI work (reimported).json")
$
```

`relay-stderr.log` is four lines and holds no `malloc`, `Segmentation` or `ASSERT`.

## Unit tests

`tests/modelcatalog_test.cpp::profilesTravelAsJson` — the document's shape, all five tiers present,
a round trip through text onto a cleared QSettings, a replace landing on the current profile moving
the live lists, a hand-written lone profile with an unsplit `key` and no effort, and the four
refusals (empty, not ours, no profiles, an invalid name) each leaving the store alone.
`ctest --test-dir build -R modelcatalog` passes.
