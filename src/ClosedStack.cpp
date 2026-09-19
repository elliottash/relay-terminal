// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ClosedStack.h"

#include "WindowState.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonValue>

#include <algorithm>
#include <functional>

namespace relay {
namespace closed {
namespace {

const QString kVersion = QStringLiteral("version");
const QString kSaved = QStringLiteral("saved");
const QString kClosed = QStringLiteral("closed");
const QString kPane = QStringLiteral("pane");
const QString kSplit = QStringLiteral("split");
const QString kChildren = QStringLiteral("children");
const QString kSession = QStringLiteral("session_id");

QString kindKey(Record::Kind kind) {
    switch (kind) {
    case Record::Pane: return QStringLiteral("pane");
    case Record::Tab: return QStringLiteral("tab");
    case Record::Window: return QStringLiteral("window");
    }
    return {};
}

// Every leaf of a node, in layout order. `visit` gets the leaf's kind ("pane", "explorer", …) and
// its body.
void walk(const QJsonObject &node, const std::function<void(const QString &, const QJsonObject &)> &visit, int depth = 0) {
    if (depth > windowstate::kMaxDepth || node.isEmpty()) return;
    if (node.contains(kSplit)) {
        for (const auto &child : node.value(kChildren).toArray())
            if (child.isObject()) walk(child.toObject(), visit, depth + 1);
        return;
    }
    for (auto it = node.constBegin(); it != node.constEnd(); ++it)
        if (it.value().isObject()) { visit(it.key(), it.value().toObject()); return; }
}

// The same node with `edit` applied to every pane body.
QJsonObject mapPanes(const QJsonObject &node, const std::function<QJsonObject(QJsonObject)> &edit, int depth = 0) {
    if (depth > windowstate::kMaxDepth) return node;
    QJsonObject out = node;
    // An attached tab is the {"project", "node"} wrapper (#JN7X): edit the panes inside it and
    // give the wrapper back, so the reopened tab still knows its project.
    if (depth == 0 && node.value(QStringLiteral("node")).isObject()) {
        out.insert(QStringLiteral("node"), mapPanes(windowstate::tabNode(node), edit, depth + 1));
        return out;
    }
    if (node.contains(kSplit)) {
        QJsonArray children;
        for (const auto &child : node.value(kChildren).toArray())
            children.append(child.isObject() ? QJsonValue(mapPanes(child.toObject(), edit, depth + 1)) : child);
        out.insert(kChildren, children);
    } else if (node.value(kPane).isObject()) {
        out.insert(kPane, edit(node.value(kPane).toObject()));
    }
    return out;
}

// The tabs of a record as plain layout nodes. A tab attached to a project is saved as the
// {"project", "node"} wrapper (#JN7X), so it is unwrapped here — this is the one funnel walk()'s
// callers go through, and a wrapper reaching walk() would report its panes as one leaf of kind
// "node" instead.
QList<QJsonObject> tabNodes(const Record &record) {
    QList<QJsonObject> nodes;
    if (record.kind == Record::Window) {
        for (const auto &tab : record.tabs) nodes.append(windowstate::tabNode(tab.toObject()));
    } else {
        nodes.append(windowstate::tabNode(record.layout));
    }
    return nodes;
}

QString plural(int count, const QString &one, const QString &many) {
    return QStringLiteral("%1 %2").arg(count).arg(count == 1 ? one : many);
}

}  // namespace

QString defaultPath() {
    const QString layout = windowstate::defaultPath();
    return layout.isEmpty() ? QString() : QFileInfo(layout).absolutePath() + QStringLiteral("/closed.json");
}

QJsonObject toJson(const Record &record) {
    QJsonObject out{{"kind", kindKey(record.kind)}, {"id", record.id}, {"closed", double(record.closedAt)}};
    if (record.kind == Record::Pane) {
        out.insert(QStringLiteral("orientation"), record.orientation == Qt::Vertical ? "v" : "h");
        out.insert(QStringLiteral("before"), record.before);
        if (!record.sizes.isEmpty() && record.slot >= 0) {
            QJsonArray sizes;
            for (int size : record.sizes) sizes.append(size);
            out.insert(QStringLiteral("sizes"), sizes);
            out.insert(QStringLiteral("slot"), record.slot);
        }
    }
    if (record.kind == Record::Window) {
        out.insert(QStringLiteral("tabs"), record.tabs);
        out.insert(QStringLiteral("index"), record.index);
        if (record.geometry.isValid())
            out.insert(QStringLiteral("geometry"), QJsonArray{record.geometry.x(), record.geometry.y(),
                                                              record.geometry.width(), record.geometry.height()});
    } else {
        out.insert(QStringLiteral("layout"), record.layout);
        if (record.kind == Record::Tab) out.insert(QStringLiteral("index"), record.index);
    }
    if (std::any_of(record.tabNames.cbegin(), record.tabNames.cend(), [](const QString &name) { return !name.isEmpty(); }))
        out.insert(QStringLiteral("tab_names"), QJsonArray::fromStringList(record.tabNames));
    if (!record.titles.isEmpty()) out.insert(QStringLiteral("titles"), QJsonArray::fromStringList(record.titles));
    return out;
}

bool fromJson(const QJsonObject &object, Record *record) {
    if (!record) return false;
    Record out;
    const QString kind = object.value(QStringLiteral("kind")).toString();
    if (kind == QStringLiteral("pane")) out.kind = Record::Pane;
    else if (kind == QStringLiteral("tab")) out.kind = Record::Tab;
    else if (kind == QStringLiteral("window")) out.kind = Record::Window;
    else return false;
    out.id = object.value(QStringLiteral("id")).toString();
    // The id is only ever compared, but it comes from a file: keep it to what Relay writes.
    if (!windowstate::isScrollbackId(out.id)) return false;
    out.closedAt = qint64(object.value(QStringLiteral("closed")).toDouble());
    out.orientation = object.value(QStringLiteral("orientation")).toString() == QStringLiteral("v") ? Qt::Vertical : Qt::Horizontal;
    out.before = object.value(QStringLiteral("before")).toBool();
    out.index = std::max(0, object.value(QStringLiteral("index")).toInt());
    for (const auto &size : object.value(QStringLiteral("sizes")).toArray()) out.sizes.append(std::max(0, size.toInt()));
    out.slot = object.contains(QStringLiteral("slot")) ? object.value(QStringLiteral("slot")).toInt(-1) : -1;
    if (out.slot >= out.sizes.size()) { out.sizes.clear(); out.slot = -1; }
    QStringList names;
    for (const auto &name : object.value(QStringLiteral("tab_names")).toArray()) names.append(name.toString());
    for (const auto &title : object.value(QStringLiteral("titles")).toArray()) out.titles.append(title.toString());
    if (out.kind == Record::Window) {
        const QJsonArray tabs = object.value(QStringLiteral("tabs")).toArray();
        for (int i = 0; i < tabs.size(); ++i) {
            if (!tabs.at(i).isObject() || !windowstate::isUsableNode(tabs.at(i).toObject())) continue;
            out.tabs.append(tabs.at(i));
            out.tabNames.append(names.value(i));
        }
        if (out.tabs.isEmpty()) return false;
        out.index = std::min(out.index, int(out.tabs.size()) - 1);
        const QJsonArray g = object.value(QStringLiteral("geometry")).toArray();
        if (g.size() == 4) out.geometry = QRect(g.at(0).toInt(), g.at(1).toInt(), g.at(2).toInt(), g.at(3).toInt());
        // Titles are per leaf; once a tab is dropped they no longer line up.
        if (out.tabs.size() != tabs.size()) out.titles.clear();
    } else {
        out.layout = object.value(QStringLiteral("layout")).toObject();
        if (!windowstate::isUsableNode(out.layout)) return false;
        if (out.kind == Record::Tab) out.tabNames = names.mid(0, 1);
    }
    *record = out;
    return true;
}

QJsonObject document(const QList<Record> &records, qint64 savedAt) {
    QJsonArray closed;
    for (const Record &record : records) closed.append(toJson(record));
    return {{kVersion, kSchemaVersion},
            {kSaved, double(savedAt > 0 ? savedAt : QDateTime::currentSecsSinceEpoch())},
            {kClosed, closed}};
}

QList<Record> load(const QString &path, QString *error) {
    if (error) error->clear();
    auto fail = [error](const QString &text) {
        if (error) *error = text;
        return QList<Record>();
    };
    if (path.isEmpty()) return {};
    QFile file(path);
    if (!file.exists()) return {};
    if (!file.open(QIODevice::ReadOnly)) return fail(file.errorString());
    QJsonParseError parse{};
    const QJsonDocument parsed = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError) return fail(parse.errorString());
    if (!parsed.isObject()) return fail(QStringLiteral("The list of closed items is not a JSON object."));
    const QJsonObject state = parsed.object();
    if (state.value(kVersion).toInt(-1) != kSchemaVersion)
        return fail(QStringLiteral("The list of closed items is version %1; this Relay writes version %2.")
                        .arg(state.value(kVersion).toInt(-1))
                        .arg(kSchemaVersion));
    QList<Record> records;
    for (const auto &value : state.value(kClosed).toArray()) {
        Record record;
        if (value.isObject() && fromJson(value.toObject(), &record)) records.append(record);
    }
    if (records.size() > kMaxItems) records = records.mid(records.size() - kMaxItems);
    return records;
}

bool save(const QString &path, const QList<Record> &records, QString *error) {
    if (error) error->clear();
    if (path.isEmpty()) {
        if (error) *error = QStringLiteral("No data location for the list of closed items.");
        return false;
    }
    if (records.isEmpty()) {
        if (QFile::exists(path) && !QFile::remove(path)) {
            if (error) *error = QStringLiteral("Could not remove %1.").arg(path);
            return false;
        }
        return true;
    }
    return windowstate::write(path, document(records), error);
}

QList<Record> push(QList<Record> *records, Record record, int maxItems) {
    QList<Record> dropped;
    if (!records) return dropped;
    records->append(std::move(record));
    while (records->size() > std::max(1, maxItems)) dropped.append(records->takeFirst());
    return dropped;
}

QStringList scrollbackIds(const QList<Record> &records) {
    // One window record per closed item, so the layout's own walk finds the ids.
    QJsonArray windows;
    for (const Record &record : records) {
        QJsonArray tabs;
        for (const QJsonObject &node : tabNodes(record)) tabs.append(node);
        windows.append(windowstate::windowRecord(QRect(), QString(), tabs, 0));
    }
    return windowstate::scrollbackIds(windows);
}

QStringList sessionIds(const Record &record) {
    QStringList ids;
    for (const QJsonObject &node : tabNodes(record))
        walk(node, [&ids](const QString &kind, const QJsonObject &body) {
            const QString id = body.value(kSession).toString();
            if (kind == kPane && !id.isEmpty() && !ids.contains(id)) ids.append(id);
        });
    return ids;
}

Record withoutSessions(const Record &record, const QStringList &open, int *removed) {
    int count = 0;
    auto edit = [&open, &count](QJsonObject pane) {
        if (open.contains(pane.value(kSession).toString())) { pane.remove(kSession); ++count; }
        return pane;
    };
    Record out = record;
    if (record.kind == Record::Window) {
        QJsonArray tabs;
        for (const auto &tab : record.tabs) tabs.append(mapPanes(tab.toObject(), edit));
        out.tabs = tabs;
    } else {
        out.layout = mapPanes(record.layout, edit);
    }
    if (removed) *removed = count;
    return out;
}

QList<TabInfo> contents(const Record &record) {
    QList<TabInfo> tabs;
    int leaf = 0;
    const QList<QJsonObject> nodes = tabNodes(record);
    for (int i = 0; i < nodes.size(); ++i) {
        TabInfo tab;
        tab.name = record.tabNames.value(i);
        walk(nodes.at(i), [&](const QString &kind, const QJsonObject &body) {
            PaneInfo info;
            info.kind = kind;
            info.title = record.titles.value(leaf++);
            info.cwd = kind == kPane ? body.value(QStringLiteral("cwd")).toString() : body.value(QStringLiteral("path")).toString();
            if (info.cwd.isEmpty()) info.cwd = body.value(QStringLiteral("workspace")).toString();
            if (kind == kPane) info.sessionId = body.value(kSession).toString();
            if (kind == kPane) info.scrollback = body.value(QStringLiteral("scrollback")).toString();
            tab.panes.append(info);
        });
        tabs.append(tab);
    }
    return tabs;
}

bool matches(const Record &record, const QString &needle) {
    const QStringList words = needle.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.isEmpty()) return true;
    QStringList hay{label(record), kindName(record.kind)};
    for (const TabInfo &tab : contents(record)) {
        hay << tab.name;
        for (const PaneInfo &pane : tab.panes) hay << pane.title << pane.cwd;
    }
    const QString text = hay.join(QLatin1Char('\n'));
    return std::all_of(words.cbegin(), words.cend(), [&text](const QString &word) { return text.contains(word, Qt::CaseInsensitive); });
}

