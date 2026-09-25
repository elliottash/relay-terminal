# Relay — instructions for Claude sessions

The project's working rules live in `WARP.md` (issues, tests, protocol, shortcut hints) and the
docs it points at. Read that first. What follows is specific to how sessions work in this repo.

Before building Relay or producing an installer/package, read `docs/BUILDING.md`. It is the
canonical cross-platform build map used by Codex, Claude and Relay agents. Keep platform commands
there aligned with the native CI workflows rather than copying them into agent-specific notes.

## Work in this checkout, on main. No branches, no worktrees.

Owner's rule, 2026-09-18: **do not create a branch or a git worktree for your work.** Edit
`/home/elliott/repos/relay-terminal` directly and commit to `main`.

This repo is worked by several Claude sessions at once, often in the same files. Branches and
worktrees look safer than they are here: work sitting on a side branch is invisible to everyone
else, so two sessions fix the same bug twice (it happened — the model dropdown and the unknown
`/command` were each implemented independently on the same day), a fast-forward aborts on any file
another session has open, and every abandoned worktree leaves a stale branch and a stale build
directory behind. Committing small and often to `main` is what keeps the other sessions honest
about what is already done.

What this means in practice:

- Commit your own work as soon as it builds and its targeted tests pass. Do not sit on it.
  Do not run the full test suites (`ctest --test-dir build`, `./scripts/test.sh`) unless the
  owner asks: run the tests that cover your change (`ctest --test-dir build -R <name>`, one
  pytest file) and leave the suites to the owner.
- Before starting, `git log --oneline -15` and `git status` — someone may have just done it, or be
  half way through the file you are about to change.
- Never commit, stash or revert a file you did not write. If another session's unfinished work
  blocks you, say so and wait, or ask them; `.board/bug_intake.txt` and `.board/feature_intake.txt`
  are the owner's inboxes and are never yours to commit.
- Never use bare `git stash` / `git stash pop`: the stash stack is shared with every other session.
- Subagents work in this checkout too. Give them a narrow, named area of the code so two of them
  cannot land in the same function, and tell them not to commit anything they did not write.

## How to commit here without reverting someone else

