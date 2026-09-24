// SPDX-License-Identifier: AGPL-3.0-or-later
// relay::links: which parts of terminal output are files, folders, URLs and Switchboard card
// references, what they resolve to, and the keyboard cursor that steps through them.
//
// Pure rules over a line of text plus a filesystem probe: no terminal, no widget and no
// real filesystem unless the caller hands one in (`systemProbe()`), so every format and
// every false positive can be tested on its own (tests/outputlinks_test.cpp).
//
// Callers:
// - engine/view/TerminalView.cpp: hover underline, click / Ctrl+click, the link context
//   menu, and the ordered list the keyboard walk (Ctrl+Shift+L) steps through.
// - src/main.cpp: routing an activated link to a Relay explorer or preview pane, to the
//   browser, or to this tab's Switchboard (a card reference), and answering `CardLookup`
//   from the pane's card index.
//
// Issues: issues/features/needs_qa_llm/2026-09-17-clickable-paths.md (#YZTK) and
// .../2026-09-17-keyboard-jump-to-output-links.md (#GWXM); card references are Switchboard
// design section 5.
#pragma once

#include <QString>
#include <QVector>

#include <functional>

namespace relay::links {

enum class Kind {
    Path,    // a file or folder, possibly with :line:column
    Url,     // scheme://rest — left alone, never treated as a path
    Card,    // #K7Q2 — a Switchboard card the pane has seen (design section 5)
    Option,  // option:agent/allow_writes — a row of Options (#FEJQ)
    Session, // session:0f3a… — a saved conversation (#FEJQ)
};

// One span of a logical line that could be a link. `start`/`length` are UTF-16 indices
// into the line that was scanned, so a view can underline exactly that text.
struct Candidate {
    int start = 0;
    int length = 0;
    Kind kind = Kind::Path;
    QString text;      // the source text of the span (quotes already removed)
    QString path;      // Path: the span without the :line:column suffix. Card: the id, upper-cased.
                       // Option: `<section>[/<row>]`. Session: the id. (The `//` is already gone.)
    int line = -1;     // 1-based, -1 when the span carries none
    int column = -1;
    bool bare = false; // Path: a bare-token stage find with no `/` in it — see Mode::Prose
};

// What the filesystem says about an absolute path.
enum class Entry {
    Missing = -1,
    File = 0,
    Directory = 1,
};

// Where the scanned line came from (#SFZC). `Program` is a program's own output, where a bare
// name that resolves against the pane's directory is a link on purpose — an `ls` of
// extension-less folders is the commonest link-bearing line there is. `Prose` is a line Relay
// printed itself (an agent message inside a relay://prose/ block, #R2WQ), where the same rule
// lights up every English word that happens to name a directory: `tests`, `docs`, `remote`.
// There a bare token — one with no `/` — is a link only when it resolves to a *file*; a folder
// in prose carries its slash (`tests/`, `src/Pane.h`). Only the bare-token stage is affected:
// quoted paths, `file:line` forms and URLs behave the same in both modes.
enum class Mode {
    Program,
    Prose,
};

using Probe = std::function<Entry(const QString &absolutePath)>;

// QFileInfo. Callers that scan a lot of text should keep one instance.
Probe systemProbe();

// Whether the caller's Switchboard index knows a card id (upper-cased, four characters), and
// what that card is called — `title` is left alone when the board has no title for it. This is
// the card equivalent of `Probe`: a pane that has seen no board hands in nothing, and then
// `#K7Q2` in its output stays plain text, exactly as an unknown id does.
using CardLookup = std::function<bool(const QString &id, QString *title)>;

// A miss can mean a card was written directly to disk after this pane loaded its board.
// Limit refreshes from repeated hovers (and from ids that really are typos). `nowMs` is
// monotonic time supplied by the pane, which also records its initial board_open here.
class UnknownCardRefresh {
public:
    static constexpr qint64 intervalMs = 5000;

