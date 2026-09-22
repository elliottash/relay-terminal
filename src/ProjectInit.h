// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// "Initialize a project and create a Board here?" — when it is asked, what it shows, and
// what a ticked box imports.
//
// Card #JN7X made the Board per project (src/Projects.h); this is the one question that ever
// creates one. The owner's rule of 2026-09-18 is that **nothing is created silently**: no agent, no
// pane, no `cd` and no launch makes `<project>/board/`. The folder appears after one yes.
//
// Everything here is pure: `decide()` takes what the window already knows and returns what to do,
// and `questionFrom()` turns a `project_probe_result` (docs/PROJECT-INIT-AND-IMPORT.md section 3)
// into the lines the pane draws. QtCore only — no widgets, no filesystem, no worker — so the whole
// decision table and every findings line is tested headlessly in tests/projectinit_test.cpp. The
// pane owns the pixels and the protocol; it owns no rules.
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace relay {
namespace projectinit {

// ----- who is asking ----------------------------------------------------------------------------
//
// The five triggers the owner listed, and the only five. Everything else — `cd`, launching Relay,
// hovering, `#` typed in the composer, opening a file — is silent, for ever.
enum class Trigger {
    AgentWork,     // (1) the first prompt sent to the agent in a pane standing in a git repo
    AgentCard,     // (2) the agent's first card: the worker asked with `board_init_request`
    Switchboard,   // (3) the Board was opened (Ctrl+Shift+S, the palette, /board)
    CardCommand,   // (4) `/card <text>`: the text is held and lands on a yes
    InitCommand,   // (5) `/init`, the explicit command, which also clears a remembered no
};

// The `projects::kReason*` a yes attaches with, which is also the trigger's name on the wire and in
// the log. `parseTrigger()` is its inverse; an unknown name leaves `*out` alone and answers false.
QString triggerName(Trigger trigger);
bool parseTrigger(const QString &name, Trigger *out);
// The five, in the order above.
QStringList triggerNames();

// ----- what the window knows ---------------------------------------------------------------------
//
// `decide()` stats nothing and reads no settings: the pane's candidate project, whether that project
// already has a board, and what the registry remembers are all passed in, so the table below is one
// function with no hidden inputs.
struct Situation {
    QString project;          // the pane's candidate project (projects::candidateFor), or empty
    QString cwd;              // the pane's live directory; only `/init` ever falls back to it
    bool hasBoard = false;    // projects::boardDirOf(project) is not empty
    bool declined = false;    // the user answered no to this project once (Registry::isDeclined)
    bool snoozed = false;     // "Not now" for this project earlier in this Relay session
    bool asking = false;      // a question is already on screen in this pane
    bool remote = false;      // a guest's view of somebody else's pane: it may never be asked
};

enum class Outcome {
    Nothing,   // say nothing, create nothing, ask nothing
    Ask,       // probe the project and put the question on screen
    Attach,    // the project already has a board: attach the tab, do not ask
    Say,       // one quiet line (`message`) and nothing else
};

struct Decision {
    Outcome outcome = Outcome::Nothing;
    QString project;            // the project the question (or the attach) is about
    QString reason;             // projects::kReason* for the attach a yes performs
    QString message;            // Say's line, and the line an Attach has when it has one
    bool clearsDecline = false; // `/init`: the user changed their mind, so the no goes

    bool asks() const { return outcome == Outcome::Ask; }
};

// The whole decision table, in one place:
//
//   a guest's pane, or a question already up   -> Nothing
//   no candidate, and not `/init`              -> Nothing  (~/Downloads is not a project)
//   no candidate, `/init`                      -> Ask about the pane's own directory
//   the project already has a board            -> Attach   (nothing to create, nothing to ask)
//   `/init`                                    -> Ask, and clear a remembered no
//   the user already said no                   -> Nothing  (for ever, across restarts)
//   "Not now" this session, and trigger (1)    -> Nothing  (an explicit trigger still asks)
//   otherwise                                  -> Ask
//
// A caller that gets `Nothing` keeps whatever quiet line it showed before this question existed;
// this function never invents one for a path that had none.
Decision decide(Trigger trigger, const Situation &situation);

// ----- what the question shows -------------------------------------------------------------------

// One line of the question. `importable` findings get an unticked checkbox; the rest are shown and
// never imported, because the question has to be honest about what it found even when Relay will
// not touch it (a GitHub remote, a Jira key in the branch name, an older `issues/` tree).
struct Finding {
    QString kind;             // a project_probe tracker kind for an import, else the hint's kind
    QString path;             // relative to the project
    int count = 0;
    QString text;             // the line as shown; the probe writes these for this dialog
    bool importable = false;

    bool operator==(const Finding &other) const {
        return kind == other.kind && path == other.path && count == other.count
               && text == other.text && importable == other.importable;
    }
};

// Everything the pane draws, derived from one `project_probe_result`.
struct Question {
    QString project;         // absolute
    QString folder;          // `<project>/board` — the only thing a yes creates
    QString title;           // the one-line question
    QString folderLine;      // what will be created, naming the folder and nothing else
    QList<Finding> imports;  // a checkbox each, all unticked
    QList<Finding> notes;    // shown, never imported
    int items = 0;           // how many trackable items were found in total

    bool isValid() const { return !project.isEmpty(); }
};

// The one-line question. It names the folder so the answer is never a guess about what appears.
QString titleLine();
// "Creates <project>/board/ — and nothing else." `project` empty in, empty out.
QString folderLineFor(const QString &project);
// `<project>/board`, the folder a yes creates. Mirrors projects::chooseBoard()'s Uninitialized
// directory; spelled here so this library needs nothing from the registry.
QString boardFolderFor(const QString &project);

// A `project_probe_result` (protocol 19.13) turned into the question. Tolerant by design: a result
// with no `trackers`, no `git` or no `hints` is a question with no findings, not an error, because a
// probe that could not read a corner of the project must not stop the user initializing it.
//
// `trackers` become the checkboxes, in the probe's own order. `hints`, an older pre-board `issues/`
// tree and the primary GitHub remote become the notes.
Question questionFrom(const QJsonObject &probeResult);

// The `kinds` a `board_import_propose`/`board_import_apply` is limited to: the tracker kinds of the
// ticked findings, deduplicated and sorted, so the same kind ticked twice asks for it once. A
// finding that is not importable is ignored however it was ticked.
QStringList importKinds(const QList<Finding> &findings, const QList<bool> &ticked);

// The one quiet line after a yes. `imported` < 0 means an import was not asked for.
//
//   "Board created in /repo/board/"
//   "Board created in /repo/board/ · 23 cards imported"
//   "Board created in /repo/board/ · nothing left to import"
QString createdLine(const QString &project, int imported);

// What the pane says when the user says no, so the line and the remembered answer are written once.
QString declinedLine(const QString &project);
// … and when they say "Not now".
QString notNowLine(const QString &project);

}  // namespace projectinit
}  // namespace relay
