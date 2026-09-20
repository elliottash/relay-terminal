// SPDX-License-Identifier: AGPL-3.0-or-later
#include "HelperChat.h"

#include <QComboBox>

#include "CopyOnSelect.h"
#include "RichEditor.h"
#include "Theme.h"
#include "ToolLabel.h"
#include "Voice.h"

#include <QApplication>
#include <QCheckBox>
#include <QIcon>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFocusEvent>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonValue>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSettings>
#include <QSizePolicy>
#include <QStyle>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

namespace relay {

// "Switchboard agent" on the board and "<Pane> helper" everywhere else (#FEJQ decision 4: the
// `switchboard` role is relabelled "Helper agent" — label only — and "each panel's header says
// where it is"). An unknown pane name gets the plain label rather than an empty head, because a
// pane added later must not make the panel look broken before this list is updated.
QString helperpane::title(const QString &pane)
{
    if (pane == options())
        return QStringLiteral("Options helper");
    if (pane == actions())
        return QStringLiteral("Actions helper");
    if (pane == sessions())
        return QStringLiteral("Sessions helper");
    if (pane == switchboard() || pane.isEmpty())
        return QStringLiteral("Switchboard agent");
    return QStringLiteral("Helper agent");
}

namespace {

// The turn's clock. `chat.seconds` is what the *worker* has already spent on the turn before this
// GUI ever saw it (a pane opened mid-conversation, a reconnect), and `m_clock` only measures from
// the last event that arrived here. The header is this panel's contract and may not grow a member
// for the offset, so it rides on the widget as a dynamic property — the same trick the chrome uses
// for `running` and `warn`, and it costs one QVariant.
constexpr char kClockBase[] = "relayChatClockBase";
// The one-second tick that redraws the clock while a turn runs. It is found by object name for the
// same reason: a second QTimer beside `m_render` would be a second member.
QString clockTimerName()
{
    return QStringLiteral("boardChatClock");
}

// Model reasoning is untrusted text: drop control characters except newline and tab, exactly as
// every other transcript surface does (src/AgentInternalsView.cpp's sanitize, CardDetail's copy).
QString sanitizeTrace(const QString &text)
{
    QString clean;
    clean.reserve(text.size());
    for (const QChar c : text) {
        const ushort u = c.unicode();
        if (u == '\n' || u == '\t' || (u >= 0x20 && u != 0x7f && !(u >= 0x80 && u < 0xa0)))
            clean += c;
    }
    return clean;
}

// Empty a layout of its rows. `deleteLater` rather than `delete`, because a queue row is rebuilt
// from the `board_chat_state` that a click on one of its own buttons caused: the worker's queue is
// authoritative (19.18), so the answer that redraws the row can still be inside the click.
void clearLayout(QLayout *layout)
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
            clearLayout(child);
        delete item;
    }
}

QString clockText(qint64 seconds)
{
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

// What a `card:`, `option:` or `session:` link names, however the model spelled it. `card:K7Q2`
// has no authority, so Qt puts K7Q2 in the path; `card://K7Q2` puts it in the host. Both are
// written by hand in an answer, so both have to arrive somewhere.
// The composer's fallbacks, widest first. RichEditor picks the widest that fits.
//
// The placeholder is now the only thing that says what the box is for: the paragraph that used to
// stand above an empty conversation is gone (owner, 2026-09-20: "there is the useless help
// sentence, and then a bunch of wasted space, and then the tiny text box"). So the widest rung
// names the agent — "Ask the Switchboard agent", "Ask the Options helper" — rather than a topic,
// because the head above it already says where the panel is, and carries the two things the
// sentence taught: Enter sends, a second prompt queues.
QStringList askPlaceholders(const QString &pane)
{
    const QString who = helperpane::title(pane);
    return {QStringLiteral("Ask the %1 — Enter sends, a second prompt queues").arg(who),
            QStringLiteral("Ask the %1 — Enter sends").arg(who),
            QStringLiteral("Ask the %1…").arg(who),
            QStringLiteral("Ask…")};
}

QString linkTarget(const QUrl &url)
{
    QString target = url.host();
    if (!url.path().isEmpty())
        target += url.path();
    while (target.startsWith(QLatin1Char('/')))
        target.remove(0, 1);
    return target;
}

// The busy strip's line: who is turning, how long it has been turning, and whether this is the
// survey turn. The clock and the survey word rode on the head row's name label until 2026-09-20,
// when the owner made every action row buttons and nothing else ("yes, lets do both left-aligned,
// drop the label") — so they moved here, to the strip that already names the agent and is on
// screen for exactly as long as there is a turn to time. `title` is where the panel is (#FEJQ
// decision 4): one worker answers four panels, so the line has to say which it is. The state word
// stays out of the *pane's* header (owner, 2026-09-19, "Header and Activity decisions"); this
// strip is the panel's own, where a running clock is the whole point.
void drawBusyLine(QLabel *label, const QString &title, bool running, bool survey, qint64 seconds)
{
    if (label == nullptr)
        return;
    QString text = QStringLiteral("✦ ") + title;
    if (running)
        text += QStringLiteral(" · ") + clockText(seconds);
    if (running && survey)
        text += QStringLiteral(" · survey");
    label->setText(text);
}

// Markdown headings come out of QTextDocument at browser sizes (an H1 is ~2x the text), which
// shouts inside a panel under a card list. Bring them down to a document scale and give them air.
void tuneHeadings(QTextDocument *doc, qreal base)
{
    static const qreal scale[] = {1.0, 1.3, 1.15, 1.05, 1.0, 1.0, 1.0};
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        const int level = block.blockFormat().headingLevel();
        if (level <= 0)
            continue;
        QTextCursor cursor(block);
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        QTextCharFormat format;
        format.setProperty(QTextFormat::FontSizeAdjustment, 0);
        format.setFontPointSize(base * scale[qMin(level, 6)]);
        format.setFontWeight(QFont::DemiBold);
        cursor.mergeCharFormat(format);
        QTextBlockFormat blockFormat = block.blockFormat();
        blockFormat.setTopMargin(level <= 2 ? 12 : 8);
        blockFormat.setBottomMargin(4);
        cursor.setBlockFormat(blockFormat);
    }
}

void insertMarkdown(QTextCursor &cursor, const QString &markdown, qreal base, const QFont &font)
{
    QTextDocument part;
    part.setDefaultFont(font);
    part.setMarkdown(markdown);
    tuneHeadings(&part, base);
    cursor.insertFragment(QTextDocumentFragment(&part));
}

// One line of the log. The first line goes into the document's own empty block rather than after
// it, so the conversation does not open on a blank paragraph.
void insertLine(QTextCursor &cursor, const QString &text, const QTextCharFormat &format,
                int topMargin)
{
    QTextBlockFormat block;
    block.setTopMargin(topMargin);
    block.setBottomMargin(2);
    if (cursor.document()->isEmpty() && cursor.block().text().isEmpty()) {
        cursor.setBlockFormat(block);
        cursor.setCharFormat(format);
    } else {
        cursor.insertBlock(block, format);
    }
    cursor.insertText(text, format);
}

// A hairline the width of the document, between one exchange and the next: an empty block two
// pixels tall painted in the border ink. QTextDocument has no rule a theme can colour — Markdown's
// `---` takes the palette's — and box-drawing characters would wrap and be copied with the text.
// (CardDetail::insertRule, same reasoning, same shape.)
void insertRule(QTextCursor &cursor, int topMargin)
{
    QTextBlockFormat rule;
    rule.setTopMargin(topMargin);
    rule.setBottomMargin(0);
    rule.setLineHeight(2, QTextBlockFormat::FixedHeight);
    rule.setBackground(theme::Border);
    QTextCharFormat hairline;
    hairline.setFontPointSize(1);
    cursor.insertBlock(rule, hairline);
}

// One reasoning block, in the words the terminal's fold uses: "thinking…" while it streams,
// "thought for N s" when it ends, over the tail of the trace. The end is the part being written
// and the part a reader needs, so the tail is what shows and the cut is named.
void insertThinking(QTextCursor &cursor, const QString &text, bool done, qint64 ms, qreal base,
                    int topMargin = 2)
{
    QTextCharFormat head;
    head.setForeground(theme::TextMuted);
    head.setFontWeight(QFont::DemiBold);
    head.setFontPointSize(qMax(theme::FloorPt, base * 0.9));
    const QString label = !done
        ? QStringLiteral("✦ thinking…")
        : (ms > 0 ? QStringLiteral("✦ thought for %1 s").arg((ms + 500) / 1000)
                  : QStringLiteral("✦ thinking stopped"));
    insertLine(cursor, label, head, topMargin);
    const QString body = sanitizeTrace(text).trimmed();
    if (body.isEmpty())
        return;
    constexpr int kTail = 2000;      // the panel is short; a card's document keeps 4000
    QTextCharFormat trace;
    trace.setForeground(theme::TextMuted);
    trace.setFontItalic(true);
    trace.setFontPointSize(qMax(theme::FloorPt, base * 0.9));
    if (body.size() > kTail)
        insertLine(cursor, QStringLiteral("… %1 more characters above").arg(body.size() - kTail),
                   trace, 2);
    const QString shown = body.size() > kTail ? body.right(kTail) : body;
    const QStringList lines = shown.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i)
        insertLine(cursor, lines.at(i), trace, i == 0 ? 2 : 1);
}

// One line for the busy strip: "reading Pane.h", "read 4 cards · 120 lines", "step 4/256". The
// same `toollabel` the cleanup notice, the card strip and the terminal pane's tool lines use, so
// the page agent is described in the words the rest of Relay uses (BoardView::turnProgressLine).
QString progressLine(const QString &type, const QJsonObject &event)
{
    if (type == QStringLiteral("status"))
        return event.value(QStringLiteral("text")).toString();
    const toollabel::Label label = toollabel::fromEvent(event);
    const QString line = type == QStringLiteral("tool_started")
                             ? (label.runningLine().isEmpty() ? label.line() : label.runningLine())
                             : label.line();
    return line.isEmpty() ? event.value(QStringLiteral("tool")).toString() : line;
}

// Voice settings, protocol 16. The same keys and the same defaults the terminal pane reads
// (src/Pane.h): one microphone behaviour everywhere, one place in Options that turns it off.
bool voiceEnabled()
{
    return QSettings().value(QStringLiteral("voice/enabled"), true).toBool();
}

QString voiceModel()
{
    const QString value = QSettings().value(QStringLiteral("voice/model")).toString();
    return value.isEmpty() ? QStringLiteral("google/gemini-3.5-flash-lite") : value;
}

