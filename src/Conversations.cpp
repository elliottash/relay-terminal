// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Conversations.h"
#include "CopyOnSelect.h"
#include "HelperChat.h"

#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QApplication>
#include <algorithm>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace relay::conversations {
namespace {
constexpr int kIdRole = Qt::UserRole + 1;
constexpr int kItemRole = Qt::UserRole + 2;
// What a row draws (see RowDelegate): a title with short tags and a muted line under it, or rich
// text that wraps. kKindRole says what the row is: "session", "thread", "group" or "preview".
constexpr int kTitleRole = Qt::UserRole + 3;
constexpr int kSubRole = Qt::UserRole + 4;
constexpr int kBadgeRole = Qt::UserRole + 5;
constexpr int kHtmlRole = Qt::UserRole + 6;
constexpr int kKindRole = Qt::UserRole + 7;
constexpr int kLoadedRole = Qt::UserRole + 8;
}  // namespace

// ----- pure helpers ------------------------------------------------------------------------

QString kindLabel(const QString &kind) {
    if (kind == QLatin1String("prompt")) return QStringLiteral("You");
    if (kind == QLatin1String("reply")) return QStringLiteral("Agent");
    if (kind == QLatin1String("tool_call")) return QStringLiteral("Tool");
    if (kind == QLatin1String("tool_output")) return QStringLiteral("Tool output");
    if (kind == QLatin1String("command")) return QStringLiteral("Command");
    if (kind == QLatin1String("command_output")) return QStringLiteral("Command output");
    return kind;
}

bool isTerminalKind(const QString &kind) {
    return kind == QLatin1String("command") || kind == QLatin1String("command_output");
}

QString highlighted(const QString &line, const QJsonArray &ranges) {
    QString out;
    int cursor = 0;
    for (const auto &value : ranges) {
        const QJsonArray pair = value.toArray();
        if (pair.size() != 2) continue;
        const int start = pair.at(0).toInt(-1);
        const int length = pair.at(1).toInt(0);
        if (start < cursor || length <= 0 || start >= line.size() || start + length > line.size()) continue;
        out += line.mid(cursor, start - cursor).toHtmlEscaped();
        // Qt's rich text has no <mark>, so the highlight is an inline background span.
        out += QStringLiteral("<span style=\"background-color:#f5d76e;color:#101216;\">")
             + line.mid(start, length).toHtmlEscaped() + QStringLiteral("</span>");
        cursor = start + length;
    }
    out += line.mid(cursor).toHtmlEscaped();
    return out;
}

QString whenText(double epochSeconds, const QDateTime &now) {
    if (epochSeconds <= 0) return QStringLiteral("—");
    const QDateTime when = QDateTime::fromSecsSinceEpoch(qint64(epochSeconds));
    const qint64 seconds = when.secsTo(now);
    if (seconds < 60 && seconds > -60) return QStringLiteral("just now");
    if (seconds < 3600 && seconds > 0) return QStringLiteral("%1 min ago").arg(seconds / 60);
    if (when.date() == now.date()) return QStringLiteral("%1 h ago").arg(seconds / 3600);
    if (when.date().addDays(1) == now.date())
        return QStringLiteral("yesterday ") + when.toString(QStringLiteral("HH:mm"));
    if (when.date().year() == now.date().year()) return when.toString(QStringLiteral("d MMM"));
    return when.toString(QStringLiteral("d MMM yyyy"));
}

double sinceFor(const QString &id, const QDateTime &now) {
    if (id == QLatin1String("today")) return double(QDateTime(now.date(), QTime(0, 0)).toSecsSinceEpoch());
    if (id == QLatin1String("week")) return double(now.addDays(-7).toSecsSinceEpoch());
    if (id == QLatin1String("month")) return double(now.addDays(-30).toSecsSinceEpoch());
    return 0;
}

QString stripAnsi(const QByteArray &bytes, int maxChars) {
    const QString text = QString::fromUtf8(bytes);
    QString out;
    out.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch == QChar(0x1b)) {
            if (i + 1 >= text.size()) break;
            const QChar next = text.at(i + 1);
            if (next == QLatin1Char('[')) {               // CSI: parameters, then a final byte
                i += 2;
                while (i < text.size() && (text.at(i) < QChar(0x40) || text.at(i) > QChar(0x7e))) ++i;
            } else if (next == QLatin1Char(']')) {        // OSC: up to BEL or ST
                i += 2;
                while (i < text.size() && text.at(i) != QChar(0x07)
                       && !(text.at(i) == QChar(0x1b) && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\\')))
                    ++i;
                if (i < text.size() && text.at(i) == QChar(0x1b)) ++i;
            } else if (next == QLatin1Char('P') || next == QLatin1Char('X') || next == QLatin1Char('^')
                       || next == QLatin1Char('_')) {     // DCS/SOS/PM/APC: up to ST
                i += 2;
                while (i + 1 < text.size() && !(text.at(i) == QChar(0x1b) && text.at(i + 1) == QLatin1Char('\\'))) ++i;
                ++i;
            } else {
                ++i;                                       // two-character escape
            }
            continue;
        }
        if (ch == QLatin1Char('\r')) continue;
        if (ch == QLatin1Char('\n') || ch == QLatin1Char('\t')) { out += ch; continue; }
        if (ch < QChar(0x20) || ch == QChar(0x7f)) continue;
        out += ch;
    }
    while (out.endsWith(QLatin1Char('\n')) || out.endsWith(QLatin1Char(' '))) out.chop(1);
    if (maxChars > 0 && out.size() > maxChars) out = out.left(maxChars);
    return out;
}

QString dateGroup(double epochSeconds, const QDateTime &now) {
    if (epochSeconds <= 0) return QStringLiteral("Older");
    const QDate when = QDateTime::fromSecsSinceEpoch(qint64(epochSeconds)).date();
    const QDate today = now.date();
    if (when >= today) return QStringLiteral("Today");            // a clock skew reads as today
    if (when == today.addDays(-1)) return QStringLiteral("Yesterday");
    if (when >= today.addDays(-6)) return QStringLiteral("This week");
    if (when >= today.addDays(-29)) return QStringLiteral("This month");
    return QStringLiteral("Older");
}

QStringList dateGroupOrder() {
    return {QStringLiteral("Today"), QStringLiteral("Yesterday"), QStringLiteral("This week"),
            QStringLiteral("This month"), QStringLiteral("Older")};
}

QString nextHeaderSort(int column, const QString &current) {
    switch (column) {
        case 0:  return current == QLatin1String("title") ? QStringLiteral("title_desc")
                                                          : QStringLiteral("title");
        case 1:  return current == QLatin1String("recent") ? QStringLiteral("oldest")
                                                           : QStringLiteral("recent");
        case 2:  return current == QLatin1String("longest") ? QStringLiteral("shortest")
                                                            : QStringLiteral("longest");
        case 3:  return current == QLatin1String("model") ? QStringLiteral("model_desc")
                                                          : QStringLiteral("model");
        default: return current;
    }
}

int headerSortColumn(const QString &sort) {
    if (sort == QLatin1String("recent") || sort == QLatin1String("oldest")) return 1;
    if (sort == QLatin1String("longest") || sort == QLatin1String("shortest")) return 2;
    if (sort == QLatin1String("title") || sort == QLatin1String("title_desc")) return 0;
    if (sort == QLatin1String("model") || sort == QLatin1String("model_desc")) return 3;
    return -1;
}

Qt::SortOrder headerSortOrder(const QString &sort) {
    return sort == QLatin1String("oldest") || sort == QLatin1String("shortest")
                   || sort == QLatin1String("title") || sort == QLatin1String("model")
               ? Qt::AscendingOrder
               : Qt::DescendingOrder;
}

