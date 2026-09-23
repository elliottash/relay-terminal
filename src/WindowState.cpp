// SPDX-License-Identifier: AGPL-3.0-or-later
#include "WindowState.h"

#include <QDateTime>
#include <QRegularExpression>
#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSaveFile>
#include <QStandardPaths>

namespace relay {
namespace windowstate {
namespace {

const QLatin1String kVersion("version");
const QLatin1String kSaved("saved");
const QLatin1String kWindows("windows");
const QLatin1String kGeometry("geometry");
const QLatin1String kScreen("screen");
const QLatin1String kTabs("tabs");
const QLatin1String kCurrent("current");
const QLatin1String kTitles("titles");
const QLatin1String kProject("project");
const QLatin1String kNode("node");

}  // namespace

QString defaultDirectory() {
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (data.isEmpty()) return {};
    return data + QStringLiteral("/relay/state");
}

QString defaultPath() {
    const QString dir = defaultDirectory();
    return dir.isEmpty() ? QString() : dir + QStringLiteral("/windows.json");
}

QString defaultLockPath() {
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (!runtime.isEmpty()) return runtime + QStringLiteral("/relay/windows.lock");
    const QString dir = defaultDirectory();
    return dir.isEmpty() ? QString() : dir + QStringLiteral("/windows.lock");
}

QJsonObject document(const QJsonArray &windows, qint64 savedAt) {
    return {{kVersion, kSchemaVersion},
            {kSaved, savedAt > 0 ? savedAt : QDateTime::currentSecsSinceEpoch()},
            {kWindows, windows}};
}

QJsonArray windowsOf(const QJsonObject &document) { return document.value(kWindows).toArray(); }

QJsonObject windowRecord(const QRect &geometry, const QString &screen, const QJsonArray &tabs,
                         int current, const QStringList &titles) {
    QJsonObject record{{kTabs, tabs}, {kCurrent, current}};
    if (geometry.isValid())
        record.insert(kGeometry, QJsonArray{geometry.x(), geometry.y(), geometry.width(), geometry.height()});
    if (!screen.isEmpty()) record.insert(kScreen, screen);
    if (!titles.isEmpty()) record.insert(kTitles, QJsonArray::fromStringList(titles));
    return record;
}

QRect geometryOf(const QJsonObject &window) {
    const QJsonArray values = window.value(kGeometry).toArray();
    if (values.size() != 4) return {};
    const int x = values.at(0).toInt(), y = values.at(1).toInt();
    const int w = values.at(2).toInt(), h = values.at(3).toInt();
    if (w <= 0 || h <= 0) return {};
    return QRect(x, y, w, h);
}

QString screenOf(const QJsonObject &window) { return window.value(kScreen).toString(); }
QJsonArray tabsOf(const QJsonObject &window) { return window.value(kTabs).toArray(); }
int currentOf(const QJsonObject &window) { return window.value(kCurrent).toInt(); }

QStringList titlesOf(const QJsonObject &window) {
    QStringList titles;
    for (const auto &value : window.value(kTitles).toArray()) titles << value.toString();
    return titles;
}

bool write(const QString &path, const QJsonObject &state, QString *error) {
    auto fail = [error](const QString &text) {
        if (error) *error = text;
        return false;
    };
    if (path.isEmpty()) return fail(QStringLiteral("No writable data directory for the window layout."));
    const QString dir = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(dir)) return fail(QStringLiteral("Could not create %1.").arg(dir));
    QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QSaveFile file(path);
    // QSaveFile writes a sibling temp file and renames it into place, so a reader (or a crash)
    // never sees a half-written layout.
    if (!file.open(QIODevice::WriteOnly)) return fail(file.errorString());
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    if (file.write(QJsonDocument(state).toJson(QJsonDocument::Indented)) < 0) {
        file.cancelWriting();
        return fail(file.errorString());
    }
    if (!file.commit()) return fail(file.errorString());
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    if (error) error->clear();
    return true;
}

QJsonObject read(const QString &path, QString *error) {
    if (error) error->clear();
    auto fail = [error](const QString &text) {
        if (error) *error = text;
        return QJsonObject();
    };
    if (path.isEmpty()) return {};
    QFile file(path);
    if (!file.exists()) return {};
    if (!file.open(QIODevice::ReadOnly)) return fail(file.errorString());
    QJsonParseError parse{};
    const QJsonDocument parsed = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError) return fail(parse.errorString());
    if (!parsed.isObject()) return fail(QStringLiteral("The saved window layout is not a JSON object."));
    const QJsonObject state = parsed.object();
    const QJsonValue version = state.value(kVersion);
    if (!version.isDouble()) return fail(QStringLiteral("The saved window layout has no schema version."));
    if (version.toInt() != kSchemaVersion)
        return fail(QStringLiteral("The saved window layout is version %1; this Relay writes version %2.")
                        .arg(version.toInt())
                        .arg(kSchemaVersion));
    if (!state.value(kWindows).isArray()) return fail(QStringLiteral("The saved window layout has no window list."));
    return state;
}

