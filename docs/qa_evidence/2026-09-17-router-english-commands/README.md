# Router: commands that are also ordinary English words (2026-09-17)

Implementer evidence for `issues/features/needs_qa_llm/2026-09-17-router-english-commands.md`.
These are **not** QA verdicts.

Owner report: typing

> look at some of my other letters in ~/admin/Advisees.\*.docx for my writing style

ran in the shell, because `look` is an installed command (bsdextrautils) and the glob made
`assist_signals` bail out. The glob half was fixed in commit 9a915b7. The owner then asked whether
`look` has siblings: *"'look' is the classic example of a command i mentioned that would also show
up in natural language. double check there arent others like that -- try to deploy a subagent to
make an inclusive list."*

This directory holds the derivation of that list, the classification of every candidate, and the
before/after measurements.

## Method

Nothing here was written from memory; every candidate was produced by a machine and then judged.

| Step | Source | Result |
|---|---|---|
| 1. Everything runnable | every `-executable` regular file in each absolute `PATH` directory (`find -L "$d" -maxdepth 1 -type f -executable`) | `path-executables.txt`, 4327 names |
| 2. Shell builtins | `compgen -b` (bash 5.2) | `bash-builtins.txt`, 61 |
| 3. Shell keywords | `compgen -k` | `bash-keywords.txt`, 22 |
| 4. Elsewhere on Linux | `busybox --list` (the busybox on this machine) | `busybox-applets.txt`, 272 applets; 7 of them are dictionary words not on PATH here (`ash cal nuke resume rpm ts watchdog`) |
| 5. A real English word list | `/usr/share/dict/american-english` + `/usr/share/dict/british-english` (Debian `wamerican`/`wbritish`), restricted to purely lowercase alphabetic entries | 65407 words |
| 6. Intersection | steps 1–3 ∩ step 5, lowercase alphabetic names only | **241 words** — `english-word-commands.tsv` (with the owning package from `dpkg -S`) |

Package families that show up in the intersection: bash builtins (38) and keywords (13), coreutils
(34), ImageMagick (10), texlive-binaries (6), procps (5), ncurses-bin (5), graphviz (5), binutils (5),
vim (4), util-linux (4), mailutils (4), bsdextrautils (4), bsdutils (3), plus findutils, grep,
diffutils, patch, tar, less, man-db, bind9-host, bind9-dnsutils, iputils, cups-client, xdg-utils,
p11-kit, git and the python/node/ruby toolchains.

Note on completeness: the dictionary is the limiting factor in both directions. It contains a few
non-words (`ls`, `comm`, `pl`), and it misses words a request can start with whose command is not an
English *dictionary* entry (`ps`, `nl`, `od`, `tac`, `seq`, `tr`, `whoami`) — those were already in
`ENGLISH_COMMANDS` from earlier work and were left there; they never score, because they need signal
words to fire.

## Classification (`classification.tsv`)

| Category | Count | Meaning |
|---|---|---|
| (c) already present | 108 | already in `ENGLISH_COMMANDS` before this change |
| (a) added | 17 | plausible first word of an English request, and installed here |
| (b) left out | 116 | English (or dictionary-listed) but never sentence-initial in a request |

**(a) added from the intersection** — `bind browse cancel eject from prove prune route screen shred
strip suspend tar transform truncate unzip zip`

Sentences that motivated each: "bind the shortcut to a new tab", "browse the docs for this api",
"cancel the pending print job", "eject the usb drive", "from the logs tell me what failed", "prove
that the fix works", "prune the branches that are merged", "route the request through the proxy",
"screen the output for errors", "shred the old key files", "strip the whitespace from these lines",
"suspend the running job", "tar up the logs", "transform the csv into json", "truncate the log file
to zero", "unzip the archive into tmp", "zip up the build output".

`from` is worth calling out: it is a preposition, not a verb, but an English request can legitimately
start with it ("from the logs, tell me what failed") and `from(1)` is installed here (mailutils).

