// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SessionInfo.h"
#include "CopyOnSelect.h"
#include "Keymap.h"
#include "ModelCatalog.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScrollBar>
#include <QShowEvent>
#include <QStyleOptionToolButton>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTimer>
#include <QToolTip>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace relay::sessioninfo {
namespace {

constexpr auto kScheme = "relay-info";

QString esc(const QString &text) { return text.toHtmlEscaped(); }

// The one name a model has (card #MDL1, rule 1). A saved conversation records the id the API took
// — "k3", "openai/gpt-6-sol", "MiniMax-M3" — and that stays on disk; this panel reads to a
// person, so it prints "kimi-k3", "gpt-6-sol", "minimax-m3". No preset is recorded per model
// here, so it is the derivation off the id, which is the same answer for every id a catalog row
// does not override.
QString modelName(const QString &modelId) { return relay::models::nameOf(modelId); }

// The worker's own name for the model this object is about, when it sent one (`model_name` /
// `models_named`, protocol 13): it knows the catalog row, so it can name a model the derivation
// alone cannot — the Kimi Coding Plan's "k3" is "kimi-k3". Without one, the derivation.
QString modelNameOf(const QJsonObject &object, const QString &field = QStringLiteral("model")) {
    const QString sent = object.value(field + QStringLiteral("_name")).toString();
    return sent.isEmpty() ? modelName(object.value(field).toString()) : sent;
}

// The provider beside the model, with the model taken out of it. The worker names a provider by
// its preset's label, and three of those labels carry the model — "z.ai · glm-5.3 · standard api
// (glm)" — so the row read "glm-5.3 · z.ai · glm-5.3 · standard api (glm)", naming one model twice
// (card #MDL1, rule 1: a model has one name, and the provider says which key is spending).
QString providerBeside(const QString &provider, const QString &name) {
    QStringList parts;
    for (const QString &part : provider.split(QStringLiteral(" · "), Qt::SkipEmptyParts))
        if (name.isEmpty() || modelName(part.section(QLatin1Char(' '), 0, 0)) != name) parts << part;
    return parts.isEmpty() ? provider : parts.join(QStringLiteral(" · "));
}

// "Models used": the names, each once and in the order they were first used, so one model that
// two providers served does not read as two. The worker's `models_named` is already exactly that;
// `models` is the older field, and its ids are named here.
QStringList modelNames(const QJsonObject &info) {
    QStringList out;
    const QJsonArray named = info.value(QStringLiteral("models_named")).toArray();
    const QJsonArray ids = named.isEmpty() ? info.value(QStringLiteral("models")).toArray() : named;
    for (const auto &value : ids) {
        const QString name = named.isEmpty() ? modelName(value.toString()) : value.toString();
        if (!name.isEmpty() && !out.contains(name)) out << name;
    }
    return out;
}

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

// The standing line under the body; InfoView::flashHint() borrows it for a moment.
QString standingHint() {
    return QStringLiteral("Links open subagent threads · Alt+Left back · F5 refresh · Esc closes · Alt+I opens this");
}

// A copyable id (#YQC3): the id itself and the ⧉ icon (U+29C9, two joined squares — the standard
// copy sign) share one relay-info:copy href, so a click on either puts the whole id on the
// clipboard. An empty id renders no href and no icon. The icon is muted and a notch smaller, so
// it reads as a button rather than text.
QString copyableId(const QString &id, const QString &what) {
    if (id.isEmpty()) return QString();
    const QList<QPair<QString, QString>> query{{QStringLiteral("text"), id}, {QStringLiteral("what"), what}};
    const int body = QApplication::font().pointSize();   // -1 when the font is pixel-based
    const int small = body > 3 ? qMax(body - 2, 7) : 9;
    const QString icon = QStringLiteral("<span class=m style=\"font-size:%1pt\">⧉</span>").arg(small);
    return link(QStringLiteral("copy"), query, QStringLiteral("<code>%1</code>").arg(esc(id))) + QStringLiteral(" ")
         + link(QStringLiteral("copy"), query, icon);
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
    // What the provider's prefix cache served, beside the input it is part of (#GMCF decision 5).
    // The key is absent when the provider says nothing about caching, and nothing is drawn then:
    // "(0 cached)" would claim the cache missed, which is a different thing (protocol § 4).
    QString cached;
    if (usage.contains(QStringLiteral("cached_tokens")))
        cached = QStringLiteral(" <span class=m>(%1 cached)</span>")
                     .arg(compactNumber(usage.value(QStringLiteral("cached_tokens")).toVariant().toLongLong()));
    return QStringLiteral("%1 in%2 · %3 out · %4 total <span class=m>· %5 request%6</span>")
        .arg(compactNumber(in), cached, compactNumber(out), compactNumber(total))
        .arg(requests).arg(requests == 1 ? QString() : QStringLiteral("s"));
}

QString costHtml(const QJsonObject &usage) {
    QStringList figures;
    if (usage.contains(QStringLiteral("cost")))
        figures << QStringLiteral("$%1 reported").arg(usage.value(QStringLiteral("cost")).toDouble(), 0, 'f', 4);
    if (usage.contains(QStringLiteral("cost_estimate")))
        figures << QStringLiteral("$%1 <span class=m>(OpenRouter list-price estimate)</span>")
                       .arg(usage.value(QStringLiteral("cost_estimate")).toDouble(), 0, 'f', 4);
    if (!figures.isEmpty()) return figures.join(QStringLiteral(" · "));
    return QStringLiteral("<span class=m>not reported by this provider</span>");
}

QString turnUsageHtml(const QJsonArray &turns) {
    if (turns.isEmpty()) return QStringLiteral("<p class=m>No per-turn usage recorded yet.</p>");
    QString html = QStringLiteral("<table class=usage><tr><th align=left>Turn</th><th align=left>Model</th>"
                                  "<th align=right>Input</th><th align=right>Cached</th>"
                                  "<th align=right>Output</th><th align=right>Last prompt</th>"
                                  "<th align=right>Requests</th><th align=right>Cost</th></tr>");
    for (const auto &value : turns) {
        const QJsonObject turn = value.toObject();
        const auto number = [&turn](const char *key) {
            const QString name = QString::fromLatin1(key);
            return turn.contains(name) ? compactNumber(turn.value(name).toVariant().toLongLong()) : QStringLiteral("—");
        };
        const QString label = turn.contains(QStringLiteral("turn"))
            ? QString::number(turn.value(QStringLiteral("turn")).toInt()) : QStringLiteral("—");
        QString source = turn.value(QStringLiteral("source")).toString();
        if (source == QLatin1String("guest")) source = QStringLiteral(" · guest reported");
        else source.clear();
        QString detail;
        if (turn.contains(QStringLiteral("handover_tokens")))
            detail += QStringLiteral(" · handover ~%1 tokens").arg(number("handover_tokens"));
        if (turn.contains(QStringLiteral("prefix_changes")))
            detail += QStringLiteral(" · %1 prefix change(s)").arg(number("prefix_changes"));
        QString cost = costHtml(turn);
        html += QStringLiteral("<tr><td>%1</td><td>%2</td>"
                               "<td align=right>%3</td><td align=right>%4</td>"
                               "<td align=right>%5</td><td align=right>%6</td>"
                               "<td align=right>%7</td><td align=right>%8</td></tr>")
                    .arg(label, esc(modelName(turn.value(QStringLiteral("model")).toString())),
                         number("prompt_tokens"), number("cached_tokens"),
                         number("completion_tokens"), number("last_prompt_tokens"),
                         number("requests"), cost);
        if (!source.isEmpty() || !detail.isEmpty())
            html += QStringLiteral("<tr><td></td><td colspan=7 class=m>%1%2</td></tr>").arg(esc(source.mid(3)), esc(detail));
    }
    return html + QStringLiteral("</table><p class=m>Latest %1 recorded turns; older turns remain in totals.</p>").arg(turns.size());
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
                 : QStringLiteral(" · ") + esc(modelNameOf(thread)));
    if (thread.value(QStringLiteral("live")).toBool())
        html += QStringLiteral(" · ") + link(QStringLiteral("live"), {{QStringLiteral("agent"), agent}, {QStringLiteral("thread"), id}},
                                          QStringLiteral("open in the subagents pane"));
    html += QStringLiteral("</p>");
    for (const auto &child : thread.value(QStringLiteral("children")).toArray())
        html += threadLine(child.toObject(), dir, depth + 1);
    return html;
}