int voiceSeconds()
{
    return voice::clampSeconds(QSettings().value(QStringLiteral("voice/max_seconds")).toInt());
}

// The ticked proposals, in the order the survey offered them. The tick state lives on the boxes
// themselves — there is one member for the keys and none for the rows — so each box carries its
// key and its ordinal and this reads them back.
QStringList tickedKeys(QWidget *survey)
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

// A section id as a heading word. The panel is handed the column *id* by `board_check {section}`
// and has no view of the board's config, so it makes the id readable rather than inventing a title.
QString prettySection(const QString &section)
{
    QString text = section;
    text.replace(QLatin1Char('-'), QLatin1Char(' '));
    text.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!text.isEmpty())
        text[0] = text.at(0).toUpper();
    return text;
}

QString problemCount(int count)
{
    return count == 1 ? QStringLiteral("1 problem") : QStringLiteral("%1 problems").arg(count);
}

}  // namespace

// ------------------------------------------------------------------------------- construction

// Top to bottom: the **head row** — the actions that need no typing (Check, Clean up), at the
// left, with the fold at its right on a panel that folds — then whatever a Check turned up, then
// the survey's offer, then the conversation, then the queue, then the **prompt box**: one frame
// holding the busy strip, the editor and the chip strip, exactly as a terminal pane's is.
//
// That division is the owner's rule, given on 2026-09-20 of the card page's Plan and Execute and
// applied to every prompt box in the app: "move those buttons out of there … because they
// actually dont do anything in the chat box. can we instead put buttons like that in a row above
// the chat box. they are actions the agent can take that dont require typing. we put the 'clean
// up' button there for the main switchboard agent, for example." So the box holds the text and
// the chips that qualify it, and the row above it holds everything else.
//
// Everything is a row in one panel inside the list page — a pane or in-pane surface, never a
// floating strip (owner's standing rule; the cleanup panel above it is built the same way).
// The composer's microphone, drawn rather than set as text or loaded from a file.
//
// It was the emoji U+1F3A4 until the live Xvfb run showed it as a missing-glyph box: the UI font
// has no such glyph and Qt does not fall back to the colour-emoji font for a QToolButton's label.
// A terminal pane uses `<theme data>/icons/mic.svg` through `Pane::stripIcon`, but that path
// comes from `theme::themeDataDir()`, which lives in `src/Theme.cpp` — carried by
// `relay-highlight`, which `relay-board` does not link. Adding that edge for one icon would make
// the board library depend on the shell highlighter; drawing the shape costs nothing, needs no
// font and no asset, and cannot go missing.
static QIcon micIcon(const QColor &ink)
{
    constexpr int kSize = 14;
    QPixmap pixmap(kSize * 2, kSize * 2);        // 2x, so it stays crisp on a scaled desktop
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(ink, 1.3);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    // The capsule, the cradle under it, the stem and the foot: a microphone at 14 px is those
    // four strokes and nothing else survives the scaling anyway.
    painter.setBrush(ink);
    painter.drawRoundedRect(QRectF(5.0, 2.0, 4.0, 6.5), 2.0, 2.0);
    painter.setBrush(Qt::NoBrush);
    painter.drawArc(QRectF(3.0, 5.0, 8.0, 6.0), 0, -180 * 16);
    painter.drawLine(QPointF(7.0, 11.0), QPointF(7.0, 12.5));
    painter.drawLine(QPointF(5.0, 12.5), QPointF(9.0, 12.5));
    painter.end();
    return QIcon(pixmap);
}

// The collapsed row's mark (owner, 2026-09-20: "it should have a question mark icon next to it").
// Painted rather than typed for the same reason the microphone is: a glyph from the button's font
// is whatever the desktop's font has at 14 px, which on this row is a "?" a third of the height
// of the word beside it. A ringed question mark reads as "ask" at that size and keeps its weight.
static QIcon askIcon(const QColor &ink)
{
    constexpr int kSize = 14;
    QPixmap pixmap(kSize * 2, kSize * 2);        // 2x, so it stays crisp on a scaled desktop
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(ink, 1.2);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QRectF(1.2, 1.2, 11.6, 11.6));
    // The hook and the dot. Three strokes: an arc over the top, the stem down to the middle, and
    // the point under it — a "?" at 14 px is that and nothing else survives the scaling.
    painter.drawArc(QRectF(4.3, 3.4, 5.4, 4.6), 200 * 16, -250 * 16);
    painter.drawLine(QPointF(7.0, 7.4), QPointF(7.0, 9.0));
    QPen dot(ink, 1.6);
    dot.setCapStyle(Qt::RoundCap);
    painter.setPen(dot);
    painter.drawPoint(QPointF(7.0, 10.8));
    painter.end();
    return QIcon(pixmap);
}