namespace {

const char *const kOperatorKeys[] = {"project", "file", "model", "branch", "before", "after", "has", "is", "in"};

bool isOperatorKey(const QString &key) {
    for (const char *known : kOperatorKeys) if (key == QLatin1String(known)) return true;
    return false;
}

// One token of a query, the way conv_index._scan_tokens splits it: a leading `-` negates, `key:`
// counts only as a bare ASCII word before the colon, and a value may be quoted.
struct Token {
    bool negated = false, hasKey = false;
    QString key, value;
    int start = 0, length = 0;
};

QList<Token> scanTokens(const QString &text) {
    QList<Token> out;
    const int size = text.size();
    int index = 0;
    while (index < size) {
        if (text.at(index).isSpace()) { ++index; continue; }
        Token token;
        token.start = index;
        if (text.at(index) == QLatin1Char('-') && index + 1 < size && !text.at(index + 1).isSpace()) {
            token.negated = true;
            ++index;
        }
        auto ascii = [](QChar ch, bool first) {
            const ushort code = ch.unicode();
            if ((code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z')) return true;
            return !first && ((code >= '0' && code <= '9') || code == '_');
        };
        if (index < size && ascii(text.at(index), true)) {
            int end = index + 1;
            while (end < size && ascii(text.at(end), false)) ++end;
            if (end < size && text.at(end) == QLatin1Char(':')) {
                token.key = text.mid(index, end - index).toLower();
                token.hasKey = true;
                index = end + 1;
            }
        }
        if (index < size && text.at(index) == QLatin1Char('"')) {
            const int close = text.indexOf(QLatin1Char('"'), index + 1);
            if (close < 0) { token.value = text.mid(index + 1); index = size; }
            else { token.value = text.mid(index + 1, close - index - 1); index = close + 1; }
        } else {
            int stop = index;
            while (stop < size && !text.at(stop).isSpace()) ++stop;
            token.value = text.mid(index, stop - index);
            index = stop;
        }
        token.length = index - token.start;
        out.append(token);
    }
    return out;
}

}  // namespace

QString chipText(const QJsonObject &op) {
    const QString key = op.value(QStringLiteral("key")).toString();
    const QString value = op.value(QStringLiteral("value")).toString();
    const bool negated = op.value(QStringLiteral("negated")).toBool();
    if (key.isEmpty() || key == QLatin1String("text"))
        return negated ? QStringLiteral("not: ") + value : value;
    return (negated ? QStringLiteral("not ") : QString()) + key + QStringLiteral(": ") + value;
}

QString removeOperator(const QString &query, const QJsonObject &op) {
    const QString key = op.value(QStringLiteral("key")).toString();
    const QString value = op.value(QStringLiteral("value")).toString();
    const bool negated = op.value(QStringLiteral("negated")).toBool();
    if (value.isEmpty()) return query;
    const QList<Token> tokens = scanTokens(query);
    for (const Token &token : tokens) {
        bool match = false;
        if (key.isEmpty() || key == QLatin1String("text")) {
            // An excluded word or phrase: `-word`, `-"a phrase"`, or `-notakey:value`, whose whole
            // token is the excluded text. The reply has already taken the quotes off.
            const QString text = token.hasKey ? token.key + QLatin1Char(':') + token.value : token.value;
            match = token.negated == negated && !isOperatorKey(token.key)
                    && text.compare(value, Qt::CaseInsensitive) == 0;
        } else {
            match = token.hasKey && token.negated == negated && token.key == key
                    && token.value.compare(value, Qt::CaseInsensitive) == 0;
        }
        if (!match) continue;
        QString out = query;
        out.remove(token.start, token.length);
        return out.simplified();
    }
    return query;   // nothing in the box matches it: leave what the user typed alone
}

QString closedAgo(qint64 closedAtMs, qint64 nowMs) {
    if (closedAtMs <= 0) return {};
    const qint64 seconds = std::max<qint64>(0, (nowMs - closedAtMs) / 1000);
    if (seconds < 60) return QStringLiteral("closed just now");
    if (seconds < 3600) return QStringLiteral("closed %1 min ago").arg(seconds / 60);
    if (seconds < 86400) return QStringLiteral("closed %1 h ago").arg(seconds / 3600);
    if (seconds < 2 * 86400) return QStringLiteral("closed yesterday");
    return QStringLiteral("closed %1 days ago").arg(seconds / 86400);
}

QStringList badges(const QJsonObject &item, bool openNow, const QString &closedText,
                    const QString &usageTag) {
    QStringList tags;
    if (item.value(QStringLiteral("pinned")).toInt() > 0) tags << QStringLiteral("pinned");
    if (openNow) tags << QStringLiteral("open");
    else if (!closedText.isEmpty()) tags << closedText;
    if (item.value(QStringLiteral("unfinished")).toBool()) tags << QStringLiteral("unfinished");
    const int files = item.value(QStringLiteral("files_count")).toInt();
    if (files > 0)
        tags << (files == 1 ? QStringLiteral("edits · 1 file") : QStringLiteral("edits · %1 files").arg(files));
    else if (item.value(QStringLiteral("has_edits")).toBool())
        tags << QStringLiteral("edits");
    const QString branch = item.value(QStringLiteral("branch")).toString();
    if (!branch.isEmpty() && branch != QLatin1String("main") && branch != QLatin1String("master"))
        tags << branch;
    // What the conversation's pane is costing the machine right now (issue #D03W) goes last, and
    // is simply absent when the pane is idle. It is the only tag that changes while the row sits
    // there, and the delegate stops drawing tags at the row's edge: anywhere earlier and its
    // changing width would push "unfinished" and "edits · N files" off a narrow pane, and make
    // the badges that are still on the row jump about as it moved between 9% and 10%.
    if (!usageTag.isEmpty()) tags << usageTag;
    return tags;
}

QString elideMiddleText(const QString &text, int maxChars) {
    if (maxChars <= 1 || text.size() <= maxChars) return text;
    if (!text.contains(QLatin1Char('/'))) return text.left(maxChars - 1) + QChar(0x2026);
    // A path: the name at the end is what identifies it, so the middle goes and the name stays
    // whole when it fits at all.
    const int name = text.size() - text.lastIndexOf(QLatin1Char('/')) - 1;
    int keepRight = (maxChars - 1) * 2 / 3;
    if (name > keepRight && name <= maxChars - 2) keepRight = name;
    const int keepLeft = maxChars - 1 - keepRight;
    return text.left(keepLeft) + QChar(0x2026) + text.right(keepRight);
}

// ----- guest sessions (protocol 26.7) -------------------------------------------------------

bool isGuestSource(const QString &source) {
    return source == QLatin1String("claude") || source == QLatin1String("codex");
}

bool isGuestItem(const QJsonObject &item) {
    return isGuestSource(item.value(QStringLiteral("source")).toString());
}

QString guestLabel(const QString &source) {
    if (source == QLatin1String("claude")) return QStringLiteral("Claude Code");
    if (source == QLatin1String("codex")) return QStringLiteral("Codex");
    return source;
}

// One argv word for a POSIX shell. Relay builds the words itself from the worker's answer, but a
// session id is a file name the guest chose and a workspace is a path the user chose, so nothing
// is passed through unquoted.
QString shellWord(const QString &word) {
    if (word.isEmpty()) return QStringLiteral("''");
    bool plain = true;
    for (const QChar c : word)
        if (!(c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('-')
              || c == QLatin1Char('.') || c == QLatin1Char('/') || c == QLatin1Char(':')
              || c == QLatin1Char('=') || c == QLatin1Char('@') || c == QLatin1Char('+'))) {
            plain = false;
            break;
        }
    if (plain) return word;
    return QStringLiteral("'") + QString(word).replace(QLatin1Char('\''), QStringLiteral("'\\''")) + QStringLiteral("'");
}

QString guestCommand(const QJsonObject &item, bool fork) {
    if (!isGuestItem(item)) return QString();
    const QJsonArray argv = item.value(fork ? QStringLiteral("fork_command") : QStringLiteral("resume_command")).toArray();
    QStringList words;
    for (const auto &value : argv) {
        const QString word = value.toString();
        if (word.isEmpty() && !value.isString()) return QString();   // not an argv at all
        words << shellWord(word);
    }
    return words.join(QLatin1Char(' '));
}

QString guestCwd(const QJsonObject &item) {
    if (!isGuestItem(item)) return QString();
    QString cwd = item.value(QStringLiteral("resume_cwd")).toString();
    if (cwd.isEmpty()) cwd = item.value(QStringLiteral("workspace")).toString();
    return cwd;
}

QJsonArray continueItems(const QJsonArray &items, const QString &project,
                         const QSet<QString> &closedIds, int max) {
    QList<QJsonObject> picked;
    for (const auto &value : items) {
        const QJsonObject item = value.toObject();
        const QString source = item.value(QStringLiteral("source")).toString();
        if (source == QLatin1String("subagent") || source == QLatin1String("terminal")) continue;
        if (!project.isEmpty() && item.value(QStringLiteral("project")).toString() != project) continue;
        const bool pinned = item.value(QStringLiteral("pinned")).toInt() > 0;
        const bool unfinished = item.value(QStringLiteral("unfinished")).toBool();
        const bool closed = closedIds.contains(item.value(QStringLiteral("session_id")).toString());
        if (!pinned && !unfinished && !closed) continue;
        picked.append(item);
    }
    std::stable_sort(picked.begin(), picked.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a.value(QStringLiteral("updated")).toDouble() > b.value(QStringLiteral("updated")).toDouble();
    });
    QJsonArray out;
    for (int i = 0; i < picked.size() && (max <= 0 || i < max); ++i) out.append(picked.at(i));
    return out;
}

QString compactTokens(double tokens) {
    if (tokens >= 1000000) return QString::number(tokens / 1000000.0, 'f', 1) + QLatin1Char('M');
    if (tokens >= 1000) return QString::number(tokens / 1000.0, 'f', 1) + QLatin1Char('k');
    return QString::number(qint64(tokens));
}

QString estimateText(const QJsonObject &event) {
    const int count = event.value(QStringLiteral("count")).toInt();
    const QString scope = event.value(QStringLiteral("scope")).toString() == QLatin1String("all")
                              ? QStringLiteral("all projects") : QStringLiteral("this project");
    if (count <= 0)
        return QStringLiteral("Every conversation in %1 already has a summary. There is nothing to do.").arg(scope);
    QString model = event.value(QStringLiteral("model")).toString();
    if (model.isEmpty()) model = QStringLiteral("the chores model");
    return QStringLiteral("%1 conversation%2 in %3 %4 no summary. Summarising them costs about %5 input "
                          "and %6 output tokens on %7. Nothing is summarised unless you press Start.")
        .arg(count)
        .arg(count == 1 ? QString() : QStringLiteral("s"),
             scope, count == 1 ? QStringLiteral("has") : QStringLiteral("have"),
             compactTokens(event.value(QStringLiteral("approx_input_tokens")).toDouble()),
             compactTokens(event.value(QStringLiteral("approx_output_tokens")).toDouble()), model);
}

// ----- the list's rich-text rows (#MDSG) ----------------------------------------------------

RichTextCache::RichTextCache(int capacity) : m_capacity(std::max(1, capacity)) {}
RichTextCache::~RichTextCache() { clear(); }

int RichTextCache::size() const { return m_entries.size(); }

void RichTextCache::clear() {
    for (const Entry &entry : std::as_const(m_entries)) delete entry.document;
    m_entries.clear();
}

QTextDocument *RichTextCache::document(const QString &html, int width, const QFont &font) {
    // The font goes in the key as its own description rather than by identity: two QFonts that
    // describe the same face lay text out the same way and must share an entry.
    const QString key = QString::number(width) + QLatin1Char('\x1f') + font.toString()
                        + QLatin1Char('\x1f') + html;
    const auto it = m_entries.find(key);
    if (it != m_entries.end()) {
        ++m_hits;
        it->used = ++m_clock;
        return it->document;
    }
    ++m_misses;
    if (m_entries.size() >= m_capacity) {
        // Full: the one that has gone longest unasked for goes. Only a miss can get here, so the
        // scan costs nothing once the visible rows are all in.
        auto oldest = m_entries.begin();
        for (auto scan = m_entries.begin(); scan != m_entries.end(); ++scan)
            if (scan->used < oldest->used) oldest = scan;
        delete oldest->document;
        m_entries.erase(oldest);
    }
    auto *document = new QTextDocument;
    // The same three calls in the same order the delegate used to make inline: the layout, and so
    // the pixels, must not change because the document is kept.
    document->setDefaultFont(font);
    document->setHtml(html);
    document->setTextWidth(width);
    m_entries.insert(key, Entry{document, ++m_clock});
    return document;
}

// ----- the session manager pane -------------------------------------------------------------

namespace {
bool isThread(const QJsonObject &item) { return item.value(QStringLiteral("source")).toString() == QLatin1String("subagent"); }
// A **signal thread** (#AQ6X decision 9): the agent Relay started itself on a failing check nobody
// claimed. It is a subagent thread like any other, but it is not one of the user's — nobody asked
// for it — so it is listed whether or not "Subagent threads" is ticked, under its project rather
// than under the board worker's session (which is not a session anyone resumes), and marked. Its
// `agent_type` is the definition it runs on (`agents_defs.BUILTINS`, "signal"); its title is the
// signal's key.
bool isSignalThread(const QJsonObject &item) {
    return isThread(item) && item.value(QStringLiteral("agent_type")).toString() == QLatin1String("signal");
}
bool isTerminal(const QJsonObject &item) { return item.value(QStringLiteral("source")).toString() == QLatin1String("terminal"); }

QString escaped(const QString &text) { return text.simplified().toHtmlEscaped(); }

// A row of the list draws itself: the title with its short tags, the muted summary under it, and —
// for the rows an unfolded session holds — rich text that wraps, so a summary paragraph and a
// highlighted match line read as prose rather than as one elided line.
class RowDelegate : public QStyledItemDelegate {
public:
    explicit RowDelegate(QTreeWidget *tree) : QStyledItemDelegate(tree), m_tree(tree) {
        // A style sheet, a theme or a font change can move the text without changing the html or
        // the column width, and the cache keys on those three alone (#MDSG). Watching the tree is
        // how it hears about it; the pane's own event filter on the tree is untouched.
        m_tree->installEventFilter(this);
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        if (index.column() == 0) {
            const QString html = index.data(kHtmlRole).toString();
            if (!html.isEmpty()) {
                QTextDocument *document = m_cache.document(html, textWidth(index), option.font);
                return QSize(int(document->idealWidth()), int(document->size().height()) + 4);
            }
            if (!index.data(kTitleRole).toString().isEmpty()) {
                const QFontMetrics metrics(option.font);
                const bool two = !index.data(kSubRole).toString().isEmpty() || !index.data(kBadgeRole).toStringList().isEmpty();
                return QSize(160, metrics.height() * (two ? 2 : 1) + 8);
            }
        }
        return QStyledItemDelegate::sizeHint(option, index);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        const QString html = index.data(kHtmlRole).toString();
        const QString title = index.data(kTitleRole).toString();
        if (index.column() != 0 || (html.isEmpty() && title.isEmpty())) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        opt.text.clear();
        const QWidget *widget = opt.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);