// The cells both the info pane and the overlay show (card #7EWF), so the two say the same thing.
QString modelCell(const QJsonObject &info) {
    const QString named = modelNameOf(info);
    QString model = esc(named);
    const QString provider = providerBeside(info.value(QStringLiteral("provider")).toString(), named);
    if (!provider.isEmpty()) model += QStringLiteral(" <span class=m>· %1</span>").arg(esc(provider));
    const QString effort = info.value(QStringLiteral("effort")).toString();
    const QString mode = info.value(QStringLiteral("mode")).toString();
    if (!effort.isEmpty()) model += QStringLiteral(" <span class=m>· effort %1</span>").arg(esc(effort));
    if (mode == QLatin1String("plan")) model += QStringLiteral(" <span class=m>· plan mode</span>");
    // Which prompt profile the pane is sending (#GMCF decision 7): only worth a word when it is
    // the short one, which is a different agent — 16 rules and 8 tools, no Switchboard, no todos.
    if (info.value(QStringLiteral("prompt_profile")).toString() == QLatin1String("short"))
        model += QStringLiteral(" <span class=m>· short prompt</span>");
    return model;
}

// Empty when the worker reported no context figures.
QString contextCell(const QJsonObject &info) {
    const QJsonObject context = info.value(QStringLiteral("context")).toObject();
    if (context.isEmpty()) return QString();
    return QStringLiteral("%1 / %2 tokens <span class=m>· %3%%4</span>")
        .arg(compactNumber(context.value(QStringLiteral("used_tokens")).toVariant().toLongLong()),
             compactNumber(context.value(QStringLiteral("window")).toVariant().toLongLong()))
        .arg(context.value(QStringLiteral("percent")).toDouble(), 0, 'f', 1)
        .arg(context.value(QStringLiteral("estimated")).toBool() ? QStringLiteral(" · estimated") : QString());
}