HelperChatPanel::HelperChatPanel(const QString &pane, QWidget *parent)
    : QWidget(parent), m_pane(pane.isEmpty() ? helperpane::switchboard() : pane)
{
    setObjectName(QStringLiteral("boardChatPanel"));
    setAttribute(Qt::WA_StyledBackground);

    // The Switchboard's panel is the page it sits on, and it is built exactly as it always was:
    // its own layout is the column of rows, with no holder and no ask row above it. Every other
    // pane gets the collapsed row first and the column inside a holder, so folding it away is
    // one call and cannot miss a row added later (#FEJQ).
    QVBoxLayout *layout = nullptr;
    if (isBoard()) {
        layout = new QVBoxLayout(this);
    } else {
        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);

        // ---- collapsed: the one row the panel is until it is asked for --------------------
        // It is a button and not a label so the keyboard can reach it, and reaching it is
        // enough: a Tab that lands here meant to ask something, so the focus goes on into the
        // composer.
        m_askRow = new QWidget(this);
        m_askRow->setObjectName(QStringLiteral("boardChatAskRow"));
        auto *askLine = new QHBoxLayout(m_askRow);
        askLine->setContentsMargins(10, 6, 10, 6);
        askLine->setSpacing(6);
        // At the **bottom right** of the pane, not the left (owner, 2026-09-20: "it should be at
        // the bottom right rather than bottom left"), so the stretch goes in front of the button.
        askLine->addStretch(1);
        m_ask = new QToolButton(m_askRow);
        m_ask->setObjectName(QStringLiteral("boardChatAsk"));
        // One button that says what it opens and how (owner, 2026-09-20: "make the button say
        // Helper Agent (Alt+Q)"). The key is inside the button's own text rather than on a muted
        // label beside it: two widgets for one offer read as a label with a stray key after it,
        // and the key is part of what the button is. updateAskRow() writes the live wording.
        m_ask->setIcon(askIcon(theme::TextMuted));
        m_ask->setIconSize(QSize(14, 14));
        m_ask->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_ask->setCursor(Qt::PointingHandCursor);
        m_ask->setFocusPolicy(Qt::StrongFocus);
        m_ask->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        m_ask->installEventFilter(this);
        askLine->addWidget(m_ask, 0);
        outer->addWidget(m_askRow);
        QObject::connect(m_ask, &QToolButton::clicked, this, [this] { expand(); });

        // ---- expanded: every row below, in one holder ------------------------------------
        m_body = new QWidget(this);
        m_body->setObjectName(QStringLiteral("boardChatBody"));
        outer->addWidget(m_body);
        layout = new QVBoxLayout(m_body);
    }
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);

    // ---- the head row: the actions that need no typing, at the left ---------------------------
    //
    // An action row is **left-aligned buttons and nothing else** (owner, 2026-09-20, of this row
    // and the card page's: "the plan / execute buttons etc, would those work better at the left?"
    // — "yes, lets do both left-aligned, drop the label"). So Check, Clean up and whatever else
    // `addToolWidget` puts here start at the row's left edge and a stretch closes it.
    //
    // On the Switchboard the row is only that: the name label is gone, because the placeholder in
    // the box already names the agent ("Ask the Switchboard agent — …") and the busy strip names
    // it again while a turn runs — with the clock and the survey word that used to ride on the
    // head (drawBusyLine). A panel that folds keeps its name, because there the head row is also
    // the row you fold from and the pane it is in has nothing else that says which helper this is
    // (#FEJQ decision 4); it has no actions of its own, so nothing is left-aligned away from it.
    auto *headRow = new QHBoxLayout;
    headRow->setSpacing(6);
    if (!isBoard()) {
        m_head = new QLabel(this);
        m_head->setObjectName(QStringLiteral("boardChatHead"));
        m_head->setText(helperpane::title(m_pane));
        headRow->addWidget(m_head, 0);
    }
    m_toolRow = new QHBoxLayout;
    m_toolRow->setSpacing(6);
    headRow->addLayout(m_toolRow, 0);
    headRow->addStretch(1);
    // The way back to one row, for a panel that has one (#FEJQ). At the end of the head, where a
    // pane's own chrome buttons are, and out of the tab order: the composer is what Shift+Tab
    // should reach from here, not the control that would throw the answer off screen.
    if (!isBoard()) {
        m_fold = new QToolButton(this);
        m_fold->setObjectName(QStringLiteral("boardChatFold"));
        m_fold->setText(QStringLiteral("⌄"));
        m_fold->setToolTip(QStringLiteral("Fold the helper back to one row. The conversation is "
                                          "kept — it opens where you left it."));
        m_fold->setCursor(Qt::PointingHandCursor);
        m_fold->setFocusPolicy(Qt::NoFocus);
        m_fold->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        headRow->addWidget(m_fold, 0);
        QObject::connect(m_fold, &QToolButton::clicked, this, [this] { collapse(); });
    }
    layout->addLayout(headRow);

    // Check, beside Clean up: the board's own format check over every card, deterministic and
    // free, whose findings draft a fix request into this composer. The panel builds it because the
    // panel is where it belongs; a board that brings its own (BoardView::buildChatPanel reparents
    // one in) replaces this one rather than standing beside it — see addToolWidget().
    //
    // The board's, and only the board's: a check over every card has nothing to say in Options or
    // Sessions (#FEJQ: "the Switchboard keeps … the survey, the queue and Check/Clean up").
    if (isBoard()) {
        m_check = new QToolButton(this);
        m_check->setObjectName(QStringLiteral("boardChatCheck"));
        m_check->setText(QStringLiteral("Check"));
        m_check->setToolTip(QStringLiteral("Re-run the board's format check over every card — ids, "
                                           "front matter, threads — and list what is wrong. Click a "
                                           "finding to draft a fix for the agent. Nothing is written "
                                           "and no model is called."));
        m_check->setCursor(Qt::PointingHandCursor);
        m_check->setFocusPolicy(Qt::NoFocus);
        m_check->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        m_toolRow->addWidget(m_check);
        QObject::connect(m_check, &QToolButton::clicked, this, [this] {
            send({{QStringLiteral("type"), QStringLiteral("board_check")}});
            if (onStatus)
                onStatus(QStringLiteral("Checking every card…"));
        });
    }

    // ---- findings: what a Check or a section's triage turned up ------------------------------
    m_findings = new QWidget(this);
    m_findings->setObjectName(QStringLiteral("boardChatFindings"));
    m_findings->setAttribute(Qt::WA_StyledBackground);
    m_findingsLayout = new QVBoxLayout(m_findings);
    m_findingsLayout->setContentsMargins(0, 0, 0, 0);
    m_findingsLayout->setSpacing(2);
    m_findings->hide();
    layout->addWidget(m_findings);

    // ---- the survey's offer (19.18): what the project already tracks, and the import ---------
    // A fresh *board*'s opening turn, so it exists only on the board; `handleEvent` drops a
    // `board_survey` that reaches any other panel, and every method here is null-guarded.
    if (isBoard()) {
        m_survey = new QWidget(this);
        m_survey->setObjectName(QStringLiteral("boardChatSurvey"));
        m_survey->setAttribute(Qt::WA_StyledBackground);
        m_surveyLayout = new QVBoxLayout(m_survey);
        m_surveyLayout->setContentsMargins(0, 0, 0, 0);
        m_surveyLayout->setSpacing(3);
        m_survey->hide();
        layout->addWidget(m_survey);
    }

    // ---- the conversation ---------------------------------------------------------------------
    m_log = new QTextBrowser(this);
    m_log->setObjectName(QStringLiteral("boardChatLog"));
    m_log->setOpenLinks(false);          // `card:K7Q2` opens the card, not a web browser
    m_log->setFocusPolicy(Qt::NoFocus);  // the arrows stay with the card list above
    m_log->setMaximumHeight(320);        // the list page is still the page; this is a panel on it
    m_log->document()->setDocumentMargin(8);
    relay::installCopyOnSelect(m_log);
    layout->addWidget(m_log);
    QObject::connect(m_log, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        if (url.scheme() == QStringLiteral("card")) {
            const QString id = (url.path().isEmpty() ? url.host() : url.path()).toUpper();
            if (onOpenCard && !id.isEmpty())
                onOpenCard(id);
            return;
        }
        // The two schemes the helper answers with (#FEJQ): `option:agent/allow_writes` reveals
        // that row in Options, `session:0f3a…` opens that conversation. An answer that names a
        // thing the app can show should be one click from showing it, the way `card:` already is.
        if (url.scheme() == QStringLiteral("option")) {
            const QString target = linkTarget(url);
            const int slash = target.indexOf(QLatin1Char('/'));
            const QString section = slash < 0 ? target : target.left(slash);
            const QString row = slash < 0 ? QString() : target.mid(slash + 1);
            if (onOpenOption && !section.isEmpty())
                onOpenOption(section, row);
            return;
        }
        if (url.scheme() == QStringLiteral("session")) {
            const QString id = linkTarget(url);
            if (onOpenSession && !id.isEmpty())
                onOpenSession(id);
            return;
        }
        if (!url.scheme().isEmpty() && url.scheme() != QStringLiteral("file")) {
            QDesktopServices::openUrl(url);
            return;
        }
        const QString path = url.isLocalFile() ? url.toLocalFile() : url.path();
        if (onOpenFile && !path.isEmpty())
            onOpenFile(path);
    });

    // ---- the queue (19.18): worker-side, in delivery order ------------------------------------
    // The board's box (#FEJQ). The FIFO itself is the worker's and serves every panel — a second
    // ask from any pane queues exactly as it always did — but the rows that *reorder* it are a
    // page the board has room for, and a helper folded to one row has nowhere to draw them. A
    // helper's queued ask is reported on the pane's status line instead, from `board_chat_queued`.
    // Above the prompt box and outside it: the box holds this turn's text, not the waiting ones.
    if (isBoard()) {
        m_queueBox = new QWidget(this);
        m_queueBox->setObjectName(QStringLiteral("boardChatQueue"));
        m_queueBox->setAttribute(Qt::WA_StyledBackground);
        m_queueLayout = new QVBoxLayout(m_queueBox);
        m_queueLayout->setContentsMargins(0, 0, 0, 0);
        m_queueLayout->setSpacing(2);
        m_queueBox->hide();
        layout->addWidget(m_queueBox);
    }

    // ---- the prompt box: one frame, the shape a terminal pane's has -------------------------
    //
    // Owner, 2026-09-20, comparing this panel with a pane: "i dont like the helper agent prompt
    // UI … there is the useless help sentence, and then a bunch of wasted space, and then the
    // tiny text box. and the buttons dont look as good." A pane's prompt box is one rounded frame
    // (`QFrame#composer`, src/Pane.h) holding the busy line, a **borderless** editor in the
    // prompt font, and a strip of chips under it — so this is that frame, rule for rule
    // (`QFrame#boardChatBox`, src/Theme.cpp). What is left outside it is what does not need
    // typing: the head row above carries Check and Clean up, and nothing sits in the box but the
    // text and the chips that qualify it.
    m_box = new QFrame(this);
    m_box->setObjectName(QStringLiteral("boardChatBox"));
    m_box->setAttribute(Qt::WA_StyledBackground);
    // Ignored across, as a pane's frame is (#G152): the chip strip is wider than a narrow pane,
    // and a frame that set the panel's minimum width would push the splitter around every time
    // the model's name changed. The strip is squeezed instead.
    m_box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *boxLayout = new QVBoxLayout(m_box);
    // The pane's own padding, which theme::polishWindow() puts on `QFrame#composer` — the frame
    // is what gives the editor its air, which is why the editor itself needs none.
    boxLayout->setContentsMargins(14, 10, 14, 8);
    boxLayout->setSpacing(4);

    // ---- the busy strip: what the turn is doing this second, and the one control that stops it -
    // The frame's first row, where a pane's "Relaying · …" line is, and on screen only while a
    // turn runs — an empty strip above the box would be the wasted space again.
    m_busy = new QWidget(m_box);
    m_busy->setObjectName(QStringLiteral("boardChatBusy"));
    m_busy->setAttribute(Qt::WA_StyledBackground);
    auto *busyRow = new QHBoxLayout(m_busy);
    busyRow->setContentsMargins(0, 0, 0, 0);
    busyRow->setSpacing(6);
    // It says where it is: this strip is under the log of an Options or Sessions panel as often
    // as under the board's, and "Switchboard agent" there was the one place the panel still
    // called itself by the board's name (#FEJQ).
    // It says where it is, how long the turn has been running and — on the board — whether this
    // is the survey turn: the head row above carries buttons only now (owner, 2026-09-20), so
    // this line is where the clock lives.
    m_busyLabel = new QLabel(m_busy);
    m_busyLabel->setObjectName(QStringLiteral("boardChatBusyLabel"));
    busyRow->addWidget(m_busyLabel, 0);
    // A page-agent turn reads the whole board before it says a word, and a panel that only said
    // "thinking…" for all of it read as stuck (owner, 2026-09-19, the same complaint that put this
    // line on a card and on the cleanup notice). Elided rather than wrapped: it is one row.
    m_busyWhat = new QLabel(m_busy);
    m_busyWhat->setObjectName(QStringLiteral("boardChatBusyWhat"));
    m_busyWhat->setTextInteractionFlags(Qt::NoTextInteraction);
    busyRow->addWidget(m_busyWhat, 1);
    m_stop = new QToolButton(m_busy);
    m_stop->setObjectName(QStringLiteral("boardChatStop"));
    m_stop->setText(QStringLiteral("✕ Stop"));
    m_stop->setToolTip(QStringLiteral("Stop this turn. Whatever it has already written to the "
                                      "board stays, and the queue carries on with the next prompt."));
    m_stop->setCursor(Qt::PointingHandCursor);
    m_stop->setFocusPolicy(Qt::NoFocus);
    busyRow->addWidget(m_stop, 0);
    m_busy->hide();
    boxLayout->addWidget(m_busy);
    QObject::connect(m_stop, &QToolButton::clicked, this, [this] { stopTurn(); });

    // ---- the editor --------------------------------------------------------------------------
    // Borderless and transparent (`QPlainTextEdit#boardChatComposer` in src/Theme.cpp), in the
    // prompt font, growing from two lines to eight exactly as a pane's does. It had a box of its
    // own inside the panel until 2026-09-20, which is what made it read as "the tiny text box at
    // the bottom": a border inside a frame draws the eye to the smaller of the two rectangles.
    // The frame is the box now, and the editor is the text in it.
    m_composer = new RichEditor(m_box);
    m_composer->setObjectName(QStringLiteral("boardChatComposer"));
    // No frame of its own, on top of the stylesheet's `border: none`. The qss carries the look;
    // this is the invariant a test can hold on to, and it survives a theme whose sheet forgets.
    m_composer->setFrameShape(QFrame::NoFrame);
    m_composer->setAutoHeight(2, 8);
    m_composer->setPlaceholders(askPlaceholders(m_pane));
    m_composer->installEventFilter(this);
    // Every route sends: this box has one destination (owner's words to the page agent), so the
    // chords that mean "terminal" or "agent" in a pane must not silently do nothing here.
    m_composer->onSubmit = [this](const QString &) { sendPrompt(); };
    boxLayout->addWidget(m_composer);

    // The strip under the box. Nothing sits at its left, so a stretch pushes the chips to the
    // right end, where a pane's context chip, model box and microphone are.
    m_composerRow = new QHBoxLayout;
    m_composerRow->setContentsMargins(2, 0, 2, 0);
    m_composerRow->setSpacing(6);
    m_composerRow->addStretch(1);

    // "72% left" — the pane's chip, for the conversation the page is holding. `context` rides on
    // the `chat: true` tagging (19.18) precisely so this can exist. First on the strip, so the
    // model box `addComposerWidget` inserts before the microphone lands between the two, exactly
    // as it does in a pane.
    m_context = new QLabel(m_box);
    m_context->setObjectName(QStringLiteral("boardChatContext"));
    m_context->hide();
    m_composerRow->addWidget(m_context, 0);

    // The microphone, protocol 16: record, `transcribe`, insert the text at the cursor. The
    // pane's own SVG, from the same place `Pane::stripIcon` reads it, so the two composers wear
    // one microphone. It was an emoji glyph until the live run showed it as a missing-glyph box:
    // the UI font has no U+1F3A4 and Qt does not fall back to the colour-emoji font for a
    // QToolButton's text.
    m_mic = new QToolButton(m_box);
    m_mic->setObjectName(QStringLiteral("boardChatMic"));
    m_mic->setIcon(micIcon(theme::TextMuted));
    m_mic->setIconSize(QSize(14, 14));
    m_mic->setAccessibleName(QStringLiteral("Voice transcription"));
    m_mic->setCursor(Qt::PointingHandCursor);
    m_mic->setFocusPolicy(Qt::NoFocus);
    m_composerRow->addWidget(m_mic, 0);
    QObject::connect(m_mic, &QToolButton::clicked, this, [this] { toggleVoice(); });

    // No Send (owner, 2026-09-20: "[remove] the send button on all, make it like the pane
    // agent"). A pane's prompt box has never had one: **Enter** sends, the placeholder says so,
    // and stopping a turn is the busy strip's ✕ Stop or Esc in the box — the same two ways a
    // pane ends one. A button whose label swapped to Stop was a second control saying what the
    // strip above it already said.
    boxLayout->addLayout(m_composerRow);
    layout->addWidget(m_box);

    // A streamed answer re-renders at most 25 times a second; a fast provider sends deltas far
    // faster than that and the log is a whole QTextDocument each time.
    m_render = new QTimer(this);
    m_render->setSingleShot(true);
    m_render->setInterval(40);
    QObject::connect(m_render, &QTimer::timeout, this, [this] { rebuildLog(); });

    auto *clock = new QTimer(this);
    clock->setObjectName(clockTimerName());
    clock->setInterval(1000);
    QObject::connect(clock, &QTimer::timeout, this, [this] { drawBusy(); });

    updateVoiceChip();
    setRunning(false);
    rebuildLog();

    // The Switchboard is the page it is on and stays open; every other panel starts as its one
    // row (owner, 2026-09-20: "the Switchboard's 320 px of log plus composer is most of a small
    // pane").
    m_collapsed = !isBoard();
    updateAskRow();
    applyCollapsed();
}