        const bool selected = opt.state & QStyle::State_Selected;
        const QColor ink = selected ? opt.palette.color(QPalette::HighlightedText)
                                    : opt.palette.color(QPalette::Text);
        QColor muted = selected ? ink : opt.palette.color(QPalette::PlaceholderText);
        if (selected) muted.setAlphaF(0.75);
        const QRect rect = opt.rect.adjusted(3, 2, -4, -2);
        painter->save();
        painter->setFont(opt.font);
        if (!html.isEmpty()) {
            QTextDocument *document = m_cache.document(html, rect.width(), opt.font);
            painter->translate(rect.topLeft());
            QAbstractTextDocumentLayout::PaintContext context;
            context.palette.setColor(QPalette::Text, ink);
            context.clip = QRectF(0, 0, rect.width(), rect.height());
            document->documentLayout()->draw(painter, context);
            painter->restore();
            return;
        }
        // The title owns the first line: a narrow pane must not turn "Fix the FTS index" into
        // "Fix the …" to make room for tags. The tags lead the second line, the summary follows.
        const QFontMetrics metrics(opt.font);
        painter->setPen(ink);
        painter->drawText(QRect(rect.left(), rect.top(), rect.width(), metrics.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, metrics.elidedText(title, Qt::ElideRight, rect.width()));
        const QStringList tags = index.data(kBadgeRole).toStringList();
        const QString sub = index.data(kSubRole).toString();
        if (tags.isEmpty() && sub.isEmpty()) { painter->restore(); return; }
        const int lineTop = rect.top() + metrics.height() + 2;
        int x = rect.left();
        painter->setPen(muted);
        for (const QString &tag : tags) {
            const int width = metrics.horizontalAdvance(tag) + 10;
            if (x + width > rect.right()) break;
            const QRect box(x, lineTop + 1, width, metrics.height() - 2);
            painter->drawRoundedRect(box, 4, 4);
            painter->drawText(box, Qt::AlignCenter, tag);
            x += width + 6;
        }
        if (!sub.isEmpty() && x < rect.right() - 24)
            painter->drawText(QRect(x, lineTop, rect.right() - x, metrics.height()),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              metrics.elidedText(sub, Qt::ElideRight, rect.right() - x));
        painter->restore();
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (object == m_tree)
            switch (event->type()) {
            case QEvent::FontChange:
            case QEvent::PaletteChange:
            case QEvent::StyleChange:
            case QEvent::ApplicationFontChange:
            case QEvent::ApplicationPaletteChange:
                m_cache.clear();
                break;
            default:
                break;
            }
        return QStyledItemDelegate::eventFilter(object, event);
    }

private:
    // How wide column 0 is for this row: the column less the indentation its depth costs.
    int textWidth(const QModelIndex &index) const {
        int depth = 1;
        for (QModelIndex parent = index.parent(); parent.isValid(); parent = parent.parent()) ++depth;
        return std::max(80, m_tree->columnWidth(0) - depth * m_tree->indentation() - 10);
    }
    QTreeWidget *m_tree = nullptr;
    // sizeHint() and paint() are const and both want the document; the cache is the delegate's
    // own scratch, not part of what it says about a row.
    mutable RichTextCache m_cache;
};
}  // namespace

SessionManager::SessionManager(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("sessionManager"));

    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("sessionsSearch"));
    m_search->setClearButtonEnabled(true);
    m_search->setPlaceholderText(QStringLiteral("Search every session · \"a phrase\" · file: model: branch: is:pinned -not · ? for the list"));
    m_search->installEventFilter(this);
    m_help = new QToolButton;
    m_help->setObjectName(QStringLiteral("sessionsHelp"));
    m_help->setText(QStringLiteral("?"));
    m_help->setAutoRaise(true);
    m_help->setToolTip(QStringLiteral("What the search box understands"));
    connect(m_help, &QToolButton::clicked, this, &SessionManager::showOperatorHelp);

    m_chipRow = new QWidget;
    m_chipRow->setObjectName(QStringLiteral("sessionsChips"));
    auto *chips = new QHBoxLayout(m_chipRow);
    chips->setContentsMargins(0, 0, 0, 0);
    chips->setSpacing(6);
    chips->addStretch(1);
    m_chipRow->setVisible(false);
    m_ignored = new QLabel;
    m_ignored->setObjectName(QStringLiteral("dialogHint"));
    m_ignored->setWordWrap(true);
    m_ignored->setVisible(false);

    m_scope = new QComboBox;
    m_scope->addItem(QStringLiteral("This project"), QStringLiteral("project"));
    m_scope->addItem(QStringLiteral("All projects"), QStringLiteral("all"));
    m_kind = new QComboBox;
    m_kind->setObjectName(QStringLiteral("sessionsKind"));
    m_kind->addItem(QStringLiteral("Everything"), QString());
    m_kind->addItem(QStringLiteral("Agent sessions"), QStringLiteral("agent"));
    m_kind->addItem(QStringLiteral("Terminal history"), QStringLiteral("terminal"));
    // The guests (protocol 26.7). They are sources of the same index, so "Everything" is
    // everything: the request names all four rather than leaving the worker to guess.
    m_kind->addItem(QStringLiteral("Claude Code sessions"), QStringLiteral("claude"));
    m_kind->addItem(QStringLiteral("Codex sessions"), QStringLiteral("codex"));
    // By project (card #916B): the projects Relay knows, fed by setKnownProjects(), or none of
    // them. A row's project is its workspace folder, so this is the index's own test on that
    // column (protocol 14.3 `project` / `outside_projects`), not a guess from the row's name.
    m_projectFilter = new QComboBox;
    m_projectFilter->setObjectName(QStringLiteral("sessionsProject"));
    m_projectFilter->addItem(QStringLiteral("Any project"), QString());
    m_projectFilter->addItem(QStringLiteral("No project"), QStringLiteral("none"));
    m_projectFilter->setToolTip(QStringLiteral("Only the sessions of one project Relay knows, or the ones outside every known project"));
    m_model = new QComboBox;
    m_model->addItem(QStringLiteral("Any model"), QString());
    m_date = new QComboBox;
    m_date->addItem(QStringLiteral("Any time"), QStringLiteral("any"));
    m_date->addItem(QStringLiteral("Today"), QStringLiteral("today"));
    m_date->addItem(QStringLiteral("Last 7 days"), QStringLiteral("week"));
    m_date->addItem(QStringLiteral("Last 30 days"), QStringLiteral("month"));
    m_branch = new QComboBox;
    m_branch->setObjectName(QStringLiteral("sessionsBranch"));
    m_branch->addItem(QStringLiteral("Any branch"), QString());
    m_branch->setVisible(false);            // shown once the facets name more than one branch
    m_group = new QComboBox;
    m_group->setObjectName(QStringLiteral("sessionsGroup"));
    m_group->addItem(QStringLiteral("By project"), QStringLiteral("project"));
    m_group->addItem(QStringLiteral("By date"), QStringLiteral("date"));
    m_group->addItem(QStringLiteral("No grouping"), QStringLiteral("none"));
    m_sort = new QComboBox;
    m_sort->setObjectName(QStringLiteral("sessionsSort"));
    m_sort->addItem(QStringLiteral("Newest first"), QStringLiteral("recent"));
    m_sort->addItem(QStringLiteral("Oldest first"), QStringLiteral("oldest"));
    m_sort->addItem(QStringLiteral("Most turns"), QStringLiteral("longest"));
    m_sort->addItem(QStringLiteral("Fewest turns"), QStringLiteral("shortest"));
    m_sort->addItem(QStringLiteral("Title A→Z"), QStringLiteral("title"));
    m_sort->addItem(QStringLiteral("Title Z→A"), QStringLiteral("title_desc"));
    m_sort->addItem(QStringLiteral("Model A→Z"), QStringLiteral("model"));
    m_sort->addItem(QStringLiteral("Model Z→A"), QStringLiteral("model_desc"));
    m_sort->addItem(QStringLiteral("Best match"), QStringLiteral("relevance"));

    // The three-state filters of protocol 14.3, as a menu so the filter row stays one line in a
    // narrow pane. Unticked means "do not filter", never "only the ones without it".
    m_filters = new QToolButton;
    m_filters->setObjectName(QStringLiteral("sessionsFilters"));
    m_filters->setText(QStringLiteral("More ▾"));
    m_filters->setPopupMode(QToolButton::InstantPopup);
    m_filterMenu = new QMenu(this);
    m_filters->setMenu(m_filterMenu);
    auto toggle = [this](const QString &label, const QString &name) {
        auto *action = m_filterMenu->addAction(label);
        action->setObjectName(name);
        action->setCheckable(true);
        connect(action, &QAction::toggled, this, &SessionManager::requery);
        return action;
    };
    m_hasEdits = toggle(QStringLiteral("Has edits"), QStringLiteral("filterHasEdits"));
    m_unfinished = toggle(QStringLiteral("Unfinished"), QStringLiteral("filterUnfinished"));
    m_pinnedOnly = toggle(QStringLiteral("Pinned"), QStringLiteral("filterPinned"));
    m_hasSummary = toggle(QStringLiteral("Has a summary"), QStringLiteral("filterHasSummary"));
    m_openTasks = toggle(QStringLiteral("Open tasks"), QStringLiteral("filterOpenTasks"));
    m_filterMenu->addSeparator();
    m_summariseAll = m_filterMenu->addAction(QStringLiteral("Summarise all…"));
    m_summariseAll->setObjectName(QStringLiteral("summariseAll"));
    m_summariseAll->setToolTip(QStringLiteral("What it would cost first; nothing is summarised until you press Start"));
    connect(m_summariseAll, &QAction::triggered, this, &SessionManager::askEstimate);

    // Owner, 2026-09-18: threads are findable here, but off unless asked for.
    m_threads = new QCheckBox(QStringLiteral("Subagent threads"));
    m_threads->setObjectName(QStringLiteral("sessionsThreads"));
    m_threads->setChecked(false);
    m_threads->setToolTip(QStringLiteral("Also list and search every subagent thread, under the session that started it"));

    m_tree = new QTreeWidget;
    m_tree->setObjectName(QStringLiteral("sessionsTree"));
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({QStringLiteral("Session"), QStringLiteral("Updated"),
                             QStringLiteral("Turns"), QStringLiteral("Model")});
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(false);
    m_tree->setAllColumnsShowFocus(true);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_tree->setItemDelegateForColumn(0, new RowDelegate(m_tree));
    // Qt's own tree sort stays off forever: it would reorder the group rows ("Continue", the
    // date groups) and sort the display text ("14 min ago"). Sorting is asked of the worker —
    // a header click is the Sort combo in another form (the sectionClicked wiring below).
    m_tree->setSortingEnabled(false);
    // The unfolded rows wrap, so their height depends on how wide the first column is.
    connect(m_tree->header(), &QHeaderView::sectionResized, this,
            [this](int section, int, int) { if (section == 0) m_tree->doItemsLayout(); });
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *item) { unfold(item); });
    m_tree->installEventFilter(this);

    m_header = new QLabel;
    m_header->setWordWrap(true);
    m_header->setTextFormat(Qt::RichText);
    m_preview = new QTextBrowser;
    m_preview->setOpenExternalLinks(false);
    m_preview->setObjectName(QStringLiteral("conversationPreview"));
    relay::installCopyOnSelect(m_preview);
    m_summarise = new QPushButton(QStringLiteral("Summarise"));
    m_summarise->setObjectName(QStringLiteral("summariseOne"));
    m_summarise->setToolTip(QStringLiteral("Write a short summary of this conversation with the chores model"));
    m_summarise->setVisible(false);
    connect(m_summarise, &QPushButton::clicked, this, &SessionManager::summariseSelected);

    auto *right = new QWidget;
    auto *rightBox = new QVBoxLayout(right);
    rightBox->setContentsMargins(0, 0, 0, 0);
    auto *headerRow = new QHBoxLayout;
    headerRow->addWidget(m_header, 1);
    headerRow->addWidget(m_summarise, 0, Qt::AlignTop);
    rightBox->addLayout(headerRow);
    rightBox->addWidget(m_preview, 1);

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(m_tree);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    m_status = new QLabel;
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    m_cancelBatch = new QPushButton(QStringLiteral("Cancel"));
    m_cancelBatch->setObjectName(QStringLiteral("cancelSummaries"));
    m_cancelBatch->setVisible(false);
    connect(m_cancelBatch, &QPushButton::clicked, this, [this] {
        if (onSummariseCancel) onSummariseCancel();
        m_note = QStringLiteral("Stopping after the conversation being summarised…");
        updateStatus();
    });

    // "Summarise all…" asks what it would cost and waits here for a Start. It is a row of the pane,
    // not a modal box: the list stays readable while the number is read.
    m_confirm = new QFrame;
    m_confirm->setObjectName(QStringLiteral("summariseConfirm"));
    m_confirm->setFrameShape(QFrame::StyledPanel);
    m_confirm->setVisible(false);
    m_confirmText = new QLabel;
    m_confirmText->setObjectName(QStringLiteral("summariseEstimate"));
    m_confirmText->setWordWrap(true);
    auto *start = new QPushButton(QStringLiteral("Start"));
    start->setObjectName(QStringLiteral("summariseStart"));
    start->setEnabled(false);
    auto *cancelConfirm = new QPushButton(QStringLiteral("Cancel"));
    auto *confirmRow = new QHBoxLayout(m_confirm);
    confirmRow->setContentsMargins(8, 6, 8, 6);
    confirmRow->addWidget(m_confirmText, 1);
    confirmRow->addWidget(cancelConfirm);
    confirmRow->addWidget(start);
    connect(start, &QPushButton::clicked, this, &SessionManager::startBatch);
    connect(cancelConfirm, &QPushButton::clicked, this, [this] { m_confirm->setVisible(false); });

    // Nothing matched, or nothing is saved yet: say which, and offer the way out of it.
    m_empty = new QLabel;
    m_empty->setObjectName(QStringLiteral("dialogHint"));
    m_empty->setWordWrap(true);
    m_searchAll = new QPushButton(QStringLiteral("Search all projects"));
    m_searchAll->setObjectName(QStringLiteral("searchAllProjects"));
    connect(m_searchAll, &QPushButton::clicked, this, [this] {
        m_scope->setCurrentIndex(m_scope->findData(QStringLiteral("all")));
    });
    m_clearFilters = new QPushButton(QStringLiteral("Clear filters"));
    m_clearFilters->setObjectName(QStringLiteral("clearFilters"));
    connect(m_clearFilters, &QPushButton::clicked, this, &SessionManager::clearFilters);
    m_emptyRow = new QWidget;
    auto *emptyRow = new QHBoxLayout(m_emptyRow);
    emptyRow->setContentsMargins(0, 0, 0, 0);
    emptyRow->addWidget(m_empty, 1);
    emptyRow->addWidget(m_searchAll);
    emptyRow->addWidget(m_clearFilters);
    m_emptyRow->setVisible(false);

    m_resume = new QPushButton(QStringLiteral("Resume here"));
    m_resume->setDefault(true);
    m_newPane = new QPushButton(QStringLiteral("Open in new pane"));
    m_info = new QPushButton(QStringLiteral("Info"));
    m_info->setToolTip(QStringLiteral("Its ⓘ view: model, tokens, file and history with subagent threads (Ctrl+I)"));
    m_rename = new QPushButton(QStringLiteral("Rename…"));
    m_pin = new QPushButton(QStringLiteral("Pin"));
    m_delete = new QPushButton(QStringLiteral("Delete…"));
    m_more = new QPushButton(QStringLiteral("Show more"));
    m_more->setVisible(false);
    auto *close = new QPushButton(QStringLiteral("Close"));

    // Two rows, so no filter is cut short in a pane half the window wide: what the list holds,
    // then how it is narrowed, grouped and ordered.
    for (QComboBox *combo : {m_scope, m_kind, m_projectFilter, m_model, m_date, m_sort, m_branch, m_group})
        combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    auto *filters = new QHBoxLayout;
    filters->setSpacing(8);
    filters->addWidget(m_scope);
    filters->addWidget(m_kind);
    filters->addWidget(m_projectFilter);
    filters->addWidget(m_threads);
    filters->addStretch(1);
    auto *narrow = new QHBoxLayout;
    narrow->setSpacing(8);
    narrow->addWidget(m_model);
    narrow->addWidget(m_branch);
    narrow->addWidget(m_date);
    narrow->addWidget(m_group);
    narrow->addWidget(m_sort);
    narrow->addWidget(m_filters);
    narrow->addStretch(1);

    auto *searchRow = new QHBoxLayout;
    searchRow->setSpacing(6);
    searchRow->addWidget(m_search, 1);
    searchRow->addWidget(m_help);

    auto *statusRow = new QHBoxLayout;
    statusRow->addWidget(m_status, 1);
    statusRow->addWidget(m_cancelBatch);
    statusRow->addWidget(m_more);

    m_reopen = new QPushButton(QStringLiteral("Reopen where it was"));
    m_reopen->setObjectName(QStringLiteral("reopenClosed"));
    m_reopen->setToolTip(QStringLiteral("Put the pane, tab or window this conversation was in back where it was (Alt+Enter)"));
    m_reopen->setVisible(false);
    connect(m_reopen, &QPushButton::clicked, this, &SessionManager::reopenClosed);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addWidget(m_info);
    buttons->addWidget(m_rename);
    buttons->addWidget(m_pin);
    buttons->addWidget(m_delete);
    buttons->addStretch(1);
    buttons->addWidget(m_reopen);
    buttons->addWidget(m_newPane);
    buttons->addWidget(m_resume);
    buttons->addWidget(close);

    auto *list = new QWidget;
    auto *box = new QVBoxLayout(list);
    box->setContentsMargins(8, 8, 8, 8);
    box->setSpacing(6);
    box->addLayout(searchRow);
    box->addWidget(m_chipRow);
    box->addWidget(m_ignored);
    box->addLayout(filters);
    box->addLayout(narrow);
    box->addLayout(statusRow);
    box->addWidget(m_confirm);
    box->addWidget(m_emptyRow);
    box->addWidget(splitter, 1);
    auto *hint = new QLabel(QStringLiteral("Enter resumes here · Shift+Enter a new pane · → a quick look · F2 rename · Ctrl+P pin · Ctrl+F search · Esc closes"));
    hint->setObjectName(QStringLiteral("dialogHint"));
    hint->setWordWrap(true);
    box->addWidget(hint);
    box->addLayout(buttons);

    m_tabs = new QTabWidget;
    m_tabs->setObjectName(QStringLiteral("sessionsTabs"));
    m_tabs->setDocumentMode(true);
    m_tabs->addTab(list, QStringLiteral("Sessions"));
    m_tabs->widget(0)->setProperty("tabId", QStringLiteral("sessions"));
    m_tabs->tabBar()->setVisible(false);   // shown once another tab is added
    // The pane chrome's buttons sit over the top-right corner: keep the tab bar's end clear.
    m_inset = new QWidget;
    m_inset->setFixedSize(0, 1);
    m_tabs->setCornerWidget(m_inset, Qt::TopRightCorner);

    // The helper agent's panel, at the bottom of the pane and collapsed to one row (#FEJQ): the
    // same panel the Switchboard carries, asking the same per-tab worker with `pane: "sessions"`.
    // Below the tabs rather than inside the list, because the helper is about the pane and every
    // tab of it — "when you are in options, actions, or sessions, you have a helper agent, same
    // as the switchboard agent" (owner).
    m_helper = new HelperChatPanel(helperpane::sessions());
    m_helper->onSend = [this](const QJsonObject &message) {
        if (onHelperSend) onHelperSend(message);
    };
    m_helper->nextRequestId = [this] {
        return nextHelperRequestId ? nextHelperRequestId() : QString();
    };
    m_helper->onHint = [this](const QString &id, const QString &keys) {
        if (onHelperHint) onHelperHint(id, keys);
    };
    m_helper->onStatus = [this](const QString &text) {
        m_note = text;          // the one-off line, cleared by the next row or query
        updateStatus();
    };
    m_helper->onOpenSession = [this](const QString &id) { revealSession(id); };
    m_helper->onOpenOption = [this](const QString &section, const QString &row) {
        if (onHelperOpenOption) onHelperOpenOption(section, row);
    };
    m_helper->onOpenCard = [this](const QString &id) {
        if (onHelperOpenCard) onHelperOpenCard(id);
    };
    m_helper->onOpenFile = [this](const QString &path) {
        if (onHelperOpenFile) onHelperOpenFile(path);
    };
    m_helper->setAskShortcut(QStringLiteral("sessions.ask"), QStringLiteral("Ctrl+/"));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(m_tabs, 1);
    outer->addWidget(m_helper, 0);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(120);
    connect(m_debounce, &QTimer::timeout, this, &SessionManager::requery);

    // The "closed N min ago" tag ages while the pane sits open. It ticks well inside the minute
    // the wording turns on, and only while something is in the closed list; the list changing
    // (an item reopened or pushed off the end) comes separately, through setClosedSessions.
    m_ages = new QTimer(this);
    m_ages->setInterval(20'000);
    connect(m_ages, &QTimer::timeout, this, &SessionManager::refreshClosedAges);

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) {
        // Searching wants the best match first, listing wants the newest — until the user picks a
        // sort by hand, after which their choice stands whatever they type.
        if (!m_sortChosen) {
            const QString want = text.trimmed().isEmpty() ? QStringLiteral("recent") : QStringLiteral("relevance");
            if (m_sort->currentData().toString() != want) {
                const QSignalBlocker quiet(m_sort);
                m_sort->setCurrentIndex(m_sort->findData(want));
                // Blocked, so the currentIndexChanged wiring did not see this: the arrow moves here.
                updateSortIndicator();
            }
        }
        scheduleQuery();
    });
    for (QComboBox *combo : {m_scope, m_kind, m_projectFilter, m_model, m_date, m_sort, m_branch})
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SessionManager::requery);
    // Grouping is drawn here, not asked of the worker.
    connect(m_group, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { rebuildTree(selectedId()); });
    // A sort the user picked by hand is theirs: the query text stops changing it. The list's own
    // switches happen under a QSignalBlocker, so any change that arrives here is the user's.
    connect(m_sort, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { m_sortChosen = true; updateSortIndicator(); });
    // A header click sorts by that column through the combo's own chain: the combo follows (so it
    // never names an order the list is not in), the click counts as the user's own sort, and the
    // header shows the arrow. Qt's own tree sort is never used (see the tree's construction).
    connect(m_tree->header(), &QHeaderView::sectionClicked, this, [this](int column) {
        const QString next = nextHeaderSort(column, m_sort->currentData().toString());
        if (next == m_sort->currentData().toString()) return;
        const int at = m_sort->findData(next);
        if (at >= 0) m_sort->setCurrentIndex(at);
    });
    updateSortIndicator();
    connect(m_threads, &QCheckBox::toggled, this, [this](bool on) {
        m_tree->headerItem()->setText(0, on ? QStringLiteral("Session / subagent thread") : QStringLiteral("Session"));
        requery();
    });
    connect(m_tree, &QTreeWidget::currentItemChanged, this, &SessionManager::selectionChanged);
    connect(m_tree, &QTreeWidget::itemActivated, this, [this] { activate(false); });
    connect(m_resume, &QPushButton::clicked, this, [this] { activate(false); });
    connect(m_newPane, &QPushButton::clicked, this, [this] { activate(true); });
    connect(m_info, &QPushButton::clicked, this, [this] {
        const QJsonObject item = selectedItem();
        if (item.isEmpty() || isTerminal(item)) return;
        if (isThread(item)) { if (onOpenThread) onOpenThread(item); }
        else if (onOpenInfo) onOpenInfo(item);
    });
    connect(m_rename, &QPushButton::clicked, this, &SessionManager::rename);
    connect(m_pin, &QPushButton::clicked, this, &SessionManager::togglePin);
    connect(m_delete, &QPushButton::clicked, this, &SessionManager::remove);
    connect(m_more, &QPushButton::clicked, this, &SessionManager::requestMore);
    connect(close, &QPushButton::clicked, this, [this] { if (onClose) onClose(); });
    updateButtons();
}

