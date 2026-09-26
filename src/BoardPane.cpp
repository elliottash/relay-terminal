// SPDX-License-Identifier: AGPL-3.0-or-later
#include "AppPaths.h"   // dataRoot, relayPython: where the `land.py` to ask about orphans lives
#include "BoardPane.h"
#include "CardPane.h"    // CardDetail, the card page (moved out of this file, #Y2BA)
#include "ModelCatalog.h"   // nameOf: a comment's `model=` is an id; the page prints the name
#include "PaneStatus.h"   // listHue: the hue this pane's band wears, which its rows select in (#MXMG)
#include "Projects.h"   // which folder of a project is its board: `switchboard/`, else `issues/`
#include "ToolLabel.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDrag>
#include <QDir>
#include <QDirIterator>
#include <QDropEvent>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFocusEvent>
#include <QFontDatabase>
#include <QFrame>
#include <QHash>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPainterPath>
#include <QCryptographicHash>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextLayout>
#include <QTimeZone>
#include <QToolButton>
#include <QTreeWidget>
#include <QBrush>
#include <QDesktopServices>
#include <QDir>
#include <QPalette>
#include <QUrl>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

#include "CopyOnSelect.h"
#include "Notifications.h"   // a signal thread's pickup says so in the bell (#AQ6X phase 3)
#include "RichEditor.h"
#include "Theme.h"

namespace relay {
namespace {

constexpr int kCardRole = Qt::UserRole;          // the card id on a card row; empty on a header

// True while one of the console's action buttons is inside its `Action::run`. The console
// rebuilds its whole action row when the context says something moved, and a rebuild frees the
// button whose `clicked` is still on the stack (src/Pane.h, `rebuildActionRow`, deletes each
// widget outright) — Clean up turning into Stop would delete itself mid-click. So a refresh
// raised from inside an action is posted to the next turn of the event loop and every other one
// is immediate, which is what keeps the row right the instant a card is opened.
// The counter and guard live in src/CardPane.h (relay::boardActionDepth, relay::ActionGuard)
// since #Y2BA moved CardDetail there; the list side and the card page share the one counter.

// Row geometry (owner review, 2026-09-18: with ~96 cards the Trello columns ran off the right
// edge and wasted the height, and a single-owner tracker reads better as rows). One line per
// card and one per section header, measured the same way for sizeHint() and paint().
constexpr int kRowPadX = 10;
constexpr int kGlyphWidth = 15;      // the status mark's box, so every title starts at one x
constexpr int kBadgeGap = 5;
constexpr int kIdGap = 8;
constexpr int kTitleMin = 80;        // the title never shrinks past this; badges go instead
constexpr int kAddWidth = 22;        // the `+` at the right of a section header
// The date columns at the right of a row (owner, 2026-09-19: "add a 'created' and 'updated'
// column"): the gap between the two and before the badges, and the room an `#ID` is owed when
// deciding whether the pane can carry the columns at all. That room is a constant rather than the
// card's own id width, so the header over the list can answer the same question the rows do — a
// header whose labels had gone while the cells were still drawn would be worse than no header.
constexpr int kDateGap = 8;

// A section's tooltip, with what the section is for on a line of its own underneath. Every
// surface that names a section without showing its cards goes through here, so the answer to
// "what is Ready to start?" is in one place (board::sectionMeaning) and reads the same in all
// of them. A section id with no definition — a column a board invented — keeps its tip as is.
QString withMeaning(const QString &tip, const QString &sectionId)
{
    const QString why = board::sectionMeaning(sectionId);
    return why.isEmpty() ? tip : tip + QLatin1Char('\n') + why;
}

QColor mix(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

QColor alpha(QColor color, int value)
{
    color.setAlpha(value);
    return color;
}

// Both stop at theme::FloorPt: a factor of the row's font is how the list keeps its proportions,
// but 0.8 of a 9pt default was 7.2pt (docs/ARCHITECTURE.md, "Legible text").
QFont smaller(const QFont &base, qreal factor)
{
    QFont font(base);
    if (base.pointSizeF() > 0)
        font.setPointSizeF(base.pointSizeF() * factor);
    else
        font.setPixelSize(qMax(8, int(base.pixelSize() * factor)));
    return theme::legible(font);
}

QFont monoFont(const QFont &base, qreal factor)
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (base.pointSizeF() > 0)
        font.setPointSizeF(base.pointSizeF() * factor);
    return theme::legible(font);
}

// The ink a badge is drawn in, and its pill's edge. An invalid edge means no pill at all: the
// quiet end of the row (the age, the thread count) is plain text, so a busy row has fewer boxes.
QPair<QColor, QColor> badgeInk(board::Badge::Kind kind)
{
    switch (kind) {
    case board::Badge::Agent:
        return {theme::Agent, mix(theme::Agent, theme::Surface, 0.55)};
    case board::Badge::Waiting:
        return {theme::Warning, mix(theme::Warning, theme::Surface, 0.5)};
    case board::Badge::Due:
        return {theme::Warning, mix(theme::Warning, theme::Surface, 0.5)};
    case board::Badge::Overdue:
        return {theme::Error, mix(theme::Error, theme::Surface, 0.5)};
    case board::Badge::TasksDone:
        return {theme::Success, mix(theme::Success, theme::Surface, 0.6)};
    // The same green as a finished checklist, and for the same reason: something that had to be
    // done is done. It is a fact about the card, so it wears a pill like the other facts.
    case board::Badge::Verified:
        return {theme::Success, mix(theme::Success, theme::Surface, 0.45)};
    case board::Badge::Status:
        return {theme::Text, theme::BorderStrong};
    case board::Badge::Assignee:
        return {theme::Text, theme::Border};
    case board::Badge::Private:
        // Brass, not amber: a card being private is a fact about it, not somebody waiting on you,
        // and amber means only the second (be81edb). Waiting above keeps the amber.
        return {theme::Tool, mix(theme::Tool, theme::Surface, 0.5)};
    // The pane holding the card (#R9G7): the link colour, because on the card page the same chip
    // *is* a link to that pane and the row should read as the same thing. Once the pane has gone
    // the chip is muted like the rest of the history.
    case board::Badge::Session:
        return {theme::Link, mix(theme::Link, theme::Surface, 0.55)};
    case board::Badge::SessionClosed:
        return {theme::TextMuted, theme::Border};
    case board::Badge::Thread:
        return {theme::TextMuted, QColor()};
    default:
        return {theme::TextMuted, theme::Border};
    }
}

// The hue the Board's rows select in (#MXMG): the brass its band wears — or, with pane
// colours off, the accent every other list selects in, because "off" should not leave this
// one pane warmer than the rest. Asked per row: a couple of colour mixes, the cheapest thing
// in this paint path.
QColor boardHue() {
    namespace t = relay::theme;
    const QColor hue = relay::panestatus::listHue(
        QStringLiteral("board"),
        {t::Background, t::Text, t::TextMuted, t::Shell, t::Agent, t::Success, t::Warning,
         t::Error, t::Action, t::Tool});
    return hue.isValid() ? hue : t::Accent;
}

// The Stage pill's ink (#MXMG). Only the stages that already own a colour in the app's
// vocabulary wear one: done is the green of a finished checklist, executing the agent's
// violet, the stages that wait on a verifier the amber of "waiting on someone". Every other
// stage stays neutral — the pill is shape first; a second rainbow on every row would carry
// no more information than the words already do.
QPair<QColor, QColor> stageInk(const QString &status)
{
    if (status == QStringLiteral("done") || status == QStringLiteral("Verified"))
        return {theme::Success, mix(theme::Success, theme::Surface, 0.55)};
    if (status == QStringLiteral("executing"))
        return {theme::Agent, mix(theme::Agent, theme::Surface, 0.55)};
    static const QStringList waiting{QStringLiteral("needs-verification"),
                                     QStringLiteral("needs-qa-llm"), QStringLiteral("needs-qa-human"),
                                     QStringLiteral("needs-review")};
    if (waiting.contains(status))
        return {theme::Warning, mix(theme::Warning, theme::Surface, 0.5)};
    if (status == QStringLiteral("dropped") || status == QStringLiteral("deferred"))
        return {theme::TextMuted, theme::BorderStrong};
    return {theme::Text, theme::BorderStrong};
}

// The Stage pill's face (#MXMG): mono and letter-spaced like the id and the engraved
// headers, small caps by upper-casing the way the headers do it — the switchboard's
// instrument typeface, where the Sessions & Projects list keeps proportional type.
QFont stagePillFont(const QFont &base)
{
    QFont font = monoFont(base, 0.78);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.4);
    font.setWeight(QFont::DemiBold);
    return font;
}

// whether or not one is showing.
int rowReserve(const QListWidget *list)
{
    return list->verticalScrollBar()->sizeHint().width() + 2 + 2 * list->frameWidth();
}

// The width a row's content is drawn in. The column header over the list measures with this too,
// so a header cell sits exactly over the column of cells below it.
int rowContentWidth(const QListWidget *list)
{
    return qMax(120, list->width() - rowReserve(list));
}

// One date column's width: "0000-00-00" in the row's own date font, so both columns are the same
// width, every row lines up with the next, and the header's label is measured over the same cell.
int dateColumnWidth(const QFont &font)
{
    return QFontMetrics(monoFont(font, 0.85)).horizontalAdvance(QStringLiteral("0000-00-00")) + 6;
}

// The room the `#ID` column takes: "#WWWW" in the id's own mono — the widest id the format
// allows — so every id lines up in one fixed column between the flag and the title (#VKFV).
int idColumnWidth(const QFont &font)
{
    return QFontMetrics(monoFont(font, 0.85)).horizontalAdvance(QStringLiteral("#WWWW")) + kIdGap;
}

// The room the ⧉ beside the `#ID` takes (#FT77): the copy glyph in the id's own mono, with a gap
// either side. It is the column's room, so every row's ⧉ lines up like every row's id.
int idCopyWidth(const QFont &font)
{
    return QFontMetrics(monoFont(font, 0.85)).horizontalAdvance(QStringLiteral("⧉")) + 6;
}

// Whether a row `width` px wide can carry the two date columns at all: the flag, the `#ID`
// column and the title's floor are owed their room first, and only then does the table get its
// right-hand columns.
bool dateColumnsFit(const QFont &font, int width)
{
    const int keep = kRowPadX + kGlyphWidth + 4 + idColumnWidth(font) + idCopyWidth(font)
                     + kTitleMin + kDateGap + 2 * dateColumnWidth(font) + kDateGap + kRowPadX;
    return width >= keep;
}

// Whether a row can carry the Viewed column (#FKSN) as well, rightmost after Updated. It is the
// first of the right-hand columns a narrowing pane gives up — before the Stage and the two dates —
// so adding it never takes Created or Updated off a pane that had room for them.
bool viewedColumnFits(const QFont &font, int width)
{
    return dateColumnsFit(font, width - dateColumnWidth(font) - kDateGap);
}

// The Stage column of a flat list (#ESDF): the widest stage name in the badges' font, so every
// row's stage lines up and the header's STAGE sits over the same cell.
int stageColumnWidth(const QFont &font)
{
    return QFontMetrics(smaller(font, 0.85)).horizontalAdvance(QStringLiteral("Needs verification"))
           + 6;
}

// Whether a row can carry the Stage column beside the two dates. A narrower flat row names its
// stage as a status badge instead, so the stage is never simply gone.
bool stageColumnFits(const QFont &font, int width)
{
    return dateColumnsFit(font, width - stageColumnWidth(font) - kDateGap);
}

// Where everything on one card row goes, relative to the row's top-left corner. The badges that
// do not fit are already gone (board::fitBadges) and the title is elided into what is left, so a
// narrow pane loses decoration before it loses meaning.
struct CardShape {
    QRect priorityRect, idRect, idCopyRect, titleRect, createdRect, updatedRect, viewedRect,
        stageRect;
    QString title, created, updated, viewed, stage;   // `stage` is empty but in a flat list (#ESDF)
    bool dates = false;                 // the two date columns are on this row
    bool viewedColumn = false;          // and the Viewed one after them (#FKSN)
    QList<QPair<board::Badge, QRect>> badges;
    // (#G2C7) While the text filter is on: the runs its terms make in the title, and the
    // matched line from the card's text as one small line of its own under the row.
    QVector<QPair<int, int>> titleRuns;
    QString snippet;
    QVector<QPair<int, int>> snippetRuns;
    QRect snippetRect;
    int height = 0;
};

// `sessionLive` says whether the pane the card was claimed by is still open (#R9G7): it only
// changes what the claim chip says, but it is measured with the rest of the badges, so it has to
// be known before the row is laid out.
CardShape cardShape(const board::Card &card, bool showStatus, const QString &stage,
                    const QFont &font, int width, bool sessionLive = true,
                    const QVector<QPair<int, int>> &titleRuns = {},
                    const QString &snippet = QString(),
                    const QVector<QPair<int, int>> &snippetRuns = {})
{
    CardShape shape;
    const QFontMetrics metrics(font);
    const QFontMetrics badgeMetrics(smaller(font, 0.85));
    shape.height = qMax(24, metrics.height() + 10);

    // The flag column, then the `#ID` column, then the title (#VKFV): the id is a fixed column
    // of its own now, so the ids of every row line up whatever the titles say.
    int x = kRowPadX;
    shape.priorityRect = QRect(x, 0, kGlyphWidth, shape.height);
    x += kGlyphWidth + 4;
    shape.idRect = QRect(x, 0, idColumnWidth(font) - kIdGap, shape.height);
    // The ⧉ that copies the reference (#FT77), straight after the id's own glyphs. Its room is
    // owed before the title's: a narrow pane loses title before it loses the button.
    const QFont idFont = monoFont(font, 0.85);
    const int idTextWidth = QFontMetrics(idFont).horizontalAdvance(card.reference());
    shape.idCopyRect = QRect(shape.idRect.left() + idTextWidth + 3, 0,
                             QFontMetrics(idFont).horizontalAdvance(QStringLiteral("⧉")) + 4,
                             shape.height);
    x += idColumnWidth(font) + idCopyWidth(font);

    // The table's right-hand columns first: the badges and the title are what give way to them,
    // and the badges that no longer fit are dropped by board::fitBadges — a narrow pane loses
    // decoration before it loses meaning.
    shape.dates = dateColumnsFit(font, width);
    int contentRight = width - kRowPadX;
    shape.viewedColumn = shape.dates && viewedColumnFits(font, width);
    int stageWidth = width;   // what stageColumnFits measures: the row less the Viewed column
    if (shape.viewedColumn) {
        const int column = dateColumnWidth(font);
        shape.viewedRect = QRect(contentRight - column, 0, column, shape.height);
        shape.viewed = board::dateCell(card.viewed);
        contentRight = shape.viewedRect.left() - kDateGap;
        stageWidth -= column + kDateGap;
    }
    if (shape.dates) {
        const int column = dateColumnWidth(font);
        shape.updatedRect = QRect(contentRight - column, 0, column, shape.height);
        shape.createdRect = QRect(shape.updatedRect.left() - kDateGap - column, 0, column,
                                  shape.height);
        shape.created = board::dateCell(card.created);
        shape.updated = board::dateCell(card.updated);
        contentRight = shape.createdRect.left() - kDateGap;
    }
    // The Stage column, left of the dates, on every card row (#ESDF, #YN4D) that is wide
    // enough; otherwise the stage rides the badges as the status one.
    if (!stage.isEmpty()) {
        if (stageColumnFits(font, stageWidth)) {
            const int column = stageColumnWidth(font);
            shape.stageRect = QRect(contentRight - column, 0, column, shape.height);
            shape.stage = QFontMetrics(stagePillFont(font)).elidedText(stage, Qt::ElideRight,
                                                                 column);
            contentRight = shape.stageRect.left() - kDateGap;
        } else {
            showStatus = true;
        }
    }
    const int available = qMax(40, contentRight - x);

    // No single badge may eat the row. A card whose `assignee` holds a sentence is a mistake in
    // the file, but the row still has to be readable, so a badge is capped at a quarter of the
    // width and elided inside its pill.
    const int cap = qMax(60, available / 4);
    QHash<QString, int> widths;
    QList<QPair<board::Badge, int>> measured;
    for (const board::Badge &badge : board::badges(card, showStatus, sessionLive)) {
        // The claim chip is exempt (#R9G7). The cap is there for a badge whose text came out of
        // the card file and could be a sentence; this one is the glyph, eight characters and at
        // most " closed", so it cannot eat the row however narrow the pane is — and elided to
        // "37fa10…" it names nobody, which is the whole of what it is for. It fits whole, or
        // fitBadges drops it and the tooltip still says who holds the card.
        const bool bounded = badge.kind == board::Badge::Session
                             || badge.kind == board::Badge::SessionClosed;
        const int full = badgeMetrics.horizontalAdvance(badge.text) + 12;
        const int w = bounded ? full : qMin(cap, full);
        widths.insert(badge.text, w);
        measured << qMakePair(badge, w);
    }
    // What the title is owed before a badge may have anything: the rest of the row is decoration
    // next to knowing which card this is.
    const int titleFloor = qBound(kTitleMin, available * 45 / 100, 280);
    const QList<board::Badge> kept =
        board::fitBadges(measured, qMax(0, available - titleFloor - 12), kBadgeGap);

    // Right to left from the row's right edge, prepending, so the order on screen ends up the
    // reading order board::badges returned.
    int right = contentRight;
    for (int i = int(kept.size()) - 1; i >= 0; --i) {
        const int w = widths.value(kept.at(i).text);
        const int h = badgeMetrics.height() + 2;
        right -= w;
        shape.badges.prepend(qMakePair(kept.at(i), QRect(right, (shape.height - h) / 2, w, h)));
        right -= kBadgeGap;
    }
    const int titleEnd = shape.badges.isEmpty() ? contentRight : right + kBadgeGap - 10;
    const int titleWidth = qMax(30, titleEnd - x);
    shape.title = metrics.elidedText(card.title.isEmpty() ? QStringLiteral("(untitled)")
                                                          : card.title,
                                     Qt::ElideRight, titleWidth);
    const int drawn = qMin(titleWidth, metrics.horizontalAdvance(shape.title));
    shape.titleRect = QRect(x, 0, drawn, shape.height);
    // (#G2C7) The filter's marks ride the row: the title's runs are clamped to the elided
    // title when they are painted, and a card the text matched grows one small line under
    // the row for the line that matched — its runs index the snippet as laid out.
    shape.titleRuns = titleRuns;
    if (!snippet.isEmpty()) {
        const QFontMetrics small(smaller(font, 0.85));
        shape.snippet = snippet;
        shape.snippetRuns = snippetRuns;
        shape.snippetRect = QRect(x, shape.height, width - kRowPadX - x, small.height());
        shape.height += small.height() + 4;
    }
    return shape;
}

// A section header: a chevron, the status name, its count, and a `+` that adds into it.
struct SectionShape {
    QRect chevronRect, titleRect, addRect, triageRect;
    int height = 0;
};

SectionShape sectionShape(const QFont &font, int width, bool first, bool canAdd)
{
    SectionShape shape;
    const QFontMetrics metrics(smaller(font, 0.85));
    const int top = first ? 6 : 14;              // the rule above needs air; the first has none
    shape.height = top + metrics.height() + 6;
    int x = kRowPadX;
    shape.chevronRect = QRect(x, top, 12, metrics.height());
    x += 14;
    // The triage ⚠ (#8YQ9) sits inside the `+`, at the row's end: every section can be checked,
    // including Done, so unlike the `+` its room is reserved on every header rather than only on
    // the ones that take new cards. Both are drawn on hover only — a header at rest is a name.
    int right = width - kRowPadX;
    if (canAdd) {
        shape.addRect = QRect(right - kAddWidth, top, kAddWidth, metrics.height());
        right -= kAddWidth;
    }
    shape.triageRect = QRect(right - kAddWidth, top, kAddWidth, metrics.height());
    right -= kAddWidth;
    shape.titleRect = QRect(x, top, qMax(20, right - x), metrics.height());
    return shape;
}

// The fold row that stands for a section's self-closed cards (#93WR): a chevron and one muted
// line, "3 closed by the agent". It is a row inside the section's cards rather than a header of
// its own, so the chevron sits where the card rows' `#ID` column starts and there is no rule
// above it — the eye reads it as the last of the cards, which is what it stands for.
struct FoldShape {
    QRect chevronRect, titleRect;
    int height = 0;
};

FoldShape foldShape(const QFont &font, int width)
{
    FoldShape shape;
    const QFontMetrics metrics(smaller(font, 0.85));
    shape.height = qMax(22, metrics.height() + 8);
    int x = kRowPadX + kGlyphWidth + 4;
    shape.chevronRect = QRect(x, 0, 12, shape.height);
    x += 14;
    shape.titleRect = QRect(x, 0, qMax(20, width - kRowPadX - x), shape.height);
    return shape;
}

// One signal's row (#AQ6X): the kind as a word — a glyph cannot say "flaky" — then the key in the
// same mono the `#ID` column uses, and at the right end the failure count, when it was last seen,
// its marks, and the claim chip a card wears. A member of a group is one step further in, under
// the row it belongs to.
struct SignalShape {
    QRect kindRect, keyRect, tailRect, chipRect;
    QString tail;          // "×4 · 3 h ago · regressed"
    QString chip;          // "⧉ abcdef12", empty when nobody has claimed it
    bool chipLive = true;
    int height = 0;
};

SignalShape signalShape(const board::Signal &signal, const QFont &font, int width, int indent,
                        const QDateTime &now, bool chipLive)
{
    SignalShape shape;
    const QFont small = smaller(font, 0.85);
    const QFontMetrics metrics(small);
    const QFontMetrics mono(monoFont(font, 0.85));
    shape.height = qMax(22, metrics.height() + 8);
    QStringList tail;
    const QString count = board::signalCountWord(signal.count);
    if (!count.isEmpty())
        tail << count;
    const QString age = board::signalAge(signal.lastSeen, now);
    if (!age.isEmpty())
        tail << age;
    tail << board::signalMarks(signal, now);
    shape.tail = tail.join(QStringLiteral(" · "));
    shape.chip = signal.session.isEmpty() ? QString()
                                          : board::sessionChip(signal.session, chipLive);
    shape.chipLive = chipLive;

    int x = kRowPadX + kGlyphWidth + 4 + indent * 14;
    const int kindWidth = metrics.horizontalAdvance(QStringLiteral("broken")) + 10;
    shape.kindRect = QRect(x, 0, kindWidth, shape.height);
    x += kindWidth;
    int right = width - kRowPadX;
    if (!shape.chip.isEmpty()) {
        const int chipWidth = metrics.horizontalAdvance(shape.chip) + 12;
        shape.chipRect = QRect(right - chipWidth, (shape.height - metrics.height() - 4) / 2,
                               chipWidth, metrics.height() + 4);
        right -= chipWidth + 8;
    }
    if (!shape.tail.isEmpty()) {
        const int tailWidth = metrics.horizontalAdvance(shape.tail);
        shape.tailRect = QRect(right - tailWidth, 0, tailWidth, shape.height);
        right -= tailWidth + 10;
    }
    shape.keyRect = QRect(x, 0, qMax(20, right - x), shape.height);
    Q_UNUSED(mono);
    return shape;
}

// Paints the single list: a section header or one card per row. It reads the card from the model
// by id, so a row holds nothing but the id and a refill never copies card data into the view.
class RowDelegate final : public QStyledItemDelegate {
public:
    RowDelegate(const board::Model *model, const QList<board::Row> *rows, QListWidget *list,
                const board::SignalsState *signalsState)
        : QStyledItemDelegate(list), m_model(model), m_rows(rows), m_list(list),
          m_signals(signalsState)
    {
    }

    // Which cards have an agent turn running on them (protocol 19.16). Several can, and the one
    // on screen is not necessarily one of them, so the list is where you see the others working.
    std::function<QString(const QString &)> turnMode;

    // Whether the pane a card was claimed by is still open (#R9G7). Asked once per row as it is
    // measured and once as it is painted, so the list picks a closing pane up at its next repaint
    // rather than holding a stale answer; unset — a test — means every claim reads as live.
    std::function<bool(const QString &)> paneExists;
    bool sessionLive(const board::Card &card) const
    {
        return card.session.isEmpty() || !paneExists || paneExists(card.session);
    }
    bool sessionLive(const QString &token) const
    {
        return token.isEmpty() || !paneExists || paneExists(token);
    }

    // The row's width comes from the list with room for its scrollbar kept whether or not the
    // scrollbar is showing: measured at one width and painted at another, the elision would be
    // computed for a row wider than the one drawn.
    int rowWidth() const { return rowContentWidth(m_list); }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const board::Row *row = rowAt(index.row());
        const int width = rowWidth();
        if (!row)
            return QSize(width, 0);
        if (row->kind == board::Row::Section)
            return QSize(width, sectionShape(option.font, width, index.row() == 0, false).height);
        if (row->kind == board::Row::Fold || row->kind == board::Row::SignalFold
            || row->kind == board::Row::DismissedFold)
            return QSize(width, foldShape(option.font, width).height);
        if (row->kind == board::Row::Signal) {
            const board::Signal *signal = m_signals ? m_signals->signalFor(row->signalKey) : nullptr;
            return QSize(width, signal ? signalShape(*signal, option.font, width, row->indent,
                                                     QDateTime::currentDateTimeUtc(),
                                                     sessionLive(signal->session)).height
                                       : 0);
        }
        const board::Card *card = m_model->card(row->cardId);
        if (!card)
            return QSize(width, 0);
        return QSize(width, filteredShape(*card, row->showStatus, row->stage, option.font, width,
                                          sessionLive(*card)).height);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        const board::Row *row = rowAt(index.row());
        if (!row)
            return;
        QRect rect = option.rect;
        rect.setWidth(qMin(rect.width(), rowWidth()));
        painter->save();
        painter->setClipRect(rect);
        painter->setRenderHint(QPainter::Antialiasing);
        if (row->kind == board::Row::Section)
            paintSection(painter, option, rect, *row, index.row() == 0);
        else if (row->kind == board::Row::Fold || row->kind == board::Row::SignalFold
                 || row->kind == board::Row::DismissedFold)
            paintFold(painter, option, rect, *row);
        else if (row->kind == board::Row::Signal)
            paintSignal(painter, option, rect, *row);
        else
            paintCard(painter, option, rect, *row);
        painter->restore();
    }

    // The `+` of the header at `rowIndex`, given that row's rect, or an empty rect.
    QRect addRectOf(int rowIndex, const QRect &itemRect) const
    {
        const board::Row *row = rowAt(rowIndex);
        if (!row || row->kind != board::Row::Section || !adds(row->columnId))
            return QRect();
        return sectionShape(m_list->font(), rowWidth(), rowIndex == 0, true)
            .addRect.translated(itemRect.topLeft());
    }

    // The ⚠ of the header at `rowIndex` (#8YQ9), given that row's rect, or an empty rect. Every
    // section has one — a section that takes no new cards can still hold a malformed card.
    QRect triageRectOf(int rowIndex, const QRect &itemRect) const
    {
        const board::Row *row = rowAt(rowIndex);
        if (!row || row->kind != board::Row::Section)
            return QRect();
        return sectionShape(m_list->font(), rowWidth(), rowIndex == 0, adds(row->columnId))
            .triageRect.translated(itemRect.topLeft());
    }

    // The flag at the left of a card row (#VKFV), given that row's rect: the box the ring or the
    // disc is painted in, widened a little because it is a click target, not a mark.
    QRect priorityRectOf(int rowIndex, const QRect &itemRect) const
    {
        const board::Row *row = rowAt(rowIndex);
        if (!row || row->kind != board::Row::Card)
            return QRect();
        return cardShape(board::Card{}, false, QString(), m_list->font(), rowWidth())
            .priorityRect.translated(itemRect.topLeft());
    }

    // The ⧉ beside a card row's `#ID` (#FT77), given that row's rect: a click target, exactly
    // where paint() draws the glyph.
    QRect idCopyRectOf(int rowIndex, const QRect &itemRect) const
    {
        const board::Row *row = rowAt(rowIndex);
        if (!row || row->kind != board::Row::Card)
            return QRect();
        const board::Card *card = m_model->card(row->cardId);
        if (!card)
            return QRect();
        return cardShape(*card, row->showStatus, row->stage, m_list->font(), rowWidth(), sessionLive(*card))
            .idCopyRect.translated(itemRect.topLeft());
    }

    // The claim chip at a card row's end (#R9G7), given that row's rect: the ⧉ that names the
    // pane that claimed the card. While that pane is open the chip is a link to it (#YJ4A), so
    // this is its click target, exactly where paint() draws it — empty when the pane has closed
    // (nothing to reveal) and when the row was too narrow to keep the badge at all, the way a
    // dropped badge stops copying its filter term.
    QRect sessionChipRectOf(int rowIndex, const QRect &itemRect) const
    {
        const board::Row *row = rowAt(rowIndex);
        if (!row || row->kind != board::Row::Card)
            return QRect();
        const board::Card *card = m_model->card(row->cardId);
        if (!card || card->session.isEmpty() || !sessionLive(card->session))
            return QRect();
        const CardShape shape = filteredShape(*card, row->showStatus, row->stage, m_list->font(),
                                              rowWidth(), true);
        for (const auto &placed : shape.badges)
            if (placed.first.kind == board::Badge::Session)
                return placed.second.translated(itemRect.topLeft());
        return QRect();
    }

    // The label a click at `at` (viewport coordinates) lands on in the card row at `rowIndex`
    // (#3ZAP): labels are the one badge that copies rather than decorates. A null string when
    // the click is anywhere else in the row.
    QString labelBadgeAt(int rowIndex, const QRect &itemRect, const QPoint &at) const
    {
        const board::Row *row = rowAt(rowIndex);
        if (!row || row->kind != board::Row::Card)
            return QString();
        const board::Card *card = m_model->card(row->cardId);
        if (!card)
            return QString();
        const CardShape shape = filteredShape(*card, row->showStatus, row->stage, m_list->font(),
                                              rowWidth(), sessionLive(*card));
        for (const auto &placed : shape.badges)
            if (placed.first.kind == board::Badge::Label
                && placed.second.translated(itemRect.topLeft()).contains(at))
                return placed.first.text;
        return QString();
    }

    // Nothing is created straight into Done: a card gets there by being closed.
    std::function<bool(const QString &columnId)> adds = [](const QString &) { return true; };

    // (#G2C7) The row as the current filter answers it: the terms marked in the title and,
    // for a card its own text matched, the line that matched under the row. Without a text
    // filter this is the plain shape and costs nothing extra.
    CardShape filteredShape(const board::Card &card, bool showStatus, const QString &stage,
                            const QFont &font, int width, bool live) const
    {
        if (!m_model->textFilterActive())
            return cardShape(card, showStatus, stage, font, width, live);
        const board::Model::FilterMark mark = m_model->filterMark(card.title, card.text);
        return cardShape(card, showStatus, stage, font, width, live, mark.runs, mark.snippet,
                         mark.snippetRuns);
    }

private:
    const board::Row *rowAt(int index) const
    {
        return index >= 0 && index < m_rows->size() ? &m_rows->at(index) : nullptr;
    }

    void paintSection(QPainter *painter, const QStyleOptionViewItem &option, const QRect &rect,
                      const board::Row &row, bool first) const
    {
        const bool canAdd = adds(row.columnId);
        const SectionShape shape = sectionShape(option.font, rect.width(), first, canAdd);
        const QPoint origin = rect.topLeft();
        const bool hover = option.state & QStyle::State_MouseOver;
        // The engraved rule over a section name is the board's hardware, not a border
        // (docs/SWITCHBOARD-AESTHETIC.md 3.1: brass is permitted on 1px rules): unlit brass on the
        // face, lit under the pointer, so exactly one rule in the pane is ever lit. On a theme with
        // `[flags] board_material = false` both tokens are the plain hairline pair.
        if (!first) {
            painter->setPen(QPen(hover ? theme::BoardMetal : theme::BoardMetalDim, 1.0));
            painter->drawLine(rect.left() + kRowPadX, origin.y() + 4,
                              rect.left() + rect.width() - kRowPadX, origin.y() + 4);
        }
        painter->setFont(smaller(option.font, 0.85));
        painter->setPen(hover ? theme::Text : theme::TextMuted);
        painter->drawText(shape.chevronRect.translated(origin), Qt::AlignLeft | Qt::AlignVCenter,
                          row.collapsed ? QStringLiteral("▸") : QStringLiteral("▾"));

        QFont title = smaller(option.font, 0.85);
        title.setWeight(QFont::DemiBold);
        title.setLetterSpacing(QFont::PercentageSpacing, 108);
        painter->setFont(title);
        painter->setPen(hover ? theme::Text : theme::TextMuted);
        const QString text = row.title.toUpper();
        const int titleWidth = QFontMetrics(title).horizontalAdvance(text);
        painter->drawText(shape.titleRect.translated(origin), Qt::AlignLeft | Qt::AlignVCenter, text);

        painter->setFont(monoFont(option.font, 0.8));
        painter->setPen(theme::TextMuted);
        painter->drawText(QRect(shape.titleRect.left() + titleWidth + 8 + origin.x(),
                                shape.titleRect.top() + origin.y(),
                                qMax(0, shape.titleRect.width() - titleWidth - 8),
                                shape.titleRect.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, QString::number(row.count));
        if (canAdd && hover) {
            painter->setFont(smaller(option.font, 0.95));
            painter->setPen(theme::Text);
            painter->drawText(shape.addRect.translated(origin), Qt::AlignCenter,
                              QStringLiteral("+"));
        }
        // Triage (#8YQ9): the board's own `check()`, scoped to this section's cards. It is
        // deterministic and free — not an agent turn — so it is a click on the header rather
        // than a sentence to the page agent, and what it finds is clickable in its turn.
        if (hover) {
            painter->setFont(smaller(option.font, 0.95));
            painter->setPen(theme::TextMuted);
            painter->drawText(shape.triageRect.translated(origin), Qt::AlignCenter,
                              QStringLiteral("\u26A0"));
        }
    }

    // The self-closed fold row (#93WR): the same chevrons and the same muted text as a folded
    // section header, and the selection band of a card row, because unlike a header this row can
    // be stood on.
    void paintFold(QPainter *painter, const QStyleOptionViewItem &option, const QRect &rect,
                   const board::Row &row) const
    {
        const FoldShape shape = foldShape(option.font, rect.width());
        const QPoint origin = rect.topLeft();
        const bool selected = option.state & QStyle::State_Selected;
        const bool hover = option.state & QStyle::State_MouseOver;
        const bool focused = m_list->hasFocus();
        if (selected || hover) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(selected ? alpha(boardHue(), focused ? 34 : 20)
                                       : mix(theme::BoardFace, theme::Text, 0.05));
            painter->drawRoundedRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
        }
        if (selected) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(focused ? boardHue() : theme::BorderStrong);
            painter->drawRoundedRect(QRectF(rect.left() + 1, rect.top() + 3, 2.0,
                                            rect.height() - 6), 1, 1);
        }
        painter->setFont(smaller(option.font, 0.85));
        painter->setPen(selected || hover ? theme::Text : theme::TextMuted);
        painter->drawText(shape.chevronRect.translated(origin), Qt::AlignLeft | Qt::AlignVCenter,
                          row.collapsed ? QStringLiteral("▸") : QStringLiteral("▾"));
        const QFontMetrics metrics(smaller(option.font, 0.85));
        painter->drawText(shape.titleRect.translated(origin), Qt::AlignLeft | Qt::AlignVCenter,
                          metrics.elidedText(row.title, Qt::ElideRight, shape.titleRect.width()));
    }

    // One signal (#AQ6X). It reads as a row of the list — the same selection band as a card — but
    // it carries no flag, no dates and no `#ID`: it is not a card, and the row should not pretend
    // it is one. The claim chip at its end is the card's own chip (board::sessionChip), so who has
    // a signal and who has a card look the same.
    void paintSignal(QPainter *painter, const QStyleOptionViewItem &option, const QRect &rect,
                     const board::Row &row) const
    {
        const board::Signal *signal = m_signals ? m_signals->signalFor(row.signalKey) : nullptr;
        if (!signal)
            return;
        const bool live = sessionLive(signal->session);
        const SignalShape shape = signalShape(*signal, option.font, rect.width(), row.indent,
                                              QDateTime::currentDateTimeUtc(), live);
        const QPoint origin = rect.topLeft();
        const bool selected = option.state & QStyle::State_Selected;
        const bool hover = option.state & QStyle::State_MouseOver;
        const bool focused = m_list->hasFocus();
        if (selected || hover) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(selected ? alpha(boardHue(), focused ? 34 : 20)
                                       : mix(theme::BoardFace, theme::Text, 0.05));
            painter->drawRoundedRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
        }
        if (selected) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(focused ? boardHue() : theme::BorderStrong);
            painter->drawRoundedRect(QRectF(rect.left() + 1, rect.top() + 3, 2.0,
                                            rect.height() - 6), 1, 1);
        }
        const QFont small = smaller(option.font, 0.85);
        const QFontMetrics metrics(small);
        painter->setFont(small);
        // A signal that was fixed and came back is the loudest thing in this block, so its kind
        // wears the amber that means "a human should look"; a dismissed one is muted to the ink of
        // history, because for now it is not work.
        painter->setPen(signal->regressed ? theme::Warning
                        : signal->dismissed() ? theme::TextMuted
                                              : theme::Tool);
        painter->drawText(shape.kindRect.translated(origin), Qt::AlignLeft | Qt::AlignVCenter,
                          board::signalKindWord(signal->kind));
        painter->setFont(monoFont(option.font, 0.85));
        painter->setPen(signal->dismissed() ? theme::TextMuted : theme::Text);
        const QFontMetrics mono(monoFont(option.font, 0.85));
        painter->drawText(shape.keyRect.translated(origin), Qt::AlignLeft | Qt::AlignVCenter,
                          mono.elidedText(signal->key, Qt::ElideMiddle, shape.keyRect.width()));
        if (!shape.tail.isEmpty()) {
            painter->setFont(small);
            painter->setPen(theme::TextMuted);
            painter->drawText(shape.tailRect.translated(origin),
                              Qt::AlignRight | Qt::AlignVCenter, shape.tail);
        }
        if (!shape.chip.isEmpty()) {
            const QRect box = shape.chipRect.translated(origin);
            const auto [ink, edge] = badgeInk(live ? board::Badge::Session
                                                   : board::Badge::SessionClosed);
            painter->setFont(small);
            if (edge.isValid()) {
                painter->setPen(QPen(edge, 1.0));
                painter->setBrush(theme::Surface);
                painter->drawRoundedRect(QRectF(box).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
            }
            painter->setPen(ink);
            painter->drawText(box, Qt::AlignCenter,
                              metrics.elidedText(shape.chip, Qt::ElideRight, box.width() - 6));
        }
    }

    void paintCard(QPainter *painter, const QStyleOptionViewItem &option, const QRect &rect,
                   const board::Row &row) const
    {
        const board::Card *card = m_model->card(row.cardId);
        if (!card)
            return;
        const CardShape shape = cardShape(*card, row.showStatus, row.stage, option.font, rect.width(),
                                          sessionLive(*card));
        const QPoint origin = rect.topLeft();
        const bool selected = option.state & QStyle::State_Selected;
        const bool hover = option.state & QStyle::State_MouseOver;
        const bool focused = m_list->hasFocus();

        // A band, not a box: rows read as a list, and the selection is the one thing with an
        // edge — a 2px bar at the left, so it is visible without a fill loud enough to hurt.
        if (selected || hover) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(selected ? alpha(boardHue(), focused ? 34 : 20)
                                       : mix(theme::BoardFace, theme::Text, 0.05));
            painter->drawRoundedRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
        }
        if (selected) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(focused ? boardHue() : theme::BorderStrong);
            painter->drawRoundedRect(QRectF(rect.left() + 1, rect.top() + 3, 2.0,
                                            rect.height() - 6), 1, 1);
        }

        painter->setFont(option.font);
        // The flag column (#VKFV): an empty ring at 0, a yellow disc at −1 and white, pale green
        // then bright green at +1…+3 — clickable, left to raise and right to lower. A card being
        // planned or discussed right now wears the agent's mark over the flag instead, in the
        // agent's colour: the one place the board shows that #B is working while you read #A.
        const QString working = turnMode ? turnMode(row.cardId) : QString();
        if (working.isEmpty()) {
            drawPriorityFlag(*painter, shape.priorityRect.translated(origin), card->priority);
        } else {
            painter->setPen(theme::Agent);
            painter->drawText(shape.priorityRect.translated(origin), Qt::AlignCenter,
                              QStringLiteral("✦"));
        }

        // The `#ID` column before the title: mono and muted, one fixed column for every row.
        painter->setFont(monoFont(option.font, 0.85));
        painter->setPen(theme::TextMuted);
        painter->drawText(shape.idRect.translated(origin), Qt::AlignLeft | Qt::AlignVCenter,
                          card->reference());
        // …and the ⧉ that copies it (#FT77): muted as the id is, the row's colour under the
        // pointer, so it reads as the button it is.
        painter->setPen(hover ? theme::Text : theme::TextMuted);
        painter->drawText(shape.idCopyRect.translated(origin), Qt::AlignLeft | Qt::AlignVCenter,
                          QStringLiteral("⧉"));

        painter->setFont(option.font);
        painter->setPen(card->closed() ? theme::TextMuted : theme::Text);
        painter->drawText(shape.titleRect.translated(origin), Qt::AlignLeft | Qt::AlignVCenter,
                          shape.title);
        // (#G2C7) The filter's marks: a soft wash behind each run in the title, and the line
        // of the card's text that matched, under the row, washed the same way — the board
        // answers the filter in the same voice the Sessions list does.
        if (!shape.titleRuns.isEmpty() || !shape.snippet.isEmpty()) {
            QColor wash = theme::TextMuted;
            wash.setAlpha(40);
            const QFontMetrics titleMetrics(option.font);
            for (const auto &run : shape.titleRuns) {
                const int start = qBound(0, run.first, shape.title.length());
                const int stop = qBound(start, run.second, shape.title.length());
                if (stop <= start)
                    continue;
                const int advance = titleMetrics.horizontalAdvance(shape.title.left(start));
                const int runWidth = titleMetrics.horizontalAdvance(shape.title.mid(start, stop - start));
                const QRectF bar(shape.titleRect.left() + advance - 1 + origin.x(),
                                 shape.titleRect.top() + 2 + origin.y(), runWidth + 2,
                                 titleMetrics.height() - 4);
                painter->setPen(Qt::NoPen);
                painter->setBrush(wash);
                painter->drawRoundedRect(bar, 3, 3);
                painter->setBrush(Qt::NoBrush);
                painter->setPen(card->closed() ? theme::TextMuted : theme::Text);
            }
            if (!shape.snippet.isEmpty()) {
                const QFont small = smaller(option.font, 0.85);
                const QFontMetrics smallMetrics(small);
                const QRect box = shape.snippetRect.translated(origin);
                for (const auto &run : shape.snippetRuns) {
                    const int start = qBound(0, run.first, shape.snippet.length());
                    const int stop = qBound(start, run.second, shape.snippet.length());
                    if (stop <= start)
                        continue;
                    const QRectF bar(box.left()
                                         + smallMetrics.horizontalAdvance(shape.snippet.left(start)) - 1,
                                     box.top() + 1,
                                     smallMetrics.horizontalAdvance(
                                         shape.snippet.mid(start, stop - start)) + 2,
                                     smallMetrics.height() - 2);
                    painter->setPen(Qt::NoPen);
                    painter->setBrush(wash);
                    painter->drawRoundedRect(bar, 3, 3);
                    painter->setBrush(Qt::NoBrush);
                }
                painter->setFont(small);
                painter->setPen(theme::TextMuted);
                painter->drawText(box, Qt::AlignLeft | Qt::AlignVCenter, shape.snippet);
            }
        }

        // The two date columns, in the same mono as the id: a column of dates reads as a column.
        // A cell the card has nothing to say in stays blank rather than wrong (board::dateCell).
        if (shape.dates) {
            painter->drawText(shape.createdRect.translated(origin),
                              Qt::AlignLeft | Qt::AlignVCenter, shape.created);
            painter->drawText(shape.updatedRect.translated(origin),
                              Qt::AlignLeft | Qt::AlignVCenter, shape.updated);
        }
        if (shape.viewedColumn)
            painter->drawText(shape.viewedRect.translated(origin),
                              Qt::AlignLeft | Qt::AlignVCenter, shape.viewed);
        if (!shape.stage.isEmpty()) {
            // The Stage as a pill (#MXMG): mono small caps like the id and the engraved
            // headers, wearing the stage's ink (stageInk) over an unlit edge. cardShape
            // measured this column with the same font, so the elide agrees with what
            // paints. Upper-cased here, not in the model, the way the headers do it.
            const QFont stageFont = stagePillFont(option.font);
            const QFontMetrics stageMetrics(stageFont);
            const QString text = stageMetrics.elidedText(
                shape.stage.toUpper(), Qt::ElideRight, shape.stageRect.width() - 12);
            const auto [ink, edge] = stageInk(row.stage);
            // In the row's own place, like the date cells: an untranslated rect painted every
            // row's pill over the first row's, so only the top card showed a stage (#YN4D).
            const QRect stageRect = shape.stageRect.translated(origin);
            const qreal pillWidth = qMin<qreal>(stageMetrics.horizontalAdvance(text) + 12,
                                                stageRect.width());
            const QRectF pill(stageRect.left(), stageRect.center().y() - 8.5, pillWidth, 17.0);
            painter->save();
            painter->setFont(stageFont);
            painter->setPen(QPen(edge, 1.0));
            painter->setBrush(theme::Surface);
            painter->drawRoundedRect(pill.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
            painter->setPen(ink);
            painter->drawText(pill, Qt::AlignCenter, text);
            painter->restore();
        }

        const QFont badgeFont = smaller(option.font, 0.85);
        painter->setFont(badgeFont);
        for (const auto &placed : shape.badges) {
            const QRect box = placed.second.translated(origin);
            // A Status badge is the stage riding the badges when the column did not fit
            // (#ESDF): it wears the stage's ink (#MXMG), the same colour its pill would.
            const auto [ink, edge] = placed.first.kind == board::Badge::Status
                ? stageInk(row.stage) : badgeInk(placed.first.kind);
            if (edge.isValid()) {
                painter->setPen(QPen(edge, 1.0));
                painter->setBrush(theme::Surface);
                painter->drawRoundedRect(QRectF(box).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
            }
            painter->setPen(ink);
            painter->drawText(box, Qt::AlignCenter,
                              QFontMetrics(badgeFont).elidedText(placed.first.text, Qt::ElideRight,
                                                                 box.width() - 8));
        }
    }

    const board::Model *m_model;
    const QList<board::Row> *m_rows;
    QListWidget *m_list;
    const board::SignalsState *m_signals = nullptr;   // signals (#AQ6X); null in no case today
};

// The section checkboxes at the top of the list page have to wrap: a pane can be ~350 px wide and
// there are eight or nine of them. Qt ships no flow layout, so this is the minimal one — pack
// left to right, break when the next item would not fit, and report the height that takes.
class FlowLayout final : public QLayout {
public:
    FlowLayout(QWidget *parent, int horizontal, int vertical)
        : QLayout(parent), m_h(horizontal), m_v(vertical) {}
    ~FlowLayout() override
    {
        while (QLayoutItem *item = takeAt(0))
            delete item;
    }

    void addItem(QLayoutItem *item) override { m_items.append(item); }
    int count() const override { return int(m_items.size()); }
    QLayoutItem *itemAt(int index) const override { return m_items.value(index); }
    QLayoutItem *takeAt(int index) override
    {
        return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
    }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return place(QRect(0, 0, width, 0), true); }
    void setGeometry(const QRect &rect) override
    {
        QLayout::setGeometry(rect);
        place(rect, false);
    }
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override
    {
        QSize size;
        for (const QLayoutItem *item : m_items)
            size = size.expandedTo(item->minimumSize());
        const QMargins margins = contentsMargins();
        return size + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    }

private:
    int place(const QRect &rect, bool measureOnly) const
    {
        const QMargins margins = contentsMargins();
        const QRect area = rect.adjusted(margins.left(), margins.top(), -margins.right(),
                                         -margins.bottom());
        int x = area.x(), y = area.y(), lineHeight = 0;
        for (QLayoutItem *item : m_items) {
            const QSize hint = item->sizeHint();
            if (x + hint.width() > area.right() + 1 && lineHeight > 0) {
                x = area.x();
                y += lineHeight + m_v;
                lineHeight = 0;
            }
            if (!measureOnly)
                item->setGeometry(QRect(QPoint(x, y), hint));
            x += hint.width() + m_h;
            lineHeight = qMax(lineHeight, hint.height());
        }
        return y + lineHeight - rect.y() + margins.bottom();
    }

    QList<QLayoutItem *> m_items;
    int m_h, m_v;
};

}  // namespace

// The header row over the list (owner, 2026-09-19: "change switchboard sorting from a sort
// button to adding header columns that you click on", then "add a 'created' and 'updated'
// column", then the flag column #VKFV): four cells — ⚑, Card, Created, Updated — each one a sort
// of the cards *inside* every section, with the arrow on the one that is on. A cell is placed
// with the same measurements the row delegate draws its columns with, so a label sits exactly
// over the cells it names; the date cells and their labels go when the pane is too
// narrow to carry them. The ⚑ is the one cell too narrow for an arrow: the accent colour alone
// says the priority sort is on.
class ColumnHeader final : public QWidget {
public:
    explicit ColumnHeader(QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("boardColumnHeader"));
        m_layout = new QHBoxLayout(this);
        m_layout->setSpacing(kDateGap);
        const QList<board::SortColumn> columns{board::SortColumn::Priority, board::SortColumn::Card,
                                               board::SortColumn::Created,
                                               board::SortColumn::Updated,
                                               board::SortColumn::Viewed};
        for (const board::SortColumn column : columns) {
            auto *cell = new QToolButton(this);
            cell->setObjectName(column == board::SortColumn::Priority
                                    ? QStringLiteral("boardHeaderPriority")
                                : column == board::SortColumn::Card
                                    ? QStringLiteral("boardHeaderCard")
                                : column == board::SortColumn::Created
                                    ? QStringLiteral("boardHeaderCreated")
                                : column == board::SortColumn::Updated
                                    ? QStringLiteral("boardHeaderUpdated")
                                    : QStringLiteral("boardHeaderViewed"));
            cell->setToolButtonStyle(Qt::ToolButtonTextOnly);
            cell->setCursor(Qt::PointingHandCursor);
            cell->setFocusPolicy(Qt::NoFocus);      // the arrows stay with the list
            cell->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
            connect(cell, &QToolButton::clicked, this, [this, column] {
                if (onSort)
                    onSort(column);
            });
            m_cell[int(column)] = cell;
            // The ⚑ is placed by hand over the rows' flag column, left of the layout: a cell in
            // the layout would push the Card label off the titles' left edge, and the layout's
            // own spacing is the date columns', not the flag's.
            if (column == board::SortColumn::Priority)
                continue;
            m_layout->addWidget(cell);
            // The stretch sits between the card's own label and the two date columns, which are
            // the table's right-hand columns. The Stage cell (#ESDF) goes after it, left of the
            // dates, over the rows' Stage column.
            if (column == board::SortColumn::Card) {
                m_layout->addStretch(1);
                m_stage = new QToolButton(this);
                m_stage->setObjectName(QStringLiteral("boardHeaderStage"));
                m_stage->setToolButtonStyle(Qt::ToolButtonTextOnly);
                m_stage->setCursor(Qt::PointingHandCursor);
                m_stage->setFocusPolicy(Qt::NoFocus);
                m_stage->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
                connect(m_stage, &QToolButton::clicked, this, [this] {
                    if (onStage)
                        onStage();
                });
                m_layout->addWidget(m_stage);
            }
        }
        layoutCells();
        updateCells();
    }

    // Which column was clicked: the pane turns it into the sort that is on.
    std::function<void(board::SortColumn)> onSort;
    // The Stage cell was clicked (#ESDF): the pane toggles sections and the flat list.
    std::function<void()> onStage;

    // The list the cells line up with: its width decides where they sit and whether the two date
    // columns fit at all (the rows ask the same question of the same width, so a header whose
    // labels had gone while the cells were still drawn cannot happen).
    void setList(QListWidget *list)
    {
        m_list = list;
        layoutCells();
    }

    // The sort that is on, as the list's own (board::Sort): the arrow follows it.
    void setSort(board::Sort sort)
    {
        if (m_ready && sort == m_sort)
            return;
        m_sort = sort;
        m_ready = true;
        updateCells();
    }

    // Sections or flat (#ESDF): STAGE wears the accent while the list is grouped by stage, and
    // the sort cells' tips say whether they order each section or the whole list.
    void setGrouping(board::Grouping grouping)
    {
        if (grouping == m_grouping)
            return;
        m_grouping = grouping;
        updateCells();
    }

protected:
    // The cells' places depend on the width and on the font, so both are re-read when either
    // changes — a theme switch can change the font under a pane that is not resized.
    void resizeEvent(QResizeEvent *) override { layoutCells(); }
    void changeEvent(QEvent *event) override
    {
        QWidget::changeEvent(event);
        if (event->type() == QEvent::FontChange)
            layoutCells();
    }

private:
    // Where the cells sit and which of them are on the page: measured from the list, whose width
    // this header shares, keeping the same reserve at the right that the rows keep for the
    // scrollbar and the same left margin the rows' titles start at.
    void layoutCells()
    {
        const int frame = m_list ? m_list->frameWidth() : 0;
        const int reserve = m_list ? rowReserve(m_list) : 0;
        const int rowWidth = m_list ? rowContentWidth(m_list) : width();
        const QFont base = font();
        m_layout->setContentsMargins(
            frame + kRowPadX + kGlyphWidth + 4 + idColumnWidth(base) + idCopyWidth(base), 3,
                                     qMax(0, reserve - frame) + kRowPadX, 3);
        const bool dates = dateColumnsFit(base, rowWidth);
        const bool viewed = dates && viewedColumnFits(base, rowWidth);
        const int column = dateColumnWidth(base);
        for (int i = 0; i < kColumns; ++i) {
            QToolButton *cell = m_cell[i];
            if (cell == nullptr)
                continue;
            // The font is set here and never in the stylesheet, so sizeHint() measures what paints.
            QFont cellFont = monoFont(base, 0.85);
            cellFont.setLetterSpacing(QFont::PercentageSpacing, 106);
            cell->setFont(cellFont);
            const int cellHeight = QFontMetrics(cellFont).height() + 6;
            cell->setMinimumHeight(cellHeight);
            if (i == int(board::SortColumn::Priority)) {
                // Over the rows' flag column (#VKFV): the box a row's ring sits in, at the rows'
                // own left edge — placed by hand, vertically centred, because no layout holds it.
                cell->setGeometry(QRect(frame + kRowPadX, qMax(3, (height() - cellHeight) / 2),
                                        kGlyphWidth + 4, cellHeight));
                continue;
            }
            const bool date = i >= int(board::SortColumn::Created);
            if (date)
                cell->setFixedWidth(column);
            cell->setVisible(!date || (i == int(board::SortColumn::Viewed) ? viewed : dates));
        }
        if (m_stage) {
            QFont cellFont = monoFont(base, 0.85);
            cellFont.setLetterSpacing(QFont::PercentageSpacing, 106);
            m_stage->setFont(cellFont);
            m_stage->setMinimumHeight(QFontMetrics(cellFont).height() + 6);
            m_stage->setFixedWidth(stageColumnWidth(base));
        }
    }

    // What each cell says: its name, and its arrow when it is the sort that is on.
    void updateCells()
    {
        const int active = board::sortColumnIndex(m_sort);
        for (int i = 0; i < kColumns; ++i) {
            QToolButton *cell = m_cell[i];
            if (cell == nullptr)
                continue;
            const board::SortColumn column = board::SortColumn(i);
            QString text = board::columnTitle(column).toUpper();
            // The ⚑ cell is one glyph wide: no arrow fits beside it, and the accent colour the
            // active property turns on says the same thing.
            if (active == i && column != board::SortColumn::Priority)
                text += board::sortAscending(m_sort) ? QStringLiteral(" ▲") : QStringLiteral(" ▼");
            cell->setText(text);
            cell->setToolTip(cellTooltip(column, m_sort, m_grouping));
            cell->setProperty("active", active == i);
            cell->style()->unpolish(cell);
            cell->style()->polish(cell);
        }
        if (m_stage) {
            const bool sections = m_grouping == board::Grouping::Sections;
            m_stage->setText(QStringLiteral("STAGE"));
            m_stage->setToolTip(sections
                                    ? QStringLiteral("Grouped by stage: a section per status. "
                                                     "Click for one list of every card, each "
                                                     "row naming its stage.")
                                    : QStringLiteral("One list of every card, each row naming its "
                                                     "stage. Click to group the cards into a "
                                                     "section per stage."));
            m_stage->setProperty("active", sections);
            m_stage->style()->unpolish(m_stage);
            m_stage->style()->polish(m_stage);
        }
    }

    // The tip: what the column is and the orders a click walks through, in the order it walks
    // through them — so the cycle back to the board's own drag order is written where it happens.
    static QString cellTooltip(board::SortColumn column, board::Sort current,
                               board::Grouping grouping)
    {
        const board::Sort first = board::nextColumnSort(column, board::Sort::Manual);
        const board::Sort second = board::nextColumnSort(column, first);
        const QString what = column == board::SortColumn::Priority
                                 ? QStringLiteral("the cards' priority flag")
                             : column == board::SortColumn::Card
                                 ? QStringLiteral("the card's title")
                             : column == board::SortColumn::Created
                                 ? QStringLiteral("when the card was created")
                             : column == board::SortColumn::Updated
                                 ? QStringLiteral("when the card last changed")
                                 // Local on purpose (#FKSN): "what was I just reading" is this
                                 // machine's question, so another machine keeps its own.
                                 : QStringLiteral("when you last opened the card on this "
                                                  "machine");
        if (board::sortColumnIndex(current) == int(column))
            return QStringLiteral("Sorted by %1 — %2. Click again for %3, or once more for the "
                                  "board's own order.")
                .arg(what, board::sortTitle(current).toLower(),
                     board::sortTitle(board::nextColumnSort(column, current)).toLower());
        return QStringLiteral("Order the cards %1 by %2: %3, then %4, then the board's own order "
                              "— the one drag and drop writes.")
            .arg(grouping == board::Grouping::Flat ? QStringLiteral("of the whole list")
                                                   : QStringLiteral("inside each section"),
                 what, board::sortTitle(first).toLower(), board::sortTitle(second).toLower());
    }

    QHBoxLayout *m_layout = nullptr;
    static constexpr int kColumns = int(board::SortColumn::Viewed) + 1;
    QToolButton *m_cell[kColumns] = {};
    QToolButton *m_stage = nullptr;
    board::Grouping m_grouping = board::Grouping::Sections;
    QListWidget *m_list = nullptr;
    board::Sort m_sort = board::Sort::Manual;
    bool m_ready = false;
};

// The one list: section headers and cards in a single vertical scroll. It reports a drop instead
// of moving the row itself — the card only moves once the worker has written the file and sent
// board_changed back — and draws the line where the card would land.
class RowList final : public QListWidget {
public:
    explicit RowList(const QList<board::Row> *rows, QWidget *parent = nullptr)
        : QListWidget(parent), m_rows(rows)
    {
        setDragDropMode(QAbstractItemView::DragDrop);
        setDefaultDropAction(Qt::MoveAction);
        setDropIndicatorShown(false);    // drawn by paintEvent below, between the rows
        setSelectionMode(QAbstractItemView::SingleSelection);
        setUniformItemSizes(false);
        setResizeMode(QListView::Adjust);           // rows re-elide when the pane resizes
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setFrameShape(QFrame::NoFrame);
        setMouseTracking(true);
        viewport()->setAttribute(Qt::WA_Hover);
        setObjectName(QStringLiteral("boardList"));
    }

    QString placeholder;             // drawn when the list is empty
    // (card id, the section it lands in, the id it goes before, the id it goes after)
    std::function<void(const QString &, const QString &, const QString &, const QString &)> onDropped;
    std::function<void(bool)> onDragging;   // a drag from this list started (true) or ended
    std::function<void(const QString &columnId)> onToggleSection, onAddInSection;
    // The self-closed fold row (#93WR): a click shows its cards or puts them away. The row is not
    // a card — it never starts a drag and nothing can be dropped on it.
    std::function<void(const QString &columnId)> onToggleFold;
    // The two signal toggles and one signal row (#AQ6X): "signals" or "dismissed" for the first,
    // the signal's key for the second. A signal row opens its page on a click, as Enter does —
    // it is not a card, so there is nothing for a click to select and then wait on.
    std::function<void(const QString &which)> onToggleSignalFold;
    std::function<void(const QString &key)> onOpenSignal;
    // The ⚠ on a section header (#8YQ9): check that section's cards and show what it found.
    std::function<void(const QString &columnId)> onTriageSection;
    // A click on a row's flag: the card and the step, +1 for a left click and −1 for a right
    // one (#VKFV). The pane clamps at −1…+3 and writes it through `board_priority`.
    std::function<void(const QString &cardId, int step)> onPriority;
    // A click on a row's label badge copies its label: filter term (#S53Z) instead of selecting the row.
    std::function<void(const QString &label)> onCopyLabel;
    // A click on the ⧉ beside a row's `#ID` copies the reference (#FT77), under the badge's
    // one-gesture rule: a copy, never a selection.
    std::function<void(const QString &cardId)> onCopyId;
    std::function<QRect(int rowIndex, const QRect &itemRect)> addRectOf, priorityRectOf,
        triageRectOf, idCopyRectOf;
    // A click on the claim chip at a card row's end (#YJ4A): while the pane that claimed the
    // card (#R9G7) is open, its ⧉ is a link that reveals that pane — the card page's chip link,
    // on the row.
    std::function<void(const QString &cardId)> onRevealPane;
    std::function<QRect(int rowIndex, const QRect &itemRect)> sessionChipRectOf;
    // The label badge a point lands on, as text, or a null string (#3ZAP).
    std::function<QString(int rowIndex, const QRect &itemRect, const QPoint &at)> labelBadgeAt;
    static QString dragging;

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QListWidget::resizeEvent(event);
        if (event->size().width() != event->oldSize().width())
            scheduleDelayedItemsLayout();   // the rows re-elide to the new width
    }

    void showEvent(QShowEvent *event) override
    {
        QListWidget::showEvent(event);
        scheduleDelayedItemsLayout();       // measured while hidden, at whatever width it had
    }

    // The label badge under a left-button event, or a null string (#3ZAP): shared by the
    // press, release and double-click guards, so a badge click is one gesture that copies and
    // never becomes a selection or an activation.
    QString labelBadgeUnder(const QMouseEvent *event) const
    {
        if (!labelBadgeAt || event->button() != Qt::LeftButton)
            return QString();
#if QT_VERSION_MAJOR >= 6
        const QPoint at = event->position().toPoint();
#else
        const QPoint at = event->pos();
#endif
        const QModelIndex index = indexAt(at);
        const board::Row *row = rowAt(index.row());
        if (!row || row->kind != board::Row::Card)
            return QString();
        return labelBadgeAt(index.row(), visualRect(index), at);
    }

    // The card id whose ⧉ a point lands on, or a null string (#FT77): the press, release and
    // double-click guards share it, the same one-gesture rule the label badges keep (#3ZAP).
    QString idCopyUnder(const QMouseEvent *event) const
    {
        if (!idCopyRectOf || event->button() != Qt::LeftButton)
            return QString();
#if QT_VERSION_MAJOR >= 6
        const QPoint at = event->position().toPoint();
#else
        const QPoint at = event->pos();
#endif
        const QModelIndex index = indexAt(at);
        const board::Row *row = rowAt(index.row());
        if (!row || row->kind != board::Row::Card)
            return QString();
        const QRect rect = idCopyRectOf(index.row(), visualRect(index));
        return rect.isValid() && rect.adjusted(-2, -2, 2, 2).contains(at) ? row->cardId
                                                                         : QString();
    }

    // The card whose claim chip a point lands on, or a null string (#YJ4A): press, release,
    // double-click and the hover cursor share it, the one-gesture rule the ⧉ id-copy keeps. The
    // rect is empty while the claiming pane is closed, so a dead chip is just paint.
    QString sessionChipAt(const QPoint &at) const
    {
        if (!sessionChipRectOf)
            return QString();
        const QModelIndex index = indexAt(at);
        const board::Row *row = rowAt(index.row());
        if (!row || row->kind != board::Row::Card)
            return QString();
        const QRect rect = sessionChipRectOf(index.row(), visualRect(index));
        return rect.isValid() && rect.adjusted(-2, -2, 2, 2).contains(at) ? row->cardId
                                                                         : QString();
    }

    QString sessionChipUnder(const QMouseEvent *event) const
    {
        if (event->button() != Qt::LeftButton)
            return QString();
#if QT_VERSION_MAJOR >= 6
        return sessionChipAt(event->position().toPoint());
#else
        return sessionChipAt(event->pos());
#endif
    }

    // A click on a section header toggles it, or adds into it; it never becomes a selection.
    void mousePressEvent(QMouseEvent *event) override
    {
#if QT_VERSION_MAJOR >= 6
        const QPoint at = event->position().toPoint();
#else
        const QPoint at = event->pos();
#endif
        const QModelIndex index = indexAt(at);
        const board::Row *row = rowAt(index.row());
        // The flag at a card row's left end is a control (#VKFV), not a selection: a left click
        // raises that card's priority and a right click lowers it. Neither ever moves the
        // selection, so a burst of clicks walks one card's flag without losing the row the
        // keyboard is on.
        if (row && row->kind == board::Row::Card && priorityRectOf
            && (event->button() == Qt::LeftButton || event->button() == Qt::RightButton)) {
            const QRect flag = priorityRectOf(index.row(), visualRect(index));
            if (flag.isValid() && flag.adjusted(-2, -2, 2, 2).contains(at)) {
                if (onPriority)
                    onPriority(row->cardId, event->button() == Qt::LeftButton ? 1 : -1);
                event->accept();
                return;
            }
        }
        // The ⧉ beside the `#ID` copies the reference (#FT77), under the same rule the label
        // badges keep: a copy, never a selection.
        if (const QString id = idCopyUnder(event); !id.isEmpty()) {
            if (onCopyId)
                onCopyId(id);
            event->accept();
            return;
        }
        // A label badge copies its label: filter term (#S53Z) rather than selecting the row; the row's
        // other badges still decorate, and a click between them selects as before.
        if (const QString label = labelBadgeUnder(event); !label.isEmpty()) {
            if (onCopyLabel)
                onCopyLabel(label);
            event->accept();
            return;
        }
        // The claim chip at the row's end is a link to the pane that claimed the card (#YJ4A):
        // a reveal, never a selection — the card page's chip, on the row.
        if (const QString id = sessionChipUnder(event); !id.isEmpty()) {
            if (onRevealPane)
                onRevealPane(id);
            event->accept();
            return;
        }
        // The fold row (#93WR) toggles on a click, like a section header, and the pane stands the
        // selection on it afterwards so Enter and ←/→ carry on from there.
        if (row && row->kind == board::Row::Fold && event->button() == Qt::LeftButton) {
            if (onToggleFold)
                onToggleFold(row->columnId);
            event->accept();
            return;
        }
        // The signal rows (#AQ6X): the two toggles behave like the fold row above, and a signal
        // itself opens its page.
        if (row && event->button() == Qt::LeftButton
            && (row->kind == board::Row::SignalFold || row->kind == board::Row::DismissedFold)) {
            if (onToggleSignalFold)
                onToggleSignalFold(row->kind == board::Row::SignalFold
                                           ? QStringLiteral("signals")
                                           : QStringLiteral("dismissed"));
            event->accept();
            return;
        }
        if (row && row->kind == board::Row::Signal && event->button() == Qt::LeftButton) {
            if (onOpenSignal)
                onOpenSignal(row->signalKey);
            event->accept();
            return;
        }
        if (row && row->kind == board::Row::Section && event->button() == Qt::LeftButton) {
            const QRect add = addRectOf ? addRectOf(index.row(), visualRect(index)) : QRect();
            const QRect triage = triageRectOf ? triageRectOf(index.row(), visualRect(index))
                                              : QRect();
            if (add.isValid() && add.adjusted(-5, -5, 5, 5).contains(at)) {
                if (onAddInSection)
                    onAddInSection(row->columnId);
            } else if (triage.isValid() && triage.adjusted(-5, -5, 5, 5).contains(at)) {
                if (onTriageSection)
                    onTriageSection(row->columnId);
            } else if (onToggleSection) {
                onToggleSection(row->columnId);
            }
            event->accept();
            return;
        }
        QListWidget::mousePressEvent(event);
    }

    // The release of a badge click must not reach the list either (#3ZAP): the press state Qt
    // keeps is stale from an earlier real press on that row, and it would hand the badge's
    // release to the row as a click — a badge is a control, not a selection.
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (!idCopyUnder(event).isEmpty()) {
            event->accept();
            return;
        }
        if (!labelBadgeUnder(event).isEmpty()) {
            event->accept();
            return;
        }
        if (!sessionChipUnder(event).isEmpty()) {
            event->accept();
            return;
        }
        QListWidget::mouseReleaseEvent(event);
    }

    // A second click on a badge is another copy, not an activation of the row (#3ZAP).
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        // The ⧉ again: a second click is another copy, not an activation of the row (#FT77).
        if (const QString id = idCopyUnder(event); !id.isEmpty()) {
            if (onCopyId)
                onCopyId(id);
            event->accept();
            return;
        }
        const QString label = labelBadgeUnder(event);
        if (!label.isEmpty()) {
            if (onCopyLabel)
                onCopyLabel(label);
            event->accept();
            return;
        }
        // The claim chip again (#YJ4A): a second click is another reveal.
        if (const QString id = sessionChipUnder(event); !id.isEmpty()) {
            if (onRevealPane)
                onRevealPane(id);
            event->accept();
            return;
        }
        QListWidget::mouseDoubleClickEvent(event);
    }

    // The claim chip is the row's one link (#YJ4A), so it says so: the pointing hand over it
    // while the pane it names is open, the arrow anywhere else on the list.
    void mouseMoveEvent(QMouseEvent *event) override
    {
#if QT_VERSION_MAJOR >= 6
        const bool overChip = !sessionChipAt(event->position().toPoint()).isEmpty();
#else
        const bool overChip = !sessionChipAt(event->pos()).isEmpty();
#endif
        viewport()->setCursor(overChip ? Qt::PointingHandCursor : Qt::ArrowCursor);
        QListWidget::mouseMoveEvent(event);
    }

    // Our own drag, not QListWidget's: the default image is the row painted on a transparent
    // pixmap, which without a compositor is a black box. This one is the row on the board's
    // background. The payload is the text `#ID`, so dropping a card on the prompt box types it.
    void startDrag(Qt::DropActions) override
    {
        QListWidgetItem *item = currentItem();
        if (!item || item->data(kCardRole).toString().isEmpty())
            return;
        dragging = item->data(kCardRole).toString();
        QRect rect = visualItemRect(item);
        rect.setWidth(qMin(rect.width(), qMax(260, width() / 2)));
        const qreal ratio = devicePixelRatioF();
        QPixmap pixmap(rect.size() * ratio);
        pixmap.setDevicePixelRatio(ratio);
        pixmap.fill(theme::BoardFace);
        {
            QPainter painter(&pixmap);
            QStyleOptionViewItem option;
            option.initFrom(this);
            option.rect = QRect(QPoint(0, 0), rect.size());
            option.state |= QStyle::State_Selected;
            itemDelegate()->paint(&painter, option, indexFromItem(item));
        }
        auto *drag = new QDrag(this);
        auto *mime = new QMimeData;
        mime->setText(QStringLiteral("#") + dragging + QLatin1Char(' '));
        drag->setMimeData(mime);
        drag->setPixmap(pixmap);
        drag->setHotSpot(QPoint(qMin(40, rect.width() / 2), rect.height() / 2));
        if (onDragging)
            onDragging(true);
        drag->exec(Qt::MoveAction | Qt::CopyAction, Qt::MoveAction);   // until dropped or cancelled
        dragging.clear();
        m_dropActive = false;
        m_dropRow = m_dropHeader = -1;
        viewport()->update();
        if (onDragging)
            onDragging(false);
    }

    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (dragging.isEmpty()) {   // only cards: a file or text dragged in is not a card
            event->ignore();
            return;
        }
        event->acceptProposedAction();
        m_dropActive = true;
        viewport()->update();
    }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (dragging.isEmpty()) {
            event->ignore();
            return;
        }
        event->acceptProposedAction();
#if QT_VERSION_MAJOR >= 6
        aimAt(event->position().toPoint());
#else
        aimAt(event->pos());
#endif
    }

    void dragLeaveEvent(QDragLeaveEvent *event) override
    {
        QListWidget::dragLeaveEvent(event);
        m_dropActive = false;
        m_dropRow = m_dropHeader = -1;
        viewport()->update();
    }

    void dropEvent(QDropEvent *event) override
    {
        const QString card = dragging;
        dragging.clear();
        m_dropActive = false;
        const int header = m_dropHeader, before = m_dropRow;
        m_dropHeader = m_dropRow = -1;
        viewport()->update();
        if (card.isEmpty()) {
            event->ignore();
            return;
        }
        // The model is the file on disk; nothing moves in the view until the worker says so.
        event->setDropAction(Qt::IgnoreAction);
        event->accept();
        drop(card, header, before);
    }

    void paintEvent(QPaintEvent *event) override
    {
        QListWidget::paintEvent(event);
        QPainter painter(viewport());
        painter.setRenderHint(QPainter::Antialiasing);
        if (count() == 0 && !placeholder.isEmpty()) {
            painter.setPen(theme::TextMuted);
            painter.setFont(smaller(font(), 0.95));
            painter.drawText(viewport()->rect().adjusted(16, 24, -16, 0),
                             Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, placeholder);
        }
        if (!m_dropActive)
            return;
        if (m_dropHeader >= 0 && m_dropHeader < count()) {
            const QRect rect = visualItemRect(item(m_dropHeader));
            painter.setPen(QPen(alpha(boardHue(), 130), 1.0));
            painter.setBrush(alpha(boardHue(), 26));
            painter.drawRoundedRect(QRectF(rect).adjusted(2.5, 2.5, -2.5, -1.5), 5, 5);
            return;
        }
        int y = 1;
        if (count() > 0) {
            const int row = qBound(0, m_dropRow, count());
            y = row < count() ? visualItemRect(item(row)).top()
                              : visualItemRect(item(count() - 1)).bottom() + 1;
        }
        painter.setPen(QPen(boardHue(), 2.0, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(kRowPadX, y), QPointF(viewport()->width() - kRowPadX, y));
    }

public:
    // Where a card dropped with `header` aimed at (or, at -1, between rows at `before`) goes.
    // Public so the pane's keyboard move and a test can take the same path as the mouse.
    void drop(const QString &card, int header, int before)
    {
        auto [columnId, slot] = board::dropTarget(*m_rows, header >= 0 ? header + 1 : before);
        QStringList order;
        if (columnId.isEmpty()) {
            // A flat list (#ESDF): no header owns the point, so the drop reorders the card among
            // every row and it stays in its own section — a stage changes by Alt+Shift+←/→ or
            // the card's menu there, never by where it was let go.
            const int own = board::rowOfCard(*m_rows, card);
            if (own < 0 || board::rowOfSection(*m_rows, m_rows->at(own).columnId) >= 0)
                return;
            columnId = m_rows->at(own).columnId;
            order = board::cardsInList(*m_rows);
            slot = 0;
            for (int i = 0; i < qBound(0, before, int(m_rows->size())); ++i)
                if (m_rows->at(i).kind == board::Row::Card)
                    ++slot;
        } else {
            order = board::cardsInSection(*m_rows, columnId);
        }
        const int from = order.indexOf(card);
        const int target = from >= 0 && from < slot ? slot - 1 : slot;
        if (from >= 0 && target == from)
            return;   // dropped where it already was: no write, no thread entry
        const auto [beforeId, afterId] = board::placement(order, card, target);
        if (onDropped)
            onDropped(card, columnId, beforeId, afterId);
    }

private:
    const board::Row *rowAt(int index) const
    {
        return index >= 0 && index < m_rows->size() ? &m_rows->at(index) : nullptr;
    }

    // A pointer inside a section header aims at that section; anywhere else it aims between two
    // rows, and board::dropTarget decides which section that point belongs to.
    void aimAt(const QPoint &pos)
    {
        int header = -1, before = count();
        const QModelIndex index = indexAt(pos);
        const board::Row *row = rowAt(index.row());
        if (row && row->kind == board::Row::Section) {
            header = index.row();
        } else {
            for (int i = 0; i < count(); ++i) {
                if (pos.y() < visualItemRect(item(i)).center().y()) {
                    before = i;
                    break;
                }
            }
        }
        if (header == m_dropHeader && before == m_dropRow)
            return;
        m_dropHeader = header;
        m_dropRow = before;
        viewport()->update();
    }

    const QList<board::Row> *m_rows;
    bool m_dropActive = false;
    int m_dropRow = -1, m_dropHeader = -1;
};

QString RowList::dragging;


// The empty Switchboard: a board with nothing patched through (docs/SWITCHBOARD-AESTHETIC.md
// intervention 5, "DO"). One unlit jack per section over its engraved name, then the words the label
// was given. These rings are the only brass circles in Relay, and what lets them past the taste
// guard in §6 ("at most two brass circles and one cord in a default window") is that the widget
// carrying them disappears the moment there is a card.
//
// It is a QLabel so that setText(), setVisible() and hide() keep working on it unchanged; only the
// painting is its own.
class EmptyBoard final : public QLabel {
public:
    explicit EmptyBoard(QWidget *parent = nullptr) : QLabel(parent) {}

    // The section names, in board order. Empty until the first `board` event arrives, which is what
    // keeps the rings off "Loading the Switchboard…": an unpatched board is a fact, not a guess.
    void setSections(const QStringList &names)
    {
        if (names == m_names)
            return;
        m_names = names;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QStringList lines = text().split(QLatin1Char('\n'));
        const QFont body = theme::legible(font(), theme::SecondaryPt);
        const QFontMetrics metrics(body);
        QFont enamel = monoFont(font(), 0.8);        // uppercase, letter-spaced: an enamel label
        enamel.setLetterSpacing(QFont::AbsoluteSpacing, 1);
        const QFontMetrics enamelMetrics(enamel);

        constexpr int kRing = 14, kRingGap = 12, kNameGap = 7, kBandGap = 26;
        // The names go under the rings when they fit; a pane 300px wide with ten sections gets the
        // rings alone rather than a row of stubs. Whichever it is, nothing wraps and nothing is
        // half drawn: the columns that do not fit are simply not there.
        int column = 0;
        for (const QString &name : m_names)
            column = qMax(column, enamelMetrics.horizontalAdvance(name.toUpper()) + 10);
        column = qBound(kRing + kRingGap, column, 110);
        const int room = qMax(0, width() - 24);
        int count = m_names.size();
        bool named = column > 0 && room / qMax(1, column) >= 3;
        if (named)
            count = qMin(count, room / column);
        const int pitch = named ? column : kRing + kRingGap;
        const int bandHeight = m_names.isEmpty() ? 0 : kRing + (named ? kNameGap + enamelMetrics.height() : 0);
        int textHeight = 0;
        for (const QString &line : lines)
            textHeight += line.isEmpty() ? metrics.height() / 2 : metrics.lineSpacing();

        int y = qMax(12, (height() - (bandHeight + (bandHeight ? kBandGap : 0) + textHeight)) / 2);
        if (bandHeight) {
            int x = (width() - pitch * count) / 2;
            for (int i = 0; i < count; ++i) {
                // A jack: an unlit brass ring, the collar's shade inside it, and the hole itself
                // as a small dark disc — small, or on a light theme the hole swallows the ring and
                // the jack reads as a bullet. The hole is mixed out of whichever of the face and
                // the metal is already the darker, so it is darker than both in every theme.
                // Nothing here is ever lit: a board with no cards has no line patched through it.
                const QRectF ring(x + (pitch - kRing) / 2.0, y, kRing, kRing);
                const QColor deeper = qGray(theme::BoardFace.rgb()) <= qGray(theme::BoardMetalDim.rgb())
                                          ? theme::BoardFace : theme::BoardMetalDim;
                painter.setPen(QPen(theme::BoardMetalDim, 1.4));
                painter.setBrush(mix(theme::BoardFace, theme::BoardMetalDim, 0.15));
                painter.drawEllipse(ring);
                painter.setPen(Qt::NoPen);
                painter.setBrush(mix(deeper, QColor(Qt::black), 0.45));
                painter.drawEllipse(ring.center(), 2.6, 2.6);
                if (named) {
                    painter.setFont(enamel);
                    painter.setPen(theme::TextMuted);
                    const QString name = m_names.at(i).toUpper();
                    painter.drawText(QRect(x, y + kRing + kNameGap, pitch, enamelMetrics.height()),
                                     Qt::AlignCenter,
                                     enamelMetrics.elidedText(name, Qt::ElideRight, pitch - 4));
                }
                x += pitch;
            }
            y += bandHeight + kBandGap;
        }
        painter.setFont(body);
        painter.setPen(theme::TextMuted);
        for (const QString &line : lines) {
            if (line.isEmpty()) {
                y += metrics.height() / 2;
                continue;
            }
            painter.drawText(QRect(12, y, qMax(0, width() - 24), metrics.lineSpacing()), Qt::AlignCenter,
                             metrics.elidedText(line, Qt::ElideRight, qMax(0, width() - 24)));
            y += metrics.lineSpacing();
        }
    }

private:
    QStringList m_names;
};

// --------------------------------------------------------------------- card detail

// The one question every delete path asks (card #CYM9): name the card, say exactly what goes,
// and say where recovery stands — Undo for 30 s, git after that only if it ever saw the card.
// Cancel is the default: a delete is never something the keyboard slipped into.
static bool confirmDeleteCard(const QString &id, const QString &title, QWidget *parent)
{
    QMessageBox confirm(QMessageBox::Warning, QStringLiteral("Delete card"),
                        QStringLiteral("Delete #%1 “%2”?").arg(id, title),
                        QMessageBox::Cancel, parent);
    confirm.setInformativeText(QStringLiteral(
        "The card file and its thread are removed from disk. Undo works for 30 seconds; "
        "git still has it if it was committed."));
    auto *remove = confirm.addButton(QStringLiteral("Delete"), QMessageBox::DestructiveRole);
    confirm.exec();
    return confirm.clickedButton() == remove;
}

// The right half of the Switchboard: the card as one document (body, then its thread, the way an
// issue page reads), a reply box under it, and the pickers and links in a header above it.

namespace board {

// ---------------------------------------------------------------------------------------------
// The two contexts (card #AGNT step 6).
//
// The owner's sentence the card is built on: "an agent interface is the prompt box. it has a set
// of options and tools that vary according to the setting/task, but in general they are shared
// systems." So the Switchboard's conversation and a card's are not two chat widgets any more —
// they are one surface, a no-shell `Pane` the window makes, and two `relay::agent::Context`s that
// say what the agent on each page is *about*: the brief, the defaults, the row of things that
// need no typing, how a link resolves, where the conversation is kept.
//
// Neither holds a widget and neither holds a worker. They read the view they belong to, and the
// view tells them when something they would answer differently has moved (`changed()`).

// The list page: the agent about the whole board.
class BoardContext final : public relay::agent::Context {
  public:
    explicit BoardContext(BoardView *view) : m_view(view) {}

    relay::agent::ContextSpec spec() const override
    {
        relay::agent::ContextSpec spec;
        spec.name = QStringLiteral("switchboard");
        spec.surface = QStringLiteral("switchboard");
        // "Helper agent" in the UI; `switchboard` on the wire, where it picks a provider and
        // nothing else (protocol 13.1). It does not change the tool set or the prompt.
        spec.agentRole = QStringLiteral("switchboard");
        spec.workspace = m_view->m_workspace;
        // Named, never inferred: `Agent.tools()` used to branch on whether the board had a card
        // scope, so a board-less helper silently took the pane branch and got the full executor.
        spec.scope = QStringLiteral("console");
        // Which store, not which path (§30.7's closed set: "", "pane", "helper"), keyed by the
        // tab. A board with no tab id sends no `persist` block at all, which is how a client says
        // "no store": the conversation lives as long as the worker and nothing is written down.
        spec.persistScope = m_view->m_tabId.isEmpty() ? QString() : QStringLiteral("helper");
        spec.persistKey = m_view->m_tabId;
        spec.briefKey = QStringLiteral("switchboard");
        spec.briefTitle = QStringLiteral("Board agent");
        spec.screen = m_view->screenHint();
        spec.shell = false;
        spec.routing = QStringLiteral("agent");
        return spec;
    }

    // Hygiene runs the deterministic check before offering the agent cleanup stage.
    QList<relay::agent::Action> actions() const override
    {
        QList<relay::agent::Action> actions;
        BoardView *view = m_view;

        relay::agent::Action check;
        check.key = QStringLiteral("boardChatCheck");
        check.letter = QStringLiteral("k");
        check.label = QStringLiteral("Hygiene");
        check.tooltip = QStringLiteral("Re-run the board's format check over every card — ids, "
                                       "front matter, threads — and list what is wrong. Click a "
                                       "finding to draft a fix for the agent. Nothing is written "
                                       "and no model is called.");
        check.run = [view] {
            const ActionGuard guard;
            view->requestCheck();
            if (view->onStatus)
                view->onStatus(QStringLiteral("Hygiene: checking every card…"));
        };
        actions << check;

        // Tests (#7BM4, design 4.13: board-wide buttons live in this row) opens the Test suites
        // pane; Profile (#7BM4 phase 5) asks the window which of four things to profile. Both
        // panes are the window's — a splitter pane beside this one, on the same tab's worker — so
        // the action only says that it was pressed.
        relay::agent::Action tests;
        tests.key = QStringLiteral("boardTests");
        tests.label = QStringLiteral("Tests");
        tests.tooltip = QStringLiteral("Test suites: this project's tests, their history and "
                                       "their runs, in a pane beside the board");
        tests.run = [view] {
            const ActionGuard guard;
            if (view->onOpenTestSuites)
                view->onOpenTestSuites();
        };
        actions << tests;

        relay::agent::Action review;
        review.key = QStringLiteral("boardReview");
        review.label = QStringLiteral("Review");
        review.tooltip = QStringLiteral("The cards that need your judgement, in a pane beside the Board");
        review.run = [view] {
            const ActionGuard guard;
            if (view->onOpenReview) view->onOpenReview();
        };
        actions << review;

        relay::agent::Action profile;
        profile.key = QStringLiteral("boardProfile");
        profile.label = QStringLiteral("Performance");
        profile.tooltip = QStringLiteral("Performance of the project: the build, the Python tests or "
                                         "the app — a table of where the time goes, in a pane "
                                         "beside the board");
        // The menu has to be anchored under the button, and an `Action` is a `std::function<void()>`
        // with no widget in it — so the button is found by the one name that is guaranteed to be
        // on it: `Action::key` becomes the button's `objectName` (src/Pane.h, rebuildActionRow).
        // With no console yet, the chat area itself is a good enough anchor.
        profile.run = [view] {
            const ActionGuard guard;
            if (!view->onProfile)
                return;
            QWidget *anchor = view->m_console
                ? view->m_console->findChild<QWidget *>(QStringLiteral("boardProfile"))
                : nullptr;
            view->onProfile(anchor ? anchor : static_cast<QWidget *>(view->m_chatArea));
        };
        actions << profile;
        return actions;
    }

    // An activated link in the answer, offered to the context before the console's own handling.
    // A card zooms on this very board; a setting and a saved conversation are the window's, and
    // the view simply passes them on (#FEJQ, §30.4, and step 8's link kinds).
    bool resolveLink(const relay::links::Target &target) override
    {
        return m_view->resolveAgentLink(target);
    }

    QString placeholder() const override
    {
        return QStringLiteral("Ask the Board agent — Enter sends, a second prompt queues");
    }

  private:
    BoardView *m_view;
};

// The open card: the agent about this one card.
class CardContext final : public relay::agent::Context {
  public:
    explicit CardContext(BoardView *view) : m_view(view) {}

    relay::agent::ContextSpec spec() const override
    {
        relay::agent::ContextSpec spec;
        spec.name = QStringLiteral("card");
        const QString card = m_view->openCardId();
        // `card:AGNT` — the surface every event of this card's turn already carries
        // (board_turns.surface_of, protocol 33). A tab with two cards open on one worker is told
        // apart by it, and the id is what the board side routes by, as it has since 19.10.
        spec.surface = card.isEmpty() ? QStringLiteral("card")
                                      : QStringLiteral("card:") + card;
        spec.agentRole = QStringLiteral("switchboard");
        spec.workspace = m_view->m_workspace;
        // A card console is a console: step 3 of this card deleted the `card` tool scope, and
        // `"card"` left `agent_context.SCOPES` with it. It still arrived because
        // `RETIRED_SCOPES` maps it — a one-release courtesy for GUIs older than the worker, not
        // a name this GUI has any business sending.
        spec.scope = QStringLiteral("console");
        // One conversation per card, persisted per (tab, card) — the owner's decision 1 on card
        // #CTRN, which is the day the comment that stood here anticipated: a card turn is an
        // ordinary console turn now, and folding every card into the tab's one conversation would
        // make two cards serial again (#DR4K, #0Z13). The tab id is in the key because a tab owns
        // one worker and that worker owns its conversation files: two tabs showing one card are
        // two conversations about it, exactly as two tabs showing one pane's project are.
        // `TabConsoleContext` keeps a card key rather than overwriting it, and supplies the tab
        // id when the page has not been told one yet.
        spec.persistScope = card.isEmpty() ? QString() : QStringLiteral("helper");
        spec.persistKey = card.isEmpty()
            ? QString()
            : (m_view->m_tabId.isEmpty() ? spec.surface
                                         : m_view->m_tabId + QLatin1Char('/') + spec.surface);
        spec.briefKey = QStringLiteral("card");
        spec.briefTitle = card.isEmpty() ? QStringLiteral("Card") : QStringLiteral("#") + card;
        spec.screen = m_view->screenHint();
        spec.shell = false;
        spec.routing = QStringLiteral("agent");
        return spec;
    }

    QList<relay::agent::Action> actions() const override { return m_view->cardActions(); }

    // Enter, Ctrl+Enter and Ctrl+Shift+Enter on a card are the card's, not the console's: the
    // line travels as `board_ask` (19.10). Before step 5 the page took the composer's own
    // `onSubmit` to get this, which took the box away from everything else that speaks through
    // it; the console now offers the submit to its context first.
    bool submit(const QString &route, const QString &) override
    {
        return m_view->cardSubmitFromConsole(route);
    }

    bool resolveLink(const relay::links::Target &target) override
    {
        return m_view->resolveAgentLink(target);
    }

    // A card's Discuss answer is appended to `issues/threads/<ID>.md` with its `model=` and
    // `turn=<session>/<turn>` provenance — the owner's decision 2 on this card, because the
    // thread is the record a verifier reads (POLICY rules 2, 4 and 7). The **worker** is what
    // writes it (`board_protocol._card_answer`, kept by step 4), before and after the model sees
    // the words, so nothing is written from here: the entry arrives as `board_thread_appended`
    // and all the context does is make sure the page that shows the thread is looking at it.
    void turnFinished(const relay::agent::TurnRecord &record) override
    {
        m_view->cardTurnFinished(record);
    }

    QString placeholder() const override
    {
        // The three chords the card page has always had, said in the box that answers them
        // (owner, #VZ69: "remove comment / discuss buttons. i would say you just press enter in
        // the prompt box to discuss / comment").
        return QStringLiteral("Reply — Enter discusses, Ctrl+Enter plans, Ctrl+Shift+Enter only comments");
    }

  private:
    BoardView *m_view;
};

}  // namespace board

// ------------------------------------------------------------------------ the view

BoardView::BoardView(const QString &workspace, QWidget *parent)
    : QWidget(parent), m_workspace(workspace)
{
    setObjectName(QStringLiteral("boardView"));
    m_requestPrefix = QStringLiteral("sb%1-").arg(quintptr(this), 0, 36);
    // The closed stages open unticked (owner, 2026-09-25: "make verified, done, dropped
    // unchecked by default"): Done is where dropped cards fold, and a "dropped" id is held for
    // the board that gives one a column. A saved choice still wins — the restore replaces this
    // whole set with `setHiddenSections`, and only when the last visit left something hidden.
    m_hidden.insert(board::verifiedSection());
    m_hidden.insert(board::doneSection());
    m_hidden.insert(QStringLiteral("dropped"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    buildChrome(layout);
    setFocusPolicy(Qt::StrongFocus);
    watchIssues();
}

// The consoles hold a pointer to their context and clear its `onChanged` as they go, so the
// widgets have to come down first: `~QWidget` would free the contexts and *then* the panes.
BoardView::~BoardView()
{
    delete m_console;
    m_console = nullptr;
    m_consoleHandle = relay::agent::ConsoleHandle();
    if (m_detail != nullptr) {
        m_detail->onActionsChanged = nullptr;
        m_cardConsoleHandle = relay::agent::ConsoleHandle();
        delete m_detail->console();
    }
    delete m_boardContext;
    m_boardContext = nullptr;
    delete m_cardContext;
    m_cardContext = nullptr;
}

void BoardView::buildChrome(QVBoxLayout *layout)
{
    // The pane's object tabs — Cards | Skills | Memories | Live (#9FX8), one row at the very top.
    // These are *objects*, not the 2026-09-18 category tabs: the Cards page is the existing
    // list untouched and `board.yaml`'s categories stay inside it, so that decision stands.
    buildPageTabs(layout);
    // The Live page (#C52H) is a tab of its own now, not a strip over the list; it is built
    // beside the other pages in buildBoardBody.
    // The pane's header row. Since 2026-09-18 there are no category tabs and no tools here: the
    // filter, "+ New card" and the section checkboxes are the top of the *list page*
    // (buildListTools), so a card that is open is not also looking at the list's controls. All
    // this row ever holds is the way back, and it is hidden while the list is on screen.
    m_head = new QWidget(this);
    m_head->setObjectName(QStringLiteral("boardHead"));
    auto *headLayout = new QVBoxLayout(m_head);
    headLayout->setContentsMargins(8, 5, 8, 6);
    headLayout->setSpacing(6);
    m_tools = new QHBoxLayout;
    m_tools->setContentsMargins(0, 0, 0, 0);
    m_tools->setSpacing(6);
    m_back = new QToolButton(m_head);
    m_back->setObjectName(QStringLiteral("boardBack"));
    m_back->setText(QStringLiteral("←  Back to board (Esc)"));
    m_back->setToolTip(QStringLiteral("Close the card and go back to the list (Esc)"));
    m_back->setCursor(Qt::PointingHandCursor);
    m_back->setFocusPolicy(Qt::NoFocus);
    m_tools->addWidget(m_back);
    m_tools->addStretch(1);
    headLayout->addLayout(m_tools);
    m_head->hide();                 // shown only while a card is open
    layout->addWidget(m_head);
    connect(m_back, &QToolButton::clicked, this, [this] {
        if (onHint)
            onHint(QStringLiteral("board.back"), QStringLiteral("Esc"));
        closeDetail();
    });

    m_problems = new QLabel(this);
    m_problems->setMinimumWidth(1);   // one line, clipped in a narrow pane; the tooltip has it all
    m_problems->setObjectName(QStringLiteral("boardProblems"));
    m_problems->setTextFormat(Qt::RichText);
    m_problems->hide();
    // Clicking a problem hands it to the page agent as a **draft** (owner, 2026-09-19: "if there
    // are problems, when you click on them, it sends a fix note to the switchboard agent" — and
    // "draft you confirm"). Nothing is sent: the request lands in the composer with the keyboard
    // in it, so the owner reads it before pressing Enter. Opening the file stays on `o`, which is
    // why the tooltip says so.
    connect(m_problems, &QLabel::linkActivated, this, [this](const QString &path) {
        if (!m_problemFix.isEmpty()) {
            draftForAgent(m_problemFix);
            return;
        }
        if (onOpenFile && !path.isEmpty())
            onOpenFile(QDir(m_workspace).absoluteFilePath(path));
    });
    // It is added to the list page's tools below, not here: with the pane's header empty while the
    // list is up, a problems line at this level would be the topmost row and the pane's hover
    // buttons would sit on its text.

    m_notice = new QFrame(this);
    m_notice->setObjectName(QStringLiteral("boardNotice"));
    m_notice->setAttribute(Qt::WA_StyledBackground);
    auto *noticeLayout = new QHBoxLayout(m_notice);
    noticeLayout->setContentsMargins(10, 4, 4, 4);
    noticeLayout->setSpacing(6);
    m_noticeText = new QLabel(m_notice);
    m_noticeText->setObjectName(QStringLiteral("boardNoticeText"));
    m_noticeText->setWordWrap(true);
    m_noticeText->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    // A `#ID` a notice names ("Created #K7Q2") is a link that zooms to the card — the same
    // `card:` anchor a card page and the cleanup panel open with (#3ZAP).
    connect(m_noticeText, &QLabel::linkActivated, this, [this](const QString &link) {
        const QUrl url(link);
        if (url.scheme() == QStringLiteral("card"))
            openCard((url.path().isEmpty() ? url.host() : url.path()).toUpper());
    });
    noticeLayout->addWidget(m_noticeText, 1);
    m_noticeUndo = new QToolButton(m_notice);
    m_noticeUndo->setObjectName(QStringLiteral("boardTextButton"));
    m_noticeUndo->setText(QStringLiteral("Undo (Ctrl+Z)"));
    m_noticeUndo->setToolTip(QStringLiteral("Put it back (Ctrl+Z)"));
    m_noticeUndo->setCursor(Qt::PointingHandCursor);
    m_noticeUndo->setFocusPolicy(Qt::NoFocus);
    noticeLayout->addWidget(m_noticeUndo);
    // The Check gate's one way through (#7BM4): on screen only for a move the gate refused, and
    // it asks for the reason before it re-sends, because the reason is what makes the override
    // a decision on the card rather than a click nobody can account for.
    m_noticeOverride = new QToolButton(m_notice);
    m_noticeOverride->setObjectName(QStringLiteral("boardTextButton"));
    m_noticeOverride->setText(QStringLiteral("Override…"));
    m_noticeOverride->setToolTip(QStringLiteral("Move it anyway, with a reason that goes on the "
                                                "card's thread as a decision"));
    m_noticeOverride->setCursor(Qt::PointingHandCursor);
    m_noticeOverride->setFocusPolicy(Qt::NoFocus);
    m_noticeOverride->hide();
    noticeLayout->addWidget(m_noticeOverride);
    auto *dismiss = new QToolButton(m_notice);
    dismiss->setObjectName(QStringLiteral("boardTextButton"));
    dismiss->setText(QStringLiteral("×"));
    dismiss->setFocusPolicy(Qt::NoFocus);
    noticeLayout->addWidget(dismiss);
    // It floats over the bottom of the board rather than taking a row: a notice that pushed the
    // columns down would move every card under the mouse, and again when it went away.
    m_notice->hide();
    m_noticeTimer = new QTimer(this);
    m_noticeTimer->setSingleShot(true);
    connect(m_noticeTimer, &QTimer::timeout, m_notice, &QWidget::hide);
    connect(dismiss, &QToolButton::clicked, m_notice, &QWidget::hide);
    connect(m_noticeUndo, &QToolButton::clicked, this, [this] { undoLast(); });
    connect(m_noticeOverride, &QToolButton::clicked, this, [this] { overrideGatedMove(); });

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);
    // The left half: the quick-add field (hidden until used) over the one scrolling list.
    m_listPane = new QWidget(m_splitter);
    m_listPane->setObjectName(QStringLiteral("boardListPane"));
    m_listPane->setMinimumWidth(240);
    m_listPane->installEventFilter(this);   // its width decides whether the tools row wraps
    auto *listLayout = new QVBoxLayout(m_listPane);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(0);
    buildListTools(listLayout);
    buildQuickAdd(listLayout);
    // The list's column header, between the tools and the rows: the sort lives here now (owner,
    // 2026-09-19). It measures against the list, so it is wired once the list is up.
    m_columnHeader = new ColumnHeader(m_listPane);
    m_columnHeader->onSort = [this](board::SortColumn column) {
        setSortOrder(board::sortId(board::nextColumnSort(column, m_model.sort())));
    };
    m_columnHeader->onStage = [this] { toggleGrouping(); };
    listLayout->addWidget(m_columnHeader);
    m_list = new RowList(&m_rows, m_listPane);
    m_columnHeader->setList(m_list);
    auto *delegate = new RowDelegate(&m_model, &m_rows, m_list, &m_signalsState);
    delegate->adds = [this](const QString &columnId) { return sectionTakesNewCards(columnId); };
    delegate->turnMode = [this](const QString &cardId) {
        return m_cardTurns.value(cardId).mode;
    };
    // Is the pane that claimed this card still open (#R9G7)? Asked as each row is drawn, so a
    // pane closed while the board is up reads as closed at the list's next repaint; the window
    // answers, and a view with no window behind it (a test) calls every claim live.
    delegate->paneExists = [this](const QString &token) { return tokenLive(token); };
    m_list->setItemDelegate(delegate);
    m_list->addRectOf = [delegate](int rowIndex, const QRect &itemRect) {
        return delegate->addRectOf(rowIndex, itemRect);
    };
    m_list->priorityRectOf = [delegate](int rowIndex, const QRect &itemRect) {
        return delegate->priorityRectOf(rowIndex, itemRect);
    };
    m_list->labelBadgeAt = [delegate](int rowIndex, const QRect &itemRect, const QPoint &at) {
        return delegate->labelBadgeAt(rowIndex, itemRect, at);
    };
    m_list->onCopyLabel = [this](const QString &label) { copyTag(label); };
    m_list->idCopyRectOf = [delegate](int rowIndex, const QRect &itemRect) {
        return delegate->idCopyRectOf(rowIndex, itemRect);
    };
    m_list->sessionChipRectOf = [delegate](int rowIndex, const QRect &itemRect) {
        return delegate->sessionChipRectOf(rowIndex, itemRect);
    };
    // A click on the claim chip reveals the pane that claimed the card (#YJ4A) — `revealClaim`
    // is the same path the card page's chip link takes (#R9G7).
    m_list->onRevealPane = [this](const QString &cardId) {
        const board::Card *card = m_model.card(cardId);
        if (card && !card->session.isEmpty())
            revealClaim(card->session);
    };
    m_list->onCopyId = [this](const QString &id) {
        copyCardReference(id);
        // A click is the slow path: `y` copies the selected card's reference (#FT77).
        if (onHint)
            onHint(QStringLiteral("copyId"), QStringLiteral("y"));
    };
    m_list->onPriority = [this](const QString &card, int step) { setCardPriority(card, step); };
    m_list->onToggleSection = [this](const QString &columnId) { toggleSection(columnId); };
    // A click on the self-closed fold row (#93WR) does what Enter on it does, and says so once:
    // the row is a keyboard row like any other.
    m_list->onToggleFold = [this](const QString &columnId) {
        if (onHint)
            onHint(QStringLiteral("board.selfClosedFold"), QStringLiteral("Enter"));
        toggleSelfClosed(columnId);
    };
    // The signals block (#AQ6X): the same click-is-Enter rule, and the same one-off hint.
    m_list->onToggleSignalFold = [this](const QString &which) {
        if (onHint)
            onHint(QStringLiteral("board.signalsFold"), QStringLiteral("Enter"));
        toggleSignalFold(which);
    };
    m_list->onOpenSignal = [this](const QString &key) { openSignal(key); };
    m_list->onAddInSection = [this](const QString &columnId) {
        if (onHint)
            onHint(QStringLiteral("board.quickAdd"), QStringLiteral("n"));
        quickAddIn(columnId);
    };
    m_list->triageRectOf = [delegate](int rowIndex, const QRect &itemRect) {
        return delegate->triageRectOf(rowIndex, itemRect);
    };
    // The ⚠ on a section header (#8YQ9): the board's own check, scoped to that section's cards.
    m_list->onTriageSection = [this](const QString &columnId) { requestCheck(columnId); };
    m_list->onDropped = [this](const QString &card, const QString &columnId, const QString &before,
                               const QString &after) {
        if (onHint)
            onHint(QStringLiteral("board.drag"), QStringLiteral("Alt+Shift+Arrows"));
        m_selected = card;
        moveCard(card, columnId, before, after);
    };
    m_list->onDragging = [this](bool on) {
        m_dragActive = on;
        if (on) {
            m_dragScroll->start();
            return;
        }
        m_dragScroll->stop();
        if (m_rebuildPending) {
            // Not from inside startDrag(): a rebuild there would run under its nested loop.
            QTimer::singleShot(0, this, [this] {
                if (m_rebuildPending && !m_dragActive)
                    rebuild();
            });
        }
    };
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this] {
        auto *item = m_list->currentItem();
        if (!item || !item->isSelected())
            return;
        const QString id = item->data(kCardRole).toString();
        if (id.isEmpty())
            return;
        m_selected = id;
        if (detailOpen())
            m_follow->start();
    });
    // A click opens the card, the way a card board did; a drag never counts as a click.
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const QString id = item->data(kCardRole).toString();
        if (id.isEmpty())
            return;
        // Ctrl+click (#HKY4): the browser's new-tab gesture — the card docks in a pane of its
        // own and this list keeps its place. Middle-click is the mouse-only version, caught in
        // RowList::mousePressEvent. Any other modifier still does nothing, so a drag or an
        // unfinished chord never opens anything.
        if (QApplication::keyboardModifiers() == Qt::ControlModifier) {
            openInOwnPane(id);
            return;
        }
        if (QApplication::keyboardModifiers() != Qt::NoModifier)
            return;
        m_selected = id;
        openSelected();
        // A click is the slow path (#HKY4): teach the mouse gesture that skips it.
        if (onHint)
            onHint(QStringLiteral("cardOwnPane"), QStringLiteral("middle-click"));
    });
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        const QString id = item->data(kCardRole).toString();
        if (id.isEmpty())
            return;
        m_selected = id;
        openSelected();
    });
    m_list->installEventFilter(this);
    syncColumnHeader();
    listLayout->addWidget(m_list, 1);
    m_splitter->addWidget(m_listPane);

    m_detail = new CardDetail(m_splitter);
    m_detail->hide();
    m_splitter->addWidget(m_detail);
    // A signal's page (#AQ6X) sits in the same half of the splitter as the card's, and only ever
    // one of the two is up: a card and a signal are both "the thing being read", and two pages
    // side by side would leave no list.
    m_signalDetail = new board::SignalDetail(m_splitter);
    m_signalDetail->hide();
    m_signalDetail->paneExists = [this](const QString &token) { return tokenLive(token); };
    m_signalDetail->onClose = [this] { closeSignal(); };
    m_signalDetail->onEscape = [this] {
        const QString key = m_signalDetail->key();
        closeSignal();
        if (board::rowOfSignal(m_rows, key) >= 0)
            selectSignal(key);
    };
    m_signalDetail->onOpenCard = [this](const QString &id) { openCardFromClick(id); };
    m_signalDetail->onFocusPane = [this](const QString &token) { revealClaim(token); };
    m_signalDetail->onClaim = [this] {
        sendSignal(QStringLiteral("signals_claim"),
                   {{QStringLiteral("pane_token"), paneClaimToken()}});
    };
    // The owner taking a signal back by hand. `gave-up` is the agents' word and is what promotes
    // a signal (decision 5); a release from this page says only that it is free again.
    m_signalDetail->onRelease = [this] {
        sendSignal(QStringLiteral("signals_release"),
                   {{QStringLiteral("reason"), QStringLiteral("released")}});
    };
    m_signalDetail->onPromote = [this] { sendSignal(QStringLiteral("signals_promote"), {}); };
    m_signalDetail->onDismiss = [this](const QString &reason, const QString &comment,
                                       const QString &until) {
        sendSignal(QStringLiteral("signals_dismiss"), {{QStringLiteral("reason"), reason},
                                                       {QStringLiteral("comment"), comment},
                                                       {QStringLiteral("until"), until}});
    };
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 2);
    m_splitter->setStretchFactor(2, 2);
    m_splitter->hide();                 // until the first `board` event: the loading line instead
    layout->addWidget(m_splitter, 1);

    auto *empty = new EmptyBoard(this);
    empty->setText(QStringLiteral("Loading the Board…"));
    m_empty = empty;
    m_empty->setObjectName(QStringLiteral("boardEmpty"));
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    layout->addWidget(m_empty, 1);

    // Under those words when — and only when — the worker has failed (#7M6E): the board that
    // never loaded used to say "Loading the Switchboard…" for ever, because the one place the
    // failure was reported was a status bar this layout does not show. The button is the same
    // `board_open` the pane opens with, and the window starts a fresh worker for it.
    m_emptyRetryRow = new QWidget(this);
    m_emptyRetryRow->setObjectName(QStringLiteral("boardEmptyRetry"));
    auto *retryRow = new QHBoxLayout(m_emptyRetryRow);
    retryRow->setContentsMargins(0, 0, 0, 14);
    auto *retry = new QToolButton(m_emptyRetryRow);
    retry->setObjectName(QStringLiteral("boardAddButton"));   // the pane's own button styling
    retry->setText(QStringLiteral("Retry"));
    retry->setCursor(Qt::PointingHandCursor);
    retry->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(retry, &QToolButton::clicked, this, [this] {
        m_workerError.clear();
        rebuild();
        reload();
    });
    retryRow->addStretch(1);
    retryRow->addWidget(retry);
    retryRow->addStretch(1);
    m_emptyRetryRow->hide();
    layout->addWidget(m_emptyRetryRow);

    // The gear's page. A sibling of the splitter rather than a window over it (owner: new
    // surfaces are panes, not floating strips), shown in its place and gone again on Save or
    // Cancel — the pane keeps its size and its place in the window either way.
    m_sections = new board::SectionEditor(this);
    m_sections->hide();
    m_sections->onClose = [this] { closeSections(); };
    m_sections->onSave = [this](const QJsonObject &message) {
        QJsonObject request = message;
        request.insert(QStringLiteral("type"), QStringLiteral("board_sections"));
        request.insert(QStringLiteral("reason"), QStringLiteral("edited in the Board"));
        send(request);
        closeSections();
    };
    // "Move this board to board/" (protocol 19.17, #916B and #1CXD): the one action that renames
    // an existing board's folder. The worker answers `board_folder_changed` (handled in
    // handleEvent) or a refusal, which the notice line shows like any other error.
    m_sections->onFolder = [this] {
        send({{QStringLiteral("type"), QStringLiteral("board_folder")},
              {QStringLiteral("folder"), relay::projects::newBoardFolder()}});
        closeSections();
    };
    layout->addWidget(m_sections, 1);

    // The page agent's panel (#8YQ9), under everything the list page shows. **Not** inside the
    // splitter: `rebuild()` hides that whole widget when the board has no cards, and a board with
    // no cards is precisely the board the survey has something to say about — put there, the
    // survey could never be seen on the only board that gets one.
    buildChatArea(layout);

    // The Skills, Memories and Live pages (#9FX8 step 2, #C52H) are siblings of the cards list —
    // built once, hidden, and shown in the tab's place by applyPage() (`m_page`, rebuild()).
    buildSkillsPage(layout);
    buildMemoriesPage(layout);
    buildLivePage(layout);
    buildBackgroundPage(layout);

    m_keys = new QLabel(this);
    m_keys->setObjectName(QStringLiteral("boardKeys"));
    m_keys->setTextFormat(Qt::RichText);
    m_keys->setMinimumWidth(1);         // clipped in a narrow pane, never widening it
    layout->addWidget(m_keys);

    m_follow = new QTimer(this);
    m_follow->setSingleShot(true);
    m_follow->setInterval(120);
    connect(m_follow, &QTimer::timeout, this, [this] {
        if (detailOpen() && !m_selected.isEmpty() && m_selected != m_detail->cardId())
            openSelected();
    });
    m_dragScroll = new QTimer(this);
    m_dragScroll->setInterval(30);
    connect(m_dragScroll, &QTimer::timeout, this, [this] { autoScrollDuringDrag(); });

    updateDetailLayout();   // the key line's first text
    m_detail->onClose = [this] { closeDetail(); };
    // ⤴ (#Y2BA): the card goes to its own pane and this one goes back to the list, so the list
    // is free to open the next card.
    m_detail->onPopOut = [this] {
        const QString id = m_detail->cardId();
        if (id.isEmpty() || !onOpenInNewPane || m_pinned)
            return;
        closeDetail();
        onOpenInNewPane(id);
    };
    m_detail->onEscape = [this] {
        // Esc in the reply box goes back to the rows; a second Esc there closes the card. With
        // the list hidden (a narrow pane) there is nothing to go back to but the board.
        if (!m_listPane->isHidden() && board::rowOfCard(m_rows, m_detail->cardId()) >= 0)
            selectCard(m_detail->cardId());
        else
            closeDetail();
    };
    m_detail->onToPrompt = [this] {
        if (onSendToTerminal && !m_detail->cardId().isEmpty())
            onSendToTerminal(QStringLiteral("#") + m_detail->cardId() + QLatin1Char(' '));
    };
    m_detail->onControlsResized = [this] { placeNotice(); };
    m_detail->onFocusPane = [this](const QString &token) { revealClaim(token); };
    m_detail->paneExists = [this](const QString &token) { return tokenLive(token); };
    m_detail->hasCard = [this](const QString &id) { return m_model.card(id) != nullptr; };
    m_detail->onCopyId = [this](const QString &id) { copyCardReference(id); };
    m_detail->onCopyTag = [this](const QString &tag) { copyTag(tag); };
    m_detail->onCopyIdHint = [this] {
        if (onHint)
            onHint(QStringLiteral("copyId"), QStringLiteral("y"));
    };
    m_detail->onOpenCard = [this](const QString &id) { openCardFromClick(id); };
    // Middle-click on a `#ID` link in the card page (#HKY4): the same docking as Ctrl+click.
    m_detail->onOpenOwnPane = [this](const QString &id) { openInOwnPane(id); };
    m_detail->onOpenCommits = [this](const QString &command) {
        if (onRunCommand)
            onRunCommand(command);
        else
            showNotice(QStringLiteral("Run it yourself: %1").arg(command), false);
    };
    // Try it (#JNYN, 31.10). `try_run` and `try_answer` go to the worker like any other message;
    // the "open" line the section names is opened here, by what it is: a command runs in a
    // terminal pane beside the board (the window's `onRunCommand`), and a path or a `relay://`
    // link goes to the opener the card's own links already use. With no window behind either —
    // relay-board on its own, a test — the command is put in the notice line so it can be copied
    // rather than silently doing nothing.
    m_detail->onTryRequest = [this](const QJsonObject &message) {
        if (message.value(QStringLiteral("type")).toString() == QStringLiteral("try_run")) {
            m_tryCard = m_detail->cardId();
            m_detail->setTryRunning(true);
            if (!m_tryClock.isValid())
                m_tryClock.start();
            showTryItProgress(QStringLiteral("starting"));
        }
        send(message);
    };
    m_detail->onOpenTry = [this](const QString &target, const QString &kind) {
        if (target.isEmpty())
            return;
        if (kind == QStringLiteral("command")) {
            if (onRunCommand)
                onRunCommand(target);
            else
                showNotice(QStringLiteral("Run it yourself: %1").arg(target), false);
            return;
        }
        if (kind == QStringLiteral("link")) {
            // `relay://card/K7Q2` is a card on this board; anything else goes to the desktop's
            // handler, which is where every other non-file scheme in a card body goes.
            const QUrl url(target);
            const QString card = url.path().section(QLatin1Char('/'), -1).toUpper();
            if (url.host() == QStringLiteral("card") && !card.isEmpty())
                openCardFromClick(card);
            else
                QDesktopServices::openUrl(url);
            return;
        }
        if (onOpenFile)
            onOpenFile(QDir(m_workspace).absoluteFilePath(target));
    };
    m_detail->onOpenPath = [this](const QString &path) {
        if (!onOpenFile || path.isEmpty())
            return;
        // A card file path is relative to the workspace; a link in the body may be relative to
        // the card's own folder. Whichever exists wins, the workspace first.
        const QString cardDir = QFileInfo(QDir(m_workspace).absoluteFilePath(m_detail->path())).absolutePath();
        for (const QString &base : {m_workspace, cardDir}) {
            const QString candidate = QDir(base).absoluteFilePath(QDir::cleanPath(path));
            if (QFileInfo::exists(candidate)) {
                onOpenFile(candidate);
                return;
            }
        }
        showNotice(QStringLiteral("No file at %1").arg(path), true);
    };
    m_detail->onReply = [this](const QString &text, const QString &mode) {
        const QString card = m_detail->cardId();
        if (card.isEmpty())
            return;
        if (!mode.isEmpty()) {
            // The worker would refuse it anyway (19.9's busy rule), but saying so here keeps the
            // question in the box instead of sending it away to bounce.
            if (cleanupRunning()) {
                m_busyCard = card;
                m_detail->restoreReply(text);
                m_detail->showError(QStringLiteral("A cleanup is running — it is rewriting cards, "
                                                   "so the agent cannot answer on one until it is "
                                                   "done. Your message is still here, unsent. Stop "
                                                   "the run with the button at the top of the "
                                                   "board, or wait for it."));
                // The line takes height at the bottom of the card, where the progress notice is.
                QTimer::singleShot(0, this, [this] { placeNotice(); });
                return;
            }
            // A card that is already working keeps the *running* turn's mode on its strip: the
            // prompt just sent is queued behind it and is not what "✦ Switchboarding · planning…"
            // is about (card #CTRN, Planning notes 5).
            const QString running = m_cardTurns.contains(card) ? m_cardTurns.value(card).mode : mode;
            m_cardTurns.insert(card, CardTurn{running, text});
            m_detail->setBusy(true, running);
            cardBusyChanged();
            // protocol 19.10: one `board_ask`, its mode "discuss" or "plan"; a plan may be
            // wordless. The verb stays what a phone sends (card #CTRN, decision 6) and what
            // changes is behind it: the turn goes to that card's own supervisor, so a second
            // Enter queues instead of bouncing. `surface` is the console's own — `card:<ID>`,
            // the tag every event of the turn comes back with (protocol 33).
            QJsonObject ask{{QStringLiteral("type"), QStringLiteral("board_ask")},
                            {QStringLiteral("card"), card}, {QStringLiteral("mode"), mode},
                            {QStringLiteral("surface"), QStringLiteral("card:") + card}};
            if (!text.isEmpty())
                ask.insert(QStringLiteral("text"), text);
            send(ask);
        } else {
            send({{QStringLiteral("type"), QStringLiteral("board_comment")},
                  {QStringLiteral("card"), card}, {QStringLiteral("text"), text},
                  {QStringLiteral("kind"), QStringLiteral("note")}});
        }
    };
    // `cancel` is the worker's own turn — here, a cleanup. A card turn runs on that card's own
    // agent (protocol 19.16), so stopping it names the card, and stopping #A leaves #B planning.
    m_detail->onCancel = [this] {
        const QString card = m_detail->cardId();
        if (card.isEmpty())
            return;
        // `surface` is provenance here, not routing: `board_protocol._cancel_card` finds the
        // supervisor by the card id it was always addressed by, and never reads the surface. It
        // rides anyway so every message this console sends names the console it came from, and
        // so a reader of one line on the wire can see which of a tab's queues it is about.
        send({{QStringLiteral("type"), QStringLiteral("board_cancel")},
              {QStringLiteral("card"), card},
              {QStringLiteral("surface"), QStringLiteral("card:") + card}});
    };
    m_detail->onExecute = [this](const QString &note, bool background) { executeCard(note, background); };
    m_detail->onVerify = [this](const QString &note) { verifyCard(note); };
    m_detail->onResume = [this](const QString &task, const QString &note) { resumeCard(task, note); };
    // The Tests strip's Check and its three actions (#7BM4, protocol 31.1): the card builds the
    // request, the view stamps it with a request id and puts it on the board worker's stdin.
    m_detail->onTestsRequest = [this](const QJsonObject &message) { send(message); };
    // "None apply" under the gate's question (#PR4Q): the answer goes on the thread — it is a
    // decision about what proves this card, and the next person to read it should see it — and
    // the move that was refused is sent again. The worker asks once, so it goes through.
    m_detail->onTestsNoneApply = [this] {
        const QString card = m_detail->cardId();
        if (card.isEmpty())
            return;
        send({{QStringLiteral("type"), QStringLiteral("board_comment")},
              {QStringLiteral("card"), card},
              {QStringLiteral("kind"), QStringLiteral("note")},
              {QStringLiteral("text"),
               QStringLiteral("Asked which checks prove #%1 on its way to a QA lane: none of "
                              "this project's checks apply to it, and it was moved with `## "
                              "Tests` left empty.").arg(card)}});
        if (m_gatedMove.isEmpty())
            return;
        QJsonObject move = m_gatedMove;
        m_gatedMove = QJsonObject();
        move.remove(QStringLiteral("id"));
        const QString id = nextRequestId();
        move.insert(QStringLiteral("id"), id);
        m_pendingNotes.insert(id, QStringLiteral("Moved #%1 with no checks named").arg(card));
        send(move);
    };
    m_detail->onModeHint = [this](const QString &mode) {
        if (!onHint)
            return;
        if (mode == QStringLiteral("plan"))
            onHint(QStringLiteral("board.plan"), QStringLiteral("p"));
        else if (mode == QStringLiteral("execute"))
            onHint(QStringLiteral("board.execute"), QStringLiteral("r"));
        else if (mode == QStringLiteral("done"))
            onHint(QStringLiteral("board.done"), QStringLiteral("d"));
        else if (mode == QStringLiteral("verify"))
            onHint(QStringLiteral("board.verify"), QStringLiteral("v"));
        else if (mode == QStringLiteral("refine"))
            onHint(QStringLiteral("board.refine"), QStringLiteral("f"));
    };
    m_detail->onMove = [this](const QString &what, const QString &value) {
        const QString card = m_detail->cardId();
        if (card.isEmpty() || value.isEmpty())
            return;
        const QString id = nextRequestId();
        m_pendingNotes.insert(id, what == QStringLiteral("tab")
                                      ? QStringLiteral("Moved #%1 to %2").arg(card, board::tabTitle(value))
                                      : QStringLiteral("Moved #%1 to %2").arg(card, board::statusTitle(value)));
        // A status pick is a move to a lane: it takes the card out of the manual section it may
        // have been parked in (#3XZV). A tab pick leaves the parking alone.
        QJsonObject move{{QStringLiteral("type"), QStringLiteral("board_move")},
                         {QStringLiteral("id"), id},
                         {QStringLiteral("card"), card},
                         {what, value},
                         {QStringLiteral("reason"), QStringLiteral("changed in the Board")}};
        if (what == QStringLiteral("status"))
            move.insert(QStringLiteral("section"), QString());
        send(move);
    };
    // The card page's flag click (#DPJB) goes through the row's own function: one clamp, one
    // `board_priority`, one notice, one undo record for both places the flag can be clicked.
    m_detail->onPriority = [this](int step) { setCardPriority(m_detail->cardId(), step); };
    m_detail->onEdit = [this](const QJsonObject &patch, const QString &baseHash) {
        saveCardEdit(patch, baseHash);
    };
    m_detail->onDone = [this] { doneSelected(); };
    m_detail->onDelete = [this] { deleteCard(m_detail->cardId()); };
    m_detail->onDeleteHint = [this] {
        if (onHint)
            onHint(QStringLiteral("board.delete"), QStringLiteral("Del"));
    };
    m_detail->onEditHint = [this] {
        if (onHint)
            onHint(QStringLiteral("board.edit"), QStringLiteral("e"));
    };
}

// The top of the list page (owner, 2026-09-18: "put 'new card' and filter at the top of the main
// org page, not in the pane header ... they shouldn't show when you are clicked on a card"). It
// lives inside the list pane, so it is there exactly when the list is: an open card that has the
// pane to itself sees the way back instead, and never the list's tools.
void BoardView::buildListTools(QVBoxLayout *layout)
{
    auto *tools = new QWidget(m_listPane);
    tools->setObjectName(QStringLiteral("boardListTools"));
    // The checkbox row below wraps, and height-for-width only reaches it if every widget between
    // it and the list pane's layout passes the question on.
    QSizePolicy wrapping(QSizePolicy::Preferred, QSizePolicy::Minimum);
    wrapping.setHeightForWidth(true);
    tools->setSizePolicy(wrapping);
    auto *toolsLayout = new QVBoxLayout(tools);
    toolsLayout->setContentsMargins(8, 5, 8, 6);
    toolsLayout->setSpacing(5);

    m_listTools = new QHBoxLayout;
    m_listTools->setContentsMargins(0, 0, 0, 0);
    m_listTools->setSpacing(6);
    m_count = new QLabel(tools);
    m_count->setObjectName(QStringLiteral("boardCount"));
    m_listTools->addWidget(m_count);
    m_filter = new QLineEdit(tools);
    m_filter->setObjectName(QStringLiteral("boardFilter"));
    m_filter->setPlaceholderText(QStringLiteral("Filter  —  any word in the card, label:bug, "
                                                "status:done, folder:changes, @agent, waiting:me"));
    m_filter->setClearButtonEnabled(true);
    m_filter->setMinimumWidth(60);      // it gives way to the buttons rather than pushing them out
    m_listTools->addWidget(m_filter, 1);
    m_snoozed = new QToolButton(tools);
    m_snoozed->setObjectName(QStringLiteral("boardSnoozedFilter"));
    m_snoozed->setText(QStringLiteral("Snoozed"));
    m_snoozed->setCheckable(true);
    m_snoozed->hide();
    m_snoozed->setToolTip(QStringLiteral("Show snoozed cards until their date"));
    m_listTools->addWidget(m_snoozed);
    // There is no sort control in this row any more (owner, 2026-09-19: "change switchboard
    // sorting from a sort button to adding header columns that you click on"): the order is the
    // list's own column header, built with the list under these tools.
    m_add = new QToolButton(tools);
    m_add->setObjectName(QStringLiteral("boardAddButton"));
    m_add->setText(QStringLiteral("+  New card (n)"));
    m_add->setToolTip(QStringLiteral("New card in Inbox (n)"));
    m_add->setCursor(Qt::PointingHandCursor);
    m_add->setFocusPolicy(Qt::NoFocus);
    // Fixed, not the tool button's default: left to shrink, a QToolButton elides its own label to
    // "…" long before the filter box has given up any of its room.
    m_add->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_listTools->addWidget(m_add);
    // Clean up and the Switchboard agent's model box used to be built here and reparented into
    // the page agent's panel. Both are gone from this row for good (card #AGNT step 6): Clean up
    // is an action on the console's row (board::BoardContext::actions) and the model is the
    // console's own picker, which is the pane's.
    toolsLayout->addLayout(m_listTools);

    // Where those two go when the pane is too narrow to hold them beside the filter: the pane's
    // hover buttons keep their room at the right of the top row whatever happens, and at ~350 px
    // that leaves the filter nothing. Right-aligned, so the row still reads as the tools' end.
    m_toolsWrapRow = new QWidget(tools);
    m_toolsWrap = new QHBoxLayout(m_toolsWrapRow);
    m_toolsWrap->setContentsMargins(0, 0, 0, 0);
    m_toolsWrap->setSpacing(6);
    m_toolsWrap->addStretch(1);
    m_toolsWrapRow->hide();
    toolsLayout->addWidget(m_toolsWrapRow);

    // One checkbox per section, all ticked until one is unticked. They wrap onto a second line
    // rather than running off a ~350 px pane, which is why this is a flow layout and not a row.
    m_checks = new QWidget(tools);
    m_checks->setObjectName(QStringLiteral("boardSectionChecks"));
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    policy.setHeightForWidth(true);     // so the QVBoxLayout gives the wrapped rows their height
    m_checks->setSizePolicy(policy);
    auto *flow = new FlowLayout(m_checks, 10, 3);
    flow->setContentsMargins(0, 0, 0, 0);
    m_checksLayout = flow;
    toolsLayout->addWidget(m_checks);

    // (#1Q5V) The label chips row (#VKFV) is gone from this pane: the filter field's
    // `label:` and a click on any row's label badge carry the same choice, without a
    // permanent row spent naming labels. m_labelPicked and setLabelFilter stay, so a
    // restored session and typed `label:` filters still narrow the list.

    // The format problems belong to the list page too — they are about the cards it is showing —
    // and here they are under the tools rather than above them, where the pane's hover buttons
    // would cover the file name that fixes them.
    toolsLayout->addWidget(m_problems);

    layout->addWidget(tools);

    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_model.setFilter(text);
        if (onNavigationChanged) onNavigationChanged();
        // The list redraws on this keystroke from the rows alone — the scoped terms and the
        // fields — and the worker's answer about the card bodies settles it a moment later
        // (#7M6E). The rows stopped carrying each card's text, which was 2.2 MB scanned on this
        // thread per key.
        rebuild();
        startSearch();
    });
    connect(m_snoozed, &QToolButton::toggled, this, [this](bool checked) {
        m_model.setSnoozedOnly(checked);
        rebuild();
    });
    connect(m_add, &QToolButton::clicked, this, [this] {
        if (onHint)
            onHint(QStringLiteral("board.quickAdd"), QStringLiteral("n"));
        quickAdd();
    });
    m_filter->installEventFilter(this);
}

// ------------------------------------------------------ the pane's object tabs (#9FX8 step 2)
//
// Cards | Skills | Memories | Live, one segmented row at the top of the pane. The row is data
// (`kPageDefs`), so another object — Artifacts, once #FVVY's runs ledger exists (#EA37) — is
// one entry in the table, one case in `setPage` and one button here: no redesign. A pinned card
// pane (#Y2BA) never carries the row (the owner's decision 1: a card pane is about its card).
const BoardView::PageDef BoardView::kPageDefs[5] = {
    {BoardView::Page::Cards, "Cards", "boardPageTabCards"},
    {BoardView::Page::Skills, "Skills", "boardPageTabSkills"},
    {BoardView::Page::Memories, "Memories", "boardPageTabMemories"},
    {BoardView::Page::Live, "Live", "boardPageTabLive"},
    {BoardView::Page::Background, "Background", "boardPageTabBackground"},
};

void BoardView::buildPageTabs(QVBoxLayout *layout)
{
    m_pageTabs = new QWidget(this);
    m_pageTabs->setObjectName(QStringLiteral("boardPageTabs"));
    auto *row = new QHBoxLayout(m_pageTabs);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);
    m_pageGroup = new QButtonGroup(m_pageTabs);
    constexpr int kPageCount = sizeof(kPageDefs) / sizeof(kPageDefs[0]);
    for (int i = 0; i < int(kPageCount); ++i) {
        auto *button = new QPushButton(QString::fromLatin1(kPageDefs[i].label), m_pageTabs);
        button->setObjectName(QString::fromLatin1(kPageDefs[i].objectName));
        button->setCheckable(true);
        button->setChecked(kPageDefs[i].page == Page::Cards);
        button->setCursor(Qt::PointingHandCursor);
        if (kPageDefs[i].page == Page::Skills)
            button->setToolTip(QStringLiteral("The servers this workspace can route to, with "
                                              "version, cases and staleness"));
        else if (kPageDefs[i].page == Page::Memories)
            button->setToolTip(QStringLiteral("This board's memory records, expired first"));
        else if (kPageDefs[i].page == Page::Live)
            button->setToolTip(QStringLiteral("The panes open on this project and the cards "
                                              "they hold, one row per pane"));
        else if (kPageDefs[i].page == Page::Background)
            button->setToolTip(QStringLiteral("Work kept running after its pane closed"));
        m_pageGroup->addButton(button, i);
        row->addWidget(button);
    }
    row->addStretch(1);
    layout->addWidget(m_pageTabs);
    connect(m_pageGroup, &QButtonGroup::idClicked, this, [this](int id) {
        setPage(static_cast<Page>(id));
    });
}

void BoardView::setPage(Page page)
{
    if (m_page == page) {
        applyPage();
        return;
    }
    m_page = page;
    constexpr int kPageCount = sizeof(kPageDefs) / sizeof(kPageDefs[0]);
    for (int i = 0; i < int(kPageCount); ++i) {
        auto *button = m_pageGroup->button(i);
        if (!button)
            continue;
        const QSignalBlocker block(button);
        button->setChecked(i == int(page));
    }
    rebuild();
}

void BoardView::showBackgroundPage()
{
    setPage(Page::Background);
}

void BoardView::showSkill(const QString &name)
{
    setPage(Page::Skills);
    if (m_skillsPage != nullptr && !name.isEmpty())
        m_skillsPage->selectSkill(name);
}

// The visibility rule rebuild() re-runs. The Skills and Memories pages replace the cards list,
// never the card page: a card opened from either — a linked chip, a memory row — takes the
// ordinary card page solo, and Esc lands back on the tab it left. While the sections editor or
// a pinned card has the pane, nothing here shows at all.
void BoardView::applyPage()
{
    if (m_pageTabs == nullptr)
        return;
    const bool chromeUsable = !m_pinned && !m_sectionsOpen;
    m_pageTabs->setVisible(chromeUsable);
    const bool cards = m_page == Page::Cards;
    const bool detailUp = detailOpen() || signalOpen();
    m_skillsPage->setVisible(chromeUsable && !detailUp && m_page == Page::Skills);
    m_memoriesPage->setVisible(chromeUsable && !detailUp && m_page == Page::Memories);
    // The Live page settles its own visibility too (syncLivePage); this is the tab-switch rule.
    m_livePage->setVisible(chromeUsable && !detailUp && m_page == Page::Live);
    m_backgroundPage->setVisible(chromeUsable && !detailUp && m_page == Page::Background);
    if (chromeUsable && !cards && !detailUp) {
        m_splitter->hide();
        m_empty->hide();
        m_emptyRetryRow->hide();
        m_keys->hide();
    }
    if (chromeUsable && !detailUp && m_page == Page::Skills)
        m_skillsPage->ensureLoaded();   // the first time the tab is seen; a no-op afterwards
    if (chromeUsable && !detailUp && m_page == Page::Memories)
        refillMemories();           // re-read the model's memory cards on every rebuild
}

void BoardView::buildSkillsPage(QVBoxLayout *layout)
{
    // The list and the skill page are the registry view Globals › Skills also shows (#9FX8 step
    // 3): project rows here (decision 2), the global ones there, one page for both. The pane
    // gives it the worker, the console under the cards list and the card page.
    m_skillsPage = new skills::SkillRegistryView(skills::SkillRegistryView::Scope::Project,
                                                 QStringLiteral("board"), this);
    m_skillsPage->hide();
    m_skillsPage->send = [this](const QJsonObject &request) {
        QJsonObject message = request;
        const QString id = nextRequestId();
        message.insert(QStringLiteral("id"), id);
        send(message);
        return id;
    };
    // Load and Re-verify draft into the console, which lives under the cards list — so the tab
    // switches to Cards and the draft lands where it is seen. A draft, never a send.
    m_skillsPage->draft = [this](const QString &text) {
        setPage(Page::Cards);
        draftForAgent(text);
    };
    // Ctrl+click on a Linked chip docks the card in its own pane (#HKY4); a plain click still
    // reveals it in this list, where a skill's cards are a zoom, not a page swap.
    m_skillsPage->openCard = [this](const QString &id) {
        if (QApplication::keyboardModifiers() == Qt::ControlModifier && openInOwnPane(id))
            return;
        openCardSolo(id);
    };
    m_skillsPage->installEventFilter(this);   // middle-click on a Linked chip docks it (#HKY4)
    // The list's viewport and the Live surface's chips are filtered for their middle-clicks
    // too (#HKY4). The Live surface is found by name — a strip over the list (#TBRH) or a tab
    // of its own (#C52H), whichever shape it has — and remove-then-install keeps a rebuild
    // from stacking the filter.
    if (m_list != nullptr && m_list->viewport() != nullptr) {
        m_list->viewport()->removeEventFilter(this);
        m_list->viewport()->installEventFilter(this);
    }
    for (const char *name : {"boardLiveStrip", "boardLivePage"}) {
        QWidget *live = findChild<QWidget *>(QString::fromLatin1(name));
        if (live != nullptr) {
            live->removeEventFilter(this);
            live->installEventFilter(this);
        }
    }
    m_skillsPage->cardTitle = [this](const QString &id) {
        const board::Card *card = m_model.card(id);
        return card != nullptr ? card->title : QString();
    };
    m_skillsPage->toast = [this](const QString &text) { toast(text); };
    // Open file and a refined copy open in a pane, as the Skills dialog's did (#JVEJ).
    m_skillsPage->openDocument = [this](const QString &path) {
        if (onOpenFile)
            onOpenFile(path);
        else
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    };
    m_skillsPage->syncActions();
    layout->addWidget(m_skillsPage, 1);
}

// ------------------------------------------------------ the Memories page (#9FX8 step 2)

void BoardView::buildMemoriesPage(QVBoxLayout *layout)
{
    m_memoriesPage = new QWidget(this);
    m_memoriesPage->setObjectName(QStringLiteral("boardMemoriesPage"));
    m_memoriesPage->hide();
    auto *column = new QVBoxLayout(m_memoriesPage);
    column->setContentsMargins(0, 6, 0, 0);
    column->setSpacing(4);

    auto *tools = new QWidget(m_memoriesPage);
    auto *toolRow = new QHBoxLayout(tools);
    toolRow->setContentsMargins(0, 0, 0, 0);
    toolRow->setSpacing(6);
    m_memoryCount = new QLabel(tools);
    m_memoryCount->setObjectName(QStringLiteral("boardMemoryCount"));
    m_memoryCount->setText(QStringLiteral("Memories"));
    toolRow->addWidget(m_memoryCount, 1);
    m_memoryReverify = new QToolButton(tools);
    m_memoryReverify->setObjectName(QStringLiteral("boardMemoryReverify"));
    m_memoryReverify->setText(QStringLiteral("Re-verify"));
    m_memoryReverify->setToolTip(QStringLiteral("Draft a re-check of the selected memory against its paths into the console — a draft, never a send"));
    m_memoryReverify->setEnabled(false);
    toolRow->addWidget(m_memoryReverify);
    m_memoryRetire = new QToolButton(tools);
    m_memoryRetire->setObjectName(QStringLiteral("boardMemoryRetire"));
    m_memoryRetire->setText(QStringLiteral("Retire"));
    m_memoryRetire->setToolTip(QStringLiteral("Move the selected memory to the archive — the file stays, nothing is deleted"));
    m_memoryRetire->setEnabled(false);
    toolRow->addWidget(m_memoryRetire);
    column->addWidget(tools);

    m_memoryList = new QTreeWidget(m_memoriesPage);
    m_memoryList->setObjectName(QStringLiteral("boardMemoryList"));
    m_memoryList->setHeaderLabels({QStringLiteral("Memory"), QStringLiteral("Reviewed"),
                                   QStringLiteral("Paths")});
    m_memoryList->setRootIsDecorated(true);
    m_memoryList->setUniformRowHeights(true);
    m_memoryList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_memoryList->setColumnWidth(0, 360);
    column->addWidget(m_memoryList, 1);
    layout->addWidget(m_memoriesPage, 1);

    connect(m_memoryList, &QTreeWidget::itemSelectionChanged, this, [this] {
        QTreeWidgetItem *item = m_memoryList->currentItem();
        // Group headers carry no id: they are not selectable, but be safe anyway.
        m_memorySelected = item == nullptr ? QString() : item->data(0, Qt::UserRole).toString();
        const bool expired = item != nullptr && item->data(0, Qt::UserRole + 1).toBool();
        m_memoryRetire->setEnabled(!m_memorySelected.isEmpty());
        m_memoryReverify->setEnabled(expired);
    });
    connect(m_memoryList, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item, int) {
        const QString id = item == nullptr ? QString() : item->data(0, Qt::UserRole).toString();
        if (!id.isEmpty())
            openMemory(id);
    });
    connect(m_memoryRetire, &QToolButton::clicked, this, [this] { retireMemory(); });
    connect(m_memoryReverify, &QToolButton::clicked, this, [this] { reverifyMemory(); });
}

void BoardView::refillMemories()
{
    if (m_memoryList == nullptr)
        return;
    const QString keep = m_memorySelected;
    QSignalBlocker block(m_memoryList);  // non-const: refill re-selects and unblocks on purpose
    m_memoryList->clear();
    // The memory cards are model rows already — the board event carries them, and their section
    // is empty so the cards list never shows them. Expired first (#EA37 (b)): what needs an eye
    // before anything else; retired and rejected stay folded — present, nothing is deleted, but
    // out of the way.
    QList<board::Card> expired, active, suggested, retired, rejected;
    for (const board::Card &card : m_model.allCards()) {
        if (card.type != QLatin1String("memory"))
            continue;
        if (card.expired)
            expired << card;
        else if (card.status == QLatin1String("retired"))
            retired << card;
        else if (card.status == QLatin1String("rejected"))
            rejected << card;
        else if (card.status == QLatin1String("suggested"))
            suggested << card;
        else
            active << card;
    }
    const auto byName = [](const board::Card &a, const board::Card &b) {
        const QString an = a.name.isEmpty() ? a.title : a.name;
        const QString bn = b.name.isEmpty() ? b.title : b.name;
        return an.compare(bn, Qt::CaseInsensitive) < 0;
    };
    std::sort(expired.begin(), expired.end(), byName);
    std::sort(active.begin(), active.end(), byName);
    std::sort(suggested.begin(), suggested.end(), byName);
    std::sort(retired.begin(), retired.end(), byName);
    std::sort(rejected.begin(), rejected.end(), byName);
    QTreeWidgetItem *restore = nullptr;
    const auto addGroup = [this, &restore, &keep](const QString &title,
                                                  const QList<board::Card> &group, bool open) {
        if (group.isEmpty())
            return;
        auto *head = new QTreeWidgetItem(m_memoryList);
        head->setText(0, QStringLiteral("%1 (%2)").arg(title).arg(group.size()));
        head->setFlags(Qt::ItemIsEnabled);
        QFont bold = head->font(0);
        bold.setBold(true);
        head->setFont(0, bold);
        head->setFirstColumnSpanned(true);
        for (const board::Card &card : group) {
            auto *item = new QTreeWidgetItem(head);
            const QString name = card.name.isEmpty() ? card.title : card.name;
            item->setText(0, QStringLiteral("#%1 %2%3")
                                 .arg(card.id, name,
                                      card.pinned ? QStringLiteral("  · pinned") : QString()));
            QString reviewed = card.reviewed.isEmpty() ? QStringLiteral("never") : card.reviewed;
            if (card.expired && !card.pathsLastCommit.isEmpty())
                reviewed += QStringLiteral("  (paths moved %1)").arg(card.pathsLastCommit);
            item->setText(1, reviewed);
            item->setText(2, card.paths.join(QStringLiteral(", ")));
            item->setToolTip(0, card.title);
            item->setData(0, Qt::UserRole, card.id);
            item->setData(0, Qt::UserRole + 1, card.expired);
            if (card.id == keep)
                restore = item;
        }
        head->setExpanded(open);
    };
    addGroup(QStringLiteral("Expired"), expired, true);
    addGroup(QStringLiteral("Active"), active, true);
    addGroup(QStringLiteral("Suggestions"), suggested, true);
    addGroup(QStringLiteral("Retired"), retired, false);
    addGroup(QStringLiteral("Rejected"), rejected, false);
    const int live = expired.size() + active.size() + suggested.size();
    m_memoryCount->setText(QStringLiteral("%1 memor%2 · %3 expired")
                               .arg(live)
                               .arg(live == 1 ? QStringLiteral("y") : QStringLiteral("ies"))
                               .arg(expired.size()));
    if (restore != nullptr) {
        block.unblock();
        m_memoryList->setCurrentItem(restore);
        m_memoryRetire->setEnabled(true);
        m_memoryReverify->setEnabled(restore->data(0, Qt::UserRole + 1).toBool());
    } else {
        m_memorySelected.clear();
        m_memoryRetire->setEnabled(false);
        m_memoryReverify->setEnabled(false);
    }
}

void BoardView::openMemory(const QString &id)
{
    // A memory is a card (#EA37: `#ID` addresses it, no `memory:` prefix anywhere): its page is
    // the ordinary card page, solo, and Esc comes back to this tab.
    openCardSolo(id);
}

void BoardView::retireMemory()
{
    if (m_memorySelected.isEmpty())
        return;
    send({{QStringLiteral("type"), QStringLiteral("board_move_card")},
          {QStringLiteral("id"), nextRequestId()},
          {QStringLiteral("card"), m_memorySelected},
          {QStringLiteral("status"), QStringLiteral("retired")},
          {QStringLiteral("reason"), QStringLiteral("Retired from the Memories tab")}});
    // The board event that follows rebuilds the page; the worker's notice says what happened.
}

void BoardView::reverifyMemory()
{
    if (m_memorySelected.isEmpty())
        return;
    const board::Card *card = m_model.card(m_memorySelected);
    const QString paths = card != nullptr ? card->paths.join(QStringLiteral(", ")) : QString();
    // A draft, never a send (owner, 2026-09-19: "draft you confirm"): the request lands in the
    // console's composer on the cards page for the person to press Enter on.
    setPage(Page::Cards);
    draftForAgent(QStringLiteral("Re-verify memory #%1: re-check it against %2 and set its "
                                 "reviewed date to today if it still holds; otherwise say what "
                                 "changed.")
                      .arg(m_memorySelected,
                           paths.isEmpty() ? QStringLiteral("its paths") : paths));
}

// What a cleanup leaves behind, in the list page rather than over it: a panel between the tools
// and the rows, dismissible, that never takes the keyboard off the list (owner rule: a new
// surface is a pane or in-pane, never a floating strip).
void BoardView::buildCleanupPanel(QVBoxLayout *layout)
{
    m_cleanupPanel = new QWidget(m_chatArea);
    m_cleanupPanel->setObjectName(QStringLiteral("boardCleanupPanel"));
    m_cleanupPanel->setAttribute(Qt::WA_StyledBackground);
    auto *panel = new QVBoxLayout(m_cleanupPanel);
    panel->setContentsMargins(10, 8, 10, 8);
    panel->setSpacing(6);

    auto *top = new QHBoxLayout;
    top->setSpacing(6);
    m_cleanupHead = new QLabel(m_cleanupPanel);
    m_cleanupHead->setObjectName(QStringLiteral("boardCleanupHead"));
    m_cleanupHead->setWordWrap(true);
    top->addWidget(m_cleanupHead, 1);
    m_cleanupApply = new QToolButton(m_cleanupPanel);
    m_cleanupApply->setObjectName(QStringLiteral("boardAddButton"));
    m_cleanupApply->setText(QStringLiteral("Apply"));
    m_cleanupApply->setToolTip(QStringLiteral("Run the cleanup for real and write these changes. "
                                              "Every write goes in the changelog, and nothing is "
                                              "committed for you."));
    m_cleanupApply->setCursor(Qt::PointingHandCursor);
    m_cleanupApply->setFocusPolicy(Qt::NoFocus);
    m_cleanupApply->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    top->addWidget(m_cleanupApply);
    m_cleanupLog = new QToolButton(m_cleanupPanel);
    m_cleanupLog->setObjectName(QStringLiteral("boardTextButton"));
    m_cleanupLog->setText(QStringLiteral("Changelog"));
    m_cleanupLog->setToolTip(QStringLiteral("Open the run's changelog in a pane"));
    m_cleanupLog->setCursor(Qt::PointingHandCursor);
    m_cleanupLog->setFocusPolicy(Qt::NoFocus);
    top->addWidget(m_cleanupLog);
    m_cleanupDismiss = new QToolButton(m_cleanupPanel);
    m_cleanupDismiss->setObjectName(QStringLiteral("boardTextButton"));
    m_cleanupDismiss->setText(QStringLiteral("Dismiss"));
    m_cleanupDismiss->setToolTip(QStringLiteral("Put this away. The changelog keeps the record."));
    m_cleanupDismiss->setCursor(Qt::PointingHandCursor);
    m_cleanupDismiss->setFocusPolicy(Qt::NoFocus);
    top->addWidget(m_cleanupDismiss);
    panel->addLayout(top);

    m_cleanupBody = new QTextBrowser(m_cleanupPanel);
    m_cleanupBody->setObjectName(QStringLiteral("boardCleanupBody"));
    m_cleanupBody->setOpenLinks(false);          // a card id opens the card, not a web browser
    m_cleanupBody->setFocusPolicy(Qt::NoFocus);  // the arrows stay with the list
    m_cleanupBody->setMaximumHeight(260);
    relay::installCopyOnSelect(m_cleanupBody);
    panel->addWidget(m_cleanupBody);

    m_cleanupPanel->hide();
    layout->addWidget(m_cleanupPanel);

    connect(m_cleanupDismiss, &QToolButton::clicked, this, [this] { hideCleanupPanel(); });
    connect(m_cleanupApply, &QToolButton::clicked, this, [this] { startCleanup(false); });
    connect(m_cleanupLog, &QToolButton::clicked, this, [this] {
        if (onOpenFile && !m_cleanupChangelog.isEmpty())
            onOpenFile(QDir(m_workspace).absoluteFilePath(m_cleanupChangelog));
    });
    connect(m_cleanupBody, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        // `card:K7Q2` goes through the same path a row click does; anything else is a file.
        if (url.scheme() == QStringLiteral("card")) {
            openCard((url.path().isEmpty() ? url.host() : url.path()).toUpper());
            return;
        }
        if (onOpenFile && !url.path().isEmpty())
            onOpenFile(QDir(m_workspace).absoluteFilePath(url.path()));
    });
}

// --------------------------------------------------------- the page agent (#8YQ9, protocol 19.18)

// The conversation about the whole board, pinned under the list on the list page. It is a panel
// in the page, not a window over it and not a second pane (owner's standing rule: a new surface
// ------------------------------------------------------------- the Switchboard agent's area
//
// The list page's agent (card #AGNT, protocol 33), pinned under the list. It is **not** inside
// the splitter: `rebuild()` hides that whole widget when the board has no cards, and a board with
// no cards is precisely the board the survey has something to say about — put there, the survey
// could never be seen on the only board that gets one. It goes away with the list page too, so a
// card is never looking at the board's conversation.
//
// Three things live in the column, top to bottom:
//
//   the Check findings   a list to act on, drafted into the composer a click at a time
//   the survey offer     an import form: checkboxes and a button, not a turn
//   the console          the agent — a no-shell `Pane` the window makes (step 5)
//
// The first two are the board's own widgets and always were, in everything but parentage: they
// were built inside the helper panel because that is where they were drawn, and the panel never
// did anything with them. They keep their object names, so src/Theme.cpp's rules and every test
// that finds them still do.
void BoardView::buildChatArea(QVBoxLayout *layout)
{
    m_chatArea = new QWidget(this);
    m_chatArea->setObjectName(QStringLiteral("boardChatArea"));
    auto *column = new QVBoxLayout(m_chatArea);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(4);

    m_findings = new QWidget(m_chatArea);
    m_findings->setObjectName(QStringLiteral("boardChatFindings"));
    m_findingsLayout = new QVBoxLayout(m_findings);
    m_findingsLayout->setContentsMargins(8, 4, 8, 4);
    m_findingsLayout->setSpacing(2);
    m_findings->hide();
    column->addWidget(m_findings);
    buildCleanupPanel(column);

    m_survey = new QWidget(m_chatArea);
    m_survey->setObjectName(QStringLiteral("boardChatSurvey"));
    m_surveyLayout = new QVBoxLayout(m_survey);
    m_surveyLayout->setContentsMargins(8, 4, 8, 4);
    m_surveyLayout->setSpacing(4);
    m_survey->hide();
    column->addWidget(m_survey);

    m_chatArea->hide();          // syncChatVisible decides, and it runs on every rebuild
    layout->addWidget(m_chatArea);
}

// The console, asked for once and only when the area is actually shown. A tab nobody asks
// anything from never pays for one (§30.7, owner decision 5), and a view with no factory — a
// test, or relay-board linked on its own — shows the findings and the survey and no agent at all.
void BoardView::ensureConsole()
{
    if (m_console != nullptr || !onCreateConsole || m_chatArea == nullptr)
        return;
    if (m_boardContext == nullptr)
        m_boardContext = new board::BoardContext(this);
    const relay::agent::ConsoleHandle handle = onCreateConsole(m_boardContext, m_chatArea);
    if (!handle)
        return;
    m_consoleHandle = handle;
    m_console = handle.widget;
    if (handle.setTranscriptHiddenUntilUsed) {
        // Pane's terminal host precedes its named queue strip in the console column.
        // Watch that host so the minimum follows the first output, not an empty terminal.
        if (auto *column = qobject_cast<QVBoxLayout *>(m_console->layout())) {
            if (auto *queue = m_console->findChild<QWidget *>(QStringLiteral("queueStrip"))) {
                const int index = column->indexOf(queue);
                if (index > 0)
                    m_listTranscriptHost = column->itemAt(index - 1)->widget();
            }
        }
        if (m_listTranscriptHost)
            m_listTranscriptHost->installEventFilter(this);
        handle.setTranscriptHiddenUntilUsed(true);
    }
    if (auto *column = qobject_cast<QVBoxLayout *>(m_chatArea->layout()))
        column->addWidget(m_console, 1);
    m_console->show();
    updateConsoleHeight();
    // The key legend ends with the console's action row, read off the context — it could say
    // nothing about it until there was a row to read (agentActionKeyLine). `ensureConsole` has
    // already set `m_console`, so the `syncChatVisible` inside this does not come back here.
    updateDetailLayout();
}

// A live turn needs the old 20-line floor for its transcript and queue strip. Before the first
// byte, the hidden terminal host needs no floor; the composer supplies its own size hint.
//
// The card page's console has no cap of its own since card #ZPHJ: the divider above it
// (CardDetail::agentSplit) decides its share, 40 % until the reader drags it. Its floor is lower,
// so the divider can hand most of the page back to the card.
void BoardView::updateConsoleHeight()
{
    const int line = QFontMetrics(font()).lineSpacing();
    const int cap = std::max(10 * line, height() * 2 / 5);
    const int floor = std::min(cap, 20 * line);
    if (m_console != nullptr) {
        m_console->setMaximumHeight(cap);
        m_console->setMinimumHeight(m_listTranscriptHost && m_listTranscriptHost->isHidden() ? 0 : floor);
    }
    if (QWidget *card = m_detail != nullptr ? m_detail->console() : nullptr) {
        card->setMaximumHeight(QWIDGETSIZE_MAX);
        card->setMinimumHeight(m_cardTranscriptHost && m_cardTranscriptHost->isHidden() ? 0 : std::min(floor, 8 * line));
    }
}

AgentSplit *BoardView::cardSplit() const
{
    return m_detail != nullptr ? m_detail->agentSplit() : nullptr;
}

// The open card's console (card #AGNT step 6). A second console, not the list page's one: the
// two are different conversations — one about the board, one about this card (§30.7's persist
// key) — and only one of the two pages is ever on screen, so nothing is drawn twice.
void BoardView::ensureCardConsole()
{
    if (m_detail == nullptr || m_detail->console() != nullptr || !onCreateConsole)
        return;
    if (m_cardContext == nullptr) {
        m_cardContext = new board::CardContext(this);
        // The card page says the row and the strip would answer differently; the context is what
        // carries that to the console, which is the only thing that draws them.
        m_detail->onActionsChanged = [this] { refreshContexts(); };
    }
    const relay::agent::ConsoleHandle handle = onCreateConsole(m_cardContext, m_detail);
    if (!handle)
        return;
    m_cardConsoleHandle = handle;   // the card page's half of `clearTranscript` (card #CTRN)
    // Card #2FQ9: the page's "⤴ Agent + shell" moves this console into a linked shell pane, and
    // the window says where it is after every move so the button reads right.
    m_detail->onLinkAgent = [this] {
        if (m_cardConsoleHandle.toggleLinked)
            m_cardConsoleHandle.toggleLinked();
    };
    m_cardContext->onLinkChanged = [this](bool linked) {
        if (m_detail != nullptr)
            m_detail->setAgentLinked(linked);
    };
    if (handle.setTranscriptHiddenUntilUsed) {
        if (auto *column = qobject_cast<QVBoxLayout *>(handle.widget->layout())) {
            if (auto *queue = handle.widget->findChild<QWidget *>(QStringLiteral("queueStrip"))) {
                const int index = column->indexOf(queue);
                if (index > 0)
                    m_cardTranscriptHost = column->itemAt(index - 1)->widget();
            }
        }
        if (m_cardTranscriptHost)
            m_cardTranscriptHost->installEventFilter(this);
        handle.setTranscriptHiddenUntilUsed(true);
    }
    // The composer inside the console *is* the card's reply box from here on. There is no hook on
    // the console for a host-defined chord yet, so the host takes the editor's submit route —
    // The editor is still handed over: it *is* this page's reply box from here on (drafts,
    // history, the Esc walk). What no longer travels with it is the submit — `CardContext::submit`
    // takes that, so the box goes on answering everything else that speaks through it.
    // **By name, not by type.** `RichEditor` declares no `Q_OBJECT`, so finding one by type alone
    // matches on `QPlainTextEdit`'s metaobject and answers the first *plain text edit* in the
    // console -- which is the transcript's fallback view whenever that has been built. `m_reply`
    // then pointed at a widget the owner never types in, `CardDetail::submit` read it empty and
    // returned, and **Enter on a card did nothing at all**: the words stayed in the box, no
    // `board_ask` went out, and the thread was never written (19.10, owner decision 2). The
    // integration drive of card #AGNT read exactly that. `composerEditor` is the name
    // `RichEditor`'s constructor puts on every prompt box in Relay.
    // The transcript is **not** hidden until it is used, as it was while a card turn drew itself
    // into the thread view above (#AGNT). A card turn is an ordinary console turn since card
    // #CTRN, so this transcript *is* the live view of it — the bubbles, the tool rows, the §12
    // queue strip, Esc — and hiding it until the first byte would be a flicker on every first
    // turn and a card page that looks like it has no agent in it.
    m_detail->setConsole(handle.widget,
                         dynamic_cast<RichEditor *>(handle.widget->findChild<QPlainTextEdit *>(QStringLiteral("composerEditor"))));
    updateConsoleHeight();   // a maximum alone is not a size, and this one draws the turn
}

// What the board's key legend adds for the console's action row, read off the context rather
// than written out here: an action a later session adds brings its own letter with it, and a
// keyless one adds nothing.
QString BoardView::agentActionKeyLine() const
{
    if (m_boardContext == nullptr || m_console == nullptr)
        return QString();
    QString line;
    for (const relay::agent::Action &action :
         relay::agent::withUniqueLetters(m_boardContext->actions())) {
        if (!action.keyed())
            continue;
        line += QStringLiteral(" &nbsp; <b>%1</b> %2")
                    .arg(action.letter.trimmed().toHtmlEscaped(), action.label.toLower().toHtmlEscaped());
    }
    return line;
}

// A card turn started or ended, or a cleanup did. Both action rows care: a card's Plan, Execute
// and Verify wait while a turn runs on it, and the board's Clean up becomes Stop.
void BoardView::cardBusyChanged()
{
    refreshContexts();
}

// Both contexts, on the next turn of the event loop. The delay is not a nicety: the console
// rebuilds its action row whole on `changed()` and frees the button that is being clicked
// (src/Pane.h, `rebuildActionRow`), so Clean up becoming Stop would delete itself inside its own
// `clicked`. Coalesced, because several of these arrive together as a turn starts or ends.
void BoardView::refreshContexts()
{
    if (m_contextRefresh)
        return;
    if (boardActionDepth() == 0) {      // nobody's button is on the stack: do it now
        if (m_boardContext)
            m_boardContext->changed();
        if (m_cardContext)
            m_cardContext->changed();
        return;
    }
    m_contextRefresh = true;
    QTimer::singleShot(0, this, [this] {
        m_contextRefresh = false;
        if (m_boardContext)
            m_boardContext->changed();
        if (m_cardContext)
            m_cardContext->changed();
    });
}

void BoardView::focusHelper()
{
    ensureConsole();
    if (m_consoleHandle.focusComposer)
        m_consoleHandle.focusComposer();
}

QWidget *BoardView::cardConsole() const
{
    return m_detail != nullptr ? m_detail->console() : nullptr;
}

void BoardView::setTabId(const QString &tabId)
{
    if (m_tabId == tabId)
        return;
    m_tabId = tabId;
    // The conversation moved, so the console has to tell its worker: a `configure` whose
    // `persist` key changes drops the live conversation and the next ask adopts the new one's
    // (§30.7, `ContextSpec::persistId`).
    refreshContexts();
}

// Put a request in the console's composer and focus it — a **draft**, never sent (owner,
// 2026-09-19: "draft you confirm"). Every finding row and the problems banner land here.
void BoardView::draftForAgent(const QString &text)
{
    if (text.isEmpty())
        return;
    ensureConsole();
    if (m_consoleHandle.draftInComposer)
        m_consoleHandle.draftInComposer(text);
}

// What is on screen in this pane, for the `screen` hint that rides on each ask (§30.7): the
// filter if one is set, the sections and how many cards each is showing, and the open card. It
// is a hint about what is being read — the agent reads the rows themselves with `board_*`.
QString BoardView::screenHint() const
{
    QStringList parts;
    if (m_filter != nullptr && !m_filter->text().trimmed().isEmpty())
        parts << QStringLiteral("filter: %1").arg(m_filter->text().trimmed());
    QStringList sections;
    for (const board::Row &row : m_rows)
        if (row.kind == board::Row::Section)
            sections << QStringLiteral("%1 %2").arg(row.title).arg(row.count);
    if (!sections.isEmpty())
        parts << sections.join(QStringLiteral(", "));
    if (detailOpen() && !m_detail->cardId().isEmpty())
        parts << QStringLiteral("open card: #%1").arg(m_detail->cardId());
    return parts.join(QStringLiteral(" · "));
}

QString BoardView::openCardId() const
{
    return m_detail != nullptr ? m_detail->cardId() : QString();
}

QList<relay::agent::Action> BoardView::cardActions() const
{
    return m_detail != nullptr ? m_detail->cardActions() : QList<relay::agent::Action>();
}

bool BoardView::cardSubmitFromConsole(const QString &route)
{
    return m_detail != nullptr && m_detail->submitFromConsole(route);
}

// A card turn ended. The thread entry is the worker's write (19.10's `_card_answer`, with the
// `model=` and `turn=<session>/<turn>` provenance the owner asked for in decision 2), and it
// arrives here as `board_thread_appended` — so the only thing left to do is read the card back
// when this pane is the one showing it and the entry has not landed on its own.
void BoardView::cardTurnFinished(const relay::agent::TurnRecord &record)
{
    if (record.surface.isEmpty() || !detailOpen())
        return;
    const QString card = record.surface.section(QLatin1Char(':'), 1);
    if (card.isEmpty() || card != m_detail->cardId())
        return;
    send({{QStringLiteral("type"), QStringLiteral("board_card_get")},
          {QStringLiteral("card"), card}});
}

// A link in an answer, offered to the context before the console opens it the ordinary way.
bool BoardView::resolveAgentLink(const relay::links::Target &target)
{
    if (!target.valid)
        return false;
    if (target.kind == relay::links::Kind::Card) {
        const QString id = relay::links::cardIdOf(target.target);
        if (id.isEmpty())
            return false;
        openCard(id);
        return true;
    }
    if (target.kind == relay::links::Kind::Option) {
        QString section, row;
        if (!relay::links::optionOf(target.target, &section, &row) || !onOpenOption)
            return false;
        onOpenOption(section, row);
        return true;
    }
    if (target.kind == relay::links::Kind::Session) {
        const QString id = relay::links::sessionIdOf(target.target);
        if (id.isEmpty() || !onOpenSession)
            return false;
        onOpenSession(id);
        return true;
    }
    return false;
}

// ------------------------------------------------------- the findings list and the survey
//
// Both were drawn inside the helper panel until card #AGNT step 6 and neither was ever a
// conversation: a finding is a row you click to draft a fix, and the survey is an import form
// with checkboxes and a button. They are board widgets, above the console, with the object names
// they have always had — src/Theme.cpp's rules and the tests find them by those.

namespace {

// Empty a layout of its rows. `deleteLater` rather than `delete`: a row can be rebuilt from
// inside one of its own click handlers.
void clearBoardLayout(QLayout *layout)
{
    if (layout == nullptr)
        return;
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->setParent(nullptr);
            widget->deleteLater();
        }
        if (QLayout *child = item->layout())
            clearBoardLayout(child);
        delete item;
    }
}

QString prettySectionName(const QString &section)
{
    QString text = section;
    text.replace(QLatin1Char('-'), QLatin1Char(' '));
    text.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!text.isEmpty())
        text[0] = text.at(0).toUpper();
    return text;
}

QString problemCountText(int count)
{
    return count == 1 ? QStringLiteral("1 problem") : QStringLiteral("%1 problems").arg(count);
}

// The keys still ticked in the survey, in the order the worker proposed them.
QStringList tickedImportKeys(QWidget *survey)
{
    if (survey == nullptr)
        return {};
    QList<QPair<int, QString>> picked;
    const QList<QCheckBox *> boxes = survey->findChildren<QCheckBox *>();
    for (QCheckBox *box : boxes) {
        const QString key = box->property("importKey").toString();
        if (box->isChecked() && !key.isEmpty())
            picked.append({box->property("importOrder").toInt(), key});
    }
    std::sort(picked.begin(), picked.end());
    QStringList keys;
    for (const auto &row : std::as_const(picked))
        keys << row.second;
    return keys;
}

}  // namespace

int BoardView::showFindings(const QJsonArray &items, const QString &section)
{
    if (m_findings == nullptr)
        return 0;
    clearBoardLayout(m_findingsLayout);
    const int total = int(items.size());

    auto *headRow = new QHBoxLayout;
    headRow->setSpacing(6);
    auto *head = new QLabel(m_findings);
    head->setObjectName(QStringLiteral("boardChatFindingsHead"));
    const QString scope = section.isEmpty()
                              ? QStringLiteral("Hygiene · Format check")
                              : QStringLiteral("Triage · %1").arg(prettySectionName(section));
    head->setText(total == 0 ? QStringLiteral("%1: nothing to fix").arg(scope)
                             : QStringLiteral("%1: %2").arg(scope, problemCountText(total)));
    headRow->addWidget(head, 1);
    if (section.isEmpty()) {
        auto *cleanup = new QToolButton(m_findings);
        cleanup->setObjectName(QStringLiteral("boardCleanup"));
        cleanup->setText(cleanupRunning() ? QStringLiteral("Stop") : QStringLiteral("Clean up"));
        cleanup->setToolTip(QStringLiteral("Hygiene stage 2: ask the agent for a cleanup preview. "
                                          "Nothing is written until you choose Apply."));
        cleanup->setFocusPolicy(Qt::NoFocus);
        headRow->addWidget(cleanup);
        connect(cleanup, &QToolButton::clicked, this, [this] { requestCleanup(); });
    }
    auto *dismiss = new QToolButton(m_findings);
    dismiss->setObjectName(QStringLiteral("boardChatFindingsClose"));
    dismiss->setText(QStringLiteral("×"));
    dismiss->setToolTip(QStringLiteral("Put this list away. Hygiene lists them again."));
    dismiss->setCursor(Qt::PointingHandCursor);
    dismiss->setFocusPolicy(Qt::NoFocus);
    headRow->addWidget(dismiss, 0);
    m_findingsLayout->addLayout(headRow);
    connect(dismiss, &QToolButton::clicked, this, [this] {
        if (m_findings != nullptr)
            m_findings->hide();
    });

    if (total == 0) {
        auto *none = new QLabel(m_findings);
        none->setObjectName(QStringLiteral("boardChatFinding"));
        none->setText(QStringLiteral("Every card's format checks out — ids, front matter and "
                                     "threads."));
        m_findingsLayout->addWidget(none);
        m_findings->show();
        return 0;
    }

    // `#ID` out of the checker's path or message, so a finding that is about a card says so.
    static const QRegularExpression cardRef(QStringLiteral("#([0-9A-Za-z]{4})\\b"));
    for (const QJsonValue &value : items) {
        const QJsonObject problem = value.toObject();
        const QString path = problem.value(QStringLiteral("path")).toString();
        const QString message = problem.value(QStringLiteral("message")).toString();
        const QString severity = problem.value(QStringLiteral("severity")).toString();
        const QColor ink = severity == QStringLiteral("error") ? theme::Error : theme::Warning;
        const QString file = path.isEmpty() ? QStringLiteral("the board")
                                            : QFileInfo(path).fileName();

        auto *row = new QLabel(m_findings);
        row->setObjectName(QStringLiteral("boardChatFinding"));
        row->setWordWrap(true);
        row->setTextFormat(Qt::RichText);
        // Links only, no text selection: theme::polishWindow() renames every selectable QLabel to
        // `cwd`, which would take these rows out of their own stylesheet rules.
        row->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
        row->setCursor(Qt::PointingHandCursor);
        row->setText(QStringLiteral("<a href=\"fix\" style=\"color:%1;text-decoration:none\">%2 "
                                    "· %3</a> · %4")
                         .arg(ink.name(), severity.toHtmlEscaped(), file.toHtmlEscaped(),
                              message.toHtmlEscaped()));
        row->setToolTip((path.isEmpty() ? message : path + QStringLiteral(": ") + message)
                        + QStringLiteral("\n\nClick to draft a fix for the Board agent — it "
                                         "goes in the composer, it is not sent."));
        // The draft is built now and carried in the connection: the href only has to be clickable.
        const QString request = board::fixRequest(path, message, total);
        const QString card = cardRef.match(path.isEmpty() ? message : path).captured(1);
        const QString draftText = card.isEmpty()
            ? request
            : QStringLiteral("%1 (card #%2)").arg(request, card.toUpper());
        connect(row, &QLabel::linkActivated, this,
                [this, draftText](const QString &) { draftForAgent(draftText); });
        m_findingsLayout->addWidget(row);
    }
    m_findings->show();
    return total;
}

// `board_survey {root, project, hints, counts, proposals, git}` (19.18): what `project_probe`
// found offline and what an import would create. The agent narrates the same data in its opening
// turn — this is the part the owner has to *act* on, so it is checkboxes and a button.
//
// The GitHub corpus is a link and an offer to *look*: `forge_sync_plan` (19.14) reads both sides
// and writes to neither. Bringing the issues in is the sync itself, and that surface is #ZKR0's
// card — nothing here ever sends `forge_sync_run`.
void BoardView::showSurvey(const QJsonObject &event)
{
    if (m_survey == nullptr)
        return;
    clearBoardLayout(m_surveyLayout);
    m_import = nullptr;
    m_importKeys.clear();
    // Deleted with the layout above (deleteLater), so these must not be followed again.
    m_forgeLook = nullptr;
    m_forgeResult = nullptr;
    m_forgeRepo.clear();
    m_forgeRequest.clear();

    const QJsonObject counts = event.value(QStringLiteral("counts")).toObject();
    const QJsonArray hints = event.value(QStringLiteral("hints")).toArray();
    const QJsonArray proposals = event.value(QStringLiteral("proposals")).toArray();
    const QJsonObject git = event.value(QStringLiteral("git")).toObject();
    const QString project = event.value(QStringLiteral("project")).toString().isEmpty()
                                ? m_workspace
                                : event.value(QStringLiteral("project")).toString();
    const QString root = event.value(QStringLiteral("root")).toString();

    auto *headRow = new QHBoxLayout;
    headRow->setSpacing(6);
    auto *head = new QLabel(m_survey);
    head->setObjectName(QStringLiteral("boardChatSurveyHead"));
    head->setText(project.isEmpty()
                      ? QStringLiteral("Survey")
                      : QStringLiteral("Survey · %1").arg(QFileInfo(project).fileName()));
    if (!root.isEmpty())
        head->setToolTip(QStringLiteral("Board folder: %1")
                             .arg(project.isEmpty() ? root
                                                    : QDir(project).relativeFilePath(root)));
    headRow->addWidget(head, 1);
    auto *dismiss = new QToolButton(m_survey);
    dismiss->setObjectName(QStringLiteral("boardChatSurveyClose"));
    dismiss->setText(QStringLiteral("×"));
    dismiss->setToolTip(QStringLiteral("Not now. Nothing is imported; the conversation above keeps "
                                       "what was found."));
    dismiss->setCursor(Qt::PointingHandCursor);
    dismiss->setFocusPolicy(Qt::NoFocus);
    headRow->addWidget(dismiss, 0);
    m_surveyLayout->addLayout(headRow);
    connect(dismiss, &QToolButton::clicked, this, [this] { hideSurvey(); });

    const auto addLine = [this](const QString &text, bool rich = false) {
        auto *label = new QLabel(m_survey);
        label->setObjectName(QStringLiteral("boardChatSurveyLine"));
        label->setWordWrap(true);
        if (rich) {
            label->setTextFormat(Qt::RichText);
            label->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
        }
        label->setText(text);
        m_surveyLayout->addWidget(label);
        return label;
    };

    const int trackers = counts.value(QStringLiteral("trackers")).toInt();
    const int found = counts.value(QStringLiteral("items")).toInt();
    if (trackers > 0)
        addLine(QStringLiteral("Found %1 item%2 in %3 tracker%4 already in this project.")
                    .arg(found)
                    .arg(found == 1 ? QString() : QStringLiteral("s"))
                    .arg(trackers)
                    .arg(trackers == 1 ? QString() : QStringLiteral("s")));
    else
        addLine(QStringLiteral("No existing tracker was found — no TODO.md, backlog, issues "
                               "list or specs. An empty board is a fine answer."));

    if (!hints.isEmpty()) {
        addLine(QStringLiteral("Relay leaves these alone:"));
        for (const QJsonValue &value : hints) {
            const QJsonObject hint = value.toObject();
            QString what = hint.value(QStringLiteral("detail")).toString();
            if (what.isEmpty())
                what = hint.value(QStringLiteral("message")).toString();
            if (what.isEmpty())
                what = hint.value(QStringLiteral("kind")).toString();
            if (what.isEmpty())
                continue;
            const QString where = hint.value(QStringLiteral("path")).toString();
            addLine(QStringLiteral("• %1%2").arg(
                what, where.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(where)));
        }
    }

    if (git.value(QStringLiteral("forge")).toString() == QStringLiteral("github")
        && !git.value(QStringLiteral("owner")).toString().isEmpty()
        && !git.value(QStringLiteral("repo")).toString().isEmpty()) {
        const QString url = QStringLiteral("https://github.com/%1/%2/issues")
                                .arg(git.value(QStringLiteral("owner")).toString(),
                                     git.value(QStringLiteral("repo")).toString());
        QLabel *link = addLine(QStringLiteral("Its issues are on GitHub: "
                                              "<a href=\"%1\" style=\"color:%2\">%3</a>")
                                   .arg(url.toHtmlEscaped(), theme::Link.name(),
                                        url.toHtmlEscaped()),
                               true);
        connect(link, &QLabel::linkActivated, this, [](const QString &target) {
            QDesktopServices::openUrl(QUrl(target));
        });
        m_forgeRepo = QStringLiteral("%1/%2").arg(git.value(QStringLiteral("owner")).toString(),
                                                  git.value(QStringLiteral("repo")).toString());
        auto *lookRow = new QHBoxLayout;
        lookRow->setSpacing(6);
        m_forgeLook = new QToolButton(m_survey);
        m_forgeLook->setObjectName(QStringLiteral("boardChatForgeLook"));
        m_forgeLook->setText(QStringLiteral("Look for issues on GitHub"));
        m_forgeLook->setToolTip(QStringLiteral("Count what is on %1 and what a sync would do. It "
                                               "reads both sides and writes to neither — no card "
                                               "is created and no issue is touched. Syncing them "
                                               "is a separate surface (#ZKR0).").arg(m_forgeRepo));
        m_forgeLook->setCursor(Qt::PointingHandCursor);
        m_forgeLook->setFocusPolicy(Qt::NoFocus);
        m_forgeLook->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        lookRow->addWidget(m_forgeLook, 0);
        lookRow->addStretch(1);
        m_surveyLayout->addLayout(lookRow);
        connect(m_forgeLook, &QToolButton::clicked, this, [this] { lookForIssues(); });
        m_forgeResult = addLine(QStringLiteral("Nothing is fetched until you ask."));
    } else if (!git.value(QStringLiteral("url")).toString().isEmpty()) {
        addLine(QStringLiteral("Primary remote (%1): %2")
                    .arg(git.value(QStringLiteral("primary")).toString(),
                         git.value(QStringLiteral("url")).toString()));
    }

    if (proposals.isEmpty()) {
        addLine(QStringLiteral("There is nothing to import. Say what the board should hold and the "
                               "agent will create the cards."));
        m_survey->show();
        return;
    }

    addLine(QStringLiteral("An import would create %1 card%2. Untick anything Relay should leave "
                           "where it is:")
                .arg(proposals.size())
                .arg(proposals.size() == 1 ? QString() : QStringLiteral("s")));

    for (int index = 0; index < proposals.size(); ++index) {
        const QJsonObject proposal = proposals.at(index).toObject();
        const QJsonObject source = proposal.value(QStringLiteral("source")).toObject();
        // The key `board_import_apply` re-derives from the project. `source_key` is what
        // board_import.Proposal.to_dict() carries; `source.key` and the source path are the
        // fallbacks, so an older worker's shape still imports.
        QString key = proposal.value(QStringLiteral("source_key")).toString();
        if (key.isEmpty())
            key = source.value(QStringLiteral("key")).toString();
        if (key.isEmpty())
            key = source.value(QStringLiteral("path")).toString();
        if (key.isEmpty())
            continue;
        const QString title = proposal.value(QStringLiteral("title")).toString();
        const QString kind = source.value(QStringLiteral("kind")).toString();
        const QString path = source.value(QStringLiteral("path")).toString();
        QString label = title.isEmpty() ? key : title;
        if (!kind.isEmpty() || !path.isEmpty())
            label += QStringLiteral("  —  %1%2")
                         .arg(kind, path.isEmpty() ? QString()
                                                   : QStringLiteral(": %1").arg(path));

        auto *box = new QCheckBox(label, m_survey);
        box->setObjectName(QStringLiteral("boardChatProposal"));
        box->setChecked(true);      // the offer is "import these"; unticking is the exception
        box->setCursor(Qt::PointingHandCursor);
        box->setFocusPolicy(Qt::NoFocus);
        box->setToolTip(path.isEmpty() ? key : path);
        box->setProperty("importKey", key);
        box->setProperty("importOrder", index);
        m_surveyLayout->addWidget(box);
        connect(box, &QCheckBox::toggled, this, [this] {
            m_importKeys = tickedImportKeys(m_survey);
            if (m_import != nullptr) {
                m_import->setText(QStringLiteral("Import %1 card%2")
                                      .arg(m_importKeys.size())
                                      .arg(m_importKeys.size() == 1 ? QString()
                                                                    : QStringLiteral("s")));
                m_import->setEnabled(!m_importKeys.isEmpty());
            }
        });
    }

    m_importKeys = tickedImportKeys(m_survey);
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(6);
    buttons->addStretch(1);
    m_import = new QToolButton(m_survey);
    m_import->setObjectName(QStringLiteral("boardChatImport"));
    m_import->setText(QStringLiteral("Import %1 card%2")
                          .arg(m_importKeys.size())
                          .arg(m_importKeys.size() == 1 ? QString() : QStringLiteral("s")));
    m_import->setToolTip(QStringLiteral("Create a card for each ticked item. Every card keeps its "
                                        "source key, so nothing is imported twice."));
    m_import->setCursor(Qt::PointingHandCursor);
    m_import->setFocusPolicy(Qt::NoFocus);
    m_import->setEnabled(!m_importKeys.isEmpty());
    buttons->addWidget(m_import, 0);
    m_surveyLayout->addLayout(buttons);
    connect(m_import, &QToolButton::clicked, this, [this] { applyImport(); });

    m_survey->show();
}

void BoardView::hideSurvey()
{
    m_forgeLook = nullptr;
    m_forgeResult = nullptr;
    m_forgeRepo.clear();
    m_forgeRequest.clear();
    if (m_survey != nullptr)
        m_survey->hide();
    m_import = nullptr;
    m_importKeys.clear();
}

// `board_import_apply {keys}` (19.13). The keys are re-derived from the project on the worker
// side and never trusted from here, so a stale tick cannot create a card twice.
void BoardView::applyImport()
{
    m_importKeys = tickedImportKeys(m_survey);
    if (m_importKeys.isEmpty()) {
        if (onStatus)
            onStatus(QStringLiteral("Nothing is ticked, so nothing was imported."));
        return;
    }
    QJsonArray keys;
    for (const QString &key : std::as_const(m_importKeys))
        keys.append(key);
    send({{QStringLiteral("type"), QStringLiteral("board_import_apply")},
          {QStringLiteral("keys"), keys}});
    if (onStatus)
        onStatus(QStringLiteral("Importing %1 card%2…")
                     .arg(keys.size())
                     .arg(keys.size() == 1 ? QString() : QStringLiteral("s")));
    // The offer is answered; the cards themselves arrive as a `board_changed`.
    hideSurvey();
}

// `forge_sync_plan` (19.14): what a sync between this board and its GitHub issues *would* do. It
// is a dry run by construction — the protocol says it "writes to neither side" — so it is safe to
// offer on a board the owner has only just made, which is exactly when the survey asks.
void BoardView::lookForIssues()
{
    if (m_forgeRepo.isEmpty() || !m_forgeRequest.isEmpty())
        return;
    m_forgeRequest = nextRequestId();
    if (m_forgeLook != nullptr) {
        m_forgeLook->setEnabled(false);
        m_forgeLook->setText(QStringLiteral("Looking…"));
    }
    if (m_forgeResult != nullptr)
        m_forgeResult->setText(QStringLiteral("Asking github.com about %1…").arg(m_forgeRepo));
    send({{QStringLiteral("type"), QStringLiteral("forge_sync_plan")},
          {QStringLiteral("id"), m_forgeRequest},
          {QStringLiteral("repo"), m_forgeRepo}});
}

void BoardView::showForgePlan(const QJsonObject &event)
{
    m_forgeRequest.clear();
    if (m_forgeLook != nullptr) {          // put the button back, whatever the answer was
        m_forgeLook->setText(QStringLiteral("Look again"));
        m_forgeLook->setEnabled(true);
    }
    if (m_forgeResult == nullptr)
        return;
    const int creates = event.value(QStringLiteral("creates")).toInt();
    const int pushed = event.value(QStringLiteral("pushed")).toInt();
    const int pulled = event.value(QStringLiteral("pulled")).toInt();
    const int conflicts = event.value(QStringLiteral("conflicts")).toInt();
    QStringList parts;
    if (pulled > 0)
        parts << QStringLiteral("%1 issue%2 would become card%2").arg(pulled)
                     .arg(pulled == 1 ? QString() : QStringLiteral("s"));
    if (creates > 0)
        parts << QStringLiteral("%1 card%2 would become issue%2").arg(creates)
                     .arg(creates == 1 ? QString() : QStringLiteral("s"));
    if (pushed > 0)
        parts << QStringLiteral("%1 card%2 would be updated there").arg(pushed)
                     .arg(pushed == 1 ? QString() : QStringLiteral("s"));
    if (conflicts > 0)
        parts << QStringLiteral("%1 conflict%2").arg(conflicts)
                     .arg(conflicts == 1 ? QString() : QStringLiteral("s"));
    const QString what = parts.isEmpty()
        ? QStringLiteral("%1 and this board already agree — there is nothing to bring in.")
              .arg(m_forgeRepo)
        : QStringLiteral("%1: %2.").arg(m_forgeRepo, parts.join(QStringLiteral(", ")));
    // Said every time, not only when there is something: a count that looked like a result and
    // then wrote nothing would be the more surprising of the two.
    m_forgeResult->setText(what + QStringLiteral("  Nothing was written on either side — syncing "
                                                 "them is its own surface (#ZKR0). Ask here and "
                                                 "the agent can bring the same issues in as "
                                                 "ordinary cards."));
}

void BoardView::showForgeError(const QJsonObject &event)
{
    m_forgeRequest.clear();
    if (m_forgeLook != nullptr) {
        m_forgeLook->setText(QStringLiteral("Look again"));
        m_forgeLook->setEnabled(true);
    }
    if (m_forgeResult == nullptr)
        return;
    const QString code = event.value(QStringLiteral("code")).toString();
    QString text = event.value(QStringLiteral("text")).toString();
    if (code == QStringLiteral("forge_auth"))
        text = QStringLiteral("GitHub has no credential here yet, so nothing could be read. ")
               + text;
    else if (code == QStringLiteral("forge_rate_limited"))
        text = QStringLiteral("GitHub is rate limiting this token. %1")
                   .arg(event.value(QStringLiteral("retry_at_text")).toString().isEmpty()
                            ? text
                            : QStringLiteral("Try again %1.")
                                  .arg(event.value(QStringLiteral("retry_at_text")).toString()));
    if (text.trimmed().isEmpty())
        text = QStringLiteral("GitHub could not be read.");
    m_forgeResult->setText(text + QStringLiteral("  Nothing was written on either side."));
}

// `board_check`, scoped to a section (a header's warning mark) or to the whole board. Check on
// the console's action row sends the unscoped one; it is the same message, and both answers come
// back carrying this pane's request prefix, which is how `board_problems` below tells a check
// somebody asked for from the board's own refresh.
void BoardView::requestCheck(const QString &columnId)
{
    QJsonObject message{{QStringLiteral("type"), QStringLiteral("board_check")}};
    if (!columnId.isEmpty())
        message.insert(QStringLiteral("section"), columnId);
    send(message);
}

// The label chips follow the labels the board carries right now — every label on a card, plus
// any the board's own config names — rather than a hard-coded list. Rebuilt only when that set
// changes, so ticking one does not delete the chip under the pointer.
void BoardView::syncLabelChecks()
{
    QStringList labels = m_model.allLabels();
    for (const QJsonValue &value : m_config.value(QStringLiteral("labels")).toArray())
        if (!value.toString().isEmpty() && !labels.contains(value.toString()))
            labels << value.toString();
    labels.sort();
    if (labels == m_labelIds)
        return;
    m_labelIds = labels;
    // A label that went away while its chip was ticked simply stops filtering.
    m_labelPicked.intersect(QSet<QString>(labels.begin(), labels.end()));
    m_model.setLabelFilter(m_labelPicked);
    // (#1Q5V) no chips to sync any more; the labels still drive m_labelPicked, which a
    // typed `label:` in the filter box reads and narrows the same rows.
}

// The pane's hover buttons take their room out of whichever row is on top for good, and in a
// ~350 px pane that leaves the filter box nothing. Rather than let two QToolButtons elide to a
// pair of identical "…", they drop to a line of their own under the filter.
void BoardView::layoutListTools()
{
    if (!m_toolsWrap || !m_add)
        return;
    const int inset = m_head && m_head->isHidden() ? m_rightInset : 0;
    const int room = m_listPane->width() - 16 - inset;
    // The filter is owed a legible width before the button may sit beside it. Only "+ New card"
    // is in this row now — Clean up went down to the page agent's button row and the model box
    // into its composer row (#8YQ9) — so only its width is counted. Reserving theirs as well
    // wrapped the row a good 150 px earlier than it had to.
    const int need = m_count->sizeHint().width() + 150 + m_add->sizeHint().width() + 24;
    const bool wrap = room < need;
    if (wrap == m_toolsWrapped)
        return;
    m_toolsWrapped = wrap;
    QHBoxLayout *from = wrap ? m_listTools : m_toolsWrap;
    QHBoxLayout *to = wrap ? m_toolsWrap : m_listTools;
    from->removeWidget(m_add);
    to->addWidget(m_add);
    m_toolsWrapRow->setVisible(wrap);
}

// The boxes follow the sections the model has right now — board.yaml's columns, whatever extra
// statuses the cards carry, and Done — rather than a hard-coded list. Rebuilt only when that set
// changes, so ticking one does not delete the box under the pointer.
void BoardView::syncSectionChecks()
{
    const QList<board::Column> sections = m_model.sections();
    QStringList ids, titles;
    for (const board::Column &section : sections) {
        ids << section.id;
        titles << section.title;
    }
    // The empty board draws one unlit jack per section under these names (EmptyBoard, above).
    // Kept as well as set: `setEmptyText` takes them off again for a worker failure, where a row
    // of jacks under the bad news would read as decoration (#7M6E).
    m_emptySections = titles;
    static_cast<EmptyBoard *>(m_empty)->setSections(titles);
    // The names as well as the ids: renaming a section in the gear leaves the ids exactly as they
    // were, and comparing those alone left a box reading "Ready to start" under a header that
    // already said "Up next".
    if (ids == m_checkIds && titles == m_checkTitles)
        return;
    m_checkIds = ids;
    m_checkTitles = titles;
    while (QLayoutItem *item = m_checksLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (const board::Column &section : sections) {
        auto *box = new QCheckBox(section.title.toUpper(), m_checks);
        box->setObjectName(QStringLiteral("boardSectionCheck"));
        box->setChecked(!m_hidden.contains(section.id));
        box->setCursor(Qt::PointingHandCursor);
        box->setFocusPolicy(Qt::NoFocus);
        const QString id = section.id;
        connect(box, &QCheckBox::toggled, this, [this, id](bool on) {
            if (on)
                m_hidden.remove(id);
            else
                m_hidden.insert(id);
            rebuild();
            // The selection may have been in what just went away: stand on the first card left.
            if (board::rowOfCard(m_rows, m_selected) < 0) {
                const int first = board::stepRow(m_rows, -1, 1);
                m_selected = first >= 0 ? m_rows.at(first).cardId : QString();
            }
        });
        m_checksLayout->addWidget(box);
    }
    // And a one-click way to clear the row (owner, 2026-09-25: "add a uncheck all button on the
    // board"): the row is long, and the way to see just one or two stages is to untick everything
    // and tick those back on. Nothing is deleted — the unticks sit in `m_hidden`, and one tick
    // per section brings the list back.
    auto *uncheck = new QToolButton(m_checks);
    uncheck->setObjectName(QStringLiteral("boardSectionUncheckAll"));
    uncheck->setText(QStringLiteral("Uncheck all"));
    uncheck->setAutoRaise(true);
    uncheck->setCursor(Qt::PointingHandCursor);
    uncheck->setFocusPolicy(Qt::NoFocus);
    uncheck->setToolTip(
        QStringLiteral("Untick every section — tick the ones you want back on"));
    connect(uncheck, &QToolButton::clicked, this, [this] {
        const QList<board::Column> sections = m_model.sections();
        if (sections.isEmpty())
            return;
        for (const board::Column &section : sections)
            m_hidden.insert(section.id);
        // The boxes themselves, signal-blocked, instead of a rebuild that would delete them from
        // under the pointer; one rebuild after the lot.
        for (int i = 0; i < m_checksLayout->count(); ++i) {
            if (auto *box = qobject_cast<QCheckBox *>(m_checksLayout->itemAt(i)->widget())) {
                const QSignalBlocker block(box);
                box->setChecked(false);
            }
        }
        rebuild();
        // The selection may have been in what just went away: stand on the first card left.
        if (board::rowOfCard(m_rows, m_selected) < 0) {
            const int first = board::stepRow(m_rows, -1, 1);
            m_selected = first >= 0 ? m_rows.at(first).cardId : QString();
        }
    });
    m_checksLayout->addWidget(uncheck);
    // After the sections themselves, because it is about the list of them rather than about any
    // one (owner, 2026-09-19: "put a gear after the list of switchboard sections").
    auto *gear = new QToolButton(m_checks);
    gear->setObjectName(QStringLiteral("boardSectionGear"));
    gear->setText(QStringLiteral("⚙"));
    gear->setAutoRaise(true);
    gear->setCursor(Qt::PointingHandCursor);
    gear->setFocusPolicy(Qt::NoFocus);
    gear->setToolTip(QStringLiteral("Add, remove, merge or rename the sections. They are a view of "
                                    "the statuses: no card moves and none of them changes."));
    connect(gear, &QToolButton::clicked, this, [this] { openSections(); });
    m_checksLayout->addWidget(gear);
    m_checks->setVisible(!sections.isEmpty());
}

// The pane's hover buttons float over the top right of the pane, so whichever row is actually on
// top has to give up that much room — the header while a card is open, the list's tools otherwise.
void BoardView::applyRightInset()
{
    const bool head = m_head && !m_head->isHidden();
    if (m_tools)
        m_tools->setContentsMargins(0, 0, head ? m_rightInset : 0, 0);
    if (m_listTools)
        m_listTools->setContentsMargins(0, 0, head ? 0 : m_rightInset, 0);
    layoutListTools();
}

// The quick-add field, built once and shown when a card is added. It lines up with the rows
// under it rather than living inside a section, so a refill never takes it away mid-typing.
void BoardView::buildQuickAdd(QVBoxLayout *layout)
{
    m_quickAddRow = new QWidget(m_listPane);
    m_quickAddRow->setObjectName(QStringLiteral("boardQuickAddRow"));
    auto *row = new QHBoxLayout(m_quickAddRow);
    row->setContentsMargins(8, 5, 8, 3);
    auto *field = new QLineEdit(m_quickAddRow);
    field->setObjectName(QStringLiteral("boardQuickAdd"));
    field->setMaxLength(200);
    field->setToolTip(QStringLiteral("The card's title. Enter reviews likely duplicates and the issue before saving"));
    row->addWidget(field);
    m_quickAdd = field;
    m_quickAddRow->hide();
    layout->addWidget(m_quickAddRow);
    field->installEventFilter(this);
    m_quickAddTriageTimer = new QTimer(this);
    m_quickAddTriageTimer->setSingleShot(true);
    m_quickAddTriageTimer->setInterval(180);
    connect(m_quickAddTriageTimer, &QTimer::timeout, this,
            [this] { requestQuickAddTriage(); });
    connect(field, &QLineEdit::textChanged, this, [this] {
        m_quickAddTriageRequest.clear();
        m_quickAddDuplicates = {};
        m_quickAddRelated = {};
        m_quickAddChosenRelated.clear();
        renderQuickAddSuggestions();
        if (!m_quickAdd->text().trimmed().isEmpty())
            m_quickAddTriageTimer->start();
    });
    connect(field, &QLineEdit::returnPressed, this, [this] {
        if (m_quickAdd->text().trimmed().isEmpty()) {
            closeQuickAdd();
            focusInput();
            return;
        }
        showQuickAddReview();
    });

    m_quickAddReview = new QWidget(m_listPane);
    m_quickAddReview->setObjectName(QStringLiteral("boardQuickAddReview"));
    auto *review = new QVBoxLayout(m_quickAddReview);
    review->setContentsMargins(8, 2, 8, 6);
    review->addWidget(new QLabel(QStringLiteral("Issue"), m_quickAddReview));
    m_quickAddIssue = new QPlainTextEdit(m_quickAddReview);
    m_quickAddIssue->setObjectName(QStringLiteral("boardQuickAddIssue"));
    m_quickAddIssue->setMaximumHeight(90);
    m_quickAddIssue->setPlaceholderText(QStringLiteral("Describe the request"));
    m_quickAddIssue->installEventFilter(this);
    review->addWidget(m_quickAddIssue);
    connect(m_quickAddIssue, &QPlainTextEdit::textChanged, this, [this] {
        m_quickAddTriageRequest.clear();
        m_quickAddTriageStatus->clear();
        m_quickAddTriageStatus->hide();
        m_quickAddDuplicates = {};
        m_quickAddRelated = {};
        m_quickAddChosenRelated.clear();
        renderQuickAddSuggestions();
        if (!m_quickAddReview->isHidden())
            m_quickAddTriageTimer->start();
    });
    auto *choices = new QHBoxLayout;
    m_quickAddTab = new QComboBox(m_quickAddReview);
    m_quickAddTab->setObjectName(QStringLiteral("boardQuickAddTab"));
    choices->addWidget(m_quickAddTab);
    m_quickAddLabels = new QLineEdit(m_quickAddReview);
    m_quickAddLabels->setObjectName(QStringLiteral("boardQuickAddLabels"));
    m_quickAddLabels->setPlaceholderText(QStringLiteral("Labels, separated by commas"));
    choices->addWidget(m_quickAddLabels, 1);
    review->addLayout(choices);
    connect(m_quickAddTab, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { m_quickAddTabTouched = true; });
    connect(m_quickAddLabels, &QLineEdit::textEdited, this,
            [this] { m_quickAddLabelsTouched = true; });
    m_quickAddTriageStatus = new QLabel(m_quickAddReview);
    m_quickAddTriageStatus->setObjectName(QStringLiteral("boardQuickAddTriageStatus"));
    m_quickAddTriageStatus->hide();
    review->addWidget(m_quickAddTriageStatus);
    m_quickAddSuggestions = new QWidget(m_listPane);
    m_quickAddSuggestions->setObjectName(QStringLiteral("boardQuickAddSuggestions"));
    m_quickAddSuggestionsLayout = new QVBoxLayout(m_quickAddSuggestions);
    m_quickAddSuggestionsLayout->setContentsMargins(0, 0, 0, 0);
    auto *buttons = new QHBoxLayout;
    m_quickAddSave = new QPushButton(QStringLiteral("Create card"), m_quickAddReview);
    m_quickAddSave->setObjectName(QStringLiteral("boardQuickAddSave"));
    connect(m_quickAddSave, &QPushButton::clicked, this, [this] { saveQuickAdd(); });
    m_quickAddCancel = new QPushButton(QStringLiteral("Cancel"), m_quickAddReview);
    connect(m_quickAddCancel, &QPushButton::clicked, this, [this] { closeQuickAdd(); });
    buttons->addWidget(m_quickAddSave);
    buttons->addWidget(m_quickAddCancel);
    buttons->addStretch();
    review->addLayout(buttons);
    m_quickAddReview->hide();
    m_quickAddSuggestions->hide();
    layout->addWidget(m_quickAddSuggestions);
    layout->addWidget(m_quickAddReview);
}

void BoardView::requestQuickAddTriage(bool semantic)
{
    if (m_quickAddRow->isHidden() || !m_quickAddTriageSupported)
        return;
    const QString text = m_quickAddReview->isHidden()
                             ? m_quickAdd->text().trimmed()
                             : m_quickAddIssue->toPlainText().trimmed();
    if (text.isEmpty())
        return;
    m_quickAddTriageTimer->stop();
    m_quickAddTriageRequest = nextRequestId();
    if (semantic) {
        m_quickAddTriageStatus->setText(QStringLiteral("Checking related cards…"));
        m_quickAddTriageStatus->show();
    }
    send({{QStringLiteral("type"), QStringLiteral("board_triage")},
          {QStringLiteral("id"), m_quickAddTriageRequest},
          {QStringLiteral("title"), m_quickAdd->text().trimmed().left(200)},
          {QStringLiteral("text"), text.left(3000)},
          {QStringLiteral("semantic"), semantic}});
}

void BoardView::showQuickAddReview()
{
    if (m_quickAddReview->isHidden()) {
        m_quickAddIssue->setPlainText(m_quickAdd->text().trimmed());
        m_quickAddReview->show();
        m_quickAddIssue->setFocus();
        m_quickAddIssue->selectAll();
        renderQuickAddSuggestions();
    }
    requestQuickAddTriage(true);
}

void BoardView::renderQuickAddSuggestions()
{
    if (!m_quickAddSuggestionsLayout)
        return;
    while (QLayoutItem *item = m_quickAddSuggestionsLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const auto addGroup = [this](const QString &name, const QJsonArray &items, bool linkable) {
        if (items.isEmpty())
            return;
        m_quickAddSuggestionsLayout->addWidget(new QLabel(name, m_quickAddSuggestions));
        for (const QJsonValue &value : items) {
            const QJsonObject suggestion = value.toObject();
            const QString id = suggestion.value(QStringLiteral("id")).toString();
            if (id.isEmpty())
                continue;
            const QString title = suggestion.value(QStringLiteral("title")).toString();
            auto *row = new QWidget(m_quickAddSuggestions);
            auto *line = new QHBoxLayout(row);
            line->setContentsMargins(0, 0, 0, 0);
            if (linkable) {
                auto *check = new QCheckBox(QStringLiteral("Link"), row);
                check->setChecked(m_quickAddChosenRelated.contains(id));
                connect(check, &QCheckBox::toggled, this, [this, id](bool checked) {
                    if (checked) m_quickAddChosenRelated.insert(id);
                    else m_quickAddChosenRelated.remove(id);
                });
                line->addWidget(check);
            }
            auto *label = new QLabel(row);
            label->setText(QStringLiteral("<a href=\"card:%1\">#%1 %2</a>")
                               .arg(id.toHtmlEscaped(), title.toHtmlEscaped()));
            label->setTextInteractionFlags(Qt::TextBrowserInteraction);
            label->setOpenExternalLinks(false);
            connect(label, &QLabel::linkActivated, this, [this, id] { selectCard(id); });
            line->addWidget(label, 1);
            m_quickAddSuggestionsLayout->addWidget(row);
        }
    };
    addGroup(QStringLiteral("Possible duplicates — open and compare before creating"),
             m_quickAddDuplicates, false);
    addGroup(QStringLiteral("Related cards — check Link to include on the new card"),
             m_quickAddRelated, true);
    if (m_quickAddDuplicates.isEmpty() && m_quickAddRelated.isEmpty()
        && !m_quickAddReview->isHidden())
        m_quickAddSuggestionsLayout->addWidget(
            new QLabel(QStringLiteral("No likely matches found."), m_quickAddSuggestions));
    m_quickAddSuggestions->setVisible(!m_quickAddRow->isHidden()
        && (!m_quickAddDuplicates.isEmpty() || !m_quickAddRelated.isEmpty()
            || !m_quickAddReview->isHidden()));
}

void BoardView::saveQuickAdd()
{
    if (!m_quickAddCreateRequest.isEmpty())
        return;
    const QString title = m_quickAdd->text().trimmed();
    if (title.isEmpty())
        return;
    const QString issue = m_quickAddIssue->toPlainText().trimmed();
    const QString status = m_model.dropStatus(m_quickAddColumn);
    QJsonObject create{{QStringLiteral("type"), QStringLiteral("board_create")},
                       {QStringLiteral("tab"), m_quickAddTab->currentData().toString()},
                       {QStringLiteral("status"), status.isEmpty() ? QStringLiteral("inbox") : status},
                       {QStringLiteral("card_type"), QStringLiteral("work")},
                       {QStringLiteral("title"), title},
                       {QStringLiteral("text"), issue.isEmpty() ? title : issue}};
    if (status.isEmpty())
        create.insert(QStringLiteral("section"), m_quickAddColumn);
    QJsonArray labels;
    for (const QString &part : m_quickAddLabels->text().split(QLatin1Char(','))) {
        const QString label = part.trimmed();
        if (!label.isEmpty() && !labels.contains(label)) labels.append(label);
    }
    if (!labels.isEmpty())
        create.insert(QStringLiteral("labels"), labels);
    QJsonArray related;
    for (const QString &id : m_quickAddChosenRelated) related.append(id);
    if (!related.isEmpty())
        create.insert(QStringLiteral("related"), related);
    m_quickAddCreateRequest = nextRequestId();
    create.insert(QStringLiteral("id"), m_quickAddCreateRequest);
    m_quickAddSave->setEnabled(false);
    m_quickAddCancel->setEnabled(false);
    send(create);
}

namespace {
// How many inotify watches this pane may hold, and how many of them may go on single files
// (#N5JJ). The board's own folders come first — 11 of them on this repo's board — and the file
// watches take what is left up to their own budget, so a board with hundreds of status folders
// can never spend the whole allowance on cards.
constexpr int kMaxWatched = 200;
constexpr int kMaxWatchedFiles = 32;
}  // namespace

// Watch the board folder and its subfolders. A card write anywhere (this window, a pane agent, a
// collaborator's merge) becomes one debounced board_refresh, which the worker answers with the
// rows that actually changed.
void BoardView::watchIssues()
{
    // Which folder that is, rather than an assumption: `board/` on a board made from 2026-09-21
    // on, and an older spelling on an older one (protocol 19.12). The worker says so on the
    // `board` event (`m_root`); before the first one, ask the filesystem the same way it does.
    const QString root = m_root.isEmpty() ? projects::boardDirOf(m_workspace) : m_root;
    if (root.isEmpty() || !QFileInfo::exists(root))
        return;
    if (!m_refresh) {
        m_refresh = new QTimer(this);
        m_refresh->setSingleShot(true);
        m_refresh->setInterval(400);
        connect(m_refresh, &QTimer::timeout, this, [this] {
            if (m_open)
                send({{QStringLiteral("type"), QStringLiteral("board_refresh")}});
            watchIssues();   // folders come and go as cards move between state subfolders
        });
    }
    if (!m_watcher) {
        m_watcher = new QFileSystemWatcher(this);
        const auto touched = [this](const QString &) { m_refresh->start(); };
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, touched);
        connect(m_watcher, &QFileSystemWatcher::fileChanged, this, touched);
    }
    QStringList wanted{root};
    QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (!m_pinned && it.hasNext() && wanted.size() < kMaxWatched)
        wanted << it.next();
    const QStringList known = m_watcher->directories();
    QStringList fresh;
    for (const QString &path : wanted)
        if (!known.contains(path))
            fresh << path;
    if (!fresh.isEmpty())
        m_watcher->addPaths(fresh);
    watchCardFiles();
}

// The few card files worth a watch of their own (#N5JJ).
//
// A directory watch fires when an entry is created, renamed or removed, never when an existing
// file's content changes. Every writer Relay owns replaces its files now, which a directory watch
// does see — but a guest CLI, an editor or a script that writes in place is still invisible, and
// the two files where that matters most are the card the page is open on and its thread. The
// cards an agent is executing get one each while there is room: they are the ones being written
// while the pane is being watched.
//
// A watched file is dropped by QFileSystemWatcher the moment it is replaced, which is exactly
// what every write here does, so this re-adds what has gone — the refresh timer calls it after
// every fire, and the card page's own open and close call it directly.
void BoardView::watchCardFiles()
{
    if (!m_watcher || m_root.isEmpty())
        return;
    // A card pane (#Y2BA) watches its own card and nothing else: the tab's list Board watches the
    // tree, and the tab's worker sends every view of the tab what changed.
    const bool pinnedOnly = m_pinned;
    const QString threads = m_root + QStringLiteral("/threads/");
    const QString privateThreads = m_root + QStringLiteral("/.private/threads/");
    QStringList wanted;
    const auto want = [&](const QString &id) {
        const board::Card *card = m_model.card(id);
        if (!card || wanted.size() >= kMaxWatchedFiles)
            return;
        if (!card->path.isEmpty())
            wanted << m_workspace + QLatin1Char('/') + card->path;
        wanted << (card->isPrivate ? privateThreads : threads) + id + QStringLiteral(".md");
    };
    if (detailOpen())
        want(m_detail->cardId());
    // Then whatever is being worked on, while the budget lasts. Ordered by id so the set is
    // stable between calls and the watcher is not churned for nothing.
    QStringList executing;
    for (const QString &id : pinnedOnly ? QStringList() : m_model.allIds())
        if (const board::Card *card = m_model.card(id);
            card && card->status == QStringLiteral("in-progress"))
            executing << id;
    executing.sort();
    for (const QString &id : executing)
        want(id);

    const QStringList known = m_watcher->files();
    QStringList fresh, stale;
    for (const QString &path : wanted)
        if (!known.contains(path) && QFileInfo::exists(path))
            fresh << path;
    for (const QString &path : known)
        if (!wanted.contains(path))
            stale << path;
    if (!stale.isEmpty())
        m_watcher->removePaths(stale);
    if (!fresh.isEmpty())
        m_watcher->addPaths(fresh);
}

// The filter bar's plain words, asked of the worker (#7M6E, protocol 19.2 `board_search`).
//
// Each card's whole body and thread used to ride on every row for one substring test here: it
// was 92.6 % of the `board` event's bytes, it overflowed the worker pipe's read buffer past
// about 1,160 cards — the pane then said "Loading the Switchboard…" for ever — and every
// keystroke scanned 2.2 MB on the GUI thread (30–80 ms). The worker already holds the text, so
// the words go to it and only the matching ids come back.
//
// Debounced rather than sent per key, so a burst of typing is one question, and only the newest
// request is believed: an answer for anything else is a superseded search and is dropped. The
// scoped terms (`status:`, `label:`, `@`, …) never leave this side, so they still cost nothing.
void BoardView::startSearch()
{
    if (!m_searchTimer) {
        m_searchTimer = new QTimer(this);
        m_searchTimer->setSingleShot(true);
        m_searchTimer->setInterval(120);
        connect(m_searchTimer, &QTimer::timeout, this, [this] { sendSearch(); });
    }
    if (board::Model::plainTerms(m_filter->text()).isEmpty()) {
        // Nothing for the worker to answer: an empty box, or a filter that is all scoped terms.
        m_searchTimer->stop();
        m_searchRequest.clear();
        m_searchAsked.clear();
        return;
    }
    m_searchTimer->start();
}

void BoardView::sendSearch()
{
    const QString terms = board::Model::plainTerms(m_filter->text()).join(QLatin1Char(' '));
    if (terms.isEmpty() || !m_searchSupported)
        return;
    // Asked again for the same words when the cards change, which is why the answer is not
    // cached here: a card file edited under a live filter has to join or leave the list.
    m_searchAsked = terms;
    m_searchRequest = nextRequestId();
    send({{QStringLiteral("id"), m_searchRequest},
          {QStringLiteral("type"), QStringLiteral("board_search")},
          {QStringLiteral("query"), terms}});
}

QString BoardView::nextRequestId()
{
    return m_requestPrefix + QString::number(++m_requestSeq);
}

// The card page's reverse links (#EE42, protocol 19.25): asked after every card read, since a
// re-read is what follows another card starting to mention this one. The answer lands in
// `handleEvent`; a worker too old to know the request refuses it and the page draws no
// "Linked from" block, which is what it drew before.
void BoardView::requestCardLinks(const QString &cardId)
{
    if (cardId.isEmpty())
        return;
    m_linksRequest = nextRequestId();
    send({{QStringLiteral("type"), QStringLiteral("board_links")},
          {QStringLiteral("id"), m_linksRequest},
          {QStringLiteral("address"), QStringLiteral("#") + cardId}});
}

void BoardView::send(QJsonObject message)
{
    if (!message.contains(QStringLiteral("id")))
        message.insert(QStringLiteral("id"), nextRequestId());
    // Every move is remembered against its request id, because the only thing that can answer a
    // Check-gate refusal (#7BM4) is the move that was refused: the notice's Override… re-sends
    // this exact message with a reason on it. Bounded, and the four senders — the picker, a
    // drag, a tab change and Execute — all come through here, so none of them has to remember
    // anything itself.
    if (message.value(QStringLiteral("type")).toString() == QStringLiteral("board_move")) {
        if (m_pendingMoves.size() >= 32)
            m_pendingMoves.clear();
        m_pendingMoves.insert(message.value(QStringLiteral("id")).toString(), message);
    }
    if (onSend)
        onSend(message);
}

// Override… on the gate notice: one line of reason, then the same move again with `override` on
// it. An empty answer (or Cancel) leaves the card where it is — the gate's whole point is that
// getting past it is deliberate and recorded.
void BoardView::overrideGatedMove()
{
    if (m_gatedMove.isEmpty())
        return;
    QJsonObject move = m_gatedMove;
    m_gatedMove = QJsonObject();
    bool answered = false;
    const QString card = move.value(QStringLiteral("card")).toString();
    const QString reason = QInputDialog::getText(
            this, QStringLiteral("Override the tests check"),
            QStringLiteral("Why is #%1 ready to move although its tests do not prove it?\n"
                           "The reason is quoted on the card's thread.").arg(card),
            QLineEdit::Normal, QString(), &answered).trimmed();
    if (!answered || reason.isEmpty()) {
        m_notice->hide();
        return;
    }
    move.remove(QStringLiteral("id"));                  // a fresh id: this is a second request
    move.insert(QStringLiteral("override"), reason);
    const QString id = nextRequestId();
    move.insert(QStringLiteral("id"), id);
    m_pendingNotes.insert(id, QStringLiteral("Moved #%1 with the tests check overridden")
                                  .arg(card));
    send(move);
}

void BoardView::reload()
{
    send({{QStringLiteral("type"), QStringLiteral("board_open")}});
}

// The words in the pane's own empty area, and whether a Retry sits under them. Only a worker
// failure gets the button (#7M6E); "Loading…" and "No cards yet." are states, not faults.
void BoardView::setEmptyText(const QString &text, bool retry)
{
    m_empty->setText(text);
    // The rings belong to a board with no cards, not to a failure: an error with a row of unlit
    // jacks under it reads as decoration over bad news.
    static_cast<EmptyBoard *>(m_empty)->setSections(retry ? QStringList() : m_emptySections);
    m_emptyRetryRow->setVisible(retry && m_empty->isVisible());
}

// "Switchboard · 84 open": the number the pane is actually about, not every card ever filed.
QString BoardView::title() const
{
    // A solo card pane (#Y2BA) is named by its card, so two of them in a tab tell apart.
    if (m_pinned) {
        const board::Card *card = m_model.card(m_pinnedCard);
        return card ? QStringLiteral("#%1 %2").arg(m_pinnedCard, card->title)
                    : QStringLiteral("#%1").arg(m_pinnedCard);
    }
    if (m_model.total() == 0)
        return QStringLiteral("Board");
    return QStringLiteral("Board · %1 open").arg(m_model.openCount());
}

void BoardView::setHeaderRightInset(int pixels)
{
    m_rightInset = pixels;
    applyRightInset();
}

QString BoardView::notice() const
{
    return m_notice->isHidden() ? QString() : m_noticeText->text();
}

void BoardView::showNotice(const QString &text, bool error, const QString &undoWriteId,
                           bool canOverride)
{
    m_noticeText->setText(text);
    m_notice->setProperty("error", error);
    m_notice->style()->unpolish(m_notice);
    m_notice->style()->polish(m_notice);
    m_noticeUndo->setVisible(!undoWriteId.isEmpty());
    m_noticeOverride->setVisible(canOverride);
    m_notice->show();
    placeNotice();
    m_noticeTimer->start(error ? 12000 : 10000);
}

void BoardView::placeNotice()
{
    if (m_notice->isHidden())
        return;
    const int width = qMin(560, this->width() - 24);
    m_notice->setFixedWidth(qMax(120, width));
    // The wrapped text's real height at this width, not adjustSize()'s guess at a narrower one.
    QLayout *layout = m_notice->layout();
    m_notice->setFixedHeight(layout->hasHeightForWidth() ? layout->totalHeightForWidth(m_notice->width())
                                                         : layout->totalSizeHint().height());
    const int x = (this->width() - m_notice->width()) / 2;
    // From the size hint, not the geometry: on a resize the layout has not placed m_keys yet.
    int bottom = height() - (m_keys->isVisible() ? m_keys->sizeHint().height() : 0) - 10;
    // A card with the pane to itself has its reply box and its Ask button along that bottom, and a
    // cleanup's progress line stays up for minutes rather than ten seconds: it goes above them.
    if (detailOpen() && m_listPane->isHidden())
        bottom -= m_detail->controlsHeight();
    m_notice->move(x, bottom - m_notice->height());
    m_notice->raise();
}

// A hash copy says so the way a terminal pane's copy-on-highlight does (#Y2F4): the same small
// fading popup, bottom-right, while the notice line above keeps the board's own wording. Built on
// first use, wearing the `toast` object name the theme already styles for a pane's popup.
void BoardView::toast(const QString &text, int milliseconds)
{
    if (text.isEmpty())
        return;
    if (!m_toast) {
        m_toast = new QLabel(this);
        m_toast->setObjectName(QStringLiteral("toast"));
        m_toast->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_toast->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_toastTimer = new QTimer(this);
        m_toastTimer->setSingleShot(true);
        connect(m_toastTimer, &QTimer::timeout, m_toast, &QWidget::hide);
    }
    m_toast->setText(text);
    m_toast->adjustSize();
    // Shown before it is placed: a widget that has never been shown is hidden, and `placeToast`
    // asks whether it is up, so placing first leaves the popup at the widget's own top-left.
    m_toast->show();
    placeToast();
    m_toast->raise();
    m_toastTimer->start(milliseconds);
}

// The bottom-right corner, kept clear of the keys row the notice also sits above.
void BoardView::placeToast()
{
    if (!m_toast)
        return;
    const int bottom = height() - (m_keys->isVisible() ? m_keys->sizeHint().height() : 0) - 10;
    m_toast->move(width() - m_toast->width() - 16, bottom - m_toast->height());
    m_toast->raise();
}

// ------------------------------------------------------------------------- viewed (#FKSN)

namespace {

// How many cards a board remembers opening. The column answers "what was I just reading", so the
// oldest stamps are dropped past this rather than letting the settings file grow for ever.
constexpr int kViewedKept = 500;

// One QSettings entry per board: its root folder — the one the worker named, else the project's
// board folder, else the project itself, so two boards never share one — hashed, because a path's
// slashes would read as settings groups.
QString viewedSettingsKey(const QString &root, const QString &workspace)
{
    QString folder = root.isEmpty() ? projects::boardDirOf(workspace) : root;
    if (folder.isEmpty())
        folder = workspace;
    const QByteArray hash = QCryptographicHash::hash(QDir::cleanPath(folder).toUtf8(),
                                                     QCryptographicHash::Sha1);
    return QStringLiteral("board/viewed/") + QString::fromLatin1(hash.toHex().left(16));
}

// The open views, so a card opened in a pane of its own (Ctrl+click, a `#ID` link) is stamped on
// the list it was opened from too. QPointer: a closed pane drops out by itself.
QList<QPointer<BoardView>> &viewedViews()
{
    static QList<QPointer<BoardView>> views;
    return views;
}

}  // namespace

void BoardView::loadViewedStamps()
{
    QHash<QString, QString> stamps;
    const QVariantMap saved = QSettings().value(viewedSettingsKey(m_root, m_workspace)).toMap();
    for (auto it = saved.constBegin(); it != saved.constEnd(); ++it)
        stamps.insert(it.key(), it.value().toString());
    m_model.setViewedStamps(stamps);
    QList<QPointer<BoardView>> &views = viewedViews();
    views.removeAll(nullptr);
    if (!views.contains(this))
        views.append(this);
}

void BoardView::stampViewed(const QString &id)
{
    if (id.isEmpty())
        return;
    const QString key = viewedSettingsKey(m_root, m_workspace);
    const QString stamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QSettings settings;
    QVariantMap saved = settings.value(key).toMap();
    saved.insert(id.toUpper(), stamp);
    if (saved.size() > kViewedKept) {
        QList<QPair<QString, QString>> byTime;   // stamp, id — oldest first once sorted
        for (auto it = saved.constBegin(); it != saved.constEnd(); ++it)
            byTime.append({it.value().toString(), it.key()});
        std::sort(byTime.begin(), byTime.end());
        for (int i = 0; i < byTime.size() - kViewedKept; ++i)
            saved.remove(byTime.at(i).second);
    }
    settings.setValue(key, saved);
    // Every open view of this board takes the stamp: the one the card opened in, and the list a
    // Ctrl+click opened it from. Only a view sorted by it has rows to move; the rest repaint.
    for (const QPointer<BoardView> &view : viewedViews()) {
        if (view.isNull() || (view != this && !view->m_model.card(id)))
            continue;
        if (view != this && viewedSettingsKey(view->m_root, view->m_workspace) != key)
            continue;
        view->m_model.setViewed(id, stamp);
        const board::Sort sort = view->m_model.sort();
        if (sort == board::Sort::RecentlyViewed || sort == board::Sort::OldestViewed)
            view->rebuild();
        else if (view->m_list != nullptr)
            view->m_list->viewport()->update();
    }
}

// ------------------------------------------------------------------------- events

void BoardView::handleEvent(const QJsonObject &event)
{
    const QString type = event.value(QStringLiteral("event")).toString();
    if (type == QStringLiteral("tree_status")) {
        const QJsonObject tree = event.value(QStringLiteral("tree_status")).isObject()
            ? event.value(QStringLiteral("tree_status")).toObject() : event;
        if (QDir::cleanPath(tree.value(QStringLiteral("project_root")).toString())
            == QDir::cleanPath(m_workspace) && tree.value(QStringLiteral("state")).toString() == QStringLiteral("active")) {
            m_repoId = tree.value(QStringLiteral("repo_id")).toString();
            m_queueMode = !m_repoId.isEmpty();
            syncLivePage();
        }
        return;
    }
    // Queue notifications are hints for this registered project; the periodic CLI read below
    // remains the recovery path after a missed event or a restarted publisher.
    if ((type == QStringLiteral("queue_status") || type == QStringLiteral("main_moved"))
        && !m_repoId.isEmpty() && event.value(QStringLiteral("repo_id")).toString() == m_repoId) {
        if (type == QStringLiteral("queue_status")) {
            m_queueJobs = event.value(QStringLiteral("jobs")).toArray();
            if (event.value(QStringLiteral("workspaces")).isArray())
                m_retainedTrees = event.value(QStringLiteral("workspaces")).toArray();
            if (event.value(QStringLiteral("main_release")).isObject())
                m_mainRelease = event.value(QStringLiteral("main_release")).toObject();
        } else {
            m_mainMoved = QStringLiteral("main moved %1 → %2")
                .arg(event.value(QStringLiteral("previous_sha")).toString().left(10),
                     event.value(QStringLiteral("sha")).toString().left(10));
            m_integrationPollAge.invalidate();
        }
        syncLivePage();
        return;
    }
    const QString requestId = event.value(QStringLiteral("id")).toString();
    const bool mine = !requestId.isEmpty() && requestId.startsWith(m_requestPrefix);
    // Defence in depth for the per-project Switchboard: every board event carries the `issues`
    // directory it came from. The window already routes each worker's events to the views of its
    // own board root, but a view that has learned its root will not take another project's cards
    // even if that routing is ever wrong — a wrong reset here silently repoints the view, and the
    // next drag or quick add writes into the other repository. Events with no `root` (an older
    // worker) are handled exactly as before.
    const QString root = event.value(QStringLiteral("root")).toString();
    // The board's folder was renamed at this pane's request (19.17): the event names the *new*
    // root, so it comes before the guard below, which would otherwise read it as another
    // project's. From here the worker speaks for the new folder; the watcher and the rows follow.
    if (type == QStringLiteral("board_folder_changed") && mine) {
        if (!root.isEmpty())
            m_root = root;
        showNotice(event.value(QStringLiteral("summary")).toString(), false);
        watchIssues();
        reload();
        return;
    }
    if (!root.isEmpty()) {
        if (!m_root.isEmpty() && root != m_root)
            return;
        if (m_root.isEmpty() && type == QStringLiteral("board"))
            m_root = root;
    }
    // A signal thread started or ended (#AQ6X phase 3, §32.4): Relay picked up a failing check
    // nobody was on. It is taken *before* the state event below, because the same
    // `signals_changed` carries the worker's list of running threads and the chip drawn in the
    // rebuild that follows is asked whether that thread is live.
    if (m_signalThreads.take(type, event) && type == QStringLiteral("signal_thread")) {
        announceSignalThread(m_signalThreads.lastEvent());
        rebuild();                        // the chip on its claim is live now, or stopped being
        return;
    }
    // Signals (#AQ6X, protocol §32.1): the worker folds its record and pushes the whole state
    // after every change, and answers `signals_list` with the same shape. It carries no card id
    // and belongs to no thread, so it is taken here, before all of the card traffic.
    if (m_signalsState.take(type, event)) {
        // A page open on a key the board no longer has is closed: nothing stays up on a signal
        // that has been resolved, removed or dismissed out of the list.
        if (signalOpen()) {
            if (const board::Signal *signal = m_signalsState.signalFor(m_signalDetail->key()))
                m_signalDetail->showSignal(*signal);
            else
                closeSignal();
        }
        rebuild();
        // A promotion is one of the two things R12 lets reach a human unasked: the card is in the
        // inbox, and the notice says which signal opened it. Only the ones that were not promoted
        // a moment ago — the event carries every promoted signal on every change, so `promoted()`
        // would announce the same card again whenever any signal anywhere moved.
        for (const board::Signal &promoted : m_signalsState.newlyPromoted()) {
            if (promoted.card.isEmpty())
                continue;
            showNotice(QStringLiteral("#%1 opened from the signal %2")
                               .arg(promoted.card, promoted.key), false);
            break;
        }
        return;
    }
    // A `signals_*` write this pane asked for went through (§32.2). The state event that follows
    // redraws the row and the page; this is only the line that says it happened.
    if (type == QStringLiteral("signals_written")) {
        const QString kind = event.value(QStringLiteral("kind")).toString();
        const QString key = event.value(QStringLiteral("key")).toString();
        const QString card = event.value(QStringLiteral("card")).toString();
        const QString failed = event.value(QStringLiteral("error")).toString();
        m_signalRequests.remove(requestId);
        // A refusal comes back as the write's own event with `error` and `code` on it, which is
        // how the worker answers (`tests_protocol._signals_write`): the page that pressed the
        // action says so on its error line, exactly as it does for the `error` event below.
        if (!failed.isEmpty()) {
            if (signalOpen() && (key.isEmpty() || m_signalDetail->key() == key))
                m_signalDetail->showError(failed);
            else
                showNotice(failed, true);
            return;
        }
        if (mine || m_signalRequests.isEmpty()) {
            const QString what = kind == QStringLiteral("claim") ? QStringLiteral("Claimed %1")
                                 : kind == QStringLiteral("release") ? QStringLiteral("Released %1")
                                 : kind == QStringLiteral("dismiss") ? QStringLiteral("Dismissed %1")
                                 : kind == QStringLiteral("promote")
                                         ? QStringLiteral("Promoted %1")
                                         : QStringLiteral("Wrote %1");
            showNotice(card.isEmpty() ? what.arg(key)
                                      : what.arg(key) + QStringLiteral(" · #%1").arg(card), false);
        }
        return;
    }
    // A `signals_*` write this pane asked for and the worker refused (§32.2). It goes on the page
    // the action was pressed on, the way a card's refusal goes under the card, and never through
    // the card paths below: this error names a signal, not a card, and no thread is waiting on it.
    // `board_claimed_elsewhere` carries the holder (#R9G7), and the page names them rather than
    // saying only "no".
    if (type == QStringLiteral("error") && mine && m_signalRequests.contains(requestId)) {
        const QString key = m_signalRequests.take(requestId);
        const QString code = event.value(QStringLiteral("code")).toString();
        const QString holder = event.value(QStringLiteral("session")).toString();
        QString what = event.value(QStringLiteral("text")).toString();
        if (code == QStringLiteral("board_claimed_elsewhere"))
            what = holder.isEmpty()
                           ? QStringLiteral("Another session is working on this signal.")
                           : QStringLiteral("%1 is working on this signal. Ask them, or take "
                                            "another one.").arg(board::sessionChip(holder, true));
        if (signalOpen() && m_signalDetail->key() == key)
            m_signalDetail->showError(what);
        else
            showNotice(what, true);
        return;
    }
    // The worker said something about itself: it died, its pipe overflowed, it would not start.
    // Until 2026-09-20 this only ever reached `statusBar()->showMessage`, which this layout does
    // not show, so a Switchboard that never loaded said "Loading the Switchboard…" for ever
    // (#7M6E). A board that *is* on screen keeps its cards and gets the ordinary error notice.
    if (type == QStringLiteral("board_worker_status")) {
        const QString text = event.value(QStringLiteral("text")).toString();
        if (text.isEmpty())
            return;
        if (m_open) {
            showNotice(text, true);
        } else {
            m_workerError = text;
            rebuild();
        }
        return;
    }
    // The rest of a chunked `board_open`, or of a `board_changed` too big for one message
    // (#7M6E): rows to patch in, exactly as an upsert is.
    if (type == QStringLiteral("board_cards")) {
        const QJsonArray cards = event.value(QStringLiteral("cards")).toArray();
        m_model.upsert(cards);
        // Redraw when the last batch lands, not on each: the first batch is already on screen,
        // and a refill per batch would cost a full list rebuild for every 400 cards.
        if (!event.value(QStringLiteral("more")).toBool()) {
            rebuild();
            if (onTitleChanged)
                onTitleChanged(title());
        }
        return;
    }
    if (type == QStringLiteral("board_triage") && mine) {
        if (requestId != m_quickAddTriageRequest || m_quickAddRow->isHidden())
            return;
        const QString phase = event.value(QStringLiteral("phase")).toString();
        if (phase == QStringLiteral("local")) {
            m_quickAddDuplicates = event.value(QStringLiteral("duplicates")).toArray();
            m_quickAddRelated = event.value(QStringLiteral("related")).toArray();
        } else if (phase == QStringLiteral("semantic")) {
            m_quickAddTriageStatus->clear();
            m_quickAddTriageStatus->hide();
            const auto addIds = [this](const QJsonArray &ids, QJsonArray &target) {
                for (const QJsonValue &value : ids) {
                    const QString id = value.toString();
                    const board::Card *card = m_model.card(id);
                    if (!card || card->closed()) continue;
                    bool known = false;
                    for (const QJsonValue &existing : target)
                        known |= existing.toObject().value(QStringLiteral("id")).toString() == id;
                    if (!known)
                        target.append(QJsonObject{{QStringLiteral("id"), id},
                                                  {QStringLiteral("title"), card->title}});
                }
            };
            addIds(event.value(QStringLiteral("duplicates")).toArray(), m_quickAddDuplicates);
            QSet<QString> duplicateIds;
            for (const QJsonValue &value : m_quickAddDuplicates)
                duplicateIds.insert(value.toObject().value(QStringLiteral("id")).toString());
            QJsonArray keptRelated;
            for (const QJsonValue &value : m_quickAddRelated) {
                const QString id = value.toObject().value(QStringLiteral("id")).toString();
                if (!duplicateIds.contains(id)) keptRelated.append(value);
                else m_quickAddChosenRelated.remove(id);
            }
            m_quickAddRelated = keptRelated;
            for (const QJsonValue &value : event.value(QStringLiteral("related")).toArray())
                if (!duplicateIds.contains(value.toString()))
                    addIds(QJsonArray{value}, m_quickAddRelated);
            const QString tab = event.value(QStringLiteral("tab")).toString();
            if (!m_quickAddTabTouched && !tab.isEmpty()) {
                const int index = m_quickAddTab->findData(tab);
                if (index >= 0) {
                    QSignalBlocker block(m_quickAddTab);
                    m_quickAddTab->setCurrentIndex(index);
                }
            }
            if (!m_quickAddLabelsTouched) {
                QStringList labels;
                for (const QJsonValue &value : event.value(QStringLiteral("labels")).toArray())
                    if (!value.toString().isEmpty()) labels << value.toString();
                if (!labels.isEmpty()) m_quickAddLabels->setText(labels.join(QStringLiteral(", ")));
            }
        }
        renderQuickAddSuggestions();
        return;
    }
    // The worker's answer about the filter's plain words (#7M6E). Only the newest question is
    // believed: anything else is a search the box has already moved past.
    if (type == QStringLiteral("board_search") && mine) {
        if (requestId != m_searchRequest)
            return;
        QSet<QString> ids;
        const QJsonArray found = event.value(QStringLiteral("ids")).toArray();
        ids.reserve(found.size());
        for (const QJsonValue &value : found)
            ids.insert(value.toString());
        m_model.setSearchResult(event.value(QStringLiteral("query")).toString(), ids);
        rebuild();
        return;
    }
    // The reverse side of the Linked panels (#EE42): the card page's own question is answered
    // here, the skill page's by the registry view. Only the newest card question is believed.
    if (type == QStringLiteral("board_links") && mine) {
        if (requestId == m_linksRequest) {
            m_linksRequest.clear();
            const QJsonArray items = event.value(QStringLiteral("items")).toArray();
            const QJsonObject item = items.isEmpty() ? QJsonObject() : items.first().toObject();
            const QString address = item.value(QStringLiteral("address")).toString();
            if (address.startsWith(QLatin1Char('#')))
                m_detail->setBacklinks(address.mid(1), item.value(QStringLiteral("reverse")).toArray());
            return;
        }
        if (m_skillsPage != nullptr)
            m_skillsPage->handleEvent(event);
        return;
    }
    if (type.startsWith(QStringLiteral("skills")) || type == QStringLiteral("error")) {
        // The Skills tab's rows and the replies to its actions (#9FX8): every skill visible from
        // this workspace, filtered to the project ones by the view (decision 2); Globals' Skills
        // section reads the same rows for the global ones. An `error` the view did not ask for
        // falls through to the handlers below.
        if (m_skillsPage != nullptr && m_skillsPage->handleEvent(event))
            return;
        if (type != QStringLiteral("error"))
            return;
    }
    if (type == QStringLiteral("board")) {
        m_open = true;
        m_directCard = false;
        m_workerError.clear();
        // The conversation itself rides on the console's own worker connection now (protocol
        // 33): a pane opened while the agent is half way through an answer catches up from the
        // `configured` and the turn events the console is sent, not from this block.
        m_config = event.value(QStringLiteral("config")).toObject();
        m_model.setConfig(m_config);
        loadViewedStamps();   // before the rows, so every card arrives with its Viewed stamp
        m_model.reset(event.value(QStringLiteral("cards")).toArray());
        m_pendingDeletes.clear();
        const QJsonObject navigation = m_restoreNavigation;
        const bool hadFocus = hasFocus();   // opened with its key (board.open) before the cards arrived
        rebuild();
        // A pane reopened with words already in its filter asks about them again: the answer it
        // holds is about the cards of a moment ago (#7M6E).
        startSearch();
        // And what the machine has open against this board (#AQ6X, §32.2): the worker pushes
        // `signals_changed` after every change, but a pane that has just opened has missed every
        // change so far, so it asks once. A worker too old to know the message refuses it, and the
        // refusal is a `signals_written` with no key — which draws nothing.
        //
        // Asked when the pane is being looked at, the way `catchUp()` is: a board in a background
        // tab has nobody to draw the rows for, and a view that is never shown at all — every test
        // that opens a board and then asserts "nothing was written" — asks nothing.
        askSignalsOnce();
        if (hadFocus)
            focusInput();
        showProblems(event.value(QStringLiteral("problems")).toArray());
        if (!navigation.isEmpty())
            restoreNavigation(navigation);
        watchCardFiles();   // which cards are executing is only known once the rows are in (#N5JJ)
        if (onTitleChanged)
            onTitleChanged(title());
        return;
    }
    if (type == QStringLiteral("board_changed")) {
        const QJsonArray upserts = event.value(QStringLiteral("upserts")).toArray();
        const QJsonArray gone = event.value(QStringLiteral("removed")).toArray();
        showProblems(event.value(QStringLiteral("problems")).toArray());
        // The config travels with every change, not only with the full `board` event: the gear
        // rewrites the section list without touching a card, so the sections would otherwise not
        // move until the pane was reopened. Before the early return below for the same reason.
        if (event.contains(QStringLiteral("config"))) {
            const QJsonObject config = event.value(QStringLiteral("config")).toObject();
            if (config != m_config) {
                m_config = config;
                m_model.setConfig(config);
                if (m_sectionsOpen)
                    m_sections->refresh(m_model);
                rebuild();
            }
        }
        // The watcher's refresh after our own write finds nothing new; do not redraw for it.
        if (upserts.isEmpty() && gone.isEmpty())
            return;
        m_model.upsert(upserts);
        QStringList removed;
        for (const QJsonValue &value : gone)
            removed << value.toString();
        m_model.remove(removed);
        rebuild();
        // The cards moved under a live filter, so the worker is asked about its words again:
        // a card whose body now holds them has to join the list, and one whose body no longer
        // does has to leave it (#7M6E). Debounced, so a storm of writes is one search.
        startSearch();
        watchCardFiles();   // a card that started or stopped executing changes what is watched
        if (onTitleChanged)
            onTitleChanged(title());
        // A card that is open stays in step with its file; other cards' changes leave it alone.
        const QString open = m_detail->cardId();
        if (!open.isEmpty() && detailOpen() && removed.contains(open)) {
            closeDetail();
            // This pane's own delete already put up its "Deleted #ID · Undo" toast (#CYM9); the
            // removal events that follow it must not talk over that. Someone else's delete —
            // another pane, an agent, a collaborator's pull — still says what happened.
            if (!deletedHere(open))
                showNotice(QStringLiteral("#%1 was removed from the board.").arg(open), true);
        }
        if (!open.isEmpty() && detailOpen()) {
            for (const QJsonValue &value : upserts)
                if (value.toObject().value(QStringLiteral("id")).toString() == open) {
                    send({{QStringLiteral("type"), QStringLiteral("board_card_get")},
                          {QStringLiteral("card"), open}});
                    break;
                }
        }
        // A card this pane deleted coming back (Undo, or a re-create) ends its marker: the next
        // removal of that id is news again, not the echo of our own write.
        for (const QJsonValue &value : upserts) {
            const QString back = value.toObject().value(QStringLiteral("id")).toString();
            if (!back.isEmpty())
                forgetDeleted(back);
        }
        return;
    }
    // The answer to one pane's own `board_card_get`: the worker echoes the requester's id, but the
    // window fans every event out to every board pane of the workspace, so without this check a
    // card opened in one tab opens in them all (#TTYB). Only the card *details* are scoped — the
    // broadcasts above (`board`, `board_changed`) still reach every pane, so the rows move together.
    if (type == QStringLiteral("board_card") && mine) {
        if (requestId == m_directCardRequest)
            m_directCardRequest.clear();
        // A #CODE reveal can be the first request this pane makes. Show its document without
        // waiting for board_open to parse and transmit every row.
        if (!m_open) {
            m_open = true;
            m_directCard = true;
            m_config = event.value(QStringLiteral("config")).toObject();
            m_model.setConfig(m_config);
            m_empty->hide();
            m_emptyRetryRow->hide();
            m_splitter->show();
            m_keys->show();
        }
        QStringList statuses;
        const QList<board::Column> sections = m_model.sections();
        for (const board::Column &column : sections)
            for (const QString &status : column.statuses)
                if (!statuses.contains(status))
                    statuses << status;
        const QString current = event.value(QStringLiteral("status")).toString();
        if (!current.isEmpty() && !statuses.contains(current))
            statuses.prepend(current);
        QList<QPair<QString, QString>> tabs;
        for (const board::Tab &item : m_model.tabs())
            if (!item.folder.isEmpty())
                tabs << qMakePair(item.id, item.title);
        m_detail->setChoices(statuses, tabs);
        // A card and a signal never share the page (#AQ6X): the card takes it.
        m_signalDetail->hide();
        if (!m_directCard)
            ensureCardConsole();
        // One console, several cards (card #CTRN). The page keeps **one** console and points it
        // at whatever card is open — the conversation, the routing and the persist key already
        // follow the card (`CardContext::spec`) — but the emulator did not, so switching cards
        // left the card before's turn in the transcript under this card's title. The console is
        // told whose transcript it is drawing: the one it was drawing is put away under its own
        // surface and this card's, if it has been here before, is drawn again.
        // Only when the card actually changed: a card is re-read whenever its file changes
        // under it (#N5JJ), and that must not wipe what is on screen. `wasShowing` is read
        // before the `show()` below overwrites the page's card id.
        const QString showing = event.value(QStringLiteral("card_id")).toString();
        const QString wasShowing = m_detail->cardId();
        if (m_cardConsoleHandle.clearTranscript && wasShowing != showing)
            m_cardConsoleHandle.clearTranscript(QStringLiteral("card:") + showing);
        m_detail->show(event);
        requestCardLinks(showing);
        refreshContexts();   // switch the console's draft key to the card just opened
        watchCardFiles();   // the open card and its thread get a watch each (#N5JJ)
        // Resume card (#FYEY): ask `land.py orphans` when a card page opens — and again when a
        // different card opens here — so the action row knows this card's dead sessions while
        // the page is up.
        if (wasShowing != showing || !detailOpen())
            m_detail->refreshOrphans();
        // Turns run per card (19.16), so the card you open may already be working: give it back
        // its strip. What it has said so far is in that card's own console and was never lost —
        // one conversation per card, and the console for this card is the one below.
        if (const QString id = event.value(QStringLiteral("card_id")).toString();
            m_cardTurns.contains(id)) {
            m_detail->setBusy(true, m_cardTurns.value(id).mode);
            // The console's own "Relaying · …" line too (card #6YS5): the turn's agent_started
            // was routed to this card's surface, which this console holds only if it was already
            // on this card when the turn began — arriving mid-turn, it never saw the start.
            if (m_cardConsoleHandle.turnRunning) m_cardConsoleHandle.turnRunning();
        } else {
            m_detail->setBusy(false);
        }
        const bool wasOpen = detailOpen();
        m_detail->setVisible(true);
        if (onNavigationChanged) onNavigationChanged();
        if (!wasOpen)
            m_detailSized = false;
        updateDetailLayout();
        // Building the console can start the tab's helper worker. Let Qt paint the card first;
        // the reply box arrives on the next event-loop pass, after the document is visible.
        if (m_directCard)
            QTimer::singleShot(20, this, [this] {
                if (detailOpen()) ensureCardConsole();
            });
        if (m_editOnOpen) {
            m_editOnOpen = false;
            const bool fresh = m_editOnOpenFresh;
            m_editOnOpenFresh = false;
            m_detail->beginEdit(false, fresh);
        } else if (!m_actionOnOpen.isEmpty()) {
            const QString action = m_actionOnOpen;
            m_actionOnOpen.clear();
            m_detail->focusDocument();
            cardAction(action);
        } else if (m_replyOnOpen) {
            m_replyOnOpen = false;
            m_detail->focusReply();
        } else if (m_detail->editing()) {
            // A re-read while the card is being edited (its file changed, or a save was refused)
            // leaves the fields, and the focus, exactly where they were.
        } else if (wasShowing != showing && m_listPane->isHidden()
                   && !m_detail->isAncestorOf(QApplication::focusWidget())) {
            // Only a card newly opened in this pane takes the focus: the list it came from is
            // hidden in a narrow pane, so the reader is brought to the page. A re-read of the
            // card already open (its file changed under it) must not touch the focus at all —
            // the reader is typing in another pane, and updates arriving here used to pull the
            // pane away from them.
            m_detail->focusDocument();
        }
        return;
    }
    if (type == QStringLiteral("board_thread_appended")) {
        if (event.value(QStringLiteral("card_id")).toString() == m_detail->cardId())
            m_detail->appendEntry(event);
        return;
    }
    // The Tests strip's two answers (#7BM4, protocol 31.2). Routed by the *card*, not by the
    // request id: a check the pane's own agent ran on the open card is about the card on screen
    // and belongs under it, and a check about another card is not this strip's news.
    if (type == QStringLiteral("tests_check") || type == QStringLiteral("tests_suggest")) {
        if (!detailOpen()
            || event.value(QStringLiteral("card")).toString() != m_detail->cardId())
            return;
        if (type == QStringLiteral("tests_check"))
            m_detail->showCheck(event);
        else
            m_detail->showSuggest(event);
        return;
    }
    // A run the strip's "Run these" started: the Test suites pane is where a run is watched, so
    // here only its ending is worth a line — and only when this pane asked for it.
    if (type == QStringLiteral("tests_run") && mine) {
        const QString state = event.value(QStringLiteral("state")).toString();
        const QString message = event.value(QStringLiteral("message")).toString();
        if ((state == QStringLiteral("finished") || state == QStringLiteral("error"))
            && !message.isEmpty())
            showNotice(message, state == QStringLiteral("error"));
        return;
    }
    // The survey (19.18): a fresh board's offer to import what the project already has. It was
    // the helper panel's until card #AGNT step 6; it is a board widget above the console now, so
    // this is where it arrives. The agent narrates the same data in its opening turn, which it
    // runs off `board_open` and which reaches the console through its own worker connection.
    if (type == QStringLiteral("board_survey")) {
        showSurvey(event);
        return;
    }
    // The survey's "Look for issues on GitHub" (19.14, #GDQN). Told from any other answer by the
    // request id it carries, both ways round: it writes to neither side, so its failures are
    // ordinary `error`s and only the one we asked for is ours.
    if (type == QStringLiteral("forge_sync_planned") && !m_forgeRequest.isEmpty()
        && requestId == m_forgeRequest) {
        showForgePlan(event);
        return;
    }
    if (type == QStringLiteral("error") && !m_forgeRequest.isEmpty()
        && requestId == m_forgeRequest) {
        showForgeError(event);
        return;
    }
    if (type == QStringLiteral("board_problems")) {
        const QJsonArray items = event.value(QStringLiteral("items")).toArray();
        // A check this pane asked for — the panel's Check button, or a section header's warning
        // mark — answers in the panel as a clickable list (#8YQ9). Every other `board_problems`
        // is the board's own refresh and belongs in the banner over the list, where it has always
        // been. A scoped check is never allowed to rewrite that banner: it leaves out the
        // problems that belong to no one section (the worker's rule), so it would under-report
        // the board.
        const QString section = event.value(QStringLiteral("section")).toString();
        if (mine) {
            showFindings(items, section);
            if (section.isEmpty()) {
                showProblems(items);
                if (onStatus)
                    onStatus(items.isEmpty() ? QStringLiteral("Hygiene: format check passed")
                                            : QStringLiteral("Hygiene: %1 format findings").arg(items.size()));
            }
            return;
        }
        showProblems(items);
        return;
    }
    if (type == QStringLiteral("board_written") && mine) {
        const QString writeId = event.value(QStringLiteral("write_id")).toString();
        const QString kind = event.value(QStringLiteral("kind")).toString();
        const QString card = event.value(QStringLiteral("card_id")).toString();
        QString note = m_pendingNotes.take(requestId);
        // A notice the worker attached to the write itself (#WC3E: Execute on a card with no
        // `## Done means`, which warns and goes on). It is the board's news rather than the
        // card's, so it rides the notice line this write was already going to draw, and one
        // write still leaves one notice.
        if (const QString said = event.value(QStringLiteral("notice")).toString().trimmed();
            !said.isEmpty()) {
            const QString escaped = said.toHtmlEscaped();
            note = note.isEmpty() ? escaped : note + QStringLiteral(" · ") + escaped;
        }
        // The delete has landed (#CYM9): from here the "Deleted #ID · Undo" toast owns the
        // card's removal events, until the card itself comes back or the pane reloads.
        m_pendingDeletes.remove(requestId);
        if (kind == QStringLiteral("board_create")) {
            m_quickAddCreateRequest.clear();
            // The id is a `card:` link, so the notice itself zooms to the new card.
            note = QStringLiteral("Created <a href=\"card:%1\" style=\"color:%2;"
                                  "text-decoration:none\">#%1</a>")
                       .arg(card.toHtmlEscaped(), theme::Link.name());
            // The draft already collected the issue and links before the write (#5KMQ).
            closeQuickAdd();
            // "New card" pressed again while the last one's page is still open (#Y2BA, the
            // owner's ask): the new card opens in a pane of its own beside this one, rather than
            // taking the open card's place. The selection stays put, so the follow timer does not
            // swap the open page for it either.
            if (detailOpen() && !m_detail->cardId().isEmpty() && m_detail->cardId() != card
                && onOpenInNewPane) {
                onOpenInNewPane(card);
            } else {
                m_selected = card;
                openSelected();
            }
        }
        if (kind == QStringLiteral("board_comment"))
            return;              // the thread itself shows it
        // The edit is written: the card goes back to being read (the new text arrives with the
        // re-read that `board_changed` asks for).
        if (kind == QStringLiteral("board_update") && card == m_detail->cardId())
            m_detail->endEdit();
        m_lastWrite = writeId;
        if (!note.isEmpty())
            showNotice(note, false, writeId);
        return;
    }
    if (type == QStringLiteral("board_undone") && mine) {
        m_lastWrite.clear();
        showNotice(QStringLiteral("Undone: #%1 is back as it was.")
                       .arg(event.value(QStringLiteral("card_id")).toString()), false);
        return;
    }
    // A cleanup's events, and whole: they are tagged `cleanup: true` with a run id and no
    // card id (19.9), and an open card's thread must never see one of them.
    if (handleCleanupEvent(type, event))
        return;
    // A Try it run's events (#JNYN, 31.10), for the same reason and in the same place: they
    // belong to the run and to the board's notice area, never to an open card's thread.
    if (handleTryItEvent(type, event))
        return;
    // Cards run in parallel with each other but not with a cleanup, and never two turns on one
    // card (19.16): say which of those it was, and put back whatever this pane had started.
    if (type == QStringLiteral("error")
        && event.value(QStringLiteral("code")).toString() == QStringLiteral("board_busy")) {
        const bool cleanupRuns = event.value(QStringLiteral("cleanup_running")).toBool();
        const QString busyCard = event.value(QStringLiteral("card_id")).toString();
        QStringList running;
        for (const QJsonValue &value : event.value(QStringLiteral("cards")).toArray())
            running << QStringLiteral("#") + value.toString();
        const QString what = cleanupRuns
            ? QStringLiteral("A cleanup is running on this board.")
            : (running.size() > 1
                   ? QStringLiteral("The agent is already working on %1.")
                         .arg(running.join(QStringLiteral(", ")))
                   : (busyCard.isEmpty()
                          ? QStringLiteral("The Board agent is busy.")
                          : QStringLiteral("The agent is answering on #%1.").arg(busyCard)));
        if (!m_cleanupRequest.isEmpty() && requestId == m_cleanupRequest) {
            endCleanup();
            m_notice->hide();
            showNotice(what + QStringLiteral(" The cleanup did not start — nothing was written. "
                                             "Try again when it has finished."), true);
            return;
        }
        // The refused ask is this pane's own: the card it was typed on is the open one.
        const QString asked = m_detail->cardId();
        if (m_cardTurns.contains(asked)) {
            const QString unsent = m_cardTurns.take(asked).unsent;
            m_detail->setBusy(false);
            cardBusyChanged();
            // The worker checks before it writes, so the question never reached the thread.
            if (cleanupRuns)
                m_busyCard = asked;
            m_detail->restoreReply(unsent);
            m_detail->showError(what + QStringLiteral(" Your message was not sent and is not in "
                                                      "the thread — it is back in the reply box. "
                                                      "Try again when that one has finished, or "
                                                      "stop it on its own card."));
            QTimer::singleShot(0, this, [this] { placeNotice(); });
            return;
        }
        showNotice(what, true);
        return;
    }
    // A board_ask turn (19.16): every event carries the card it belongs to, and the board may be
    // showing another one. What the page keeps of a turn is the fact that it is running — the
    // strip, the list's working marker, the actions that wait for it — and nothing of its text:
    // since card #CTRN the turn's own events are drawn by that card's console, which is the only
    // console they are delivered to (`RelayWindow::deliverToConsoles`). The page used to render
    // `delta`, `thinking_*` and a progress line from the tool events as well, and that was the
    // same bytes drawn twice.
    const QString card = event.value(QStringLiteral("card_id")).toString();
    if (!card.isEmpty() && m_cardTurns.contains(card)) {
        const bool here = card == m_detail->cardId();
        CardTurn &turn = m_cardTurns[card];
        if (type == QStringLiteral("done") || type == QStringLiteral("error")
            || type == QStringLiteral("cancelled")) {
            const QString endedMode = turn.mode;   // read before the turn is dropped
            m_cardTurns.remove(card);
            cardBusyChanged();
            m_list->viewport()->update();       // the row stops saying it is working
            if (here) {
                m_detail->setBusy(false);
                if (type == QStringLiteral("error"))
                    m_detail->showError(event.value(QStringLiteral("text")).toString());
            } else if (type == QStringLiteral("error")) {
                showNotice(QStringLiteral("#%1: %2").arg(card, event.value(QStringLiteral("text")).toString()),
                           true);
            }
            // The bell learns the turn ended even when the board is not the pane being looked at
            // (#NQP9); the view itself posts nothing and moves no focus.
            if (onTurnEnded) onTurnEnded(card, endedMode, type);
            return;
        }
    }
    // A write this pane asked for and the worker refused (a move into Needs QA without evidence,
    // a stale hash): say so where the card was dropped instead of in a status bar.
    if (type == QStringLiteral("error") && mine) {
        if (requestId == m_directCardRequest) {
            m_directCardRequest.clear();
            setEmptyText(event.value(QStringLiteral("text")).toString());
            reload();
            return;
        }
        if (!m_quickAddTriageRequest.isEmpty() && requestId == m_quickAddTriageRequest) {
            m_quickAddTriageRequest.clear();
            m_quickAddTriageSupported = false;
            m_quickAddTriageStatus->clear();
            m_quickAddTriageStatus->hide();
            return;
        }
        if (requestId == m_quickAddCreateRequest) {
            m_quickAddCreateRequest.clear();
            m_quickAddSave->setEnabled(true);
            m_quickAddCancel->setEnabled(true);
        }
        m_pendingNotes.remove(requestId);
        m_pendingDeletes.remove(requestId);
        const QString text = event.value(QStringLiteral("text")).toString();
        // A worker too old to know `board_search` (#7M6E) refuses it, and it would refuse one per
        // burst of typing. That is not news: the filter goes on matching the row's own fields,
        // which is all it could do before the message existed. Stop asking, and say nothing.
        if (!m_searchRequest.isEmpty() && requestId == m_searchRequest) {
            m_searchRequest.clear();
            m_searchSupported = false;
            return;
        }
        // The same for `board_links` (#EE42): an older worker's refusal leaves the card page
        // without its "Linked from" block, as it was before the request existed.
        if (!m_linksRequest.isEmpty() && requestId == m_linksRequest) {
            m_linksRequest.clear();
            return;
        }
        // The Check gate (#7BM4): the move was refused because the tests this card names do not
        // prove it. The refusal is the board's news, not the card's — a drag from a column is
        // refused the same way — so it goes in the notice bar, with the one affordance that gets
        // past it: Override…, which asks for the reason in a line and re-sends the same move
        // with it. The worker quotes that reason into a `decision` entry on the thread.
        // The gate's one question (#PR4Q): this card names no checks, so before it lands the
        // board asks which ones prove it — once, recorded on the thread by the worker. The
        // question is a box under the card's own Tests strip, with "None apply" beside Save,
        // because that is where the answer is going to live.
        if (event.value(QStringLiteral("code")).toString() == QStringLiteral("tests_none")) {
            m_gatedMove = m_pendingMoves.value(requestId);
            showNotice(text, true);
            if (const QString card = event.value(QStringLiteral("card")).toString();
                !card.isEmpty() && card == m_detail->cardId()) {
                m_detail->openTestsEditor(true);
                send({{QStringLiteral("type"), QStringLiteral("board_card_get")},
                      {QStringLiteral("card"), card}});
            }
            return;
        }
        if (event.value(QStringLiteral("code")).toString() == QStringLiteral("tests_gate")) {
            m_gatedMove = m_pendingMoves.value(requestId);
            showNotice(text, true, QString(), !m_gatedMove.isEmpty());
            // Nothing was written, so no `board_changed` follows and the status picker is still
            // showing the lane the card did not go to. Read the card again so it snaps back to
            // where the card actually is: a picker that lies about a refused move is worse than
            // the refusal.
            if (const QString card = event.value(QStringLiteral("card")).toString();
                !card.isEmpty() && card == m_detail->cardId())
                send({{QStringLiteral("type"), QStringLiteral("board_card_get")},
                      {QStringLiteral("card"), card}});
            return;
        }
        // The card was written by someone else between the read and the save. Nothing was
        // overwritten and nothing typed is lost: read the card again (which brings the new hash
        // and says what changed) and leave the text in the editor for a second Save.
        if (event.value(QStringLiteral("code")).toString() == QStringLiteral("board_conflict")
            && m_detail->editing()) {
            m_detail->showError(QStringLiteral("#%1 changed on disk, so nothing was written. Your "
                                               "text is still here: Save again to write it over "
                                               "that version, or Esc to drop it.")
                                    .arg(m_detail->cardId()));
            send({{QStringLiteral("type"), QStringLiteral("board_card_get")},
                  {QStringLiteral("card"), m_detail->cardId()}});
            return;
        }
        // A question the agent could not take (no provider key, say): the question itself is
        // already in the thread, so say that under it rather than over the board. An error with
        // no card of its own belongs to the card this pane just asked on, which is the open one.
        if (const QString asked = m_detail->cardId(); m_cardTurns.contains(asked)) {
            m_cardTurns.remove(asked);
            cardBusyChanged();
            m_list->viewport()->update();
            m_detail->setBusy(false);
            m_detail->showError(QStringLiteral("The Board agent could not answer: %1 "
                                               "Your message is kept in the thread.").arg(text));
            return;
        }
        showNotice(text, true);
    }
}

void BoardView::showProblems(const QJsonArray &problems)
{
    int errors = 0;
    QString first, path;
    for (const QJsonValue &value : problems) {
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("severity")).toString() != QStringLiteral("error"))
            continue;
        ++errors;
        if (first.isEmpty()) {
            path = item.value(QStringLiteral("path")).toString();
            first = item.value(QStringLiteral("message")).toString();
        }
    }
    m_problems->setVisible(errors > 0);
    if (errors == 0) {
        m_problemFix.clear();
        return;
    }
    m_problemFix = board::fixRequest(path, first, errors);
    // The file first (it is the link that fixes it), then the message.
    QString text = QStringLiteral("⚠ %1").arg(errors == 1 ? QStringLiteral("1 problem")
                                                         : QStringLiteral("%1 problems").arg(errors));
    if (!path.isEmpty())
        text += QStringLiteral(" · <a href=\"%1\" style=\"color:%2\">%3</a>")
                    .arg(path.toHtmlEscaped(), theme::Warning.name(),
                         QFileInfo(path).fileName().toHtmlEscaped());
    text += QStringLiteral(" · ") + first.toHtmlEscaped();
    m_problems->setText(text);
    m_problems->setToolTip(path + QStringLiteral(": ") + first
                           + QStringLiteral("\n\nClick to draft a fix for the Board agent — "
                                            "it is put in its composer, not sent. `o` opens the "
                                            "file; python3 scripts/relay-board.py check lists "
                                            "every problem."));
}

// ------------------------------------------------------------------------ rendering

// "84 open" at the left of the filter row, with what the filter is hiding when one is set, and
// "62 of 84 open" when a section checkbox is keeping cards off the page. The pane's own title
// carries the board's own number, so the count is never only inside the list.
void BoardView::updateCounts()
{
    const int open = m_model.openCount();
    const int hidden = m_model.hiddenCount(m_hidden);
    QString text = QStringLiteral("%1 open").arg(open);
    if (!m_model.filter().trimmed().isEmpty() || !m_model.labelFilter().isEmpty()) {
        // With a filter set — words or ticked label chips — what the rows say is the honest
        // number: it counts the closed cards a `status:done` search turns up, which "open"
        // never does.
        int matched = 0;
        for (const board::Row &row : std::as_const(m_rows))
            if (row.kind == board::Row::Card)
                ++matched;
        text = QStringLiteral("%1 shown").arg(matched);
    } else if (hidden > 0) {
        text = QStringLiteral("%1 of %2 open").arg(open - hidden).arg(open);
    }
    m_count->setText(text);
    QString tip = QStringLiteral("%1 card%2 on the board in all").arg(m_model.total())
                      .arg(m_model.total() == 1 ? QString() : QStringLiteral("s"));
    if (hidden > 0)
        tip += QStringLiteral("\n%1 open card%2 hidden by the section checkboxes")
                   .arg(hidden).arg(hidden == 1 ? QString() : QStringLiteral("s"));
    m_count->setToolTip(tip);

    // Each box says how many cards its section holds right now, so unticking one is a decision
    // taken with the number in view rather than after the fact.
    for (int i = 0; i < m_checkIds.size() && i < m_checksLayout->count(); ++i) {
        // The last widget in the row is the gear, not a section: qobject_cast skips it.
        auto *box = qobject_cast<QCheckBox *>(m_checksLayout->itemAt(i)->widget());
        if (!box)
            continue;
        const QString id = m_checkIds.at(i);
        const int header = board::rowOfSection(m_rows, id);
        const QString title = sectionTitle(id);
        // A flat list (#ESDF) has no headers to read the count off: its cards are the count.
        const int held = header >= 0 ? m_rows.at(header).count
                                     : int(board::cardsInSection(m_rows, id).size());
        QString tip;
        if (m_hidden.contains(id))
            tip = QStringLiteral("%1 — hidden; tick to put the section back").arg(title);
        else if (header >= 0 || held > 0)
            tip = QStringLiteral("%1 · %2 card%3 — untick to hide the section")
                      .arg(title).arg(held)
                      .arg(held == 1 ? QString() : QStringLiteral("s"));
        else
            tip = QStringLiteral("%1 — nothing matches the filter").arg(title);
        box->setToolTip(withMeaning(tip, id));
    }
}

QStringList BoardView::columnIds() const
{
    QStringList out;
    const QList<board::Column> sections = m_model.sections();
    for (const board::Column &section : sections)
        out << section.id;
    return out;
}

QString BoardView::defaultCategory() const
{
    // The first work-card folder, which is what quick add files into. `planning` is skipped:
    // until #X7NB it was tagged as a plan-card tab and so was never the default, and a card
    // quick-added into a folder named after the Planning stage would read as already planned.
    for (const board::Tab &tab : m_model.tabs())
        if (!tab.folder.isEmpty() && tab.type == QStringLiteral("work")
            && tab.id != QStringLiteral("planning"))
            return tab.id;
    return QStringLiteral("features");
}

QString BoardView::sectionTitle(const QString &columnId) const
{
    const QList<board::Column> sections = m_model.sections();
    for (const board::Column &section : sections)
        if (section.id == columnId)
            return section.title;
    return board::statusTitle(columnId);
}

// Nothing is created straight into Done: a card gets there by being closed. A manual section
// takes them (#3XZV): a quick-add there is parked in it, its status still inbox.
bool BoardView::sectionTakesNewCards(const QString &columnId) const
{
    if (columnId == board::verifiedSection() || columnId == board::doneSection())
        return false;
    const QList<board::Column> sections = m_model.sections();
    for (const board::Column &section : sections) {
        if (section.id != columnId)
            continue;
        const QString landing = section.statuses.value(0);
        return !landing.isEmpty() ? (landing != QStringLiteral("done")
                                     && landing != QStringLiteral("dropped"))
                                  : true;   // collects nothing: a section filled by hand
    }
    return false;
}

void BoardView::selectRow(int index)
{
    if (index < 0 || index >= m_rows.size() || index >= m_list->count())
        return;
    QListWidgetItem *item = m_list->item(index);
    // Exactly one of the four is ever set (#93WR, #AQ6X): a fold row, a signal row and the two
    // signal toggles are selectable but are not cards, so standing on one empties the card
    // selection and every card action is inert.
    const board::Row &row = m_rows.at(index);
    m_selectedFold = row.kind == board::Row::Fold ? row.columnId : QString();
    m_selectedSignal = row.kind == board::Row::Signal ? row.signalKey : QString();
    m_selectedSignalFold = row.kind == board::Row::SignalFold ? QStringLiteral("signals")
                           : row.kind == board::Row::DismissedFold ? QStringLiteral("dismissed")
                                                                   : QString();
    m_selected = item->data(kCardRole).toString();
    m_list->setCurrentItem(item);
    item->setSelected(true);
    m_list->scrollToItem(item);
    m_list->setFocus();
}

// One list, refilled in place: the scroll position, the selection, the focus and the open
// quick-add field all survive a change, and nothing flashes.
void BoardView::refill()
{
    const bool hadFocus = m_list->hasFocus();
    const int scroll = m_list->verticalScrollBar()->value();
    // A new pane starts as a compact overview: every section is a count that opens on demand.
    // Restored panes skip this seed and keep exactly the folds saved in their layout.
    if (!m_collapsedSeeded) {
        m_collapsedSeeded = true;
        for (const board::Column &section : m_model.sections())
            m_collapsed.insert(section.id);
    }
    m_rows = m_model.rows(m_collapsed, m_hidden, m_selfClosedOpen);
    // The signals block (#AQ6X) goes at the very top, above the first section header. It is the
    // board's, not a section's: every section of a new pane starts folded, so a row inside one
    // would be hidden exactly when a test has just gone red, and a signal has no status to sit
    // under. A filter is about cards, so the block gets out of its way — the same rule that stops
    // a section folding while a search is on.
    if (m_model.filter().trimmed().isEmpty() && m_model.labelFilter().isEmpty()) {
        const QList<board::Row> block =
                m_signalsState.rows(m_signalFolds.contains(QStringLiteral("signals")),
                                    m_signalFolds.contains(QStringLiteral("dismissed")));
        for (int i = int(block.size()) - 1; i >= 0; --i)
            m_rows.prepend(block.at(i));
    }

    if (!m_model.filter().trimmed().isEmpty())
        m_list->placeholder = QStringLiteral("No card matches this filter.\nEsc clears it.");
    else if (!m_hidden.isEmpty() && m_model.openCount() > 0)
        m_list->placeholder = QStringLiteral("Every section is hidden.\nTick one at the top to see "
                                             "its cards.");
    else
        m_list->placeholder = QStringLiteral("Nothing open.\nPress n to add a card.");

    const QSignalBlocker block(m_list);
    m_list->setUpdatesEnabled(false);
    m_list->clear();
    for (const board::Row &row : std::as_const(m_rows)) {
        auto *item = new QListWidgetItem(m_list);
        if (row.kind == board::Row::Section) {
            // A header is not a card: it is never selected, never dragged, and Up/Down steps
            // straight over it (board::stepRow).
            item->setFlags(Qt::ItemIsEnabled);
            item->setToolTip(withMeaning(row.collapsed
                                             ? QStringLiteral("%1 · %2 cards — click to show them")
                                                   .arg(row.title).arg(row.count)
                                             : QStringLiteral("%1 · %2 cards — click to fold")
                                                   .arg(row.title).arg(row.count),
                                         row.columnId));
            continue;
        }
        // The self-closed fold row (#93WR): selectable, because Enter and ←/→ work on it, and
        // nothing else — it carries no card id, so it cannot be dragged, opened, moved or deleted,
        // and the handlers that read that id all step over it.
        if (row.kind == board::Row::Fold) {
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            item->setToolTip(row.collapsed
                                 ? QStringLiteral("Cards the agent finished and closed itself, "
                                                  "without a verifier. Enter or → to show them.")
                                 : QStringLiteral("Cards the agent finished and closed itself, "
                                                  "without a verifier. Enter or ← to put them "
                                                  "away."));
            if (row.columnId == selectedFold()) {
                m_list->setCurrentItem(item);
                item->setSelected(true);
            }
            continue;
        }
        // The signals block (#AQ6X): the two toggles and one row per signal. Selectable, because
        // Enter and ←/→ work on them, and never draggable — a signal is not a card and cannot be
        // moved into a section.
        if (row.kind == board::Row::SignalFold || row.kind == board::Row::DismissedFold) {
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            const bool signalsRow = row.kind == board::Row::SignalFold;
            item->setToolTip(signalsRow ? board::signalsFoldTip(row.collapsed)
                                        : board::dismissedFoldTip(row.collapsed));
            if (selectedSignalFold() == (signalsRow ? QStringLiteral("signals")
                                                    : QStringLiteral("dismissed"))) {
                m_list->setCurrentItem(item);
                item->setSelected(true);
            }
            continue;
        }
        if (row.kind == board::Row::Signal) {
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            if (const board::Signal *signal = m_signalsState.signalFor(row.signalKey)) {
                QString tip = board::signalRowLine(*signal, QDateTime::currentDateTimeUtc());
                if (!signal->session.isEmpty()) {
                    const QString chip = board::sessionChip(signal->session, true);
                    tip += !tokenLive(signal->session)
                                   ? QStringLiteral("\nClaimed by %1, which has closed").arg(chip)
                           : m_signalThreads.isRunning(signal->session)
                                   ? QStringLiteral("\nWorked by %1, a signal thread Relay "
                                                    "started. Enter on the chip opens it.").arg(chip)
                                   : QStringLiteral("\nClaimed by %1").arg(chip);
                }
                if (!signal->card.isEmpty())
                    tip += QStringLiteral("\nPromoted to #%1").arg(signal->card);
                tip += QStringLiteral("\nEnter opens it.");
                item->setToolTip(tip);
            }
            if (row.signalKey == selectedSignal()) {
                m_list->setCurrentItem(item);
                item->setSelected(true);
            }
            continue;
        }
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        item->setData(kCardRole, row.cardId);
        if (const board::Card *card = m_model.card(row.cardId)) {
            QString tip = QStringLiteral("%1 · %2\nPriority %3 — left-click raises the flag, "
                                         "right-click lowers it.\n%4")
                              .arg(card->reference(), card->title,
                                   card->priority > 0 ? QStringLiteral("+%1").arg(card->priority)
                                   : card->priority < 0 ? QStringLiteral("−1")
                                                        : QStringLiteral("0"),
                                   card->path);
            // The claim chip is eight characters of a session token (#R9G7), which says nothing
            // on its own: the tooltip is where the row says what they are and whether that pane
            // is still there. The chip itself is asked about here, as the row is filled, so a
            // pane closed while the board is up reads as closed at the next refill.
            if (!card->session.isEmpty()) {
                const QString chip = board::sessionChip(card->session, true);
                // A card a signal thread promoted and claimed wears the thread's token, and no
                // pane has one (#AQ6X phase 3), so the same answer the chip is drawn from.
                tip += !tokenLive(card->session)
                           ? QStringLiteral("\nClaimed by the pane %1, which has closed").arg(chip)
                   : m_signalThreads.isRunning(card->session)
                           ? QStringLiteral("\nWorked by %1, a signal thread Relay started")
                                     .arg(chip)
                           : QStringLiteral("\nClaimed by the pane %1. Click the chip to open it.")
                                     .arg(chip);
            }
            item->setToolTip(tip);
        }
        if (row.cardId == m_selected) {
            m_list->setCurrentItem(item);
            item->setSelected(true);
        }
    }
    m_list->doItemsLayout();
    m_list->verticalScrollBar()->setValue(scroll);
    m_list->setUpdatesEnabled(true);
    m_list->viewport()->update();
    // A card moved with the keyboard keeps the focus: it follows the card to its new section.
    if (hadFocus) {
        m_list->setFocus();
        if (QListWidgetItem *current = m_list->currentItem())
            m_list->scrollToItem(current);
    }
}

void BoardView::rebuild()
{
    if (m_dragActive) {
        m_rebuildPending = true;
        return;
    }
    m_rebuildPending = false;
    bool hasSnoozed = false;
    for (const QString &id : m_model.allIds()) {
        const board::Card *card = m_model.card(id);
        if (card && card->snoozed && !card->closed()) {
            hasSnoozed = true;
            break;
        }
    }
    if (m_snoozed) {
        if (!hasSnoozed && m_snoozed->isChecked()) {
            const QSignalBlocker blocker(m_snoozed);
            m_snoozed->setChecked(false);
            m_model.setSnoozedOnly(false);
        }
        m_snoozed->setVisible(hasSnoozed);
    }
    syncSectionChecks();   // before the refill: the boxes decide which sections it puts in
    syncLabelChecks();     // and the chips which labels it keeps
    refill();
    updateCounts();

    const bool empty = m_model.total() == 0;
    if (!m_open && !m_workerError.isEmpty())
        // The board never loaded and the worker said why (#7M6E). Here rather than in the status
        // bar, which this layout does not show — so "Loading the Switchboard…" used to stay up
        // for ever, which is what a board of more than about 1,160 cards did to it.
        setEmptyText(m_workerError + QStringLiteral("\n\nThe cards are files; nothing has been "
                                                    "lost."), true);
    else if (!m_open)
        setEmptyText(QStringLiteral("Loading the Board…"));
    else
        setEmptyText(QStringLiteral("No cards yet.\n\nPress n or click “+ New card” to add the first one."));
    m_empty->setVisible(!m_open || empty);
    m_emptyRetryRow->setVisible(m_empty->isVisible() && !m_workerError.isEmpty());
    m_splitter->setVisible(m_open && !empty);
    m_keys->setVisible(m_open && !empty);
    if (m_sectionsOpen) {        // the gear's page has the pane: a redraw must not push it off
        m_empty->hide();
        m_emptyRetryRow->hide();
        m_splitter->hide();
        m_keys->hide();
    }
    syncChatVisible();
    applyPage();    // the Skills/Memories/Live pages' visibility (#9FX8, #C52H) settles last, over the list's
    syncLivePage();
    syncBackgroundPage();
}

// The page agent belongs to the list page: it is there whenever the board is, including a board
// with no cards yet, and it is away while a card or the sections page has the pane.
void BoardView::syncChatVisible()
{
    if (m_chatArea == nullptr)
        return;
    const bool show = m_open && !m_pinned && !m_sectionsOpen && !detailOpen() && !signalOpen()
                      && m_page == Page::Cards;   // the console belongs to the cards page (#9FX8)
    if (show)
        ensureConsole();          // asked for the first time the area is actually shown
    m_chatArea->setVisible(show);
}

// The gear at the end of the section checkboxes: the section list itself, editable. It is a page
// in this pane, so the board stays where it is and Esc or Cancel comes straight back.
void BoardView::openSections()
{
    if (!m_open)
        return;
    m_sectionsOpen = true;
    m_sections->setModel(m_model);
    m_sections->setFolder(QFileInfo(m_root.isEmpty() ? projects::boardDirOf(m_workspace) : m_root).fileName());
    m_sections->show();
    m_sections->setFocus(Qt::OtherFocusReason);   // Esc closes the page from the moment it opens
    rebuild();
}

void BoardView::closeSections()
{
    if (!m_sectionsOpen)
        return;
    m_sectionsOpen = false;
    m_sections->hide();
    rebuild();
}

// A section folds and unfolds; which sections are folded is saved with the window's layout.
// A click on a column header (and a restored pane's saved sort): set the model, show it on the
// header, redraw. Safe on a board that has not opened yet — the rows are empty until the first
// `board` event, and the order is waiting for them.
void BoardView::setSortOrder(const QString &id)
{
    const board::Sort sort = board::sortFromId(id);
    if (sort == m_model.sort())
        return;
    m_model.setSort(sort);
    syncColumnHeader();
    rebuild();
    // The selection may have moved out from under the card: stand on it where it landed.
    const int at = board::rowOfCard(m_rows, m_selected);
    if (at >= 0)
        selectRow(at);
}

// The header shows what is on: the cell that wears the arrow. A restored pane opens with it
// right, because the sort rides the layout node and the header reads the model.
void BoardView::syncColumnHeader()
{
    if (!m_columnHeader)
        return;
    m_columnHeader->setSort(m_model.sort());
    m_columnHeader->setGrouping(m_model.grouping());
}

void BoardView::restoreGrouping(const QString &id)
{
    const board::Grouping grouping = board::groupingFromId(id);
    m_model.setGrouping(grouping);
    if (id.isEmpty() && grouping == board::Grouping::Flat && m_model.sort() == board::Sort::Manual)
        m_model.setSort(board::Sort::RecentlyUpdated);
    syncColumnHeader();
    if (m_open)
        rebuild();
}

// The Stage header's click (#ESDF): the board a section per stage, or one list of every card
// with the stage in its own column.
void BoardView::toggleGrouping()
{
    const bool toFlat = m_model.grouping() == board::Grouping::Sections;
    m_model.setGrouping(toFlat ? board::Grouping::Flat : board::Grouping::Sections);
    if (toFlat && m_model.sort() == board::Sort::Manual)
        m_model.setSort(board::Sort::RecentlyUpdated);
    else if (!toFlat)
        m_model.setSort(board::Sort::Manual);
    syncColumnHeader();
    rebuild();
    const int at = board::rowOfCard(m_rows, m_selected);
    if (at >= 0)
        selectRow(at);
    showNotice(toFlat ? QStringLiteral("One list, %1. Click STAGE to group by stage again.")
                            .arg(board::sortTitle(m_model.sort()).toLower())
                      : QStringLiteral("Grouped by stage. Click STAGE for one list of every card."), false);
}

// One click on a row's flag (#VKFV): raise it (left) or lower it (right), clamped at −1…+3.
// The row moves the moment it is clicked and the worker's `board_changed` settles it, exactly
// like a drag; a click at 0 that lands on 0 is still sent, so the notice and the undo record
// agree with what the file says.
void BoardView::setCardPriority(QString id, int step)
{
    const board::Card *card = m_model.card(id);
    if (!card)
        return;
    const int priority = qBound(-1, card->priority + step, 3);
    board::Card updated = *card;
    updated.priority = priority;
    m_model.upsert(updated);
    rebuild();
    // The page shows the click at once if it is the card on screen (#DPJB); the worker's
    // `board_changed` then confirms it, the way it settles the row.
    if (detailOpen() && m_detail->cardId() == id)
        m_detail->setPriority(priority);
    static const char *const names[5] = {"−1", "0", "+1", "+2", "+3"};
    const QString requestId = nextRequestId();
    m_pendingNotes.insert(requestId,
                          priority == 0
                              ? QStringLiteral("Cleared the flag on #%1").arg(id)
                              : QStringLiteral("Flagged #%1 at %2").arg(
                                    id, QString::fromLatin1(names[priority + 1])));
    send({{QStringLiteral("type"), QStringLiteral("board_priority")},
          {QStringLiteral("id"), requestId},
          {QStringLiteral("card"), id},
          {QStringLiteral("priority"), priority}});
}

void BoardView::toggleSection(QString columnId)
{
    if (columnId.isEmpty())
        return;
    m_collapsedSeeded = true;
    if (m_collapsed.contains(columnId))
        m_collapsed.remove(columnId);
    else
        m_collapsed.insert(columnId);
    const bool hadFocus = m_list->hasFocus();
    rebuild();
    // The selection may be inside what just folded: stand on the nearest card still on screen.
    if (board::rowOfCard(m_rows, m_selected) < 0) {
        const int header = board::rowOfSection(m_rows, columnId);
        int next = board::stepRow(m_rows, header, 1);
        if (next < 0)
            next = board::stepRow(m_rows, header, -1);
        if (next >= 0) {
            if (hadFocus)
                selectRow(next);
            else
                m_selected = m_rows.at(next).cardId;
        } else {
            m_selected.clear();
        }
    }
}

// The self-closed fold row (#93WR): show this section's self-closed cards, or put them away.
// Folding while the selection is inside moves the selection to the fold row, which is where ←
// left off and where → carries on from.
void BoardView::toggleSelfClosed(QString columnId)
{
    if (columnId.isEmpty())
        return;
    const bool hadFocus = m_list->hasFocus();
    if (m_selfClosedOpen.contains(columnId))
        m_selfClosedOpen.remove(columnId);
    else
        m_selfClosedOpen.insert(columnId);
    rebuild();
    const int fold = board::rowOfFold(m_rows, columnId);
    if (board::rowOfCard(m_rows, m_selected) >= 0)
        return;               // the selected card is still on screen: nothing to move
    m_selected.clear();
    if (fold < 0) {
        m_selectedFold.clear();
        return;               // the row itself has gone (its section folded, or a filter)
    }
    m_selectedFold = columnId;
    if (hadFocus)
        selectRow(fold);
}

QJsonArray BoardView::openSelfClosed() const
{
    QStringList ids(m_selfClosedOpen.begin(), m_selfClosedOpen.end());
    ids.sort();
    return QJsonArray::fromStringList(ids);
}

void BoardView::setOpenSelfClosed(const QJsonArray &state)
{
    m_selfClosedOpen.clear();
    for (const QJsonValue &value : state)
        if (!value.toString().isEmpty())
            m_selfClosedOpen.insert(value.toString());
    if (m_open)
        rebuild();
}

// --------------------------------------------------------------------- signals (#AQ6X)
//
// Two toggles and one row per signal, above the sections; a page in the card detail's place; four
// messages out (protocol §32.2). Nothing here writes the record: the worker folds it and pushes
// `signals_changed`, so every one of these ends in a message or in a redraw, never in a state of
// its own that the worker could disagree with.

QString BoardView::selectedSignalFold() const
{
    return m_selected.isEmpty() && m_selectedFold.isEmpty() && m_selectedSignal.isEmpty()
                   ? m_selectedSignalFold
                   : QString();
}

QString BoardView::selectedSignal() const
{
    return m_selected.isEmpty() && m_selectedFold.isEmpty() ? m_selectedSignal : QString();
}

QString BoardView::signalFoldOf(const QString &key) const
{
    const board::Signal *signal = m_signalsState.signalFor(key);
    if (!signal)
        return QString();
    return signal->dismissed() ? QStringLiteral("dismissed") : QStringLiteral("signals");
}

void BoardView::toggleSignalFold(QString which)
{
    if (which != QStringLiteral("signals") && which != QStringLiteral("dismissed"))
        return;
    const bool hadFocus = m_list->hasFocus();
    if (m_signalFolds.contains(which))
        m_signalFolds.remove(which);
    else
        m_signalFolds.insert(which);
    rebuild();
    const int fold = which == QStringLiteral("signals") ? board::rowOfSignalFold(m_rows)
                                                        : board::rowOfDismissedFold(m_rows);
    if (!m_selected.isEmpty() && board::rowOfCard(m_rows, m_selected) >= 0)
        return;               // a card is selected and still on screen: nothing to move
    if (fold < 0) {
        m_selectedSignalFold.clear();
        return;               // the row has gone (a filter, or the last signal resolved)
    }
    m_selected.clear();
    m_selectedFold.clear();
    m_selectedSignal.clear();
    m_selectedSignalFold = which;
    if (hadFocus)
        selectRow(fold);
}

QJsonArray BoardView::openSignals() const
{
    QStringList ids(m_signalFolds.begin(), m_signalFolds.end());
    ids.sort();
    return QJsonArray::fromStringList(ids);
}

void BoardView::setOpenSignals(const QJsonArray &state)
{
    m_signalFolds.clear();
    for (const QJsonValue &value : state) {
        const QString id = value.toString();
        // Only the two ids this version knows: a saved node from a later one must not open
        // something at random.
        if (id == QStringLiteral("signals") || id == QStringLiteral("dismissed"))
            m_signalFolds.insert(id);
    }
    if (m_open)
        rebuild();
}

void BoardView::selectSignal(const QString &key)
{
    if (key.isEmpty())
        return;
    // A signal behind a folded toggle cannot be stood on, so reaching one by key opens what holds
    // it — the same rule a self-closed card's group follows (#93WR).
    if (board::rowOfSignal(m_rows, key) < 0) {
        const QString which = signalFoldOf(key);
        if (which.isEmpty())
            return;
        if (!m_signalFolds.contains(which)) {
            m_signalFolds.insert(which);
            rebuild();
        }
    }
    const int at = board::rowOfSignal(m_rows, key);
    if (at < 0)
        return;
    selectRow(at);
}

void BoardView::openSignal(const QString &key)
{
    const board::Signal *signal = m_signalsState.signalFor(key);
    if (!signal)
        return;
    selectSignal(key);
    // A card and a signal never share the pane: whichever is opened takes the page.
    if (detailOpen())
        m_detail->hide();
    m_signalDetail->showSignal(*signal);
    updateDetailLayout();
    m_signalDetail->setFocus();
}

void BoardView::closeSignal()
{
    if (m_signalDetail->isHidden())
        return;
    m_soloReveal = false;   // back to the list, so the list is what comes back (#K4SQ)
    m_signalDetail->hide();
    updateDetailLayout();
    focusInput();
}

bool BoardView::signalOpen() const
{
    return m_signalDetail && !m_signalDetail->isHidden();
}

void BoardView::askSignalsOnce()
{
    // Once per view: the worker pushes `signals_changed` after every change from here on, so a
    // second question would only repeat an answer the pane already has.
    if (m_signalsAsked || !m_open || !isVisible())
        return;
    m_signalsAsked = true;
    send({{QStringLiteral("type"), QStringLiteral("signals_list")}});
}

QString BoardView::cardSignalStrip() const
{
    return m_detail->signalStrip();
}

bool BoardView::tokenLive(const QString &token) const
{
    if (token.isEmpty())
        return true;                   // nothing claims it, so nothing to be closed
    // A signal thread's token belongs to no pane (#AQ6X phase 3): it is the thread's own id, and
    // the thread runs in the board worker. So this is asked first — `paneExists` would answer no
    // for a thread that is working, and the chip would say `closed` while a fix was under way.
    if (m_signalThreads.isRunning(token))
        return true;
    return !paneExists || paneExists(token);
}

void BoardView::revealClaim(const QString &token)
{
    if (token.isEmpty())
        return;
    // A claim under a signal thread's id opens that thread's history, which is the same place the
    // Sessions manager's Enter opens and the notification's button goes (decision 9). A pane's
    // token reveals the pane, as it always did.
    if (const board::SignalThreadRun *run = m_signalThreads.forToken(token)) {
        if (onOpenThread) {
            onOpenThread(run->threadId, run->sessionId);
            return;
        }
    }
    if (onFocusPane)
        onFocusPane(token);
}

QRect BoardView::claimChipRect(const QString &cardId) const
{
    // The same rect the click guard hit-tests, through the same std::function the pane wires
    // (#YJ4A): a test clicking here and a user clicking there can only be the same click.
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).kind != board::Row::Card || m_rows.at(i).cardId != cardId)
            continue;
        const QListWidgetItem *item = m_list->item(i);
        return item ? m_list->sessionChipRectOf(i, m_list->visualItemRect(const_cast<QListWidgetItem *>(item)))
                    : QRect();
    }
    return QRect();
}

void BoardView::announceSignalThread(const board::SignalThreadRun &run)
{
    // Decision 9: "you get a notification that you can click on to open the agent thread". One
    // entry per thread — the `started` event posts it, the `finished` one amends it in place, so
    // the bell never shows two lines about one fault.
    if (run.threadId.isEmpty())
        return;
    QString reason;
    if (run.outcome == QStringLiteral("dismissed")) {
        if (const board::Signal *signal = m_signalsState.signalFor(run.key))
            reason = board::dismissReasonTitle(signal->dismissedReason);
    }
    const QString title = board::signalThreadTitle(run, reason);
    const QString body = board::signalThreadBody(run);
    const QString action = board::signalThreadActionId(run);
    const QString label = action.isEmpty() ? QString() : board::signalThreadActionLabel();
    const QString existing = m_signalThreadNotices.value(run.threadId);
    if (!existing.isEmpty()) {
        relay::NotificationCenter::instance().amend(existing, title, body, label, action,
                                                    board::signalThreadKind(run));
        if (!run.running)
            m_signalThreadNotices.remove(run.threadId);
        return;
    }
    const QString id = relay::NotificationCenter::instance().postWithAction(
            title, body, board::signalThreadKind(run), m_root, label, action);
    if (run.running && !id.isEmpty())
        m_signalThreadNotices.insert(run.threadId, id);
}

QString BoardView::paneClaimToken() const
{
    // The Switchboard pane runs no agent of its own, so it claims under its own view token: two
    // board panes never chase one test, and a signal thread (#AQ6X phase 3) claims under its
    // thread id instead. No pane has this token, so the chip reads `closed`, which is honest.
    return QStringLiteral("board-") + m_requestPrefix;
}

void BoardView::sendSignal(const QString &type, const QJsonObject &fields)
{
    const QString key = m_signalDetail->key();
    if (key.isEmpty())
        return;
    const QString id = nextRequestId();
    m_signalRequests.insert(id, key);
    QJsonObject message{{QStringLiteral("type"), type}, {QStringLiteral("id"), id},
                        {QStringLiteral("key"), key}};
    for (auto at = fields.constBegin(); at != fields.constEnd(); ++at)
        message.insert(at.key(), at.value());
    send(message);
}


QJsonArray BoardView::collapsedSections() const
{
    QStringList ids(m_collapsed.begin(), m_collapsed.end());
    ids.sort();
    return QJsonArray::fromStringList(ids);
}

void BoardView::setCollapsedSections(const QJsonArray &state)
{
    m_collapsed.clear();
    m_collapsedSeeded = true;      // a restored pane keeps what the window remembered, even none
    for (const QJsonValue &value : state)
        if (!value.toString().isEmpty())
            m_collapsed.insert(value.toString());
    if (m_open)
        rebuild();
}

// The same shape and the same home in the layout node as the folded set, one key over. Unticked
// is not folded: a folded section is a header with its cards put away, an unticked one is not on
// the page at all and its cards are out of the count.
QJsonArray BoardView::hiddenSections() const
{
    QStringList ids(m_hidden.begin(), m_hidden.end());
    ids.sort();
    return QJsonArray::fromStringList(ids);
}

// The label chips' ticked set, in the same shape and home in the layout node as the hidden
// sections (#VKFV). A label the board no longer carries is kept harmlessly: it filters nothing
// until a card wears it again.
QJsonArray BoardView::labelFilter() const
{
    QStringList labels(m_labelPicked.begin(), m_labelPicked.end());
    labels.sort();
    return QJsonArray::fromStringList(labels);
}

void BoardView::setLabelFilter(const QJsonArray &state)
{
    m_labelPicked.clear();
    for (const QJsonValue &value : state)
        if (!value.toString().isEmpty())
            m_labelPicked.insert(value.toString());
    m_model.setLabelFilter(m_labelPicked);
    // (#1Q5V) the chips are gone; the picked labels apply straight to the model.
    if (m_open)
        rebuild();
}

void BoardView::setHiddenSections(const QJsonArray &state)
{
    m_hidden.clear();
    for (const QJsonValue &value : state)
        if (!value.toString().isEmpty())
            m_hidden.insert(value.toString());
    // The boxes exist only once the sections do; syncSectionChecks reads m_hidden when it builds
    // them, and this keeps any that are already up in step.
    for (int i = 0; i < m_checkIds.size() && m_checksLayout && i < m_checksLayout->count(); ++i) {
        if (auto *box = qobject_cast<QCheckBox *>(m_checksLayout->itemAt(i)->widget())) {
            const QSignalBlocker block(box);
            box->setChecked(!m_hidden.contains(m_checkIds.at(i)));
        }
    }
    if (m_open)
        rebuild();
}

void BoardView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateConsoleHeight();   // the console's share of the pane follows the pane's own height
    updateDetailLayout();
    placeNotice();
    placeToast();
}

// Catch up when the pane is looked at again (#N5JJ).
//
// A directory watch misses an in-place write, and Relay's own writers all replace their files
// now — but a guest CLI, an editor or a script need not, and a tab that was in the background
// will have missed whatever inotify coalesced away. So the pane asks once when it is shown and
// once when the focus arrives, debounced through the same 400 ms timer as a watcher fire.
// Not a poll: #057J is taking idle wakeups out of Relay, and a refresh that finds nothing is
// about 6 ms of worker with the parse cache behind it (#7M6E), which is only worth spending when
// somebody is actually reading the board.
void BoardView::showEvent(QShowEvent *event)
{
    // The pane is being looked at: ask what the machine has open against this board (#AQ6X), if
    // the cards have arrived and nobody has asked yet.
    askSignalsOnce();
    QWidget::showEvent(event);
    catchUp();
}

void BoardView::catchUp()
{
    if (m_open && m_refresh)
        m_refresh->start();
}

// ------------------------------------------------------ the Live strip (#TBRH)
//
// PROJECT-BOARD-DESIGN §6 and #EA37 decision 3: no pane tab. What the Board lacks is the reverse
// of the Projects page — from here, who is working on this project right now — so the top of
// the Cards tab carries one chip per open terminal pane attached to the project: its token's
// first eight (the chip every other surface shows, `board::sessionChip`), its model, ✦ while a
// turn runs, and then the open cards whose `session` is that pane. The pane chip reveals the
// pane; a card chip opens the card. It is computed from the window's panes and the model's own
// rows on every refresh and never written anywhere — a pane is never a stored link target
// (§4.1). The Projects page keeps attach, reveal and filter.
// ------------------------------------------------------ the Live page (#TBRH, tab since #C52H)

void BoardView::buildLivePage(QVBoxLayout *layout)
{
    m_livePage = new QWidget(this);
    m_livePage->setObjectName(QStringLiteral("boardLivePage"));
    m_livePage->installEventFilter(this);   // middle-click on a card chip docks it (#HKY4)
    m_livePage->hide();
    auto *column = new QVBoxLayout(m_livePage);
    column->setContentsMargins(0, 6, 0, 0);
    column->setSpacing(4);
    auto *channel = new QHBoxLayout;
    m_integrationSummary = new QLabel(m_livePage);
    m_integrationSummary->setObjectName(QStringLiteral("boardIntegrationSummary"));
    channel->addWidget(m_integrationSummary, 1);
    m_mainButton = new QPushButton(QStringLiteral("Relay (main)"), m_livePage);
    m_mainButton->setObjectName(QStringLiteral("boardMainLauncher"));
    m_mainButton->setToolTip(QStringLiteral("Open the current installed runnable-main release"));
    connect(m_mainButton, &QPushButton::clicked, this, [this] {
        const QString script = dataRoot() + QStringLiteral("/scripts/relay-land");
        if (!m_queueMode || m_mainRelease.value(QStringLiteral("current")).toObject().isEmpty()
            || !QFileInfo::exists(script)) return;
        // main-run resolves the atomic `current` channel itself. The project is a single argv
        // value, never text pasted into a shell or a path to the source checkout's binary.
        qint64 pid = 0;
        if (!QProcess::startDetached(relayPython(),
                {script, QStringLiteral("--repo"), m_workspace, QStringLiteral("main-run")},
                m_workspace, &pid))
            showNotice(QStringLiteral("Could not launch the installed Relay (main) release."), false);
    });
    channel->addWidget(m_mainButton);
    column->addLayout(channel);
    m_liveEmpty = new QLabel(QStringLiteral("No panes are open on this project."), m_livePage);
    m_liveEmpty->setObjectName(QStringLiteral("boardLiveEmpty"));
    column->addWidget(m_liveEmpty);
    auto *rows = new QWidget(m_livePage);   // one row widget per open pane, cleared wholesale
    m_liveRows = new QVBoxLayout(rows);
    m_liveRows->setContentsMargins(0, 0, 0, 0);
    m_liveRows->setSpacing(4);
    column->addWidget(rows);
    column->addStretch(1);
    layout->addWidget(m_livePage, 1);
    // Panes open, close and start turns without any board event, so the page also looks again
    // on a slow tick — only while the pane is on screen, and only redrawing what changed.
    m_liveTimer = new QTimer(this);
    m_liveTimer->setInterval(2000);
    connect(m_liveTimer, &QTimer::timeout, this, [this] {
        if (isVisible()) {
            syncLivePage();
            syncBackgroundPage();
            if (m_page == Page::Live) pollIntegrationStatus();
        }
    });
    m_liveTimer->start();
}

void BoardView::pollIntegrationStatus()
{
    if (m_integrationPoll || (m_integrationPollAge.isValid() && m_integrationPollAge.elapsed() < 5000))
        return;
    m_integrationPollAge.start();
    pollIntegrationStatusStep(0);
}

void BoardView::pollIntegrationStatusStep(int step)
{
    if (step > 2) { syncLivePage(); return; }
    const QString script = dataRoot() + (step == 0 ? QStringLiteral("/scripts/relay-tree")
                                                  : QStringLiteral("/scripts/relay-land"));
    if (!QFileInfo::exists(script)) {
        m_queueProblem = QStringLiteral("Integration command is unavailable: %1").arg(script);
        syncLivePage();
        return;
    }
    QStringList args{script};
    if (step == 0) args << QStringLiteral("resolve") << m_workspace;
    else {
        args << QStringLiteral("--repo") << m_workspace;
        args << (step == 1 ? QStringLiteral("snapshot") : QStringLiteral("events"));
        if (step == 2) args << QStringLiteral("--since") << QString::number(m_lastIntegrationEvent);
    }
    auto *process = new QProcess(this);
    m_integrationPoll = process;
    process->setWorkingDirectory(m_workspace);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, process, step](int code, QProcess::ExitStatus exit) {
        const QByteArray output = process->readAllStandardOutput();
        const QString error = QString::fromUtf8(process->readAllStandardError()).trimmed();
        process->deleteLater();
        if (m_integrationPoll != process) return;
        m_integrationPoll = nullptr;
        const QJsonDocument document = QJsonDocument::fromJson(output);
        if (step == 0) {
            m_queueMode = exit == QProcess::NormalExit && code == 0
                          && document.object().value(QStringLiteral("mode")).toString() == QStringLiteral("queue");
            m_repoId = m_queueMode ? document.object().value(QStringLiteral("id")).toString() : QString();
            if (!m_queueMode) {
                m_queueJobs = {}; m_retainedTrees = {}; m_mainRelease = {}; m_queueProblem.clear();
                syncLivePage();
                return;
            }
            m_queueProblem.clear();
        } else if (exit != QProcess::NormalExit || code != 0) {
            m_queueProblem = error.isEmpty() ? QStringLiteral("Integration status request failed") : error;
        } else if (step == 1 && document.isObject()
                   && document.object().value(QStringLiteral("repo_id")).toString() == m_repoId) {
            QJsonObject snapshot = document.object();
            snapshot.insert(QStringLiteral("event"), QStringLiteral("queue_status"));
            handleEvent(snapshot);
            m_queueProblem.clear();
        } else if (step == 2 && document.isArray()) {
            for (const QJsonValue &value : document.array()) {
                const QJsonObject row = value.toObject();
                m_lastIntegrationEvent = qMax(m_lastIntegrationEvent,
                    qint64(row.value(QStringLiteral("id")).toDouble()));
                if (row.value(QStringLiteral("kind")).toString() != QStringLiteral("main_moved")) continue;
                QJsonObject notice = row;
                notice.insert(QStringLiteral("event"), QStringLiteral("main_moved"));
                handleEvent(notice);
            }
        } else {
            m_queueProblem = QStringLiteral("Integration status returned an unexpected response");
        }
        pollIntegrationStatusStep(step + 1);
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_integrationPoll != process) return;
        m_integrationPoll = nullptr;
        m_queueProblem = QStringLiteral("Integration status command could not start: ") + process->errorString();
        process->deleteLater();
        syncLivePage();
    });
    process->start(relayPython(), args);
}

void BoardView::syncLivePage()
{
    if (m_livePage == nullptr)
        return;
    // The tab row's own rule (applyPage), narrowed to the Live tab: never on Cards or Skills,
    // under an open card or signal, the sections editor or a pinned card pane (#Y2BA).
    const bool onLive = !m_pinned && !m_sectionsOpen && !detailOpen() && !signalOpen()
                        && m_page == Page::Live;
    m_livePage->setVisible(onLive);
    const QJsonArray panes = onLive && livePanes ? livePanes() : QJsonArray{};
    struct Live { QString token, title, model; bool busy; QStringList cards; QJsonObject tree; };
    QList<Live> rows;
    QString key;
    for (const QJsonValue &value : panes) {
        const QJsonObject pane = value.toObject();
        Live live{pane.value(QStringLiteral("token")).toString(),
                  pane.value(QStringLiteral("title")).toString(),
                  pane.value(QStringLiteral("model")).toString(),
                  pane.value(QStringLiteral("busy")).toBool(), {},
                  pane.value(QStringLiteral("tree_status")).toObject()};
        if (live.token.isEmpty())
            continue;
        live.cards = m_model.claimedBy(live.token);
        key += QStringLiteral("%1|%2|%3|%4|").arg(live.token, live.title, live.model,
                                                   live.busy ? QStringLiteral("1") : QString());
        key += QString::fromUtf8(QJsonDocument(live.tree).toJson(QJsonDocument::Compact));
        for (const QString &id : std::as_const(live.cards)) {
            const board::Card *card = m_model.card(id);
            key += id + QLatin1Char(':') + (card ? card->title : QString()) + QLatin1Char(',');
        }
        key += QLatin1Char('\n');
        rows << live;
    }
    key += QString::fromUtf8(QJsonDocument(m_queueJobs).toJson(QJsonDocument::Compact));
    key += QString::fromUtf8(QJsonDocument(m_retainedTrees).toJson(QJsonDocument::Compact));
    key += QString::fromUtf8(QJsonDocument(m_mainRelease).toJson(QJsonDocument::Compact));
    key += m_queueProblem + m_mainMoved + (m_queueMode ? QStringLiteral("queue") : QStringLiteral("legacy"));
    m_integrationSummary->setVisible(onLive && m_queueMode);
    m_mainButton->setVisible(onLive && m_queueMode);
    if (m_queueMode) {
        const QJsonObject current = m_mainRelease.value(QStringLiteral("current")).toObject();
        const QString sha = current.value(QStringLiteral("sha")).toString();
        QString channel = sha.isEmpty() ? QStringLiteral("Runnable main: no installed release")
            : QStringLiteral("Runnable main %1").arg(sha.left(10));
        if (m_mainRelease.value(QStringLiteral("lag_commits")).isDouble())
            channel += QStringLiteral(" · %1 commits behind").arg(m_mainRelease.value(QStringLiteral("lag_commits")).toInt());
        const QString releaseError = m_mainRelease.value(QStringLiteral("error")).toString();
        if (!releaseError.isEmpty()) channel += QStringLiteral(" · ") + releaseError;
        if (!m_mainMoved.isEmpty()) channel += QStringLiteral(" · ") + m_mainMoved;
        if (!m_queueProblem.isEmpty()) channel += QStringLiteral(" · ") + m_queueProblem;
        m_integrationSummary->setText(channel);
        m_integrationSummary->setToolTip(channel);
        m_mainButton->setEnabled(!sha.isEmpty());
    }
    m_liveEmpty->setVisible(onLive && rows.isEmpty()
        && (!m_queueMode || (m_retainedTrees.isEmpty() && m_queueJobs.isEmpty())));
    if (key == m_liveKey)
        return;
    m_liveKey = key;
    while (QLayoutItem *item = m_liveRows->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    for (const Live &live : std::as_const(rows)) {
        auto *rowWidget = new QWidget(m_livePage);
        rowWidget->setObjectName(QStringLiteral("boardLiveRow"));
        auto *lines = new QVBoxLayout(rowWidget);
        lines->setContentsMargins(0, 0, 0, 0);
        lines->setSpacing(1);
        auto *row = new QHBoxLayout;
        row->setContentsMargins(8, 3, 8, 3);
        row->setSpacing(6);
        lines->addLayout(row);
        QString text = board::sessionChip(live.token, true);
        if (!live.model.isEmpty())
            text += QStringLiteral(" · ") + live.model;
        if (live.busy)
            text += QStringLiteral(" ✦");
        auto *paneChip = new QPushButton(text, rowWidget);
        paneChip->setObjectName(QStringLiteral("boardLivePaneChip"));
        paneChip->setProperty("token", live.token);
        paneChip->setCursor(Qt::PointingHandCursor);
        paneChip->setToolTip(QStringLiteral("%1%2 — reveal this pane")
                                 .arg(live.title.isEmpty() ? live.token.left(8) : live.title,
                                      live.busy ? QStringLiteral(" (working)") : QString()));
        const QString token = live.token;
        connect(paneChip, &QPushButton::clicked, this, [this, token] {
            if (onFocusPane)
                onFocusPane(token);
        });
        row->addWidget(paneChip);
        auto *title = new QLabel(rowWidget);
        title->setObjectName(QStringLiteral("boardLiveTitle"));
        const QString name = live.title.isEmpty() ? live.token.left(8) : live.title;
        title->setText(name.size() > 48 ? name.left(47) + QStringLiteral("…") : name);
        title->setToolTip(name);
        row->addWidget(title, 1);
        for (const QString &id : live.cards) {
            const board::Card *card = m_model.card(id);
            const QString cardTitle = card ? card->title : QString();
            const QString shortTitle =
                cardTitle.size() > 36 ? cardTitle.left(35) + QStringLiteral("…") : cardTitle;
            auto *cardChip = new QPushButton(
                shortTitle.isEmpty() ? QStringLiteral("#%1").arg(id)
                                     : QStringLiteral("#%1 %2").arg(id, shortTitle), rowWidget);
            cardChip->setObjectName(QStringLiteral("boardLiveCardChip"));
            cardChip->setProperty("card", id);
            cardChip->setCursor(Qt::PointingHandCursor);
            cardChip->setToolTip(
                QStringLiteral("#%1 %2 — open the card in this pane").arg(id, cardTitle));
            // Ctrl+click docks the card in its own pane (#HKY4); a plain click zooms the
            // list to it, which is what a Live chip has always meant.
            connect(cardChip, &QPushButton::clicked, this, [this, id] {
                if (QApplication::keyboardModifiers() == Qt::ControlModifier && openInOwnPane(id))
                    return;
                openCardSolo(id);
            });
            row->addWidget(cardChip);
        }
        const QString state = live.tree.value(QStringLiteral("state")).toString();
        if (!state.isEmpty() && state != QStringLiteral("legacy")) {
            const QString workspaceId = live.tree.value(QStringLiteral("workspace_id")).toString();
            QString detail = live.tree.value(QStringLiteral("branch")).toString();
            if (!workspaceId.isEmpty()) detail += QStringLiteral(" · workspace ") + workspaceId.left(8);
            if (state == QStringLiteral("refused"))
                detail = QStringLiteral("Workspace unavailable · ") + live.tree.value(QStringLiteral("reason")).toString();
            for (int index = m_queueJobs.size() - 1; index >= 0; --index) {
                const QJsonObject job = m_queueJobs.at(index).toObject();
                if (workspaceId.isEmpty() || job.value(QStringLiteral("workspace_id")).toString() != workspaceId) continue;
                detail += QStringLiteral(" · job %1: %2").arg(
                    job.value(QStringLiteral("id")).toString().left(8),
                    job.value(QStringLiteral("status")).toString());
                const QString reason = job.value(QStringLiteral("reason")).toString();
                if (!reason.isEmpty()) detail += QStringLiteral(" · ") + reason;
                break;
            }
            auto *metadata = new QLabel(detail, rowWidget);
            metadata->setObjectName(QStringLiteral("boardLiveWorkspace"));
            metadata->setToolTip(live.tree.value(QStringLiteral("execution_cwd")).toString() + QStringLiteral("\n") + detail);
            lines->addWidget(metadata);
        }
        m_liveRows->addWidget(rowWidget);
        rowWidget->show();
    }
    if (m_queueMode) {
        for (const QJsonValue &value : m_queueJobs) {
            const QJsonObject job = value.toObject();
            const QString id = job.value(QStringLiteral("id")).toString();
            if (id.isEmpty()) continue;
            QString detail = QStringLiteral("Job %1 · %2").arg(id.left(8), job.value(QStringLiteral("status")).toString());
            const QString card = job.value(QStringLiteral("card")).toString();
            if (!card.isEmpty()) detail += QStringLiteral(" · #%1").arg(card);
            if (job.value(QStringLiteral("age_seconds")).isDouble())
                detail += QStringLiteral(" · %1 s").arg(qRound64(job.value(QStringLiteral("age_seconds")).toDouble()));
            const QString reason = job.value(QStringLiteral("reason")).toString();
            if (!reason.isEmpty()) detail += QStringLiteral(" · ") + reason;
            auto *label = new QLabel(detail, m_livePage);
            label->setObjectName(QStringLiteral("boardQueueJob"));
            m_liveRows->addWidget(label);
            label->show();
        }
        for (const QJsonValue &value : m_retainedTrees) {
            const QJsonObject tree = value.toObject();
            const QString state = tree.value(QStringLiteral("state")).toString();
            const QString id = tree.value(QStringLiteral("workspace_id")).toString();
            if (state == QStringLiteral("removed") || id.isEmpty()) continue;
            bool shown = false;
            for (const Live &live : std::as_const(rows))
                if (live.tree.value(QStringLiteral("workspace_id")).toString() == id) { shown = true; break; }
            if (shown) continue;
            QString detail = QStringLiteral("Workspace %1 · %2 · %3")
                .arg(id.left(8), tree.value(QStringLiteral("branch")).toString(), state);
            if (tree.value(QStringLiteral("unlanded")).toBool()
                || state == QStringLiteral("released") || state == QStringLiteral("retained"))
                detail += QStringLiteral(" · work retained");
            const QString error = tree.value(QStringLiteral("reason")).toString();
            if (!error.isEmpty()) detail += QStringLiteral(" · ") + error;
            auto *label = new QLabel(detail, m_livePage);
            label->setObjectName(QStringLiteral("boardRetainedWorkspace"));
            label->setToolTip(tree.value(QStringLiteral("execution_cwd")).toString());
            m_liveRows->addWidget(label);
            label->show();
        }
    }
}

void BoardView::buildBackgroundPage(QVBoxLayout *layout)
{
    m_backgroundPage = new QWidget(this);
    m_backgroundPage->setObjectName(QStringLiteral("boardBackgroundPage"));
    m_backgroundPage->hide();
    auto *column = new QVBoxLayout(m_backgroundPage);
    column->setContentsMargins(0, 6, 0, 0);
    column->setSpacing(4);
    m_backgroundEmpty = new QLabel(QStringLiteral("No background work on this project."), m_backgroundPage);
    m_backgroundEmpty->setObjectName(QStringLiteral("boardBackgroundEmpty"));
    column->addWidget(m_backgroundEmpty);
    auto *rows = new QWidget(m_backgroundPage);
    m_backgroundRows = new QVBoxLayout(rows);
    m_backgroundRows->setContentsMargins(0, 0, 0, 0);
    m_backgroundRows->setSpacing(4);
    column->addWidget(rows);
    column->addStretch(1);
    layout->addWidget(m_backgroundPage, 1);
}

void BoardView::syncBackgroundPage()
{
    if (!m_backgroundPage) return;
    const bool shown = !m_pinned && !m_sectionsOpen && !detailOpen() && !signalOpen()
                       && m_page == Page::Background;
    m_backgroundPage->setVisible(shown);
    const QJsonArray panes = shown && backgroundPanes ? backgroundPanes() : QJsonArray{};
    QString key;
    QJsonArray rows;
    for (const QJsonValue &value : panes) {
        const QJsonObject pane = value.toObject();
        const QString token = pane.value(QStringLiteral("token")).toString();
        if (token.isEmpty()) continue;
        rows.append(pane);
        key += token + QLatin1Char('|') + pane.value(QStringLiteral("title")).toString()
            + QLatin1Char('|') + pane.value(QStringLiteral("model")).toString()
            + QLatin1Char('|') + pane.value(QStringLiteral("state")).toString() + QLatin1Char('\n');
    }
    m_backgroundEmpty->setVisible(shown && rows.isEmpty());
    if (key == m_backgroundKey) return;
    m_backgroundKey = key;
    while (QLayoutItem *item = m_backgroundRows->takeAt(0)) {
        if (QWidget *widget = item->widget()) widget->deleteLater();
        delete item;
    }
    for (const QJsonValue &value : rows) {
        const QJsonObject pane = value.toObject();
        const QString token = pane.value(QStringLiteral("token")).toString();
        const QString title = pane.value(QStringLiteral("title")).toString();
        const QString model = pane.value(QStringLiteral("model")).toString();
        const QString state = pane.value(QStringLiteral("state")).toString();
        const QString stateLabel = state == QLatin1String("interrupted") ? QStringLiteral("Interrupted")
            : state == QLatin1String("needs-you") ? QStringLiteral("Needs you")
            : state == QLatin1String("done") ? QStringLiteral("Done")
            : state == QLatin1String("failed") ? QStringLiteral("Failed")
            : QStringLiteral("Working");
        auto *rowWidget = new QWidget(m_backgroundPage);
        rowWidget->setObjectName(QStringLiteral("boardBackgroundRow"));
        auto *row = new QHBoxLayout(rowWidget);
        row->setContentsMargins(8, 3, 8, 3);
        row->setSpacing(6);
        auto *name = new QLabel(title.isEmpty() ? token.left(8) : title, rowWidget);
        name->setObjectName(QStringLiteral("boardBackgroundTitle"));
        name->setToolTip(title);
        row->addWidget(name, 1);
        if (!model.isEmpty()) row->addWidget(new QLabel(model, rowWidget));
        auto *status = new QLabel(stateLabel, rowWidget);
        status->setObjectName(QStringLiteral("boardBackgroundState"));
        row->addWidget(status);
        auto *open = new QPushButton(QStringLiteral("Reopen"), rowWidget);
        open->setObjectName(QStringLiteral("boardBackgroundOpen"));
        open->setProperty("token", token);
        connect(open, &QPushButton::clicked, this, [this, token] {
            if (onFocusBackground) onFocusBackground(token);
        });
        row->addWidget(open);
        m_backgroundRows->addWidget(rowWidget);
        rowWidget->show();
    }
}

// Wide enough, the open card sits beside the list; in a narrow pane it takes the whole pane
// (the list comes back when it closes) instead of squeezing the rows to a sliver.
void BoardView::updateDetailLayout()
{
    // The key line says what the keys do in what is on screen: the list, or a card that has the
    // pane to itself.
    static const QString boardKeys = QStringLiteral(
        "<b>Enter</b> open &nbsp; <b>Shift+Enter</b> own pane &nbsp; <b>e</b> edit &nbsp; <b>p</b> plan &nbsp; <b>r</b> run &nbsp; "
        "<b>v</b> verify &nbsp; <b>d</b> done &nbsp; "
        "<b>n</b> new &nbsp; <b>←/→</b> fold section &nbsp; "
        "<b>Alt+Shift+↑↓</b> reorder &nbsp; <b>Alt+Shift+←→</b> status &nbsp; <b>m</b> move "
        "&nbsp; <b>Del</b> delete &nbsp; <b>/</b> or <b>Esc</b> filter &nbsp; <b>a</b> ask the agent &nbsp; "
        "<b>t</b> #ID to prompt &nbsp; <b>y</b> copy &nbsp; "
        "<b>o</b> file &nbsp; <b>Ctrl+Z</b> undo");
    static const QString cardKeys = QStringLiteral(
        "<b>Esc</b> back to the board &nbsp; <b>e</b> edit &nbsp; <b>d</b> done &nbsp; <b>Tab</b> reply &nbsp; "
        "<b>Enter</b> discuss &nbsp; <b>p</b> or <b>Ctrl+Enter</b> plan &nbsp; <b>r</b> run "
        "&nbsp; <b>v</b> verify &nbsp; <b>Del</b> delete &nbsp; <b>Ctrl+Shift+Enter</b> comment only");
    // A link's reveal (#K4SQ) gives the open page the pane to itself at any width; otherwise only
    // a narrow pane stacks the page over the list.
    // A pinned card pane (#Y2BA) says the same, except that Esc closes the pane.
    static const QString pinnedKeys = QString(cardKeys).replace(
        QStringLiteral("<b>Esc</b> back to the board"), QStringLiteral("<b>Esc</b> close pane"));
    if (m_detail)
        m_detail->setPopOutVisible(bool(onOpenInNewPane) && !m_pinned);
    const bool stacked = width() < board::kCardSplitWidth || m_soloReveal || m_pinned;
    const QString keys = m_pinned ? pinnedKeys : detailOpen() && stacked ? cardKeys : boardKeys;
    // Wherever the board's own line is the one on screen, it ends with the Switchboard agent's
    // action row — read off the context's own `actions()` (agentActionKeyLine) rather than
    // written out above, so a session that puts a keyed button there gets its entry in the line
    // for free and a keyless one adds nothing.
    const QString line = keys == boardKeys ? keys + agentActionKeyLine() : keys;
    if (m_keys->text() != line)
        m_keys->setText(line);
    // A signal's page (#AQ6X) has four actions and no thread, so a stacked pane says its own line
    // rather than the card's. Both pages live in the same half of the splitter and only one is
    // ever up (openSignal and the `board_card` handler hide the other), so "a page is open" is
    // the two together, and everything below reads `pageOpen` rather than `detailOpen()`.
    static const QString signalKeys = QStringLiteral(
        "<b>Esc</b> back to the board &nbsp; <b>Claim</b> take it &nbsp; <b>Release</b> give it "
        "back &nbsp; <b>Dismiss</b> with a reason and an expiry &nbsp; <b>Promote</b> open a card");
    const bool pageOpen = detailOpen() || signalOpen();
    if (signalOpen() && stacked && m_keys->text() != signalKeys)
        m_keys->setText(signalKeys);
    // The header exists only while a page is open, and then it says one thing: the way back. A
    // pinned pane (#Y2BA) keeps it while its card is still loading, and never shows its list.
    if (m_head->isHidden() == (pageOpen || m_pinned)) {
        m_head->setVisible(pageOpen || m_pinned);
        applyRightInset();
    }
    syncChatVisible();
    if (!pageOpen) {
        m_listPane->setVisible(!m_pinned);
        placeNotice();      // back to the bottom of the list
        return;
    }
    const bool wasStacked = m_listPane->isHidden();
    m_listPane->setVisible(!stacked);
    placeNotice();          // the top of the pane while the page has it, the bottom otherwise
    if (stacked)
        return;
    if (!m_detailSized || wasStacked || m_sizedForSignal != signalOpen()) {
        const int total = qMax(1, m_splitter->width());
        const int detail = qBound(360, total * 45 / 100, total - 300);
        // Three children now (the list, the card, the signal): the one that is away gets nothing,
        // so switching between the two pages never leaves a sliver of the other one.
        m_splitter->setSizes({total - detail, signalOpen() ? 0 : detail,
                              signalOpen() ? detail : 0});
        m_detailSized = true;
        m_sizedForSignal = signalOpen();
    }
}

bool BoardView::detailOpen() const
{
    return !m_detail->isHidden();
}

// ------------------------------------------------------------------------- actions

void BoardView::moveCard(const QString &id, const QString &columnId, const QString &beforeId,
                         const QString &afterId)
{
    if (id.isEmpty())
        return;
    // Verified is not a status, it is a fact the worker stamped (#T71W). Dropping a card in would
    // have to invent a signature, so the drop is refused here rather than sent and refused there,
    // and the line says the one way in. Reordering *inside* Verified is fine.
    const board::Card *moving = m_model.card(id);
    if (columnId == board::verifiedSection()
        && !(moving && m_model.sectionOf(*moving) == columnId)) {
        showNotice(QStringLiteral("A card is verified by closing it from a QA lane with a "
                                  "different model."),
                   true);
        return;
    }
    const QString requestId = nextRequestId();
    QJsonObject message{{QStringLiteral("type"), QStringLiteral("board_move")},
                        {QStringLiteral("id"), requestId},
                        {QStringLiteral("card"), id},
                        {QStringLiteral("reason"), QStringLiteral("moved in the Board")}};
    const QString status = m_model.dropStatus(columnId);
    const bool sameSection = moving && m_model.sectionOf(*moving) == columnId;
    if (sameSection && m_model.sort() != board::Sort::Manual) {
        showNotice(QStringLiteral("The list is sorted by %1, so reordering is off. Click that "
                                  "column's header again for the board's own order.")
                       .arg(board::sortTitle(m_model.sort()).toLower()),
                   true);
        return;
    }
    QString note;
    if (!status.isEmpty() && !sameSection) {
        // Within its own section a card keeps its exact status (Needs QA stays LLM or human),
        // and a move to a status section takes it out of the manual section it may have been
        // parked in: an empty `section` is the unpark (#3XZV).
        message.insert(QStringLiteral("status"), status);
        message.insert(QStringLiteral("section"), QString());
        note = QStringLiteral("Moved #%1 to %2").arg(id, sectionTitle(columnId));
    } else if (status.isEmpty() && !sameSection) {
        // A manual section: the drop parks the card there and its status stays what it was.
        message.insert(QStringLiteral("section"), columnId);
        note = QStringLiteral("Moved #%1 to %2").arg(id, sectionTitle(columnId));
    } else {
        note = QStringLiteral("Reordered #%1").arg(id);
    }
    if (!beforeId.isEmpty())
        message.insert(QStringLiteral("before"), beforeId);
    if (!afterId.isEmpty())
        message.insert(QStringLiteral("after"), afterId);
    m_pendingNotes.insert(requestId, note);
    send(message);
}

void BoardView::moveToTab(const QString &id, const QString &tabId)
{
    if (id.isEmpty() || tabId.isEmpty())
        return;
    const QString requestId = nextRequestId();
    m_pendingNotes.insert(requestId, QStringLiteral("Moved #%1 to %2").arg(
                                         id, m_model.tab(tabId) ? m_model.tab(tabId)->title : tabId));
    send({{QStringLiteral("type"), QStringLiteral("board_move")}, {QStringLiteral("id"), requestId},
          {QStringLiteral("card"), id}, {QStringLiteral("tab"), tabId},
          {QStringLiteral("reason"), QStringLiteral("moved in the Board")}});
}

void BoardView::doneSelected()
{
    if (detailOpen() && m_detail->editing())
        return;
    const QString card = detailOpen() ? m_detail->cardId() : m_selected;
    const board::Card *item = m_model.card(card);
    if (!item || item->status == QStringLiteral("done"))
        return;
    const QString requestId = nextRequestId();
    m_pendingNotes.insert(requestId, QStringLiteral("Marked #%1 done").arg(card));
    send({{QStringLiteral("type"), QStringLiteral("board_move")},
          {QStringLiteral("id"), requestId}, {QStringLiteral("card"), card},
          {QStringLiteral("status"), QStringLiteral("done")},
          {QStringLiteral("section"), QString()},
          {QStringLiteral("reason"), QStringLiteral("marked done in the Board")}});
}

void BoardView::undoLast()
{
    if (m_lastWrite.isEmpty()) {
        showNotice(QStringLiteral("Nothing to undo."), false);
        return;
    }
    send({{QStringLiteral("type"), QStringLiteral("board_undo")},
          {QStringLiteral("write_id"), m_lastWrite}});
}

void BoardView::quickAdd()
{
    // Inbox by default (owner, #5G43): `n` and the toolbar button create in the board's intake
    // section, never in whatever section happens to hold the selection — with an in-progress card
    // selected, every new card was starting life in In progress. A section's own + button is the
    // way to add straight into that section.
    QString intake;
    for (const QString &id : columnIds()) {
        if (!sectionTakesNewCards(id))
            continue;
        if (intake.isEmpty())
            intake = id;      // a board reordered to have no Inbox: its first section
        if (m_model.dropStatus(id) == QStringLiteral("inbox")) {
            intake = id;
            break;
        }
    }
    quickAddIn(intake);
}

// The field sits over the list rather than inside a section: with one long list, a field at a
// section's head would be scrolled out of sight as often as not. It names the section it adds to,
// and it takes the card's *title*. Enter opens the pre-save review, where the owner can write
// the issue and compare suggestions before the card is created (#5KMQ).
void BoardView::quickAddIn(const QString &columnId)
{
    if (!m_open)
        return;
    if (m_model.total() == 0) {
        // The empty board hides the list; show it so the field has somewhere to go.
        m_empty->hide();
        m_emptyRetryRow->hide();
        m_splitter->show();
    }
    QString id = columnId;
    if (id.isEmpty() || !sectionTakesNewCards(id))
        id = columnIds().value(0);
    if (id.isEmpty())
        return;
    m_quickAddColumn = id;
    m_quickAdd->setPlaceholderText(QStringLiteral("Title of a new card in %1 — Enter reviews it, Esc closes")
                                       .arg(sectionTitle(id)));
    if (m_quickAddRow->isHidden()) {
        QSignalBlocker block(m_quickAddTab);
        m_quickAddTab->clear();
        for (const board::Tab &tab : m_model.tabs())
            if (!tab.folder.isEmpty()) m_quickAddTab->addItem(tab.title, tab.id);
        const int defaultIndex = m_quickAddTab->findData(defaultCategory());
        m_quickAddTab->setCurrentIndex(defaultIndex >= 0 ? defaultIndex : 0);
        m_quickAddTabTouched = false;
        m_quickAddLabelsTouched = false;
    }
    m_quickAddRow->show();
    m_quickAdd->setFocus();
}

void BoardView::closeQuickAdd()
{
    if (!m_quickAddCreateRequest.isEmpty())
        return;
    m_quickAddCreateRequest.clear();
    m_quickAddSave->setEnabled(true);
    m_quickAddCancel->setEnabled(true);
    m_quickAddTriageTimer->stop();
    m_quickAddTriageRequest.clear();
    m_quickAddTriageStatus->clear();
    m_quickAddTriageStatus->hide();
    m_quickAddChosenRelated.clear();
    m_quickAddDuplicates = {};
    m_quickAddRelated = {};
    m_quickAdd->clear();
    m_quickAddIssue->clear();
    m_quickAddLabels->clear();
    m_quickAddReview->hide();
    m_quickAddRow->hide();
    m_quickAddSuggestions->hide();
}

void BoardView::openSelected()
{
    if (m_selected.isEmpty())
        return;
    if (detailOpen() && m_detail->cardId() == m_selected) {
        updateDetailLayout();
        return;
    }
    // A card being edited is not swapped for another one under the typing: the selection may
    // move on the list, but the open card stays until the edit is saved or dropped. Whatever was
    // queued for the card that is not going to open goes with it, rather than waiting to happen
    // to the next card that does.
    if (detailOpen() && m_detail->editing()) {
        m_editOnOpen = false;
        m_editOnOpenFresh = false;
        m_replyOnOpen = false;
        m_actionOnOpen.clear();
        return;
    }
    stampViewed(m_selected);   // local only (#FKSN): nothing goes to the worker or the card file
    QJsonObject request{{QStringLiteral("type"), QStringLiteral("board_card_get")},
                        {QStringLiteral("card"), m_selected}};
    if (!m_open) {
        m_directCardRequest = nextRequestId();
        request.insert(QStringLiteral("id"), m_directCardRequest);
    }
    send(request);
}

// `e`, the Edit button, a click on the title or a double-click in the text: the card's own words
// become fields, and Save sends one `board_update` for the title and the `## Issue` text
// together (protocol 19.3). The GUI never writes the file; the worker does, hash-checked against
// the version this card was read at, so an edit made elsewhere meanwhile is never lost silently.
void BoardView::editSelected()
{
    if (detailOpen() && (m_selected.isEmpty() || m_detail->cardId() == m_selected)) {
        const bool fresh = m_editOnOpenFresh;
        m_editOnOpenFresh = false;
        m_detail->beginEdit(false, fresh);
        return;
    }
    if (m_selected.isEmpty())
        return;
    m_editOnOpen = true;
    openSelected();
}

// `p` / `r`: Plan or Run the open card, or the selected one once it has been read (the
// buttons need its sections and front matter to know whether it has a plan).
void BoardView::cardAction(const QString &action)
{
    if (!(detailOpen() && (m_selected.isEmpty() || m_detail->cardId() == m_selected))) {
        if (m_selected.isEmpty())
            return;
        m_actionOnOpen = action;
        openSelected();
        return;
    }
    if (action == QStringLiteral("plan"))
        m_detail->plan();
    else if (action == QStringLiteral("refine"))
        m_detail->refine();
    else if (action == QStringLiteral("verify"))
        m_detail->verify();
    else
        m_detail->execute();
}

// Execute (#XS6Q): the card goes to a terminal pane's agent. The window opens the pane
// (`onExecuteCard`), whose agent gets the card attached and the task text that tells it the
// board's conventions (board::executeTask), and the pane's session token comes back.
//
// With a token the board records the hand-off as a **claim** (#R9G7): one `board_claim` on the
// worker, which is the three writes Execute used to send by hand — assignee agent, the move to
// Executing, and the progress entry naming the pane — plus the `session` field in the card's
// front matter, so the Switchboard shows who has the card and another agent reading the board
// sees it is taken. Without a token the window could not open a pane, and the old three writes
// stand: the card still records the hand-off, it just has no pane to name.
//
// Nothing here is a model turn, so the Switchboard worker stays free for the next Discuss or Plan.
void BoardView::executeCard(const QString &note, bool background)
{
    const QString card = m_detail->cardId();
    if (card.isEmpty())
        return;
    if (!onExecuteCard) {
        m_detail->showError(QStringLiteral("This window cannot open a terminal pane for the card."));
        return;
    }
    const QJsonObject front = m_detail->front();
    // Read before the pane is opened: opening one spins the event loop, and the update below is
    // hash-checked against the version the card was read at.
    const QString baseHash = m_detail->hash();
    const QString status = m_detail->status();
    // The pane opens before anything is written, because what is written names it (#HKAP): the
    // claim, and the progress entry it makes, read "Claimed (<first 8 characters>) · …" and
    // draw as a link that reveals that pane.
    const QString paneToken = onExecuteCard(card, board::executeTask(card, m_detail->title(), m_detail->hasPlan(),
                                                                     m_detail->hasAcceptance(), note), background);
    if (!paneToken.isEmpty()) {
        const QString id = nextRequestId();
        m_pendingNotes.insert(id, QStringLiteral("Claimed #%1 · Run").arg(card));
        send({{QStringLiteral("type"), QStringLiteral("board_claim")}, {QStringLiteral("id"), id},
              {QStringLiteral("card"), card}, {QStringLiteral("pane_token"), paneToken},
              {QStringLiteral("text"), note}});
        return;
    }
    // No pane: the writes Execute has always made, in this order on the worker's stdin — the
    // hash-checked update first, then the move (which re-reads the file), then the note.
    if (front.value(QStringLiteral("assignee")).toString() != QStringLiteral("agent"))
        send({{QStringLiteral("type"), QStringLiteral("board_update")}, {QStringLiteral("id"), nextRequestId()},
              {QStringLiteral("card"), card}, {QStringLiteral("base_hash"), baseHash},
              {QStringLiteral("patch"), QJsonObject{{QStringLiteral("fields"),
                                                     QJsonObject{{QStringLiteral("assignee"), QStringLiteral("agent")}}}}}});
    if (status != QStringLiteral("executing") && status != QStringLiteral("in-progress")) {
        const QString id = nextRequestId();
        m_pendingNotes.insert(id, QStringLiteral("Moved #%1 to Running · Run").arg(card));
        send({{QStringLiteral("type"), QStringLiteral("board_move")}, {QStringLiteral("id"), id},
              {QStringLiteral("card"), card}, {QStringLiteral("status"), QStringLiteral("executing")},
              {QStringLiteral("reason"), QStringLiteral("Run: handed to a terminal pane")}});
    }
    // No pane to name, so no `pane_token` and nothing for the thread to link to: the plain
    // Execute wording, which is what the entry said before panes were linked at all.
    const QString entry =
            QStringLiteral("Run · handed to a new terminal pane beside the Board, whose "
                           "agent works on it and records its commits in `links.commits`.");
    send({{QStringLiteral("type"), QStringLiteral("board_comment")}, {QStringLiteral("card"), card},
          {QStringLiteral("kind"), QStringLiteral("progress")},
          {QStringLiteral("text"), entry + (note.isEmpty() ? QString()
                                                           : QStringLiteral("\n\n") + note)}});
}

// Resume card (#FYEY): what Run does — the card goes to a terminal pane — but the pane's first
// prompt is the orphan listing `land.py orphans` gave for this card plus the landing steps,
// because the salvage reads the card's `## Done means` before it lands or abandons anything.
// The claim is Execute's pane claim with its own wording. No pane to open writes nothing: the
// orphan hunks stay exactly where they are and the card stays unclaimed.
void BoardView::resumeCard(const QString &task, const QString &note)
{
    const QString card = m_detail->cardId();
    if (card.isEmpty())
        return;
    if (!onExecuteCard) {
        m_detail->showError(QStringLiteral("This window cannot open a terminal pane for the card."));
        return;
    }
    const QString paneToken = onExecuteCard(card, task, true);
    if (paneToken.isEmpty())
        return;
    const QString id = nextRequestId();
    m_pendingNotes.insert(id, QStringLiteral("Claimed #%1 · Resume").arg(card));
    send({{QStringLiteral("type"), QStringLiteral("board_claim")}, {QStringLiteral("id"), id},
          {QStringLiteral("card"), card}, {QStringLiteral("pane_token"), paneToken},
          {QStringLiteral("text"), note}});
}

// Verify (#T71W): the card goes to a terminal pane on the verifier the worker recommends — a
// different provider family from the one that implemented it. The board records the hand-off the
// way Execute does, minus the two writes that would be wrong here: the card keeps its QA status
// (only the verdict moves it) and its assignee (the implementer is still the implementer). Then
// the window opens the pane on that runner (`onVerifyCard`), whose agent — or guest CLI — gets the
// QA brief (board::verifyTask).
void BoardView::verifyCard(const QString &note)
{
    const QString card = m_detail->cardId();
    if (card.isEmpty())
        return;
    if (!onVerifyCard) {
        m_detail->showError(QStringLiteral("This window cannot open a terminal pane for the card."));
        return;
    }
    const QJsonObject qa = m_detail->qa();
    const QString runner = board::verifyRunner(qa);
    if (runner.isEmpty())
        return;                       // CardDetail::verify() has already said why on the card
    const QString label = board::verifyLabel(qa);
    const QString why = qa.value(QStringLiteral("recommended")).toObject()
                            .value(QStringLiteral("why")).toString();
    // The canonical signature is the `qa` block's; the card's own field is the fallback for a
    // worker that sends no `qa` (and then there is no runner either, so this is belt and braces).
    QString implementedBy = qa.value(QStringLiteral("implemented_by")).toString();
    if (implementedBy.isEmpty())
        implementedBy = m_detail->front().value(QStringLiteral("implemented_by")).toString();
    // The pane opens before the note is sent (#HKAP), because the note carries its session
    // token — the same treatment as Execute's entry: "Verifying (<first 8 characters>) · …"
    // links to the verifier's pane. Without a token, the plain Verify wording stays.
    const QString paneToken = onVerifyCard(card, runner,
                                           board::verifyTask(card, m_detail->title(), label, implementedBy,
                                                             m_detail->status(), note));
    QString text = paneToken.isEmpty()
            ? QStringLiteral("Verify · handed to a new terminal pane on %1").arg(label)
            : QStringLiteral("Verifying (%1) · handed to a new terminal pane on %2")
                      .arg(paneToken.left(8), label);
    if (!why.isEmpty())
        text += QStringLiteral(" · ") + why;
    if (!note.isEmpty())
        text += QStringLiteral("\n\n") + note;
    send({{QStringLiteral("type"), QStringLiteral("board_comment")}, {QStringLiteral("card"), card},
          {QStringLiteral("kind"), QStringLiteral("progress")},
          {QStringLiteral("pane_token"), paneToken},
          {QStringLiteral("text"), text}});
}

void BoardView::saveCardEdit(const QJsonObject &patch, const QString &baseHash)
{
    const QString card = m_detail->cardId();
    if (card.isEmpty() || patch.isEmpty())
        return;
    if (baseHash.isEmpty()) {
        showNotice(QStringLiteral("#%1 cannot be saved: reopen the card and try again.").arg(card), true);
        return;
    }
    const QString id = nextRequestId();
    m_pendingNotes.insert(id, QStringLiteral("Saved #%1").arg(card));
    send({{QStringLiteral("type"), QStringLiteral("board_update")}, {QStringLiteral("id"), id},
          {QStringLiteral("card"), card}, {QStringLiteral("base_hash"), baseHash},
          {QStringLiteral("patch"), patch}});
}

void BoardView::closeDetail()
{
    // A pinned pane has no list to go back to (#Y2BA): closing its page closes the pane. Every way
    // out comes through here — Esc, the page's ×, the header's button, the card being removed.
    if (m_pinned && onClosePane) {
        onClosePane();
        return;
    }
    m_soloReveal = false;   // back to the list, so the list is what comes back (#K4SQ)
    m_follow->stop();
    m_detail->hide();
    m_signalDetail->hide();
    if (m_directCard) {
        m_directCard = false;
        m_open = false;
        rebuild();
        reload();
    }
    if (onNavigationChanged) onNavigationChanged();
    updateDetailLayout();
    applyPage();
    syncLivePage();
    syncBackgroundPage();
    watchCardFiles();   // the card that was open no longer needs a watch of its own (#N5JJ)
    focusInput();
}

void BoardView::focusFilter()
{
    // A pinned card pane has no list page and so no filter (#Y2BA).
    if (m_pinned)
        return;
    // The filter is the list page's, so `/` from a card that has the pane to itself goes back to
    // the board first rather than typing into a box nobody can see.
    if (m_listPane->isHidden())
        closeDetail();
    m_filter->setFocus();
    m_filter->selectAll();
}

void BoardView::openFind()
{
    // `find.inView` (Ctrl+F) with the keyboard on the board (#9NBZ): an open card is a
    // document that can scroll for screens, so that page finds inside its own document; the
    // list page's find is the filter, which `/` already focuses — same key, either page.
    if (detailOpen())
        m_detail->openFind();
    else
        focusFilter();
}

void BoardView::selectCard(const QString &id)
{
    m_selected = id;
    m_selectedFold.clear();
    m_selectedSignal.clear();
    m_selectedSignalFold.clear();
    // A card the fold row is holding cannot be stood on until the row is open (#93WR), so a card
    // reached by id — a `#ID` in a card's or a thread's text, a link from the helper, the cleanup
    // panel's anchors — opens what holds it: its self-closed group, and the section around it.
    // Only for a self-closed card: every other card is where it always was.
    if (board::rowOfCard(m_rows, id) < 0) {
        const board::Card *card = m_model.card(id);
        const QString section = card ? m_model.sectionOf(*card) : QString();
        if (card && !section.isEmpty() && m_hidden.contains(section)) {
            // The closed stages are unticked by default (owner, 2026-09-25: "make verified,
            // done, dropped unchecked by default"), so a card reached by id in one ticks its
            // section back on and unfolds it — a reference finds what it names, whatever holds it.
            m_hidden.remove(section);
            m_collapsedSeeded = true;
            m_collapsed.remove(section);
            rebuild();
        }
        if (card && board::selfClosed(*card) && !section.isEmpty()) {
            bool opened = false;
            if (!m_selfClosedOpen.contains(section)) {
                m_selfClosedOpen.insert(section);
                opened = true;
            }
            if (m_collapsed.contains(section)) {
                m_collapsedSeeded = true;
                m_collapsed.remove(section);
                opened = true;
            }
            if (opened)
                rebuild();
        }
    }
    const int at = board::rowOfCard(m_rows, id);
    if (at >= 0)
        selectRow(at);
}

void BoardView::copyReference()
{
    copyCardReference(m_selected);
}

// Card references retain their # prefix, independently of label copying (#S53Z).
void BoardView::copyCardReference(const QString &id)
{
    if (id.isEmpty())
        return;
    QApplication::clipboard()->setText(QStringLiteral("#") + id);
    showNotice(QStringLiteral("Copied #%1").arg(id), false);
    toast(QStringLiteral("#%1 copied").arg(id));
}

// A label badge or meta label copies a term the filter understands (#S53Z).
void BoardView::copyTag(const QString &tag)
{
    if (tag.isEmpty())
        return;
    const QString term = QStringLiteral("label:") + tag;
    QApplication::clipboard()->setText(term);
    showNotice(QStringLiteral("Copied %1").arg(term), false);
    toast(QStringLiteral("%1 copied").arg(term));
}

// Zoom to a card by id (#3ZAP): a `#ID` reference in a card's text takes the same path the
// cleanup panel's `card:` anchors do.
QJsonObject BoardView::navigationState() const
{
    if (!m_restoreNavigation.isEmpty())
        return m_restoreNavigation;
    return {{QStringLiteral("filter"), m_filter->text()},
            {QStringLiteral("selected"), m_selected},
            {QStringLiteral("card"), detailOpen() ? m_detail->cardId() : QString()},
            // The object tab (#9FX8, #C52H): which of Cards | Skills | Memories | Live | Background the pane was on.
            // Persisted per pane — and panes are per board — so each board comes back on its own.
            {QStringLiteral("page"), int(m_page)}};
}

void BoardView::restoreNavigation(const QJsonObject &state)
{
    if (!m_open) {
        m_restoreNavigation = state;
        return;
    }
    m_restoreNavigation = {};
    m_filter->setText(state.value(QStringLiteral("filter")).toString());
    const QString selected = state.value(QStringLiteral("selected")).toString();
    if (m_model.card(selected))
        selectCard(selected);
    const QString card = state.value(QStringLiteral("card")).toString();
    if (m_model.card(card))
        openCard(card);
    const int page = state.value(QStringLiteral("page")).toInt();
    if (page > int(Page::Cards) && page <= int(Page::Background))
        setPage(static_cast<Page>(page));
}

void BoardView::openCard(const QString &id)
{
    if (id.isEmpty())
        return;
    // A pinned pane is always one card's (#Y2BA). Link clicks there dock a new pane
    // (openCardFromClick); this path is pinSolo's and a restore's.
    if (m_pinned)
        m_pinnedCard = id;
    selectCard(id);
    m_selected = id;
    openSelected();
}

// A card link clicked outside the board (#K4SQ) — a `#ID` in chat or a notification — opens the
// board on the card alone: the reveal marks the pane solo and openCard opens the card, so the
// page that lands takes the whole pane at any width. Only closing the page (closeDetail,
// closeSignal) drops the solo; opening another card from the one on screen keeps it, so an
// in-board link does not pop the list back in beside the next card.
void BoardView::openCardSolo(const QString &id)
{
    if (id.isEmpty())
        return;
    m_soloReveal = true;
    openCard(id);
}

// A solo card pane (#Y2BA): openCardSolo for good. The reveal flag keeps the page stacked at any
// width, `m_pinned` keeps the list from ever coming back, and closeDetail hands the close to the
// window. Called again on a pinned pane it simply moves the pane to that card.
void BoardView::pinSolo(const QString &id)
{
    if (id.isEmpty())
        return;
    m_pinned = true;
    m_pinnedCard = id;
    if (onTitleChanged)
        onTitleChanged(title());
    // The tree watch is the list's (watchIssues): a card pane keeps the board folder itself, for a
    // card file that is replaced, and its own card's files (watchCardFiles).
    if (m_watcher) {
        QStringList drop = m_watcher->directories();
        drop.removeAll(m_root.isEmpty() ? projects::boardDirOf(m_workspace) : m_root);
        if (!drop.isEmpty())
            m_watcher->removePaths(drop);
    }
    m_back->setText(QStringLiteral("×  Close pane (Esc)"));
    m_back->setToolTip(QStringLiteral("Close this card's pane (Esc)"));
    closeQuickAdd();
    openCardSolo(id);
    updateDetailLayout();
    syncLivePage();   // a pinned card pane never shows the Live tab (#Y2BA, #C52H)
    syncBackgroundPage();
    if (onNavigationChanged) onNavigationChanged();
}

void BoardView::sendSelectionToTerminal()
{
    if (m_selected.isEmpty() || !onSendToTerminal)
        return;
    onSendToTerminal(QStringLiteral("#") + m_selected + QLatin1Char(' '));
}

void BoardView::openSelectedFile()
{
    const board::Card *card = m_model.card(m_selected);
    if (card && onOpenFile && !card->path.isEmpty())
        onOpenFile(m_workspace + QLatin1Char('/') + card->path);
}

void BoardView::moveSelected()
{
    if (m_selected.isEmpty())
        return;
    QMenu menu(this);
    const QList<board::Column> columns = m_model.sections();
    const board::Card *card = m_model.card(m_selected);
    const QString here = card ? m_model.sectionOf(*card) : QString();
    int index = 1;
    for (const board::Column &column : columns) {
        QAction *action = menu.addAction(QStringLiteral("&%1  %2").arg(index++).arg(column.title));
        action->setEnabled(column.id != here);
        const QString id = column.id;
        connect(action, &QAction::triggered, this, [this, id] { moveCard(m_selected, id, {}, {}); });
    }
    menu.addSeparator();
    for (const board::Tab &tab : m_model.tabs()) {
        if (tab.folder.isEmpty() || (card && tab.id == card->tab))
            continue;
        QAction *action = menu.addAction(QStringLiteral("Move to %1").arg(tab.title));
        const QString id = tab.id;
        connect(action, &QAction::triggered, this, [this, id] { moveToTab(m_selected, id); });
    }
    menu.addSeparator();
    // The `m` menu is where the owner looked for a way to close a card and found none (#CYM9):
    // delete sits at its foot, destructive and labelled so, below every move.
    QAction *remove = menu.addAction(QStringLiteral("Delete card…"));
    connect(remove, &QAction::triggered, this, [this] {
        if (onHint)
            onHint(QStringLiteral("board.delete"), QStringLiteral("Del"));
        deleteSelected();
    });
    // Under the selected row rather than wherever the mouse happens to be.
    QPoint at = QCursor::pos();
    if (QListWidgetItem *current = m_list->currentItem())
        at = m_list->viewport()->mapToGlobal(m_list->visualItemRect(current).bottomLeft());
    menu.exec(at);
}

void BoardView::deleteSelected()
{
    deleteCard(m_selected);
}

// The owner's delete (card #CYM9): confirm, then one `board_delete` message; the worker takes
// the file and its thread off disk and the write's Undo puts them back. The card is remembered
// as ours until the write lands, so the removal events that follow do not talk over the toast.
void BoardView::deleteCard(const QString &id)
{
    if (id.isEmpty())
        return;
    const board::Card *card = m_model.card(id);
    if (!confirmDeleteCard(id, card ? card->title : QString(), this))
        return;
    const QString requestId = nextRequestId();
    m_pendingNotes.insert(requestId, QStringLiteral("Deleted #%1").arg(id));
    m_pendingDeletes.insert(requestId, id);
    send({{QStringLiteral("type"), QStringLiteral("board_delete")},
          {QStringLiteral("id"), requestId},
          {QStringLiteral("card"), id},
          {QStringLiteral("reason"), QStringLiteral("deleted in the Board")}});
}

bool BoardView::deletedHere(const QString &card) const
{
    return m_pendingDeletes.values().contains(card);
}

void BoardView::forgetDeleted(const QString &card)
{
    for (auto it = m_pendingDeletes.begin(); it != m_pendingDeletes.end();) {
        if (it.value() == card)
            it = m_pendingDeletes.erase(it);
        else
            ++it;
    }
}

void BoardView::focusInput()
{
    const int at = board::rowOfCard(m_rows, m_selected);
    if (at >= 0) {
        selectRow(at);
        return;
    }
    const int first = board::stepRow(m_rows, -1, 1);
    if (first >= 0) {
        selectRow(first);
        return;
    }
    // No card to stand on (an empty board, or a filter with no match): the view itself takes the
    // keys, so `n` still adds and `/` still filters.
    setFocus();
}

// Up/Down walk the card rows of the whole list, stepping over the section headers, so the
// selection crosses a section break without a detour (design 4.6).
void BoardView::step(int delta)
{
    const int at = m_list->currentRow();
    const int from = at >= 0 ? at : (delta > 0 ? -1 : int(m_rows.size()));
    const int next = board::stepRow(m_rows, from, delta);
    if (next >= 0)
        selectRow(next);
}

// Alt+Shift+Up/Down: one place up or down inside the card's own section.
void BoardView::reorder(int delta)
{
    const int at = board::rowOfCard(m_rows, m_selected);
    if (at < 0)
        return;
    const QString columnId = m_rows.at(at).columnId;
    // In a flat list (#ESDF) a place up or down is the row above or below, whatever its stage;
    // the card keeps its own section, so only its rank moves.
    const QStringList order = m_model.grouping() == board::Grouping::Flat
                                  ? board::cardsInList(m_rows)
                                  : board::cardsInSection(m_rows, columnId);
    const int from = order.indexOf(m_selected);
    const int slot = from + delta;
    if (from < 0 || slot < 0 || slot >= order.size())
        return;
    const auto [before, after] = board::placement(order, m_selected, slot);
    moveCard(m_selected, columnId, before, after);
}

// Alt+Shift+Left/Right: to the previous or next status, which is the section above or below.
void BoardView::shiftSection(int delta)
{
    const int at = board::rowOfCard(m_rows, m_selected);
    if (at < 0)
        return;
    const QStringList ids = columnIds();
    const int here = ids.indexOf(m_rows.at(at).columnId);
    const int next = here + delta;
    if (here < 0 || next < 0 || next >= ids.size())
        return;
    moveCard(m_selected, ids.at(next), {}, {});
}

// Left folds the section the selection is in — or, on the self-closed fold row or on one of its
// cards, puts those cards away first (#93WR). One key, innermost first: ← on a self-closed card
// lands on the fold row, ← again folds the whole section.
void BoardView::foldSelected()
{
    // The signals block (#AQ6X): ← on its toggle puts it away, and ← on a signal row goes back to
    // the toggle that holds it and puts the whole block away — exactly what ← does from a
    // self-closed card (#93WR).
    if (!selectedSignalFold().isEmpty()) {
        if (m_signalFolds.contains(selectedSignalFold()))
            toggleSignalFold(selectedSignalFold());
        return;
    }
    if (!selectedSignal().isEmpty()) {
        const QString which = signalFoldOf(selectedSignal());
        if (!which.isEmpty() && m_signalFolds.contains(which))
            toggleSignalFold(which);
        return;
    }
    if (!selectedFold().isEmpty()) {
        const QString columnId = selectedFold();
        if (m_selfClosedOpen.contains(columnId))
            toggleSelfClosed(columnId);
        else
            toggleSection(columnId);
        return;
    }
    // A flat list (#ESDF) has no section to fold: ← on a card does nothing rather than fold a
    // header that is not on the page.
    if (m_model.grouping() == board::Grouping::Flat)
        return;
    const int at = board::rowOfCard(m_rows, m_selected);
    if (at < 0)
        return;
    const QString columnId = m_rows.at(at).columnId;
    const board::Card *card = m_model.card(m_selected);
    if (card && board::selfClosed(*card) && m_selfClosedOpen.contains(columnId)) {
        toggleSelfClosed(columnId);
        return;
    }
    toggleSection(columnId);
}

// Right unfolds the folded section nearest the selection, above or below, and stands on its
// first card. Nearest, not "the next one down": right after Left the folded header is next to
// the selection, so the two keys undo each other, and at the end of the list Right opens Done
// rather than jumping back to whatever was folded at the top.
void BoardView::unfoldNearest()
{
    // On a signals toggle (#AQ6X), → shows its rows and steps onto the first of them. On a signal
    // row it does nothing: a signal has nothing to unfold, and its page is Enter's.
    if (!selectedSignalFold().isEmpty()) {
        const QString which = selectedSignalFold();
        if (!m_signalFolds.contains(which))
            toggleSignalFold(which);
        const int fold = which == QStringLiteral("signals") ? board::rowOfSignalFold(m_rows)
                                                           : board::rowOfDismissedFold(m_rows);
        const int first = board::stepRow(m_rows, fold, 1);
        if (first >= 0 && m_rows.at(first).kind == board::Row::Signal)
            selectRow(first);
        return;
    }
    if (!selectedSignal().isEmpty())
        return;
    // On the self-closed fold row (#93WR), → is that row's own key: it shows its cards, and on an
    // open row it steps into the first of them, the way ← steps back out.
    if (!selectedFold().isEmpty()) {
        const QString columnId = selectedFold();
        if (!m_selfClosedOpen.contains(columnId))
            toggleSelfClosed(columnId);
        const int first = board::stepRow(m_rows, board::rowOfFold(m_rows, columnId), 1);
        if (first >= 0 && m_rows.at(first).kind == board::Row::Card)
            selectRow(first);
        return;
    }
    int from = board::rowOfCard(m_rows, m_selected);
    if (from < 0)
        from = 0;
    QString target;
    int best = -1;
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).kind != board::Row::Section || !m_rows.at(i).collapsed)
            continue;
        const int distance = qAbs(i - from);
        if (best < 0 || distance < best) {
            best = distance;
            target = m_rows.at(i).columnId;
        }
    }
    if (target.isEmpty())
        return;
    toggleSection(target);
    const int first = board::stepRow(m_rows, board::rowOfSection(m_rows, target), 1);
    if (first >= 0 && m_rows.at(first).columnId == target)
        selectRow(first);
}

// Keys that work anywhere in the pane that is not a text field: the list, the view itself.
bool BoardView::handleBoardKey(QKeyEvent *key)
{
    const auto mods = key->modifiers() & ~Qt::KeypadModifier;
    if (mods == Qt::ControlModifier && key->key() == Qt::Key_Z) {
        undoLast();
        return true;
    }

    if (mods != Qt::NoModifier && mods != Qt::ShiftModifier)
        return false;
    const QString text = key->text();
    if (text == QStringLiteral("n")) {
        // A pinned card pane has no list to put the field on (#Y2BA): the tab's list board takes
        // the new card, and with this card still open it lands in a pane of its own.
        if (m_pinned) {
            if (onQuickAddElsewhere)
                onQuickAddElsewhere();
            return true;
        }
        quickAdd();
        return true;
    }
    if (text == QStringLiteral("/")) {
        focusFilter();
        return true;
    }
    // `a` asks the page agent (#8YQ9): it puts the keyboard in the panel's composer, as `/` puts
    // it in the filter — one key for narrowing the list, one for asking about it. It is a bare
    // letter and not a chord on purpose: Ctrl+/ is already `help.shortcuts`, and the window's
    // event filter matches the keymap and accepts that key before the board ever sees it.
    if (text == QStringLiteral("a") && !m_pinned) {
        if (detailOpen())
            closeDetail();      // the panel is the list page's
        focusChat();
        return true;
    }
    // `e` edits the open card wherever the keyboard is inside the pane — the rows, the card's
    // document, or the pane itself when an open card left the focus nowhere in particular.
    if (text == QStringLiteral("e") && (detailOpen() || !m_selected.isEmpty())) {
        editSelected();
        return true;
    }
    if (text == QStringLiteral("d") && mods == Qt::NoModifier) {
        doneSelected();
        return true;
    }
    // `p` plans, `r` runs and `v` verifies the open card, or the selected one (opening it
    // first) (#XS6Q; `v` is #T71W).
    if ((text == QStringLiteral("p") || text == QStringLiteral("r") || text == QStringLiteral("v")
         || text == QStringLiteral("f"))
        && (detailOpen() || !m_selected.isEmpty())) {
        cardAction(text == QStringLiteral("p") ? QStringLiteral("plan")
                   : text == QStringLiteral("f") ? QStringLiteral("refine")   // #6W9X
                   : text == QStringLiteral("v") ? QStringLiteral("verify")
                                                 : QStringLiteral("execute"));
        return true;
    }
    // The Switchboard agent's action row carries its own letters (owner, 2026-09-20: "it should
    // have the letter hotkeys for each switchboard action as well"): Check is `k`, Clean up is
    // `u`, and a button another session puts on that row brings whatever letter it set on itself.
    // The page asks the panel rather than naming the buttons here, so a new button on the row
    // needs no change in this function — and a letter this page already spends never reaches it,
    // because every one of them is answered above.
    if (!text.isEmpty() && m_consoleHandle.runActionLetter && m_consoleHandle.runActionLetter(text))
        return true;
    // Del deletes the open or the selected card (#CYM9), after the confirm the button asks.
    // Owner-only by construction: an agent has no way to send this message.
    if (mods == Qt::NoModifier && key->key() == Qt::Key_Delete
        && (detailOpen() || !m_selected.isEmpty())) {
        deleteCard(detailOpen() ? m_detail->cardId() : m_selected);
        return true;
    }
    if (key->key() == Qt::Key_Escape && detailOpen()) {
        closeDetail();
        return true;
    }
    // On the main page Esc is the way to the filter bar (#K9X6). An open card has gone back
    // above; an active filter comes off next — the same first Esc as inside the box — and with
    // nothing left to undo the bar itself takes the focus and selects what is in it. The list
    // reaches this through its own event filter, so the rule lives here once for the whole page.
    if (key->key() == Qt::Key_Escape) {
        if (!m_filter->text().isEmpty())
            m_filter->clear();
        else
            focusFilter();
        return true;
    }
    return false;
}

void BoardView::keyPressEvent(QKeyEvent *event)
{
    if (!handleBoardKey(event))
        QWidget::keyPressEvent(event);
}

// While a card is dragged near the top or bottom of the list, scroll that way: otherwise a
// section off screen can never be dropped on.
void BoardView::autoScrollDuringDrag()
{
    QWidget *viewport = m_list->viewport();
    const QPoint at = viewport->mapFromGlobal(QCursor::pos());
    if (at.x() < -40 || at.x() > viewport->width() + 40)
        return;
    QScrollBar *bar = m_list->verticalScrollBar();
    if (at.y() >= -8 && at.y() < 36)
        bar->setValue(bar->value() - 14);
    else if (at.y() > viewport->height() - 36 && at.y() <= viewport->height() + 8)
        bar->setValue(bar->value() + 14);
}

// The card in a pane of its own — #Y2BA's docking, and the one path every gesture to it
// shares (#HKY4): Shift+Enter, middle-click and Ctrl+click. A pinned pane keeps its own card:
// a link to another card docks that card beside it, and a link to its own card is already
// on screen. A page here on the same card closes with it — or the board would show the same
// card as the new pane — while a page on another card stays where it is, like the browser tab
// the click came from.
bool BoardView::openInOwnPane(const QString &id)
{
    if (id.isEmpty() || !onOpenInNewPane)
        return false;
    if (m_pinned) {
        if (id != m_pinnedCard)
            onOpenInNewPane(id);
        return true;
    }
    if (detailOpen() && m_detail->cardId() == id)
        closeDetail();
    onOpenInNewPane(id);
    return true;
}

// Every click-shaped open of a card in this pane (#HKY4): Ctrl+click takes the browser's
// new-tab meaning and docks the card beside this one. A card pane never gives its card up to a
// link, so any click there docks the linked card beside it; in the list Board a plain click
// opens it here as before.
void BoardView::openCardFromClick(const QString &id)
{
    if (id.isEmpty())
        return;
    if ((m_pinned || QApplication::keyboardModifiers() == Qt::ControlModifier) && openInOwnPane(id))
        return;
    openCard(id);
}

bool BoardView::eventFilter(QObject *object, QEvent *event)
{
    // Middle-click on a card row in the list (#HKY4): the browser's new-tab gesture — the card
    // docks in a pane of its own and the list keeps its selection. Caught on the viewport the
    // press lands on, before the list's own handling swallows it.
    if (m_list != nullptr && object == m_list->viewport()
        && event->type() == QEvent::MouseButtonPress
        && static_cast<QMouseEvent *>(event)->button() == Qt::MiddleButton) {
        auto *press = static_cast<QMouseEvent *>(event);
#if QT_VERSION_MAJOR >= 6
        const QListWidgetItem *item = m_list->itemAt(press->position().toPoint());
#else
        const QListWidgetItem *item = m_list->itemAt(press->pos());
#endif
        const QString id = item != nullptr ? item->data(kCardRole).toString() : QString();
        if (!id.isEmpty() && openInOwnPane(id)) {
            press->accept();
            return true;
        }
        return false;   // a header row or an empty spot: the list keeps the press
    }
    // A middle-click on a chip on the Live page or the skills page (#HKY4): the chip buttons
    // ignore the press, so it walks up to the page, which finds the chip under it by its
    // "card" property and docks that card in a pane of its own.
    QWidget *host = qobject_cast<QWidget *>(object);
    if (host != nullptr
        && (object == m_skillsPage || host->objectName() == QLatin1String("boardLiveStrip")
            || host->objectName() == QLatin1String("boardLivePage"))) {
        if (event->type() == QEvent::MouseButtonPress
            && static_cast<QMouseEvent *>(event)->button() == Qt::MiddleButton) {
            auto *press = static_cast<QMouseEvent *>(event);
#if QT_VERSION_MAJOR >= 6
            const QPoint at = press->position().toPoint();
#else
            const QPoint at = press->pos();
#endif
            for (QWidget *child = host->childAt(at); child != nullptr;
                 child = child->parentWidget()) {
                const QString card = child->property("card").toString();
                if (!card.isEmpty()) {
                    if (openInOwnPane(card)) {
                        press->accept();
                        return true;
                    }
                    break;
                }
            }
        }
        return false;
    }
    if ((object == m_listTranscriptHost || object == m_cardTranscriptHost)
        && (event->type() == QEvent::Show || event->type() == QEvent::Hide)) {
        // Qt sends Hide before the host's visible state changes. Recheck after that event.
        QTimer::singleShot(0, this, [this] { updateConsoleHeight(); });
    }
    // The list pane's width, not the view's, decides whether the tools row wraps: with a card
    // open beside it, the list has only its half of the splitter.
    if (object == m_listPane && event->type() == QEvent::Resize) {
        layoutListTools();
        return false;
    }
    // An empty quick-add field that loses the focus has been abandoned; one with text in it is
    // waiting for the person to come back to it. Enter leaves it open and focused either way.
    if (object == m_quickAdd && event->type() == QEvent::FocusOut) {
        if (m_quickAdd->text().trimmed().isEmpty() && m_quickAddReview->isHidden())
            closeQuickAdd();
        return false;
    }
    // Clicking into the filter is the mouse way there; Esc reaches the bar from anywhere on
    // the main page (#K9X6), so that is the hint. Keyboard focus (`/`, Esc itself, Tab) is
    // already the fast path and says nothing.
    if (object == m_filter && event->type() == QEvent::FocusIn
        && static_cast<QFocusEvent *>(event)->reason() == Qt::MouseFocusReason && onHint) {
        onHint(QStringLiteral("board.filter"), QStringLiteral("Esc"));
    }
    // Coming back to the pane is when what the watcher missed is worth asking about (#N5JJ).
    if ((object == m_filter || object == m_list) && event->type() == QEvent::FocusIn)
        catchUp();
    if (event->type() != QEvent::KeyPress)
        return QWidget::eventFilter(object, event);
    auto *key = static_cast<QKeyEvent *>(event);
    if (object == m_filter) {
        if (key->key() == Qt::Key_Escape) {
            if (m_filter->text().isEmpty())
                focusInput();
            else
                m_filter->clear();
            return true;
        }
        if (key->key() == Qt::Key_Down || key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            focusInput();
            return true;
        }
        return QWidget::eventFilter(object, event);
    }
    if (object == m_quickAdd) {
        if (key->key() == Qt::Key_Escape) {
            closeQuickAdd();
            focusInput();
            return true;
        }
        return QWidget::eventFilter(object, event);
    }
    if (object == m_quickAddIssue) {
        if (key->key() == Qt::Key_Escape) {
            closeQuickAdd();
            focusInput();
            return true;
        }
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
            && key->modifiers() == Qt::ControlModifier) {
            saveQuickAdd();
            return true;
        }
        return QWidget::eventFilter(object, event);
    }
    if (object != m_list)
        return QWidget::eventFilter(object, event);

    const auto mods = key->modifiers() & ~Qt::KeypadModifier;
    if (mods == (Qt::AltModifier | Qt::ShiftModifier)) {
        switch (key->key()) {
        case Qt::Key_Left:
            shiftSection(-1);
            return true;
        case Qt::Key_Right:
            shiftSection(1);
            return true;
        case Qt::Key_Up:
            reorder(-1);
            return true;
        case Qt::Key_Down:
            reorder(1);
            return true;
        default:
            break;
        }
    }
    // Shift+Enter (#Y2BA): the selected card in a pane of its own, beside this board, which
    // stays on its list. A fold or signal row has nothing to open, so it falls through.
    if (mods == Qt::ShiftModifier && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
        && onOpenInNewPane && !m_selected.isEmpty() && selectedFold().isEmpty()
        && selectedSignalFold().isEmpty() && selectedSignal().isEmpty() && m_model.card(m_selected)) {
        // The card leaves this board the way the page's ⤴ takes it, through the one docking
        // path every gesture shares (#HKY4): a page here already on it closes, or the list
        // would show the same card as the new pane.
        openInOwnPane(m_selected);
        return true;
    }
    if (handleBoardKey(key))
        return true;
    if (mods == Qt::NoModifier) {
        switch (key->key()) {
        // Up/Down (and Page/Home/End) are taken over from QListWidget, which would leave the
        // current row standing on a section header it cannot select.
        case Qt::Key_Up:
            step(-1);
            return true;
        case Qt::Key_Down:
            step(1);
            return true;
        case Qt::Key_PageUp:
        case Qt::Key_PageDown: {
            const int page = qMax(1, m_list->viewport()->height() / qMax(1, rowHeight()) - 1);
            for (int i = 0; i < page; ++i)
                step(key->key() == Qt::Key_PageUp ? -1 : 1);
            return true;
        }
        case Qt::Key_Home:
        case Qt::Key_End: {
            const int edge = key->key() == Qt::Key_Home
                                 ? board::stepRow(m_rows, -1, 1)
                                 : board::stepRow(m_rows, int(m_rows.size()), -1);
            if (edge >= 0)
                selectRow(edge);
            return true;
        }
        case Qt::Key_Left:
            foldSelected();
            return true;
        case Qt::Key_Right:
            unfoldNearest();
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            // Enter on the self-closed fold row (#93WR) toggles it; a fold row is not a card and
            // has nothing to open. The signals block (#AQ6X) reads the same way: its toggles
            // toggle, and a signal row opens its page.
            if (!selectedFold().isEmpty())
                toggleSelfClosed(selectedFold());
            else if (!selectedSignalFold().isEmpty())
                toggleSignalFold(selectedSignalFold());
            else if (!selectedSignal().isEmpty())
                openSignal(selectedSignal());
            else
                openSelected();
            return true;
        default:
            break;
        }
        const QString text = key->text();
        if (text == QStringLiteral("m")) {
            moveSelected();
            return true;
        }
        if (text == QStringLiteral("c")) {
            if (detailOpen() && m_detail->cardId() == m_selected) {
                m_detail->focusReply();
            } else {
                m_replyOnOpen = true;
                openSelected();
            }
            return true;
        }
        if (text == QStringLiteral("y")) {
            copyReference();
            return true;
        }
        if (text == QStringLiteral("t")) {
            sendSelectionToTerminal();
            return true;
        }
        if (text == QStringLiteral("o")) {
            openSelectedFile();
            return true;
        }
    }
    return QWidget::eventFilter(object, event);
}

// One card row's height, for a page step.
int BoardView::rowHeight() const
{
    const int at = board::stepRow(m_rows, -1, 1);
    if (at >= 0 && at < m_list->count())
        return qMax(16, m_list->visualItemRect(m_list->item(at)).height());
    return qMax(16, QFontMetrics(m_list->font()).height() + 10);
}

// ------------------------------------------------------------------- the cleanup run (19.9)

// The one button, in its three states. Clicking it while a run is going stops that run; clicking
// it otherwise starts a **preview**, never the real thing: a cleanup rewrites many of the owner's
// files, and `dry_run` produces the same plan while every write tool refuses. The Apply button in
// the result panel is the only way to a run that writes.
void BoardView::requestCleanup()
{
    if (cleanupRunning()) {
        send({{QStringLiteral("type"), QStringLiteral("cancel")}});
        showCleanupProgress(QStringLiteral("stopping"));
        return;
    }
    startCleanup(true);
}

void BoardView::startCleanup(bool dryRun)
{
    if (cleanupRunning())
        return;
    hideCleanupPanel();
    m_cleanupDry = dryRun;
    m_cleanupWrites = 0;
    m_cleanupCards = m_model.total();
    m_cleanupChangelog.clear();
    m_cleanupRequest = nextRequestId();
    // Held from the click, not from the worker's answer: the button has to say Stop at once, and
    // a card's ask has to be refused here rather than be sent and bounced.
    m_cleanupRun = QStringLiteral("starting");
    m_cleanupClock.start();
    updateCleanupButton();
    cardBusyChanged();
    showCleanupProgress(QStringLiteral("starting"));
    send({{QStringLiteral("type"), QStringLiteral("board_cleanup")},
          {QStringLiteral("id"), m_cleanupRequest},
          {QStringLiteral("dry_run"), dryRun}});
}

void BoardView::endCleanup()
{
    m_cleanupRun.clear();
    m_cleanupRequest.clear();
    if (m_cleanupGuard)
        m_cleanupGuard->stop();
    // "A cleanup is running" stops being true here, so the line that said it on a card goes with
    // the run — but only on the card it was said about, and only if that card is still open.
    if (!m_busyCard.isEmpty()) {
        if (m_busyCard == m_detail->cardId())
            m_detail->showError(QString());
        m_busyCard.clear();
    }
    updateCleanupButton();
    cardBusyChanged();
}

void BoardView::updateCleanupButton()
{
    if (auto *button = m_findings->findChild<QToolButton *>(QStringLiteral("boardCleanup"))) {
        button->setText(cleanupRunning() ? QStringLiteral("Stop") : QStringLiteral("Clean up"));
        button->setToolTip(cleanupRunning() ? QStringLiteral("Stop the Hygiene cleanup run. Existing writes stay in the changelog.")
                                          : QStringLiteral("Hygiene stage 2: preview agent cleanup before Apply writes changes."));
    }
    // The cleanup stage lives beside the format findings. Refresh the context too so the
    // board and card action availability follows the running turn.
    refreshContexts();
    layoutListTools();
}

// A cleanup takes minutes, so its progress line stays up instead of timing out like a move's
// notice. `step` is the tool it is on, the last write it made, or a word for a state.
void BoardView::showCleanupProgress(const QString &step)
{
    const qint64 seconds = m_cleanupClock.isValid() ? m_cleanupClock.elapsed() / 1000 : 0;
    QString text = m_cleanupDry ? QStringLiteral("Hygiene · Cleanup preview") : QStringLiteral("Hygiene · Cleanup");
    text += QStringLiteral(" · %1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
    if (!step.isEmpty())
        text += QStringLiteral(" · ") + step;
    if (m_cleanupWrites > 0)
        text += QStringLiteral(" · %1 %2").arg(m_cleanupWrites)
                    .arg(m_cleanupDry ? QStringLiteral("proposed") : QStringLiteral("written"));
    if (m_cleanupDry)
        text += QStringLiteral(" · nothing is written");
    m_noticeText->setText(text);
    m_notice->setProperty("error", false);
    m_notice->style()->unpolish(m_notice);
    m_notice->style()->polish(m_notice);
    m_noticeUndo->setVisible(false);
    m_noticeOverride->setVisible(false);
    m_notice->show();
    placeNotice();
    m_noticeTimer->stop();      // it goes when the run does, not on a timer
}

void BoardView::hideCleanupPanel()
{
    if (m_cleanupPanel)
        m_cleanupPanel->hide();
}

// A Try it run's progress (#JNYN, 31.10). A staging takes minutes, so the line stays up instead
// of timing out, exactly as a cleanup's does.
void BoardView::showTryItProgress(const QString &step)
{
    const qint64 seconds = m_tryClock.isValid() ? m_tryClock.elapsed() / 1000 : 0;
    QString text = m_tryCard.isEmpty() ? QStringLiteral("Preparing review")
                                       : QStringLiteral("Preparing review · #%1").arg(m_tryCard);
    text += QStringLiteral(" · %1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
    if (m_tryReusing)
        text += QStringLiteral(" · reusing the staged environment");
    if (!step.isEmpty())
        text += QStringLiteral(" · ") + step;
    m_noticeText->setText(text);
    m_notice->setProperty("error", false);
    m_notice->style()->unpolish(m_notice);
    m_notice->style()->polish(m_notice);
    m_noticeUndo->setVisible(false);
    m_noticeOverride->setVisible(false);
    m_notice->show();
    placeNotice();
    m_noticeTimer->stop();      // it goes when the run does, not on a timer
}

// 31.10's events. A `tryit` event is the run talking; a turn event the run tagged carries
// `tryit: true` and a `run_id`, and must not reach the card's thread — the person reads the
// section the run leaves behind, not the model's commentary while it works.
bool BoardView::handleTryItEvent(const QString &type, const QJsonObject &event)
{
    if (type != QStringLiteral("tryit")) {
        if (!event.value(QStringLiteral("tryit")).toBool())
            return false;
        return true;             // the run's own turn events, swallowed whole
    }
    const QString card = event.value(QStringLiteral("card_id")).toString();
    const QString state = event.value(QStringLiteral("state")).toString();
    if (state == QStringLiteral("started")) {
        m_tryRun = event.value(QStringLiteral("run_id")).toString();
        if (m_tryRun.isEmpty())
            m_tryRun = QStringLiteral("running");
        m_tryCard = card;
        m_tryReusing = event.value(QStringLiteral("reusing")).toBool();
        if (!m_tryClock.isValid())
            m_tryClock.start();
        if (card == m_detail->cardId())
            m_detail->setTryRunning(true);
        showTryItProgress(QStringLiteral("reading the card"));
        return true;
    }
    if (state == QStringLiteral("progress")) {
        showTryItProgress(event.value(QStringLiteral("line")).toString());
        return true;
    }
    if (state == QStringLiteral("answered")) {
        // The verdict, the reveal and `## Human QA` are all written by now; the card re-reads
        // itself from `board_changed`, so this only says so.
        showNotice(QStringLiteral("Answer recorded on #%1. The expected result is in the "
                                  "review section, with your answer recorded.").arg(card),
                   false);
        return true;
    }
    // finished, stopped or error: the run is over either way.
    m_tryRun.clear();
    m_tryReusing = false;
    m_tryClock.invalidate();
    m_detail->setTryRunning(false);
    m_noticeTimer->stop();
    m_notice->hide();
    const QString message = event.value(QStringLiteral("message")).toString();
    const QString out = event.value(QStringLiteral("out")).toString();
    if (state == QStringLiteral("error")) {
        showNotice(message.isEmpty() ? QStringLiteral("Review staging could not start.") : message, true);
    } else if (state == QStringLiteral("stopped")) {
        showNotice(QStringLiteral("Review staging stopped. Whatever it captured is in %1.").arg(out),
                   false);
    } else if (event.value(QStringLiteral("section_written")).toBool()) {
        // The card page re-renders from `board_changed`; the notice says where to look, and
        // reading the card again makes the strip appear without waiting for the watcher.
        if (!card.isEmpty())
            send({{QStringLiteral("type"), QStringLiteral("board_card_get")},
                  {QStringLiteral("card"), card}});
        showNotice(QStringLiteral("#%1 has a staged review: one task and one question. The expected "
                                  "result stays sealed until you answer.").arg(card), false);
    } else {
        // The turn could not stage it, and said so on the thread (brief step 7). Amber, because
        // this is the case the whole feature exists to keep off a review request.
        showNotice(message.isEmpty()
                       ? QStringLiteral("Review could not stage #%1, and wrote no section.").arg(card)
                       : message, true);
    }
    return true;
}

// 19.9's events, kept away from any card thread. They carry `cleanup: true` and a `run_id` and
// never a `card_id`, so this runs before the card routing and swallows every one of them.
bool BoardView::handleCleanupEvent(const QString &type, const QJsonObject &event)
{
    if (type == QStringLiteral("board_cleanup_started")) {
        m_cleanupRun = event.value(QStringLiteral("run_id")).toString();
        if (m_cleanupRun.isEmpty())
            m_cleanupRun = QStringLiteral("running");
        m_cleanupDry = event.value(QStringLiteral("dry_run")).toBool();
        m_cleanupChangelog = event.value(QStringLiteral("changelog")).toString();
        m_cleanupCards = event.value(QStringLiteral("cards")).toInt(m_model.total());
        m_cleanupWrites = 0;
        if (!m_cleanupClock.isValid())
            m_cleanupClock.start();
        updateCleanupButton();
        showCleanupProgress(QStringLiteral("reading %1 cards").arg(m_cleanupCards));
        return true;
    }
    if (type == QStringLiteral("board_cleanup_summary")) {
        endCleanup();
        m_noticeTimer->stop();
        m_notice->hide();
        showCleanupSummary(event);
        return true;
    }
    if (!event.value(QStringLiteral("cleanup")).toBool())
        return false;
    // Everything below belongs to the run, and to nothing else.
    if (type == QStringLiteral("board_activity")) {
        ++m_cleanupWrites;
        const QString card = event.value(QStringLiteral("id")).toString();
        QString line = event.value(QStringLiteral("summary")).toString();
        if (!card.isEmpty())
            line = QStringLiteral("#%1 %2").arg(card, line);
        showCleanupProgress(line);
        return true;
    }
    if (type == QStringLiteral("tool_started")) {
        // One concise line rather than the bare tool name (protocol § 23): "reading agent.py",
        // "running git status". Without a `label` this falls back to the legacy preview, and
        // without that to the tool's own name, so an older worker says as much as it used to.
        const toollabel::Label label = toollabel::fromEvent(event);
        const QString line = label.runningLine().isEmpty() ? label.line() : label.runningLine();
        showCleanupProgress(line.isEmpty() ? event.value(QStringLiteral("tool")).toString() : line);
        return true;
    }
    if (type == QStringLiteral("tool_result")) {
        // Past tense with its stats once the call has landed: "read 4 cards · 120 lines". A call
        // that never happened says why on the same line, which is what line() already puts there.
        const toollabel::Label label = toollabel::fromEvent(event);
        if (!label.line().isEmpty())
            showCleanupProgress(label.line());
        return true;
    }
    if (type == QStringLiteral("status")) {
        showCleanupProgress(event.value(QStringLiteral("text")).toString());
        return true;
    }
    if (type == QStringLiteral("error")) {
        showCleanupProgress(QStringLiteral("failed: %1")
                                .arg(event.value(QStringLiteral("text")).toString()));
    }
    if (type == QStringLiteral("done") || type == QStringLiteral("error")
        || type == QStringLiteral("cancelled")) {
        // The summary is the last word (19.9) and ends the run. This only catches a worker that
        // died before sending one, which would otherwise leave the button on Stop for good.
        if (!m_cleanupGuard) {
            m_cleanupGuard = new QTimer(this);
            m_cleanupGuard->setSingleShot(true);
            m_cleanupGuard->setInterval(20000);
            connect(m_cleanupGuard, &QTimer::timeout, this, [this] {
                if (!cleanupRunning())
                    return;
                endCleanup();
                showNotice(QStringLiteral("The cleanup ended without a summary. Its changelog, if "
                                          "it wrote one, has what it did."), true);
            });
        }
        m_cleanupGuard->start();
    }
    // delta, answer, thinking, turn_summary and the rest of a turn's chatter: the run's prose is
    // in its report and its changelog, not streamed over the board.
    return true;
}

void BoardView::showCleanupSummary(const QJsonObject &summary)
{
    const QString outcome = summary.value(QStringLiteral("outcome")).toString();
    const bool dry = summary.value(QStringLiteral("dry_run")).toBool();
    const QJsonObject counts = summary.value(QStringLiteral("counts")).toObject();
    const QJsonArray changes = summary.value(QStringLiteral("changes")).toArray();
    const QJsonArray refusals = summary.value(QStringLiteral("refusals")).toArray();
    const int writes = counts.value(QStringLiteral("writes")).toInt();
    const int proposed = counts.value(QStringLiteral("proposed")).toInt();
    const double seconds = summary.value(QStringLiteral("seconds")).toDouble();
    m_cleanupChangelog = summary.value(QStringLiteral("changelog")).toString();

    // The heading says which of the two ran, and how it ended, in that order: whether anything
    // was written to the owner's files is the first thing to know.
    QString head = dry ? QStringLiteral("Hygiene · Cleanup preview — nothing was written")
                       : QStringLiteral("Hygiene · Cleanup — the board was rewritten");
    if (outcome == QStringLiteral("cancelled"))
        head += dry ? QStringLiteral(" · stopped") : QStringLiteral(" · stopped part way");
    else if (outcome == QStringLiteral("error"))
        head += QStringLiteral(" · it failed");
    QStringList facts;
    facts << QStringLiteral("%1 card%2 before, %3 after")
                 .arg(summary.value(QStringLiteral("cards_before")).toInt(m_cleanupCards))
                 .arg(summary.value(QStringLiteral("cards_before")).toInt(m_cleanupCards) == 1
                          ? QString() : QStringLiteral("s"))
                 .arg(summary.value(QStringLiteral("cards_after")).toInt(m_cleanupCards));
    facts << (dry ? QStringLiteral("%1 proposed").arg(proposed)
                  : QStringLiteral("%1 written").arg(writes));
    for (const QString &action : {QStringLiteral("merge"), QStringLiteral("split"),
                                  QStringLiteral("move"), QStringLiteral("update"),
                                  QStringLiteral("create"), QStringLiteral("comment"),
                                  QStringLiteral("sections")}) {
        const int n = counts.value(action).toInt();
        if (n > 0)
            facts << QStringLiteral("%1 %2").arg(n).arg(action);
    }
    if (seconds > 0)
        facts << QStringLiteral("%1:%2").arg(int(seconds) / 60)
                     .arg(int(seconds) % 60, 2, 10, QLatin1Char('0'));
    m_cleanupHead->setText(QStringLiteral("<b>%1</b><br>%2")
                               .arg(head.toHtmlEscaped(), facts.join(QStringLiteral(" · ")).toHtmlEscaped()));

    QString html;
    if (changes.isEmpty()) {
        html += QStringLiteral("<p>%1</p>")
                    .arg(dry ? QStringLiteral("It proposed no change.")
                             : QStringLiteral("It changed nothing. Doing less and saying why is a "
                                              "good outcome for this run."));
    } else {
        html += QStringLiteral("<p><b>%1</b></p><ul>")
                    .arg(dry ? QStringLiteral("What it would do") : QStringLiteral("What it did"));
        for (const QJsonValue &value : changes) {
            const QJsonObject change = value.toObject();
            const QString id = change.value(QStringLiteral("card_id")).toString();
            const QString action = change.value(QStringLiteral("action")).toString();
            const QString text = change.value(QStringLiteral("summary")).toString();
            const QString path = change.value(QStringLiteral("path")).toString();
            QString line = QStringLiteral("<b>%1</b>").arg(action.toHtmlEscaped());
            if (!id.isEmpty())
                line += QStringLiteral(" <a href=\"card:%1\">#%1</a>").arg(id.toHtmlEscaped());
            if (!text.isEmpty())
                line += QStringLiteral(" — %1").arg(text.toHtmlEscaped());
            if (!path.isEmpty())
                line += QStringLiteral(" · <a href=\"%1\">%2</a>")
                            .arg(path.toHtmlEscaped(), QFileInfo(path).fileName().toHtmlEscaped());
            html += QStringLiteral("<li>%1</li>").arg(line);
        }
        html += QStringLiteral("</ul>");
        if (summary.value(QStringLiteral("truncated")).toBool())
            html += QStringLiteral("<p>Only the first %1 are listed here; the changelog has every "
                                   "one.</p>").arg(changes.size());
    }
    if (!refusals.isEmpty()) {
        html += QStringLiteral("<p><b>Refused (%1)</b></p><ul>").arg(refusals.size());
        for (const QJsonValue &value : refusals) {
            const QJsonObject refusal = value.toObject();
            html += QStringLiteral("<li>%1 — %2</li>")
                        .arg(refusal.value(QStringLiteral("tool")).toString().toHtmlEscaped(),
                             refusal.value(QStringLiteral("error")).toString().toHtmlEscaped());
        }
        html += QStringLiteral("</ul>");
    }
    const QString report = summary.value(QStringLiteral("report")).toString();
    if (!report.isEmpty()) {
        html += QStringLiteral("<p><b>The agent's report</b></p>");
        const QStringList paragraphs = report.split(QStringLiteral("\n\n"), Qt::SkipEmptyParts);
        for (const QString &paragraph : paragraphs)
            html += QStringLiteral("<p>%1</p>").arg(paragraph.trimmed().toHtmlEscaped());
    }
    m_cleanupBody->setHtml(html);
    m_cleanupBody->verticalScrollBar()->setValue(0);

    // Apply only follows a preview that found something, and only when it ran to the end: half a
    // plan is not a plan.
    m_cleanupApply->setVisible(dry && outcome == QStringLiteral("done") && !changes.isEmpty());
    m_cleanupLog->setVisible(!m_cleanupChangelog.isEmpty());
    m_cleanupLog->setToolTip(m_cleanupChangelog.isEmpty()
                                 ? QString()
                                 : QStringLiteral("Open %1 in a pane").arg(m_cleanupChangelog));
    m_cleanupPanel->setProperty("failed", outcome == QStringLiteral("error"));
    m_cleanupPanel->style()->unpolish(m_cleanupPanel);
    m_cleanupPanel->style()->polish(m_cleanupPanel);
    m_cleanupPanel->show();
    // The list keeps the keyboard: every control in the panel is NoFocus, so nothing moved.
}

}  // namespace relay


QJsonObject relay::BoardView::drive(const QJsonObject &request)
{
    const QString op = request.value(QStringLiteral("op")).toString();
    const QString name = request.value(QStringLiteral("name")).toString();
    const auto fail = [](const char *reason) {
        return QJsonObject{{"ok", false}, {"error", QString::fromLatin1(reason)}};
    };
    if (op == QLatin1String("open")) {
        const QString id = request.value(QStringLiteral("card")).toString().toUpper().remove(QLatin1Char('#'));
        if (!m_model.card(id)) return fail("unknown_card");
        openCard(id);
        return {{"ok", true}, {"card", id}};
    }
    if (op == QLatin1String("read") && name == QLatin1String("notice"))
        return {{"ok", true}, {"text", m_notice->isVisible() ? m_noticeText->text() : QString()}};
    if (op == QLatin1String("read") && name == QLatin1String("sections")) {
        if (!detailOpen()) return fail("card_not_open");
        auto *doc = findChild<QTextBrowser *>(QStringLiteral("boardCardDocument"));
        return {{"ok", true}, {"card", selectedCard()}, {"text", doc ? doc->toPlainText() : QString()}};
    }
    // Only uniquely named, visible Board controls. No terminal or agent console text input.
    const QSet<QString> fields{QStringLiteral("boardFilter"), QStringLiteral("boardCardTitleEdit"),
        QStringLiteral("boardIssueEditor"), QStringLiteral("boardTestsEditor"), QStringLiteral("boardQuickAdd"),
        QStringLiteral("boardCardFind")};   // the card page's find strip (#9NBZ)
    if (op == QLatin1String("type") && !fields.contains(name)) return fail("not_a_named_box");
    QList<QWidget *> matches;
    for (QWidget *widget : findChildren<QWidget *>(name))
        if (widget->isVisible()) matches << widget;
    if (matches.size() != 1) return fail(matches.isEmpty() ? "control_not_visible" : "ambiguous_control");
    QWidget *widget = matches.first();
    if (op == QLatin1String("read")) {
        QString text;
        if (auto *label = qobject_cast<QLabel *>(widget)) text = label->text();
        else if (auto *line = qobject_cast<QLineEdit *>(widget)) text = line->text();
        else if (auto *edit = qobject_cast<QPlainTextEdit *>(widget)) text = edit->toPlainText();
        else if (auto *doc = qobject_cast<QTextBrowser *>(widget)) text = doc->toPlainText();
        else if (auto *button = qobject_cast<QAbstractButton *>(widget)) text = button->text();
        else if (name == QLatin1String("boardTestsFindings")) {
            QStringList lines;
            for (QLabel *label : widget->findChildren<QLabel *>())
                if (label->isVisible()) lines << label->text();
            text = lines.join(QLatin1Char('\n'));
        } else return fail("control_not_readable");
        return {{"ok", true}, {"text", text}, {"enabled", widget->isEnabled()}};
    }
    if (!widget->isEnabled()) return fail("control_disabled");
    if (op == QLatin1String("press")) {
        auto *button = qobject_cast<QAbstractButton *>(widget);
        if (!button || !name.startsWith(QLatin1String("board"))) return fail("not_a_named_button");
        // Queue a click: modal dialogs must not block the socket response.
        QTimer::singleShot(0, button, [button] { button->click(); });
        return {{"ok", true}};
    }
    if (op == QLatin1String("type")) {
        const QString text = request.value(QStringLiteral("text")).toString();
        if (auto *line = qobject_cast<QLineEdit *>(widget)) {
            if (line->isReadOnly()) return fail("read_only");
            line->setFocus(); line->selectAll(); line->insert(text);
        } else if (auto *edit = qobject_cast<QPlainTextEdit *>(widget)) {
            if (edit->isReadOnly()) return fail("read_only");
            edit->setFocus(); edit->selectAll(); edit->insertPlainText(text);
        } else return fail("not_a_named_box");
        return {{"ok", true}};
    }
    return fail("unknown_operation");
}
