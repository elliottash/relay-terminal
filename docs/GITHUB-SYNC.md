# Switchboard ↔ GitHub Issues, two-way

Card [`#GDQN`](../issues/features/2026-09-17-github-issues-sync.md). Design context:
[`SWITCHBOARD-DESIGN.md`](SWITCHBOARD-DESIGN.md) §10 phase 3, format:
[`SWITCHBOARD-FORMAT.md`](SWITCHBOARD-FORMAT.md).

Implementation: `backend/relay_core/forge_sync.py` (the provider-neutral engine) and
`backend/relay_core/forge_github.py` (the GitHub provider). Tests: `tests/test_forge_sync.py`,
`tests/test_forge_github.py`, against the in-process `tests/fake_github.py`. No model is called and
nothing touches the network outside a `plan()` or `run()`.

## 1. What syncs, and what never does

| Syncs | Never syncs |
|---|---|
| shared **work** cards (`type: work`, outside `.private/`) | private cards (anything under the board's `.private/` root) |
| their threads, bodies, labels, status, tab, assignee | plan cards (`type: plan`) and memory cards (`type: memory`) |
| | alias cards, `board.yaml`, `BOARD.md`, the sync state itself |

Two independent mechanisms, because one of them is not enough:

1. **The type filter.** `ForgeSync.shared_cards()` is the only source of anything that is rendered
   into a request, so a private, plan or memory card's *words* are never in one.
2. **The guard.** What could still slip out is a *mention*: a private card's title or id quoted in
   a shared card's body, or a plan's title used as a link title. So every call the engine makes
   goes through `GuardedProvider`, one proxy in front of the provider, which scans every outgoing
   string (title, body, comment, label, assignee) against `PrivacyGuard` and raises
   `ForgePrivacyError` **before the request is made**. The card is reported as an error in the
   result and the run carries on with the others; nothing half-written is ever sent.

The guard matches *identities* — the id of a private card, and the title (plus `name`,
`description`, `goal`) of every card that must not sync — on word boundaries, case-insensitively.
It deliberately does not match every sentence of a private body: two cards asking for the same
thing in the same words are normal, and refusing that would make the sync useless without making
it safer. A linked plan appears in an issue as its bare id (`` `#K7Q2` ``) and never as its title;
a *private* plan is not even linked.

## 2. The mapping

| Switchboard | GitHub |
|---|---|
| card title (the body's `# ` heading) | issue title |
| card body below the title | issue body, with a hidden footer appended |
| `## Tasks` items | task-list checkboxes; the `<!-- t:xx -->` markers ride along and GitHub hides them |
| tab (the card's category folder) | label `tab:<tab id>` |
| status | label `status:<status>`; `done` closes as **completed**, `dropped` closes as **not planned** |
| `labels` | the issue's other labels |
| `assignee` | issue assignee, when the person maps to a login (`login_map`) |
| card id `#K7Q2` | `<!-- relay-id: K7Q2 -->` in the issue body, and `links.github: owner/repo#123` on the card |
| thread entries (`comment`, `question`, `decision`, `evidence`, `progress`, `note`) | issue comments |
| thread `event` entries | nothing — the issue already shows state changes as labels |
| linked plan cards | a line of bare ids in the footer; plans themselves do not sync |

**The footer.** Everything from `<!-- relay-sync -->` to the end of an issue body belongs to Relay
(the plan links and the id marker) and is stripped before the body is compared with, or copied
into, a card. A human may type freely above it.

**Hidden markers** are HTML comments: GitHub drops them from the rendered page but keeps them in
the raw Markdown its web editor shows, so they survive an edit unless someone deletes the line. If
that happens, `links.github` still names the issue, and the next sync writes the marker back
(`_repair_issue_marker`; the same for a comment whose `relay-entry` marker was edited out).

**Comments do not echo.** A comment Relay posted carries
`<!-- relay-entry: <thread entry id> card=<ID> -->` and is never imported as a thread entry. A
comment written on GitHub becomes a thread entry under its own author, with
`via=github github_comment=<id> github_author=<login>`; those entries are never pushed back. An
imported comment later edited on GitHub is appended again with `edit_of=<the first entry>`, because
threads are append-only and nothing is ever rewritten.

**Reopening.** A closed issue reopened on GitHub brings the card back to the status its
`status:` label names, or to `ready` when the label still says `done`/`dropped`. Moving the card
out of `done`/`dropped` here reopens the issue.

**Issues nobody here knows** become cards: the `tab:` label picks the folder (or `default_tab`, or
the board's first tab), the `status:` label and the issue's state pick the status, and the issue
gains the hidden `relay-id` marker so a second clone cannot import it twice. Pull requests are
never issues: they are filtered out of every listing.

## 3. The state file

`<board>/.private/forge-sync.json`, mode `0600`, JSON, version 1.

```json
{"version": 1, "repo": "owner/repo", "issues_etag": "\"ab12…\"",
 "last_seen": "2026-09-18T12:00:01Z",
 "cards": {"K7Q2": {"number": 12, "card_hash": "…", "updated_at": "…", "etag": "\"…\"",
                    "comments_etag": "\"…\"",
                    "base": {"title": "…", "prose": "…", "tasks": [["a3", false, "…"]],
                             "labels": ["voice"], "status": "ready", "tab": "features",
                             "assignee": null},
                    "comments": {"20260917T141203Z-a1": 8881},
                    "pulled": {"8882": {"entry": "20260918T101010Z-b2", "updated_at": "…"}},
                    "remote_entries": ["20260917T141203Z-a1"],
                    "conflict": "9f2c…"}}}
```

- It sits in the board's **private root**, which `switchboard/.gitignore` excludes from git; the
  engine writes that line if the board does not have it yet. It is per machine: the ETags and the
  `updated_at` values are conversations with one server, and a shared file would conflict on every
  sync.
- It holds **no card text beyond the baseline needed to merge**, and **no credential** — there is
  no field for one, and the provider never hands it over.
- `relay-board.py check` ignores it: `Board.card_paths()` only walks `*.md` two levels deep, and a
  `.json` in the private root is neither a card nor a thread. A sync leaves `check` clean.
- It is rewritten **atomically after every card**, and after every comment, so a run killed half
  way resumes exactly where it stopped and never posts the same comment twice.
- **Losing it is survivable.** `links.github` in the card's front matter is the durable link, so
  no issue and no comment is created twice (a comment Relay posted is recognised by its hidden
  marker in the listing, not only by the state). Without a baseline the engine is conservative: a
  field that differs on the two sides becomes a conflict rather than an overwrite.
- **One board syncs to one repository.** `links.github` holds a single `owner/repo#n`, so pointing
  a board at a second repository starts a fresh mapping and replaces the link. If both are ever
  wanted at once, that is a format change (a list under `links.github`) and an owner decision.

## 4. Conflicts

For every linked card the engine has three versions of each field: the **baseline** (last synced),
the **card now** and the **issue now**.

| card | issue | result |
|---|---|---|
| changed | unchanged | **push** |
| unchanged | changed | **pull** |
| changed the same way | changed the same way | nothing; the baseline moves on |
| changed differently | changed differently | **conflict** |

Fields are merged independently, so a title edited here and a checkbox ticked there both land in
one run. Labels merge as sets (both sides' additions survive; a removal on either side propagates).
Tasks merge per item id, so ticking `#K7Q2.a3` on GitHub and renaming `#K7Q2.b7` here is not a
conflict.

A conflict **writes to neither side**. It is recorded:

- on the card's thread, as a `note` entry naming the field and quoting both versions;
- in the result, under `conflicts: [{card, number, field, card: "…", issue: "…"}]`.

The baseline for a conflicted field stays where it was, so the conflict keeps being reported until
someone resolves it by editing one side; the thread entry is written once per distinct pair of
versions (`conflict` in the state file), so an unresolved conflict does not spam the thread on
every sync.

## 5. Credentials

Resolved at sync time, never at import:

1. the explicit `token` argument;
2. `GH_TOKEN`, then `GITHUB_TOKEN`;
3. `gh auth token --hostname <host>` (a subprocess, run only when a sync needs it);
4. `git credential fill` for `https://<host>`.

The token is held in one attribute, sent only as the `Authorization: Bearer` header, and scrubbed
out of anything this code raises or logs (`GitHubProvider._safe`, and `__repr__` does not print
it). It is never written to the state file. `tests/test_forge_github.py::TokenNeverLeaksTests`
holds that line, including the case where the forge echoes the token back in an error body.

## 6. Rate limits, ETags and Enterprise

- Every listing is conditional: the stored ETag goes out as `If-None-Match`, and a `304` means
  "nothing changed". An idle sync therefore costs one conditional request for the issue list and
  no rate-limit quota at all; the result says `"idle": true`.
- Listings page through the `Link: …; rel="next"` header, `per_page=100`, bounded by `MAX_PAGES`.
- **Secondary** limits (`Retry-After`) are waited out when the wait is short (`MAX_WAIT`, 60 s) and
  there are attempts left; otherwise the run stops and the result carries
  `retry_at` (unix time) and `retry_at_text` (`"09:14"`). It never hangs.
- **Primary** limits (`x-ratelimit-remaining: 0`) use `x-ratelimit-reset` for the same result.
- Work already done is kept: the state is saved per card, so a limit half way through a first sync
  leaves the issues already created linked, and the next run finishes the rest without duplicates.
- **GitHub Enterprise:** pass `base_url`. `https://ghe.example.com` is expanded to its REST root
  `https://ghe.example.com/api/v3`; a URL that already has a path, or an address, is used as given.
- **Forks:** `forge_github.provider_for()` follows `parent` when the fork has issues disabled and
  the parent has them. A repository with issues disabled and no usable parent is refused before
  anything is written.

## 7. Safety

- **`dry_run`** (`ForgeSync.plan()`) returns the whole plan — per card the action, the fields that
  would be pushed and pulled, the conflicts, the comment counts — and makes no write of any kind,
  on either side. The GUI shows this before the first real sync.
- **Bulk cap.** A run whose plan would *create* more than `create_cap` issues (default 20) refuses:
  `needs_confirm: true`, `would_create` in `creates`, nothing sent. `confirm_bulk` lets it through.
  This is what stops a board of 186 cards becoming 186 public issues by accident.
- **Nothing is ever deleted**, on either side. Closing is `done`/`dropped`; a card removed locally
  leaves its issue alone.
- **Idempotent.** A second run with nothing changed writes nothing and makes no request beyond the
  conditional ones.
- **Resumable.** State per card, written atomically; comments recorded one at a time.

## 8. Protocol (proposed; not yet wired)

Two messages, on the worker that already owns the board (`board_protocol.BoardCommands`), so the
Switchboard pane can show the plan and then run it.

| Request | Reply |
|---|---|
| `forge_sync_plan {id, dry_run: true, repo?, base_url?}` | `forge_sync_planned {id, root, repo, cards, creates, pushed, pulled, conflicts, needs_confirm, cap}` |
| `forge_sync_run {id, confirm_bulk?, repo?, base_url?}` | `forge_sync_progress {id, root, card, action, done, total}` … then `forge_sync_done {id, root, repo, pushed, pulled, imported, comments_out, comments_in, conflicts, errors, retry_at?, retry_at_text?}` |

Both run on a thread, like `hosted_quota`, so the worker loop never waits on the network; a failure
answers `error {id, text, code}` and never a traceback. The `repo` and `base_url` come from
`board.yaml` (`github: {repo, base_url, login_map, create_cap, default_tab}`) when they are not
given. `forge_sync_progress` is the engine's `on_progress` callback — `{card, title, action,
number, done, total}` after every card a *run* applies; `plan()` never calls it. After
`forge_sync_done` the handler emits the usual `board_changed`, since a sync writes card files
outside `BoardTools`.

## 9. Another forge

`ForgeProvider` is the whole surface: `repo_info`, `list_issues(since, etag)`, `get_issue(number,
etag)`, `create_issue`, `update_issue`, `list_comments(number, since, etag)`, `create_comment`,
`update_comment`, `ensure_labels`. It speaks in `Issue`, `Comment` and `RepoInfo`, which are plain
dataclasses with GitHub's *vocabulary* but nothing of its wire format.

A GitLab or Gitea provider is one new module beside `forge_github.py`:

- map issue *iid* to `Issue.number`, `state: opened|closed` to `open|closed`, and GitLab's
  `state_event`/`closed` semantics to `state_reason` (`completed` / `not_planned`); Gitea's REST is
  close enough to GitHub's that most of the translation is a rename.
- return `None` from the conditional calls on a 304, set `list_etag` / `comments_etag`, and raise
  `ForgeRateLimited` / `ForgeAuthError` / `ForgeUnavailable` from `forge_sync`.
- nothing else changes: the mapping, the markers, the three-way merge, the privacy guard, the
  state file and the caps are all in `forge_sync.py` and are already provider-neutral.