QString SessionManager::query() const { return m_search ? m_search->text() : QString(); }

void SessionManager::setQuery(const QString &text) {
    if (m_search->text() == text) return;
    m_search->setText(text);   // re-runs the query through textChanged
}

void SessionManager::focusSearch() {
    m_search->setFocus();
    m_search->selectAll();
}

bool SessionManager::threadsShown() const { return m_threads->isChecked(); }
void SessionManager::setThreadsShown(bool on) { m_threads->setChecked(on); }

void SessionManager::addTab(const QString &id, const QString &label, QWidget *widget) {
    if (!widget || id.isEmpty() || id == QLatin1String("sessions")) return;
    for (int i = 0; i < m_tabs->count(); ++i)
        if (m_tabs->widget(i)->property("tabId").toString() == id) return;
    widget->setProperty("tabId", id);
    m_tabs->addTab(widget, label);
    m_tabs->tabBar()->setVisible(true);
}

void SessionManager::showTab(const QString &id) {
    for (int i = 0; i < m_tabs->count(); ++i)
        if (m_tabs->widget(i)->property("tabId").toString() == (id.isEmpty() ? QStringLiteral("sessions") : id)) {
            m_tabs->setCurrentIndex(i);
            return;
        }
}

QString SessionManager::currentTab() const {
    return m_tabs->currentWidget() ? m_tabs->currentWidget()->property("tabId").toString() : QString();
}

QString SessionManager::paneTitle() const { return QStringLiteral("Sessions"); }

void SessionManager::focusView() {
    if (currentTab() == QLatin1String("sessions")) focusSearch();
    else if (m_tabs->currentWidget()) m_tabs->currentWidget()->setFocus(Qt::OtherFocusReason);
}

// ----- the helper agent's panel (#FEJQ) ---------------------------------------------------
//
// The manager holds no worker of its own: what the panel sends goes out through `onHelperSend`
// and what the worker answers comes back through `helperEvent`, the same shape the session search
// already has. Every message the panel sends already carries `pane: "sessions"`, and it takes only
// the events tagged with it, so handing it the whole stream is safe.

void SessionManager::helperEvent(const QString &type, const QJsonObject &event) {
    if (m_helper) m_helper->handleEvent(type, event);
}

void SessionManager::setHelperPresets(const QJsonArray &presets) {
    if (m_helper) m_helper->setPresets(presets);
}

void SessionManager::addHelperComposerWidget(QWidget *widget) {
    if (m_helper) m_helper->addComposerWidget(widget);
}

void SessionManager::setHelperShortcut(const QString &hintId, const QString &keys) {
    if (m_helper) m_helper->setAskShortcut(hintId, keys);
}

void SessionManager::focusHelper() {
    if (m_helper) m_helper->expand();
}

void SessionManager::helperDraft(const QString &text) {
    if (m_helper) m_helper->prefill(text);
}

// `session:<id>` in an answer. The row is usually on screen already — the helper was asked about
// what is in the list — and then this is a selection and nothing else. When the query in force
// does not draw it, searching for the id is what a person would do next, and `m_pendingSelect`
// picks the row out when the results land.
void SessionManager::revealSession(const QString &sessionId) {
    if (sessionId.isEmpty()) return;
    if (QTreeWidgetItem *row = m_rows.value(sessionId)) {
        m_tree->setCurrentItem(row);
        m_tree->scrollToItem(row);
        return;
    }
    m_pendingSelect = sessionId;
    setQuery(sessionId);
}

void SessionManager::setHeaderRightInset(int pixels) {
    m_inset->setFixedSize(std::max(0, pixels), 1);
    // With no tab bar the search row is the first row; its right end must stay clear too.
    if (auto *list = m_tabs->widget(0); list && list->layout())
        list->layout()->setContentsMargins(8, 8, 8 + (m_tabs->tabBar()->isVisible() ? 0 : std::max(0, pixels)), 8);
}

void SessionManager::scheduleQuery() { m_debounce->start(); }

void SessionManager::refresh() { requery(); }

void SessionManager::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    // An ask row that does nothing is worse than no ask row, so the helper's panel is there only
    // once the window has given it somewhere to send (the Ask row on the ⓘ view does the same with
    // `onAskOwner`). By the time the pane is on screen the wiring has happened or it never will.
    if (m_helper) m_helper->setVisible(bool(onHelperSend));
    // Opening the list must show the sessions without the user typing anything.
    requery();
}

QString SessionManager::scopeId() const { return m_scope->currentData().toString(); }

bool SessionManager::anyFilter() const {
    return m_hasEdits->isChecked() || m_unfinished->isChecked() || m_pinnedOnly->isChecked()
           || m_hasSummary->isChecked() || m_openTasks->isChecked()
           || !m_model->currentData().toString().isEmpty() || !m_branch->currentData().toString().isEmpty()
           || m_date->currentData().toString() != QLatin1String("any")
           || !m_kind->currentData().toString().isEmpty()
           || !m_projectFilter->currentData().toString().isEmpty();
}

void SessionManager::clearFilters() {
    const QSignalBlocker quietModel(m_model), quietBranch(m_branch), quietDate(m_date), quietKind(m_kind),
        quietProject(m_projectFilter);
    m_model->setCurrentIndex(0);
    m_branch->setCurrentIndex(0);
    m_date->setCurrentIndex(0);
    m_kind->setCurrentIndex(0);
    m_projectFilter->setCurrentIndex(0);
    for (QAction *action : {m_hasEdits, m_unfinished, m_pinnedOnly, m_hasSummary, m_openTasks}) {
        const QSignalBlocker quiet(action);
        action->setChecked(false);
    }
    requery();
}

// The `conversations` request body for what the box and the filters say (protocol 14.3). The
// operators the user types are the worker's business: they are left in `query` untouched, and the
// filters only ever add their own explicit fields, so the two never fight over one thing.
QJsonObject SessionManager::queryRequest() const {
    QJsonObject request{{QStringLiteral("query"), m_search->text()},
                        {QStringLiteral("scope"), scopeId()},
                        {QStringLiteral("limit"), 100}};
    if (m_openTasks->isChecked()) request.insert(QStringLiteral("has_open_tasks"), true);
    if (m_hasEdits->isChecked()) request.insert(QStringLiteral("has_edits"), true);
    if (m_unfinished->isChecked()) request.insert(QStringLiteral("unfinished"), true);
    if (m_pinnedOnly->isChecked()) request.insert(QStringLiteral("pinned"), true);
    if (m_hasSummary->isChecked()) request.insert(QStringLiteral("has_summary"), true);
    const QString model = m_model->currentData().toString();
    if (!model.isEmpty()) request.insert(QStringLiteral("model"), model);
    const QString branch = m_branch->currentData().toString();
    if (!branch.isEmpty()) request.insert(QStringLiteral("branch"), branch);
    const double since = sinceFor(m_date->currentData().toString(), QDateTime::currentDateTime());
    if (since > 0) request.insert(QStringLiteral("since"), since);
    const QString kind = m_kind->currentData().toString();
    request.insert(QStringLiteral("sources"),
                   kind.isEmpty() ? QJsonArray{QStringLiteral("agent"), QStringLiteral("terminal"),
                                               QStringLiteral("claude"), QStringLiteral("codex")}
                                  : QJsonArray{kind});
    // Threads are always asked for, and the *unticked* box drops the user's own again below
    // (`rebuildTree`): a signal thread is Relay's, not theirs, and decision 9 says it is listed.
    // Asking only when the box is ticked would mean a pickup was invisible until somebody found a
    // checkbox for a feature they had never heard of.
    request.insert(QStringLiteral("include_threads"), true);
    // The "Project" chooser (#916B). Either field makes the worker answer across all projects
    // whatever `scope` says, and it reports that scope back, which the menu then follows.
    const QString project = m_projectFilter->currentData().toString();
    if (project == QLatin1String("none")) {
        QJsonArray outside;
        for (const auto &known : m_knownProjects) outside.append(known.second);
        request.insert(QStringLiteral("outside_projects"), outside);
    } else if (!project.isEmpty()) {
        request.insert(QStringLiteral("project"), project);
    }
    const QString sort = m_sort->currentData().toString();
    if (sort != QLatin1String("recent")) request.insert(QStringLiteral("sort"), sort);
    return request;
}

void SessionManager::requery() {
    if (!onQuery) return;
    m_nextOffset = -1;
    m_previewFor.clear();          // a new list: the side preview is asked for afresh
    onQuery(queryRequest());
}

void SessionManager::requestMore() {
    if (!onQuery || m_nextOffset < 0) return;
    QJsonObject request = queryRequest();
    request.insert(QStringLiteral("offset"), m_nextOffset);
    onQuery(request);
}

void SessionManager::removed(const QString &sessionId) {
    Q_UNUSED(sessionId);
    requery();
}

void SessionManager::setResults(const QJsonObject &event) {
    const QJsonArray items = event.value(QStringLiteral("items")).toArray();
    const QString keep = m_pendingSelect.isEmpty() ? selectedId() : m_pendingSelect;
    m_pendingSelect.clear();
    if (event.value(QStringLiteral("offset")).toInt() > 0) {
        for (const auto &value : items) m_items.append(value);   // "Show more": the next page
    } else {
        m_items = items;
    }
    m_nextOffset = event.contains(QStringLiteral("next_offset")) ? event.value(QStringLiteral("next_offset")).toInt() : -1;
    m_more->setVisible(m_nextOffset >= 0);
    m_elapsed = event.value(QStringLiteral("elapsed_ms")).toDouble();
    // The scope the worker actually used: `project:` in the box means all projects, whatever the
    // menu says, and the menu follows rather than lying about what is listed (protocol 14.3).
    const QString scope = event.value(QStringLiteral("scope")).toString();
    if (!scope.isEmpty() && scope != scopeId() && m_scope->findData(scope) >= 0) {
        const QSignalBlocker quiet(m_scope);
        m_scope->setCurrentIndex(m_scope->findData(scope));
    }
    rebuildChips(event.value(QStringLiteral("parsed")).toObject());
    fillFacets(event.value(QStringLiteral("facets")).toObject());
    rebuildTree(keep);
}

void SessionManager::rebuildChips(const QJsonObject &parsed) {
    auto *row = qobject_cast<QHBoxLayout *>(m_chipRow->layout());
    while (row->count() > 1) {                       // the trailing stretch stays
        QLayoutItem *item = row->takeAt(0);
        if (QWidget *widget = item->widget()) { widget->hide(); widget->setParent(nullptr); widget->deleteLater(); }
        delete item;
    }
    const QJsonArray operators = parsed.value(QStringLiteral("operators")).toArray();
    for (const auto &value : operators) {
        const QJsonObject op = value.toObject();
        auto *chip = new QToolButton;
        chip->setObjectName(QStringLiteral("stripChip"));
        chip->setProperty("chipKey", op.value(QStringLiteral("key")).toString());
        chip->setText(chipText(op) + QStringLiteral("  ×"));
        chip->setToolTip(QStringLiteral("Take this out of the search"));
        connect(chip, &QToolButton::clicked, this,
                [this, op] { m_search->setText(removeOperator(m_search->text(), op)); });
        row->insertWidget(row->count() - 1, chip);
    }
    m_chipRow->setVisible(!operators.isEmpty());
    const QJsonArray ignored = parsed.value(QStringLiteral("ignored")).toArray();
    QStringList words;
    for (const auto &value : ignored) words << value.toString();
    m_ignored->setText(words.isEmpty() ? QString()
                                       : QStringLiteral("ignored: ") + words.join(QStringLiteral(" · ")));
    m_ignored->setVisible(!words.isEmpty());
}