// ------------------------------------------------------------------------------- wiring

// Every message this panel sends carries the name of the pane it is in (#FEJQ). One helper worker
// serves the whole tab, so `pane` is what picks the brief on the worker's side and what the turn
// events coming back are tagged with; without it, the Sessions helper's question would be
// answered as if it had been asked on the board.
void HelperChatPanel::send(QJsonObject message)
{
    if (!message.contains(QStringLiteral("id")) && nextRequestId)
        message.insert(QStringLiteral("id"), nextRequestId());
    message.insert(QStringLiteral("pane"), m_pane);
    if (onSend)
        onSend(message);
}

// ------------------------------------------------------------------------------- collapsed

// One row, or the whole panel. Nothing is destroyed either way: a helper folded back keeps its
// conversation, its draft and its queue, so opening it again is where you left it.
void HelperChatPanel::applyCollapsed()
{
    if (m_askRow != nullptr)
        m_askRow->setVisible(m_collapsed);
    // Folding with the cursor still in the composer: hand the focus to the row that replaces it
    // first. Qt hands it on itself when the widget holding it hides, and it does that as a Tab —
    // and a Tab onto the ask row means "open the panel", which would unfold what was just folded.
    // The ask row is already visible here, so the focus lands where the eye is.
    if (m_collapsed && m_ask != nullptr && m_body != nullptr
        && m_body->isAncestorOf(QApplication::focusWidget()))
        m_ask->setFocus(Qt::OtherFocusReason);
    if (m_body != nullptr)
        m_body->setVisible(!m_collapsed);
    if (!m_collapsed)
        updateLogHeight();
}

// "Helper Agent (Alt+Q)" — the name of what the row opens, with the live key in parentheses
// (owner, 2026-09-20: "make the button say Helper Agent (Alt+Q)"). One widget: the key rides in
// the button's own text, so an unbound key simply leaves the name alone rather than leaving an
// empty label behind. It is the same wording in Options, Actions and Sessions, because it is one
// helper and one panel; where you are is the head's job, not this row's.
void HelperChatPanel::updateAskRow()
{
    if (m_ask == nullptr)
        return;
    m_ask->setText(m_askKeys.isEmpty() ? QStringLiteral("Helper Agent")
                                       : QStringLiteral("Helper Agent (%1)").arg(m_askKeys));
    m_ask->setToolTip(m_askKeys.isEmpty()
        ? QStringLiteral("Ask the helper agent about this pane.")
        : QStringLiteral("Ask the helper agent about this pane (%1).").arg(m_askKeys));
}

// The log is sized to the pane it is in, not to the board's list page (#FEJQ): at most ~40 % of
// the pane's height, and never less than three lines — below that an answer is a slot, not a
// conversation. The board keeps its 320, which is what the page was built around.
//
// And never taller than what it holds (owner, 2026-09-20): a QTextBrowser's size hint is a
// generic rectangle and its policy is Expanding, so a two-line answer used to be drawn at the top
// of a 320 px well with the rest of it empty — the "bunch of wasted space" between the panel's
// head and its box. The cap is the ceiling; the document's own height is what is asked for.
// rebuildLog() hides the log outright while the conversation is empty, so the floor here is one
// line and not three: three lines of nothing is the same complaint, one size smaller.
void HelperChatPanel::updateLogHeight()
{
    if (m_log == nullptr)
        return;
    const int margin = 2 * int(m_log->document()->documentMargin());
    const int line = QFontMetrics(m_log->font()).lineSpacing();
    int cap = 320;
    if (!isBoard()) {
        const QWidget *pane = parentWidget() != nullptr ? parentWidget() : this;
        cap = qMin(320, qMax(3 * line + margin, pane->height() * 2 / 5));
    }
    const int content = int(std::ceil(m_log->document()->size().height())) + margin;
    m_log->setMaximumHeight(qBound(line + margin, content, cap));
}

void HelperChatPanel::expand()
{
    if (m_collapsed) {
        m_collapsed = false;
        applyCollapsed();
    }
    focusComposer();
}

void HelperChatPanel::collapse()
{
    if (isBoard() || m_collapsed)      // the board's panel is the page; it has nowhere to fold to
        return;
    m_collapsed = true;
    applyCollapsed();
}

// The mode swapped under the panel (Options ↔ Actions, src/SettingsPane.cpp). Only the name
// changes: the head says where the panel is, the composer's placeholders ask about this pane, and
// from the next message on the worker picks the other brief. The log is left alone — it is one
// conversation with one worker (§30.7), and the turns already in it were answers to this person.
void HelperChatPanel::setPane(const QString &pane)
{
    if (pane.isEmpty() || pane == m_pane || isBoard() || pane == helperpane::switchboard())
        return;
    m_pane = pane;
    if (m_composer != nullptr)
        m_composer->setPlaceholders(askPlaceholders(m_pane));
    if (m_head != nullptr)
        m_head->setText(helperpane::title(m_pane));
    setRunning(m_running);      // redraws the busy line, which is what carries the name and clock
    updateAskRow();
}

void HelperChatPanel::setAskShortcut(const QString &hintId, const QString &keys)
{
    m_askHint = hintId;
    m_askKeys = keys;
    updateAskRow();
}

void HelperChatPanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (!m_collapsed)
        updateLogHeight();
}

// Clean up and Check. The widgets belong to whoever made them — BoardView owns Clean up, its
// Stop/Clean up text and its run (19.9) — and this only gives them a home.
void HelperChatPanel::addToolWidget(QWidget *widget)
{
    if (widget == nullptr || m_toolRow == nullptr)
        return;
    // The board brought its own Check. Two buttons with one name and one job would be a bug the
    // eye finds before the code does, so the board's wins (it can scope a check to a section) and
    // the panel's own goes. Either half may land first; neither has to know about the other.
    if (widget != m_check && m_check != nullptr
        && widget->objectName() == QStringLiteral("boardChatCheck")) {
        m_toolRow->removeWidget(m_check);
        m_check->hide();
        m_check->setParent(nullptr);
        m_check->deleteLater();
        m_check = nullptr;
    }
    widget->setParent(this);
    m_toolRow->addWidget(widget);
    widget->show();
}

// The model box (#BRD3): on the chip strip inside the prompt box, between the context chip and
// the microphone, which is exactly where a pane's sits — context, model, microphone, right to the
// frame's edge. The microphone is the last thing on the strip and stays there.
void HelperChatPanel::addComposerWidget(QWidget *widget)
{
    if (widget == nullptr || m_composerRow == nullptr)
        return;
    // The first combo in this strip is the helper's model box (#PK5Q). Remembered so Alt+M can
    // drop it open from the composer, the way Alt+M drops a terminal pane's box open: the key is
    // the keyboard's way to the same control the mouse already has.
    if (m_modelBox == nullptr)
        if (auto *box = qobject_cast<QComboBox *>(widget)) m_modelBox = box;
    widget->setParent(m_box != nullptr ? static_cast<QWidget *>(m_box) : this);
    const int at = m_mic != nullptr ? m_composerRow->indexOf(m_mic) : -1;
    if (at >= 0)
        m_composerRow->insertWidget(at, widget);
    else
        m_composerRow->addWidget(widget);
    widget->show();
}

// ------------------------------------------------------------------------------- state

// The whole panel from one `chat` block, so a pane opened mid-conversation catches up from one
// event and a queue op's answer is drawn from the worker's own list rather than from a guess here.
void HelperChatPanel::setChatState(const QJsonObject &chat)
{
    if (chat.isEmpty())
        return;
    m_history = chat.value(QStringLiteral("history")).toArray();
    m_queue.clear();
    const QJsonArray queue = chat.value(QStringLiteral("queue")).toArray();
    for (const QJsonValue &value : queue) {
        const QJsonObject item = value.toObject();
        m_queue.append(QueueItem{item.value(QStringLiteral("id")).toString(),
                                 item.value(QStringLiteral("text")).toString()});
    }
    const bool running = chat.value(QStringLiteral("running")).toBool();
    const QString turn = chat.value(QStringLiteral("turn_id")).toString();
    m_surveyTurn = chat.value(QStringLiteral("survey")).toBool();
    m_turnId = running ? turn : QString();

    // What the worker has already spent on this turn; the local clock measures from here.
    setProperty(kClockBase, chat.value(QStringLiteral("seconds")).toDouble());
    m_clock.start();

    // Whatever streamed but this block does not know about yet stays on screen: a `delta` that
    // arrived after the worker built the block would otherwise blink out for a frame.
    if (!running) {
        m_streamed.clear();
        m_thinking.clear();
        m_thinkingDone = false;
        m_thinkingMs = 0;
        setProgress(QString());
    } else if (!m_history.isEmpty() && !turn.isEmpty()) {
        // The block's last entry *is* this turn's answer — `PageAgent._collect` keeps it current
        // while the turn runs. It is the better copy, so it becomes the live one and leaves the
        // history: the running turn is the live block and nothing else. Clearing `m_streamed`
        // instead would leave the same answer in the history *and* an empty live block under it,
        // which is what queueing a second prompt used to do — the answer appeared to split in
        // two the moment the block arrived.
        const QJsonObject last = m_history.last().toObject();
        if (last.value(QStringLiteral("role")).toString() == QStringLiteral("agent")
            && last.value(QStringLiteral("turn")).toString() == turn) {
            m_streamed = last.value(QStringLiteral("text")).toString();
            m_history.removeLast();
        }
    }

    setRunning(running);
    rebuildQueue();
    rebuildLog();
}

