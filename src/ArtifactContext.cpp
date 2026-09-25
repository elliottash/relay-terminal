// SPDX-License-Identifier: AGPL-3.0-or-later
// The rules behind src/ArtifactContext.h: which task plugin a file activates, what its commands
// send, and the `context` block a docked file agent is configured with (card #PBZ4).
#include "ArtifactContext.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#include <algorithm>

namespace relay::agent {

namespace {

const QStringList kOrigins{QStringLiteral("project"), QStringLiteral("global"), QStringLiteral("bundled")};

QJsonObject readJson(const QString &file)
{
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(in.read(1 << 20));
    return doc.isObject() ? doc.object() : QJsonObject();
}

// fnmatch.translate, which is what `task_plugins.match_activation` matches with: `*` and `?` cross
// a '/', `[...]` is a class and `[!...]` its negation, everything else is literal.
QRegularExpression globExpression(const QString &glob)
{
    QString out;
    for (int i = 0; i < glob.size(); ++i) {
        const QChar c = glob.at(i);
        if (c == QLatin1Char('*')) {
            out += QStringLiteral(".*");
        } else if (c == QLatin1Char('?')) {
            out += QLatin1Char('.');
        } else if (c == QLatin1Char('[')) {
            int j = i + 1;
            if (j < glob.size() && glob.at(j) == QLatin1Char('!'))
                ++j;
            if (j < glob.size() && glob.at(j) == QLatin1Char(']'))
                ++j;
            while (j < glob.size() && glob.at(j) != QLatin1Char(']'))
                ++j;
            if (j >= glob.size()) {
                out += QStringLiteral("\\[");
                continue;
            }
            QString inside = glob.mid(i + 1, j - i - 1);
            if (inside.startsWith(QLatin1Char('!')))
                inside = QLatin1Char('^') + inside.mid(1);
            inside.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
            out += QLatin1Char('[') + inside + QLatin1Char(']');
            i = j;
        } else {
            out += QRegularExpression::escape(QString(c));
        }
    }
    return QRegularExpression(QStringLiteral("\\A(?:%1)\\z").arg(out));
}

// `instructions.git_root`: the nearest folder holding `.git`, stopping at the home folder, or the
// folder itself.
QString gitRoot(const QString &dir)
{
    const QString home = QDir::homePath();
    QDir at(dir);
    for (;;) {
        if (QFileInfo::exists(at.filePath(QStringLiteral(".git"))))
            return at.absolutePath();
        if (at.absolutePath() == home || !at.cdUp())
            break;
    }
    return dir;
}

struct Found {
    QString origin;
    ArtifactPlugin plugin;
};

// Every valid package under one origin's folders; the first folder that names an id keeps it.
void scan(const QStringList &dirs, const QString &origin, QHash<QString, Found> &into)
{
    for (const QString &dir : dirs) {
        if (dir.isEmpty())
            continue;
        const QDir root(dir);
        const QStringList packages = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &name : packages) {
            const QJsonObject manifest = readJson(root.filePath(name + QStringLiteral("/plugin.json")));
            ArtifactPlugin plugin = readPluginManifest(manifest, origin);
            if (!plugin.valid() || into.contains(plugin.id))
                continue;   // an earlier origin (project over global over bundled) already has it
            into.insert(plugin.id, Found{origin, plugin});
        }
    }
}

// `task_plugins.enablement`, less the digest check: a project package runs only once it has been
// enabled for this project; a global or bundled one is enabled unless it was switched off. (The
// digest — "changed since it was enabled" — is the worker's to enforce when a tool of the package
// runs; here a command is only a prompt the agent is asked, so listing it grants nothing.)
bool enabled(const QJsonObject &state, const QString &project, const QString &id, const QString &origin)
{
    const QJsonValue entry = state.value(QStringLiteral("projects")).toObject().value(project).toObject()
                                 .value(id).toObject().value(origin);
    if (entry.isObject()) {
        const QJsonValue on = entry.toObject().value(QStringLiteral("enabled"));
        if (on.isBool())
            return on.toBool();
    }
    return origin != QStringLiteral("project");
}

QString fileName(const QString &path)
{
    if (path.startsWith(QStringLiteral("ssh://")))
        return QUrl(path).path().section(QLatin1Char('/'), -1);
    return QFileInfo(path).fileName();
}

} // namespace

// ---- the plugin ---------------------------------------------------------------------------------

QString ArtifactPlugin::space() const
{
    return id.section(QLatin1Char('.'), -1);
}