// The model and branch menus are the distinct values of the rows the filters select, so they never
// need a second request and never empty out as the user types (protocol 14.3).
void SessionManager::fillFacets(const QJsonObject &facets) {
    auto fill = [](QComboBox *combo, const QJsonArray &values, const QString &any) {
        const QString keep = combo->currentData().toString();
        const QSignalBlocker quiet(combo);
        combo->clear();
        combo->addItem(any, QString());
        for (const auto &value : values) {
            const QString text = value.toString();
            if (!text.isEmpty()) combo->addItem(text, text);
        }
        // A value that has gone out of the facets stays selectable until the user drops it.
        if (!keep.isEmpty() && combo->findData(keep) < 0) combo->addItem(keep, keep);
        combo->setCurrentIndex(std::max(0, combo->findData(keep)));
        return combo->count() - 1;
    };
    if (facets.contains(QStringLiteral("models")))
        fill(m_model, facets.value(QStringLiteral("models")).toArray(), QStringLiteral("Any model"));
    if (facets.contains(QStringLiteral("branches"))) {
        const int branches = fill(m_branch, facets.value(QStringLiteral("branches")).toArray(), QStringLiteral("Any branch"));
        m_branch->setVisible(branches > 1);          // one branch everywhere is not a filter
    }
}

// One row of the list: the title with its tags, the muted summary under it, and the columns.
QTreeWidgetItem *SessionManager::addSessionRow(QTreeWidgetItem *parent, const QJsonObject &item) {
    const bool terminal = isTerminal(item);
    const int open = item.value(QStringLiteral("open_requests")).toInt();
    auto *row = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
    row->setText(1, whenText(item.value(QStringLiteral("updated")).toDouble(), QDateTime::currentDateTime()));
    row->setText(2, QString::number(item.value(QStringLiteral("turns")).toInt())
                        + (open > 0 ? QStringLiteral(" · %1 open").arg(open) : QString()));
    const QString source = item.value(QStringLiteral("source")).toString();
    row->setText(3, terminal ? QStringLiteral("terminal")
                 : isGuestSource(source) ? guestLabel(source)
                                         : item.value(QStringLiteral("model")).toString());
    decorate(row, item);
    // An arrow to unfold the quick look: the row needs a child before it has one.
    auto *placeholder = new QTreeWidgetItem(row);
    placeholder->setData(0, kKindRole, QStringLiteral("preview"));
    placeholder->setData(0, kHtmlRole, QStringLiteral("Loading the quick look…"));
    spanFirstColumn(placeholder);
    placeholder->setFlags(Qt::ItemIsEnabled);
    return row;
}

// setFirstColumnSpanned() on a row that is already in the tree makes the view lay itself out then
// and there, and Qt re-measures the three "resize to contents" columns over every row while it is
// at it. Filling a hundred-row page row by row therefore cost about a hundred of those walks —
// most of the 100–190 ms a keystroke took (#MDSG). While the list is being filled the spans are
// collected instead and set in one go at the end, which lays the finished tree out once.
void SessionManager::spanFirstColumn(QTreeWidgetItem *row) {
    if (m_filling) m_spanRows.append(row);
    else row->setFirstColumnSpanned(true);
}

void SessionManager::decorate(QTreeWidgetItem *row, const QJsonObject &item) {
    const QString sessionId = item.value(QStringLiteral("session_id")).toString();
    const bool thread = isThread(item);
    row->setData(0, kIdRole, sessionId);
    row->setData(0, kItemRole, QString::fromUtf8(QJsonDocument(item).toJson(QJsonDocument::Compact)));
    row->setData(0, kKindRole, thread ? QStringLiteral("thread") : QStringLiteral("session"));

    QString title = item.value(QStringLiteral("title")).toString();
    if (title.isEmpty()) title = QStringLiteral("Untitled");
    if (isSignalThread(item)) {
        // The mark says whose thread it is, and the title is the signal's key (#AQ6X decision 9).
        // No `↳ a3 signal ·` prefix: it hangs under the project, not under a session, and the
        // agent id would be the only part of that prefix the reader could not act on.
        title = QStringLiteral("⚑ signal · %1").arg(title);
    } else if (thread) {
        const QString agent = item.value(QStringLiteral("agent_id")).toString();
        const QString type = item.value(QStringLiteral("agent_type")).toString();
        title = QStringLiteral("↳ %1%2 · %3").arg(agent, type.isEmpty() ? QString() : QLatin1Char(' ') + type, title);
    } else if (isTerminal(item)) {
        title = QStringLiteral("$ ") + title;
    }
    row->setText(0, title);            // the plain text a screen reader and the tests read
    row->setData(0, kTitleRole, title);

    // What the conversation was about, in one muted line: its summary, else how it opened.
    QString sub = item.value(QStringLiteral("summary")).toString().simplified();
    if (sub.isEmpty()) sub = item.value(QStringLiteral("first_prompt")).toString().simplified();
    if (sub.isEmpty()) sub = item.value(QStringLiteral("snippet")).toString().simplified();
    row->setData(0, kSubRole, thread ? QString() : sub);

    QString closedText;
    if (const auto it = m_closed.constFind(sessionId); it != m_closed.constEnd())
        closedText = closedAgo(it->second, QDateTime::currentMSecsSinceEpoch());
    row->setData(0, kBadgeRole, thread ? QStringList()
                                        : badges(item, m_openSessions.contains(sessionId), closedText,
                                                 m_liveUsage.value(sessionId)));

    QString tip = item.value(QStringLiteral("workspace")).toString();
    if (isSignalThread(item))
        tip = QStringLiteral("Relay started this thread itself on the failing check %1, which no "
                             "pane had claimed. Enter opens its history.\n%2")
                  .arg(item.value(QStringLiteral("title")).toString(), tip);
    else if (thread)
        tip = QStringLiteral("Subagent thread %1 (%2) of “%3”%4\n%5")
                  .arg(item.value(QStringLiteral("agent_id")).toString(), item.value(QStringLiteral("agent_type")).toString(),
                       item.value(QStringLiteral("owner_title")).toString(),
                       item.value(QStringLiteral("spawn_turn")).isDouble()
                           ? QStringLiteral(", started in turn %1").arg(item.value(QStringLiteral("spawn_turn")).toInt()) : QString(),
                       tip);
    const QJsonArray rowMatches = item.value(QStringLiteral("matches")).toArray();
    if (!rowMatches.isEmpty()) {
        const QJsonObject match = rowMatches.first().toObject();
        tip += QStringLiteral("\nturn %1 · %2").arg(match.value(QStringLiteral("turn")).toInt())
                   .arg(kindLabel(match.value(QStringLiteral("kind")).toString()));
    } else if (!sub.isEmpty()) {
        tip += QLatin1Char('\n') + sub;
    }
    row->setToolTip(0, tip);
    m_rows.insert(sessionId, row);
}

void SessionManager::rebuildTree(const QString &keep) {
    m_filling = true;
    // Qt measures a "resize to contents" column by walking every row of the tree, and unfolding a
    // row makes the view do it again — so restoring a hundred unfolded rows walked the list a
    // hundred times (#MDSG). The three narrow columns keep the width they have while the list is
    // filled and are measured once, at the end: the same measurement, over the finished tree.
    QHeaderView *header = m_tree->header();
    for (int column = 1; column < m_tree->columnCount(); ++column)
        header->setSectionResizeMode(column, QHeaderView::Interactive);
    // A refresh must leave the reader where they were: the same row selected, the same rows
    // unfolded and the list scrolled to the same place.
    QSet<QString> unfolded;
    std::function<void(QTreeWidgetItem *)> collect = [&](QTreeWidgetItem *row) {
        for (int i = 0; i < row->childCount(); ++i) {
            QTreeWidgetItem *child = row->child(i);
            if (child->isExpanded() && !child->data(0, kIdRole).toString().isEmpty())
                unfolded.insert(child->data(0, kIdRole).toString());
            collect(child);
        }
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *top = m_tree->topLevelItem(i);
        if (top->isExpanded() && !top->data(0, kIdRole).toString().isEmpty())
            unfolded.insert(top->data(0, kIdRole).toString());
        collect(top);
    }
    const int scroll = m_tree->verticalScrollBar()->value();
    m_tree->clear();
    m_rows.clear();

    QHash<QString, QTreeWidgetItem *> groups;
    QTreeWidgetItem *first = nullptr, *wanted = nullptr;
    const QDateTime now = QDateTime::currentDateTime();
    const QString grouping = m_group->currentData().toString();
    int matches = 0, sessions = 0, threads = 0;
    auto groupFor = [this, &groups](const QString &name) -> QTreeWidgetItem * {
        QTreeWidgetItem *group = groups.value(name);
        if (!group) {
            group = new QTreeWidgetItem(m_tree, {name.isEmpty() ? QStringLiteral("(no project)") : name});
            spanFirstColumn(group);
            group->setFlags(Qt::ItemIsEnabled);
            group->setExpanded(true);
            group->setData(0, kKindRole, QStringLiteral("group"));
            QFont font = group->font(0);
            font.setBold(true);
            group->setFont(0, font);
            groups.insert(name, group);
        }
        return group;
    };
    auto note = [&](QTreeWidgetItem *row, const QJsonObject &item) {
        if (!first) first = row;
        if (!keep.isEmpty() && item.value(QStringLiteral("session_id")).toString() == keep) wanted = row;
    };

    // "Continue": with nothing typed, what this project was in the middle of — pinned, unfinished
    // or just closed — above everything else. Grouped by project it is not repeated below (it would
    // sit two rows from itself); grouped by date it is, because a date group with a hole in it lies.
    QSet<QString> continued;
    if (m_search->text().trimmed().isEmpty()) {
        QSet<QString> closedIds;
        for (auto it = m_closed.cbegin(); it != m_closed.cend(); ++it) closedIds.insert(it.key());
        const QJsonArray top = continueItems(m_items, m_project, closedIds);
        if (!top.isEmpty()) {
            QTreeWidgetItem *group = groupFor(QStringLiteral("Continue"));
            for (const auto &value : top) {
                const QJsonObject item = value.toObject();
                note(addSessionRow(group, item), item);
                if (grouping == QLatin1String("project"))
                    continued.insert(item.value(QStringLiteral("session_id")).toString());
            }
        }
    }
    // Grouped by date the buckets read in their own order, not in the order the rows arrive.
    if (grouping == QLatin1String("date")) {
        QSet<QString> used;
        for (const auto &value : std::as_const(m_items)) {
            const QJsonObject item = value.toObject();
            if (!isThread(item)) used.insert(dateGroup(item.value(QStringLiteral("updated")).toDouble(), now));
        }
        for (const QString &name : dateGroupOrder()) if (used.contains(name)) groupFor(name);
    }

    for (const auto &value : std::as_const(m_items)) {
        const QJsonObject item = value.toObject();
        if (isThread(item)) continue;
        ++sessions;
        matches += item.value(QStringLiteral("match_count")).toInt();
        if (continued.contains(item.value(QStringLiteral("session_id")).toString())) continue;
        QTreeWidgetItem *parent = nullptr;
        if (grouping == QLatin1String("project")) parent = groupFor(item.value(QStringLiteral("project")).toString());
        else if (grouping == QLatin1String("date")) parent = groupFor(dateGroup(item.value(QStringLiteral("updated")).toDouble(), now));
        note(addSessionRow(parent, item), item);
    }
    auto rowsById = m_rows;   // the sessions placed so far: a thread hangs under its owner
    // A signal thread goes straight into its project group (#AQ6X decision 9, "those go into the
    // sessions manger"). Its owner is the board worker's own session, which nobody resumes and
    // which would otherwise draw a muted placeholder row above it saying nothing.
    for (const auto &value : std::as_const(m_items)) {
        const QJsonObject item = value.toObject();
        if (!isSignalThread(item)) continue;
        ++threads;
        matches += item.value(QStringLiteral("match_count")).toInt();
        QTreeWidgetItem *parent = grouping == QLatin1String("date")
                ? groupFor(dateGroup(item.value(QStringLiteral("updated")).toDouble(), now))
                : groupFor(item.value(QStringLiteral("project")).toString());
        note(addSessionRow(parent, item), item);
    }
    rowsById = m_rows;
    // Threads: under their parent thread, else under their owner session, else in the project
    // group with the owner named on the row. A thread whose parent is still to be placed waits a
    // round; once a round places nothing, the rest go under their owners.
    //
    // The user's own, so the unticked "Subagent threads" box drops them here — the request always
    // asks for threads, because a signal thread is listed whatever the box says.
    QList<QJsonObject> pending;
    if (m_threads->isChecked())
        for (const auto &value : std::as_const(m_items))
            if (isThread(value.toObject()) && !isSignalThread(value.toObject()))
                pending << value.toObject();
    // Under one owner, threads read in the order they were started (a1 before a2), like the
    // owner's history does; the sessions themselves stay newest first.
    std::stable_sort(pending.begin(), pending.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a.value(QStringLiteral("created")).toDouble() < b.value(QStringLiteral("created")).toDouble();
    });
    bool waitForParents = true;
    while (!pending.isEmpty()) {
        QList<QJsonObject> later;
        for (const QJsonObject &item : std::as_const(pending)) {
            const QString parentId = item.value(QStringLiteral("parent_thread")).toString();
            const QString ownerId = item.value(QStringLiteral("owner_session")).toString();
            QTreeWidgetItem *parent = !parentId.isEmpty() ? rowsById.value(parentId) : nullptr;
            const bool parentListed = !parentId.isEmpty() && std::any_of(pending.cbegin(), pending.cend(),
                [&parentId](const QJsonObject &other) { return other.value(QStringLiteral("session_id")).toString() == parentId; });
            if (!parent && parentListed && waitForParents) { later << item; continue; }
            if (!parent) parent = rowsById.value(ownerId);
            if (!parent && !ownerId.isEmpty()) {
                // The owner session is not among the results (it does not match the search, or is
                // on another page): it still heads its threads, as a muted row that resumes it.
                const QString ownerTitle = item.value(QStringLiteral("owner_title")).toString();
                const QJsonObject owner{{QStringLiteral("session_id"), ownerId}, {QStringLiteral("source"), QStringLiteral("agent")},
                                        {QStringLiteral("title"), ownerTitle.isEmpty() ? QStringLiteral("Session ") + ownerId.left(8) : ownerTitle},
                                        {QStringLiteral("session_dir"), item.value(QStringLiteral("session_dir"))},
                                        {QStringLiteral("workspace"), item.value(QStringLiteral("workspace"))},
                                        {QStringLiteral("project"), item.value(QStringLiteral("project"))},
                                        {QStringLiteral("owner_only"), true}};
                // Muted, not italic: one mark is enough and italic muted text is hard to read
                // (docs/ARCHITECTURE.md, "Legible text").
                parent = new QTreeWidgetItem(groupFor(item.value(QStringLiteral("project")).toString()),
                                             {owner.value(QStringLiteral("title")).toString(), QString(), QString(), QString()});
                parent->setForeground(0, m_tree->palette().color(QPalette::PlaceholderText));
                parent->setData(0, kIdRole, ownerId);
                parent->setData(0, kItemRole, QString::fromUtf8(QJsonDocument(owner).toJson(QJsonDocument::Compact)));
                parent->setData(0, kKindRole, QStringLiteral("session"));
                parent->setToolTip(0, QStringLiteral("Owner session of these threads (it does not match this search itself)"));
                rowsById.insert(ownerId, parent);
                m_rows.insert(ownerId, parent);
                if (!keep.isEmpty() && ownerId == keep) wanted = parent;
            }
            if (!parent) parent = groupFor(item.value(QStringLiteral("project")).toString());   // no owner recorded
            const QString status = item.value(QStringLiteral("status")).toString();
            auto *row = new QTreeWidgetItem(parent, {QString(),
                whenText(item.value(QStringLiteral("updated")).toDouble(), now),
                status.isEmpty() ? QStringLiteral("thread") : status,
                item.value(QStringLiteral("model")).toString()});
            row->setForeground(0, m_tree->palette().color(QPalette::PlaceholderText));
            parent->setExpanded(true);
            decorate(row, item);
            rowsById.insert(item.value(QStringLiteral("session_id")).toString(), row);
            note(row, item);
            matches += item.value(QStringLiteral("match_count")).toInt();
            ++threads;
        }
        // Nothing placed this round (a cycle, or parents that never arrive): stop waiting.
        waitForParents = later.size() < pending.size();
        pending = later;
    }

    // Every group row and every quick-look placeholder at once, now the tree is whole.
    for (QTreeWidgetItem *row : std::as_const(m_spanRows)) row->setFirstColumnSpanned(true);
    m_spanRows.clear();

    // Back where the reader was: the same rows unfolded (from what was already fetched), the same
    // row current, the same scroll offset.
    for (const QString &id : std::as_const(unfolded))
        for (QTreeWidgetItem *row : m_rows.values(id))
            if (row->data(0, kKindRole).toString() == QLatin1String("session")) row->setExpanded(true);
    m_matches = matches;
    m_sessions = sessions;
    m_threadCount = threads;
    m_filling = false;
    for (int column = 1; column < m_tree->columnCount(); ++column)
        header->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    if (QTreeWidgetItem *select = wanted ? wanted : first) m_tree->setCurrentItem(select);
    m_tree->verticalScrollBar()->setValue(std::min(scroll, m_tree->verticalScrollBar()->maximum()));
    updateStatus();
    updateEmptyState();
    updateButtons();
}

