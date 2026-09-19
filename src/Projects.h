// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Which project a pane is in, where that project's Switchboard lives, and which projects Relay
// knows about.
//
// Card #JN7X made the Switchboard per project instead of one global board. This is the model the
// owner asked for on top of it:
//
//   * A tab is attached to no project by default. Nothing is inferred from the directory Relay was
//     launched in, ever.
//   * A pane's *candidate* project is derived fresh from its live terminal cwd, by looking at the
//     filesystem and nothing else — `candidateFor()`. A candidate is an offer, not an attachment.
//   * Attaching is always an explicit action (opening the Switchboard, `/card`, the `#` picker,
//     executing a card, …). Each of those is one of the closed set of reasons below, and it is the
//     reason a project is written into the registry.
//   * **A project's board lives in the project**, in a folder called `switchboard/` whose marker is
//     `switchboard/board.yaml`. Relay never creates it behind the user's back: the folder appears
//     only after they answer the one-time "Initialize a project and create a Switchboard here?"
//     question. That is what `Board::Uninitialized` and `Board::needsConsent` say.
//   * Boards that already live at `issues/board.yaml` keep working exactly as they are; they are
//     found by the same walk and used where they are. `switchboard/` wins if a project somehow has
//     both, because that is the name Relay creates today.
//   * The registry of known projects is removable: every record carries why it became known and
//     when. A "no, do not make a Switchboard here" answer is remembered too (`decline()`), so the
//     same project is not asked about twice; an explicit `/init` clears it.
//
// **No git subprocess, ever.** The walk is `QDir`/`QFileInfo` only. Running `git` to answer "which
// project is this" put a synchronous process on the GUI thread once already (see
// `issues/changes/needs_qa_llm/2026-09-17-file-completion-runs-synchronous-git-on-the-gui.md`), and
// this function is called from cwd changes, which arrive as fast as the user types `cd`.
//
// QtCore only and no widgets, so all of it is tested on a QTemporaryDir instead of a window.
// `relay::boardRootFor()` (src/BoardWorkspace.h) answers the narrower "where is this pane's board
// root" question, but it lives in `relay-board`, which links Widgets; the ancestor walk is repeated
// here rather than dragging Widgets into every caller of `candidateFor()`.
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace relay {
namespace projects {

// Bumped when projects.json's meaning changes. A file with another version is treated as
// unreadable (kept aside as `.corrupt`) rather than guessed at.
constexpr int kSchemaVersion = 1;

// ----- where a board is kept ---------------------------------------------------------------------

// The folder Relay creates for a new project's board, and its marker file.
inline constexpr const char *kBoardFolder = "switchboard";
inline constexpr const char *kBoardMarkerFile = "board.yaml";
// The folder boards were kept in before 2026-09-18. Still found, still used in place, never made.
inline constexpr const char *kLegacyBoardFolder = "issues";

// ----- why a project became known --------------------------------------------------------------
//
// A closed set: `remember()` refuses anything else, so the registry can be read back as "you
// attached this because you opened its Switchboard" and never as a free-text note.
inline constexpr const char *kReasonSwitchboard = "switchboard";   // the Switchboard was opened on it
inline constexpr const char *kReasonCardCommand = "card-command";  // `/card` was run in a pane there
inline constexpr const char *kReasonCardPicker = "card-picker";    // a card was picked with `#`
inline constexpr const char *kReasonExecuteCard = "execute-card";  // a card of it was executed
inline constexpr const char *kReasonPicker = "picker";             // it was chosen in the project picker
inline constexpr const char *kReasonAgentWrite = "agent-write";    // an agent wrote to its board
inline constexpr const char *kReasonRestored = "restored";         // a saved layout brought it back
inline constexpr const char *kReasonRepoBoard = "repo-board";      // its board was already in the repo
inline constexpr const char *kReasonInitCommand = "init-command";  // `/init` initialised it
inline constexpr const char *kReasonAgentCard = "agent-card";      // an agent filed a card against it

// The ten reasons above, in that order.
QStringList reasons();
bool isReason(const QString &reason);

// ----- how a board is stored ---------------------------------------------------------------------
//
// The `board` field of a record, and what `chooseBoard()` decided.
inline constexpr const char *kBoardRepo = "repo";   // a board folder in the project
inline constexpr const char *kBoardNone = "none";   // no board yet (or none wanted)

// ----- the candidate project of a directory ------------------------------------------------------

// The project a live terminal cwd is in, or an empty string. Filesystem walk only:
//
//   1. the nearest ancestor (the directory itself first) holding `switchboard/board.yaml` or
//      `issues/board.yaml` — an initialised project wins however far up it is, because that marker
//      is a deliberate statement and `.git` is not;
//   2. otherwise the nearest ancestor holding a `.git` entry. A *file* counts as well as a
//      directory, so a linked worktree and a submodule are projects like any other checkout;
//   3. otherwise empty.
//
// An empty or relative `cwd` yields empty: a relative path would be resolved against the process's
// own current directory, which is the launch directory this whole model exists to stop consulting.
//
// `$HOME` being a git repository (dotfiles) makes `$HOME` a legitimate candidate, and it is
// returned like any other. That is deliberate and harmless: a candidate is only an offer, nothing
// is initialised without the user saying yes, and someone who does not want to be asked again
// declines it once (`Registry::decline()`). The walk stops at the first marker, so a project
// *inside* a dotfiles `$HOME` still wins.
QString candidateFor(const QString &cwd);

// The board folder a project already has: `<project>/switchboard` when that holds a `board.yaml`,
// otherwise `<project>/issues` when that does, otherwise empty. `switchboard/` wins when a project
// has both — it is the name Relay creates, and the older folder is left alone rather than merged.
// An empty result means "not initialised", which is the only thing that raises the consent
// question. Impure, by nature; `chooseBoard()` takes the answer as an argument.
QString boardDirOf(const QString &project);

// ----- the key of a project ----------------------------------------------------------------------

// The one spelling of a path, mirroring `normalize_workspace()` in backend/relay_core/conv_index.py:
// the canonical path when it exists, otherwise the cleaned absolute path. Empty in, empty out.
//
// One knowing difference: for a path that does *not* exist, Python's `Path.resolve()` still
// resolves symlinks in the part of it that does, and `QFileInfo::absoluteFilePath()` cannot. Both
// sides only ever hash directories that exist, so this shows up in nothing but a hand-written
// literal — which is why the pinned test literal has no symlinked ancestor.
QString normalize(const QString &path);

// sha256 of an already-normalized path, first 16 hex characters. Pure: no filesystem access, so
// `chooseBoard()` can use it. Empty in, empty out.
QString digest(const QString &normalizedPath);

// The project key. Equal to `conv_index.workspace_digest()` for the same path, which is what ties a
// project to its indexed conversations and its session directory — boards live in the project now,
// but conversations are still grouped by this. The agreement is pinned by one shared literal
// asserted on both sides: see tests/projects_test.cpp and tests/test_conv_index.py.
QString keyFor(const QString &path);

// The display name of a project: its directory name, falling back to the path itself. Mirrors
// `conv_index.project_name()`.
QString nameFor(const QString &path);

// ----- records and boards ------------------------------------------------------------------------

// One known project, as stored in projects.json.
struct Record {
    QString path;              // absolute, normalized
    QString key;               // keyFor(path)
    QString name;              // nameFor(path) unless the user renamed it
    QString board;             // kBoardRepo | kBoardNone
    QString boardDir;          // the board folder, when it has one
    QString reason;            // why it first became known; never rewritten
    qint64 knownSince = 0;     // unix seconds, set once
    qint64 lastAttached = 0;   // unix seconds, bumped by every remember()

    bool isValid() const { return !path.isEmpty(); }
};

// Where a project's cards are, or would be.
struct Board {
    enum Kind {
        None,            // no project, so no board
        InRepo,          // a board folder that is there now
        Uninitialized,   // a project with no board: `dir` is where one *would* go, if the user says yes
        Inbox,           // `<boardsRoot>/inbox/switchboard`, for cards filed with no project attached
    };
    Kind kind = None;
    QString dir;                // the board folder; empty for None
    QString project;            // the project it belongs to; empty for None and Inbox
    QString key;                // keyFor(project); empty for None and Inbox
    bool needsConsent = false;  // nothing may be written here until the user answers "initialize?"

    bool isValid() const { return kind != None && !dir.isEmpty(); }
    // Somewhere cards can be written right now, with nothing to ask first.
    bool isWritable() const { return isValid() && !needsConsent; }
};

// `<GenericDataLocation>/relay/boards`, i.e. `$XDG_DATA_HOME/relay/boards`. Only the personal inbox
// lives here now; a project's own board never does. Empty when there is no writable data location.
QString boardsRoot();

// Which board a project's cards belong to. **Pure**: it stats nothing, so the two facts it cannot
// know — the board folder the project already has, and what Relay already recorded about it — are
// passed in. `known` may be null; `existingBoardDir` is what `boardDirOf()` returned.
//
//   * empty or relative `project`   -> None. A board never lives at a relative path.
//   * `existingBoardDir` non-empty  -> InRepo, at exactly that folder. Wherever the board already
//                                      is, in `switchboard/` or in an older `issues/`, that is
//                                      where the cards are; Relay does not move it.
//   * otherwise                     -> Uninitialized at `<project>/switchboard`, with needsConsent.
//                                      That is where a board *would* go. Nothing may be created
//                                      there until the user answers the one-time question.
//
// `boardsRoot` is taken so callers pass the same root they give `inbox()`; a project's board no
// longer lives under it, so it does not affect the result.
//
// `project` is expected to be the absolute, canonical path `candidateFor()` returns; the key is
// derived from it without a stat, so a path that is absolute but not canonical gets a key
// `keyFor()` would not agree with.
Board chooseBoard(const QString &project, const QString &existingBoardDir, const Record *known,
                  const QString &boardsRoot);

// The personal inbox: `<boardsRoot>/inbox/switchboard`, where a card filed with no project attached
// goes. It is in Relay's own data directory, so it belongs to nobody's repository and needs no
// consent. Empty `boardsRoot` yields None.
Board inbox(const QString &boardsRoot);

// The `board` field a Board maps to. Only InRepo is a board that exists, so everything else — a
// project waiting to be initialised, and the inbox, which is not a project — reads as kBoardNone.
QString boardKindName(Board::Kind kind);

// ----- the registry of known projects ------------------------------------------------------------
//
// `$XDG_DATA_HOME/relay/state/projects.json` (0600, atomic), beside windows.json:
//
//   {"version": 1, "saved": <unix seconds>,
//    "projects": [{path, key, name, board, board_dir, reason, known_since, last_attached}, ...],
//    "declined": [{"path": "...", "at": <unix seconds>}, ...]}
//
// The file is a convenience, never a source of truth — every record can be made again by attaching
// the project again — so a damaged one is moved aside rather than parsed hopefully. It is also the
// user's list to prune: `forget()` removes a record and nothing else.
class Registry {
public:
    // An empty path means defaultPath(). Tests pass their own; nothing else should.
    explicit Registry(const QString &path = QString());

    QString path() const { return m_path; }
    // Points the registry at another file and drops what is in memory. Load again afterwards.
    void setPath(const QString &path);

    // Reads the file. A missing file is success with an empty registry. An unreadable, malformed or
    // foreign-version file is a failure with a one-line message in *error, the registry is left
    // empty and usable, and the file is copied to `<path>.corrupt` first so nothing is lost when the
    // next write replaces it.
    bool load(QString *error = nullptr);
    // Atomic (temp file + rename) and 0600, creating the 0700 state directory on demand. Every
    // mutator below saves for itself; this is for a caller that changed nothing but wants the file.
    bool save(QString *error = nullptr) const;
    // Where load() put a file it could not read; empty until that happens.
    QString corruptPath() const { return m_path.isEmpty() ? QString() : m_path + QStringLiteral(".corrupt"); }

    // Upsert. A path that is already known keeps its original `reason` and `knownSince` — what made
    // Relay first notice a project is worth more than what touched it last — and has `board`,
    // `boardDir` and `lastAttached` refreshed. An empty `board`/`boardDir` leaves the stored ones
    // alone. `reason` must be one of the closed set; anything else is refused. `now` is unix
    // seconds, 0 meaning "right now" (tests pass their own so ordering is not a race). Remembering
    // a path also undeclines it: the user just acted on it.
    bool remember(const QString &path, const QString &reason, const QString &board = QString(),
                  const QString &boardDir = QString(), qint64 now = 0, QString *error = nullptr);
    // Removes the record. Does not touch the project's board folder, the declined list or anything
    // on disk outside projects.json. False only when the file could not be written.
    bool forget(const QString &path, QString *error = nullptr);

    // A project Relay knows: one with a record, or one that already has a board folder — a board in
    // the repository says "this is a project" by itself and needs no registration. This is the one
    // const method that stats.
    bool isKnown(const QString &path) const;
    // The stored record, or an invalid one.
    Record record(const QString &path) const;
    // Every record, most recently attached first; ties break on path so the order is stable.
    QList<Record> knownProjects() const;
    int count() const { return int(m_projects.size()); }

    // "No, do not make a Switchboard here." Remembered so the same project is never asked twice.
    // Declining a known project forgets its record as well; the two lists never hold the same path.
    // An explicit `/init` (or any `remember()`) clears it — the user changed their mind.
    bool decline(const QString &path, qint64 now = 0, QString *error = nullptr);
    bool undecline(const QString &path, QString *error = nullptr);
    bool isDeclined(const QString &path) const;
    // The declined paths, most recently declined first.
    QStringList declined() const;

private:
    int indexOf(const QString &normalized) const;
    int declinedIndexOf(const QString &normalized) const;

    QString m_path;
    QList<Record> m_projects;
    QList<QPair<QString, qint64>> m_declined;
};

// `$XDG_DATA_HOME/relay/state`, the directory windows.json is in. Empty without a data location.
QString stateDirectory();
// `<stateDirectory()>/projects.json`. Empty without a data location.
QString defaultPath();

// The impure convenience over chooseBoard(): calls boardDirOf(), looks the project up in `known`
// (may be null) and calls chooseBoard() with boardsRoot(). A project the user has already declined
// still comes back Uninitialized — the Board says where a board would go, and the caller asks the
// registry whether it is allowed to raise the question again.
Board boardFor(const QString &project, const Registry *known = nullptr);

}  // namespace projects
}  // namespace relay
