// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// The terminal pane's tool-call lines (#TK9C, docs/AGENT-SESSIONS-PROTOCOL.md § 23): everything
// about them that is not a byte written into the terminal.
//
// The pane prints one row per tool call — `▸ ran pytest · 212 lines · exit 1 · 8 s` — wrapped in an
// OSC 8 anchor the engine's fold layer owns (docs/ENGINE.md, "Folds"). Clicking it unfolds the
// call's detail underneath it, in place. Four things have to be decided for that, and all four are
// here so they can be tested without a window (tests/calllines_test.cpp):
//
//   * the anchor URI — built, and read back when a click or a fold request arrives;
//   * the row's text — title, stats, the ✗ of a failure, cut to the pane's columns so it never
//     wraps (a wrapped anchor line would hang its fold under the wrong row);
//   * whether a result rewrites the row already on screen or starts a new one, and when a run of
//     consecutive reads stays on one row (LineCursor — the whole state machine);
//   * the fold's content: § 23.5's `detail` sections, or a merged run's member lines, as the
//     engine's FoldLine spans.
//
// Pure: QtCore, QColor and the three parsers this shares with the other surfaces (ToolLabel,
// DiffView, MarkdownAnsi). No widget, no theme — the colours arrive in a Palette the caller fills
// from the live theme tokens.
#include "MarkdownAnsi.h"      // MarkdownStream renders through it, a chunk at a time
#include "TerminalBackend.h"   // relay::FoldSpan, relay::FoldLine
#include "ToolLabel.h"

#include <QColor>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace relay::calllines {

// The fold layer is given this prefix; every URI under it is an anchor it owns.
inline const QLatin1String kFoldPrefix("relay://call/");
// A line whose click is *not* a fold (a file, a diff pane, a subagent, a card) needs a scheme the
// fold layer does not swallow, so it gets its own host and is routed like relay://turn.
inline const QLatin1String kOpenPrefix("relay://open-call/");
// What a fold is allowed to hold. Beyond it the fold ends with "… N more lines · open in pane".
inline constexpr int kFoldLineCap = 5000;
// The reasoning fold's two heights (#K48R; owner, 2026-09-19): while
// the block streams it shows the *last* six rendered rows — the end is the part being written —
// and a settled fold opened by hand shows the *first* eighteen. Rendered rows, at the pane's
// current width: one long paragraph must not escape the cap by being a single line.
inline constexpr int kThinkingStreamRows = 6;
inline constexpr int kThinkingDoneRows = 18;

// ----- the anchor URI -------------------------------------------------------------------------

// relay://call/<pane>/<turn>/<call>, with the ids percent-encoded. `extra` above zero makes the
// merged run's URI: the first member's id, a `+`, and how many calls the row now stands for.
QString foldUri(const QString &pane, const QString &turn, const QString &call, int extra = 0);
// The same, under relay://open-call/. A merged run never opens anything but its fold.
QString openUri(const QString &pane, const QString &turn, const QString &call);

struct Ref {
    bool valid = false;
    bool fold = false;      // relay://call/… (true) or relay://open-call/… (false)
    QString pane, turn, call;
    int extra = 0;          // members of a merged run, 0 when the URI names one call
    bool merged() const { return extra > 1; }
};
// Reads back either scheme. Anything else comes back invalid.
Ref parseUri(const QString &uri);

// The `<call>` component for a run of `count` calls that began with `first`.
QString runCall(const QString &first, int count);

// ----- the row --------------------------------------------------------------------------------

// One row, split where its ink changes: the title (the error ink when the call failed) and the
// muted remainder. Already cut to the width it was asked for.
struct Row {
    QString title;
    QString rest;       // " · 212 lines · exit 1 · 8 s", or empty
    bool failed = false;
    bool refused = false;  // ink grade: tool ink when refused, error ink when merely failed
    QString text() const { return title + rest; }
};

// The finished row. `cells` is how many columns the text may use (the pane subtracts the "▸ "
// placeholder and any trailing hint); zero or less means "do not cut".
Row finishedRow(const toollabel::Label &label, int cells);
// The row while the call runs. `liveLines` above zero appends the live counter the streaming
// output feeds ("running pytest… · 120 lines"); a non-empty `since` appends the start timestamp
// the caller stamps the row with ("… · since 14:32:05").
Row runningRow(const toollabel::Label &label, int cells, qint64 liveLines = 0,
               const QString &since = {});