On 2026-09-18 alone, five commits silently undid other sessions' work: each was built from an index
or a base that was older than `main`, so it wrote files back to their old contents. The working tree
still had the new code, so nobody noticed until a clean export failed to build. That is how
`backend/relay_core/tools.py` went twice in one day (#E99H) and how #W5N2 lost
`docs/REMOTE-AND-MULTIPLAYER-DESIGN.md`; they came back in `f38682f` and `26362e2`.

The hand-run recipe that replaced it then failed four more ways in one evening on 2026-09-19:
`git checkout HEAD -- <path>` on a path another session was editing overwrote work that existed
nowhere else; `git reset -q HEAD -- <paths>` run while the checkout sat on a different branch
unstaged somebody else's files; a merge written as
`git merge-file ours <(git show A:p) <(git show B:p)` exited 0 and applied nothing, because
`merge-file` cannot read a `/dev/fd` pipe; and the shared index quietly filled with entries equal to
older commits' blobs as `main` moved, so the next plain `git commit` by anyone reverted whatever had
landed since. Every one of those looks like success at the terminal.

So it is not hand-run any more. **`scripts/land.py` is the commit procedure here, and the only one.**

```
python3 scripts/land.py begin <me> <the paths you are about to change>
# edit, build and test in this checkout, exactly as before
python3 scripts/land.py commit <me> -m "message"      # or -m path/to/message.txt
```

`<me>` is any short name you pick for your session. `begin` snapshots those files as they are right
now — including files another session has already half-edited, and files that do not exist yet.
`commit` then takes, for each path, base = that snapshot, ours = the file at the current tip of
`refs/heads/main`, theirs = your working copy, and three-way merges them into *the tip plus your
hunks*: the other session's uncommitted edits were in the snapshot, so they are neither committed
nor touched, and they are still sitting in the working tree afterwards. It hashes the merged blobs
into a private index read from that same tip, `commit-tree`s onto it, checks that
`git diff --name-only TIP NEW` lists exactly your paths, and compare-and-swaps with
`git update-ref refs/heads/main NEW TIP`. If `main` moved while you were testing, the swap fails —
that is the one case it exists for — and the whole merge is recomputed against the new tip and
retried, up to ten times. Afterwards it sets the shared index entry for your paths, and only your
paths, to what it committed, so `git status` shows what is still uncommitted and nobody's next
`git commit` can revert you.

What it refuses to do, and why each refusal is an incident from the list above:

- **It never commits from the shared index and never `git add`s into it.** `python3
  scripts/land.py hook install` puts a `pre-commit` hook in place that makes git itself refuse a
  commit whose index is the shared one, saying so in a sentence; `begin` installs it if it is
  missing. The owner's escape hatch is `RELAY_ALLOW_SHARED_COMMIT=1 git commit …`.
- **It never runs `git checkout`, `git stash` or `git reset`, and never writes a working-tree
  file.** The one exception is `doctor --fix` on a path whose index entry *and* working copy are
  both byte-for-byte an older commit's blob — provably nobody's edit.
- **It merges through real temporary files**, never process substitution.
- **It reads the tip once per attempt** and passes that same sha to `commit-tree -p` and to
  `update-ref`'s old-value argument. It never re-reads the branch between building and swapping.
- **It never commits `.board/bug_intake.txt`, `.board/feature_intake.txt` or a `*.orig` file** (the
  first two are the owner's inboxes), and it refuses a path that `.gitignore` covers, so a build
  directory cannot be swept in.
- **A path you edited without `begin` is refused**: with no snapshot there is no way to tell your
  diff from anyone else's. Either claim it (`begin` snapshots it as it is now, so only what you do
  from then on lands) or pass `--whole <path>`, which commits that entire working copy and prints
  the hunks it is about to take with it.
- **A conflict aborts.** It names the paths, exits 3, and leaves the branch, the index and the
  working tree exactly as they were. Pull the other session's version into your copy by hand
  (`git show main:<path>`), then run `commit` again — or, as the message says, need fewer files:
  restructuring so the contested file needs no change is usually cheaper than merging harder.

`--dry-run` prints the merged diff without landing anything, `--paths` lands a subset of what you
claimed, and `abandon <me>` drops the snapshots (never the working tree). `--help` is written for a
session that has not read this file.

### Contested hunks, and the two-step commit

A snapshot tells your hunks from someone else's only for as long as nobody else edits that file
after it was taken. On 2026-09-19 a session held a snapshot of `src/Pane.h` for forty minutes,
another session edited the header in that window, and `commit` read the second session's work as
its own: it landed half of somebody else's change, without the other file it needed, and `main`
stopped compiling. It had skipped `--dry-run` the second time, and there was then no way to say
"take back the hunks I landed by mistake, leave the working tree alone".

So `begin` now leaves a **marker**: when you claim a path a live session already claims, your
snapshot is recorded in their session data too. At their commit, a hunk that is also in
diff(their snapshot, your marker) was in the tree before you started, so it is theirs; anything
else appeared afterwards and is **contested**. At *your* commit, a path claimed by a session that
began before you has *every* hunk contested — you hold no marker for them, so nothing in the file
can say who typed what.

`commit` always prints a per-path stat (hunks, +/- lines, snapshot age, who else holds the path).
When a path has contested hunks, or its snapshot is older than 15 minutes (`--stale-minutes`), it
does **not** land: it prints the numbered hunks with the contested ones marked, plus a digest of
exactly what it would land, and exits 4. Then either

```
commit <me> -m "..." --confirm <digest>                  land all of it
commit <me> -m "..." --exclude-hunk src/Pane.h:2,5-7     leave those hunks out
commit <me> -m "..." --only-hunk src/Pane.h:1,3          land only those hunks
```

Hunks left out are neither committed nor touched: they stay in the working tree and a later commit
picks them up. Either selection flag prints a new digest. The digest covers the tip and the exact
bytes of every path, so an edit in the tree, a different selection or `main` moving makes it stop
matching and you are asked again. Uncontested, fresh paths still land in one step.

`begin --contact <name>` records how to reach you — a land-session name is not an address — and it
is shown in every claim warning. `python3 scripts/land.py who` lists the live sessions, what they
claim, how old their snapshots are and their contacts. A session that has run no land.py command
for 12 hours is stale: `who` and `doctor` say so, and it stops contesting anything.

`begin --base <rev>`, or `--from-head`, snapshots that revision's version of each path instead of
the working copy — for a file you had already edited before claiming it. **This is the supported
replacement for hand-editing files under `/tmp/claude-1000/land/<me>/snap/`.** Its hunks are then
diff(`<rev>:path`, working copy), which includes anything another session left in that file, so
such a path always goes through the confirm review, never in one step.

### The build gate: what gets built is what would land

Every session builds the same `build/` from the same working tree, and that tree holds everyone's
uncommitted code — so "it compiles here" says nothing about the commit. On 2026-09-19 two commits
landed hunks that only built because the other half of somebody else's change was sitting in the
tree; the tree that went onto the branch did not compile at all.

So when the paths being landed include C++ or build files (`src/`, `engine/`, `tests/*.cpp`,
`CMakeLists.txt`, `*.cmake`), `commit` materialises the **exact** tree it is about to put on `main`
into a build slot, `/tmp/claude-1000/land/verify-slots/<repo>-<n>/src`, builds it in `.../build`,
and performs the compare-and-swap only if that exits 0. Otherwise it prints the first compiler
errors, lands nothing and exits 5. Only files whose blob changed are rewritten, so a slot stays
incremental whoever used it last. There are `RELAY_LAND_VERIFY_SLOTS` slots (default 2) shared by
every session and each is locked for a whole build: until card #SZHQ every session kept its own
verify build forever, and on 2026-09-24 that was 255 of them and 153 GB. A slot is the tool's own
check, not a workspace: nobody edits there. `land.py gc` (which also runs by itself at most hourly)
drops sessions idle for 3 days; `who` prints what the land root takes on disk. `--verify-cmd "<shell>"`, `--verify-tests "<ctest regex>"` and `--verify-target`
override the default (configure if needed, then `cmake --build … --target relay`); `--no-verify`
exists and is refused whenever any path is contested or stale. Landed `.py` files are byte-compiled
the same way, which costs nothing.

### `repair <sha> --paths p...`: take back what one commit landed

`python3 scripts/land.py repair <sha> --paths src/Pane.h` lands one commit that sets each of those
paths to `<sha>^`'s version *plus* whatever landed on them after `<sha>` (three-way: base
`<sha>:path`, ours the tip, theirs `<sha>^:path`), then points the shared index at the new blobs so
nobody's next `git commit` puts the bad version back. A conflict aborts with nothing changed, and
`--dry-run` shows the diff. It never touches the working tree — which still holds the other
session's code — so **verify a repair on a clean export of the resulting tree**
(`git archive <new sha> | tar -x -C <scratch dir>`), never in the checkout.

