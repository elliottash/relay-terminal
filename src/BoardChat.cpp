// SPDX-License-Identifier: AGPL-3.0-or-later
#include "BoardChat.h"

#include "CopyOnSelect.h"
#include "RichEditor.h"
#include "Theme.h"
#include "ToolLabel.h"
#include "Voice.h"

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
#include <QHBoxLayout>
#include <QJsonValue>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
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
#include <utility>

namespace relay {
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

// The header line: who this is, and — while it turns — how long it has been turning. The state
// word stays out of the *pane's* header (owner, 2026-09-19, "Header and Activity decisions"); this
// is the panel's own head, where a running clock is the whole point.
void drawHead(QLabel *head, bool running, bool survey, qint64 seconds)
{
    if (head == nullptr)
        return;
    QString text = QStringLiteral("Switchboard agent");
    if (running)
        text += QStringLiteral(" · ") + clockText(seconds);
    if (running && survey)
        text += QStringLiteral(" · survey");
    head->setText(text);
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

// Top to bottom: who this is and the board-wide buttons, then whatever a Check turned up, then the
// survey's offer, then the conversation, then the strip that says what the turn is doing, then the
// queue, then the composer. Everything is a row in one panel inside the list page — a pane or
// in-pane surface, never a floating strip (owner's standing rule; the cleanup panel above it is
// built the same way).
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

BoardChatPanel::BoardChatPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("boardChatPanel"));
    setAttribute(Qt::WA_StyledBackground);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);

    // ---- the head row: the agent's name and clock, then the board-wide buttons ----------------
    auto *headRow = new QHBoxLayout;
    headRow->setSpacing(6);
    m_head = new QLabel(this);
    m_head->setObjectName(QStringLiteral("boardChatHead"));
    headRow->addWidget(m_head, 0);
    headRow->addStretch(1);
    m_toolRow = new QHBoxLayout;
    m_toolRow->setSpacing(6);
    headRow->addLayout(m_toolRow, 0);
    layout->addLayout(headRow);

    // Check, beside Clean up: the board's own format check over every card, deterministic and
    // free, whose findings draft a fix request into this composer. The panel builds it because the
    // panel is where it belongs; a board that brings its own (BoardView::buildChatPanel reparents
    // one in) replaces this one rather than standing beside it — see addToolWidget().
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
    m_survey = new QWidget(this);
    m_survey->setObjectName(QStringLiteral("boardChatSurvey"));
    m_survey->setAttribute(Qt::WA_StyledBackground);
    m_surveyLayout = new QVBoxLayout(m_survey);
    m_surveyLayout->setContentsMargins(0, 0, 0, 0);
    m_surveyLayout->setSpacing(3);
    m_survey->hide();
    layout->addWidget(m_survey);

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
        if (!url.scheme().isEmpty() && url.scheme() != QStringLiteral("file")) {
            QDesktopServices::openUrl(url);
            return;
        }
        const QString path = url.isLocalFile() ? url.toLocalFile() : url.path();
        if (onOpenFile && !path.isEmpty())
            onOpenFile(path);
    });

    // ---- the busy strip: what the turn is doing this second, and the one control that stops it -
    m_busy = new QWidget(this);
    m_busy->setObjectName(QStringLiteral("boardChatBusy"));
    m_busy->setAttribute(Qt::WA_StyledBackground);
    auto *busyRow = new QHBoxLayout(m_busy);
    busyRow->setContentsMargins(0, 0, 0, 0);
    busyRow->setSpacing(6);
    auto *busyLabel = new QLabel(QStringLiteral("✦ Switchboard agent"), m_busy);
    busyLabel->setObjectName(QStringLiteral("boardChatBusyLabel"));
    busyRow->addWidget(busyLabel, 0);
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
    layout->addWidget(m_busy);
    QObject::connect(m_stop, &QToolButton::clicked, this, [this] { stopTurn(); });

    // ---- the queue (19.18): worker-side, in delivery order ------------------------------------
    m_queueBox = new QWidget(this);
    m_queueBox->setObjectName(QStringLiteral("boardChatQueue"));
    m_queueBox->setAttribute(Qt::WA_StyledBackground);
    m_queueLayout = new QVBoxLayout(m_queueBox);
    m_queueLayout->setContentsMargins(0, 0, 0, 0);
    m_queueLayout->setSpacing(2);
    m_queueBox->hide();
    layout->addWidget(m_queueBox);

    // ---- the composer -------------------------------------------------------------------------
    m_composerRow = new QHBoxLayout;
    m_composerRow->setSpacing(6);
    m_composer = new RichEditor(this);
    m_composer->setObjectName(QStringLiteral("boardChatComposer"));
    m_composer->setAutoHeight(1, 6);
    m_composer->setPlaceholders({QStringLiteral("Ask about the board — Enter sends, a second "
                                                "prompt queues"),
                                 QStringLiteral("Ask about the board — Enter sends"),
                                 QStringLiteral("Ask about the board…"),
                                 QStringLiteral("Ask…")});
    m_composer->installEventFilter(this);
    // Every route sends: this box has one destination (owner's words to the page agent), so the
    // chords that mean "terminal" or "agent" in a pane must not silently do nothing here.
    m_composer->onSubmit = [this](const QString &) { sendPrompt(); };
    m_composerRow->addWidget(m_composer, 1);

    // The microphone, protocol 16: record, `transcribe`, insert the text at the cursor. The
    // pane's own SVG, from the same place `Pane::stripIcon` reads it, so the two composers wear
    // one microphone. It was an emoji glyph until the live run showed it as a missing-glyph box:
    // the UI font has no U+1F3A4 and Qt does not fall back to the colour-emoji font for a
    // QToolButton's text.
    m_mic = new QToolButton(this);
    m_mic->setObjectName(QStringLiteral("boardChatMic"));
    m_mic->setIcon(micIcon(theme::TextMuted));
    m_mic->setIconSize(QSize(14, 14));
    m_mic->setAccessibleName(QStringLiteral("Voice transcription"));
    m_mic->setCursor(Qt::PointingHandCursor);
    m_mic->setFocusPolicy(Qt::NoFocus);
    m_composerRow->addWidget(m_mic, 0);
    QObject::connect(m_mic, &QToolButton::clicked, this, [this] { toggleVoice(); });

    // "72% left" — the pane's chip, for the conversation the page is holding. `context` rides on
    // the `chat: true` tagging (19.18) precisely so this can exist.
    m_context = new QLabel(this);
    m_context->setObjectName(QStringLiteral("boardChatContext"));
    m_context->hide();
    m_composerRow->addWidget(m_context, 0);

    m_send = new QToolButton(this);
    m_send->setObjectName(QStringLiteral("boardChatSend"));
    m_send->setCursor(Qt::PointingHandCursor);
    m_send->setFocusPolicy(Qt::NoFocus);   // Tab stays with the composer
    m_send->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_composerRow->addWidget(m_send, 0);
    QObject::connect(m_send, &QToolButton::clicked, this, [this] {
        if (m_running)
            stopTurn();
        else
            sendPrompt();
    });
    layout->addLayout(m_composerRow);

    // A streamed answer re-renders at most 25 times a second; a fast provider sends deltas far
    // faster than that and the log is a whole QTextDocument each time.
    m_render = new QTimer(this);
    m_render->setSingleShot(true);
    m_render->setInterval(40);
    QObject::connect(m_render, &QTimer::timeout, this, [this] { rebuildLog(); });

    auto *clock = new QTimer(this);
    clock->setObjectName(clockTimerName());
    clock->setInterval(1000);
    QObject::connect(clock, &QTimer::timeout, this, [this] {
        const double base = property(kClockBase).toDouble();
        drawHead(m_head, m_running, m_surveyTurn,
                 qint64(base) + (m_clock.isValid() ? m_clock.elapsed() / 1000 : 0));
    });

    updateVoiceChip();
    setRunning(false);
    rebuildLog();
}

