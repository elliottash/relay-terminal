// SPDX-License-Identifier: GPL-3.0-or-later
#include "PaneStatus.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace relay::panestatus {

int urgency(State state) { return int(state); }

State mostUrgent(const QList<State> &states) {
    State out = State::Idle;
    for (State s : states) if (urgency(s) > urgency(out)) out = s;
    return out;
}

QString stateName(State state) {
    switch (state) {
    case State::Idle: return QStringLiteral("idle");
    case State::Running: return QStringLiteral("running");
    case State::Subagents: return QStringLiteral("subagents");
    case State::Working: return QStringLiteral("working");
    case State::Recommends: return QStringLiteral("recommends");
    case State::Done: return QStringLiteral("done");
    case State::Failed: return QStringLiteral("failed");
    case State::NeedsYou: return QStringLiteral("needs-you");
    }
    return {};
}

QString stateLabel(State state) {
    switch (state) {
    case State::Idle: return QStringLiteral("Terminal idle");
    case State::Running: return QStringLiteral("Command running");
    case State::Subagents: return QStringLiteral("Subagents working");
    case State::Working: return QStringLiteral("Agent working");
    case State::Recommends: return QStringLiteral("The agent suggests a command · it is in the prompt box");
    case State::Done: return QStringLiteral("Agent done");
    case State::Failed: return QStringLiteral("Agent turn failed");
    case State::NeedsYou: return QStringLiteral("Needs you");
    }
    return {};
}

State resolve(const Facts &facts, quint64 seenSerial) {
    if (facts.programAsking || facts.handoffWaiting) return State::NeedsYou;
    if (facts.agentBusy) return State::Working;
    if (facts.finishSerial > seenSerial) {
        if (facts.lastOutcome == QStringLiteral("error")) return State::Failed;
        if (facts.lastOutcome == QStringLiteral("done")) return facts.lastAsked ? State::NeedsYou : State::Done;
        // "cancelled": the user stopped it, which is not news to them.
    }
    if (facts.handoffOffered) return State::Recommends;
    if (facts.liveSubagents > 0) return State::Subagents;
    if (facts.processBusy) return State::Running;
    return State::Idle;
}

bool endsWithQuestion(const QString &reply) {
    const QStringList lines = reply.split(QLatin1Char('\n'));
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        QString line = it->trimmed();
        if (line.isEmpty() || line.startsWith(QStringLiteral("```"))) {
            if (line.isEmpty()) continue;
            return false;   // a reply that ends in a code block is not asking
        }
        static const QString closers = QStringLiteral("*_`\"')]»”’ ");
        while (!line.isEmpty() && closers.contains(line.back())) line.chop(1);
        return line.endsWith(QLatin1Char('?')) || line.endsWith(QChar(0xff1f));
    }
    return false;
}

// ----- what the pane is waiting for (cards #V7QD, #KP4M) ----------------------------------------
namespace {
// ". . ." grown one character at a time and padded back out, so nothing beside it shifts.
QString waitingDots(int phase) {
    static const QString dots = QStringLiteral(". . .");
    const int shown = phase < 0 ? dots.size() : QList<int>{0, 1, 3, 5}.value(phase % 4);
    return dots.left(shown).leftJustified(dots.size());
}

QString countOf(int n, const QString &noun) {
    return QStringLiteral("%1 %2%3").arg(n).arg(noun, n == 1 ? QString() : QStringLiteral("s"));
}
}  // namespace

QString waitingSubject(const Waiting &waiting) {
    QStringList parts;
    if (waiting.subagents > 0 && (waiting.onSubagents || !waiting.mainBusy))
        parts << countOf(waiting.subagents, QStringLiteral("subagent"));
    if (waiting.jobs > 0 && (waiting.onJobs || !waiting.mainBusy))
        parts << countOf(waiting.jobs, QStringLiteral("job"));
    return parts.join(QStringLiteral(", "));
}

bool isWaiting(const Waiting &waiting) { return !waitingSubject(waiting).isEmpty(); }

