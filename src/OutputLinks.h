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
    Path, // a file or folder, possibly with :line:column
    Url,  // scheme://rest — left alone, never treated as a path
    Card, // #K7Q2 — a Switchboard card the pane has seen (design section 5)
};

// One span of a logical line that could be a link. `start`/`length` are UTF-16 indices
// into the line that was scanned, so a view can underline exactly that text.
struct Candidate {
    int start = 0;
    int length = 0;
    Kind kind = Kind::Path;
    QString text;      // the source text of the span (quotes already removed)
    QString path;      // Path: the span without the :line:column suffix. Card: the id, upper-cased.
    int line = -1;     // 1-based, -1 when the span carries none
    int column = -1;
};

// What the filesystem says about an absolute path.
enum class Entry {
    Missing = -1,
    File = 0,
    Directory = 1,
};

using Probe = std::function<Entry(const QString &absolutePath)>;

// QFileInfo. Callers that scan a lot of text should keep one instance.
Probe systemProbe();

// Whether the caller's Switchboard index knows a card id (upper-cased, four characters), and
// what that card is called — `title` is left alone when the board has no title for it. This is
// the card equivalent of `Probe`: a pane that has seen no board hands in nothing, and then
// `#K7Q2` in its output stays plain text, exactly as an unknown id does.
using CardLookup = std::function<bool(const QString &id, QString *title)>;

// The target a card reference resolves to: `relay://card/K7Q2`. src/main.cpp's
// openOutputTarget() already routes relay:// targets, so a clicked, keyboard-walked or
// right-clicked card link travels the same one path as every other link.
QString cardTarget(const QString &id);
// The id inside a `relay://card/<id>` target, or empty when it is not one.
QString cardIdOf(const QString &target);

struct Target {
    bool valid = false;
    Kind kind = Kind::Path;
    QString target;         // the URL as written, the cleaned absolute path, or relay://card/<id>
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
// ids, URLs and `#K7Q2` card references; skips `--flags`, bare numbers and version strings.
QVector<Candidate> candidates(const QString &text);

// `candidate` against a pane's directory and board. `cwd` resolves relative paths, `home`
// expands `~`. A path the probe does not find is not a link, and neither is a card id `cards`
// does not know (`Target::valid` stays false in both cases).
Target resolve(const Candidate &candidate, const QString &cwd, const QString &home, const Probe &probe,
               const CardLookup &cards = {});

struct Found {
    Candidate candidate;
    Target target;
};

// The candidates of one line that resolve, in reading order.
QVector<Found> scan(const QString &text, const QString &cwd, const QString &home, const Probe &probe,
                    const CardLookup &cards = {});

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
