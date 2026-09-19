// SPDX-License-Identifier: GPL-3.0-or-later
#include "Projects.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

namespace relay {
namespace projects {
namespace {

const QLatin1String kVersion("version");
const QLatin1String kSaved("saved");
const QLatin1String kProjects("projects");
const QLatin1String kDeclined("declined");
const QLatin1String kPath("path");
const QLatin1String kKey("key");
const QLatin1String kName("name");
const QLatin1String kBoard("board");
const QLatin1String kBoardDir("board_dir");
const QLatin1String kReason("reason");
const QLatin1String kKnownSince("known_since");
const QLatin1String kLastAttached("last_attached");
const QLatin1String kAt("at");

const QLatin1String kGitEntry(".git");

qint64 secondsOr(qint64 now) { return now > 0 ? now : QDateTime::currentSecsSinceEpoch(); }

// `<dir>/<folder>` — the board folder itself, not its marker.
QString boardFolderIn(const QString &dir, const char *folder)
{
    return dir + QLatin1Char('/') + QLatin1String(folder);
}

// True when `<dir>/<folder>/board.yaml` is there, which is the only thing that makes a directory a
// board. A bare `switchboard/` folder with nothing in it is not one.
bool hasBoardMarker(const QString &dir, const char *folder)
{
    return QFileInfo::exists(boardFolderIn(dir, folder) + QLatin1Char('/') + QLatin1String(kBoardMarkerFile));
}

// The cleaned absolute spelling of a path, without asking the filesystem anything.
QString cleanedAbsolute(const QString &path)
{
    if (path.isEmpty()) return {};
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

// `~` and `~/...` the way Python's Path.expanduser() does, so keyFor() agrees with
// conv_index.workspace_digest() on a workspace that was written with a tilde. `~user` is left
// alone: Relay never produces one, and guessing another user's home would be worse than not.
QString expandTilde(const QString &path)
{
    if (path == QLatin1String("~")) return QDir::homePath();
    if (path.startsWith(QLatin1String("~/"))) return QDir::homePath() + path.mid(1);
    return path;
}

Record recordFrom(const QJsonObject &object)
{
    Record record;
    record.path = object.value(kPath).toString();
    record.key = object.value(kKey).toString();
    record.name = object.value(kName).toString();
    record.board = object.value(kBoard).toString();
    record.boardDir = object.value(kBoardDir).toString();
    record.reason = object.value(kReason).toString();
    record.knownSince = qint64(object.value(kKnownSince).toDouble());
    record.lastAttached = qint64(object.value(kLastAttached).toDouble());
    return record;
}

QJsonObject jsonFrom(const Record &record)
{
    QJsonObject object{{kPath, record.path},
                       {kKey, record.key},
                       {kName, record.name},
                       {kBoard, record.board},
                       {kReason, record.reason},
                       {kKnownSince, double(record.knownSince)},
                       {kLastAttached, double(record.lastAttached)}};
    if (!record.boardDir.isEmpty()) object.insert(kBoardDir, record.boardDir);
    return object;
}

}  // namespace

// ----- reasons ---------------------------------------------------------------------------------

QStringList reasons()
{
    return {QString::fromLatin1(kReasonSwitchboard), QString::fromLatin1(kReasonCardCommand),
            QString::fromLatin1(kReasonCardPicker),  QString::fromLatin1(kReasonExecuteCard),
            QString::fromLatin1(kReasonPicker),      QString::fromLatin1(kReasonAgentWrite),
            QString::fromLatin1(kReasonRestored),    QString::fromLatin1(kReasonRepoBoard),
            QString::fromLatin1(kReasonInitCommand), QString::fromLatin1(kReasonAgentCard)};
}

bool isReason(const QString &reason) { return reasons().contains(reason); }

// ----- the candidate project of a directory ------------------------------------------------------

QString candidateFor(const QString &cwd)
{
    if (cwd.isEmpty()) return {};
    const QString expanded = expandTilde(cwd);
    // A relative candidate resolves against the process's current directory — the launch directory
    // that card #JN7X stopped consulting — so it stands for no pane and is refused rather than
    // guessed at.
    if (!QFileInfo(expanded).isAbsolute()) return {};

    const QString start = normalize(expanded);
    if (start.isEmpty()) return {};

    // Two passes in one walk, not one: a board marker anywhere up the chain beats a nearer `.git`,
    // because a board folder was made on purpose and a `.git` is just a checkout.
    QString firstGit;
    // A plain string walk, not QDir::cdUp(): cdUp() refuses to step into a directory that is not
    // there, which would stop the walk dead on a pane whose cwd has just been deleted.
    for (QString here = start;;) {
        if (hasBoardMarker(here, kBoardFolder) || hasBoardMarker(here, kLegacyBoardFolder)) return here;
        // A linked worktree and a submodule have `.git` as a *file*; both are projects.
        if (firstGit.isEmpty() && QFileInfo::exists(here + QLatin1Char('/') + kGitEntry)) firstGit = here;
        const QString parent = QFileInfo(here).path();
        if (parent.isEmpty() || parent == here) break;   // `/` is its own parent
        here = parent;
    }
    return firstGit;
}

QString boardDirOf(const QString &project)
{
    if (project.isEmpty()) return {};
    const QString root = QDir::cleanPath(project);
    // `switchboard/` wins when a project has both: it is the folder Relay creates today, and the
    // older one is left where it is rather than merged into it.
    if (hasBoardMarker(root, kBoardFolder)) return boardFolderIn(root, kBoardFolder);
    if (hasBoardMarker(root, kLegacyBoardFolder)) return boardFolderIn(root, kLegacyBoardFolder);
    return {};
}

// ----- the key of a project ----------------------------------------------------------------------

QString normalize(const QString &path)
{
    if (path.isEmpty()) return {};
    const QString expanded = expandTilde(path);
    const QString canonical = QFileInfo(expanded).canonicalFilePath();
    return canonical.isEmpty() ? cleanedAbsolute(expanded) : canonical;
}

QString digest(const QString &normalizedPath)
{
    if (normalizedPath.isEmpty()) return {};
    const QByteArray hash =
        QCryptographicHash::hash(normalizedPath.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QString::fromLatin1(hash.left(16));
}

QString keyFor(const QString &path) { return digest(normalize(path)); }

QString nameFor(const QString &path)
{
    if (path.isEmpty()) return {};
    const QString name = QFileInfo(QDir::cleanPath(path)).fileName();
    return name.isEmpty() ? path : name;
}

// ----- records and boards ------------------------------------------------------------------------

QString boardsRoot()
{
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (data.isEmpty()) return {};
    return data + QStringLiteral("/relay/boards");
}

QString boardKindName(Board::Kind kind)
{
    switch (kind) {
    case Board::InRepo: return QString::fromLatin1(kBoardRepo);
    case Board::None:
    case Board::Uninitialized:
    case Board::Inbox: break;
    }
    return QString::fromLatin1(kBoardNone);
}

Board chooseBoard(const QString &project, const QString &existingBoardDir, const Record *known,
                  const QString &boardsRoot)
{
    // A project's board lives in the project since 2026-09-18; the root is still taken so callers
    // pass one root to chooseBoard() and inbox() alike.
    Q_UNUSED(boardsRoot);

    Board board;
    // QFileInfo::isAbsolute() is a look at the string, not at the disk, so this stays pure.
    if (project.isEmpty() || !QFileInfo(project).isAbsolute()) return board;

    const QString cleaned = QDir::cleanPath(project);
    board.project = cleaned;
    // Pure: the caller hands us the canonical path candidateFor() produced, so hashing it without a
    // stat gives the same key keyFor() would. The key groups the project's conversations; the board
    // no longer hangs off it.
    board.key = (known && !known->key.isEmpty()) ? known->key : digest(cleaned);

    if (!existingBoardDir.isEmpty()) {
        // Wherever the board already is — `switchboard/` or an older `issues/` — that is where the
        // cards are. Relay uses it in place and never moves it.
        board.kind = Board::InRepo;
        board.dir = QDir::cleanPath(existingBoardDir);
        board.needsConsent = false;
        return board;
    }

    // No board yet. `dir` is where one would go; nothing may be written there until the user has
    // answered "Initialize a project and create a Switchboard here?" for themselves.
    board.kind = Board::Uninitialized;
    board.dir = boardFolderIn(cleaned, kBoardFolder);
    board.needsConsent = true;
    return board;
}

Board inbox(const QString &boardsRoot)
{
    if (boardsRoot.isEmpty()) return {};
    Board board;
    board.kind = Board::Inbox;
    // Relay's own data directory, so it is in nobody's repository and there is nothing to consent to.
    board.dir = boardFolderIn(QDir::cleanPath(boardsRoot) + QStringLiteral("/inbox"), kBoardFolder);
    board.needsConsent = false;
    return board;
}

Board boardFor(const QString &project, const Registry *known)
{
    if (project.isEmpty()) return {};
    const QString normalized = normalize(project);
    Record record;
    if (known) record = known->record(normalized);
    return chooseBoard(normalized, boardDirOf(normalized), record.isValid() ? &record : nullptr,
                       boardsRoot());
}

// ----- the registry of known projects ------------------------------------------------------------

QString stateDirectory()
{
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (data.isEmpty()) return {};
    return data + QStringLiteral("/relay/state");
}

QString defaultPath()
{
    const QString dir = stateDirectory();
    return dir.isEmpty() ? QString() : dir + QStringLiteral("/projects.json");
}

Registry::Registry(const QString &path) : m_path(path.isEmpty() ? defaultPath() : path) {}

void Registry::setPath(const QString &path)
{
    m_path = path.isEmpty() ? defaultPath() : path;
    m_projects.clear();
    m_declined.clear();
}

int Registry::indexOf(const QString &normalized) const
{
    for (int i = 0; i < m_projects.size(); ++i)
        if (m_projects.at(i).path == normalized) return i;
    return -1;
}

int Registry::declinedIndexOf(const QString &normalized) const
{
    for (int i = 0; i < m_declined.size(); ++i)
        if (m_declined.at(i).first == normalized) return i;
    return -1;
}

bool Registry::load(QString *error)
{
    if (error) error->clear();
    m_projects.clear();
    m_declined.clear();

    auto fail = [this, error](const QString &text) {
        // Never lost, never parsed hopefully: what could not be read is kept beside the file so the
        // next write does not take it with it.
        const QString aside = corruptPath();
        if (!aside.isEmpty()) {
            QFile::remove(aside);
            QFile::copy(m_path, aside);
        }
        if (error) *error = text;
        return false;
    };

    if (m_path.isEmpty()) {
        if (error) *error = QStringLiteral("No writable data directory for the project registry.");
        return false;
    }
    QFile file(m_path);
    if (!file.exists()) return true;   // nothing known yet is not a failure
    if (!file.open(QIODevice::ReadOnly)) return fail(file.errorString());
    const QByteArray text = file.readAll();
    file.close();

    QJsonParseError parse{};
    const QJsonDocument parsed = QJsonDocument::fromJson(text, &parse);
    if (parse.error != QJsonParseError::NoError) return fail(parse.errorString());
    if (!parsed.isObject()) return fail(QStringLiteral("The project registry is not a JSON object."));
    const QJsonObject root = parsed.object();
    const QJsonValue version = root.value(kVersion);
    if (!version.isDouble()) return fail(QStringLiteral("The project registry has no schema version."));
    if (version.toInt() != kSchemaVersion)
        return fail(QStringLiteral("The project registry is version %1; this Relay writes version %2.")
                        .arg(version.toInt())
                        .arg(kSchemaVersion));
    if (!root.value(kProjects).isArray())
        return fail(QStringLiteral("The project registry has no project list."));

    for (const QJsonValue &value : root.value(kProjects).toArray()) {
        if (!value.isObject()) continue;
        const Record record = recordFrom(value.toObject());
        // A record with no path names nothing, and a duplicate would make forget() ambiguous.
        if (!record.isValid() || indexOf(record.path) >= 0) continue;
        m_projects.append(record);
    }
    for (const QJsonValue &value : root.value(kDeclined).toArray()) {
        if (!value.isObject()) continue;
        const QJsonObject object = value.toObject();
        const QString path = object.value(kPath).toString();
        if (path.isEmpty() || declinedIndexOf(path) >= 0) continue;
        m_declined.append({path, qint64(object.value(kAt).toDouble())});
    }
    return true;
}

bool Registry::save(QString *error) const
{
    auto fail = [error](const QString &text) {
        if (error) *error = text;
        return false;
    };
    if (error) error->clear();
    if (m_path.isEmpty()) return fail(QStringLiteral("No writable data directory for the project registry."));

    QJsonArray projects;
    for (const Record &record : m_projects) projects.append(jsonFrom(record));
    QJsonArray declined;
    for (const auto &entry : m_declined)
        declined.append(QJsonObject{{kPath, entry.first}, {kAt, double(entry.second)}});
    const QJsonObject root{{kVersion, kSchemaVersion},
                           {kSaved, double(QDateTime::currentSecsSinceEpoch())},
                           {kProjects, projects},
                           {kDeclined, declined}};

    const QString dir = QFileInfo(m_path).absolutePath();
    if (!QDir().mkpath(dir)) return fail(QStringLiteral("Could not create %1.").arg(dir));
    QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    // QSaveFile writes a sibling temp file and renames it, so a reader (or a crash) never sees half
    // a registry — the same way windows.json is written.
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) return fail(file.errorString());
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0) {
        file.cancelWriting();
        return fail(file.errorString());
    }
    if (!file.commit()) return fail(file.errorString());
    QFile::setPermissions(m_path, QFile::ReadOwner | QFile::WriteOwner);
    return true;
}

bool Registry::remember(const QString &path, const QString &reason, const QString &board,
                        const QString &boardDir, qint64 now, QString *error)
{
    if (error) error->clear();
    const QString normalized = normalize(path);
    if (normalized.isEmpty()) {
        if (error) *error = QStringLiteral("A project needs a path.");
        return false;
    }
    if (!isReason(reason)) {
        if (error) *error = QStringLiteral("\"%1\" is not one of the reasons a project becomes known.").arg(reason);
        return false;
    }
    const qint64 at = secondsOr(now);
    const int index = indexOf(normalized);
    if (index < 0) {
        Record record;
        record.path = normalized;
        record.key = keyFor(normalized);
        record.name = nameFor(normalized);
        record.board = board.isEmpty() ? QString::fromLatin1(kBoardNone) : board;
        record.boardDir = boardDir;
        record.reason = reason;
        record.knownSince = at;
        record.lastAttached = at;
        m_projects.append(record);
    } else {
        Record &record = m_projects[index];
        // `reason` and `knownSince` are what made Relay first notice the project; they are history
        // and are never rewritten by a later attachment.
        if (!board.isEmpty()) record.board = board;
        if (!boardDir.isEmpty()) record.boardDir = boardDir;
        if (record.key.isEmpty()) record.key = keyFor(normalized);
        if (record.name.isEmpty()) record.name = nameFor(normalized);
        record.lastAttached = at;
    }
    // The user just acted on it, so an older "do not make a Switchboard here" answer is out of date.
    const int declined = declinedIndexOf(normalized);
    if (declined >= 0) m_declined.removeAt(declined);
    return save(error);
}

bool Registry::forget(const QString &path, QString *error)
{
    if (error) error->clear();
    const QString normalized = normalize(path);
    const int index = indexOf(normalized);
    if (index >= 0) m_projects.removeAt(index);
    return save(error);
}

bool Registry::isKnown(const QString &path) const
{
    const QString normalized = normalize(path);
    if (normalized.isEmpty()) return false;
    if (indexOf(normalized) >= 0) return true;
    // A board folder in the project says "this is a project" on its own; it needs no record, and
    // asking the user to initialise what they have already set up would be noise.
    return !boardDirOf(normalized).isEmpty();
}

Record Registry::record(const QString &path) const
{
    const int index = indexOf(normalize(path));
    return index < 0 ? Record{} : m_projects.at(index);
}

QList<Record> Registry::knownProjects() const
{
    QList<Record> sorted = m_projects;
    std::stable_sort(sorted.begin(), sorted.end(), [](const Record &a, const Record &b) {
        if (a.lastAttached != b.lastAttached) return a.lastAttached > b.lastAttached;
        return a.path < b.path;   // a stable order for projects attached in the same second
    });
    return sorted;
}

bool Registry::decline(const QString &path, qint64 now, QString *error)
{
    if (error) error->clear();
    const QString normalized = normalize(path);
    if (normalized.isEmpty()) {
        if (error) *error = QStringLiteral("A declined project needs a path.");
        return false;
    }
    const int index = indexOf(normalized);
    if (index >= 0) m_projects.removeAt(index);   // the two lists never hold the same path
    const int declined = declinedIndexOf(normalized);
    if (declined >= 0) m_declined.removeAt(declined);
    m_declined.prepend({normalized, secondsOr(now)});
    return save(error);
}

bool Registry::undecline(const QString &path, QString *error)
{
    if (error) error->clear();
    const int index = declinedIndexOf(normalize(path));
    if (index >= 0) m_declined.removeAt(index);
    return save(error);
}

bool Registry::isDeclined(const QString &path) const
{
    const QString normalized = normalize(path);
    return !normalized.isEmpty() && declinedIndexOf(normalized) >= 0;
}

QStringList Registry::declined() const
{
    QList<QPair<QString, qint64>> sorted = m_declined;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const QPair<QString, qint64> &a, const QPair<QString, qint64> &b) {
                         if (a.second != b.second) return a.second > b.second;
                         return a.first < b.first;
                     });
    QStringList paths;
    paths.reserve(sorted.size());
    for (const auto &entry : sorted) paths << entry.first;
    return paths;
}

}  // namespace projects
}  // namespace relay