QString waitingLine(const Waiting &waiting, int phase) {
    const QString subject = waitingSubject(waiting);
    if (subject.isEmpty()) return {};
    return QStringLiteral("waiting for %1 %2").arg(subject, waitingDots(phase));
}

QStringList waitingLines(const Waiting &waiting, int phase) {
    const QString subject = waitingSubject(waiting);
    if (subject.isEmpty()) return {};
    const QString dots = waitingDots(phase);
    // The narrowest rung still has to mean something: with one kind the count alone does ("2 . . ."
    // beside a turn clock that spells it out), but "2, 1 . . ." would not, so that says "waiting".
    const QString shortest = subject.contains(QLatin1Char(','))
                                 ? QStringLiteral("waiting %1").arg(dots)
                                 : QStringLiteral("%1 %2").arg(subject.section(QLatin1Char(' '), 0, 0), dots);
    return {QStringLiteral("waiting for %1 %2").arg(subject, dots),
            QStringLiteral("%1 %2").arg(subject, dots), shortest};
}

bool isRemoteProgram(const QString &programName) {
    static const QSet<QString> names{QStringLiteral("ssh"), QStringLiteral("mosh"), QStringLiteral("mosh-client"),
                                     QStringLiteral("telnet"), QStringLiteral("autossh")};
    return names.contains(programName);
}

QString remoteHost(const QString &commandLine) {
    const QStringList words = commandLine.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.isEmpty()) return {};
    const QString program = QFileInfo(words.first()).fileName();
    QStringList args = words.mid(1);
    if (program == QStringLiteral("mosh-client")) {
        // mosh execs "mosh-client -# 'the original arguments' IP PORT".
        const int hash = int(args.indexOf(QStringLiteral("-#")));
        if (hash >= 0 && hash + 1 < args.size()) {
            QString original = args.mid(hash + 1).join(QLatin1Char(' '));
            original.remove(QLatin1Char('\'')).remove(QLatin1Char('"'));
            return remoteHost(QStringLiteral("mosh ") + original);
        }
        return args.isEmpty() ? QString() : args.first();
    }
    // Options that take a value, per program. Anything else starting with '-' is a flag.
    QString valued;
    if (program == QStringLiteral("ssh") || program == QStringLiteral("autossh"))
        valued = QStringLiteral("BbcDEeFIiJLlMmOoPpQRSWw");
    else if (program == QStringLiteral("mosh"))
        // mosh -p PORT; the rest are --long=value. mosh-client's -# string has lost its quotes, so
        // the words of --ssh="ssh -o ControlPath=…" (Relay's wrapper, #S5SH) arrive loose: read
        // them with ssh's valued letters, which include p, so "-o X" is never taken for the host.
        valued = QStringLiteral("BbcDEeFIiJLlMmOoPpQRSWw");
    else if (program == QStringLiteral("telnet"))
        valued = QStringLiteral("bEeklnSX");
    else
        return {};
    QString user, host;
    for (int i = 0; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--")) { if (i + 1 < args.size() && host.isEmpty()) host = args.at(i + 1); break; }
        if (arg.startsWith(QStringLiteral("--"))) {
            // mosh --ssh="ssh -p 2222": the value may continue over the next words until a quote closes.
            if (arg.count(QLatin1Char('"')) == 1) while (++i < args.size() && !args.at(i).contains(QLatin1Char('"'))) {}
            continue;
        }
        if (arg.startsWith(QLatin1Char('-')) && arg.size() > 1) {
            // A cluster like -tt or -vp2222: a valued letter takes the rest, or the next word.
            for (int k = 1; k < arg.size(); ++k) {
                const QChar letter = arg.at(k);
                if (!valued.contains(letter)) continue;
                const QString value = k + 1 < arg.size() ? arg.mid(k + 1) : (i + 1 < args.size() ? args.at(++i) : QString());
                if (letter == QLatin1Char('l')) user = value;   // ssh -l me, telnet -l me
                break;
            }
            continue;
        }
        host = arg;
        break;
    }
    if (host.startsWith(QStringLiteral("ssh://"))) {
        host = host.mid(6);
        const int slash = int(host.indexOf(QLatin1Char('/')));
        if (slash >= 0) host = host.left(slash);
        const int colon = int(host.lastIndexOf(QLatin1Char(':')));
        if (colon > host.indexOf(QLatin1Char('@'))) host = host.left(colon);
    }
    if (host.isEmpty()) return {};
    if (!user.isEmpty() && !host.contains(QLatin1Char('@'))) host = user + QLatin1Char('@') + host;
    return host;
}