PluginSearch PluginSearch::defaults(const QString &bundledDir)
{
    PluginSearch search;
    search.bundled = bundledDir;
    QString config = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (config.isEmpty())
        config = QDir::homePath() + QStringLiteral("/.config");
    search.global = config + QStringLiteral("/relay/plugins");
    search.state = qEnvironmentVariable("RELAY_PLUGIN_STATE");
    if (search.state.isEmpty())
        search.state = config + QStringLiteral("/relay/plugins.json");
    return search;
}

bool activationMatches(const QString &glob, const QString &path, const QString &root)
{
    if (glob.isEmpty() || path.isEmpty())
        return false;
    QString subject;
    if (!glob.contains(QLatin1Char('/'))) {
        subject = fileName(path);
    } else {
        if (root.isEmpty() || path.startsWith(QStringLiteral("ssh://")))
            return false;
        const QString rel = QDir(root).relativeFilePath(path);
        if (rel.startsWith(QStringLiteral("..")))
            return false;
        subject = rel;
    }
    return globExpression(glob).match(subject).hasMatch();
}

ArtifactPlugin readPluginManifest(const QJsonObject &manifest, const QString &origin, const QString &role)
{
    ArtifactPlugin plugin;
    static const QRegularExpression idPattern(
        QStringLiteral("\\A[a-z][a-z0-9-]{0,30}(\\.[a-z][a-z0-9-]{0,30}){1,3}\\z"));
    const QString id = manifest.value(QStringLiteral("id")).toString();
    if (!idPattern.match(id).hasMatch())
        return plugin;
    plugin.id = id;
    plugin.name = manifest.value(QStringLiteral("name")).toString(id);
    plugin.origin = origin;
    for (const QJsonValue &glob : manifest.value(QStringLiteral("activation")).toObject()
                                      .value(QStringLiteral("files")).toArray())
        if (glob.isString())
            plugin.files << glob.toString();
    // Slash commands are schema 2's (#6FDD); a version-1 manifest is still a plugin, with none.
    if (manifest.value(QStringLiteral("schema_version")).toInt() != 2)
        return plugin;
    static const QRegularExpression namePattern(QStringLiteral("\\A[a-z][a-z0-9-]{0,31}\\z"));
    QSet<QString> seen;
    for (const QJsonValue &value : manifest.value(QStringLiteral("commands")).toArray()) {
        const QJsonObject item = value.toObject();
        PluginCommand command;
        command.name = item.value(QStringLiteral("name")).toString();
        if (!namePattern.match(command.name).hasMatch() || seen.contains(command.name))
            continue;
        const QJsonArray roles = item.value(QStringLiteral("when")).toObject().value(QStringLiteral("roles")).toArray();
        if (!roles.isEmpty() && !roles.contains(QJsonValue(role)))
            continue;
        const QJsonObject action = item.value(QStringLiteral("action")).toObject();
        command.kind = action.value(QStringLiteral("kind")).toString();
        if (command.kind == QStringLiteral("prompt"))
            command.target = action.value(QStringLiteral("prompt")).toString();
        else if (command.kind == QStringLiteral("tool"))
            command.target = action.value(QStringLiteral("tool")).toString();
        else
            continue;   // a `program_line` needs a program, and an editor has none
        if (command.target.trimmed().isEmpty())
            continue;
        command.description = item.value(QStringLiteral("description")).toString();
        QStringList args;
        for (const QJsonValue &arg : item.value(QStringLiteral("args")).toArray()) {
            const QJsonObject a = arg.toObject();
            const QString name = a.value(QStringLiteral("name")).toString();
            if (name.isEmpty())
                continue;
            args << (a.value(QStringLiteral("required")).toBool() ? QStringLiteral("<%1>").arg(name)
                                                                  : QStringLiteral("[%1]").arg(name));
        }
        command.args = args.join(QLatin1Char(' '));
        seen.insert(command.name);
        plugin.commands << command;
    }
    return plugin;
}