`python3 scripts/land.py doctor` is the thing to run when the checkout looks wrong. It reports a
staged entry whose blob is an older commit's version of that path — the shape that reverts people —
and `--fix` puts the index (and, only in the provable case above, the working copy) back to HEAD's
version, which loses nothing. Staged content that is *not* in history is somebody's uncommitted
work: it reports that and leaves it alone, and the same goes for stray branches, extra worktrees,
`*.orig` files and a checkout that is not on `main`. It exits non-zero when it found something.

## Build through `scripts/relay-build`

`src/main.cpp` and the window sources can take time to compile, and every session builds the
same `build/` directory. On 2026-09-19 that produced a green build of code nobody had written: a
compile that had already read `src/Pane.h` wrote its object **35 seconds after** another session
edited that header, so the next `cmake --build` saw an object newer than every source, rebuilt
nothing, printed "Built target relay", and `./build/relay` went on running the code from before the
edit. Nothing reports this. The change simply appears not to work, and the session that made it
starts debugging code that was never compiled.

So build through the wrapper, from any directory inside the checkout:

```
scripts/relay-build                            # configure if needed, then build
scripts/relay-build --target relay-editor-tests   # extra args go to `cmake --build`
scripts/relay-build --check "Fold thinking"    # fail unless build/relay holds that literal
```

