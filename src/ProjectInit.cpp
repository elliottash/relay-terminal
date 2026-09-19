// SPDX-License-Identifier: GPL-3.0-or-later
#include "ProjectInit.h"

#include "Projects.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>

namespace relay {
namespace projectinit {
namespace {

const QLatin1String kTrackers("trackers");
const QLatin1String kHints("hints");
const QLatin1String kGit("git");
const QLatin1String kBoard("board");
const QLatin1String kCounts("counts");
const QLatin1String kProject("project");
const QLatin1String kKind("kind");
const QLatin1String kPath("path");
const QLatin1String kCount("count");
const QLatin1String kSummary("summary");
const QLatin1String kDetail("detail");
const QLatin1String kRemotes("remotes");
const QLatin1String kPrimary("primary");
const QLatin1String kPrimaryReason("primary_reason");
const QLatin1String kName("name");
const QLatin1String kForge("forge");
const QLatin1String kOwner("owner");
const QLatin1String kRepo("repo");
const QLatin1String kHost("host");
const QLatin1String kCards("cards");
const QLatin1String kConvertible("convertible");
const QLatin1String kItems("items");

// The board folder's name, so a change to `projects::kBoardFolder` moves both at once.
QString folderName() { return QString::fromLatin1(relay::projects::kBoardFolder); }

QString trimmedString(const QJsonValue &value)
{
    return value.isString() ? value.toString().trimmed() : QString();
}

}  // namespace

// ----- who is asking ------------------------------------------------------------------------------

QString triggerName(Trigger trigger)
{
    switch (trigger) {
    case Trigger::AgentWork: return QString::fromLatin1(relay::projects::kReasonAgentWork);
    case Trigger::AgentCard: return QString::fromLatin1(relay::projects::kReasonAgentCard);
    case Trigger::Switchboard: return QString::fromLatin1(relay::projects::kReasonSwitchboard);
    case Trigger::CardCommand: return QString::fromLatin1(relay::projects::kReasonCardCommand);
    case Trigger::InitCommand: return QString::fromLatin1(relay::projects::kReasonInitCommand);
    }
    return QString::fromLatin1(relay::projects::kReasonAgentWork);
}

QStringList triggerNames()
{
    return {triggerName(Trigger::AgentWork), triggerName(Trigger::AgentCard),
            triggerName(Trigger::Switchboard), triggerName(Trigger::CardCommand),
            triggerName(Trigger::InitCommand)};
}

bool parseTrigger(const QString &name, Trigger *out)
{
    for (Trigger trigger : {Trigger::AgentWork, Trigger::AgentCard, Trigger::Switchboard,
                            Trigger::CardCommand, Trigger::InitCommand}) {
        if (triggerName(trigger) != name) continue;
        if (out) *out = trigger;
        return true;
    }
    return false;
}

// ----- when it is asked ---------------------------------------------------------------------------

Decision decide(Trigger trigger, const Situation &situation)
{
    Decision decision;
    // A guest is looking at somebody else's pane over the wire. The events behind this question are
    // desktop-only (remote/wire.py), the answer would be somebody else's to give, and the folder
    // would appear on a machine the guest cannot see. It is never offered there.
    if (situation.remote) return decision;
    // One question at a time in a pane: a second trigger while the first is still on screen would
    // replace the findings under the user's cursor.
    if (situation.asking) return decision;

    decision.project = situation.project;
    bool hasBoard = situation.hasBoard;
    bool declined = situation.declined;
    if (decision.project.isEmpty()) {
        // `/init` is the one trigger that works with no candidate at all: it offers to treat the
        // pane's own directory as the project, which is how a brand-new folder gets a Switchboard.
        // Nothing else ever asks outside a project — ~/Downloads must stay silent.
        if (trigger != Trigger::InitCommand) return decision;
        if (situation.cwd.isEmpty()) {
            decision.outcome = Outcome::Say;
            decision.message = QStringLiteral("No directory to initialize a project in.");
            return decision;
        }
        decision.project = situation.cwd;
        // `hasBoard`/`declined` were answered about the (empty) candidate, so they say nothing
        // about the directory being offered instead.
        hasBoard = false;
        declined = false;
    }

    decision.reason = triggerName(trigger);
    if (hasBoard) {
        // Nothing to create and nothing to ask: the explicit command says so, the rest attach in
        // silence exactly as they did before this question existed.
        decision.outcome = Outcome::Attach;
        if (trigger == Trigger::InitCommand)
            decision.message = QStringLiteral("%1 already has a Switchboard.")
                                   .arg(relay::projects::nameFor(decision.project));
        return decision;
    }
    if (trigger == Trigger::InitCommand) {
        // The explicit command overrules a remembered no — typing `/init` is changing your mind.
        decision.outcome = Outcome::Ask;
        decision.clearsDecline = true;
        return decision;
    }
    // "No, do not make a Switchboard here", remembered on disk: nothing asks again, in any tab,
    // after any restart. Only `/init` above gets past this.
    if (declined) return decision;
    // "Not now" is not a no. It stops the quiet trigger — the agent prompt — for the rest of this
    // Relay session, and every explicit act the user performs asks again.
    if (situation.snoozed && trigger == Trigger::AgentWork) return decision;
    decision.outcome = Outcome::Ask;
    return decision;
}

// ----- what the question shows ---------------------------------------------------------------------

QString titleLine() { return QStringLiteral("Initialize a project and create a Switchboard here?"); }

QString boardFolderFor(const QString &project)
{
    if (project.isEmpty()) return {};
    QString root = project;
    while (root.size() > 1 && root.endsWith(QLatin1Char('/'))) root.chop(1);
    return root + QLatin1Char('/') + folderName();
}

QString folderLineFor(const QString &project)
{
    if (project.isEmpty()) return {};
    return QStringLiteral("Creates %1/ — and nothing else.").arg(boardFolderFor(project));
}

Question questionFrom(const QJsonObject &probeResult)
{
    Question question;
    question.project = trimmedString(probeResult.value(kProject));
    question.folder = boardFolderFor(question.project);
    question.title = titleLine();
    question.folderLine = folderLineFor(question.project);
    question.items = probeResult.value(kCounts).toObject().value(kItems).toInt();

    // The trackers: one unticked checkbox each, in the probe's own (kind, path) order. The label is
    // the probe's own `summary`, which is written for this dialog — "23 task(s), 20 not done; 3 in
    // completed/ and archive/ are left alone" says more than Relay could reconstruct from a count.
    for (const QJsonValue &value : probeResult.value(kTrackers).toArray()) {
        const QJsonObject tracker = value.toObject();
        Finding finding;
        finding.kind = trimmedString(tracker.value(kKind));
        finding.path = trimmedString(tracker.value(kPath));
        finding.count = tracker.value(kCount).toInt();
        finding.text = trimmedString(tracker.value(kSummary));
        if (finding.text.isEmpty())
            finding.text = QStringLiteral("%1 item(s) in %2").arg(finding.count).arg(finding.path);
        finding.importable = !finding.kind.isEmpty();
        if (finding.importable) question.imports.append(finding);
    }

    // An older `issues/` tree that is not a board yet. It is *not* an import: converting it happens
    // in place and is its own operation (docs/SWITCHBOARD-FORMAT.md section 6), so the question says
    // it is there and that a yes leaves it alone.
    const QJsonObject board = probeResult.value(kBoard).toObject();
    if (trimmedString(board.value(kKind)) == QStringLiteral("pre-board")) {
        Finding finding;
        finding.kind = QStringLiteral("pre-board");
        finding.path = trimmedString(board.value(kPath));
        finding.count = board.value(kCards).toInt();
        const int convertible = board.value(kConvertible).toInt();
        finding.text = QStringLiteral("an existing %1 tree (%2 file(s), %3 card-like) — left exactly as it is")
                           .arg(finding.path.isEmpty() ? QStringLiteral("issues/") : finding.path)
                           .arg(finding.count)
                           .arg(convertible);
        question.notes.append(finding);
    }

    // The primary remote, when it is a GitHub of some kind. Shown, never acted on: the first sync
    // always shows its dry-run plan first, and this question does not start one.
    const QJsonObject git = probeResult.value(kGit).toObject();
    const QString primary = trimmedString(git.value(kPrimary));
    for (const QJsonValue &value : git.value(kRemotes).toArray()) {
        const QJsonObject remote = value.toObject();
        if (trimmedString(remote.value(kName)) != primary) continue;
        const QString forge = trimmedString(remote.value(kForge));
        if (forge != QStringLiteral("github") && forge != QStringLiteral("github-enterprise")) break;
        const QString owner = trimmedString(remote.value(kOwner));
        const QString repo = trimmedString(remote.value(kRepo));
        if (owner.isEmpty() || repo.isEmpty()) break;
        Finding finding;
        finding.kind = QStringLiteral("github-remote");
        finding.path = trimmedString(remote.value(kHost));
        finding.count = 1;
        finding.text = forge == QStringLiteral("github")
                           ? QStringLiteral("On GitHub as %1/%2").arg(owner, repo)
                           : QStringLiteral("On %1 as %2/%3").arg(finding.path, owner, repo);
        question.notes.append(finding);
        const QString why = trimmedString(git.value(kPrimaryReason));
        if (!why.isEmpty()) {
            Finding reason;
            reason.kind = QStringLiteral("github-primary");
            reason.path = finding.path;
            reason.text = why;
            question.notes.append(reason);
        }
        break;
    }

    // Everything else the probe found and will not import: issue templates, a ticket key in the
    // branch name, a tracker Relay cannot read. Each carries its own sentence.
    for (const QJsonValue &value : probeResult.value(kHints).toArray()) {
        const QJsonObject hint = value.toObject();
        Finding finding;
        finding.kind = trimmedString(hint.value(kKind));
        finding.path = trimmedString(hint.value(kPath));
        finding.count = hint.value(kCount).toInt();
        finding.text = trimmedString(hint.value(kDetail));
        if (finding.text.isEmpty()) continue;
        question.notes.append(finding);
    }
    return question;
}

QStringList importKinds(const QList<Finding> &findings, const QList<bool> &ticked)
{
    QSet<QString> kinds;
    for (int i = 0; i < findings.size(); ++i) {
        if (i >= ticked.size() || !ticked.at(i)) continue;
        const Finding &finding = findings.at(i);
        if (!finding.importable || finding.kind.isEmpty()) continue;
        kinds.insert(finding.kind);
    }
    QStringList sorted(kinds.cbegin(), kinds.cend());
    sorted.sort();
    return sorted;
}

QString createdLine(const QString &project, int imported)
{
    const QString folder = boardFolderFor(project);
    if (folder.isEmpty()) return {};
    QString line = QStringLiteral("Switchboard created in %1/").arg(folder);
    if (imported > 0) line += QStringLiteral(" · %1 card(s) imported").arg(imported);
    else if (imported == 0) line += QStringLiteral(" · nothing left to import");
    return line;
}

QString declinedLine(const QString &project)
{
    return QStringLiteral("No Switchboard in %1. Relay will not ask again; /init creates one.")
        .arg(relay::projects::nameFor(project));
}

QString notNowLine(const QString &project)
{
    return QStringLiteral("Not now. /init creates a Switchboard in %1 when you want one.")
        .arg(relay::projects::nameFor(project));
}

}  // namespace projectinit
}  // namespace relay