QString kindName(Record::Kind kind) {
    switch (kind) {
    case Record::Pane: return QStringLiteral("Pane");
    case Record::Tab: return QStringLiteral("Tab");
    case Record::Window: return QStringLiteral("Window");
    }
    return {};
}

QString label(const Record &record) {
    const QList<TabInfo> tabs = contents(record);
    int panes = 0;
    for (const TabInfo &tab : tabs) panes += tab.panes.size();
    auto nameOf = [](const PaneInfo &pane) {
        if (!pane.title.isEmpty()) return pane.title;
        const QString dir = QDir(pane.cwd).dirName();
        return dir.isEmpty() ? pane.cwd : dir;
    };
    if (record.kind == Record::Window)
        return plural(tabs.size(), QStringLiteral("tab"), QStringLiteral("tabs")) + QStringLiteral(" · ")
               + plural(panes, QStringLiteral("pane"), QStringLiteral("panes"));
    const TabInfo tab = tabs.value(0);
    if (record.kind == Record::Pane) return tab.panes.isEmpty() ? QString() : nameOf(tab.panes.first());
    QString text = tab.name;
    if (text.isEmpty()) {
        QStringList names;
        for (const PaneInfo &pane : tab.panes) {
            const QString name = nameOf(pane);
            if (!name.isEmpty() && !names.contains(name)) names.append(name);
        }
        text = names.join(QStringLiteral("; "));
    }
    if (panes > 1) text += QStringLiteral(" · ") + plural(panes, QStringLiteral("pane"), QStringLiteral("panes"));
    return text;
}