// ------------------------------------------------------------------------------- wiring

void BoardChatPanel::send(QJsonObject message)
{
    if (!message.contains(QStringLiteral("id")) && nextRequestId)
        message.insert(QStringLiteral("id"), nextRequestId());
    if (onSend)
        onSend(message);
}

// Clean up and Check. The widgets belong to whoever made them — BoardView owns Clean up, its
// Stop/Clean up text and its run (19.9) — and this only gives them a home.
void BoardChatPanel::addToolWidget(QWidget *widget)
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

// The model box, once #BRD3 has landed one: among the chips, left of the microphone, where a
// pane's is. Never at the end — Send is the last thing on the row and stays there.
void BoardChatPanel::addComposerWidget(QWidget *widget)
{
    if (widget == nullptr || m_composerRow == nullptr)
        return;
    widget->setParent(this);
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
void BoardChatPanel::setChatState(const QJsonObject &chat)
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

// Send or Stop, one button. A property and not a font: a stylesheet rule with a pseudo-state that
// changed the font would paint one width and measure another (tests/buttonfit_test.cpp, and the
// same comment on BoardView::updateCleanupButton).
void BoardChatPanel::setRunning(bool running)
{
    // A sync takes the board's busy guard (19.14), which this conversation holds while it turns:
    // the look waits rather than coming back as a refusal the owner has to read.
    if (m_forgeLook != nullptr && m_forgeRequest.isEmpty())
        m_forgeLook->setEnabled(!running);
    m_running = running;
    if (m_send != nullptr) {
        m_send->setText(running ? QStringLiteral("Stop") : QStringLiteral("Send"));
        m_send->setToolTip(running
            ? QStringLiteral("Stop this turn. The queue carries on with the next prompt.")
            : QStringLiteral("Ask the page agent (Enter). A prompt typed while it answers joins "
                             "the queue rather than being refused."));
        m_send->setProperty("running", running);
        m_send->style()->unpolish(m_send);
        m_send->style()->polish(m_send);
    }
    if (m_busy != nullptr)
        m_busy->setVisible(running);
    if (auto *clock = findChild<QTimer *>(clockTimerName())) {
        if (running)
            clock->start();
        else
            clock->stop();
    }
    const double base = property(kClockBase).toDouble();
    drawHead(m_head, running, m_surveyTurn,
             qint64(base) + (running && m_clock.isValid() ? m_clock.elapsed() / 1000 : 0));
}

void BoardChatPanel::setProgress(const QString &line)
{
    m_progress = line;
    if (m_busyWhat == nullptr)
        return;
    const int room = qMax(60, m_busyWhat->width());
    m_busyWhat->setText(QFontMetrics(m_busyWhat->font()).elidedText(line, Qt::ElideRight, room));
    m_busyWhat->setToolTip(line);
    m_busyWhat->setVisible(m_running && !line.isEmpty());
}

void BoardChatPanel::appendDelta(const QString &text)
{
    m_streamed += text;
    if (!m_render->isActive())
        m_render->start();
}

void BoardChatPanel::appendThinking(const QString &text)
{
    if (m_thinking.size() < 200000)      // the terminal's own cap (src/Pane.h)
        m_thinking += text;
    if (!m_render->isActive())
        m_render->start();
}

void BoardChatPanel::finishThinking(qint64 ms)
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
void BoardChatPanel::settleTurn(const QString &how)
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

void BoardChatPanel::rebuildLog()
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

    if (!any)
        insertLine(cursor, QStringLiteral("Ask about the board itself — what is where, what "
                                          "duplicates what, how to reorganize it. Enter sends; a "
                                          "second prompt queues behind the first."), muted, 0);

    // Follow the stream while it runs; otherwise leave the reader where they were.
    bar->setValue(m_running || atBottom ? bar->maximum() : was);
}

