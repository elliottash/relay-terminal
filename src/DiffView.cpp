// SPDX-License-Identifier: GPL-3.0-or-later
#include "DiffView.h"
#include "CopyOnSelect.h"
#include "Theme.h"

#include <QFontDatabase>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QVBoxLayout>

namespace relay {

namespace {

// ----- parsing ----------------------------------------------------------------------------------

// CRLF (and a lone CR) so a diff that travelled through Windows tooling still splits into lines.
QStringList splitLines(const QString &text) {
    QString clean = text;
    clean.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    clean.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return clean.split(QLatin1Char('\n'));
}

// `--- a/x.py\t2026-09-18 10:00:00` → `a/x.py`: git and difflib may put a timestamp after a tab.
QString headerPath(const QString &line) {
    QString rest = line.mid(4);
    const int tab = rest.indexOf(QLatin1Char('\t'));
    if (tab >= 0) rest = rest.left(tab);
    return rest.trimmed();
}

QString stripPathPrefix(const QString &path) {
    return path.startsWith(QLatin1String("a/")) || path.startsWith(QLatin1String("b/")) ? path.mid(2) : path;
}

// The lines git writes around a diff that are not part of any hunk.
bool isFileHeader(const QString &line) {
    static const char *const prefixes[] = {"--- ", "+++ ", "diff --git ", "index ", "new file mode ",
                                           "deleted file mode ", "old mode ", "new mode ",
                                           "similarity index ", "rename from ", "rename to ",
                                           "copy from ", "copy to ", "Binary files "};
    for (const char *prefix : prefixes)
        if (line.startsWith(QLatin1String(prefix))) return true;
    return false;
}

// `@@ -12,7 +12,9 @@ section heading`. A missing count means one line, as in `@@ -3 +3 @@`.
bool parseHunkHeader(const QString &line, int *oldStart, int *oldCount, int *newStart, int *newCount) {
    static const QRegularExpression header(QStringLiteral(R"(^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@)"));
    const QRegularExpressionMatch match = header.match(line);
    if (!match.hasMatch()) return false;
    *oldStart = match.captured(1).toInt();
    *oldCount = match.capturedLength(2) ? match.captured(2).toInt() : 1;
    *newStart = match.captured(3).toInt();
    *newCount = match.capturedLength(4) ? match.captured(4).toInt() : 1;
    return true;
}

// The single pass behind parseUnifiedDiff() and diffFromPreview(). `first` and `last` come back as
// the index range of the lines that belong to the diff, or -1 when the text has no diff in it.
ParsedDiff scanDiff(const QStringList &text, int *first, int *last) {
    ParsedDiff diff;
    *first = *last = -1;
    int oldNumber = 0, newNumber = 0, oldLeft = 0, newLeft = 0;
    for (int index = 0; index < text.size(); ++index) {
        const QString &raw = text.at(index);
        DiffLine line;
        line.text = raw;
        // A hunk header wins over the counts, and only over them: every line inside a hunk carries
        // a marker, so an unmarked `@@ -a,b +c,d @@` is the next hunk even when the one before it
        // promised more lines than it had (a hand-written or truncated diff).
        int oldStart = 0, oldCount = 0, newStart = 0, newCount = 0;
        const bool startsHunk = parseHunkHeader(raw, &oldStart, &oldCount, &newStart, &newCount);
        if (!startsHunk && (oldLeft > 0 || newLeft > 0)) {
            // Inside a hunk, so the counts decide: a `+++` here is a line of the file, not a header.
            if (raw.startsWith(QLatin1Char('+'))) {
                line.kind = DiffLine::Add;
                line.newLine = newNumber++;
                if (newLeft > 0) --newLeft;
                ++diff.added;
            } else if (raw.startsWith(QLatin1Char('-'))) {
                line.kind = DiffLine::Remove;
                line.oldLine = oldNumber++;
                if (oldLeft > 0) --oldLeft;
                ++diff.removed;
            } else if (raw.startsWith(QLatin1Char('\\'))) {
                line.kind = DiffLine::NoNewline;   // counts for neither side
            } else if (raw.startsWith(QLatin1Char(' ')) || raw.isEmpty()) {
                line.kind = DiffLine::Context;
                line.oldLine = oldNumber++;
                line.newLine = newNumber++;
                if (oldLeft > 0) --oldLeft;
                if (newLeft > 0) --newLeft;
            } else {
                break;   // a truncated preview: nothing after this can be trusted as the hunk's
            }
        } else if (startsHunk) {
            line.kind = DiffLine::Hunk;
            oldNumber = oldStart; oldLeft = oldCount;
            newNumber = newStart; newLeft = newCount;
        } else if (isFileHeader(raw)) {
            line.kind = DiffLine::FileHeader;
            if (raw.startsWith(QLatin1String("--- "))) {
                diff.oldPath = stripPathPrefix(headerPath(raw));
                if (diff.oldPath == QLatin1String("/dev/null")) diff.created = true;
            } else if (raw.startsWith(QLatin1String("+++ "))) {
                diff.newPath = stripPathPrefix(headerPath(raw));
            } else if (raw.startsWith(QLatin1String("new file mode "))) {
                diff.created = true;
            }
        } else if (*first >= 0 && raw.startsWith(QLatin1Char('\\'))) {
            line.kind = DiffLine::NoNewline;   // after the last line of the last hunk
        } else if (*first >= 0) {
            break;      // the preview's trailer ("Old bytes: …") — the diff ended on the line before
        } else {
            continue;   // the preview's heading and path, before the diff starts
        }
        if (*first < 0) *first = index;
        *last = index;
        diff.lines.append(line);
    }
    return diff;
}

// ----- colours ----------------------------------------------------------------------------------

QColor blend(const QColor &a, const QColor &b, double weightOfA) {
    const double w = qBound(0.0, weightOfA, 1.0);
    return QColor::fromRgbF(a.redF() * w + b.redF() * (1 - w),
                            a.greenF() * w + b.greenF() * (1 - w),
                            a.blueF() * w + b.blueF() * (1 - w));
}

// Everything one render paints with, read from the live theme tokens at the moment it runs. The
// tints are the theme's own green and red laid over its surface — a fixed Breeze green would go
// muddy on a light theme, and would not follow a theme switch.
struct DiffInk {
    QColor addBackground, removeBackground, hunkBackground, headerBackground;
    QColor text, muted, addMark, removeMark, gutter, gutterText, rule;
};

DiffInk currentInk() {
    DiffInk ink;
    ink.text = theme::Text;
    ink.muted = theme::TextMuted;
    ink.addBackground = blend(theme::Success, theme::Surface, 0.18);
    ink.removeBackground = blend(theme::Error, theme::Surface, 0.18);
    ink.hunkBackground = blend(theme::Border, theme::Surface, 0.45);
    ink.headerBackground = theme::SurfaceRaised;
    // The marker column carries the colour; the code itself stays in the reading ink.
    ink.addMark = blend(theme::Success, theme::Text, 0.8);
    ink.removeMark = blend(theme::Error, theme::Text, 0.8);
    ink.gutter = blend(theme::Background, theme::Surface, 0.5);
    ink.gutterText = theme::TextMuted;
    ink.rule = theme::Border;
    return ink;
}

}  // namespace

// ----- ParsedDiff -------------------------------------------------------------------------------

QString ParsedDiff::path() const {
    if (!newPath.isEmpty() && newPath != QLatin1String("/dev/null")) return newPath;
    return oldPath;
}

QString ParsedDiff::fileName() const {
    const QString whole = path();
    return whole == QLatin1String("/dev/null") ? QString() : whole.section(QLatin1Char('/'), -1);
}

ParsedDiff parseUnifiedDiff(const QString &text) {
    int first = -1, last = -1;
    return scanDiff(splitLines(text), &first, &last);
}

QString diffFromPreview(const QString &preview) {
    const QStringList lines = splitLines(preview);
    int first = -1, last = -1;
    scanDiff(lines, &first, &last);
    if (first < 0) return QString();
    return lines.mid(first, last - first + 1).join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString diffTitle(const ParsedDiff &diff, const QString &label) {
    QString name = label.trimmed();
    if (name.isEmpty()) name = diff.fileName();
    if (name.isEmpty()) name = QStringLiteral("Diff");
    return QStringLiteral("%1  +%2 −%3").arg(name).arg(diff.added).arg(diff.removed);
}

// ----- the text area and its gutter ---------------------------------------------------------------

class DiffTextEdit;

// The numbers live in a widget of their own, beside the text rather than inside it, so that a
// selection copies the diff and not the numbering (the split Qt's code-editor example uses).
class DiffGutter final : public QWidget {
public:
    explicit DiffGutter(DiffTextEdit *editor);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    DiffTextEdit *m_editor = nullptr;
};

class DiffTextEdit final : public QPlainTextEdit {
public:
    explicit DiffTextEdit(QWidget *parent = nullptr);

    // A pointer to the view's own vector: it is the same object for the view's whole life, so the
    // gutter always paints the diff the document holds.
    void setLines(const QVector<DiffLine> *lines) { m_lines = lines; }
    const QVector<DiffLine> *lines() const { return m_lines; }
    int gutterWidth() const;
    void paintGutter(QPaintEvent *event);
    void layOutGutter();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    // Digits per column: the widest line number in the diff, never fewer than two.
    int numberDigits() const;
    const QVector<DiffLine> *m_lines = nullptr;
    DiffGutter *m_gutter = nullptr;
};

DiffGutter::DiffGutter(DiffTextEdit *editor) : QWidget(editor), m_editor(editor) {}

QSize DiffGutter::sizeHint() const { return QSize(m_editor->gutterWidth(), 0); }

void DiffGutter::paintEvent(QPaintEvent *event) { m_editor->paintGutter(event); }

DiffTextEdit::DiffTextEdit(QWidget *parent) : QPlainTextEdit(parent) {
    setObjectName(QStringLiteral("diffText"));
    setReadOnly(true);
    setFocusPolicy(Qt::StrongFocus);
    // Tab leaves the diff rather than being eaten by a read-only editor, so the decision buttons
    // in the header above are reachable from the keyboard (26.5).
    setTabChangesFocus(true);
    setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    // Diffs are code: wrapping would break the columns the eye follows down a hunk.
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    relay::installCopyOnSelect(this);
    m_gutter = new DiffGutter(this);
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect &rect, int dy) {
        if (dy) m_gutter->scroll(0, dy);
        else m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
        if (rect.contains(viewport()->rect())) layOutGutter();
    });
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { layOutGutter(); });
    layOutGutter();
}