// The busy strip is the whole of "a turn is running": it appears inside the prompt box, names
// what the turn is doing and carries the one control that ends it. There is no Send button to
// swap to Stop any more (owner, 2026-09-20).
void HelperChatPanel::setRunning(bool running)
{
    // A sync takes the board's busy guard (19.14), which this conversation holds while it turns:
    // the look waits rather than coming back as a refusal the owner has to read.
    if (m_forgeLook != nullptr && m_forgeRequest.isEmpty())
        m_forgeLook->setEnabled(!running);
    m_running = running;
    if (m_busy != nullptr)
        m_busy->setVisible(running);
    if (auto *clock = findChild<QTimer *>(clockTimerName())) {
        if (running)
            clock->start();
        else
            clock->stop();
    }
    drawBusy();
}

// The strip's own line, from the clock this panel keeps: `kClockBase` is what the worker had
// already spent on the turn before this GUI saw it, `m_clock` measures from there.
void HelperChatPanel::drawBusy()
{
    const double base = property(kClockBase).toDouble();
    drawBusyLine(m_busyLabel, helperpane::title(m_pane), m_running, m_surveyTurn,
                 qint64(base) + (m_running && m_clock.isValid() ? m_clock.elapsed() / 1000 : 0));
}

void HelperChatPanel::setProgress(const QString &line)
{
    m_progress = line;
    if (m_busyWhat == nullptr)
        return;
    const int room = qMax(60, m_busyWhat->width());
    m_busyWhat->setText(QFontMetrics(m_busyWhat->font()).elidedText(line, Qt::ElideRight, room));
    m_busyWhat->setToolTip(line);
    m_busyWhat->setVisible(m_running && !line.isEmpty());
}

void HelperChatPanel::appendDelta(const QString &text)
{
    m_streamed += text;
    if (!m_render->isActive())
        m_render->start();
}

void HelperChatPanel::appendThinking(const QString &text)
{
    if (m_thinking.size() < 200000)      // the terminal's own cap (src/Pane.h)
        m_thinking += text;
    if (!m_render->isActive())
        m_render->start();
}

void HelperChatPanel::finishThinking(qint64 ms)
{
    m_thinkingDone = true;
    m_thinkingMs = ms;
    if (!m_render->isActive())
        m_render->start();
}

// The turn ended. The worker's own history already holds the answer (PageAgent._collect), but a
// terminal event carries no `chat` block, so the panel folds what it streamed into its copy and a
// later `board_chat_state` simply replaces it with the same text. The reasoning goes in with it,
// which is the one thing the worker's history does not keep: it stays readable under the answer it
// produced until a state event refreshes the log.
void HelperChatPanel::settleTurn(const QString &how)
{
    const QString answer = m_streamed.trimmed();
    const QString trace = m_thinking.trimmed();
    if (!answer.isEmpty() || !trace.isEmpty()) {
        // Replace rather than append when the block already carried this turn's partial answer.
        if (!m_history.isEmpty()) {
            const QJsonObject last = m_history.last().toObject();
            if (last.value(QStringLiteral("role")).toString() == QStringLiteral("agent")
                && !m_turnId.isEmpty()
                && last.value(QStringLiteral("turn")).toString() == m_turnId)
                m_history.removeLast();
        }
        QJsonObject entry{{QStringLiteral("role"), QStringLiteral("agent")},
                          {QStringLiteral("text"), how == QStringLiteral("done")
                                                       ? answer
                                                       : answer + QStringLiteral("  (") + how
                                                             + QStringLiteral(")")},
                          {QStringLiteral("turn"), m_turnId}};
        if (!trace.isEmpty()) {
            entry.insert(QStringLiteral("thinking"), m_thinking);
            entry.insert(QStringLiteral("thinking_ms"), double(m_thinkingMs));
        }
        m_history.append(entry);
    }
    m_streamed.clear();
    m_thinking.clear();
    m_thinkingDone = false;
    m_thinkingMs = 0;
    m_turnId.clear();
    m_surveyTurn = false;
    setProgress(QString());
    setRunning(false);
    rebuildLog();
}

// ------------------------------------------------------------------------------- the log

void HelperChatPanel::rebuildLog()
{
    if (m_log == nullptr)
        return;
    QScrollBar *bar = m_log->verticalScrollBar();
    const int was = bar->value();
    const bool atBottom = was >= bar->maximum() - 8;
    QTextDocument *doc = m_log->document();
    doc->clear();
    const QFont font = m_log->font();
    const qreal base = font.pointSizeF() > 0 ? font.pointSizeF() : 10.0;
    QTextCursor cursor(doc);
    cursor.movePosition(QTextCursor::End);

    QTextCharFormat muted;
    muted.setForeground(theme::TextMuted);
    muted.setFontPointSize(qMax(theme::FloorPt, base * 0.9));
    QTextCharFormat who = muted;
    who.setFontWeight(QFont::DemiBold);
    who.setFontLetterSpacing(105);
    QTextCharFormat agentWho = who;
    agentWho.setForeground(theme::Agent);
    QTextCharFormat body;
    body.setForeground(theme::Text);
    body.setFontPointSize(base);
    QTextCharFormat failed = body;
    failed.setForeground(theme::Error);

    bool any = false;
    for (const QJsonValue &value : std::as_const(m_history)) {
        const QJsonObject entry = value.toObject();
        const QString role = entry.value(QStringLiteral("role")).toString();
        const QString text = entry.value(QStringLiteral("text")).toString();
        if (text.trimmed().isEmpty())
            continue;
        if (role == QStringLiteral("error")) {
            // A refusal or a failed turn, kept in the log where the prompt that caused it is.
            insertLine(cursor, text, failed, any ? 10 : 0);
        } else if (role == QStringLiteral("agent")) {
            insertLine(cursor, QStringLiteral("✦ AGENT"), agentWho, any ? 12 : 0);
            const QString trace = entry.value(QStringLiteral("thinking")).toString();
            if (!trace.trimmed().isEmpty())
                insertThinking(cursor, trace, true,
                               entry.value(QStringLiteral("thinking_ms")).toVariant().toLongLong(),
                               base);
            cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
            insertMarkdown(cursor, text, base, font);
        } else {
            // The owner's own words. A hairline above them is the seam between one exchange and
            // the next — the same demarcation a card's thread got (#VZ69) — and the words are
            // plain text, not Markdown: they are what was typed.
            if (any)
                insertRule(cursor, 16);
            insertLine(cursor, QStringLiteral("YOU"), who, any ? 8 : 0);
            const QStringList lines = text.split(QLatin1Char('\n'));
            for (int i = 0; i < lines.size(); ++i)
                insertLine(cursor, lines.at(i), body, i == 0 ? 3 : 1);
        }
        any = true;
    }

    const bool liveTrace = !m_thinking.trimmed().isEmpty() || m_thinkingDone;
    if (m_running || !m_streamed.isEmpty() || liveTrace) {
        insertLine(cursor, QStringLiteral("✦ AGENT"), agentWho, any ? 12 : 0);
        if (m_surveyTurn)
            cursor.insertText(QStringLiteral("  survey"), muted);
        if (liveTrace)
            insertThinking(cursor, m_thinking, m_thinkingDone, m_thinkingMs, base);
        cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
        if (m_streamed.isEmpty()) {
            if (!liveTrace) {
                QTextCharFormat thinking = muted;
                thinking.setFontItalic(true);
                cursor.insertText(QStringLiteral("thinking…"), thinking);
            }
        } else {
            insertMarkdown(cursor, m_streamed, base, font);
        }
        any = true;
    }

    // An empty conversation takes **no height at all** (owner, 2026-09-20: "there is the useless
    // help sentence, and then a bunch of wasted space, and then the tiny text box"). The
    // paragraph that used to invite a question is gone — the placeholder in the box says the same
    // thing, in the box it is about — and with nothing to show the log is hidden rather than left
    // as an empty block over the prompt. The moment a turn arrives it is back, growing with its
    // content up to its cap (updateLogHeight).
    m_log->setVisible(any);
    if (any)
        updateLogHeight();

    // Follow the stream while it runs; otherwise leave the reader where they were.
    bar->setValue(m_running || atBottom ? bar->maximum() : was);
}

QString HelperChatPanel::transcript() const
{
    return m_log != nullptr ? m_log->document()->toPlainText() : QString();
}

QString HelperChatPanel::draft() const
{
    return m_composer != nullptr ? m_composer->toPlainText() : QString();
}

// ------------------------------------------------------------------------------- sending

// 19.18: a second prompt **queues**, it is not refused. The page agent never refuses its own
// prompt (#N8VK's FIFO semantics, the pane's rule), so nothing here checks `m_running` first — the
// worker answers with `board_chat_queued` and the queue row appears from that.
// Alt+M (Keymap `agent.modelBox`), from a composer that has the cursor. Expanding first, because
// a collapsed panel has no strip to drop anything open in.
void HelperChatPanel::openModelBox()
{
    if (m_modelBox == nullptr)
        return;
    if (m_collapsed)
        expand();
    m_modelBox->setFocus(Qt::ShortcutFocusReason);
    m_modelBox->showPopup();
}

void HelperChatPanel::submitComposer()
{
    sendPrompt();
}

void HelperChatPanel::sendPrompt()
{
    if (m_composer == nullptr)
        return;
    const QString text = m_composer->toPlainText().trimmed();
    if (text.isEmpty())
        return;
    // A slash command typed here, before the line is a prompt. `/model` and `/models` are the
    // pane's own words for the box beside this one and for Options › Models, and a prompt box is
    // a prompt box: what works in a terminal pane's works here (#PK5Q).
    if (text.startsWith(QLatin1Char('/')) && onSlashCommand) {
        const QString line = text.mid(1);
        const QString name = line.section(QLatin1Char(' '), 0, 0);
        if (!name.isEmpty() && onSlashCommand(name, line.section(QLatin1Char(' '), 1).trimmed())) {
            m_composer->clear();
            m_composer->remember(text);
            return;
        }
    }
    send({{QStringLiteral("type"), QStringLiteral("board_chat")},
          {QStringLiteral("text"), text}});
    m_composer->clear();
    m_composer->remember(text);
}

