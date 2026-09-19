# #T7QM — the tab title is the repo name of its project, else the folder of the active pane (2026-09-19)

Owner's request: `change the tab title to be the repo name of the associated project,
otherwise the folder of the active pane.` The label, in order:

1. a hand-set `/rename-tab` name, if there is one;
2. the repo name of the tab's attached project (`#JN7X`), if it has one;
3. the repo name of the active pane's own directory (`candidateFor`: a folder with an
   `issues/board.yaml` is a project — this checkout qualifies);
4. the folder of that directory — and `~` when it is home.

## What was run

`drive.sh` is the only run needed: there is no before/after because the rule is new.

It starts the built `relay` under Xvfb on `:95` with an isolated
`XDG_CONFIG_HOME`/`XDG_DATA_HOME`/`XDG_CACHE_HOME`, `RELAY_KEYRING=off`, shell integration on
and `onboarded=true`, on a workspace that is an ordinary folder outside any project
(`qa-playground`) so the tab starts on its folder name. **No provider is configured anywhere in
this run and none is started** — a tab label is the GUI's own since 2026-09-19 (protocol 18.3),
so the six scenes below are their own proof that no model is asked: the label is right
immediately, and moves with every `cd`.

Commands are typed into the composer and Return hands them to the pane's shell; OSC 7 carries the
new directory back, which is what the label follows. An `echo ready` warm-up makes sure shell
integration is loaded before the first `cd` (the OSC 7 for a prompt only comes after it loads).

## What to look at

`implementer-NN-*.png` is the whole window; the tab in question is the first tab in the row.
Scene 06's script step is the one subtlety: a bare `/rename-tab` opens the same in-place editor
prefilled with the hand-set name, and committing an empty field hands the tab back to its place
(ctrl+a, BackSpace, Return).

| Scene | Pane does | Tab label |
|---|---|---|
| `01-repo-name-from-a-subdirectory` | `cd <this repo>/src` | `relay-terminal` |
| `02-folder-outside-any-repo` | `cd /tmp` | `tmp` |
| `03-home-is-tilde` | `cd ~` | `~` |
| `04-back-in-the-repo` | `cd <this repo>/backend` | `relay-terminal` |
| `05-rename-tab-wins` | `/rename-tab My own name` | `My own name` |
| `06-clearing-returns-to-the-place` | `/rename-tab`, field cleared, Return | `relay-terminal` |

## How the implementer read them back

The label crop (`convert -crop 260x32+40+18 … -resize 500%`, binarized) OCR'd through tesseract
gave: `relay-terminal`, `tmp`, `~`, `relay-terminal`, `My own name`, `relay-terminal` — the six
readings above, in order. `relay-stderr.log` (next to this file) has no error, warning or assert
line in it.