// A merged run's row: "read 6 files · 4,100 lines".
Row mergedRow(const toollabel::MergeRun &run, int cells);

// Cuts `text` to `cells` columns, ending it with "…" when anything was dropped.
QString fit(const QString &text, int cells);

// ----- what a click does ----------------------------------------------------------------------

enum class Click { Fold, File, Diff, Subagent, Card, Plan, Todos };
// § 23.6, with its one override: a failed call always opens its fold, whatever it would have
// opened. An unknown `open.type` folds too.
Click clickFor(const toollabel::Label &label);

// Which scheme the row's OSC 8 anchor takes: relay://call/ (true) or relay://open-call/ (false).
// A fold when the terminal *has* a fold layer and the row's click is one the fold answers —
// Click::Fold, a merged run, Click::Todos, whose task list is the detail and so has nothing
// else to open (card #BDXG), or Click::Card, whose row folds like any other and links only its
// `#K7Q2` segment to the card itself (cardSegment, card #1NW3). Everything else opens a pane,
// and on a backend with no fold layer every row anchors open-call so a click still reaches the
// detail — there a card row's whole line opens the card, as it always did.
bool anchorsFold(Click click, bool merged, bool backendFolds);

// The span of a card row's own `#K7Q2` inside Row::text() — the segment the pane draws under
// relay://card/<id> while the rest of the row stays the fold anchor (card #1NW3). -1 when the
// label names no card or the id is not in the text (cut away by `fit` on a narrow pane).
int cardSegment(const Row &row, const toollabel::Label &label);

// ----- what a live tool_output adds to the row's counter ------------------------------------------

// How much a `tool_output` event adds to the running row's line count (#TK9C), in either of the
// two shapes protocol § 23.10 allows: the text itself, or — when the GUI has asked the worker for
// `stream_tool_output: false`, because nothing here is reading the text — the counts the worker
// made from exactly that text. The two must agree to the line, which is what this one function is
// for: the pane never counts newlines itself, and tests/calllines_test.cpp holds both shapes of
// the same chunk against each other (card #PPR4).
struct OutputCount {
    int lines = 0;         // newlines in this chunk
    bool partial = false;  // it does not end on one, so a line is still open and the row counts it
    bool counted = false;  // the counts arrived without the text: there is nothing to print
};
OutputCount toolOutputCount(const QJsonObject &event);

// ----- the state machine ------------------------------------------------------------------------
//
// Which row the cursor is on, whether it may still be rewritten, and how far a run of mergeable
// calls has got. The pane feeds it events and writes the bytes each Step asks for; it never writes
// anything itself.
//
//     const Step step = cursor.result(callId, label);
//     if (step.endRun) out += "\r\n";              // the held row is finished
//     if (step.rewrite) out += "\r\x1b[2K";        // redraw the row the cursor is on
//     else if (step.newRow) ensureLineStart();
//     draw(step.row, step.call);
//     if (!step.hold) out += "\r\n";               // a mergeable row keeps the cursor
//
struct Step {
    bool endRun = false;    // a held row is on screen with no newline: end it before anything else
    bool rewrite = false;   // "\r" + erase, then redraw in place
    bool newRow = false;    // a fresh row (the pane's ensureLineStart() first)
    bool hold = false;      // leave the cursor on the row: the run may still grow
    bool nothing = false;   // print nothing at all (a merged call starting inside a run)
    Row row;
    QString call;           // the URI's <call> component: "c3", or "c3+4" for a merged row
    QString callId;         // the one call, or a run's first member — what foldUri() takes
    int extra = 0;          // a run's member count, 0 when the row stands for one call
    bool merged = false;
};

// One member of a merged run, for the run's fold.
struct RunMember {
    QString line;   // "read agent.py · 412 lines"
    QString path;   // workspace-relative, for the FoldSpan link; may be empty
    QString call;
};