int DiffTextEdit::numberDigits() const {
    int widest = 0;
    if (m_lines)
        for (const DiffLine &line : *m_lines) widest = qMax(widest, qMax(line.oldLine, line.newLine));
    int digits = 2;
    while (widest >= 100) { widest /= 10; ++digits; }
    return digits;
}

int DiffTextEdit::gutterWidth() const {
    // Two columns of numbers, a gap between them and a margin either side.
    return fontMetrics().horizontalAdvance(QLatin1Char('9')) * (numberDigits() * 2 + 1) + 14;
}

void DiffTextEdit::layOutGutter() {
    setViewportMargins(gutterWidth(), 0, 0, 0);
    const QRect area = contentsRect();
    m_gutter->setGeometry(area.left(), area.top(), gutterWidth(), area.height());
}

void DiffTextEdit::resizeEvent(QResizeEvent *event) {
    QPlainTextEdit::resizeEvent(event);
    layOutGutter();
}

void DiffTextEdit::paintGutter(QPaintEvent *event) {
    const DiffInk ink = currentInk();
    QPainter painter(m_gutter);
    painter.fillRect(event->rect(), ink.gutter);
    painter.setPen(ink.rule);
    painter.drawLine(m_gutter->width() - 1, event->rect().top(), m_gutter->width() - 1, event->rect().bottom());
    if (!m_lines) return;
    const int digits = numberDigits();
    painter.setFont(font());
    painter.setPen(ink.gutterText);
    QTextBlock block = firstVisibleBlock();
    int index = block.blockNumber();
    qreal top = blockBoundingGeometry(block).translated(contentOffset()).top();
    while (block.isValid() && top <= event->rect().bottom()) {
        const qreal bottom = top + blockBoundingRect(block).height();
        if (block.isVisible() && bottom >= event->rect().top() && index < m_lines->size()) {
            const DiffLine &line = m_lines->at(index);
            const QString numbers = QStringLiteral("%1 %2")
                .arg(line.oldLine > 0 ? QString::number(line.oldLine) : QString(), digits)
                .arg(line.newLine > 0 ? QString::number(line.newLine) : QString(), digits);
            painter.drawText(QRectF(0, top, m_gutter->width() - 7, bottom - top),
                             Qt::AlignRight | Qt::AlignVCenter, numbers);
        }
        block = block.next();
        ++index;
        top = bottom;
    }
}

