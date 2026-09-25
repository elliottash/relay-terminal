// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ContextDock.h"

#include "Theme.h"   // the collapsed row's ink

#include <QApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace relay {

namespace {

// The collapsed row's mark, painted as Options, Sessions and Models paint it (owner, 2026-09-20:
// "it should have a question mark icon next to it"): a font's "?" at 14 px is a third of the word
// beside it.
QIcon askIcon(const QColor &ink)
{
    constexpr int kSize = 14;
    QPixmap pixmap(kSize * 2, kSize * 2);   // 2x, so it stays crisp on a scaled desktop
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(ink, 1.2);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QRectF(1.2, 1.2, 11.6, 11.6));
    painter.drawArc(QRectF(4.3, 3.4, 5.4, 4.6), 200 * 16, -250 * 16);
    painter.drawLine(QPointF(7.0, 7.4), QPointF(7.0, 9.0));
    QPen dot(ink, 1.6);
    dot.setCapStyle(Qt::RoundCap);
    painter.setPen(dot);
    painter.drawPoint(QPointF(7.0, 10.8));
    painter.end();
    return QIcon(pixmap);
}

}  // namespace

// The same object names as the other feet, so the theme's stylesheet draws them all alike.
ContextDock::ContextDock(relay::agent::Context *context, const QString &title, const QString &about,
                         QWidget *parent)
    : QWidget(parent), m_context(context), m_title(title), m_about(about)
{
    setObjectName(QStringLiteral("boardChatPanel"));
    setAttribute(Qt::WA_StyledBackground);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ---- collapsed: one button at the bottom right ------------------------------------------
    m_askRow = new QWidget(this);
    m_askRow->setObjectName(QStringLiteral("boardChatAskRow"));
    auto *askLine = new QHBoxLayout(m_askRow);
    askLine->setContentsMargins(10, 6, 10, 6);
    askLine->setSpacing(6);
    askLine->addStretch(1);
    m_ask = new QToolButton(m_askRow);
    m_ask->setObjectName(QStringLiteral("boardChatAsk"));
    m_ask->setIcon(askIcon(theme::TextMuted));
    m_ask->setIconSize(QSize(14, 14));
    m_ask->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_ask->setCursor(Qt::PointingHandCursor);
    m_ask->setFocusPolicy(Qt::StrongFocus);
    m_ask->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    askLine->addWidget(m_ask, 0);
    outer->addWidget(m_askRow);
    connect(m_ask, &QToolButton::clicked, this, [this] {
        // The mouse, not the key: Alt+Q comes in through `focusHelper()` and teaches nothing.
        if (onHelperHint && !m_askHintId.isEmpty())
            onHelperHint();
        focusHelper();
    });

    // ---- expanded: the console, under a row that folds it away ------------------------------
    m_body = new QWidget(this);
    m_body->setObjectName(QStringLiteral("boardChatBody"));
    auto *body = new QVBoxLayout(m_body);
    body->setContentsMargins(10, 8, 10, 8);
    body->setSpacing(6);
    auto *headRow = new QHBoxLayout;
    headRow->setSpacing(6);
    m_head = new QLabel(m_title, m_body);
    m_head->setObjectName(QStringLiteral("boardChatHead"));
    headRow->addWidget(m_head, 0);
    headRow->addStretch(1);
    auto *foldButton = new QToolButton(m_body);
    foldButton->setObjectName(QStringLiteral("boardChatFold"));
    foldButton->setText(QStringLiteral("⌄"));
    foldButton->setToolTip(QStringLiteral("Fold the agent back to one row. The conversation is kept — "
                                          "it opens where you left it."));
    foldButton->setCursor(Qt::PointingHandCursor);
    foldButton->setFocusPolicy(Qt::NoFocus);
    foldButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    headRow->addWidget(foldButton, 0);
    connect(foldButton, &QToolButton::clicked, this, [this] { fold(); });
    body->addLayout(headRow);
    m_body->setVisible(false);
    outer->addWidget(m_body);

    // The cap is a share of the *pane*, so the dock follows its host's size, not its own.
    if (parent)
        parent->installEventFilter(this);
    updateRow();
}

// The console goes with the body in ~QWidget. Its wrapper writes to the context as it goes
// (`TabConsoleContext`), which is why the *host* deletes this dock before its context.
ContextDock::~ContextDock() = default;

void ContextDock::setHelperShortcut(const QString &hintId, const QString &keys)
{
    m_askHintId = hintId;
    m_askKeys = keys;
    updateRow();
}

// Asking for the cursor is asking for the console: Alt+Q, a click on the row and a drafted request
// all mean the box has to be there to type in.
void ContextDock::focusHelper()
{
    if (!onCreateConsole)
        return;
    ensureConsole();
    if (m_collapsed) {
        m_collapsed = false;
        applyCollapsed();
    }
    if (m_console.focusComposer)
        m_console.focusComposer();
}

// A draft, never a send (owner, 2026-09-19: "draft you confirm").
void ContextDock::helperDraft(const QString &text)
{
    focusHelper();
    if (m_console.draftInComposer)
        m_console.draftInComposer(text);
}

void ContextDock::fold()
{
    if (m_collapsed)
        return;
    m_collapsed = true;
    applyCollapsed();
}

QString ContextDock::rowText() const { return m_ask ? m_ask->text() : QString(); }

bool ContextDock::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize)
        updateHeight();
    return QWidget::eventFilter(watched, event);
}

void ContextDock::ensureConsole()
{
    if (m_console || !onCreateConsole || !m_context)
        return;
    m_console = onCreateConsole(m_context, m_body);
    if (!m_console)
        return;
    if (auto *body = qobject_cast<QVBoxLayout *>(m_body->layout()))
        body->addWidget(m_console.widget, 1);
    m_console.widget->show();
    updateHeight();
}

// One row, or the row and the console. Nothing is destroyed either way: an agent folded back keeps
// its conversation, its draft and its queue.
void ContextDock::applyCollapsed()
{
    m_askRow->setVisible(m_collapsed);
    // Folding with the cursor in the composer: hand the focus to the row first, or Qt passes it on
    // as a Tab — onto the ask row, which would unfold what was just folded.
    if (m_collapsed && m_body->isAncestorOf(QApplication::focusWidget()))
        m_ask->setFocus(Qt::OtherFocusReason);
    m_body->setVisible(!m_collapsed);
    if (m_console.setCollapsed)
        m_console.setCollapsed(m_collapsed);
    if (!m_collapsed)
        updateHeight();
}

// "Agent (Alt+Q)" — the live key in the button's own text (#E8V1's label).
void ContextDock::updateRow()
{
    m_ask->setText(m_askKeys.isEmpty() ? QStringLiteral("Agent") : QStringLiteral("Agent (%1)").arg(m_askKeys));
    m_ask->setToolTip(m_askKeys.isEmpty() ? m_about + QLatin1Char('.')
                                          : QStringLiteral("%1 (%2).").arg(m_about, m_askKeys));
}

// At most ~40 % of the pane, and never so little that the transcript is a slot: SettingsPane's rule.
void ContextDock::updateHeight()
{
    const int line = QFontMetrics(font()).lineSpacing();
    const int paneHeight = parentWidget() ? parentWidget()->height() : height();
    const int cap = std::max(10 * line, paneHeight * 2 / 5);
    m_body->setMaximumHeight(cap);
    m_body->setMinimumHeight(std::min(cap, 20 * line));
}

}  // namespace relay
