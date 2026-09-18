// SPDX-License-Identifier: GPL-3.0-or-later
#include "SessionInfo.h"

#include <QApplication>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QStyleOptionToolButton>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

namespace relay::sessioninfo {
namespace {

constexpr auto kScheme = "relay-info";

QString esc(const QString &text) { return text.toHtmlEscaped(); }

// relay-info:<path>?key=value&... with every value percent-encoded by hand, so a path holding
// '&', '=', '#' or '+' comes back exactly as it went in (see linkQuery).
QString link(const QString &path, const QList<QPair<QString, QString>> &query, const QString &label) {
    QStringList parts;
    for (const auto &[key, value] : query)
        if (!value.isEmpty()) parts << key + QLatin1Char('=') + QString::fromLatin1(QUrl::toPercentEncoding(value));
    const QString href = QString::fromLatin1(kScheme) + QLatin1Char(':') + path
                       + (parts.isEmpty() ? QString() : QLatin1Char('?') + parts.join(QLatin1Char('&')));
    return QStringLiteral("<a href=\"%1\">%2</a>").arg(href.toHtmlEscaped(), label);
}

QString when(double epoch, const QDateTime &now) {
    if (epoch <= 0) return QStringLiteral("—");
    const QDateTime at = QDateTime::fromSecsSinceEpoch(qint64(epoch));
    const qint64 ago = at.secsTo(now);
    QString rel;
    if (ago < 60 && ago > -60) rel = QStringLiteral("just now");
    else if (ago > 0 && ago < 3600) rel = QStringLiteral("%1 min ago").arg(ago / 60);
    else if (ago > 0 && ago < 86400) rel = QStringLiteral("%1 h ago").arg(ago / 3600);
    const QString stamp = at.date() == now.date() ? at.toString(QStringLiteral("HH:mm"))
                                                  : at.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    return rel.isEmpty() ? stamp : stamp + QStringLiteral(" · ") + rel;
}

QString row(const QString &label, const QString &html) {
    return QStringLiteral("<tr><td class=k>%1</td><td>%2</td></tr>").arg(esc(label), html);
}

QString usageHtml(const QJsonObject &usage) {
    const qint64 in = usage.value(QStringLiteral("prompt_tokens")).toVariant().toLongLong();
    const qint64 out = usage.value(QStringLiteral("completion_tokens")).toVariant().toLongLong();
    const qint64 total = usage.value(QStringLiteral("total_tokens")).toVariant().toLongLong();
    const qint64 requests = usage.value(QStringLiteral("requests")).toVariant().toLongLong();
    if (requests == 0 && total == 0) return QStringLiteral("<span class=m>none reported yet</span>");
    return QStringLiteral("%1 in · %2 out · %3 total <span class=m>· %4 request%5</span>")
        .arg(compactNumber(in), compactNumber(out), compactNumber(total))
        .arg(requests).arg(requests == 1 ? QString() : QStringLiteral("s"));
}

QString costHtml(const QJsonObject &usage) {
    if (usage.contains(QStringLiteral("cost")))
        return QStringLiteral("$%1").arg(usage.value(QStringLiteral("cost")).toDouble(), 0, 'f', 4);
    return QStringLiteral("<span class=m>not reported by this provider</span>");
}

QString statusMark(const QString &status) {
    if (status == QLatin1String("done")) return QStringLiteral("✓");
    if (status == QLatin1String("failed")) return QStringLiteral("✗");
    if (status == QLatin1String("stopped")) return QStringLiteral("■");
    if (status == QLatin1String("running") || status == QLatin1String("waiting")) return QStringLiteral("●");
    return QStringLiteral("·");
}

// One subagent thread as a line of the history: its link, what it is, how it ended.
QString threadLine(const QJsonObject &thread, const QString &dir, int depth) {
    const QString id = thread.value(QStringLiteral("id")).toString();
    const QString agent = thread.value(QStringLiteral("agent_id")).toString();
    const QString type = thread.value(QStringLiteral("type")).toString();
    const QString title = thread.value(QStringLiteral("title")).toString();
    const QString status = thread.value(QStringLiteral("status")).toString();
    const QString owner = thread.value(QStringLiteral("owner_session")).toString();
    QString label = QStringLiteral("%1 %2%3").arg(esc(agent), esc(type), title.isEmpty() ? QString() : QStringLiteral(" · “") + esc(title) + QStringLiteral("”"));
    QString html = QStringLiteral("<p class=t style=\"margin-left:%1px\">↳ %2 %3 <span class=m>%4%5</span>")
        .arg(18 + depth * 18)
        .arg(statusMark(status),
             link(QStringLiteral("thread"), {{QStringLiteral("id"), id}, {QStringLiteral("dir"), dir},
                                             {QStringLiteral("owner"), owner}}, label),
             esc(status), thread.value(QStringLiteral("model")).toString().isEmpty() ? QString()
                 : QStringLiteral(" · ") + esc(thread.value(QStringLiteral("model")).toString()));
    if (thread.value(QStringLiteral("live")).toBool())
        html += QStringLiteral(" · ") + link(QStringLiteral("live"), {{QStringLiteral("agent"), agent}, {QStringLiteral("thread"), id}},
                                          QStringLiteral("open in the subagents pane"));
    html += QStringLiteral("</p>");
    for (const auto &child : thread.value(QStringLiteral("children")).toArray())
        html += threadLine(child.toObject(), dir, depth + 1);
    return html;
}

QString style() {
    const QPalette palette = QApplication::palette();
    const QString muted = palette.color(QPalette::PlaceholderText).name();
    const QString linkColor = palette.color(QPalette::Link).name();
    return QStringLiteral(
        "<style>td.k{color:%1;padding-right:14px;white-space:nowrap;vertical-align:top}"
        "span.m{color:%1} a{color:%2;text-decoration:none} p.t{margin-top:2px;margin-bottom:2px}"
        "p.turn{margin-top:10px;margin-bottom:2px} h2{margin-bottom:4px} h3{margin-top:16px;margin-bottom:4px}"
        "p.msg{margin-top:6px;margin-bottom:2px} pre{margin-top:0;white-space:pre-wrap}</style>")
        .arg(muted, linkColor);
}

QString sessionHtml(const QJsonObject &info, const QDateTime &now) {
    const QString dir = info.value(QStringLiteral("session_dir")).toString();
    QString title = info.value(QStringLiteral("title")).toString();
    if (title.isEmpty()) title = QStringLiteral("Untitled session");
    QString html = QStringLiteral("<h2>%1</h2>").arg(esc(title));
    if (!info.value(QStringLiteral("live")).toBool())
        html += QStringLiteral("<p class=m><span class=m>A saved session, not the one in this pane.</span></p>");
    html += QStringLiteral("<table>");
    QString model = esc(info.value(QStringLiteral("model")).toString());
    const QString provider = info.value(QStringLiteral("provider")).toString();
    if (!provider.isEmpty()) model += QStringLiteral(" <span class=m>· %1</span>").arg(esc(provider));
    const QString effort = info.value(QStringLiteral("effort")).toString();
    const QString mode = info.value(QStringLiteral("mode")).toString();
    if (!effort.isEmpty()) model += QStringLiteral(" <span class=m>· effort %1</span>").arg(esc(effort));
    if (mode == QLatin1String("plan")) model += QStringLiteral(" <span class=m>· plan mode</span>");
    html += row(QStringLiteral("Model"), model);
    QStringList models;
    for (const auto &value : info.value(QStringLiteral("models")).toArray()) models << value.toString();
    if (models.size() > 1) html += row(QStringLiteral("Models used"), esc(models.join(QStringLiteral(", "))));
    const QJsonObject context = info.value(QStringLiteral("context")).toObject();
    if (!context.isEmpty()) {
        html += row(QStringLiteral("Context"),
                    QStringLiteral("%1 / %2 tokens <span class=m>· %3%%4</span>")
                        .arg(compactNumber(context.value(QStringLiteral("used_tokens")).toVariant().toLongLong()),
                             compactNumber(context.value(QStringLiteral("window")).toVariant().toLongLong()))
                        .arg(context.value(QStringLiteral("percent")).toDouble(), 0, 'f', 1)
                        .arg(context.value(QStringLiteral("estimated")).toBool() ? QStringLiteral(" · estimated") : QString()));
    }
    const QJsonObject usage = info.value(QStringLiteral("usage")).toObject();
    html += row(QStringLiteral("Tokens"), usageHtml(usage));
    html += row(QStringLiteral("Cost"), costHtml(usage));
    const QString sessionId = info.value(QStringLiteral("session_id")).toString();
    const QString file = info.value(QStringLiteral("file")).toString();
    QString sessionCell = QStringLiteral("<code>%1</code>").arg(esc(sessionId));
    if (!file.isEmpty())
        sessionCell += QStringLiteral("<br>") + (info.value(QStringLiteral("file_exists")).toBool(true)
            ? link(QStringLiteral("file"), {{QStringLiteral("path"), file}}, esc(file))
            : esc(file) + QStringLiteral(" <span class=m>(not saved yet: nothing is written before the first turn)</span>"));
    html += row(QStringLiteral("Session"), sessionCell);
    QString workspace = esc(info.value(QStringLiteral("workspace")).toString());
    const QString branch = info.value(QStringLiteral("git_branch")).toString();
    if (!branch.isEmpty()) workspace += QStringLiteral(" <span class=m>· branch %1</span>").arg(esc(branch));
    html += row(QStringLiteral("Workspace"), workspace);
    html += row(QStringLiteral("Started"), esc(when(info.value(QStringLiteral("created")).toDouble(), now)));
    html += row(QStringLiteral("Updated"), esc(when(info.value(QStringLiteral("updated")).toDouble(), now)));
    const int threads = info.value(QStringLiteral("thread_count")).toInt();
    html += row(QStringLiteral("Turns"), QStringLiteral("%1%2").arg(info.value(QStringLiteral("turns")).toInt())
                .arg(threads > 0 ? QStringLiteral(" <span class=m>· %1 subagent thread%2</span>").arg(threads)
                                       .arg(threads == 1 ? QString() : QStringLiteral("s")) : QString()));
    QStringList instructions;
    for (const auto &value : info.value(QStringLiteral("instructions")).toArray()) {
        const QString path = value.toString();
        instructions << link(QStringLiteral("file"), {{QStringLiteral("path"), path}}, esc(QFileInfo(path).fileName().isEmpty() ? path : QFileInfo(path).fileName()));
    }
    html += row(QStringLiteral("Instructions"), instructions.isEmpty() ? QStringLiteral("<span class=m>none loaded</span>")
                                                                        : instructions.join(QStringLiteral(", ")));
    const QString forked = info.value(QStringLiteral("forked_from")).toString();
    if (!forked.isEmpty())
        html += row(QStringLiteral("Forked from"), link(QStringLiteral("session"), {{QStringLiteral("id"), forked}, {QStringLiteral("dir"), dir}},
                                                         QStringLiteral("<code>%1</code>").arg(esc(forked.left(8)))));
    html += QStringLiteral("</table>");

    html += QStringLiteral("<h3>History</h3>");
    const QJsonArray history = info.value(QStringLiteral("history")).toArray();
    if (history.isEmpty()) html += QStringLiteral("<p><span class=m>No turns yet.</span></p>");
    for (const auto &value : history) {
        const QJsonObject turn = value.toObject();
        const int files = turn.value(QStringLiteral("files")).toInt();
        html += QStringLiteral("<p class=turn><b>%1</b> <span class=m>%2%3</span><br>%4</p>")
                    .arg(turn.value(QStringLiteral("turn")).toInt())
                    .arg(esc(when(turn.value(QStringLiteral("time")).toDouble(), now)),
                         files > 0 ? QStringLiteral(" · %1 file%2 changed").arg(files).arg(files == 1 ? QString() : QStringLiteral("s")) : QString(),
                         esc(turn.value(QStringLiteral("prompt")).toString()));
        for (const auto &thread : turn.value(QStringLiteral("threads")).toArray())
            html += threadLine(thread.toObject(), dir, 0);
    }
    const QJsonArray unplaced = info.value(QStringLiteral("unplaced_threads")).toArray();
    if (!unplaced.isEmpty()) {
        html += QStringLiteral("<p class=turn><span class=m>Threads from turns no longer in the history (rewound or compacted away)</span></p>");
        for (const auto &thread : unplaced) html += threadLine(thread.toObject(), dir, 0);
    }
    return html;
}

QString threadHtml(const QJsonObject &info, const QDateTime &now) {
    const QString dir = info.value(QStringLiteral("session_dir")).toString();
    const QString owner = info.value(QStringLiteral("owner_session")).toString();
    const QString ownerTitle = info.value(QStringLiteral("owner_title")).toString();
    const QString parent = info.value(QStringLiteral("parent_thread")).toString();
    QString html;
    // The way back is the first thing on the page.
    QStringList up;
    if (!owner.isEmpty())
        up << (info.value(QStringLiteral("owner_exists")).toBool(true)
                   ? link(QStringLiteral("session"), {{QStringLiteral("id"), owner}, {QStringLiteral("dir"), dir}},
                          QStringLiteral("↑ owner session") + (ownerTitle.isEmpty() ? QString() : QStringLiteral(": “") + esc(ownerTitle) + QStringLiteral("”")))
                   : QStringLiteral("<span class=m>↑ owner session <code>%1</code> (not saved or deleted)</span>").arg(esc(owner.left(8))));
    if (!parent.isEmpty())
        up << link(QStringLiteral("thread"), {{QStringLiteral("id"), parent}, {QStringLiteral("dir"), dir}, {QStringLiteral("owner"), owner}},
                   QStringLiteral("↑ parent thread") + (info.value(QStringLiteral("parent_title")).toString().isEmpty() ? QString()
                       : QStringLiteral(": “") + esc(info.value(QStringLiteral("parent_title")).toString()) + QStringLiteral("”")));
    if (!up.isEmpty()) html += QStringLiteral("<p>") + up.join(QStringLiteral(" &nbsp; ")) + QStringLiteral("</p>");
    const QString agent = info.value(QStringLiteral("agent_id")).toString();
    html += QStringLiteral("<h2>Subagent %1 · %2</h2>").arg(esc(agent), esc(info.value(QStringLiteral("title")).toString()));
    html += QStringLiteral("<table>");
    const QString status = info.value(QStringLiteral("status")).toString();
    QString statusCell = statusMark(status) + QStringLiteral(" ") + esc(status);
    if (info.value(QStringLiteral("live")).toBool())
        statusCell += QStringLiteral(" · ") + link(QStringLiteral("live"), {{QStringLiteral("agent"), agent}, {QStringLiteral("thread"), info.value(QStringLiteral("thread_id")).toString()}},
                                                  QStringLiteral("open in the subagents pane"));
    html += row(QStringLiteral("Status"), statusCell);
    html += row(QStringLiteral("Type"), esc(info.value(QStringLiteral("type")).toString())
                + (info.value(QStringLiteral("background")).toBool() ? QStringLiteral(" <span class=m>· background</span>") : QString()));
    QStringList models;
    for (const auto &value : info.value(QStringLiteral("models")).toArray()) models << value.toString();
    html += row(QStringLiteral("Model"), esc(models.isEmpty() ? info.value(QStringLiteral("model")).toString() : models.join(QStringLiteral(", ")))
                + (info.value(QStringLiteral("effort")).toString().isEmpty() ? QString()
                   : QStringLiteral(" <span class=m>· effort %1</span>").arg(esc(info.value(QStringLiteral("effort")).toString()))));
    const QJsonObject usage = info.value(QStringLiteral("usage")).toObject();
    html += row(QStringLiteral("Tokens"), usageHtml(usage));
    html += row(QStringLiteral("Cost"), costHtml(usage));
    const QJsonValue spawn = info.value(QStringLiteral("spawn_turn"));
    html += row(QStringLiteral("Started"), esc(when(info.value(QStringLiteral("created")).toDouble(), now))
                + (spawn.isDouble() ? QStringLiteral(" <span class=m>· during turn %1 of its owner</span>").arg(spawn.toInt()) : QString()));
    html += row(QStringLiteral("Updated"), esc(when(info.value(QStringLiteral("updated")).toDouble(), now)));
    const int runs = info.value(QStringLiteral("runs")).toInt();
    if (runs > 1) html += row(QStringLiteral("Runs"), QStringLiteral("%1 <span class=m>(resumed by messages)</span>").arg(runs));
    const QString file = info.value(QStringLiteral("file")).toString();
    QString idCell = QStringLiteral("<code>%1</code>").arg(esc(info.value(QStringLiteral("thread_id")).toString()));
    if (!file.isEmpty()) idCell += QStringLiteral("<br>") + link(QStringLiteral("file"), {{QStringLiteral("path"), file}}, esc(file));
    html += row(QStringLiteral("Thread"), idCell);
    html += QStringLiteral("</table><h3>History</h3>");
    const QJsonArray history = info.value(QStringLiteral("history")).toArray();
    if (history.isEmpty()) html += QStringLiteral("<p><span class=m>Nothing recorded yet.</span></p>");
    for (const auto &value : history) {
        const QJsonObject item = value.toObject();
        const QString role = item.value(QStringLiteral("role")).toString();
        const QString text = item.value(QStringLiteral("text")).toString().trimmed();
        if (role == QLatin1String("threads")) {
            html += QStringLiteral("<p class=msg><span class=m>Threads it started</span></p>");
        } else {
            const QString who = role == QLatin1String("user") ? QStringLiteral("Task / message")
                              : role == QLatin1String("assistant") ? QStringLiteral("Subagent") : QStringLiteral("Tool output");
            QStringList calls;
            for (const auto &call : item.value(QStringLiteral("tool_calls")).toArray()) {
                const QJsonObject c = call.toObject();
                calls << QStringLiteral("<code>%1</code> <span class=m>%2</span>")
                             .arg(esc(c.value(QStringLiteral("name")).toString()), esc(c.value(QStringLiteral("arguments")).toString().left(120)));
            }
            if (text.isEmpty() && calls.isEmpty()) continue;
            html += QStringLiteral("<p class=msg><b>%1</b></p>").arg(who);
            if (!text.isEmpty()) {
                QString shown = text.left(role == QLatin1String("tool") ? 600 : 2400);
                if (shown.size() < text.size()) shown += QStringLiteral(" …");
                html += QStringLiteral("<pre>%1</pre>").arg(esc(shown));
            }
            for (const QString &c : calls) html += QStringLiteral("<p class=t>→ %1</p>").arg(c);
        }
        for (const auto &thread : item.value(QStringLiteral("threads")).toArray())
            html += threadLine(thread.toObject(), dir, 0);
    }
    return html;
}

}  // namespace

QHash<QString, QString> linkQuery(const QUrl &url) {
    QHash<QString, QString> out;
    const QString query = url.query(QUrl::FullyEncoded);
    for (const QString &part : query.split(QLatin1Char('&'), Qt::SkipEmptyParts)) {
        const int eq = part.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        out.insert(part.left(eq), QUrl::fromPercentEncoding(part.mid(eq + 1).toLatin1()));
    }
    return out;
}

QString compactNumber(qint64 value) {
    if (value >= 1000000) return QString::number(double(value) / 1e6, 'f', 1) + QLatin1Char('M');
    if (value >= 10000) return QString::number(double(value) / 1e3, 'f', 1) + QLatin1Char('k');
    return QString::number(value);
}

QString renderInfo(const QJsonObject &info, const QDateTime &now) {
    const QString body = info.value(QStringLiteral("kind")).toString() == QLatin1String("thread")
                             ? threadHtml(info, now) : sessionHtml(info, now);
    return style() + body;
}

// ----- the painted button ------------------------------------------------------------------------

InfoButton::InfoButton(QWidget *parent) : QToolButton(parent) {
    setAutoRaise(true);
    setFocusPolicy(Qt::NoFocus);
    setAccessibleName(QStringLiteral("Conversation info"));
}

QSize InfoButton::sizeHint() const {
    const int side = fontMetrics().height() + 6;
    return {side, side};
}

void InfoButton::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    QStyleOptionToolButton option;
    initStyleOption(&option);
    option.text.clear();
    option.icon = QIcon();
    style()->drawPrimitive(QStyle::PE_PanelButtonTool, &option, &painter, this);   // hover / pressed tile
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor ink = palette().color(underMouse() ? QPalette::BrightText : QPalette::ButtonText);
    const qreal side = std::min(width(), height()) - 8.0;
    const QRectF circle((width() - side) / 2.0, (height() - side) / 2.0, side, side);
    QPen pen(ink, std::max(1.2, side / 11.0));
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(circle);
    // The i: a dot and a stem, centred.
    const qreal cx = circle.center().x();
    const qreal dot = std::max(1.6, side / 8.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(ink);
    painter.drawEllipse(QPointF(cx, circle.top() + side * 0.29), dot / 2.0 + 0.3, dot / 2.0 + 0.3);
    const qreal stemWidth = std::max(1.6, side / 8.0);
    painter.drawRoundedRect(QRectF(cx - stemWidth / 2.0, circle.top() + side * 0.43, stemWidth, side * 0.36), 0.6, 0.6);
}

// ----- the view ----------------------------------------------------------------------------------

InfoView::InfoView(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("sessionInfo"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(6);
    auto *header = new QHBoxLayout;
    header->setSpacing(4);
    m_back = new QToolButton;
    m_back->setText(QStringLiteral("←"));
    m_back->setAutoRaise(true);
    m_back->setToolTip(QStringLiteral("Back (Alt+Left)"));
    m_back->setFocusPolicy(Qt::NoFocus);
    connect(m_back, &QToolButton::clicked, this, [this] { back(); });
    header->addWidget(m_back);
    m_title = new QLabel(QStringLiteral("Conversation info"));
    m_title->setObjectName(QStringLiteral("sessionInfoTitle"));
    QFont bold = m_title->font();
    bold.setBold(true);
    m_title->setFont(bold);
    m_title->setTextFormat(Qt::PlainText);
    header->addWidget(m_title, 1);
    m_refresh = new QToolButton;
    m_refresh->setText(QStringLiteral("↻"));
    m_refresh->setAutoRaise(true);
    m_refresh->setToolTip(QStringLiteral("Refresh (F5)"));
    m_refresh->setFocusPolicy(Qt::NoFocus);
    connect(m_refresh, &QToolButton::clicked, this, [this] { refresh(); });
    header->addWidget(m_refresh);
    m_inset = new QWidget;
    m_inset->setFixedWidth(0);
    header->addWidget(m_inset);
    layout->addLayout(header);
    m_body = new QTextBrowser;
    m_body->setObjectName(QStringLiteral("sessionInfoBody"));
    m_body->setOpenLinks(false);
    m_body->setOpenExternalLinks(false);
    m_body->installEventFilter(this);
    connect(m_body, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) { linkActivated(url); });
    layout->addWidget(m_body, 1);
    // Alt+I is the fast path since 2026-09-18; /status and /info still open it (docs/ARCHITECTURE.md).
    auto *hint = new QLabel(QStringLiteral("Links open subagent threads · Alt+Left back · F5 refresh · Esc closes · Alt+I opens this"));
    hint->setObjectName(QStringLiteral("dialogHint"));
    layout->addWidget(hint);
    updateHeader();
}

void InfoView::showLiveSession() { m_stack.clear(); navigate(QJsonObject(), true); }

void InfoView::showSession(const QString &sessionId, const QString &sessionDir) {
    navigate({{QStringLiteral("session_id"), sessionId}, {QStringLiteral("session_dir"), sessionDir}}, true);
}

void InfoView::showThread(const QString &threadId, const QString &sessionDir, const QString &ownerSession) {
    QJsonObject request{{QStringLiteral("thread_id"), threadId}, {QStringLiteral("session_dir"), sessionDir}};
    if (!ownerSession.isEmpty()) request.insert(QStringLiteral("owner_session"), ownerSession);
    navigate(request, true);
}

void InfoView::back() {
    if (m_stack.size() < 2) return;
    m_stack.removeLast();
    navigate(m_stack.last(), false);
}

void InfoView::refresh() {
    if (!m_stack.isEmpty()) navigate(m_stack.last(), false);
}

void InfoView::refreshIfLive() {
    if (!m_stack.isEmpty() && m_current.value(QStringLiteral("live")).toBool() && isVisible()) refresh();
}

void InfoView::navigate(const QJsonObject &request, bool push) {
    if (push) {
        // Opening what is already on screen is a refresh, not another step to go back through.
        if (m_stack.isEmpty() || m_stack.last() != request) m_stack.append(request);
    }
    m_pendingId = QStringLiteral("info-%1").arg(++m_counter);
    QJsonObject message = request;
    message.insert(QStringLiteral("id"), m_pendingId);
    if (m_current.isEmpty()) m_body->setHtml(QStringLiteral("<p>Loading…</p>"));
    updateHeader();
    if (onRequest) onRequest(message);
}

void InfoView::setInfo(const QJsonObject &event) {
    if (event.value(QStringLiteral("id")).toString() != m_pendingId) return;   // an older answer
    m_pendingId.clear();
    const int scroll = m_body->verticalScrollBar() ? m_body->verticalScrollBar()->value() : 0;
    const bool sameThing = m_current.value(QStringLiteral("session_id")) == event.value(QStringLiteral("session_id"))
                        && m_current.value(QStringLiteral("thread_id")) == event.value(QStringLiteral("thread_id"));
    m_current = event;
    m_body->setHtml(renderInfo(event, QDateTime::currentDateTime()));
    if (sameThing && m_body->verticalScrollBar()) m_body->verticalScrollBar()->setValue(scroll);
    updateHeader();
    if (onTitleChanged) onTitleChanged();
}

void InfoView::setError(const QString &requestId, const QString &text) {
    if (requestId != m_pendingId) return;
    m_pendingId.clear();
    m_current = QJsonObject();
    m_body->setHtml(QStringLiteral("<p>%1</p>").arg(esc(text)));
    updateHeader();
}

void InfoView::updateHeader() {
    m_back->setVisible(m_stack.size() > 1);
    QString title = paneTitle();
    m_title->setText(title);
}

QString InfoView::paneTitle() const {
    if (m_current.isEmpty()) return QStringLiteral("Conversation info");
    if (m_current.value(QStringLiteral("kind")).toString() == QLatin1String("thread"))
        return QStringLiteral("Thread %1 · %2").arg(m_current.value(QStringLiteral("agent_id")).toString(),
                                                     m_current.value(QStringLiteral("title")).toString());
    const QString title = m_current.value(QStringLiteral("title")).toString();
    return QStringLiteral("Info · ") + (title.isEmpty() ? QStringLiteral("Untitled session") : title);
}

void InfoView::focusView() { m_body->setFocus(Qt::OtherFocusReason); }

void InfoView::setHeaderRightInset(int pixels) { m_inset->setFixedWidth(std::max(0, pixels)); }

void InfoView::linkActivated(const QUrl &url) {
    if (url.scheme() != QLatin1String(kScheme)) return;
    const QHash<QString, QString> values = linkQuery(url);
    auto value = [&values](const char *key) { return values.value(QString::fromLatin1(key)); };
    const QString what = url.path();
    if (what == QLatin1String("thread")) showThread(value("id"), value("dir"), value("owner"));
    else if (what == QLatin1String("session")) showSession(value("id"), value("dir"));
    else if (what == QLatin1String("live")) { if (onOpenLive) onOpenLive(value("agent"), value("thread")); }
    else if (what == QLatin1String("file")) { if (onOpenFile) onOpenFile(value("path")); }
}

bool InfoView::eventFilter(QObject *object, QEvent *event) {
    if (object == m_body && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) { if (onClose) onClose(); return true; }
        if ((key->key() == Qt::Key_Left && key->modifiers() & Qt::AltModifier) || key->key() == Qt::Key_Backspace) {
            back();
            return true;
        }
        if (key->key() == Qt::Key_F5) { refresh(); return true; }
    }
    return QWidget::eventFilter(object, event);
}

}  // namespace relay::sessioninfo