class LineCursor {
public:
    // tool_started. `label` is the started label (title as far as it is known, `merge` when the
    // call may join a run).
    Step start(const QString &call, const toollabel::Label &label);
    // A live tool_output tick while `call` runs: rewrite the running row with its counter. Comes
    // back with `nothing` when the row is no longer the one on screen.
    Step live(const QString &call, const toollabel::Label &label, qint64 lines, int cells);
    // tool_result.
    Step result(const QString &call, const toollabel::Label &label, int cells);
    // Anything else is about to print (prose, a note, a steer, the turn ending, closeInline).
    // Ends any held row; the next result starts a row of its own.
    Step other();

    // A row is on screen without its trailing newline.
    bool holding() const { return m_held; }
    // The <call> component of the row on screen, or empty.
    QString openCall() const;
    // The members of the run on screen, oldest first. Empty unless a run of two or more is held.
    const QVector<RunMember> &members() const { return m_members; }
    // How wide the running and merged rows are drawn. Set once from the pane's columns.
    void setCells(int cells) { m_cells = cells; }

private:
    void dropRun();
    toollabel::MergeRun m_run;
    QVector<RunMember> m_members;
    QString m_first;            // the run's first call id
    QString m_started;          // the call whose "running…" row is on screen
    bool m_held = false;        // a row is on screen with no newline
    bool m_dirty = false;       // something else printed since the row was drawn
    bool m_runRow = false;      // the held row belongs to a run, not to one started call
    QDateTime m_rowStart;       // when the held running row appeared: its "since HH:mm:ss" stamp
    int m_cells = 0;
};

// ----- the fold's content -----------------------------------------------------------------------

// Every colour a fold uses, from the caller's live theme.
struct Palette {
    QColor text;                 // the default; invalid = the terminal's own foreground
    QColor muted;                // headings, hunk headers, the last row
    QColor code;                 // a command line
    QColor link;                 // a Markdown link: the theme's link green; invalid = code
    QColor add, remove;          // the black-or-white ink on the diff fills (theme::contrastInk)
    QColor addBg, removeBg;      // the diff fills themselves: the theme's green and red
    QColor error;
    QColor accent;               // a task in progress, in the ink the tasks panel gives it
};

struct FoldOptions {
    int maxLines = kFoldLineCap;
    QString openInPane;      // the link "open in pane" carries; empty drops that word
    QString openInPaneText;  // what that link reads; empty means "open in pane"
    QString openPath;        // the link "open <name>" carries (an absolute path)
    QString openName;        // what to show after "open "; defaults to openPath's last component
    bool diffToPane = false; // the diff went to a diff pane: say so instead of repeating it
    // The OSC 8 anchor a markdown link's label is hung from inside this fold (card #MDKN,
    // src/LabelLinks.h). A fold's rows are FoldSpans, not cells, so the label's target travels in
    // `FoldSpan::link` and the view hit-tests it there; empty (the default) and the renderer
    // leaves a label plain, which is what every fold that is not rendered markdown wants.
    QString linkAnchor;
};

// The glyph a task's status is drawn with: ○ pending, ◐ in progress, ✓ completed, ✕ cancelled,
// ⏸ deferred, ✗ blocked. One table for the whole program — `RequestLedgerModel::statusGlyph`, which
// the tasks panel, the queue strip and the task menu draw with, calls this — so an `update_todos`
// fold and the panel its last row opens can never drift apart (card #BDXG). Pure, so the fold is
// testable without a window.
QString taskGlyph(const QString &status);

// § 23.5's `detail` sections of a `tool_output` reply, as fold rows. Falls back to the reply's own
// `text`/`preview` when it carries no sections (a worker from before § 23.5).
//
// The `tasks` style (an `update_todos` call, § 23.5) is the one section that is not text: each
// "[status] text" line becomes a row of `taskGlyph(status)` and the task, completed and cancelled
// ones muted and one in progress in the accent ink, exactly as the tasks panel paints them.
QVector<FoldLine> foldForReply(const QJsonObject &reply, const Palette &palette, const FoldOptions &options);

