// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Unified diffs from the agent's file tools: the parser that the concise tool-call notification
// and the diff pane share, and the read-only viewer that shows one.
//
// The worker does not hand the GUI a bare diff — it hands it a tool *preview*
// (backend/relay_core/tools.py, `write_file`):
//
//     WRITE FILE
//
//     /abs/path/x.py
//
//     --- a/x.py
//     +++ b/x.py
//     @@ -1,3 +1,4 @@
//      keep
//     -old
//     +new
//
//     Old bytes: 12; new bytes: 30.
//
// so parseUnifiedDiff() skips everything before the first header or hunk and stops at the first
// line that cannot belong to the diff; diffFromPreview() returns that slice as text for a caller
// that wants to re-render it elsewhere. Both are pure — no widget, no theme — so every rule is
// tested headless (tests/diffview_test.cpp).
//
// Inside a hunk the *counts* in `@@ -a,b +c,d @@` decide what a line is, not its first character:
// a file being written may itself contain lines starting with `+++`, `---` or `@@`.
#include <QString>
#include <QVector>
#include <QWidget>

class QLabel;

namespace relay {

// One line of a parsed diff, in the order the diff had them. `text` is the raw line, marker and
// all (" context", "-old", "+new"), so text selected in the view is still a usable diff.
struct DiffLine {
    enum Kind { FileHeader, Hunk, Context, Add, Remove, NoNewline, Other };
    Kind kind = Other;
    QString text;
    // 1-based numbers in the old and in the new file, or 0 where the line has none: the headers,
    // and the side an added or a removed line does not exist on.
    int oldLine = 0;
    int newLine = 0;
};

struct ParsedDiff {
    // The paths from `--- ` and `+++ `, with git's `a/` and `b/` prefix and any trailing
    // timestamp removed. Either may be "/dev/null".
    QString oldPath, newPath;
    bool created = false;         // `--- /dev/null` or `new file mode`: the file is new
    int added = 0, removed = 0;   // lines, not bytes
    QVector<DiffLine> lines;

    bool isEmpty() const { return lines.isEmpty(); }
    // The file the diff is about: the new path, or the old one when the file was deleted.
    QString path() const;
    // Its last component, which is what a title shows.
    QString fileName() const;
};

// Parses one file's unified diff, tolerating a tool preview around it, CRLF, several hunks,
// `\ No newline at end of file` and a diff that is not there at all ("(No text changes)").
ParsedDiff parseUnifiedDiff(const QString &text);

// The diff inside a tool preview, as text with a trailing newline. Empty when the preview has
// no diff in it, which is how "(No text changes)" arrives.
QString diffFromPreview(const QString &preview);

// The one-line title: a name plus the two counts, e.g. "x.py  +3 −1". `label` overrides the name
// from the diff, which is what a caller with a nicer path (or a tool's own wording) passes.
QString diffTitle(const ParsedDiff &diff, const QString &label = QString());

// The viewer's text area and its line-number gutter. Defined in DiffView.cpp: nothing outside
// needs the type, but DiffView holds one by pointer.
class DiffTextEdit;

// A read-only view of one unified diff: a heading row, then the diff itself in a monospace area
// with an old/new line-number gutter beside it, added lines on a green tint and removed lines on
// a red tint. The tints come from the live theme tokens (src/Theme.h) and are re-derived on
// themeChanged(), so a light theme gets a light tint and switching needs no new view.
//
// Keys: the usual scrolling keys and Ctrl+C from the text area, and n / p for the next and the
// previous hunk. The line numbers are painted beside the text rather than inserted into it, so a
// copy yields the plain diff, markers included.
class DiffView final : public QWidget {
public:
    explicit DiffView(QWidget *parent = nullptr);

    // `title` names the file in the heading (the pane host usually passes the path it opened);
    // empty falls back to the file name in the diff. `unifiedDiff` may be a whole tool preview.
    void setDiff(const QString &title, const QString &unifiedDiff);
    // "x.py  +3 −1".
    QString title() const;
    // The pane host calls this when the pane takes focus; the text area is what reads keys.
    void focusInput();

    const ParsedDiff &diff() const { return m_diff; }
    int lineCount() const { return int(m_diff.lines.size()); }
    int hunkCount() const;
    // The diff as text, exactly as selecting everything and copying would yield it.
    QString plainText() const;
    // n / p. Both return false when there is no hunk to move to.
    bool nextHunk();
    bool previousHunk();

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    void render();
    bool jumpToHunk(int direction);
    ParsedDiff m_diff;
    QString m_label;
    QLabel *m_header = nullptr;
    DiffTextEdit *m_text = nullptr;
};

}  // namespace relay