QString turnsCell(const QJsonObject &info) {
    const int threads = info.value(QStringLiteral("thread_count")).toInt();
    return QStringLiteral("%1%2").arg(info.value(QStringLiteral("turns")).toInt())
        .arg(threads > 0 ? QStringLiteral(" <span class=m>· %1 subagent thread%2</span>").arg(threads)
                               .arg(threads == 1 ? QString() : QStringLiteral("s")) : QString());
}

QString instructionsCell(const QJsonObject &info) {
    QStringList instructions;
    for (const auto &value : info.value(QStringLiteral("instructions")).toArray()) {
        const QString path = value.toString();
        instructions << link(QStringLiteral("file"), {{QStringLiteral("path"), path}}, esc(QFileInfo(path).fileName().isEmpty() ? path : QFileInfo(path).fileName()));
    }
    return instructions.isEmpty() ? QStringLiteral("<span class=m>none loaded</span>") : instructions.join(QStringLiteral(", "));
}

QString style() {
    const QPalette palette = QApplication::palette();
    const QString muted = palette.color(QPalette::PlaceholderText).name();
    const QString linkColor = palette.color(QPalette::Link).name();
    return QStringLiteral(
        "<style>td.k{color:%1;padding-right:14px;white-space:nowrap;vertical-align:top}"
        "span.m{color:%1} a{color:%2;text-decoration:none} p.t{margin-top:2px;margin-bottom:2px}"
        "p.turn{margin-top:10px;margin-bottom:2px} h2{margin-bottom:4px} h3{margin-top:16px;margin-bottom:4px}"
        "p.msg{margin-top:6px;margin-bottom:2px} pre{margin-top:0;white-space:pre-wrap}"
        "table.usage{border-spacing:0 2px} table.usage th,table.usage td{padding-right:12px;vertical-align:top}"
        "table.usage th:last-child,table.usage td:last-child{padding-right:0}</style>")
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
    html += row(QStringLiteral("Model"), modelCell(info));
    const QStringList models = modelNames(info);
    if (models.size() > 1) html += row(QStringLiteral("Models used"), esc(models.join(QStringLiteral(", "))));
    if (const QString context = contextCell(info); !context.isEmpty()) html += row(QStringLiteral("Context"), context);
    const QJsonObject usage = info.value(QStringLiteral("usage")).toObject();
    html += row(QStringLiteral("Tokens"), usageHtml(usage));
    html += row(QStringLiteral("Cost"), costHtml(usage));
    const int children = info.value(QStringLiteral("children_count")).toInt();
    if (children > 0) {
        const QJsonObject childUsage = info.value(QStringLiteral("children_usage")).toObject();
        const QJsonObject taskUsage = info.value(QStringLiteral("task_usage")).toObject();
        html += row(QStringLiteral("Subagents"), QStringLiteral("%1 · %2").arg(children).arg(usageHtml(childUsage)));
        html += row(QStringLiteral("Task total"), usageHtml(taskUsage));
        html += row(QStringLiteral("Task cost"), costHtml(taskUsage));
    }
    const QString sessionId = info.value(QStringLiteral("session_id")).toString();
    const QString file = info.value(QStringLiteral("file")).toString();
    QString sessionCell = copyableId(sessionId, QStringLiteral("session id"));
    if (!file.isEmpty())
        sessionCell += QStringLiteral("<br>") + (info.value(QStringLiteral("file_exists")).toBool(true)
            ? link(QStringLiteral("file"), {{QStringLiteral("path"), file}}, esc(file))
            : esc(file) + QStringLiteral(" <span class=m>(not saved yet: nothing is written before the first turn)</span>"));
    html += row(QStringLiteral("Session"), sessionCell);
    QString workspace = esc(info.value(QStringLiteral("workspace")).toString());
    const QString branch = info.value(QStringLiteral("git_branch")).toString();
    if (!branch.isEmpty()) workspace += QStringLiteral(" <span class=m>· branch %1</span>").arg(esc(branch));
    html += row(QStringLiteral("Workspace"), workspace);
    // Card #FYEY: which backend this pane's worker runs from — the pane stamps the id in
    // (PaneSession.cpp); the worker cannot see the checkout moving under its pin. "live" means
    // running unpinned from the checkout; "changed" means the checkout has moved on and only a
    // worker restart (the pane's Restart agent banner) loads the new tree.
    const QString backendRev = info.value(QStringLiteral("backend_rev")).toString();
    if (!backendRev.isEmpty()) {
        const QString cell = backendRev == QLatin1String("live")
                                 ? QStringLiteral("live <span class=m>· unpinned, from the checkout</span>")
                                 : QStringLiteral("<code>%1</code>").arg(esc(backendRev))
                                       + (info.value(QStringLiteral("backend_changed")).toBool()
                                              ? QStringLiteral(" <span class=m>· backend changed; Restart agent to load it</span>")
                                              : QString());
        html += row(QStringLiteral("Backend"), cell);
    }
    html += row(QStringLiteral("Started"), esc(when(info.value(QStringLiteral("created")).toDouble(), now)));
    html += row(QStringLiteral("Updated"), esc(when(info.value(QStringLiteral("updated")).toDouble(), now)));
    html += row(QStringLiteral("Turns"), turnsCell(info));
    html += row(QStringLiteral("Instructions"), instructionsCell(info));
    const QString forked = info.value(QStringLiteral("forked_from")).toString();
    if (!forked.isEmpty())
        html += row(QStringLiteral("Forked from"), link(QStringLiteral("session"), {{QStringLiteral("id"), forked}, {QStringLiteral("dir"), dir}},
                                                         QStringLiteral("<code>%1</code>").arg(esc(forked.left(8)))));
    html += QStringLiteral("</table>");

    const QJsonArray turnsUsage = info.value(QStringLiteral("turns_usage")).toArray();
    if (!turnsUsage.isEmpty()) html += QStringLiteral("<h3>Usage by turn</h3>") + turnUsageHtml(turnsUsage);

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
    const QStringList models = modelNames(info);
    html += row(QStringLiteral("Model"), esc(models.isEmpty() ? modelNameOf(info) : models.join(QStringLiteral(", ")))
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
    QString idCell = copyableId(info.value(QStringLiteral("thread_id")).toString(), QStringLiteral("thread id"));
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

QString renderSummary(const QJsonObject &info, const QString &paneId, const QDateTime &now) {
    QString title = info.value(QStringLiteral("title")).toString();
    if (title.isEmpty()) title = QStringLiteral("Untitled session");
    QString html = QStringLiteral("<p><b>%1</b></p><table>").arg(esc(title));
    html += row(QStringLiteral("Model"), modelCell(info));
    if (const QString context = contextCell(info); !context.isEmpty()) html += row(QStringLiteral("Context"), context);
    html += row(QStringLiteral("Tokens"), usageHtml(info.value(QStringLiteral("usage")).toObject()));
    // The session ID is the one Board links and relay://session/ name; the pane's short ID is
    // what logs and land.py claims say ("pane a1b2c3d4"), kept small beside it so the two can be
    // matched (owner, 2026-09-25).
    QString session = copyableId(info.value(QStringLiteral("session_id")).toString(), QStringLiteral("session id"));
    if (session.isEmpty()) session = QStringLiteral("<span class=m>not saved yet</span>");
    if (!paneId.isEmpty()) session += QStringLiteral("<br><span class=m>pane %1</span>").arg(esc(paneId));
    html += row(QStringLiteral("Session"), session);
    html += row(QStringLiteral("Started"), esc(when(info.value(QStringLiteral("created")).toDouble(), now)));
    html += row(QStringLiteral("Turns"), turnsCell(info));
    html += row(QStringLiteral("Instructions"), instructionsCell(info));
    return style() + html + QStringLiteral("</table>");
}

// ----- the painted button ------------------------------------------------------------------------

InfoButton::InfoButton(QWidget *parent) : QToolButton(parent) {
    setAutoRaise(true);
    setFocusPolicy(Qt::TabFocus);
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

InfoOverlay::InfoOverlay(QWidget *pane, QWidget *anchor, const QString &paneId)
    : QFrame(pane), m_anchor(anchor), m_paneId(paneId) {
    setObjectName(QStringLiteral("paneInfoOverlay"));
    setAttribute(Qt::WA_StyledBackground);
    // Opaque whatever the stylesheet says: it sits over the terminal's text.
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Window);
    setFrameShape(QFrame::StyledPanel);
    setFocusPolicy(Qt::StrongFocus);
    hide();
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 10);
    m_body = new QLabel;
    m_body->setObjectName(QStringLiteral("paneInfoBody"));
    m_body->setTextFormat(Qt::RichText);
    m_body->setWordWrap(true);
    m_body->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_body->setOpenExternalLinks(false);
    m_body->setMaximumWidth(440);
    connect(m_body, &QLabel::linkActivated, this, [this](const QString &href) { linkActivated(href); });
    layout->addWidget(m_body);
    if (pane) pane->installEventFilter(this);
}

void InfoOverlay::open() {
    if (m_current.isEmpty()) m_body->setText(QStringLiteral("<p>Loading…</p>"));
    place();
    show();
    raise();
    setFocus(Qt::PopupFocusReason);
    // A click anywhere outside closes it, the way a popup would, without it being one.
    qApp->installEventFilter(this);
    request();
}

void InfoOverlay::close() {
    if (!isVisible()) return;
    qApp->removeEventFilter(this);
    m_pendingId.clear();
    hide();
    if (onClosed) onClosed();
}

void InfoOverlay::refreshIfOpen() {
    if (isVisible()) request();
}

void InfoOverlay::request() {
    m_pendingId = QStringLiteral("info-overlay-%1").arg(++m_counter);
    if (onRequest) onRequest({{QStringLiteral("id"), m_pendingId}});
}

void InfoOverlay::setInfo(const QJsonObject &event) {
    if (!owns(event.value(QStringLiteral("id")).toString())) return;
    m_pendingId.clear();
    m_current = event;
    m_body->setText(renderSummary(event, m_paneId, QDateTime::currentDateTime()));
    if (isVisible()) place();
}

void InfoOverlay::setError(const QString &requestId, const QString &text) {
    if (!owns(requestId)) return;
    m_pendingId.clear();
    m_current = QJsonObject();
    m_body->setText(QStringLiteral("<p>%1</p>").arg(esc(text)));
    if (isVisible()) place();
}

QString InfoOverlay::html() const { return m_body->text(); }

void InfoOverlay::place() {
    QWidget *pane = parentWidget();
    if (!pane) return;
    adjustSize();
    const int margin = 6;
    const int w = std::min(width(), std::max(120, pane->width() - 2 * margin));
    resize(w, heightForWidth(w) > 0 ? heightForWidth(w) : height());
    QPoint at(pane->width() - width() - margin, margin + 28);
    if (m_anchor && m_anchor->isVisible()) {
        const QPoint below = m_anchor->mapTo(pane, QPoint(m_anchor->width(), m_anchor->height() + 4));
        at = {below.x() - width(), below.y()};
    }
    at.setX(std::clamp(at.x(), margin, std::max(margin, pane->width() - width() - margin)));
    at.setY(std::clamp(at.y(), margin, std::max(margin, pane->height() - height() - margin)));
    move(at);
}

void InfoOverlay::linkActivated(const QString &href) {
    const QUrl url(href);
    if (url.scheme() != QLatin1String(kScheme)) return;
    const QHash<QString, QString> values = linkQuery(url);
    if (url.path() == QLatin1String("file")) {
        if (onOpenFile) onOpenFile(values.value(QStringLiteral("path")));
        close();
    } else if (url.path() == QLatin1String("copy")) {
        QClipboard *clipboard = QApplication::clipboard();
        if (clipboard->supportsSelection()) clipboard->setText(values.value(QStringLiteral("text")), QClipboard::Selection);
        clipboard->setText(values.value(QStringLiteral("text")));
        QToolTip::showText(QCursor::pos(), QStringLiteral("Copied %1").arg(values.value(QStringLiteral("what"), QStringLiteral("to the clipboard"))), this);
    }
}

void InfoOverlay::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) { close(); return; }
    QFrame::keyPressEvent(event);
}