// The same reply as plain text: the call's line, then every section as foldForReply() lays it out
// (headings, `$ ` on a command, a task's glyph before it), one line per row, no colour and no
// cap. For the surface that has no fold layer — a backend without the capability anchors every
// row to relay://open-call and shows the call's detail in a preview pane (#BDXG) — so it reads
// the same list the fold would have drawn rather than the result's JSON.
QString replyAsText(const QJsonObject &reply);

// One unified diff as fold rows, drawn exactly as a `detail` section of style "diff" is: the fold
// of a write or an edit whose diff the surface already holds. A short diff folds under its row
// *collapsed* (#WXT6), so the click answers from the stored diff with no worker round trip — and
// still answers when the worker's fifty-turn log has scrolled past the call.
QVector<FoldLine> foldForDiff(const QString &unifiedDiff, const Palette &palette,
                              const FoldOptions &options);

// A merged run's fold: one row per member, each linking to the file it read.
QVector<FoldLine> foldForRun(const QVector<RunMember> &members, const Palette &palette,
                             const FoldOptions &options);

// One muted row — what a fold says when the turn is gone from the worker's log, or the pane has no
// worker to ask.
QVector<FoldLine> foldForNote(const QString &text, const Palette &palette);

// Markdown (agent reasoning) as fold rows: rendered by the streaming renderer the terminal's prose
// uses (MarkdownAnsi), then its ANSI mapped onto the fold's palette — prose muted (it is chrome
// around the reply), code blocks in the code colour, links in the link colour, inline code bold
// (as the terminal shows it), headings plain text, **Problem:** red.
// Empty input comes back empty; the caller decides what a fold with nothing to show says.
//
// `cells` above zero is the width the fold's rows are laid out at — the grid less
// relay::kFoldIndent, where FoldLayer::layout() wraps — and makes `options.maxLines` a cap on the
// rows the *view* will paint: the rows come back pre-wrapped to that width, which is what the view
// would have done with them anyway, so a paragraph that wraps twenty times counts as twenty and
// cannot escape the cap by being one line. Past the cap, `tail` keeps the *last* rows under a
// "… N earlier lines" row — a stream's end is the part being written — and without it the first
// rows are kept and the rest named on a "… N more lines" row. The two are arguments rather than
// FoldOptions fields because this is the only fold that has an end worth keeping (#K48R).
QVector<FoldLine> foldForMarkdown(const QString &markdown, const Palette &palette,
                                  const FoldOptions &options, int cells = 0, bool tail = false);

// The same rendering for a block that is still arriving, done once per character instead of once
// per flush (#PPR4). The Activity pane re-rendered the whole reasoning block — up to 400 000
// characters of Markdown — four times a second while it streamed, and wrote all of it into its
// document again each time.
//
// Feed the block's *new* text and take two things: what `feed()` returns has settled and is never
// drawn again, and `tail()` is the short run of rows after it — the line still being written, a
// table the renderer is still holding, and the trailing blank rows a finished render trims — which
// the caller redraws each time. Their concatenation is always exactly what `foldForMarkdown()`
// returns for everything fed so far, with no width, no cap and no link row (the pane passes none);
// tests/calllines_test.cpp checks that against a corpus split at every offset.
class MarkdownStream {
public:
    explicit MarkdownStream(const Palette &palette);

    // Renders the next chunk and returns the rows it settled, oldest first.
    QVector<FoldLine> feed(const QString &text);
    // The rows after the settled ones, rendered from what the renderer is still holding back.
    QVector<FoldLine> tail() const;
    // Has any row settled? False while the block is still short enough to be taken back whole —
    // which is what tells the caller to say "nothing yet" rather than draw an empty fold.
    bool settled() const { return m_settled; }

private:
    Palette m_palette;
    MarkdownAnsi m_renderer;
    FoldSpan m_span;                 // the attribute state the last chunk ended in
    QVector<FoldLine> m_pending;     // rows that may still change; the last is the open line
    bool m_ink = false;              // a span with something other than whitespace has arrived
    bool m_settled = false;
};

// Control characters and escape sequences out of stored output: a fold row is text, and the view
// paints it; an ANSI escape left in it would be drawn as mojibake.
QString stripAnsi(const QString &text);

}  // namespace relay::calllines