void SessionManager::updateStatus() {
    if (!m_note.isEmpty()) { m_status->setText(m_note); return; }
    const QString scope = scopeId() == QLatin1String("project") ? QStringLiteral("this project")
                                                                : QStringLiteral("all projects");
    // `m_threadCount` is what was *drawn*, signal threads included: they are listed whether or
    // not the box is ticked (#AQ6X), so a count that ignored them would disagree with the list.
    const QString counted = (m_threads->isChecked() || m_threadCount > 0)
        ? QStringLiteral("%1 session(s), %2 thread(s)").arg(m_sessions).arg(m_threadCount)
        : QStringLiteral("%1 session(s)").arg(m_sessions);
    QString text = m_search->text().trimmed().isEmpty()
        ? QStringLiteral("%1 in %2 · %3 ms").arg(counted, scope).arg(m_elapsed)
        : QStringLiteral("%1, %2 match(es) in %3 · %4 ms").arg(counted).arg(m_matches).arg(scope).arg(m_elapsed);
    // The "open" tag means a pane already holds it; resuming would load it twice, so Enter goes
    // to that pane instead. Say so rather than letting the key surprise anyone.
    if (m_openSessions.contains(selectedId()))
        text += QStringLiteral(" · already open: Enter goes to that pane");
    else if (m_closed.contains(selectedId()))
        text += QStringLiteral(" · Alt+Enter reopens it where it was");
    m_status->setText(text);
}

// The header's sort arrow for the sort the list is in. "relevance" belongs to no column, so it
// hides the arrow; every other sort points at its column, ascending or descending.
void SessionManager::updateSortIndicator() {
    QHeaderView *header = m_tree->header();
    const int column = headerSortColumn(m_sort->currentData().toString());
    header->setSortIndicatorShown(column >= 0);
    if (column >= 0) header->setSortIndicator(column, headerSortOrder(m_sort->currentData().toString()));
}

void SessionManager::updateEmptyState() {
    const bool empty = m_items.isEmpty();
    const bool searching = !m_search->text().trimmed().isEmpty();
    m_emptyRow->setVisible(empty);
    m_searchAll->setVisible(empty && scopeId() == QLatin1String("project"));
    m_clearFilters->setVisible(empty && anyFilter());
    if (!empty) return;
    m_header->clear();
    m_empty->setText(searching
        ? QStringLiteral("Nothing matches “%1”.").arg(m_search->text())
        : anyFilter() ? QStringLiteral("No conversation matches these filters.")
                      : QStringLiteral("No saved conversations here yet. One is saved as soon as an agent answers."));
    m_preview->setPlainText(searching
        ? QStringLiteral("No session%1 or terminal command matches “%2”.")
              .arg(m_threads->isChecked() ? QStringLiteral(", subagent thread") : QString(), m_search->text())
        : QStringLiteral("No saved sessions yet in this scope."));
}

QString SessionManager::selectedId() const {
    auto *item = m_tree->currentItem();
    return item ? item->data(0, kIdRole).toString() : QString();
}

QJsonObject SessionManager::selectedItem() const {
    auto *item = m_tree->currentItem();
    if (!item) return {};
    return QJsonDocument::fromJson(item->data(0, kItemRole).toString().toUtf8()).object();
}

void SessionManager::selectionChanged() {
    if (m_filling) return;
    // The rows an unfolded session holds are not conversations: walking through them leaves the
    // side preview and the buttons on the session itself.
    if (QTreeWidgetItem *current = m_tree->currentItem();
        current && current->data(0, kIdRole).toString().isEmpty()) return;
    if (!m_batchRunning) m_note.clear();          // a one-off message lasts until the next row
    updateButtons();
    updateStatus();
    const QString id = selectedId();
    if (id.isEmpty()) { m_header->clear(); m_preview->clear(); return; }
    const QJsonObject item = selectedItem();
    const QJsonArray matches = item.value(QStringLiteral("matches")).toArray();
    // Show the matching turns at once; the full preview arrives from the worker.
    QString html;
    for (const auto &value : matches) {
        const QJsonObject match = value.toObject();
        html += QStringLiteral("<p><b>turn %1 · %2</b><br>%3</p>")
                    .arg(match.value(QStringLiteral("turn")).toInt())
                    .arg(kindLabel(match.value(QStringLiteral("kind")).toString()),
                         highlighted(match.value(QStringLiteral("line")).toString(),
                                     match.value(QStringLiteral("ranges")).toArray()));
    }
    // The same row after a refresh: what the worker sent for it is still right, so it is shown
    // again rather than asked for again.
    const bool same = id == m_previewFor;
    m_preview->setHtml(same ? m_previewHtml : html.isEmpty() ? QStringLiteral("<p>Loading…</p>") : html);
    QString sub = item.value(QStringLiteral("workspace")).toString().toHtmlEscaped();
    if (isSignalThread(item))
        sub = QStringLiteral("Signal thread — Relay started it on a failing check nobody claimed"
                             "<br>%1").arg(sub);
    else if (isThread(item))
        sub = QStringLiteral("Subagent thread of “%1”<br>%2")
                  .arg(item.value(QStringLiteral("owner_title")).toString().toHtmlEscaped(), sub);
    m_header->setText(QStringLiteral("<b>%1</b><br>%2").arg(item.value(QStringLiteral("title")).toString().toHtmlEscaped(), sub));
    if (!same) requestPreview(id);
}

// One request per conversation: the side preview and an unfolded row are the same `conversation`
// reply, so selecting a row and unfolding it does not ask twice.
void SessionManager::requestPreview(const QString &sessionId) {
    if (sessionId.isEmpty() || !onPreview || m_previewPending == sessionId) return;
    m_previewPending = sessionId;
    onPreview(sessionId, m_search->text());
}

// → , Space or the arrow on a session row: the quick look, built from the reply's `overview`
// (protocol 14.4) and kept, so folding and unfolding it again costs nothing.
void SessionManager::unfold(QTreeWidgetItem *row) {
    if (!row || row->data(0, kKindRole).toString() != QLatin1String("session")) return;
    if (row->data(0, kLoadedRole).toBool()) return;
    const QString id = row->data(0, kIdRole).toString();
    if (m_overviews.contains(id)) { fillUnfolded(row, m_overviews.value(id)); return; }
    if (!m_filling) requestPreview(id);
}

void SessionManager::fillUnfolded(QTreeWidgetItem *row, const QJsonObject &overview) {
    if (!row) return;
    for (int i = row->childCount() - 1; i >= 0; --i)
        if (row->child(i)->data(0, kKindRole).toString() == QLatin1String("preview"))
            delete row->takeChild(i);
    const QJsonObject item = QJsonDocument::fromJson(row->data(0, kItemRole).toString().toUtf8()).object();
    const QString sessionId = row->data(0, kIdRole).toString();
    int at = 0;
    auto add = [&](const QString &html) {
        auto *line = new QTreeWidgetItem;
        line->setData(0, kKindRole, QStringLiteral("preview"));
        line->setData(0, kHtmlRole, html);
        line->setFirstColumnSpanned(true);
        line->setFlags(Qt::ItemIsEnabled);
        row->insertChild(at++, line);
        return line;
    };
    auto say = [&](const QString &label, const QString &body) {
        if (body.trimmed().isEmpty()) return;
        add(QStringLiteral("<b>%1</b> %2").arg(label.toHtmlEscaped(), escaped(body)));
    };
    // What matched comes first: it is why this row is in the list at all.
    const QJsonArray matches = item.value(QStringLiteral("matches")).toArray();
    if (!m_search->text().trimmed().isEmpty())
        for (const auto &value : matches) {
            const QJsonObject match = value.toObject();
            add(QStringLiteral("<b>turn %1 · %2</b> %3")
                    .arg(match.value(QStringLiteral("turn")).toInt())
                    .arg(kindLabel(match.value(QStringLiteral("kind")).toString()),
                         highlighted(match.value(QStringLiteral("line")).toString(),
                                     match.value(QStringLiteral("ranges")).toArray())));
        }
    const QString summary = overview.value(QStringLiteral("summary")).toString();
    if (!summary.trimmed().isEmpty()) {
        say(QStringLiteral("Summary"), summary);
    } else if (onSummarise && !isTerminal(item)) {
        // No summary yet: the button to write one sits where the summary would be.
        QTreeWidgetItem *line = add(QString());
        // Short: the row is indented three levels deep and a long label is clipped in a narrow pane.
        auto *button = new QPushButton(m_summarising.contains(sessionId) ? QStringLiteral("Summarising…")
                                                                        : QStringLiteral("Summarise"));
        button->setToolTip(QStringLiteral("Write a two-sentence summary of this conversation (a cheap model call)"));
        button->setObjectName(QStringLiteral("summariseRow"));
        button->setEnabled(!m_summarising.contains(sessionId));
        connect(button, &QPushButton::clicked, this, [this, item] { summarise(item); });
        // The row is a spanned cell with no text of its own: it has to be told how tall the
        // button is, and the button sits at the left rather than stretching across the row.
        auto *holder = new QWidget;
        auto *box = new QHBoxLayout(holder);
        box->setContentsMargins(0, 2, 0, 2);
        button->setMinimumWidth(button->sizeHint().width());
        box->addWidget(button);
        box->addStretch(1);
        line->setSizeHint(0, QSize(button->sizeHint().width() + 8, button->sizeHint().height() + 6));
        m_tree->setItemWidget(line, 0, holder);
    }
    say(QStringLiteral("First:"), overview.value(QStringLiteral("first_prompt")).toString());
    const QJsonArray turns = overview.value(QStringLiteral("last_turns")).toArray();
    for (const auto &value : turns) {
        const QJsonObject turn = value.toObject();
        say(QStringLiteral("You:"), turn.value(QStringLiteral("prompt")).toString());
        say(QStringLiteral("Agent:"), turn.value(QStringLiteral("reply")).toString());
    }
    const QJsonArray files = overview.value(QStringLiteral("files")).toArray();
    if (!files.isEmpty()) {
        const int total = overview.value(QStringLiteral("files_count")).toInt(files.size());
        QStringList shown;
        for (int i = 0; i < files.size() && i < 12; ++i)
            shown << elideMiddleText(files.at(i).toString(), 70).toHtmlEscaped();
        if (total > shown.size()) shown << QStringLiteral("and %1 more").arg(total - shown.size());
        add(QStringLiteral("<b>Files (%1)</b> %2").arg(total).arg(shown.join(QStringLiteral(" · "))));
    }
    const QJsonArray todos = overview.value(QStringLiteral("todos")).toArray();
    QStringList open;
    for (const auto &value : todos) {
        const QJsonObject todo = value.toObject();
        const QString status = todo.value(QStringLiteral("status")).toString();
        if (status == QLatin1String("completed") || status == QLatin1String("cancelled")) continue;
        open << escaped(todo.value(QStringLiteral("text")).toString());
    }
    if (!open.isEmpty())
        add(QStringLiteral("<b>Still to do</b> %1").arg(open.join(QStringLiteral(" · "))));
    if (at == 0) add(QStringLiteral("Nothing was indexed for this conversation yet."));
    row->setData(0, kLoadedRole, true);
}

