// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Which project a pane is in, where that project's Board lives, and which projects Relay
// knows about.
//
// Card #JN7X made the Board per project instead of one global board. This is the model the
// owner asked for on top of it:
//
//   * A tab is attached to no project by default. Nothing is inferred from the directory Relay was
//     launched in, ever.
//   * A pane's *candidate* project is derived fresh from its live terminal cwd, by looking at the
//     filesystem and nothing else — `candidateFor()`. A candidate is an offer, not an attachment.
//   * Attaching is always an explicit action (opening the Board, `/card`, the `#` picker,
//     executing a card, …). Each of those is one of the closed set of reasons below, and it is the
//     reason a project is written into the registry.
//   * **A project's board lives in the project**, in a folder called `board/` whose marker is
//     `board/board.yaml` — the owner's decision of 2026-09-21 (#1CXD). Relay never creates it
//     behind the user's back: the folder appears only after they answer the one-time "Initialize a
//     project and create a Board here?" question. That is what `Board::Uninitialized` and
//     `Board::needsConsent` say.
//   * Boards that already live at `.switchboard/board.yaml`, `switchboard/board.yaml` or
//     `issues/board.yaml` keep working exactly as they are; they are found by the same walk and
//     used where they are. Nothing moves by itself: `boardFolders()` is the one precedence order,
//     and the one action that renames a folder is the user's own "Move this board to board/"
//     (backend `board_folder`, protocol 19.17).
//   * The registry of known projects is removable: every record carries why it became known and
//     when. A "no, do not make a Board here" answer is remembered too (`decline()`), so the
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

// The folders a board may be kept in, and the marker file that makes one a board.
// `board/` is what Relay creates since 2026-09-21 (owner, card #1CXD); `.switchboard/` is what it
// created between 2026-09-19 and then, `switchboard/` between 2026-09-18 and 2026-09-19, and
// `issues/` is the original spelling, including this repository's own.
inline constexpr const char *kNewBoardFolder = "board";
inline constexpr const char *kHiddenBoardFolder = ".switchboard";
inline constexpr const char *kBoardFolder = "switchboard";
inline constexpr const char *kLegacyBoardFolder = "issues";
inline constexpr const char *kBoardMarkerFile = "board.yaml";

// The four above, in precedence order: `board`, `.switchboard`, `switchboard`, `issues`. **Reading
// is tolerant and ordered**: every lookup walks this one list and the first folder that holds
// `board.yaml` is the board. It mirrors `BOARD_FOLDERS` in backend/relay_core/board.py exactly, in
// the same order, and tests/projects_test.cpp reads that file to keep the two from drifting.
QStringList boardFolders();

// The folder a *new* board goes in: `board`, whatever a project already on disk is called. Pure.
// There is no setting: the "Hidden Switchboard folder" toggle went with the decision of
// 2026-09-21, and a board that already exists moves only through the explicit "Move this board to
// board/" action (protocol 19.17).
QString newBoardFolder();

// ----- the default project for loose cards ------------------------------------------------------
//
// A card filed in a tab attached to nothing needs somewhere to go. There is no board outside a
// project any more (the personal inbox was dropped on 2026-09-19, card #916B), so the answer is one
// optional setting: a project folder the user chose, empty by default. Empty, or a path that is no
// longer a directory, means "ask me" — the project picker, and the init question behind it.
inline constexpr const char *kDefaultProjectSetting = "board/default_project";
QString defaultProject();

// ----- why a project became known --------------------------------------------------------------
//
// A closed set: `remember()` refuses anything else, so the registry can be read back as "you
// attached this because you opened its Board" and never as a free-text note.
inline constexpr const char *kReasonSwitchboard = "switchboard";   // the Board was opened on it
inline constexpr const char *kReasonCardCommand = "card-command";  // `/card` was run in a pane there
inline constexpr const char *kReasonCardPicker = "card-picker";    // a card was picked with `#`
inline constexpr const char *kReasonExecuteCard = "execute-card";  // a card of it was executed
inline constexpr const char *kReasonPicker = "picker";             // it was chosen in the project picker
inline constexpr const char *kReasonAgentWrite = "agent-write";    // an agent wrote to its board
inline constexpr const char *kReasonRestored = "restored";         // a saved layout brought it back
inline constexpr const char *kReasonRepoBoard = "repo-board";      // its board was already in the repo
inline constexpr const char *kReasonInitCommand = "init-command";  // `/init` initialised it
inline constexpr const char *kReasonAgentCard = "agent-card";      // an agent filed a card against it
inline constexpr const char *kReasonAgentWork = "agent-work";      // the user set the agent to work in it

// The eleven reasons above, in that order.
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
//   1. the nearest ancestor (the directory itself first) holding a board: any of `boardFolders()`
//      with a `board.yaml` in it — an initialised project wins however far up it is, because that
//      marker is a deliberate statement and `.git` is not;
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

// The board folder a project already has: the first of `boardFolders()` that holds a `board.yaml`,
// otherwise empty. The earlier spelling in that order wins when a project has more than one folder,
// and the others are left alone rather than merged. An empty result means "not initialised", which
// is the only thing that raises the consent question. Impure, by nature; `chooseBoard()` takes the
// answer as an argument.
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
    };
    Kind kind = None;
    QString dir;                // the board folder; empty for None
    QString project;            // the project it belongs to; empty for None
    QString key;                // keyFor(project); empty for None
    bool needsConsent = false;  // nothing may be written here until the user answers "initialize?"

    bool isValid() const { return kind != None && !dir.isEmpty(); }
    // Somewhere cards can be written right now, with nothing to ask first.
    bool isWritable() const { return isValid() && !needsConsent; }
};

// Which board a project's cards belong to. **Pure**: it stats nothing and reads no settings, so the
// three facts it cannot know — the board folder the project already has, what Relay already recorded
// about it, and the folder a new board would be created in — are passed in. `known` may be null;
// `existingBoardDir` is what `boardDirOf()` returned; an empty `newFolder` means `kNewBoardFolder`.
//
//   * empty or relative `project`   -> None. A board never lives at a relative path.
//   * `existingBoardDir` non-empty  -> InRepo, at exactly that folder. Wherever the board already
//                                      is, or in an older `.switchboard/`, `switchboard/` or `issues/`, that
//                                      is where the cards are; Relay does not move it.
//   * otherwise                     -> Uninitialized at `<project>/<newFolder>`, with needsConsent.
//                                      That is where a board *would* go. Nothing may be created
//                                      there until the user answers the one-time question.
//
// There is no board outside a project: the personal inbox was dropped on 2026-09-19 (#916B), and a
// loose card goes to `defaultProject()` or to the project the user picks.
//
// `project` is expected to be the absolute, canonical path `candidateFor()` returns; the key is
// derived from it without a stat, so a path that is absolute but not canonical gets a key
// `keyFor()` would not agree with.
Board chooseBoard(const QString &project, const QString &existingBoardDir, const Record *known,
                  const QString &newFolder = QString());

// The `board` field a Board maps to. Only InRepo is a board that exists, so a project waiting to be
// initialised — and no project at all — reads as kBoardNone.
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

    // "No, do not make a Board here." Remembered so the same project is never asked twice.
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
// (may be null) and calls chooseBoard() with `newBoardFolder()`, the folder a new board gets. A project the
// user has already declined still comes back Uninitialized — the Board says where a board would go,
// and the caller asks the registry whether it is allowed to raise the question again.
Board boardFor(const QString &project, const Registry *known = nullptr);

}  // namespace projects
}  // namespace relay