QJsonObject tabNode(const QJsonObject &tab) {
    const QJsonValue node = tab.value(kNode);
    return node.isObject() ? node.toObject() : tab;
}

QString tabProject(const QJsonObject &tab) {
    return tab.value(kNode).isObject() ? tab.value(kProject).toString() : QString();
}

QString tabTheme(const QJsonObject &tab) {
    return tab.value(kNode).isObject() ? tab.value(QStringLiteral("theme")).toString() : QString();
}

QString tabId(const QJsonObject &tab) {
    return tab.value(kNode).isObject() ? tab.value(QStringLiteral("tab_id")).toString() : QString();
}

bool isUsableNode(const QJsonObject &node, int depth) {
    if (depth > kMaxDepth || node.isEmpty()) return false;
    // A saved *tab* may be the {"project", "node"} wrapper (#JN7X): judge what is inside it. Only
    // at the top, because the wrapper is a tab's shape and never a node's; the depth still goes
    // up, so a hand-edited file cannot nest wrappers into a loop.
    if (depth == 0 && node.value(kNode).isObject()) return isUsableNode(tabNode(node), depth + 1);
    if (node.contains(QStringLiteral("split"))) {
        const QJsonArray children = node.value(QStringLiteral("children")).toArray();
        if (children.isEmpty()) return false;
        for (const auto &child : children)
            if (!child.isObject() || !isUsableNode(child.toObject(), depth + 1)) return false;
        return true;
    }
    if (node.contains(QStringLiteral("pane"))) return node.value(QStringLiteral("pane")).isObject();
    // The Switchboard is keyed on its workspace and tab, not on a file path.
    if (node.contains(QStringLiteral("board"))) {
        const QJsonObject board = node.value(QStringLiteral("board")).toObject();
        return node.value(QStringLiteral("board")).isObject()
               && !board.value(QStringLiteral("workspace")).toString().isEmpty();
    }
    // An Options or Actions pane (card #XAME) restores with whatever the catalog holds then; a
    // saved tab or row id that no longer exists is simply not revealed.
    if (node.contains(QStringLiteral("settings"))) return node.value(QStringLiteral("settings")).isObject();
    // The Test suites pane (card #7BM4) restores empty and asks its tab's board worker for the
    // inventory again, so the object may hold nothing but the project it was opened on.
    if (node.contains(QStringLiteral("testsuites"))) return node.value(QStringLiteral("testsuites")).isObject();
    // The models pane (card #MDL1 t:a11): the object holds the directory and the tab it was left
    // on, and it comes back serving the first terminal pane of its tab, so there is nothing in it
    // that can go stale. A node listed here is not merely *restorable* — a node this function does
    // not know makes its whole **split** unusable, and with it the tab: a driven quit-and-reopen
    // came back with no models pane *and no terminal*, because the pair was one split.
    if (node.contains(QStringLiteral("models"))) return node.value(QStringLiteral("models")).isObject();
    // The Sessions & Projects pane (card #8EXS): like the models pane, it comes back serving the
    // first terminal pane of its tab and re-reads its tab and search text from the object, so
    // there is nothing in it that can go stale either.
    if (node.contains(QStringLiteral("sessions"))) return node.value(QStringLiteral("sessions")).isObject();
    // The Activity pane (card #QT8C) is saved beside its owner (PaneChrome::serialize writes
    // `internals`) and buildNode() restores it empty until the next event — but this gate did not
    // know it, so a tab holding a terminal *and* an Activity pane came back as nothing (card #ACT1,
    // the same trap the models pane fell into in bb5fba2b).
    if (node.contains(QStringLiteral("internals"))) return node.value(QStringLiteral("internals")).isObject();
    // A subagent pane (card #WD83) comes back with its tabs' text; one with no tabs is not saved.
    if (node.contains(QStringLiteral("subagents"))) {
        const QJsonObject subagents = node.value(QStringLiteral("subagents")).toObject();
        return node.value(QStringLiteral("subagents")).isObject() && !subagents.value(QStringLiteral("tabs")).toArray().isEmpty();
    }
    for (const char *kind : {"explorer", "preview", "plan"}) {
        const QString key = QString::fromLatin1(kind);
        if (!node.contains(key)) continue;
        // A tool pane whose file is gone still restores: buildNode() falls back to a terminal pane.
        return node.value(key).isObject() && !node.value(key).toObject().value(QStringLiteral("path")).toString().isEmpty();
    }
    return false;
}