QString BoardChatPanel::transcript() const
{
    return m_log != nullptr ? m_log->document()->toPlainText() : QString();
}

QString BoardChatPanel::draft() const
{
    return m_composer != nullptr ? m_composer->toPlainText() : QString();
}

// ------------------------------------------------------------------------------- sending

// 19.18: a second prompt **queues**, it is not refused. The page agent never refuses its own
// prompt (#N8VK's FIFO semantics, the pane's rule), so nothing here checks `m_running` first — the
// worker answers with `board_chat_queued` and the queue row appears from that.
void BoardChatPanel::sendPrompt()
{
    if (m_composer == nullptr)
        return;
    const QString text = m_composer->toPlainText().trimmed();
    if (text.isEmpty())
        return;
    send({{QStringLiteral("type"), QStringLiteral("board_chat")},
          {QStringLiteral("text"), text}});
    m_composer->clear();
    m_composer->remember(text);
}

void BoardChatPanel::stopTurn()
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
void BoardChatPanel::prefill(const QString &text)
{
    if (m_composer == nullptr)
        return;
    m_composer->setPlainText(text);
    focusComposer();
}

void BoardChatPanel::focusComposer()
{
    if (m_composer == nullptr)
        return;
    m_composer->setFocus(Qt::OtherFocusReason);
    QTextCursor cursor = m_composer->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_composer->setTextCursor(cursor);
}