// ----- DiffView ---------------------------------------------------------------------------------

DiffView::DiffView(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("diffView"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(6);
    m_header = new QLabel;
    m_header->setObjectName(QStringLiteral("turnHeader"));   // the heading style the panes share
    m_header->setTextFormat(Qt::PlainText);
    m_header->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    // The header is a row, because a decision on the diff lives in it (26.5). Both buttons are
    // hidden until `setDecision` puts one there, so an ordinary tool-call diff looks as it did.
    auto *heading = new QHBoxLayout;
    heading->setContentsMargins(0, 0, 0, 0);
    heading->setSpacing(6);
    heading->addWidget(m_header, 1);
    m_reject = new QPushButton;
    m_accept = new QPushButton;
    for (QPushButton *button : {m_reject, m_accept}) {
        button->setObjectName(QStringLiteral("diffDecision"));
        button->setFocusPolicy(Qt::StrongFocus);
        button->setAutoDefault(false);
        button->hide();
        heading->addWidget(button);
    }
    connect(m_accept, &QPushButton::clicked, this, [this] { settle(true); });
    connect(m_reject, &QPushButton::clicked, this, [this] { settle(false); });
    layout->addLayout(heading);
    m_text = new DiffTextEdit;
    m_text->setLines(&m_diff.lines);
    m_text->installEventFilter(this);
    layout->addWidget(m_text, 1);
    m_header->setText(title());
    // The tints are derived at render time, so a theme switch only has to render again.
    connect(theme::notifier(), &theme::Notifier::themeChanged, this, [this] { render(); });
}

DiffView::~DiffView() {
    // The view is going away with the change on it: nobody can answer a proposal that is not on
    // screen, and the guest is waiting on one. Reject (26.5).
    settle(false);
}

void DiffView::setDecision(const QString &acceptLabel, const QString &rejectLabel,
                           std::function<void(bool accepted)> answer) {
    settle(false);                 // one decision at a time; the one before it is off the screen
    m_answer = std::move(answer);
    if (!m_answer) {
        m_accept->hide();
        m_reject->hide();
        return;
    }
    m_accept->setText(acceptLabel.isEmpty() ? QStringLiteral("Accept") : acceptLabel);
    m_reject->setText(rejectLabel.isEmpty() ? QStringLiteral("Reject") : rejectLabel);
    m_accept->show();
    m_reject->show();
}

void DiffView::clearDecision() {
    m_answer = nullptr;
    m_accept->hide();
    m_reject->hide();
}

void DiffView::settle(bool accepted) {
    if (!m_answer) return;
    auto answer = std::move(m_answer);
    m_answer = nullptr;
    m_accept->hide();
    m_reject->hide();
    answer(accepted);
}

void DiffView::setDiff(const QString &title, const QString &unifiedDiff) {
    // A new diff in this view replaces whatever was being decided; see `setDecision`.
    settle(false);
    m_label = title.trimmed();
    m_diff = parseUnifiedDiff(unifiedDiff);
    render();
    m_header->setText(this->title());
    m_header->setToolTip(m_diff.path());
}

QString DiffView::title() const { return diffTitle(m_diff, m_label); }

void DiffView::focusInput() { m_text->setFocus(Qt::OtherFocusReason); }

int DiffView::hunkCount() const {
    int hunks = 0;
    for (const DiffLine &line : m_diff.lines)
        if (line.kind == DiffLine::Hunk) ++hunks;
    return hunks;
}

QString DiffView::plainText() const { return m_text->toPlainText(); }

void DiffView::render() {
    const DiffInk ink = currentInk();
    m_text->clear();
    QTextCursor cursor(m_text->document());
    cursor.beginEditBlock();
    if (m_diff.isEmpty()) {
        QTextCharFormat quiet;
        quiet.setForeground(ink.muted);
        quiet.setFontItalic(true);
        cursor.insertText(QStringLiteral("(No text changes)"), quiet);
    }
    bool firstLine = true;
    for (const DiffLine &line : m_diff.lines) {
        if (!firstLine) cursor.insertBlock();
        firstLine = false;
        QTextBlockFormat block;
        QTextCharFormat body, marker;
        bool marked = false;   // an Add or a Remove: its first character is coloured on its own
        body.setForeground(ink.text);
        switch (line.kind) {
        case DiffLine::FileHeader:
            block.setBackground(ink.headerBackground);
            body.setFontWeight(QFont::Bold);
            break;
        case DiffLine::Hunk:
            block.setBackground(ink.hunkBackground);
            body.setForeground(ink.muted);
            break;
        case DiffLine::Add:
            block.setBackground(ink.addBackground);
            marker = body;
            marker.setForeground(ink.addMark);
            marker.setFontWeight(QFont::Bold);
            marked = true;
            break;
        case DiffLine::Remove:
            block.setBackground(ink.removeBackground);
            marker = body;
            marker.setForeground(ink.removeMark);
            marker.setFontWeight(QFont::Bold);
            marked = true;
            break;
        case DiffLine::NoNewline:
        case DiffLine::Other:
            body.setForeground(ink.muted);
            body.setFontItalic(true);
            break;
        case DiffLine::Context:
            break;
        }
        cursor.setBlockFormat(block);
        // The +/- marker in the state's colour, the code in the reading ink: the tint says what
        // happened at a glance, and the code stays as legible as it is in a preview pane.
        if (marked && !line.text.isEmpty()) {
            cursor.setCharFormat(marker);
            cursor.insertText(line.text.left(1));
            cursor.setCharFormat(body);
            cursor.insertText(line.text.mid(1));
        } else {
            cursor.setCharFormat(body);
            cursor.insertText(line.text);
        }
    }
    cursor.endEditBlock();
    m_text->moveCursor(QTextCursor::Start);
    m_text->layOutGutter();
    m_text->viewport()->update();
}

bool DiffView::nextHunk() { return jumpToHunk(1); }

bool DiffView::previousHunk() { return jumpToHunk(-1); }

bool DiffView::jumpToHunk(int direction) {
    const int here = m_text->textCursor().blockNumber();
    int target = -1;
    for (int index = 0; index < m_diff.lines.size(); ++index) {
        if (m_diff.lines.at(index).kind != DiffLine::Hunk) continue;
        if (direction > 0 && index > here) { target = index; break; }
        if (direction < 0 && index < here) target = index;   // the last one before the cursor
    }
    if (target < 0) return false;
    QTextCursor cursor(m_text->document()->findBlockByNumber(target));
    m_text->setTextCursor(cursor);
    m_text->centerCursor();
    return true;
}

bool DiffView::eventFilter(QObject *object, QEvent *event) {
    if (object == m_text && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->modifiers() == Qt::NoModifier) {
            if (key->key() == Qt::Key_N) { nextHunk(); return true; }
            if (key->key() == Qt::Key_P) { previousHunk(); return true; }
        }
    }
    return QWidget::eventFilter(object, event);
}

}  // namespace relay