ArtifactPlugin pluginForFile(const QString &path, const PluginSearch &search)
{
    if (path.isEmpty())
        return {};
    const bool remote = path.startsWith(QStringLiteral("ssh://"));
    QString root, project;
    QStringList projectDirs;
    if (!remote) {
        const QFileInfo info(path);
        const QString dir = QFileInfo(info.absolutePath()).canonicalFilePath().isEmpty()
                                ? info.absolutePath()
                                : QFileInfo(info.absolutePath()).canonicalFilePath();
        root = gitRoot(dir);
        project = root;
        // `project_plugin_dirs`: the file's folder and each parent up to the git root.
        for (QDir at(dir);;) {
            projectDirs << at.filePath(QStringLiteral(".relay/plugins"));
            if (at.absolutePath() == root || !at.cdUp())
                break;
        }
    }
    QHash<QString, Found> found;
    scan(projectDirs, kOrigins.at(0), found);
    scan({search.global}, kOrigins.at(1), found);
    scan({search.bundled}, kOrigins.at(2), found);
    const QJsonObject state = search.state.isEmpty() ? QJsonObject() : readJson(search.state);

    QList<Found> matches;
    for (const Found &entry : std::as_const(found)) {
        const bool selects = std::any_of(entry.plugin.files.cbegin(), entry.plugin.files.cend(),
                                         [&](const QString &glob) { return activationMatches(glob, path, root); });
        if (selects && enabled(state, project, entry.plugin.id, entry.origin))
            matches << entry;
    }
    std::sort(matches.begin(), matches.end(), [](const Found &a, const Found &b) {
        const int oa = int(kOrigins.indexOf(a.origin)), ob = int(kOrigins.indexOf(b.origin));
        return oa != ob ? oa < ob : a.plugin.id < b.plugin.id;
    });
    return matches.isEmpty() ? ArtifactPlugin() : matches.first().plugin;
}

// ---- the context --------------------------------------------------------------------------------

QString artifactKey(const QString &path, int limit)
{
    const QString plain = QStringLiteral("file:") + path;
    if (plain.size() <= limit)
        return plain;
    const QByteArray digest = QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
    return QStringLiteral("file:sha1:") + QString::fromLatin1(digest);
}

void ArtifactContext::setFile(const QString &path)
{
    if (path == m_file)
        return;
    m_file = path;
    changed();
}

void ArtifactContext::setPlugin(const ArtifactPlugin &plugin)
{
    m_plugin = plugin;
    changed();
}

QString ArtifactContext::title() const
{
    const QString name = fileName(m_file);
    return name.isEmpty() ? QStringLiteral("File agent") : QStringLiteral("%1 agent").arg(name);
}

ContextSpec ArtifactContext::spec() const
{
    ContextSpec spec;
    spec.name = QStringLiteral("artifact");
    // The worker's `surface` is at most 64 characters and the persist key 128 with the tab id in
    // front of it (`RelayWindow::TabConsoleContext`), so a long path is keyed by its digest.
    spec.surface = artifactKey(m_file, 64);
    spec.agentRole = QStringLiteral("switchboard");
    spec.scope = QStringLiteral("console");
    // One conversation per file: the window puts the tab id in front (`<tab>/file:<path>`), as it
    // does for a card, so two files in one tab are two conversations and a restart finds each.
    if (!m_file.isEmpty()) {
        spec.persistScope = QStringLiteral("helper");
        spec.persistKey = artifactKey(m_file, 80);
    }
    spec.briefKey = QStringLiteral("artifact");
    spec.briefTitle = title();
    spec.screen = screen();
    spec.shell = false;
    spec.routing = QStringLiteral("agent");
    spec.file = m_file;
    spec.plugin = m_plugin.id;
    return spec;
}

QString ArtifactContext::screen() const
{
    if (m_file.isEmpty())
        return {};
    const ArtifactState now = state ? state() : ArtifactState();
    QStringList lines;
    QString head = QStringLiteral("File: %1").arg(m_file);
    QStringList flags;
    if (!now.mode.isEmpty())
        flags << now.mode;
    flags << (now.editable ? QStringLiteral("editing") : QStringLiteral("read only"));
    if (now.dirty)
        flags << QStringLiteral("unsaved edits");
    lines << head + QStringLiteral(" (") + flags.join(QStringLiteral(", ")) + QLatin1Char(')');
    if (m_plugin.valid())
        lines << QStringLiteral("Plugin: %1 (%2)").arg(m_plugin.name, m_plugin.id);
    if (now.line > 0)
        lines << QStringLiteral("Cursor: line %1, column %2").arg(now.line).arg(std::max(1, now.column));
    if (now.firstLine > 0 && !now.selection.isEmpty()) {
        const QString where = now.firstLine == now.lastLine ? QStringLiteral("line %1").arg(now.firstLine)
                                                            : QStringLiteral("lines %1-%2").arg(now.firstLine).arg(now.lastLine);
        const QString sofar = lines.join(QLatin1Char('\n'));
        const int room = std::max(0, kScreenLimit - int(sofar.size()) - 40);
        QString text = now.selection;
        if (text.size() > room)
            text = text.left(std::max(0, room - 1)) + QChar(0x2026);
        lines << QStringLiteral("Selection (%1):\n%2").arg(where, text);
    }
    return lines.join(QLatin1Char('\n'));
}