ColourMode colourModeFrom(const QString &value) {
    if (value == QStringLiteral("group")) return ColourMode::ByGroup;
    if (value == QStringLiteral("off")) return ColourMode::Off;
    return ColourMode::ByType;
}

QString colourModeId(ColourMode mode) {
    switch (mode) {
    case ColourMode::ByType: return QStringLiteral("type");
    case ColourMode::ByGroup: return QStringLiteral("group");
    case ColourMode::Off: return QStringLiteral("off");
    }
    return QStringLiteral("type");
}

QStringList colourModeIds() { return {QStringLiteral("type"), QStringLiteral("group"), QStringLiteral("off")}; }
QStringList colourModeLabels() { return {QStringLiteral("By type"), QStringLiteral("By group"), QStringLiteral("Off")}; }

namespace {
double channel(double c) {
    c /= 255.0;
    return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}
double luminance(const QColor &c) {
    return 0.2126 * channel(c.red()) + 0.7152 * channel(c.green()) + 0.0722 * channel(c.blue());
}

// Moves `color` towards black or white, whichever is further from the ground, until it reaches
// `ratio` against it. Colours that already do are returned as they are.
QColor atLeast(const QColor &color, const QColor &ground, double ratio) {
    if (contrast(color, ground) >= ratio) return color;
    const QColor pole = isLight(ground) ? QColor(Qt::black) : QColor(Qt::white);
    for (int step = 1; step <= 20; ++step) {
        const QColor tried = mix(pole, color, step / 20.0);
        if (contrast(tried, ground) >= ratio) return tried;
    }
    return pole;
}

struct Kind { QString type, label, group; Glyph glyph; };
const QList<Kind> &kinds() {
    static const QList<Kind> list{
        {QStringLiteral("board"), QStringLiteral("Switchboard"), QStringLiteral("tools"), Glyph::Switchboard},
        // The Settings pane splits into Options and Actions (2026-09-18); "settings" is styled as
        // Options until nothing sets it any more.
        {QStringLiteral("options"), QStringLiteral("Options"), QStringLiteral("tools"), Glyph::Options},
        {QStringLiteral("settings"), QStringLiteral("Options"), QStringLiteral("tools"), Glyph::Options},
        {QStringLiteral("actions"), QStringLiteral("Actions"), QStringLiteral("tools"), Glyph::Actions},
        {QStringLiteral("sessions"), QStringLiteral("Sessions"), QStringLiteral("tools"), Glyph::Sessions},
        // Multiplayer (#W5N2): who is on the panes you are sharing, and what is waiting for you.
        {QStringLiteral("sharing"), QStringLiteral("Sharing"), QStringLiteral("tools"), Glyph::Phone},
        {QStringLiteral("subagent"), QStringLiteral("Subagent"), QStringLiteral("agents"), Glyph::Subagent},
        {QStringLiteral("turn"), QStringLiteral("Agent turn"), QStringLiteral("agents"), Glyph::Turn},
    };
    return list;
}

// Plain on purpose: a terminal is the default look, and a file pane's own first row is its header.
bool plainType(const QString &type) {
    static const QSet<QString> plain{QString(), QStringLiteral("terminal"), QStringLiteral("explorer"),
                                     QStringLiteral("preview"), QStringLiteral("plan"), QStringLiteral("diff")};
    return plain.contains(type);
}

TypeStyle tinted(const QColor &hue, const Tokens &t, double strength) {
    TypeStyle style;
    style.band = true;
    style.fill = mix(hue, t.background, strength);
    style.line = mix(hue, t.background, 0.45);
    style.ink = atLeast(hue, style.fill, 3.0);
    style.text = atLeast(mix(hue, t.text, 0.3), style.fill, 4.5);
    return style;
}

double tintStrength(const Tokens &t) { return isLight(t.background) ? 0.10 : 0.13; }
}  // namespace