**(b) left out**, grouped by why:

| Reason | Words |
|---|---|
| application name; nobody opens a request with it | ark assistant audacious audacity baobab bitmap designer dolphin dragon evince linguist nautilus spectacle sushi sweeper totem wish xterm yelp |
| editor or pager invoked by name, not an English verb | editor pager vi vim ex ed red |
| language, toolchain or package-manager name | bash dash sh python ruby node pip gem rake jar latex ninja gawk flex bison swig cc as gold gs scalar codex acorn curl apt snap |
| internal helper of graphviz/texlive/ncurses | cluster dotty lefty mingle patchwork dot conjure ebb grog ht pl tangle tie toe tic twill weave tabs |
| shell grammar word or non-verb builtin | for if in else then while until function times typeset declare complete disown exec caller |
| English only as a noun, adjective or preposition mid-sentence | as col size strings w dd arch defaults manifest messages profiles quota expiry locale sieve sos skill pro net proxy |
| system or service program whose name is not an everyday verb | bridge duplicity flock ftp parted pollinate samba service telnet aha |
| destructive, and not phrased as a sentence-initial verb here | rm ls |

Two of these are close calls and are recorded as such: `strings` ("strings the user sees should be
translated" is a real sentence, but a request starting with a bare plural noun is not how people type)
and `defaults` (macOS `defaults write …`; "defaults for the new panes …" is a noun phrase, not a
request). Both stay out. If QA disagrees they are one word each to add.

## Words added that are not installed on this machine

Relay runs on other machines, so these come from documentation rather than this PATH. Every one is a
real program or shell builtin somewhere; none was invented.

| Word | Where it exists |
|---|---|
| `accept`, `reject`, `disable` | CUPS (`cupsaccept`/`accept`, `cupsreject`/`reject`, `cupsdisable`/`disable`); `disable` is also a ksh builtin |
| `at`, `batch` | `at(1)` / `batch(1)`, on most distributions and on macOS |
| `banner` | `banner(1)`: sysvbanner / bsdmainutils on Linux, BSD and macOS |
| `bundle` | Ruby Bundler, `bundle(1)` |
| `dump`, `restore` | `dump(8)` / `restore(8)`, the classic BSD and Linux backup pair |
| `jot` | BSD and macOS `jot(1)` |
| `just` | the `just` command runner, `just(1)` |
| `pass` | passwordstore.org `pass(1)` |
| `please` | the `please` build system, and `please(1)` as a sudo alternative |
| `resume` | busybox applet (listed by `busybox --list` on this machine, not on PATH) |
| `sample` | macOS `sample(1)` |
| `spell` | GNU `spell(1)` |
| `talk` | `talk(1)`, Debian's `talk` package and macOS |
| `tidy` | HTML Tidy, `tidy(1)` |
| `where` | `where` builtin in zsh and csh |
| `wipe` | the `wipe` package, `wipe(1)` |

Already present from earlier work and in the same class: `open`, `say`, `log`, `apply`, `fetch`
(macOS/BSD), `tree`, `units`, `locate` (other distributions), `print` (ksh), `go`, `task`, `todo`,
`note`, `plan`, `review`, `code`, `chat`.

**Deliberately not added:** `uv` (not an English word — initials), `cal`, `bc`, `df`, `du`, `sed`,
`awk` (not English), `nuke` (busybox-only, and initramfs-only), `zsh`-only word-like builtins other
than `where`. One entry was **removed**: `base`, which matches no program of any name — `basename`,
`base32`, `base64` and `basenc` all exist, `base` does not. The duplicate `look` in the old list was
also collapsed.

## SIGNAL_WORDS

The list grew from 84 to 200 words. Added at weight 2: indefinite pronouns and reflexives
(`something`, `anything`, `everyone`, `myself`, …). Added at weight 1: the rest of the common English
prepositions (`through between across against during except since than via within near like off down
back away because while whether unless …`), auxiliaries and modals (`have has had am being doing done
might must shall need needs want wants let lets`), ordinary adverbs (`also just still only very too
really actually maybe probably now today yesterday once ever never always often sometimes`) and
quantifiers (`much more most less least few many several each both another other same such`).

Weight 1 is deliberately not enough on its own: `shutdown now`, `make all`, `watch more`, `last back`,
`time again` and `test out` all score 1 and stay in the shell.

Contractions in the list (`why's`, `what's`) are unreachable — an apostrophe zeroes the score, and
`bash -n` rejects the unbalanced quote before that. They are kept for readers and noted in the code.

## Measurements

The table is 99 inputs: 54 English sentences that must reach the agent and 45 real commands that must
stay in the shell, using the same words. It lives in `tests/test_router.py` (`ROUTING_TABLE`) so it
cannot drift from the code. Words not installed here are passed as `known_commands`, so the table
measures routing rather than the local package set; `cwd` is an empty temporary directory.

| | before (HEAD f908d16) | after |
|---|---|---|
| sentences routed to the agent | 25 / 54 | **54 / 54** |
| commands kept in the shell | 45 / 45 | **45 / 45** |
| overall | 70 / 99 (70.7%) | **99 / 99 (100%)** |

Full per-case output: `measurements-before.txt`, `measurements-after.txt`. Every one of the 29
regressions fixed was a sentence that ran silently as a shell command, the same failure the owner hit.
No shell command moved; false positives stayed at zero.

Two further invariants are asserted by the tests:

- every sentence in the table that would also parse as a runnable command sets `needs_assist` with an
  `assist_reason`, so `route_assist` decides rather than the local guess being silently wrong;
- no command in the shell half sets `needs_assist`, so ordinary shell use never pays for a model call.

## False-positive sweep (`sweep-false-positives.txt`)

- All 232 `ENGLISH_COMMANDS` words × 14 ordinary argument shapes (`-h`, `file.txt`, `/etc/passwd`,
  `-n 5 log.txt`, `src/main.py`, …) = 3248 inputs: **0** would ask the model.
- All 232 words × each of the 144 weight-1 signal words as the only argument = 33408 inputs: 5680
  reach the threshold, and **all 5680** are explained by the pre-existing `BARE_WORD_ODD` and
  `go`-subcommand rules (`compare about`, `split away`: a command whose argument must be a file, given
  a bare word that is no file). **0** are caused by the new signal words.

`BARE_WORD_ODD` gained `find patch strip truncate shred prove tidy spell sum transform` (their
argument must be an existing file, so a bare non-file word is a sentence). `unzip` was tried and
removed again: `unzip archive` is a normal command, since unzip appends `.zip` itself. `make`,
`watch`, `grep` and `look` are out for the same reason: their arguments are targets, commands,
patterns and dictionary words, not files.

## Remaining ambiguous cases

Honest limits, all of which set `needs_assist` or are listed here as misses:

1. **A sentence with no function words at all.** "prune old branches", "find duplicate files" (this
   one is caught by `BARE_WORD_ODD`), "strip trailing whitespace" — the only evidence is that the
   words are English, and a bare-noun argument list is also what many real commands look like.
2. **`git`-style subcommand words as the first word.** "log the output", "show me the diff",
   "commit the change" only become ambiguous on a machine where `log`, `show` or `commit` resolve
   (macOS has `log`). `git log` itself is never affected: the first word is `git`.
3. **`LITERAL_TEXT` commands.** `echo`, `printf`, `print`, `say`, `banner`, `logger`, `wall`, `write`,
   `notify`, `send` take literal text, so "say the build is broken" guesses *shell* — correct for
   `say`, wrong if the user meant the agent. It sets `needs_assist`, so the model settles it.
4. **Single-word input.** `look`, `make`, `watch` alone score 0 and run. That is right.
5. **The user's own scripts.** A `./review`, `plan` or `deploy` on PATH makes those words ambiguous
   on that machine only; that is exactly what the "only if it resolves" rule is for.