bool InfoOverlay::eventFilter(QObject *object, QEvent *event) {
    if (object == parentWidget() && event->type() == QEvent::Resize && isVisible()) place();
    if (isVisible() && event->type() == QEvent::MouseButtonPress) {
        auto *widget = qobject_cast<QWidget *>(object);
        // The ⓘ itself is left alone: its click toggles, and closing here first would reopen it.
        const bool inside = widget && (widget == this || isAncestorOf(widget)
                                       || (m_anchor && (widget == m_anchor || m_anchor->isAncestorOf(widget))));
        if (widget && !inside) close();
    }
    if (isVisible() && event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape
        && object != this) {
        // Esc closes it even after focus went back into the pane (a click on its body).
        auto *widget = qobject_cast<QWidget *>(object);
        if (widget && parentWidget() && (widget == parentWidget() || parentWidget()->isAncestorOf(widget))) {
            close();
            return true;
        }
    }
    return QFrame::eventFilter(object, event);
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
    m_body->viewport()->installEventFilter(this);   // mouse events go to the viewport, not m_body
    relay::installCopyOnSelect(m_body);
    connect(m_body, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) { linkActivated(url); });
    layout->addWidget(m_body, 1);
    // The Ask row (#FEJQ). It sits between the page and the standing key line, so the last thing
    // under the figures is the offer to ask about them. The draft is handed up to the window,
    // which puts it in the owning pane's composer; this view sends nothing itself.
    m_ask = new relay::askrow::AskRow(QStringLiteral("Ask the agent about this session"),
                                      QStringLiteral("drafts a question in the terminal's prompt box · nothing is sent"));
    m_ask->onAsk = [this](const QString &text) { if (onAskOwner) onAskOwner(text); };
    m_ask->hide();   // until the window has wired onAskOwner; updateAskRow() decides from then on
    layout->addWidget(m_ask);
    // Alt+I is the fast path since 2026-09-18; /status and /info still open it (docs/ARCHITECTURE.md).
    m_hint = new QLabel(standingHint());
    m_hint->setObjectName(QStringLiteral("dialogHint"));
    layout->addWidget(m_hint);
    // A copy (#YQC3) borrows the line for two seconds; restarting the timer keeps a second copy's
    // full two seconds even when it lands inside the first one's.
    m_hintTimer = new QTimer(this);
    m_hintTimer->setSingleShot(true);
    connect(m_hintTimer, &QTimer::timeout, this, [this] { m_hint->setText(standingHint()); });
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
    updateAskRow();
}