bool BoardChatPanel::composerHasFocus() const
{
    return m_composer != nullptr && m_composer->hasFocus();
}

// The slow path into this box is the mouse (WARP.md's standing hint rule): clicking into the
// composer teaches the key that would have put the cursor here. BoardView registers Ctrl+/.
bool BoardChatPanel::eventFilter(QObject *object, QEvent *event)
{
    if (object == m_composer && event->type() == QEvent::FocusIn) {
        auto *focus = static_cast<QFocusEvent *>(event);
        if (focus->reason() == Qt::MouseFocusReason && onHint)
            onHint(QStringLiteral("board.chat"), QStringLiteral("Ctrl+/"));
    }
    return QWidget::eventFilter(object, event);
}

// ------------------------------------------------------------------------------- the queue

// One row per queued prompt, in delivery order. The worker's queue is authoritative (19.18): a
// click sends `board_chat_queue_remove` / `board_chat_queue_move` and waits for the
// `board_chat_state` that answers it. Nothing here mutates `m_queue`, so a row can never show an
// order the worker does not have — which is what would happen the first time a move was refused.
void BoardChatPanel::rebuildQueue()
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
int BoardChatPanel::showFindings(const QJsonArray &items, const QString &section)
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
void BoardChatPanel::showSurvey(const QJsonObject &event)
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
void BoardChatPanel::lookForIssues()
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

void BoardChatPanel::showForgePlan(const QJsonObject &event)
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

void BoardChatPanel::showForgeError(const QJsonObject &event)
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

void BoardChatPanel::hideSurvey()
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
void BoardChatPanel::applyImport()
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

void BoardChatPanel::updateContextChip(const QJsonObject &event)
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

void BoardChatPanel::toggleVoice()
{
    if (m_capture != nullptr && m_capture->recording())
        stopVoice();
    else
        startVoice();
}

void BoardChatPanel::startVoice()
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

void BoardChatPanel::stopVoice()
{
    if (m_capture == nullptr || !m_capture->recording())
        return;
    m_capture->stop();
    if (onStatus)
        onStatus(QStringLiteral("Transcribing…"));
    updateVoiceChip();
}

void BoardChatPanel::ensureCapture()
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

void BoardChatPanel::onTranscribed(const QJsonObject &event)
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

void BoardChatPanel::updateVoiceChip()
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
bool BoardChatPanel::voiceKeyStored() const
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

void BoardChatPanel::offerVoiceKey()
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
bool BoardChatPanel::handleEvent(const QString &type, const QJsonObject &event)
{
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
