// SPDX-License-Identifier: AGPL-3.0-or-later
// The rules behind src/AgentContext.h: the label a keyed action wears, which letters a row may
// answer, and the exact bytes of the `context` block that crosses to the worker.
#include "AgentContext.h"

#include <QJsonValue>
#include <QSet>

namespace relay::agent {

namespace {

// The wire spellings, in one place, so a typo cannot make `toJson` and `fromJson` disagree
// quietly. They are snake_case because the protocol is (protocol 1).
const QString kName = QStringLiteral("name");
const QString kSurface = QStringLiteral("surface");
const QString kAgentRole = QStringLiteral("agent_role");
const QString kWorkspace = QStringLiteral("workspace");
const QString kScope = QStringLiteral("scope");
const QString kShell = QStringLiteral("shell");
const QString kRouting = QStringLiteral("routing");
const QString kPersist = QStringLiteral("persist");
const QString kBrief = QStringLiteral("brief");
const QString kKey = QStringLiteral("key");
const QString kTitle = QStringLiteral("title");
const QString kScreen = QStringLiteral("screen");
const QString kReadonly = QStringLiteral("readonly");

} // namespace

// ---- Action ---------------------------------------------------------------------------------

QString Action::fullLabel() const
{
    const QString key = letter.trimmed();
    if (key.isEmpty())
        return label;
    return QStringLiteral("%1 (%2)").arg(label, key);
}

QString labelWithoutKey(const QString &label)
{
    const int at = label.lastIndexOf(QStringLiteral(" ("));
    return at > 0 && label.endsWith(QLatin1Char(')')) ? label.left(at) : label;
}

QStringList placeholderRungs(const QString &line)
{
    const QString top = line.trimmed();
    if (top.isEmpty())
        return {};
    QStringList rungs{top};
    for (QString rung = top;;) {
        const int comma = rung.lastIndexOf(QStringLiteral(", "));
        if (comma <= 0)
            break;
        rung = rung.left(comma);
        rungs << rung;
    }
    if (const int dash = top.indexOf(QStringLiteral(" \u2014 ")); dash > 0)
        rungs << top.left(dash);
    rungs << QStringLiteral("\u2026");
    rungs.removeDuplicates();
    return rungs;
}

QList<Action> withUniqueLetters(QList<Action> actions)
{
    QSet<QString> taken;
    for (Action &action : actions) {
        const QString letter = action.letter.trimmed();
        // One letter, and one only: "xx" or "Ctrl+X" is not a row letter, and silently clearing it
        // is better than answering a key nobody can see on the button.
        if (letter.size() != 1) {
            action.letter.clear();
            continue;
        }
        const QString folded = letter.toLower();
        if (taken.contains(folded)) {
            action.letter.clear();
            continue;
        }
        taken.insert(folded);
        action.letter = letter;
    }
    return actions;
}

int actionForLetter(const QList<Action> &actions, const QString &letter)
{
    const QString wanted = letter.trimmed().toLower();
    if (wanted.size() != 1)
        return -1;
    for (int i = 0; i < actions.size(); ++i) {
        const Action &action = actions.at(i);
        if (action.letter.trimmed().toLower() != wanted)
            continue;
        return action.enabled ? i : -1;
    }
    return -1;
}

// ---- ContextSpec ----------------------------------------------------------------------------

QJsonObject ContextSpec::toJson() const
{
    QJsonObject json;
    json.insert(kName, name);
    json.insert(kSurface, surface.isEmpty() ? name : surface);
    json.insert(kAgentRole, agentRole);
    json.insert(kWorkspace, workspace);
    json.insert(kScope, scope);
    json.insert(kShell, shell);
    json.insert(kRouting, routing);
    // No key, no store: a worker that is sent no `persist` keeps one conversation for as long as
    // it lives and writes nothing down, which is exactly what protocol 30.7 says a `configure`
    // with no tab means. An empty scope with a key is a different thing — a board-less tab — and
    // is sent.
    if (!persistKey.isEmpty()) {
        QJsonObject persist;
        persist.insert(kScope, persistScope);
        persist.insert(kKey, persistKey);
        json.insert(kPersist, persist);
    }
    if (!briefKey.isEmpty() || !briefTitle.isEmpty()) {
        QJsonObject brief;
        brief.insert(kKey, briefKey);
        brief.insert(kTitle, briefTitle);
        json.insert(kBrief, brief);
    }
    return json;
}

ContextSpec ContextSpec::fromJson(const QJsonObject &json)
{
    ContextSpec spec;
    spec.name = json.value(kName).toString();
    spec.surface = json.value(kSurface).toString();
    spec.agentRole = json.value(kAgentRole).toString();
    spec.workspace = json.value(kWorkspace).toString();
    spec.scope = json.value(kScope).toString();
    spec.shell = json.value(kShell).toBool();
    spec.routing = json.value(kRouting).toString();
    const QJsonObject persist = json.value(kPersist).toObject();
    spec.persistScope = persist.value(kScope).toString();
    spec.persistKey = persist.value(kKey).toString();
    const QJsonObject brief = json.value(kBrief).toObject();
    spec.briefKey = brief.value(kKey).toString();
    spec.briefTitle = brief.value(kTitle).toString();
    // `screen` and `readonly` are the turn's, but a spec read back from a request that carried
    // both blocks should not lose them — `fromJson` takes them when they are there.
    spec.screen = json.value(kScreen).toString();
    spec.readonly = json.value(kReadonly).toBool();
    return spec;
}

QJsonObject ContextSpec::askFields() const
{
    QJsonObject json;
    const QString id = surface.isEmpty() ? name : surface;
    if (!id.isEmpty())
        json.insert(kSurface, id);
    const QString hint = screen.trimmed();
    if (!hint.isEmpty())
        json.insert(kScreen, hint.left(kScreenLimit));
    if (readonly)
        json.insert(kReadonly, true);
    return json;
}

QString ContextSpec::persistId() const
{
    if (persistKey.isEmpty())
        return {};
    return persistScope + QChar(u'\x1f') + persistKey;
}

bool ContextSpec::operator==(const ContextSpec &other) const
{
    return name == other.name && surface == other.surface && agentRole == other.agentRole
           && workspace == other.workspace && scope == other.scope
           && persistScope == other.persistScope && persistKey == other.persistKey
           && briefKey == other.briefKey && briefTitle == other.briefTitle
           && screen == other.screen && readonly == other.readonly && shell == other.shell
           && routing == other.routing;
}

// ---- TurnRecord -----------------------------------------------------------------------------

QString TurnRecord::turnRef() const
{
    if (sessionId.isEmpty() || turnId.isEmpty())
        return {};
    return sessionId + QLatin1Char('/') + turnId;
}

} // namespace relay::agent