    void requested(qint64 nowMs) { m_lastRequestMs = nowMs; }
    bool due(qint64 nowMs) {
        if (m_lastRequestMs >= 0 && nowMs - m_lastRequestMs < intervalMs) return false;
        requested(nowMs);
        return true;
    }

private:
    qint64 m_lastRequestMs = -1;
};

// The target a card reference resolves to: `relay://card/K7Q2`. src/main.cpp's
// openOutputTarget() already routes relay:// targets, so a clicked, keyboard-walked or
// right-clicked card link travels the same one path as every other link.
QString cardTarget(const QString &id);
// The id inside a `relay://card/<id>` target, or empty when it is not one.
QString cardIdOf(const QString &target);

// The other two things an agent's answer can name, and the app can show (#FEJQ, card #AGNT step
// 8): a row of Options and a saved conversation. They travel as `relay://option/<section>[/<row>]`
// and `relay://session/<id>` for the same reason a card does — the engine carries one string per
// link, so the kind has to survive in it, and the host routes it in openOutputTarget() beside
// `relay://card/`. `<row>` may itself hold slashes (`option:models/provider/glm-coding`), so the
// split is at the *first* one, exactly as the helper's own click handler split it.
QString optionTarget(const QString &section, const QString &row);
// The section and row inside a `relay://option/…` target. False when it is not one; `row` is
// emptied when the target names a section alone.
bool optionOf(const QString &target, QString *section, QString *row);
QString sessionTarget(const QString &id);
// The id inside a `relay://session/<id>` target, or empty when it is not one.
QString sessionIdOf(const QString &target);

struct Target {
    bool valid = false;
    Kind kind = Kind::Path;
    QString target;         // the URL as written, the cleaned absolute path, or relay://card|option|session/…
    QString label;          // Card only: the card's title, when the board knows one
    bool directory = false; // Path only
    int line = -1;
    int column = -1;
};

// `token` without its trailing `:LINE[:COLUMN]` (and a trailing `:`). Returns false when
// nothing is left. "src/main.cpp:42:7" -> ("src/main.cpp", 42, 7); "a.py:9:" -> ("a.py", 9, -1).
bool splitLocation(const QString &token, QString *path, int *line, int *column);

// Every candidate in one logical line (soft wrap already joined), left to right and
// non-overlapping. Recognises quoted paths with spaces, `file:line[:column]`,
// `file(line,column)` (tsc/MSVC), Python tracebacks (`File "x.py", line 12`), pytest node
// ids, URLs, `#K7Q2` card references and the `option:`/`session:` forms; skips `--flags`, bare
// numbers and version strings.
QVector<Candidate> candidates(const QString &text);

// `candidate` against a pane's directory and board. `cwd` resolves relative paths, `home`
// expands `~`. A path the probe does not find is not a link, and neither is a card id `cards`
// does not know (`Target::valid` stays false in both cases). An `option:`/`session:` span needs
// no such probe — it is spelled out, not guessed at, so it always resolves and the *host* decides
// whether it can show the thing (`Context::resolveLink`), the way an unroutable URL is left to the
// browser. `mode` follows the surface the
// line was printed on (see `Mode`): in `Mode::Prose` a bare candidate links only to a file.
Target resolve(const Candidate &candidate, const QString &cwd, const QString &home, const Probe &probe,
               const CardLookup &cards = {}, Mode mode = Mode::Program);

struct Found {
    Candidate candidate;
    Target target;
};

// The candidates of one line that resolve, in reading order.
QVector<Found> scan(const QString &text, const QString &cwd, const QString &home, const Probe &probe,
                    const CardLookup &cards = {}, Mode mode = Mode::Program);

// The keyboard cursor over an ordered list of links (#GWXM). Index 0 is the oldest link,
// count-1 the newest (nearest the prompt).
//
// The first step from idle lands on the newest link whichever way it is going, because the
// interesting output is the one that just scrolled past. After that -1 walks towards older
// links and +1 towards newer ones, wrapping at both ends.
class Cursor {
public:
    void setCount(int count); // keeps the current index when it still exists
    int count() const { return m_count; }
    bool active() const { return m_index >= 0; }
    int index() const { return m_index; }
    // Returns the new index, or -1 when there is nothing to step to.
    int step(int delta);
    void cancel() { m_index = -1; }

private:
    int m_count = 0;
    int m_index = -1;
};

} // namespace relay::links