// The Ask row (#FEJQ). `onAskOwner` is a plain callback the window assigns, like onClose and
// onOpenFile, so there is no setter to notice it arriving: the row is decided again wherever what
// it would say can have moved — every navigation and every answer go through updateHeader(), and
// a view wired after it was built catches up when the pane shows it.
//
// The chips carry the figures the page is showing, because the question and the screen have to
// agree; and they are only live on the pane's own session, because the pane's agent is the one
// that will answer and it can answer only for itself.
void InfoView::updateAskRow() {
    if (!m_ask) return;
    m_ask->setVisible(bool(onAskOwner));
    if (!onAskOwner) return;

    QVector<relay::askrow::Question> questions;
    const QJsonObject context = m_current.value(QStringLiteral("context")).toObject();
    if (context.contains(QStringLiteral("percent"))) {
        const double percent = context.value(QStringLiteral("percent")).toDouble();
        const qint64 used = context.value(QStringLiteral("used_tokens")).toVariant().toLongLong();
        const qint64 window = context.value(QStringLiteral("window")).toVariant().toLongLong();
        const QString usedOfWindow = window > 0 ? QStringLiteral("%1 / %2").arg(compactNumber(used), compactNumber(window))
                                                : QString();
        questions.append({QStringLiteral("Context · %1%").arg(percent, 0, 'f', 1),
                          relay::askrow::contextQuestion(percent, usedOfWindow)});
    }
    questions.append({QStringLiteral("What it has done"), relay::askrow::summaryQuestion()});
    questions.append({QStringLiteral("Costliest turn"), relay::askrow::costliestTurnQuestion()});
    m_ask->setQuestions(questions);

    const bool thread = m_current.value(QStringLiteral("kind")).toString() == QLatin1String("thread");
    if (m_current.isEmpty())
        m_ask->setAvailable(false, QStringLiteral("There is nothing to ask about yet: this page's "
                                                  "figures have not arrived."));
    else if (thread)
        m_ask->setAvailable(false, QStringLiteral("This is a subagent thread, not the session in the "
                                                  "pane this page belongs to, and that pane's agent "
                                                  "can answer only for itself. Follow “↑ owner "
                                                  "session” to ask about the session that started it."));
    else if (!m_current.value(QStringLiteral("live")).toBool())
        m_ask->setAvailable(false, QStringLiteral("This is a saved session, not the one running in "
                                                  "the pane this page belongs to, and that pane's "
                                                  "agent can answer only for itself."));
    else
        m_ask->setAvailable(true);
}