void SessionManager::setPreview(const QJsonObject &event) {
    const QString id = event.value(QStringLiteral("session_id")).toString();
    if (!id.isEmpty()) {
        if (m_previewPending == id) m_previewPending.clear();
        if (event.contains(QStringLiteral("overview"))) {
            const QJsonObject overview = event.value(QStringLiteral("overview")).toObject();
            m_overviews.insert(id, overview);
            const auto rows = m_rows.values(id);
            for (QTreeWidgetItem *row : rows)
                if (row->isExpanded()) fillUnfolded(row, overview);
            const QString summary = overview.value(QStringLiteral("summary")).toString();
            if (!summary.isEmpty()) updateItemSummary(id, summary);
        }
    }
    if (id != selectedId()) return;
    const QJsonArray items = event.value(QStringLiteral("items")).toArray();
    const bool thread = event.value(QStringLiteral("source")).toString() == QLatin1String("subagent");
    QString html;
    int lastTurn = -1;
    for (const auto &value : items) {
        const QJsonObject entry = value.toObject();
        const int turn = entry.value(QStringLiteral("turn")).toInt();
        if (turn != lastTurn) {
            lastTurn = turn;
            html += thread ? QStringLiteral("<h4>run %1</h4>").arg(turn) : QStringLiteral("<h4>turn %1</h4>").arg(turn);
        }
        const QString kind = entry.value(QStringLiteral("kind")).toString();
        const QJsonArray ranges = entry.value(QStringLiteral("ranges")).toArray();
        QString body;
        if (!ranges.isEmpty())
            body = highlighted(entry.value(QStringLiteral("line")).toString(), ranges);
        else
            body = entry.value(QStringLiteral("text")).toString().left(1200).toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
        QString status;
        if (entry.contains(QStringLiteral("exit_status")))
            status = QStringLiteral(" · exit %1").arg(entry.value(QStringLiteral("exit_status")).toInt());
        html += QStringLiteral("<p><b>%1%2</b><br>%3</p>")
                    .arg(thread && kind == QLatin1String("prompt") ? QStringLiteral("Task") : thread && kind == QLatin1String("reply")
                             ? QStringLiteral("Subagent") : kindLabel(kind), status, body);
    }
    if (html.isEmpty()) html = QStringLiteral("<p>This conversation has no indexed turns.</p>");
    m_preview->setHtml(html);
    m_previewFor = id;
    m_previewHtml = html;
    const int found = event.value(QStringLiteral("match_count")).toInt();
    QString sub = event.value(QStringLiteral("workspace")).toString().toHtmlEscaped();
    if (thread)
        sub = QStringLiteral("Subagent thread of “%1”<br>%2").arg(event.value(QStringLiteral("owner_title")).toString().toHtmlEscaped(), sub);
    m_header->setText(QStringLiteral("<b>%1</b><br>%2%3")
                          .arg(event.value(QStringLiteral("title")).toString().toHtmlEscaped(), sub,
                               found > 0 ? QStringLiteral(" · %1 matching turn(s)").arg(found) : QString()));
}

void SessionManager::updateButtons() {
    const QJsonObject item = selectedItem();
    const bool has = !item.isEmpty();
    const bool terminal = isTerminal(item);
    const bool thread = isThread(item);
    // A guest session (protocol 26.7) is the guest's own file: Relay resumes it by running the
    // tool's command, and everything that reads a Relay session file — the ⓘ view, the summary —
    // has nothing to read.
    const bool guest = isGuestItem(item);
    m_resume->setEnabled(has && !terminal);
    m_resume->setText(thread ? QStringLiteral("Open history") : QStringLiteral("Resume here"));
    m_newPane->setEnabled(has && !terminal && !thread);
    m_info->setEnabled(has && !terminal && !guest);
    m_rename->setEnabled(has);
    m_pin->setEnabled(has);
    m_delete->setEnabled(has);
    m_pin->setText(item.value(QStringLiteral("pinned")).toInt() > 0 ? QStringLiteral("Unpin") : QStringLiteral("Pin"));
    m_resume->setToolTip(terminal ? QStringLiteral("Terminal history cannot be resumed; it is here to be searched.")
                         : thread ? QStringLiteral("Open this subagent thread's history, with the way back to its owner session.")
                         : guest ? QStringLiteral("Runs %1 in this pane, in %2 — %3 resumes its own session.")
                                       .arg(guestCommand(item),
                                            guestCwd(item).isEmpty() ? QStringLiteral("this pane's directory") : guestCwd(item),
                                            guestLabel(item.value(QStringLiteral("source")).toString()))
                         : m_openSessions.contains(selectedId())
                             ? QStringLiteral("This conversation is already open: Relay goes to that pane rather than loading it twice.")
                             : QStringLiteral("Replace this pane's conversation with the selected one."));
    const QString sessionId = item.value(QStringLiteral("session_id")).toString();
    m_reopen->setVisible(m_closed.contains(sessionId));
    const bool summarisable = has && !thread && !terminal && !guest && bool(onSummarise);
    const bool waiting = m_summarising.contains(sessionId);
    m_summarise->setVisible(summarisable && (waiting || item.value(QStringLiteral("summary")).toString().trimmed().isEmpty()));
    m_summarise->setEnabled(!waiting);
    m_summarise->setText(waiting ? QStringLiteral("Summarising…") : QStringLiteral("Summarise"));
}

void SessionManager::activate(bool newPane) {
    const QJsonObject item = selectedItem();
    if (item.isEmpty()) return;
    if (isTerminal(item)) {
        m_note = QStringLiteral("Terminal history cannot be resumed; use the preview.");
        updateStatus();
        return;
    }
    if (isThread(item)) {
        if (onOpenThread) onOpenThread(item);
        return;
    }
    if (onResume) onResume(item, newPane);
}

// Ctrl+Enter. The worker can only fork the conversation it is holding, so a saved one that nobody
// has loaded is opened in a new pane instead; whoever wires onFork says which it is.
void SessionManager::fork() {
    const QJsonObject item = selectedItem();
    if (item.isEmpty() || isTerminal(item) || isThread(item)) return;
    if (onFork) onFork(item);
    else activate(true);
}

// Alt+Enter: this conversation's pane, tab or window was closed — put it back where it was, with
// everything else that went with it, rather than resuming it alone here.
void SessionManager::reopenClosed() {
    const QString id = selectedId();
    const auto it = m_closed.constFind(id);
    if (it == m_closed.constEnd()) {
        m_note = QStringLiteral("That conversation was not closed from this window; Enter resumes it here.");
        updateStatus();
        return;
    }
    if (onReopenClosed) onReopenClosed(it->first);
}

void SessionManager::summariseSelected() { summarise(selectedItem()); }

void SessionManager::summarise(const QJsonObject &item) {
    const QString sessionId = item.value(QStringLiteral("session_id")).toString();
    if (sessionId.isEmpty() || !onSummarise || m_summarising.contains(sessionId)) return;
    m_summarising.insert(sessionId);
    onSummarise(sessionId, item.value(QStringLiteral("session_dir")).toString());
    // Both the button in the header and the one in the unfolded row say what is happening.
    for (QTreeWidgetItem *row : m_rows.values(sessionId)) {
        row->setData(0, kLoadedRole, false);
        if (row->isExpanded()) fillUnfolded(row, m_overviews.value(sessionId));
    }
    updateButtons();
}

// `conversation_summary`: one saved conversation, asked for by the button.
void SessionManager::setSummary(const QJsonObject &event) {
    const QString id = event.value(QStringLiteral("session_id")).toString();
    m_summarising.remove(id);
    const QString error = event.value(QStringLiteral("error")).toString();
    if (!error.isEmpty()) {
        m_note = QStringLiteral("Could not summarise: ") + error;
        for (QTreeWidgetItem *row : m_rows.values(id))
            if (row->isExpanded()) {
                row->setData(0, kLoadedRole, false);
                fillUnfolded(row, m_overviews.value(id));
                auto *line = new QTreeWidgetItem;
                line->setData(0, kKindRole, QStringLiteral("preview"));
                line->setData(0, kHtmlRole, QStringLiteral("<b>Summary failed</b> ") + error.toHtmlEscaped());
                line->setFirstColumnSpanned(true);
                line->setFlags(Qt::ItemIsEnabled);
                row->insertChild(0, line);
            }
        updateStatus();
        updateButtons();
        return;
    }
    updateItemSummary(id, event.value(QStringLiteral("summary")).toString());
    updateButtons();
}

// `session_summary`: the conversation a pane is holding summarised itself as it went.
void SessionManager::setSessionSummary(const QJsonObject &event) {
    updateItemSummary(event.value(QStringLiteral("session_id")).toString(),
                      event.value(QStringLiteral("summary")).toString());
}

// A row whose summary has just arrived reads the new one at once: no re-query, no lost place.
void SessionManager::updateItemSummary(const QString &sessionId, const QString &summary) {
    if (sessionId.isEmpty() || summary.trimmed().isEmpty()) return;
    for (int i = 0; i < m_items.size(); ++i) {
        QJsonObject item = m_items.at(i).toObject();
        if (item.value(QStringLiteral("session_id")).toString() != sessionId) continue;
        if (item.value(QStringLiteral("summary")).toString() == summary) return;
        item.insert(QStringLiteral("summary"), summary);
        m_items.replace(i, item);
    }
    if (m_overviews.contains(sessionId)) {
        QJsonObject overview = m_overviews.value(sessionId);
        overview.insert(QStringLiteral("summary"), summary);
        m_overviews.insert(sessionId, overview);
    }
    for (QTreeWidgetItem *row : m_rows.values(sessionId)) {
        if (row->data(0, kKindRole).toString() != QLatin1String("session")) continue;
        QJsonObject item = QJsonDocument::fromJson(row->data(0, kItemRole).toString().toUtf8()).object();
        item.insert(QStringLiteral("summary"), summary);
        decorate(row, item);
        if (row->isExpanded()) {
            row->setData(0, kLoadedRole, false);
            fillUnfolded(row, m_overviews.value(sessionId));
        }
    }
    updateButtons();
}

// "Summarise all…": what it would cost first, and a Start that has to be pressed.
void SessionManager::askEstimate() {
    if (!onSummariseEstimate) return;
    m_batchScope = scopeId();
    m_confirmText->setText(QStringLiteral("Working out what that would cost…"));
    if (auto *start = m_confirm->findChild<QPushButton *>(QStringLiteral("summariseStart"))) start->setEnabled(false);
    m_confirm->setVisible(true);
    onSummariseEstimate(m_batchScope);
}

void SessionManager::setSummariseEstimate(const QJsonObject &event) {
    const QString scope = event.value(QStringLiteral("scope")).toString();
    if (!scope.isEmpty()) m_batchScope = scope;
    m_confirmText->setText(estimateText(event));
    if (auto *start = m_confirm->findChild<QPushButton *>(QStringLiteral("summariseStart")))
        start->setEnabled(event.value(QStringLiteral("count")).toInt() > 0 && !m_batchRunning);
    m_confirm->setVisible(true);
}

void SessionManager::startBatch() {
    m_confirm->setVisible(false);
    if (!onSummariseAll || m_batchRunning) return;
    m_batchRunning = true;
    m_summariseAll->setEnabled(false);
    m_cancelBatch->setVisible(true);
    m_note = QStringLiteral("Summarising…");
    updateStatus();
    onSummariseAll(m_batchScope.isEmpty() ? scopeId() : m_batchScope);
}

void SessionManager::setSummariseProgress(const QJsonObject &event) {
    const QString id = event.value(QStringLiteral("session_id")).toString();
    if (!id.isEmpty()) {
        m_summarising.remove(id);
        updateItemSummary(id, event.value(QStringLiteral("summary")).toString());
    }
    const int done = event.value(QStringLiteral("done")).toInt();
    const int total = event.value(QStringLiteral("total")).toInt();
    if (!event.value(QStringLiteral("finished")).toBool()) {
        m_note = QStringLiteral("Summarising %1 of %2…").arg(done).arg(total);
        updateStatus();
        return;
    }
    m_batchRunning = false;
    m_summariseAll->setEnabled(true);
    m_cancelBatch->setVisible(false);
    const int failed = event.value(QStringLiteral("failed")).toInt();
    m_note.clear();
    m_status->setText(event.value(QStringLiteral("cancelled")).toBool()
        ? QStringLiteral("Stopped after %1 of %2 summaries.").arg(done).arg(total)
        : QStringLiteral("Summarised %1 conversation(s)%2.").arg(done)
              .arg(failed > 0 ? QStringLiteral(", %1 failed").arg(failed) : QString()));
    QTimer::singleShot(0, this, &SessionManager::requery);
}

void SessionManager::setProject(const QString &project) {
    if (m_project == project) return;
    m_project = project;
    if (!m_items.isEmpty()) rebuildTree(selectedId());
}

void SessionManager::setKnownProjects(const QList<QPair<QString, QString>> &projects) {
    if (m_knownProjects == projects) return;
    m_knownProjects = projects;
    // Rebuild the chooser around the two fixed rows, keeping what was chosen — a folder that is
    // no longer known stays selectable until the user drops it, as the facet menus do.
    const QString keep = m_projectFilter->currentData().toString();
    const QSignalBlocker quiet(m_projectFilter);
    m_projectFilter->clear();
    m_projectFilter->addItem(QStringLiteral("Any project"), QString());
    for (const auto &known : projects) {
        // Two projects called "src": the folder tells them apart.
        const bool ambiguous = std::count_if(projects.begin(), projects.end(),
                                             [&known](const QPair<QString, QString> &other) { return other.first == known.first; }) > 1;
        m_projectFilter->addItem(ambiguous ? QStringLiteral("%1  (%2)").arg(known.first, known.second) : known.first,
                                 known.second);
    }
    m_projectFilter->addItem(QStringLiteral("No project"), QStringLiteral("none"));
    if (!keep.isEmpty() && m_projectFilter->findData(keep) < 0) m_projectFilter->addItem(keep, keep);
    m_projectFilter->setCurrentIndex(std::max(0, m_projectFilter->findData(keep)));
    const bool changed = m_projectFilter->currentData().toString() != keep;
    if (changed) requery();
}

void SessionManager::setOpenSessions(const QStringList &sessionIds) {
    if (m_openSessions == sessionIds) return;
    m_openSessions = sessionIds;
    if (!m_items.isEmpty()) rebuildTree(selectedId());
}