QJsonArray usableWindows(const QJsonObject &document) {
    QJsonArray windows;
    for (const auto &value : windowsOf(document)) {
        if (!value.isObject()) continue;
        const QJsonObject window = value.toObject();
        QJsonArray tabs;
        for (const auto &tab : tabsOf(window))
            if (tab.isObject() && isUsableNode(tab.toObject())) tabs.append(tab);
        if (tabs.isEmpty()) continue;
        QJsonObject kept = window;
        kept.insert(kTabs, tabs);
        kept.insert(kCurrent, qBound(0, currentOf(window), int(tabs.size()) - 1));
        windows.append(kept);
    }
    return windows;
}

QRect clampToScreens(const QRect &geometry, const QString &screenName, const QList<Screen> &screens) {
    if (!geometry.isValid() || screens.isEmpty()) return geometry;
    const Screen *target = nullptr;
    for (const Screen &screen : screens)
        if (!screenName.isEmpty() && screen.name == screenName && screen.available.isValid()) { target = &screen; break; }
    if (!target)
        for (const Screen &screen : screens)
            if (screen.available.isValid() && screen.available.contains(geometry.center())) { target = &screen; break; }
    if (!target)
        for (const Screen &screen : screens)
            if (screen.available.isValid() && screen.available.intersects(geometry)) { target = &screen; break; }
    if (!target)
        for (const Screen &screen : screens)
            if (screen.available.isValid()) { target = &screen; break; }
    if (!target) return geometry;
    const QRect area = target->available;
    QRect result = geometry;
    result.setWidth(std::min(result.width(), area.width()));
    result.setHeight(std::min(result.height(), area.height()));
    result.moveLeft(qBound(area.left(), result.left(), area.right() - result.width() + 1));
    result.moveTop(qBound(area.top(), result.top(), area.bottom() - result.height() + 1));
    return result;
}

QString resolveDirectory(const QString &cwd, const QString &workspace, const QString &home) {
    if (!cwd.isEmpty() && QFileInfo(cwd).isDir()) return cwd;
    if (!workspace.isEmpty() && QFileInfo(workspace).isDir()) return workspace;
    return home;
}

// ----- the terminal scrollback of a saved pane ------------------------------------------------

QString scrollbackDirectory() {
    const QString dir = defaultDirectory();
    return dir.isEmpty() ? QString() : dir + QStringLiteral("/scrollback");
}