void HelperChatPanel::stopTurn()
{
    if (!m_running)
        return;
    send({{QStringLiteral("type"), QStringLiteral("board_chat_cancel")}});
    if (onStatus)
        onStatus(QStringLiteral("Stopping the page agent…"));
}

// A draft, never a send (owner, 2026-09-19: "draft you confirm"). A clicked finding, a clicked
// problem in the banner over the list and anything else that wants the agent to do something all
// land here, in the box, with the cursor after them.
void HelperChatPanel::prefill(const QString &text)
{
    if (m_composer == nullptr)
        return;
    m_composer->setPlainText(text);
    focusComposer();
}

void HelperChatPanel::focusComposer()
{
    if (m_composer == nullptr)
        return;
    // Asking for the cursor is asking for the panel: a shortcut, a clicked finding or a prefilled
    // draft all mean the box has to be there to type in (#FEJQ).
    if (m_collapsed) {
        m_collapsed = false;
        applyCollapsed();
    }
    m_composer->setFocus(Qt::OtherFocusReason);
    QTextCursor cursor = m_composer->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_composer->setTextCursor(cursor);
}

bool HelperChatPanel::composerHasFocus() const
{
    return m_composer != nullptr && m_composer->hasFocus();
}

// The slow path into this box is the mouse (WARP.md's standing hint rule): clicking into the
// composer teaches the key that would have put the cursor here. BoardView registers Ctrl+/.
bool HelperChatPanel::eventFilter(QObject *object, QEvent *event)
{
    if (object == m_composer && event->type() == QEvent::FocusIn) {
        auto *focus = static_cast<QFocusEvent *>(event);
        if (focus->reason() == Qt::MouseFocusReason && onHint && !m_askKeys.isEmpty())
            onHint(m_askHint, m_askKeys);
    }
    // The accent border a pane's prompt box wears while that pane is the live one. Here the live
    // box is the one the cursor is in — one panel per pane, four of them in a window — so the
    // frame takes the same `relayActive` property from the editor's focus and the qss rule is the
    // one `QFrame#composer` has. Repolished by hand: a dynamic property does not restyle itself.
    if (object == m_composer && m_box != nullptr
        && (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut)) {
        m_box->setProperty("relayActive", event->type() == QEvent::FocusIn);
        m_box->style()->unpolish(m_box);
        m_box->style()->polish(m_box);
    }
    // Esc in the box ends the turn, as it does in a pane's prompt box ("Esc stops", the busy
    // line's own words). The ✕ on the busy strip is the mouse's way to the same call; with no
    // turn running Esc is left alone, so a panel in a pane does not swallow the key the pane
    // wants.
    if (object == m_composer && event->type() == QEvent::KeyPress && m_running
        && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape
        && static_cast<QKeyEvent *>(event)->modifiers() == Qt::NoModifier) {
        stopTurn();
        return true;
    }
    // A collapsed panel expands on focus as well as on a click (#FEJQ): Tab reaching the ask row
    // is the keyboard saying the same thing the click says, and it lands in the composer. Only a
    // *deliberate* focus, though — Qt hands the first focusable child the focus when the pane is
    // shown or the window is activated, and a panel that unfolded itself on every show would
    // never be collapsed at all.
    if (object == m_ask && event->type() == QEvent::FocusIn) {
        switch (static_cast<QFocusEvent *>(event)->reason()) {
        case Qt::TabFocusReason:
        case Qt::BacktabFocusReason:
        case Qt::MouseFocusReason:
        case Qt::ShortcutFocusReason:
            expand();
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(object, event);
}

// ------------------------------------------------------------------------------- the queue

// One row per queued prompt, in delivery order. The worker's queue is authoritative (19.18): a
// click sends `board_chat_queue_remove` / `board_chat_queue_move` and waits for the
// `board_chat_state` that answers it. Nothing here mutates `m_queue`, so a row can never show an
// order the worker does not have — which is what would happen the first time a move was refused.
void HelperChatPanel::rebuildQueue()
{
    if (m_queueBox == nullptr)
        return;
    clearLayout(m_queueLayout);
    m_queueBox->setVisible(!m_queue.isEmpty());
    if (m_queue.isEmpty())
        return;

    auto *head = new QLabel(m_queueBox);
    head->setObjectName(QStringLiteral("boardChatQueueHead"));
    head->setText(m_queue.size() == 1
                      ? QStringLiteral("1 prompt waiting")
                      : QStringLiteral("%1 prompts waiting").arg(m_queue.size()));
    head->setToolTip(QStringLiteral("Prompts typed while the agent was answering. They start in "
                                    "this order as each turn ends."));
    m_queueLayout->addWidget(head);

    const int room = qMax(140, m_queueBox->width() - 150);
    for (int index = 0; index < m_queue.size(); ++index) {
        const QueueItem item = m_queue.at(index);
        auto *row = new QWidget(m_queueBox);
        row->setObjectName(QStringLiteral("boardChatQueueRow"));
        row->setAttribute(Qt::WA_StyledBackground);
        auto *line = new QHBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(6);

        auto *position = new QLabel(QStringLiteral("%1.").arg(index + 1), row);
        position->setObjectName(QStringLiteral("boardChatQueuePos"));
        line->addWidget(position, 0);

        auto *preview = new QLabel(row);
        preview->setObjectName(QStringLiteral("boardChatQueueText"));
        const QString text = item.text.simplified();
        preview->setText(QFontMetrics(preview->font()).elidedText(text, Qt::ElideRight, room));
        preview->setToolTip(item.text);
        line->addWidget(preview, 1);

        // ▲/▼ only when there is somewhere to go: one prompt cannot be reordered, and a button
        // that does nothing is worse than no button.
        if (m_queue.size() > 1) {
            auto *up = new QToolButton(row);
            up->setObjectName(QStringLiteral("boardChatQueueMove"));
            up->setText(QStringLiteral("▲"));
            up->setToolTip(QStringLiteral("Send this one sooner"));
            up->setCursor(Qt::PointingHandCursor);
            up->setFocusPolicy(Qt::NoFocus);
            up->setEnabled(index > 0);
            line->addWidget(up, 0);
            const QString id = item.id;
            const int above = index - 1;
            QObject::connect(up, &QToolButton::clicked, this, [this, id, above] {
                send({{QStringLiteral("type"), QStringLiteral("board_chat_queue_move")},
                      {QStringLiteral("item"), id},
                      {QStringLiteral("to"), above}});
            });

            auto *down = new QToolButton(row);
            down->setObjectName(QStringLiteral("boardChatQueueMove"));
            down->setText(QStringLiteral("▼"));
            down->setToolTip(QStringLiteral("Send this one later"));
            down->setCursor(Qt::PointingHandCursor);
            down->setFocusPolicy(Qt::NoFocus);
            down->setEnabled(index < m_queue.size() - 1);
            line->addWidget(down, 0);
            const int below = index + 1;
            QObject::connect(down, &QToolButton::clicked, this, [this, id, below] {
                send({{QStringLiteral("type"), QStringLiteral("board_chat_queue_move")},
                      {QStringLiteral("item"), id},
                      {QStringLiteral("to"), below}});
            });
        }

        auto *drop = new QToolButton(row);
        drop->setObjectName(QStringLiteral("boardChatQueueDrop"));
        drop->setText(QStringLiteral("×"));
        drop->setToolTip(QStringLiteral("Take this prompt out of the queue. It is not sent."));
        drop->setCursor(Qt::PointingHandCursor);
        drop->setFocusPolicy(Qt::NoFocus);
        line->addWidget(drop, 0);
        const QString id = item.id;
        QObject::connect(drop, &QToolButton::clicked, this, [this, id] {
            send({{QStringLiteral("type"), QStringLiteral("board_chat_queue_remove")},
                  {QStringLiteral("item"), id}});
        });

        m_queueLayout->addWidget(row);
    }
}

// ------------------------------------------------------------------------------- findings

// A Check (or a section's ⚠ triage) answered with `board_problems {items, section}`. Every problem
// is one clickable line, and a click drafts the fix — board::fixRequest is the one wording, shared
// with the banner over the list, so the two paths cannot drift.
//
// "Nothing to fix" is shown too, and that is deliberate: a button that answers silently when all is
// well is a button the reader believes is broken.
int HelperChatPanel::showFindings(const QJsonArray &items, const QString &section)
{
    if (m_findings == nullptr)
        return 0;
    clearLayout(m_findingsLayout);
    const int total = int(items.size());

    auto *headRow = new QHBoxLayout;
    headRow->setSpacing(6);
    auto *head = new QLabel(m_findings);
    head->setObjectName(QStringLiteral("boardChatFindingsHead"));
    const QString scope = section.isEmpty()
                              ? QStringLiteral("Check")
                              : QStringLiteral("Triage · %1").arg(prettySection(section));
    head->setText(total == 0 ? QStringLiteral("%1: nothing to fix").arg(scope)
                             : QStringLiteral("%1: %2").arg(scope, problemCount(total)));
    headRow->addWidget(head, 1);
    auto *dismiss = new QToolButton(m_findings);
    dismiss->setObjectName(QStringLiteral("boardChatFindingsClose"));
    dismiss->setText(QStringLiteral("×"));
    dismiss->setToolTip(QStringLiteral("Put this list away. Check lists them again."));
    dismiss->setCursor(Qt::PointingHandCursor);
    dismiss->setFocusPolicy(Qt::NoFocus);
    headRow->addWidget(dismiss, 0);
    m_findingsLayout->addLayout(headRow);
    QObject::connect(dismiss, &QToolButton::clicked, this, [this] {
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
        // `cwd`, which would take these rows out of their own stylesheet rules (CardDetail's
        // m_meta carries the same comment).
        row->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
        row->setCursor(Qt::PointingHandCursor);
        row->setText(QStringLiteral("<a href=\"fix\" style=\"color:%1;text-decoration:none\">%2 "
                                    "· %3</a> · %4")
                         .arg(ink.name(), severity.toHtmlEscaped(), file.toHtmlEscaped(),
                              message.toHtmlEscaped()));
        row->setToolTip((path.isEmpty() ? message : path + QStringLiteral(": ") + message)
                        + QStringLiteral("\n\nClick to draft a fix for the page agent — it "
                                         "goes in the composer, it is not sent."));
        // The draft is built now and carried in the connection: the href only has to be clickable.
        const QString request = board::fixRequest(path, message, total);
        const QString card = cardRef.match(path.isEmpty() ? message : path).captured(1);
        const QString draftText = card.isEmpty()
            ? request
            : QStringLiteral("%1 (card #%2)").arg(request, card.toUpper());
        QObject::connect(row, &QLabel::linkActivated, this,
                         [this, draftText](const QString &) { prefill(draftText); });
        m_findingsLayout->addWidget(row);
    }
    m_findings->show();
    return total;
}

// ------------------------------------------------------------------------------- the survey

// `board_survey {root, project, hints, counts, proposals, git}` (19.18): what `project_probe` found
// offline and what an import would create. The agent narrates the same data in its opening turn —
// this is the part the owner has to *act* on, so it is checkboxes and a button rather than prose.
//
// The GitHub corpus is a link and nothing else until #GDQN (the engine) and #ZKR0 (its Switchboard
// surface) land. Nothing here fetches or syncs, and the block says so in a sentence rather than
// leaving the reader to wonder why the link does not do more.
void HelperChatPanel::showSurvey(const QJsonObject &event)
{
    if (m_survey == nullptr)
        return;
    clearLayout(m_surveyLayout);
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
    // The project the probe walked. An event that does not name it falls back to the board's root,
    // which the `board` event gave us — that is what `setWorkspace` is for, and it is also what
    // makes the board folder below readable instead of a second absolute path.
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
    QObject::connect(dismiss, &QToolButton::clicked, this, [this] { hideSurvey(); });

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
        QObject::connect(link, &QLabel::linkActivated, this, [](const QString &target) {
            QDesktopServices::openUrl(QUrl(target));
        });
        // The offer the card asks for (owner, 2026-09-19: "if its .git, it should offer to look
        // on github.com for an issues corpus to sync"). Looking is `forge_sync_plan` (19.14),
        // which #GDQN landed: it reads both sides and reports what a sync *would* do, writing to
        // neither. Bringing them in is the sync, and its surface is #ZKR0 — so this offers the
        // look and says plainly where the rest lives. Nothing here ever sends `forge_sync_run`.
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
        m_forgeLook->setEnabled(!m_running);
        lookRow->addWidget(m_forgeLook, 0);
        lookRow->addStretch(1);
        m_surveyLayout->addLayout(lookRow);
        QObject::connect(m_forgeLook, &QToolButton::clicked, this, [this] { lookForIssues(); });
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
        QObject::connect(box, &QCheckBox::toggled, this, [this] {
            m_importKeys = tickedKeys(m_survey);
            if (m_import != nullptr) {
                m_import->setText(QStringLiteral("Import %1 card%2")
                                      .arg(m_importKeys.size())
                                      .arg(m_importKeys.size() == 1 ? QString()
                                                                    : QStringLiteral("s")));
                m_import->setEnabled(!m_importKeys.isEmpty());
            }
        });
    }

    m_importKeys = tickedKeys(m_survey);
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
    QObject::connect(m_import, &QToolButton::clicked, this, [this] { applyImport(); });

    m_survey->show();
}

// `forge_sync_plan` (19.14): what a sync between this board and its GitHub issues *would* do.
// It is a dry run by construction — the protocol says it "writes to neither side" — so this is
// safe to offer on a board the owner has only just made, which is exactly when the survey asks.
void HelperChatPanel::lookForIssues()
{
    if (m_forgeRepo.isEmpty() || !m_forgeRequest.isEmpty())
        return;
    m_forgeRequest = nextRequestId ? nextRequestId() : QString();
    if (m_forgeLook != nullptr) {
        m_forgeLook->setEnabled(false);
        m_forgeLook->setText(QStringLiteral("Looking\u2026"));
    }
    if (m_forgeResult != nullptr)
        m_forgeResult->setText(QStringLiteral("Asking github.com about %1\u2026").arg(m_forgeRepo));
    QJsonObject message{{QStringLiteral("type"), QStringLiteral("forge_sync_plan")},
                        {QStringLiteral("repo"), m_forgeRepo}};
    if (!m_forgeRequest.isEmpty())
        message.insert(QStringLiteral("id"), m_forgeRequest);
    send(message);
}

// Put the button back, whatever the answer was.
static void settleForgeButton(QToolButton *button)
{
    if (button == nullptr)
        return;
    button->setText(QStringLiteral("Look again"));
    button->setEnabled(true);
}

void HelperChatPanel::showForgePlan(const QJsonObject &event)
{
    m_forgeRequest.clear();
    settleForgeButton(m_forgeLook);
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
        ? QStringLiteral("%1 and this board already agree \u2014 there is nothing to bring in.")
              .arg(m_forgeRepo)
        : QStringLiteral("%1: %2.").arg(m_forgeRepo, parts.join(QStringLiteral(", ")));
    // Said every time, not only when there is something: a count that looked like a result and
    // then wrote nothing would be the more surprising of the two.
    m_forgeResult->setText(what + QStringLiteral("  Nothing was written on either side \u2014 "
                                                 "syncing them is its own surface (#ZKR0). Ask "
                                                 "here and the agent can bring the same issues in "
                                                 "as ordinary cards."));
}

void HelperChatPanel::showForgeError(const QJsonObject &event)
{
    m_forgeRequest.clear();
    settleForgeButton(m_forgeLook);
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

void HelperChatPanel::hideSurvey()
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

// `board_import_apply {keys}` (19.13), the same message the import dialog sends. The keys are
// re-derived from the project on the worker side and never trusted from here, so a stale tick
// cannot create a card twice.
void HelperChatPanel::applyImport()
{
    m_importKeys = tickedKeys(m_survey);
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

// ------------------------------------------------------------------------------- the context chip

void HelperChatPanel::updateContextChip(const QJsonObject &event)
{
    if (m_context == nullptr)
        return;
    const qint64 used = event.value(QStringLiteral("used_tokens")).toVariant().toLongLong();
    const qint64 window = event.value(QStringLiteral("window")).toVariant().toLongLong();
    const qint64 limit = event.value(QStringLiteral("limit_tokens")).toVariant().toLongLong();
    const double percent = event.value(QStringLiteral("percent")).toDouble();
    if (window <= 0) {
        m_context->hide();
        return;
    }
    // The pane's words exactly, because it is the same measurement: what is left of the window the
    // next request is measured against (src/Pane.h, updateContextLabel).
    const double left = qMax(0.0, 100.0 - percent);
    m_context->setText(QStringLiteral("%1% left")
                           .arg(QString::number(left, 'f', left < 10 ? 1 : 0)));
    const bool near = limit > 0 && used >= limit * 9 / 10;
    m_context->setProperty("warn", near);
    m_context->style()->unpolish(m_context);
    m_context->style()->polish(m_context);
    m_context->setToolTip(QStringLiteral("The page agent's conversation: %1 of %2 tokens%3\n"
                                         "Auto-compacts at %4 tokens")
                              .arg(QLocale().toString(used), QLocale().toString(window),
                                   event.value(QStringLiteral("estimated")).toBool()
                                       ? QStringLiteral(" (estimated)")
                                       : QString(),
                                   QLocale().toString(limit)));
    m_context->show();
}

// ------------------------------------------------------------------------------- the microphone

void HelperChatPanel::toggleVoice()
{
    if (m_capture != nullptr && m_capture->recording())
        stopVoice();
    else
        startVoice();
}

void HelperChatPanel::startVoice()
{
    const auto say = [this](const QString &text) {
        if (onStatus)
            onStatus(text);
    };
    if (!voiceEnabled()) {
        say(QStringLiteral("Voice transcription is off (Options › Voice)."));
        return;
    }
    if (m_transcribing) {
        say(QStringLiteral("Still transcribing the last clip…"));
        return;
    }
    // Nothing is recorded without a key: the clip would have nowhere to go.
    if (!voiceKeyStored()) {
        offerVoiceKey();
        return;
    }
    ensureCapture();
    voice::Options options;
    options.tool = QSettings().value(QStringLiteral("voice/tool")).toString();
    options.device = QSettings().value(QStringLiteral("voice/device")).toString();
    options.seconds = voiceSeconds();
    QString error;
    if (!m_capture->start(options, &error)) {
        say(error);
        updateVoiceChip();
        return;
    }
    updateVoiceChip();
    say(QStringLiteral("Listening… click the microphone again to transcribe."));
}

void HelperChatPanel::stopVoice()
{
    if (m_capture == nullptr || !m_capture->recording())
        return;
    m_capture->stop();
    if (onStatus)
        onStatus(QStringLiteral("Transcribing…"));
    updateVoiceChip();
}

void HelperChatPanel::ensureCapture()
{
    if (m_capture != nullptr)
        return;
    m_capture = new voice::Capture(this);
    QObject::connect(m_capture, &voice::Capture::ready, this,
                     [this](const QString &path, qint64 ms) {
        Q_UNUSED(ms);
        m_voiceClip = path;
        m_transcribing = true;
        // Our own request id, from the view's prefix: the worker echoes it back, so a clip
        // recorded here can never land in a terminal pane's prompt box and vice versa.
        m_voiceRequest = nextRequestId ? nextRequestId() : QString();
        updateVoiceChip();
        send({{QStringLiteral("type"), QStringLiteral("transcribe")},
              {QStringLiteral("id"), m_voiceRequest},
              {QStringLiteral("path"), path},
              {QStringLiteral("model"), voiceModel()}});
    });
    QObject::connect(m_capture, &voice::Capture::failed, this, [this](const QString &message) {
        m_transcribing = false;
        updateVoiceChip();
        if (onStatus)
            onStatus(message);
    });
    QObject::connect(m_capture, &voice::Capture::elapsed, this, [this] { updateVoiceChip(); });
}

void HelperChatPanel::onTranscribed(const QJsonObject &event)
{
    // The clip has done its work; Relay keeps no audio.
    if (!m_voiceClip.isEmpty()) {
        QFile::remove(m_voiceClip);
        m_voiceClip.clear();
    }
    m_transcribing = false;
    m_voiceRequest.clear();
    updateVoiceChip();
    if (!event.value(QStringLiteral("ok")).toBool()) {
        if (event.value(QStringLiteral("code")).toString() == QStringLiteral("no_key")) {
            offerVoiceKey();
            return;
        }
        if (onStatus)
            onStatus(QStringLiteral("Voice: ")
                     + event.value(QStringLiteral("error")).toString());
        return;
    }
    const QString text = event.value(QStringLiteral("text")).toString();
    if (text.isEmpty()) {
        if (onStatus)
            onStatus(QStringLiteral("Nothing was said."));
        return;
    }
    if (m_composer == nullptr)
        return;
    // Inserted with the cursor, not by replacing the document, so Ctrl+Z still undoes it — and
    // never submitted, so a misheard word is fixed before anything runs.
    QTextCursor cursor = m_composer->textCursor();
    const QString before = m_composer->toPlainText();
    const voice::Insertion insertion = voice::insertTranscript(before, cursor.position(), text);
    const int at = qBound(0, cursor.position(), before.size());
    cursor.setPosition(at);
    cursor.insertText(insertion.text.mid(at, insertion.text.size() - before.size()));
    cursor.setPosition(qBound(0, insertion.cursor, insertion.text.size()));
    m_composer->setTextCursor(cursor);
    m_composer->setFocus(Qt::OtherFocusReason);
}

void HelperChatPanel::updateVoiceChip()
{
    if (m_mic == nullptr)
        return;
    const bool recording = m_capture != nullptr && m_capture->recording();
    m_mic->setProperty("recording", recording);
    m_mic->style()->unpolish(m_mic);
    m_mic->style()->polish(m_mic);
    if (recording) {
        const qint64 seconds = m_capture->elapsedMs() / 1000;
        m_mic->setIcon(micIcon(theme::Error));
        m_mic->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_mic->setText(QLatin1Char(' ') + clockText(seconds));
        m_mic->setToolTip(QStringLiteral("Listening… click to transcribe"));
    } else {
        m_mic->setIcon(micIcon(theme::TextMuted));
        m_mic->setToolButtonStyle(Qt::ToolButtonIconOnly);
        m_mic->setText(QString());
        m_mic->setToolTip(m_transcribing
                              ? QStringLiteral("Transcribing…")
                              : QStringLiteral("Speak instead of typing. The transcript goes into "
                                               "the box, never straight to the agent."));
    }
}

// The openrouter row of the worker's `presets` event. Before it arrives nothing is known, and the
// worker answers with the `no_key` code instead of this guess.
bool HelperChatPanel::voiceKeyStored() const
{
    if (m_presets.isEmpty())
        return true;
    for (const QJsonValue &value : m_presets) {
        const QJsonObject preset = value.toObject();
        if (preset.value(QStringLiteral("id")).toString() == QStringLiteral("openrouter"))
            return preset.value(QStringLiteral("has_stored_key")).toBool();
    }
    return false;
}

void HelperChatPanel::offerVoiceKey()
{
    if (onStatus)
        onStatus(QStringLiteral("Voice needs an OpenRouter key."));
    QMessageBox box(window());
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(QStringLiteral("Voice transcription"));
    box.setText(QStringLiteral("Voice needs an OpenRouter key."));
    // The keys dialog itself belongs to the terminal pane (src/ModelSettings.h, opened from
    // src/Pane.h); the Switchboard has no handle on it, so this names where it lives rather than
    // opening a second copy. The Warp import is a plain worker message and is offered here.
    box.setInformativeText(QStringLiteral(
        "Relay transcribes with %1 on OpenRouter, whatever model the board runs on, so voice needs "
        "an OpenRouter key of its own. Add one in Options › API keys. Recordings are sent to "
        "OpenRouter and Google; nothing is recorded or sent until a key is stored.")
                               .arg(voiceModel()));
    QPushButton *warp = box.addButton(QStringLiteral("Import from Warp"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() == warp)
        send({{QStringLiteral("type"), QStringLiteral("import_warp")}});
}

// ------------------------------------------------------------------------------- events

// 19.18's events, kept away from any card thread exactly as a cleanup's are (the `cleanup: true`
// precedent): they carry `chat: true` and a `turn_id`, and never a `card_id`. Anything this
// returns false for is somebody else's — a card's turn, the pane's own — and must go on unread.
bool HelperChatPanel::handleEvent(const QString &type, const QJsonObject &event)
{
    // Whose answer is this? One helper worker serves the whole tab (#FEJQ), so every panel sees
    // every turn event and only one of them may take it. The worker tags what it sends with the
    // pane that asked; an event with no tag is the board's, because that is what the worker sent
    // before there were other panes and the Switchboard is the panel that was there then.
    //
    // Only the conversation is filtered this way: `transcribed` is already told apart by the
    // request id it answers (a phone's clip and a terminal pane's carry their own), and so is the
    // forge look below.
    const QJsonValue chatValue = event.value(QStringLiteral("chat"));
    if (type.startsWith(QStringLiteral("board_chat")) || chatValue.toBool()
        || chatValue.isObject()) {
        const QString from = event.value(QStringLiteral("pane")).toString();
        if (!from.isEmpty() ? from != m_pane : !isBoard())
            return false;
    }

    if (type == QStringLiteral("board_chat_started")) {
        m_streamed.clear();
        m_thinking.clear();
        m_thinkingDone = false;
        m_thinkingMs = 0;
        const QJsonObject chat = event.value(QStringLiteral("chat")).toObject();
        if (!chat.isEmpty())
            setChatState(chat);
        const QString turn = event.value(QStringLiteral("turn_id")).toString();
        if (!turn.isEmpty())
            m_turnId = turn;
        // The turn starts *now* as far as this panel is concerned, whatever the block's `seconds`
        // rounded to: the event and the turn are the same instant.
        setProperty(kClockBase, 0.0);
        m_clock.start();
        setRunning(true);
        setProgress(QString());
        rebuildLog();
        return true;
    }
    if (type == QStringLiteral("board_chat_queued")) {
        // `chat` is `true` here, not the block (19.18's example): the worker announces the item and
        // sends the state separately. Honour a block when one does come — an older or newer worker
        // may carry it — and otherwise add the row from the event itself.
        const QJsonValue chat = event.value(QStringLiteral("chat"));
        if (chat.isObject()) {
            setChatState(chat.toObject());
        } else {
            const QString id = event.value(QStringLiteral("id")).toString();
            const QString text = event.value(QStringLiteral("text")).toString();
            bool known = false;
            for (const QueueItem &item : std::as_const(m_queue))
                known = known || item.id == id;
            if (!id.isEmpty() && !known)
                m_queue.append(QueueItem{id, text});
            rebuildQueue();
        }
        if (onStatus)
            onStatus(QStringLiteral("Queued — it starts when this turn ends."));
        return true;
    }
    if (type == QStringLiteral("board_chat_state")
        || type == QStringLiteral("board_chat_cancelled")) {
        setChatState(event.value(QStringLiteral("chat")).toObject());
        return true;
    }
    if (type == QStringLiteral("board_survey")) {
        if (!isBoard())
            return false;            // a fresh board's opening turn; no other pane has one
        // The narration turn starts immediately after this event and carries no `survey` flag of
        // its own, so the head is told here; `settleTurn` and the next `chat` block clear it.
        m_surveyTurn = true;
        showSurvey(event);
        return true;
    }
    if (type == QStringLiteral("forge_sync_planned")) {
        if (m_forgeRequest.isEmpty()
            || event.value(QStringLiteral("id")).toString() != m_forgeRequest)
            return false;
        showForgePlan(event);
        return true;
    }
    if (type == QStringLiteral("error") && !m_forgeRequest.isEmpty()
        && event.value(QStringLiteral("id")).toString() == m_forgeRequest) {
        // Every way a look can fail comes back as one `error` (19.14): no credential, a rate
        // limit, an unreachable forge, a board pointed at another repository, the busy guard.
        showForgeError(event);
        return true;
    }
    if (type == QStringLiteral("transcribed")) {
        // Only ours. A phone's clip and a terminal pane's carry their own ids and must reach their
        // own boxes, so an id this panel did not send is not this panel's to take.
        const QString request = event.value(QStringLiteral("id")).toString();
        if (m_voiceRequest.isEmpty() || request != m_voiceRequest)
            return false;
        onTranscribed(event);
        return true;
    }

    // Everything below belongs to the page agent's turn, and to nothing else.
    if (!event.value(QStringLiteral("chat")).toBool())
        return false;

    // A turn whose start this panel never saw. The **survey** is exactly that: the worker runs it
    // straight off `board_open` (board_protocol._maybe_survey calls `chat.ask` itself) and there is
    // no `board_chat_started` for it, so without this the opening turn of a brand new board would
    // stream into a panel that still believed nothing was running — no clock, no Stop, and no busy
    // strip to say what it was reading. Any tagged turn event is proof that a turn is running.
    if (!m_running && type != QStringLiteral("done") && type != QStringLiteral("error")
        && type != QStringLiteral("cancelled") && type != QStringLiteral("turn_summary")
        && type != QStringLiteral("context")) {
        const QString turn = event.value(QStringLiteral("turn_id")).toString();
        if (!turn.isEmpty())
            m_turnId = turn;
        setProperty(kClockBase, 0.0);
        m_clock.start();
        setRunning(true);
    }

    if (type == QStringLiteral("delta")) {
        appendDelta(event.value(QStringLiteral("text")).toString());
        return true;
    }
    if (type == QStringLiteral("thinking_delta") || type == QStringLiteral("thinking")) {
        appendThinking(event.value(QStringLiteral("text")).toString());
        return true;
    }
    if (type == QStringLiteral("thinking_done")) {
        finishThinking(event.value(QStringLiteral("elapsed_ms")).toVariant().toLongLong());
        return true;
    }
    if (type == QStringLiteral("tool_started") || type == QStringLiteral("tool_result")
        || type == QStringLiteral("status")) {
        setProgress(progressLine(type, event));
        return true;
    }
    if (type == QStringLiteral("context")) {
        updateContextChip(event);
        return true;
    }
    if (type == QStringLiteral("turn_started") || type == QStringLiteral("turn_summary")) {
        return true;    // consumed: the strip and the log already say what these carry
    }
    if (type == QStringLiteral("error")) {
        // Why the turn stopped, in the log where the prompt that caused it is — not in a toast
        // that is gone before it is read.
        const QString text = event.value(QStringLiteral("text")).toString();
        if (!text.trimmed().isEmpty())
            m_history.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("error")},
                                         {QStringLiteral("text"), text}});
        settleTurn(QStringLiteral("error"));
        if (onStatus && !text.trimmed().isEmpty())
            onStatus(text);
        return true;
    }
    if (type == QStringLiteral("done") || type == QStringLiteral("cancelled")) {
        settleTurn(type);
        return true;
    }
    return false;
}

}  // namespace relay