QString place(const Record &record, const QString &home) {
    for (const TabInfo &tab : contents(record))
        for (const PaneInfo &pane : tab.panes) {
            if (pane.cwd.isEmpty()) continue;
            if (!home.isEmpty() && pane.cwd == home) return QStringLiteral("~");
            if (!home.isEmpty() && pane.cwd.startsWith(home + QLatin1Char('/'))) return QStringLiteral("~") + pane.cwd.mid(home.size());
            return pane.cwd;
        }
    return {};
}

QString age(qint64 closedAt, qint64 now) {
    if (closedAt <= 0) return {};
    const qint64 seconds = std::max<qint64>(0, (now - closedAt) / 1000);
    if (seconds < 60) return QStringLiteral("just now");
    if (seconds < 3600) return QStringLiteral("%1 min ago").arg(seconds / 60);
    if (seconds < 86400) return QStringLiteral("%1 h ago").arg(seconds / 3600);
    if (seconds < 2 * 86400) return QStringLiteral("yesterday");
    if (seconds < 7 * 86400) return QStringLiteral("%1 days ago").arg(seconds / 86400);
    return QDateTime::fromMSecsSinceEpoch(closedAt).toString(QStringLiteral("yyyy-MM-dd"));
}

}  // namespace closed
}  // namespace relay