bool isScrollbackId(const QString &id) {
    if (id.size() < 8 || id.size() > 64) return false;
    for (const QChar c : id) {
        const ushort u = c.unicode();
        const bool ok = (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || u == '-';
        if (!ok) return false;
    }
    return true;
}

QString scrollbackPath(const QString &id) {
    const QString dir = scrollbackDirectory();
    if (dir.isEmpty() || !isScrollbackId(id)) return {};
    return dir + QLatin1Char('/') + id + QStringLiteral(".txt");
}

QStringList clampScrollback(QStringList lines, int maxLines, qint64 maxBytes) {
    // A terminal's bottom rows are usually blank; they are not worth a restart.
    while (!lines.isEmpty() && lines.constLast().trimmed().isEmpty()) lines.removeLast();
    if (maxLines <= 0 || maxBytes <= 0) return {};
    if (lines.size() > maxLines) lines = lines.mid(lines.size() - maxLines);
    qint64 bytes = 0;
    int first = lines.size();
    // Walk back from the newest line and stop at the byte cap: the tail is what the user was
    // looking at when Relay quit.
    for (int i = lines.size() - 1; i >= 0; --i) {
        const qint64 size = qint64(lines.at(i).toUtf8().size()) + 1;
        if (bytes + size > maxBytes) break;
        bytes += size;
        first = i;
    }
    return first == 0 ? lines : lines.mid(first);
}

namespace {

// The file side of both stores: the per-pane one below and the per-session one in
// `relay::sessiontext`, which saves the same text under a different name (card #0TJ9). One copy,
// so the two can never drift apart on permissions, clamping or the empty-file rule.
bool writeScrollbackFile(const QString &path, const QStringList &lines, QString *error) {
    auto fail = [error](const QString &text) {
        if (error) *error = text;
        return false;
    };
    if (error) error->clear();
    if (path.isEmpty()) return fail(QStringLiteral("No writable place for this scrollback."));
    const QStringList kept = clampScrollback(lines);
    if (kept.isEmpty()) {
        // Nothing to bring back: leaving the previous file would restore stale output.
        QFile::remove(path);
        return true;
    }
    const QString dir = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(dir)) return fail(QStringLiteral("Could not create %1.").arg(dir));
    QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return fail(file.errorString());
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    if (file.write(kept.join(QLatin1Char('\n')).toUtf8() + '\n') < 0) {
        file.cancelWriting();
        return fail(file.errorString());
    }
    if (!file.commit()) return fail(file.errorString());
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    return true;
}

QStringList readScrollbackFile(const QString &path, int maxLines) {
    if (path.isEmpty()) return {};
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) return {};
    // A file that grew past the cap (an older Relay, or an edit) is read from its tail only.
    if (file.size() > kScrollbackMaxBytes) file.seek(file.size() - kScrollbackMaxBytes);
    QStringList lines = QString::fromUtf8(file.read(kScrollbackMaxBytes + 1)).split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.constLast().isEmpty()) lines.removeLast();   // the trailing newline
    return clampScrollback(lines, std::min(maxLines, kScrollbackMaxLines), kScrollbackMaxBytes);
}

}  // namespace

bool writeScrollback(const QString &id, const QStringList &lines, QString *error) {
    const QString path = scrollbackPath(id);
    if (path.isEmpty()) {
        if (error) *error = QStringLiteral("No writable place for this pane's scrollback.");
        return false;
    }
    return writeScrollbackFile(path, lines, error);
}

QStringList readScrollback(const QString &id, int maxLines) {
    return readScrollbackFile(scrollbackPath(id), maxLines);
}

namespace {

void collectScrollbackIds(const QJsonObject &node, QStringList *ids, int depth) {
    if (depth > kMaxDepth) return;
    if (node.contains(QStringLiteral("split"))) {
        for (const auto &child : node.value(QStringLiteral("children")).toArray())
            if (child.isObject()) collectScrollbackIds(child.toObject(), ids, depth + 1);
        return;
    }
    const QString id = node.value(QStringLiteral("pane")).toObject().value(QStringLiteral("scrollback")).toString();
    if (isScrollbackId(id) && !ids->contains(id)) ids->append(id);
}

}  // namespace

QStringList scrollbackIds(const QJsonArray &windows) {
    QStringList ids;
    for (const auto &value : windows) {
        if (!value.isObject()) continue;
        for (const auto &tab : tabsOf(value.toObject()))
            // tabNode(): an attached tab is a wrapper, and its panes' saved text must still be
            // found or the next prune deletes every one of them (#JN7X).
            if (tab.isObject()) collectScrollbackIds(tabNode(tab.toObject()), &ids, 0);
    }
    return ids;
}

int pruneScrollback(const QStringList &keep) {
    const QString dir = scrollbackDirectory();
    if (dir.isEmpty()) return 0;
    int removed = 0;
    const auto files = QDir(dir).entryInfoList({QStringLiteral("*.txt")}, QDir::Files);
    for (const QFileInfo &file : files)
        if (!keep.contains(file.completeBaseName()) && QFile::remove(file.absoluteFilePath())) ++removed;
    return removed;
}

void removeAllScrollback() {
    const QString dir = scrollbackDirectory();
    if (!dir.isEmpty()) QDir(dir).removeRecursively();
}

}  // namespace windowstate

// ----- the terminal text of a conversation (card #0TJ9) ---------------------------------------