QString ArtifactContext::promptFor(const PluginCommand &command, const QString &args) const
{
    const QString extra = args.trimmed();
    QString prompt;
    if (command.kind == QStringLiteral("tool")) {
        prompt = QStringLiteral("Run the %1 tool for %2").arg(command.target, m_file);
        prompt += extra.isEmpty() ? QStringLiteral(".") : QStringLiteral(" with: %1").arg(extra);
        return prompt;
    }
    prompt = command.target;
    prompt.replace(QStringLiteral("{file}"), m_file);
    if (!extra.isEmpty())
        prompt += QStringLiteral("\n\n") + extra;
    return prompt;
}

QList<Action> ArtifactContext::actions() const
{
    QList<Action> row;
    const ArtifactState now = state ? state() : ArtifactState();
    // The editor's own two first, so their letters are fixed whatever the plugin declares; the
    // plugin's commands take the first free letter of their name, or none (`withUniqueLetters`).
    QSet<QChar> used{QLatin1Char('s'), QLatin1Char('r')};
    for (const PluginCommand &command : m_plugin.commands) {
        Action action;
        action.key = QStringLiteral("artifact.") + command.name;
        for (const QChar c : command.name) {
            if (c.isLetter() && !used.contains(c)) {
                action.letter = QString(c);
                used.insert(c);
                break;
            }
        }
        QString label = command.name;
        if (!label.isEmpty())
            label[0] = label.at(0).toUpper();
        action.label = label;
        action.tooltip = QStringLiteral("/%1 — %2").arg(command.name, command.description);
        action.enabled = bool(sendPrompt) && !m_file.isEmpty();
        const QString prompt = promptFor(command, QString());
        auto send = sendPrompt;
        action.run = [send, prompt] { if (send) send(prompt); };
        row << action;
    }
    if (save) {
        Action action;
        action.key = QStringLiteral("artifact.save");
        action.letter = QStringLiteral("s");
        action.label = QStringLiteral("Save");
        action.tooltip = QStringLiteral("Write the buffer to %1 (Ctrl+S)").arg(fileName(m_file));
        action.enabled = now.editable && now.dirty;
        auto run = save;
        action.run = [run] { run(); };
        row << action;
    }
    if (revert) {
        Action action;
        action.key = QStringLiteral("artifact.revert");
        action.letter = QStringLiteral("r");
        action.label = QStringLiteral("Revert");
        action.tooltip = QStringLiteral("Put the buffer back to the file as it is on disk");
        action.enabled = now.dirty;
        auto run = revert;
        action.run = [run] { run(); };
        row << action;
    }
    return row;
}

QList<ContextCommand> ArtifactContext::slashCommands() const
{
    QList<ContextCommand> out;
    if (m_file.isEmpty() || !sendPrompt)
        return out;
    for (const PluginCommand &command : m_plugin.commands) {
        ContextCommand slash;
        slash.name = command.name;
        slash.args = command.args;
        slash.description = command.description;
        slash.group = m_plugin.name;
        slash.space = m_plugin.space();
        const ArtifactContext *self = this;
        const PluginCommand copy = command;
        slash.prompt = [self, copy](const QString &args) { return self->promptFor(copy, args); };
        out << slash;
    }
    return out;
}

bool ArtifactContext::resolveLink(const relay::links::Target &target)
{
    // `file:line` for this very file lands in the editor; any other path opens the ordinary way.
    if (!target.valid || target.kind != relay::links::Kind::Path || !goToLine || m_file.isEmpty())
        return false;
    if (m_file.startsWith(QStringLiteral("ssh://")))
        return false;
    const QString mine = QFileInfo(m_file).canonicalFilePath();
    const QString theirs = QFileInfo(target.target).canonicalFilePath();
    if (mine.isEmpty() || mine != theirs)
        return false;
    goToLine(std::max(1, target.line));
    return true;
}

void ArtifactContext::turnFinished(const TurnRecord &record)
{
    if (onTurnFinished)
        onTurnFinished(record);
}

QString ArtifactContext::placeholder() const
{
    if (m_plugin.valid() && !m_plugin.commands.isEmpty())
        return QStringLiteral("Ask about %1, or / for %2 commands, Enter sends").arg(fileName(m_file), m_plugin.name);
    return QStringLiteral("Ask about %1, Enter sends").arg(fileName(m_file));
}

} // namespace relay::agent