TypeStyle typeStyle(const QString &paneType, ColourMode mode, const Tokens &tokens, const QString &label) {
    if (plainType(paneType)) return {};
    Kind kind{paneType, QString(), QStringLiteral("tools"), Glyph::Tool};
    for (const Kind &k : kinds()) if (k.type == paneType) { kind = k; break; }
    if (kind.label.isEmpty()) {
        kind.label = paneType;
        kind.label.replace(QLatin1Char('-'), QLatin1Char(' '));
        if (!kind.label.isEmpty()) kind.label[0] = kind.label.at(0).toUpper();
    }
    // By type: brass for the Switchboard (docs/SWITCHBOARD-AESTHETIC.md), green for Options and
    // Actions (one Settings pane until 2026-09-18, so one tint; the glyph tells them apart), the
    // terminal's own blue for Sessions (they are the terminals' conversations), violet for
    // everything the agent does. A fifth hue would have to come from outside the theme's own
    // colours. By group: every tool pane brass, every agent pane violet.
    // Off: the band stays, because it is the pane's header and its name (the Options pane draws
    // no title of its own), but in the theme's neutral ink rather than a hue.
    QColor hue;
    if (mode == ColourMode::Off) hue = tokens.muted;
    else if (kind.group == QStringLiteral("agents")) hue = tokens.agent;
    else if (mode == ColourMode::ByGroup) hue = tokens.warning;
    else if (kind.glyph == Glyph::Options || kind.glyph == Glyph::Actions) hue = tokens.success;
    else if (kind.type == QStringLiteral("sessions")) hue = tokens.shell;
    else hue = tokens.warning;
    TypeStyle style = tinted(hue, tokens, mode == ColourMode::Off ? 0.06 : tintStrength(tokens));
    style.label = label.isEmpty() ? kind.label : label;
    style.glyph = kind.glyph;
    style.group = kind.group;
    return style;
}

TypeStyle remoteStyle(const Tokens &tokens) {
    // Stronger than a type tint, and in the one hue no pane type uses: this is where your typing goes.
    TypeStyle style = tinted(tokens.error, tokens, isLight(tokens.background) ? 0.13 : 0.18);
    style.line = mix(tokens.error, tokens.background, 0.8);
    style.glyph = Glyph::Remote;
    style.group = QStringLiteral("remote");
    return style;
}

TypeStyle phoneStyle(const Tokens &tokens) {
    TypeStyle style = tinted(tokens.shell, tokens, isLight(tokens.background) ? 0.12 : 0.16);
    style.glyph = Glyph::Phone;
    style.group = QStringLiteral("phone");
    style.label = QStringLiteral("Phone");
    return style;
}

QColor stateInk(State state, const Tokens &t) {
    switch (state) {
    case State::Idle: return t.muted;
    case State::Running: return t.shell;
    case State::Subagents: case State::Working: return t.agent;
    case State::Recommends: return t.shell;
    case State::Done: return t.success;
    case State::Failed: return t.error;
    case State::NeedsYou: return t.warning;
    }
    return t.muted;
}

double contrast(const QColor &a, const QColor &b) {
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

QColor mix(const QColor &a, const QColor &b, double weightOfA) {
    const double w = std::clamp(weightOfA, 0.0, 1.0);
    return QColor(int(std::lround(a.red() * w + b.red() * (1 - w))),
                  int(std::lround(a.green() * w + b.green() * (1 - w))),
                  int(std::lround(a.blue() * w + b.blue() * (1 - w))));
}

bool isLight(const QColor &background) { return luminance(background) > 0.35; }

}  // namespace relay::panestatus