namespace sessiontext {
namespace {

// Remove CSI SGR sequences so a line that was saved with formatting can still be matched by the
// plain-text rules that locate turn markers and restore marks.
QString stripSgr(const QString &text) {
    static const QRegularExpression sgr(QStringLiteral("\\x1b\\[[0-9;:]*m"));
    return QString(text).remove(sgr);
}

bool isHex(const QString &text, int from, int count, bool anyCase) {
    if (from + count > text.size()) return false;
    for (int i = from; i < from + count; ++i) {
        const ushort u = text.at(i).unicode();
        const bool lower = (u >= '0' && u <= '9') || (u >= 'a' && u <= 'f');
        if (!(lower || (anyCase && u >= 'A' && u <= 'F'))) return false;
    }
    return true;
}

// A directory Relay may put a sidecar in: absolute, and nothing that could climb out of it. The
// path comes from the worker, not from the user, but it becomes a file name here and a session
// directory carrying `..` would write outside the sessions tree.
bool isUsableDirectory(const QString &directory) {
    if (directory.isEmpty() || !QDir::isAbsolutePath(directory)) return false;
    const QString clean = QDir::cleanPath(directory);
    return clean == directory || clean + QLatin1Char('/') == directory;
}

}  // namespace

bool isSessionId(const QString &id) { return id.size() == 32 && isHex(id, 0, 32, false); }

bool isGuestId(const QString &id) {
    if (id.size() != 36) return false;
    for (int dash : {8, 13, 18, 23})
        if (id.at(dash) != QLatin1Char('-')) return false;
    return isHex(id, 0, 8, true) && isHex(id, 9, 4, true) && isHex(id, 14, 4, true)
           && isHex(id, 19, 4, true) && isHex(id, 24, 12, true);
}

bool isGuestSource(const QString &source) {
    return source == QLatin1String("claude") || source == QLatin1String("codex");
}

QString sessionPath(const QString &sessionDir, const QString &id) {
    if (!isUsableDirectory(sessionDir) || !isSessionId(id)) return {};
    return QDir::cleanPath(sessionDir) + QLatin1Char('/') + id + QStringLiteral(".scrollback.txt");
}

QString rewoundPath(const QString &sessionDir, const QString &id, int n) {
    if (n < 1 || n > 9999 || !isUsableDirectory(sessionDir) || !isSessionId(id)) return {};
    return QDir::cleanPath(sessionDir) + QLatin1Char('/') + id
           + QStringLiteral(".rewound-%1.scrollback.txt").arg(n);
}

QString guestDirectory() {
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (data.isEmpty()) return {};
    return data + QStringLiteral("/relay/sessions/guests");
}

QString guestPath(const QString &source, const QString &id) {
    const QString dir = guestDirectory();
    if (dir.isEmpty() || !isGuestSource(source) || !isGuestId(id)) return {};
    return dir + QLatin1Char('/') + source + QLatin1Char('/') + id + QStringLiteral(".scrollback.txt");
}

bool write(const QString &path, const QStringList &lines, QString *error) {
    if (path.isEmpty()) {
        if (error) *error = QStringLiteral("No writable place for this conversation's terminal text.");
        return false;
    }
    return windowstate::writeScrollbackFile(path, lines, error);
}

QStringList read(const QString &path, int maxLines) {
    return windowstate::readScrollbackFile(path, maxLines);
}

QStringList sidecars(const QString &sessionDir, const QString &id) {
    const QString text = sessionPath(sessionDir, id);
    if (text.isEmpty()) return {};
    QStringList found;
    if (QFileInfo::exists(text)) found << text;
    const auto rewound = QDir(QDir::cleanPath(sessionDir))
                             .entryInfoList({id + QStringLiteral(".rewound-*.scrollback.txt")}, QDir::Files, QDir::Name);
    for (const QFileInfo &file : rewound) found << file.absoluteFilePath();
    return found;
}

int turnStart(const QStringList &lines, const QString &prompt) {
    static const QString marker = QStringLiteral("✦ ");   // the ✦ a turn's first line wears
    const QString first = prompt.section(QLatin1Char('\n'), 0, 0).trimmed();
    if (first.isEmpty()) return -1;
    for (int i = lines.size() - 1; i >= 0; --i) {
        const QString plain = stripSgr(lines.at(i));
        if (!plain.startsWith(marker)) continue;
        // The printed line is the prompt cut at the pane's width, so it is a prefix of it — and
        // the pane's other ✦ lines ("✦ the command finished …") are not, which is what keeps them
        // from being mistaken for a turn.
        const QString shown = plain.mid(marker.size()).trimmed();
        if (!shown.isEmpty() && first.startsWith(shown)) return i;
    }
    return -1;
}

}  // namespace sessiontext
}  // namespace relay