// What each open conversation's pane is using (issue #D03W), pushed by the window's status
// poll: session id to a "cpu 12% · mem 3%" tag. Rows are patched in place rather than the
// tree rebuilt — the numbers move every poll, and a rebuild would lose an unfold.
void SessionManager::setLiveUsage(const QHash<QString, QString> &usage) {
    if (m_liveUsage == usage) return;
    if (m_tree) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        bool redraw = false;
        // The union, not just the new keys: a pane that went idle drops out of `usage`, and its
        // row has to lose the tag as surely as a busy pane's row gains one.
        QSet<QString> touched;
        for (auto it = usage.cbegin(); it != usage.cend(); ++it) touched.insert(it.key());
        for (auto it = m_liveUsage.cbegin(); it != m_liveUsage.cend(); ++it) touched.insert(it.key());
        for (const QString &sessionId : std::as_const(touched)) {
            for (QTreeWidgetItem *row : m_rows.values(sessionId)) {
                if (!row || row->data(0, kKindRole).toString() != QLatin1String("session")) continue;
                const QJsonObject item =
                    QJsonDocument::fromJson(row->data(0, kItemRole).toString().toUtf8()).object();
                const auto closed = m_closed.constFind(sessionId);
                const QString closedText = closed != m_closed.constEnd()
                                               ? closedAgo(closed->second, now) : QString();
                const QStringList tags = badges(item, m_openSessions.contains(sessionId), closedText,
                                                usage.value(sessionId));
                if (row->data(0, kBadgeRole).toStringList() == tags) continue;
                row->setData(0, kBadgeRole, tags);
                redraw = true;
            }
        }
        if (redraw) m_tree->viewport()->update();
    }
    m_liveUsage = usage;
}

void SessionManager::setClosedSessions(const QHash<QString, QPair<QString, qint64>> &closed) {
    if (m_closed == closed) return;
    m_closed = closed;
    // Reopened or pushed off the end of the 25: the tag goes and "Reopen where it was" goes with
    // it. Nothing needs asking again — the rows are redrawn from what is already here.
    if (m_closed.isEmpty()) m_ages->stop();
    else if (!m_ages->isActive()) m_ages->start();
    if (!m_items.isEmpty()) rebuildTree(selectedId());
    else updateButtons();
}

// One pass over the rows of conversations that are in the closed list, re-reading the clock. The
// whole tag list is rebuilt rather than the words patched, so "open" still wins over "closed …"
// for a conversation someone has meanwhile resumed in a pane.
void SessionManager::refreshClosedAges() {
    if (m_closed.isEmpty() || !m_tree) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool redraw = false;
    for (auto it = m_closed.cbegin(); it != m_closed.cend(); ++it) {
        const QString text = closedAgo(it->second, now);
        const bool open = m_openSessions.contains(it.key());
        for (QTreeWidgetItem *row : m_rows.values(it.key())) {
            if (!row || row->data(0, kKindRole).toString() != QLatin1String("session")) continue;
            const QJsonObject item =
                QJsonDocument::fromJson(row->data(0, kItemRole).toString().toUtf8()).object();
            const QStringList tags = badges(item, open, text);
            if (row->data(0, kBadgeRole).toStringList() == tags) continue;
            row->setData(0, kBadgeRole, tags);
            redraw = true;
        }
    }
    if (redraw) m_tree->viewport()->update();
}

// What the box understands, in the box's own words. A popup rather than a dialog: it is a reminder,
// not a decision.
void SessionManager::showOperatorHelp() {
    auto *popup = new QFrame(this, Qt::Popup);
    popup->setObjectName(QStringLiteral("operatorHelp"));
    popup->setFrameShape(QFrame::StyledPanel);
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setAutoFillBackground(true);
    auto *text = new QLabel(QStringLiteral(
        "<b>Words</b> match anywhere in a conversation; <b>\"a phrase\"</b> matches together.<br>"
        "<b>-word</b> leaves out conversations that hold it.<br>"
        "<b>project:</b>name · <b>file:</b>part-of-a-path · <b>model:</b>name · <b>branch:</b>name<br>"
        "<b>after:</b>2026-09-01 · <b>before:</b>today · <b>after:</b>7d<br>"
        "<b>has:</b>tasks|edits|summary · <b>is:</b>pinned|unfinished · <b>in:</b>terminal|agent<br>"
        "Naming a project searches every project."));
    text->setTextFormat(Qt::RichText);
    auto *box = new QVBoxLayout(popup);
    box->setContentsMargins(10, 8, 10, 8);
    box->addWidget(text);
    popup->adjustSize();
    popup->move(m_help->mapToGlobal(QPoint(m_help->width() - popup->width(), m_help->height() + 2)));
    popup->show();
}

void SessionManager::rename() {
    const QJsonObject item = selectedItem();
    if (item.isEmpty() || !onRename) return;
    bool ok = false;
    const QString title = QInputDialog::getText(this, QStringLiteral("Rename"),
                                                QStringLiteral("Title (empty restores the generated one):"),
                                                QLineEdit::Normal, item.value(QStringLiteral("title")).toString(), &ok);
    if (!ok) return;
    m_pendingSelect = item.value(QStringLiteral("session_id")).toString();
    onRename(m_pendingSelect, title);
}

void SessionManager::togglePin() {
    const QJsonObject item = selectedItem();
    if (item.isEmpty() || !onPin) return;
    m_pendingSelect = item.value(QStringLiteral("session_id")).toString();
    onPin(m_pendingSelect, item.value(QStringLiteral("pinned")).toInt() == 0);
}

void SessionManager::remove() {
    const QJsonObject item = selectedItem();
    if (item.isEmpty() || !onDelete) return;
    const bool terminal = isTerminal(item);
    const bool thread = isThread(item);
    const bool guest = isGuestItem(item);
    QMessageBox confirm(QMessageBox::Warning, QStringLiteral("Delete"),
                        terminal ? QStringLiteral("Delete the indexed terminal history of “%1”?")
                                       .arg(item.value(QStringLiteral("project")).toString())
                        : thread ? QStringLiteral("Delete the subagent thread “%1”?").arg(item.value(QStringLiteral("title")).toString())
                        : guest ? QStringLiteral("Forget Relay's copy of “%1”?").arg(item.value(QStringLiteral("title")).toString())
                                 : QStringLiteral("Delete “%1” permanently?").arg(item.value(QStringLiteral("title")).toString()),
                        QMessageBox::Cancel, this);
    confirm.setInformativeText(terminal
        ? QStringLiteral("Only the index rows are removed; your shell's own history file is untouched.")
        : thread ? QStringLiteral("The thread's file and its index rows are deleted; its owner session keeps the result it was given.")
        // Relay never writes in ~/.claude or ~/.codex (protocol 26.7), so this says what it does:
        // the row goes, the transcript stays, and the next scan lists the session again.
        : guest ? QStringLiteral("%1 owns this session; Relay only indexed it. The index rows go and the transcript "
                                 "stays where it is, so the session is listed again the next time Relay scans.")
                      .arg(guestLabel(item.value(QStringLiteral("source")).toString()))
                 : QStringLiteral("The session, its subagent threads, its checkpoint file copies and its index rows are deleted. This cannot be undone."));
    auto *remove = confirm.addButton(QStringLiteral("Delete"), QMessageBox::DestructiveRole);
    confirm.exec();
    if (confirm.clickedButton() != remove) return;
    onDelete(item.value(QStringLiteral("session_id")).toString());
}

bool SessionManager::eventFilter(QObject *object, QEvent *event) {
    if (event->type() != QEvent::KeyPress) return QWidget::eventFilter(object, event);
    if (object != m_search && object != m_tree) return QWidget::eventFilter(object, event);
    auto *key = static_cast<QKeyEvent *>(event);
    const bool plain = !(key->modifiers() & ~Qt::KeypadModifier);
    if (key->key() == Qt::Key_Escape) {
        // The query first, the pane second: Esc should never lose a list you are still reading.
        if (!m_search->text().isEmpty()) { m_search->clear(); m_search->setFocus(); return true; }
        if (onClose) onClose();
        return true;
    }
    if (key->key() == Qt::Key_I && key->modifiers() == Qt::ControlModifier) { m_info->click(); return true; }
    if (key->key() == Qt::Key_F && key->modifiers() == Qt::ControlModifier) { focusSearch(); return true; }
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
        if (key->modifiers() & Qt::ControlModifier) fork();
        else if (key->modifiers() & Qt::AltModifier) reopenClosed();
        else activate(key->modifiers() & Qt::ShiftModifier);
        return true;
    }
    if (key->key() == Qt::Key_P && key->modifiers() == Qt::ControlModifier) { togglePin(); return true; }
    if (key->key() == Qt::Key_F2 && plain) { rename(); return true; }

    if (object == m_search) {
        switch (key->key()) {
        case Qt::Key_Down:
        case Qt::Key_Up:
        case Qt::Key_PageDown:
        case Qt::Key_PageUp:
            QApplication::sendEvent(m_tree, key);
            return true;
        case Qt::Key_Right:
            // At the end of what was typed there is nothing left to walk through, so → unfolds the
            // row the list is on, the same as it does with the keyboard in the list.
            if (plain && !m_search->hasSelectedText() && m_search->cursorPosition() == m_search->text().size()) {
                if (QTreeWidgetItem *row = m_tree->currentItem()) row->setExpanded(true);
                return true;
            }
            break;
        case Qt::Key_Left:
            if (plain && !m_search->hasSelectedText() && m_search->cursorPosition() == 0) {
                if (QTreeWidgetItem *row = m_tree->currentItem()) row->setExpanded(false);
                return true;
            }
            break;
        default:
            break;
        }
        return QWidget::eventFilter(object, event);
    }

    // On the rows: Space unfolds, Delete deletes, / goes to the box, and anything else you type is
    // the start of a search — the same keyboard the "Recently closed" list has.
    if (key->key() == Qt::Key_Space && plain) {
        if (QTreeWidgetItem *row = m_tree->currentItem()) row->setExpanded(!row->isExpanded());
        return true;
    }
    if (key->key() == Qt::Key_Delete && plain) { remove(); return true; }
    if (plain) {
        const QString text = key->text();
        if (!text.isEmpty() && text.at(0).isPrint()) {
            m_search->setFocus(Qt::OtherFocusReason);
            if (text != QLatin1String("/")) m_search->insert(text);   // "/" only moves the keyboard
            return true;
        }
    }
    return QWidget::eventFilter(object, event);
}

// ----- find bar ------------------------------------------------------------------------------

FindBar::FindBar(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("findBar"));
    m_field = new QLineEdit;
    m_field->setPlaceholderText(QStringLiteral("Find in this pane"));
    m_field->setClearButtonEnabled(true);
    m_label = new QLabel;
    m_label->setObjectName(QStringLiteral("findCount"));
    m_previous = new QToolButton; m_previous->setText(QStringLiteral("↑")); m_previous->setAutoRaise(true);
    m_previous->setToolTip(QStringLiteral("Previous match (Shift+Enter)"));
    m_next = new QToolButton; m_next->setText(QStringLiteral("↓")); m_next->setAutoRaise(true);
    m_next->setToolTip(QStringLiteral("Next match (Enter)"));
    m_inConversation = new QPushButton(QStringLiteral("In conversation"));
    m_inConversation->setFlat(true);
    m_inConversation->setVisible(false);
    m_close = new QToolButton; m_close->setText(QStringLiteral("×")); m_close->setAutoRaise(true);
    m_close->setToolTip(QStringLiteral("Close (Esc)"));

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(8, 4, 8, 4);
    row->setSpacing(6);
    row->addWidget(new QLabel(QStringLiteral("Find")));
    row->addWidget(m_field, 1);
    row->addWidget(m_label);
    row->addWidget(m_previous);
    row->addWidget(m_next);
    row->addWidget(m_inConversation);
    row->addWidget(m_close);

    m_field->installEventFilter(this);
    connect(m_field, &QLineEdit::textChanged, this, &FindBar::refresh);
    connect(m_next, &QToolButton::clicked, this, [this] { step(false); });
    connect(m_previous, &QToolButton::clicked, this, [this] { step(true); });
    connect(m_close, &QToolButton::clicked, this, [this] { hide(); if (onClosed) onClosed(); });
    connect(m_inConversation, &QPushButton::clicked, this,
            [this] { if (onOpenConversation) onOpenConversation(m_field->text()); });
}

QString FindBar::text() const { return m_field->text(); }

void FindBar::setTerminalSearchable(bool can) { m_canSearchTerminal = can; updateLabel(); }

void FindBar::start(const QString &preset) {
    // Setting the text already re-runs the search through textChanged; do not ask twice.
    const bool changed = !preset.isEmpty() && preset != m_field->text();
    if (changed) m_field->setText(preset);
    show();
    m_field->setFocus();
    m_field->selectAll();
    if (!changed) refresh();
}

void FindBar::refresh() {
    m_conversationMatches = -1;
    const QString needle = m_field->text();
    m_terminalMatches = (needle.isEmpty() || !onFind || !m_canSearchTerminal) ? 0 : onFind(needle, false);
    if (!needle.isEmpty() && onCountConversation) onCountConversation(needle);
    updateLabel();
}

void FindBar::step(bool backwards) {
    if (m_field->text().isEmpty() || !onFind || !m_canSearchTerminal) return;
    m_terminalMatches = onFind(m_field->text(), backwards);
    updateLabel();
}

void FindBar::setConversationMatches(int matches) {
    m_conversationMatches = matches;
    updateLabel();
}

void FindBar::updateLabel() {
    if (m_field->text().isEmpty()) { m_label->setText(QString()); m_inConversation->setVisible(false); return; }
    QStringList parts;
    parts << (m_canSearchTerminal ? QStringLiteral("%1 in terminal").arg(m_terminalMatches)
                                  : QStringLiteral("terminal search needs the Relay engine"));
    if (m_conversationMatches >= 0) parts << QStringLiteral("%1 in conversation").arg(m_conversationMatches);
    m_label->setText(parts.join(QStringLiteral(" · ")));
    m_inConversation->setVisible(m_conversationMatches > 0);
}

void FindBar::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) { hide(); if (onClosed) onClosed(); return; }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        step(event->modifiers() & Qt::ShiftModifier);
        return;
    }
    QWidget::keyPressEvent(event);
}

bool FindBar::eventFilter(QObject *object, QEvent *event) {
    if (object != m_field || event->type() != QEvent::KeyPress) return QWidget::eventFilter(object, event);
    auto *key = static_cast<QKeyEvent *>(event);
    if (key->key() == Qt::Key_Escape) { hide(); if (onClosed) onClosed(); return true; }
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
        step(key->modifiers() & Qt::ShiftModifier);
        return true;
    }
    return QWidget::eventFilter(object, event);
}

}  // namespace relay::conversations