void InfoView::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    updateAskRow();   // the window wires onAskOwner after the view is built
}

void InfoView::flashHint(const QString &message) {
    m_hint->setText(message);
    m_hintTimer->start(2000);
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
    else if (what == QLatin1String("copy")) {
        // PRIMARY as well as the clipboard, the same deal copy-on-select makes: a shaky click's
        // selection copy may have put the ⧉ glyph there first (see eventFilter).
        QClipboard *clipboard = QApplication::clipboard();
        if (clipboard->supportsSelection()) clipboard->setText(value("text"), QClipboard::Selection);
        clipboard->setText(value("text"));
        flashHint(value("what").isEmpty() ? QStringLiteral("Copied to the clipboard")
                                          : QStringLiteral("Copied %1 to the clipboard").arg(value("what")));
    }
}

bool InfoView::eventFilter(QObject *object, QEvent *event) {
    if (object == m_body->viewport() && event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        m_pressAnchor = mouse->button() == Qt::LeftButton ? m_body->anchorAt(mouse->pos())
                                                          : QString();
    } else if (object == m_body->viewport() && event->type() == QEvent::MouseButtonRelease) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        const QString pressed = m_pressAnchor;
        m_pressAnchor.clear();
        // A shaky click on a copy link: past the drag threshold QTextBrowser abandons the anchor
        // and selects instead, so anchorClicked never fires and copy-on-select puts the selected
        // glyph — the ⧉ button itself — on the clipboard. Rescue copy links only (a drag across
        // one wants its text on the clipboard anyway), and only when a selection says Qt did not
        // activate the link itself. Scheduled after the release like copy-on-select's own
        // singleShot: the selection is cleared first, so whichever runs second leaves the
        // clipboard holding the id, not the glyph.
        if (mouse->button() == Qt::LeftButton && !pressed.isEmpty()
            && m_body->anchorAt(mouse->pos()) == pressed) {
            const QUrl url(pressed);
            if (url.scheme() == QLatin1String(kScheme) && url.path() == QLatin1String("copy")) {
                QPointer<InfoView> self(this);
                QTimer::singleShot(0, m_body, [self, url] {
                    if (!self || !self->m_body->textCursor().hasSelection()) return;
                    QTextCursor cursor = self->m_body->textCursor();
                    cursor.clearSelection();
                    self->m_body->setTextCursor(cursor);
                    self->linkActivated(url);
                });
            }
        }
    }
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