It does three things:

- **One build at a time.** It holds an exclusive `flock` on `build/.relay-build.lock` around the
  whole configure-and-build. A session that has to wait is told whose build it is waiting for
  (pid, and `RELAY_SESSION` if you set it); `--wait-seconds N` caps the wait and exits 2.
- **Configure only when `build/` is not configured**, and stop on a non-zero cmake exit rather than
  build whatever the last good configure left behind. `--reconfigure` and `--cmake-arg=-DFOO=bar`
  force it.
- **No object newer than the header it was compiled from.** After a successful build it sets every
  object, archive and ELF binary it produced back to the time the build *started*. A header edited
  while the compile was running is then newer than the object built from it, so the next build
  recompiles it — which is what make would have done if compiling were instantaneous. Everything
  gets the *same* timestamp, so no binary looks older than the objects it was linked from.

With `ccache` installed, every Relay configure (`build/`, `build-fast/`, the verify slots)
compiles through one shared cache in `~/.cache/relay/ccache` (card #V52P,
`cmake/CompilerCache.cmake`; `docs/BUILDING.md` has the switches), so a header state one session's
verify slot compiled is mostly cache hits for the next one's.

`RELAY_JOBS` (default 8) is the parallelism, clamped to what the memory limit the
build runs under allows (about one 2 GiB compile job per 2 GiB, so a default build
fits an agent pane's 8 GiB MemoryMax instead of being OOM-killed, card #04EC), and
`scripts/build.sh` goes through the wrapper too.
`tests/test_relay_build.py` reproduces the incident in throwaway CMake projects: plain
`cmake --build` misses the mid-compile edit, the wrapper rebuilds it.

If you suspect a stale object anyway — your change is not in the binary and the build says there is
nothing to do — delete the object and build again:

```
rm build/CMakeFiles/relay.dir/src/main.cpp.o
```

## Fix clear gaps; do not list them

Owner's rule, 2026-09-18: **when you find a clear gap in your own work, fix it rather than list
it.** A "Known gaps" or "Deliberately left" section is for what genuinely needs the owner — a product
decision, a trade-off with no obvious answer, or work outside what you may touch — not for loose ends
you could have tied. Before reporting, go through your gaps and ask of each: is the right behaviour
obvious, and is it within reach? If yes, do it, test it, and report it as done. What stays listed
must say why it could not be done here (whose decision it is, or which session's files it needs).
The same goes for subagents: tell them this rule, and when their report lists a gap that fails the
test, send them back to fix it rather than passing it on.

## `src/main.cpp` was split (2026-09-18). Here is where things went.

`main.cpp` was 13,000 lines and every session was editing the same file. It is now the includes,
`registerUrlHandler()`, `migrateFastRoleSettings()`, the quit signal and `main()` — about 330
lines — and one header per unit beside it:

- `src/AppPaths.h` — `dataRoot()`, `relayFuzzyScore()`, and the `RELAY_*` fallbacks.
- `src/Keymap.h` — `ActionDef`, `Keymap`.
- `src/Isolation.h` — `namespace isolation`.
- `src/Pane.h` — `QueueRowDelegate` and `Pane`. This is still 8,000 of the lines. Since card #AGNT
  (2026-09-21) `Pane` is also **the agent console**: a pane built with a `relay::agent::Context`
  whose spec says `shell: false` starts no pty and is what the Board's list page and card
  page, Options, Actions and Sessions embed. Only `RelayWindow::createAgentConsole` constructs one.
- `src/PaneChrome.h` — `ToolPane`, `PaneChrome`.
- `src/WindowChrome.h` — `ChromeButton`, `NotificationsPopup`.
- `src/RelayWindow.h` — `ClosedItem`, the `WindowManager` declaration, then `RelayWindow`. Those
  two share a header because they name each other in inline member bodies: `WindowManager::forget()`
  takes a `RelayWindow *`, `RelayWindow` holds a `WindowManager *`, so neither can be complete first.
- `src/WindowManagerImpl.h` — the `WindowManager` members that were already written out of line
  below `RelayWindow`, now `inline`.

The original split was for navigation. Card #243T moved 18 `Pane` and 13 `RelayWindow` method
bodies into nine `.cpp` files. Editing one of those bodies now compiles its file without rebuilding
`main.cpp`; editing either class header still recompiles several window sources. Keep new method
bodies in the matching `.cpp` file when possible, and use `scripts/relay-build --fast` for a
separate `build-fast/` developer binary (`-O0 -g1`). The normal `build/` remains the optimized
local validation build.

Two units joined them on 2026-09-21, and they are **not** part of the one translation unit: they are
their own library (`relay-agentcontext`, QtCore only), so `relay-board`, `relay-settings` and
`relay-conversations` can link them without pulling in a window.

- `src/AgentContext.{h,cpp}` — `relay::agent::Context` (what an agent is *about*: the spec, the
  action row, `submit`, link resolution, `turnFinished`, the placeholder — and **no tool list,
  ever**), with `ContextSpec`, `Action`, `TurnRecord`, `ConsoleHandle` and `ConsoleFactory`.
- `src/AgentHost.h` — `relay::agent::Host`, what a console is drawn on. `Pane` implements it.

`src/HelperChat.{h,cpp}`, `src/BoardChat.h`, `src/HelperModelBox.{h,cpp}` and the
`relay-helperchat` library are **retired** with the same card: there is no second chat surface, and
no `board_chat*` message on the wire. Read `docs/ARCHITECTURE.md`, "Agents are consoles; contexts
are what they are about", before adding a surface that asks an agent anything.

Two practical consequences:

- Put a new window-level class in the header its neighbours are in, not back in `main.cpp`, and add
  it to the `relay` target's source list in `CMakeLists.txt` if it gets a file of its own (AUTOMOC
  and IDEs read that list; nothing here has `Q_OBJECT`).
- Each header includes what it uses and compiles on its own. If you add a use of a Qt class, add its
  `#include` to that header, not only to `main.cpp`; the new window source files compile the
  headers separately.

`scripts/split-main.py` is the tool that did it: it finds every region by content anchor, moves the
text without rewriting it (the only change is the word `inline`), and `--check` rebuilds the
original from the new files to prove no line was dropped or duplicated. It was run twice, once on
`git show HEAD:src/main.cpp` and once on the working tree, so the sessions that had uncommitted
edits in `main.cpp` got them back as ordinary uncommitted diffs in the new files. It is one-shot and
kept for the record; it refuses to run on an already-split `main.cpp`.

<!-- relay:switchboard-policy start -->
## Board (Relay)

This project has a Relay board in `.board/`: its cards are the record of what was asked and
what was done, in plain Markdown in git. Read them there or in `.board/BOARD.md`. To search its cards with `rg`, name `.board/` explicitly or use `rg --hidden`; a plain project-wide `rg` skips hidden folders.

**Before doing work, read `.board/POLICY.md`** and follow it: check whether the request is already
done, find the card that asks for it or file one, claim it, plan on it if it needs a plan, do the
work, then land it in needs-verification with its evidence. The policy is the same one Relay's own
agents get in their system prompt; `.board/POLICY.md` also says how to do each of their `board_*`
tool calls by editing files, which is what you have.

<!-- Generated by Relay (relay_core.board.pointer_text): this block is replaced whenever the
     board scaffold runs. Edit around it, not inside it. -->
<!-- relay:switchboard-policy end -->
