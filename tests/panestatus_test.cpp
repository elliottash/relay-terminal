// SPDX-License-Identifier: GPL-3.0-or-later
// Pane types, pane states and remote sessions (cards #SPBN and #XM0T): the urgency order a tab
// uses, how a pane's facts become one state, the ssh/mosh/telnet destination, and that every
// tint keeps its glyph and its label legible in every shipped theme.
#include "PaneStatus.h"
#include "ThemeFile.h"

#include <QDir>
#include <QFile>
#include <QTest>

#include <array>
#include <cmath>

using namespace relay::panestatus;

namespace {
Tokens tokensOf(const relay::theme::ThemeSpec &spec) {
    Tokens t;
    t.background = spec.uiColor(QStringLiteral("background"));
    t.text = spec.uiColor(QStringLiteral("text"));
    t.muted = spec.uiColor(QStringLiteral("text_muted"));
    t.shell = spec.uiColor(QStringLiteral("shell"));
    t.agent = spec.uiColor(QStringLiteral("agent"));
    t.success = spec.uiColor(QStringLiteral("success"));
    t.warning = spec.uiColor(QStringLiteral("warning"));
    t.error = spec.uiColor(QStringLiteral("error"));
    t.action = spec.uiColor(QStringLiteral("action"));
    return t;
}

// CIELAB dE76, the same arithmetic tests/theme_test.cpp measures the token palette with.
double deltaE(const QColor &a, const QColor &b) {
    const auto lab = [](const QColor &c) {
        const auto lin = [](int v) {
            const double s = v / 255.0;
            return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
        };
        const double r = lin(c.red()), g = lin(c.green()), bl = lin(c.blue());
        const double x = (0.4124 * r + 0.3576 * g + 0.1805 * bl) / 0.95047;
        const double y = 0.2126 * r + 0.7152 * g + 0.0722 * bl;
        const double z = (0.0193 * r + 0.1192 * g + 0.9505 * bl) / 1.08883;
        const auto f = [](double t) { return t > 0.008856 ? std::cbrt(t) : 7.787 * t + 16.0 / 116.0; };
        return std::array<double, 3>{116 * f(y) - 16, 500 * (f(x) - f(y)), 200 * (f(y) - f(z))};
    };
    const auto p = lab(a), q = lab(b);
    return std::sqrt(std::pow(p[0] - q[0], 2) + std::pow(p[1] - q[1], 2) + std::pow(p[2] - q[2], 2));
}

QList<relay::theme::ThemeSpec> shippedThemes() {
    QList<relay::theme::ThemeSpec> out;
    const QDir dir(QStringLiteral("data/theme/themes"));
    for (const QString &name : dir.entryList({QStringLiteral("*.toml")}, QDir::Files, QDir::Name)) {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly)) continue;
        out << relay::theme::parseTheme(QString::fromUtf8(file.readAll()), QFileInfo(name).completeBaseName(),
                                        relay::theme::builtinDark());
    }
    return out;
}
}  // namespace

class PaneStatusTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void urgencyOrderIsTheCardsDecision() {
        const QList<State> order{State::Idle, State::Running, State::Subagents, State::Working,
                                 State::Recommends, State::Done, State::Failed, State::NeedsYou};
        for (int i = 1; i < order.size(); ++i) QVERIFY(urgency(order.at(i)) > urgency(order.at(i - 1)));
        QCOMPARE(mostUrgent({State::Running, State::Done, State::Working}), State::Done);
        QCOMPARE(mostUrgent({State::Idle, State::NeedsYou, State::Failed}), State::NeedsYou);
        QCOMPARE(mostUrgent({}), State::Idle);
        QCOMPARE(stateName(State::NeedsYou), QStringLiteral("needs-you"));
    }

    // Cards #V7QD and #KP4M: "waiting for 2 subagents, 1 job . . ." in the prompt box. The rule,
    // the wording of the mixed case, and the padding that keeps the line still while it animates.
    void whatThePaneIsWaitingFor() {
        Waiting w;
        QVERIFY(!isWaiting(w));                       // nothing running, nothing to say
        w.subagents = 3; w.mainBusy = true;
        QVERIFY(!isWaiting(w));                       // busy doing something else, not waiting
        QVERIFY(waitingLine(w, 3).isEmpty());
        w.onSubagents = true;                         // an agent_wait, or a foreground subagent
        QCOMPARE(waitingLine(w, 3), QStringLiteral("waiting for 3 subagents . . ."));
        w = Waiting{}; w.jobs = 2; w.mainBusy = true;
        QVERIFY(!isWaiting(w));
        w.onJobs = true;                              // a command_output waiting on a job
        QCOMPARE(waitingLine(w, 3), QStringLiteral("waiting for 2 jobs . . ."));
        // The turn ended: whatever is still running is what it waits for, blocked or not.
        w = Waiting{}; w.subagents = 1; w.jobs = 1;
        QCOMPARE(waitingLine(w, 3), QStringLiteral("waiting for 1 subagent, 1 job . . ."));
        // Blocked on one kind while the other merely runs: only what blocks it is named.
        w = Waiting{}; w.subagents = 2; w.jobs = 4; w.mainBusy = true; w.onJobs = true;
        QCOMPARE(waitingLine(w, 3), QStringLiteral("waiting for 4 jobs . . ."));
        w.onSubagents = true;
        QCOMPARE(waitingLine(w, 3), QStringLiteral("waiting for 2 subagents, 4 jobs . . ."));

        // The dots grow with the phase and wrap, and every phase is the same width, so the line
        // never jiggles. A negative phase is "no animation": the dots are drawn in full.
        Waiting one; one.jobs = 1; one.onJobs = true; one.mainBusy = true;
        const QStringList phases{waitingLine(one, 0), waitingLine(one, 1), waitingLine(one, 2), waitingLine(one, 3)};
        QCOMPARE(phases.at(0), QStringLiteral("waiting for 1 job      "));
        QCOMPARE(phases.at(1), QStringLiteral("waiting for 1 job .    "));
        QCOMPARE(phases.at(2), QStringLiteral("waiting for 1 job . .  "));
        QCOMPARE(phases.at(3), QStringLiteral("waiting for 1 job . . ."));
        for (const QString &text : phases) QCOMPARE(text.size(), phases.at(3).size());
        QCOMPARE(waitingLine(one, 4), phases.at(0));
        QCOMPARE(waitingLine(one, -1), phases.at(3));

        // The narrow-pane rungs, longest first. One kind keeps its count; the mixed line cannot be
        // cut to "2, 4", so its last rung says "waiting".
        Waiting jobs; jobs.jobs = 2; jobs.onJobs = true; jobs.mainBusy = true;
        QCOMPARE(waitingLines(jobs, 3), (QStringList{QStringLiteral("waiting for 2 jobs . . ."),
                                                     QStringLiteral("2 jobs . . ."), QStringLiteral("2 . . .")}));
        Waiting subs; subs.subagents = 3; subs.onSubagents = true; subs.mainBusy = true;
        QCOMPARE(waitingLines(subs, 3), (QStringList{QStringLiteral("waiting for 3 subagents . . ."),
                                                     QStringLiteral("3 subagents . . ."), QStringLiteral("3 . . .")}));
        Waiting both; both.subagents = 2; both.jobs = 1;
        QCOMPARE(waitingLines(both, 3), (QStringList{QStringLiteral("waiting for 2 subagents, 1 job . . ."),
                                                     QStringLiteral("2 subagents, 1 job . . ."),
                                                     QStringLiteral("waiting . . .")}));
        QVERIFY(waitingLines(Waiting{}, 3).isEmpty());
        QVERIFY(waitingSubject(Waiting{}).isEmpty());
    }

    void factsBecomeOneState() {
        Facts f;
        QCOMPARE(resolve(f, 0), State::Idle);
        f.processBusy = true;
        QCOMPARE(resolve(f, 0), State::Running);
        f.liveSubagents = 2;
        QCOMPARE(resolve(f, 0), State::Subagents);
        f.agentBusy = true;
        QCOMPARE(resolve(f, 0), State::Working);
        f.programAsking = true;   // a [Y/n] or a password beats everything
        QCOMPARE(resolve(f, 0), State::NeedsYou);
    }

    void aFinishIsNewsUntilSeen() {
        Facts f;
        f.finishSerial = 3; f.lastOutcome = QStringLiteral("done");
        QCOMPARE(resolve(f, 2), State::Done);
        QCOMPARE(resolve(f, 3), State::Idle);        // seen
        f.lastAsked = true;
        QCOMPARE(resolve(f, 2), State::NeedsYou);    // it asked you something
        QCOMPARE(resolve(f, 3), State::Idle);
        f.lastOutcome = QStringLiteral("error");
        QCOMPARE(resolve(f, 2), State::Failed);
        f.lastOutcome = QStringLiteral("cancelled"); // you stopped it: not news
        QCOMPARE(resolve(f, 2), State::Idle);
        f.lastOutcome = QStringLiteral("done"); f.lastAsked = false; f.agentBusy = true;
        QCOMPARE(resolve(f, 2), State::Working);     // the next queued turn already runs
    }

    void handedCommands() {
        Facts f;
        f.handoffOffered = true;
        QCOMPARE(resolve(f, 0), State::Recommends);
        f.handoffWaiting = true;                     // the agent's next turn waits on it
        QCOMPARE(resolve(f, 0), State::NeedsYou);
    }

    void questions() {
        QVERIFY(endsWithQuestion(QStringLiteral("Done.\n\nShould I also update the docs?")));
        QVERIFY(endsWithQuestion(QStringLiteral("Which one do you want: **A or B?**\n\n")));
        QVERIFY(endsWithQuestion(QStringLiteral("Proceed (\"yes\" or \"no\")?")));
        QVERIFY(!endsWithQuestion(QStringLiteral("Why? Because the test was flaky.\nFixed.")));
        QVERIFY(!endsWithQuestion(QStringLiteral("Run this?\n```\nmake\n```")));
        QVERIFY(!endsWithQuestion(QString()));
    }

    void remoteDestinations() {
        QVERIFY(isRemoteProgram(QStringLiteral("ssh")));
        QVERIFY(isRemoteProgram(QStringLiteral("mosh-client")));
        QVERIFY(!isRemoteProgram(QStringLiteral("tmux")));
        QCOMPARE(remoteHost(QStringLiteral("ssh box")), QStringLiteral("box"));
        QCOMPARE(remoteHost(QStringLiteral("/usr/bin/ssh -p 2222 me@box uptime")), QStringLiteral("me@box"));
        QCOMPARE(remoteHost(QStringLiteral("ssh -l me box")), QStringLiteral("me@box"));
        QCOMPARE(remoteHost(QStringLiteral("ssh -tt -o StrictHostKeyChecking=no -i key box -t tmux")), QStringLiteral("box"));
        QCOMPARE(remoteHost(QStringLiteral("ssh -vp2222 me@box")), QStringLiteral("me@box"));
        QCOMPARE(remoteHost(QStringLiteral("ssh -J jump me@inner")), QStringLiteral("me@inner"));
        QCOMPARE(remoteHost(QStringLiteral("ssh ssh://me@box:2222")), QStringLiteral("me@box"));
        QCOMPARE(remoteHost(QStringLiteral("ssh -V")), QString());
        QCOMPARE(remoteHost(QStringLiteral("mosh --ssh=\"ssh -p 2222\" me@box -- tmux a")), QStringLiteral("me@box"));
        QCOMPARE(remoteHost(QStringLiteral("mosh-client -# 'me@box' 10.0.0.2 60001")), QStringLiteral("me@box"));
        // Relay's mosh wrapper (#S5SH): the ssh options inside --ssh are not the destination.
        QCOMPARE(remoteHost(QStringLiteral("mosh-client -# --ssh=ssh -o ControlMaster=auto -o ControlPath=/run/r/%C "
                                           "-o ControlPersist=600 --experimental-remote-ip=remote localhost | 127.0.0.1 60001")),
                 QStringLiteral("localhost"));
        QCOMPARE(remoteHost(QStringLiteral("telnet towel.blinkenlights.nl 23")), QStringLiteral("towel.blinkenlights.nl"));
        QCOMPARE(remoteHost(QStringLiteral("vim notes")), QString());
    }

    void colourModes() {
        QCOMPARE(colourModeFrom(QString()), ColourMode::ByType);
        QCOMPARE(colourModeFrom(QStringLiteral("group")), ColourMode::ByGroup);
        QCOMPARE(colourModeFrom(QStringLiteral("off")), ColourMode::Off);
        QCOMPARE(colourModeId(ColourMode::ByGroup), QStringLiteral("group"));
        QCOMPARE(colourModeIds().size(), colourModeLabels().size());
    }

    void whoGetsABand() {
        const Tokens t = tokensOf(relay::theme::builtinDark());
        QVERIFY(!typeStyle(QString(), ColourMode::ByType, t).band);
        QVERIFY(!typeStyle(QStringLiteral("terminal"), ColourMode::ByType, t).band);
        QVERIFY(!typeStyle(QStringLiteral("preview"), ColourMode::ByType, t).band);
        QVERIFY(!typeStyle(QStringLiteral("diff"), ColourMode::ByType, t).band);
        // Off keeps the band (it is the pane's name) in neutral ink: every type looks the same.
        const TypeStyle off = typeStyle(QStringLiteral("board"), ColourMode::Off, t);
        QVERIFY(off.band);
        QCOMPARE(off.fill, typeStyle(QStringLiteral("subagent"), ColourMode::Off, t).fill);
        QVERIFY(off.fill != typeStyle(QStringLiteral("board"), ColourMode::ByType, t).fill);
        const TypeStyle board = typeStyle(QStringLiteral("board"), ColourMode::ByType, t);
        QVERIFY(board.band);
        QCOMPARE(board.label, QStringLiteral("Switchboard"));   // sentence case: legible text rule
        QCOMPARE(board.glyph, Glyph::Switchboard);
        // A type nobody registered is a tool with its own name.
        const TypeStyle mystery = typeStyle(QStringLiteral("db-browser"), ColourMode::ByType, t);
        QVERIFY(mystery.band);
        QCOMPARE(mystery.label, QStringLiteral("Db browser"));
        QCOMPARE(mystery.glyph, Glyph::Tool);
        QCOMPARE(typeStyle(QStringLiteral("sessions"), ColourMode::ByType, t, QStringLiteral("Resume")).label, QStringLiteral("Resume"));
    }

    void byTypeDiffersByGroupShares() {
        const Tokens t = tokensOf(relay::theme::builtinDark());
        // Owner, 2026-09-18: "make actions red-orange". Options and Actions were one Settings pane
        // until that day and shared the green, telling themselves apart by the glyph alone; Actions
        // now has a hue of its own, so by type the four tool panes are four colours.
        const QStringList tools{QStringLiteral("board"), QStringLiteral("options"), QStringLiteral("actions"),
                                QStringLiteral("sessions")};
        QSet<QRgb> byType, byGroup;
        for (const QString &type : tools) {
            byType.insert(typeStyle(type, ColourMode::ByType, t).fill.rgb());
            byGroup.insert(typeStyle(type, ColourMode::ByGroup, t).fill.rgb());
        }
        QCOMPARE(byType.size(), 4);
        QCOMPARE(byGroup.size(), 1);
        QCOMPARE(typeStyle(QStringLiteral("actions"), ColourMode::ByType, t).ink, t.action);
        QVERIFY(typeStyle(QStringLiteral("actions"), ColourMode::ByType, t).fill != typeStyle(QStringLiteral("options"), ColourMode::ByType, t).fill);
        QVERIFY(typeStyle(QStringLiteral("actions"), ColourMode::ByType, t).glyph != typeStyle(QStringLiteral("options"), ColourMode::ByType, t).glyph);
        // "By group" is unchanged: every tool pane still shares the tools tint.
        QCOMPARE(typeStyle(QStringLiteral("actions"), ColourMode::ByGroup, t).fill.rgb(), *byGroup.cbegin());
        // ... and "off" still flattens it with the rest.
        QCOMPARE(typeStyle(QStringLiteral("actions"), ColourMode::Off, t).fill,
                 typeStyle(QStringLiteral("options"), ColourMode::Off, t).fill);
        // Today's Settings pane is styled as Options until the split lands.
        QCOMPARE(typeStyle(QStringLiteral("settings"), ColourMode::ByType, t).glyph, Glyph::Options);
        const QColor agents = typeStyle(QStringLiteral("subagent"), ColourMode::ByGroup, t).fill;
        QVERIFY(!byGroup.contains(agents.rgb()));
        QVERIFY(!byGroup.contains(remoteStyle(t).fill.rgb()));
        QVERIFY(remoteStyle(t).fill != agents);
    }

    // The tints are low-strength and never cost legibility, in every theme that ships.
    void legibleInEveryTheme() {
        const auto themes = shippedThemes();
        QVERIFY(themes.size() >= 5);
        for (const auto &spec : themes) {
            const Tokens t = tokensOf(spec);
            // The pane's own title and folder sit on the remote band, in the theme's text colours.
            QVERIFY2(contrast(t.text, remoteStyle(t).fill) >= 4.5, spec.id.toUtf8().constData());
            QList<TypeStyle> styles{remoteStyle(t), phoneStyle(t)};
            for (const QString &type : {QStringLiteral("board"), QStringLiteral("options"), QStringLiteral("actions"),
                                        QStringLiteral("settings"), QStringLiteral("sessions"), QStringLiteral("subagent"), QStringLiteral("turn"), QStringLiteral("other")})
                for (ColourMode mode : {ColourMode::ByType, ColourMode::ByGroup, ColourMode::Off}) styles << typeStyle(type, mode, t);
            for (const TypeStyle &s : styles) {
                const QByteArray where = (spec.id + QLatin1Char(' ') + s.label).toUtf8();
                QVERIFY2(contrast(s.text, s.fill) >= 4.5, where.constData());
                QVERIFY2(contrast(s.ink, s.fill) >= 3.0, where.constData());
                // Low strength: the ground stays close to the pane's own background.
                QVERIFY2(contrast(s.fill, t.background) < 1.6, where.constData());
            }
        }
    }

    // Owner, 2026-09-18: "make actions red-orange". That puts a pane type next door to the ssh
    // band, which is red and is a safety signal — the one band that must never be mistaken for
    // something ordinary. Four things keep them apart, and this is what says so: the hue itself
    // (CIELAB dE 20 or better, the margin the themes hold their other colour pairs to), the
    // strength of the band, the weight of its hairline, and the glyph. The texture is a fifth:
    // PaneChrome hatches the remote band and nothing else, which is why the *fills* are allowed to
    // be the near neighbours that any pair of low-strength tints on one background must be.
    void actionsAreNotAnSshPane() {
        for (const auto &spec : shippedThemes()) {
            const Tokens t = tokensOf(spec);
            const TypeStyle actions = typeStyle(QStringLiteral("actions"), ColourMode::ByType, t);
            const TypeStyle remote = remoteStyle(t);
            const QByteArray id = spec.id.toUtf8();
            QVERIFY2(deltaE(actions.ink, remote.ink) >= 20.0,
                     qPrintable(QStringLiteral("%1: the Actions glyph %2 and the ssh glyph %3 are only dE %4 apart")
                                    .arg(spec.id, actions.ink.name(), remote.ink.name())
                                    .arg(deltaE(actions.ink, remote.ink), 0, 'f', 1)));
            // The remote band is the firmer of the two, in fill and in hairline.
            QVERIFY2(contrast(remote.fill, t.background) > contrast(actions.fill, t.background), id.constData());
            QVERIFY2(contrast(remote.line, t.background) > contrast(actions.line, t.background), id.constData());
            QCOMPARE(actions.glyph, Glyph::Actions);
            QCOMPARE(remote.glyph, Glyph::Remote);
            // ... and it says so whatever the colour setting is, because it ignores the setting.
            for (ColourMode mode : {ColourMode::ByType, ColourMode::ByGroup, ColourMode::Off})
                QVERIFY2(typeStyle(QStringLiteral("actions"), mode, t).fill != remote.fill, id.constData());
        }
    }

    // The Actions hue is the theme's, never a constant: a hard-coded orange is the bug the
    // "Legible text" rules were written against (docs/ARCHITECTURE.md §14).
    void everyShippedThemePicksItsOwnActionsHue() {
        QSet<QRgb> inks;
        const auto themes = shippedThemes();
        QVERIFY(themes.size() >= 5);
        for (const auto &spec : themes) {
            const Tokens t = tokensOf(spec);
            QVERIFY2(t.action.isValid(), spec.id.toUtf8().constData());
            QCOMPARE(typeStyle(QStringLiteral("actions"), ColourMode::ByType, t).ink, t.action);
            inks.insert(t.action.rgb());
        }
        QCOMPARE(inks.size(), themes.size());
    }

    // ----- the title-bar buttons that open a tool pane (owner, 2026-09-18) ---------------------
    // "the sessions / actions / switchboard / options buttons at the top right should be
    // highlighted when they are open (using the header colors). click again to close those panes."
    void everyToolButtonOwnsOnePaneType() {
        const Tokens t = tokensOf(relay::theme::builtinDark());
        const QList<ToolButton> buttons = toolButtons();
        QCOMPARE(buttons.size(), 4);
        QSet<QString> types, actions;
        QSet<int> glyphs;
        for (const ToolButton &spec : buttons) {
            const QByteArray where = spec.paneType.toUtf8();
            types.insert(spec.paneType);
            actions.insert(spec.action);
            // The button wears the band's glyph, so the pane type has to be one that gets a band.
            const TypeStyle band = typeStyle(spec.paneType, ColourMode::ByType, t);
            QVERIFY2(band.band, where.constData());
            QVERIFY2(band.glyph != Glyph::None && band.glyph != Glyph::Tool, where.constData());
            glyphs.insert(int(band.glyph));
            // The tooltip says what the click will do, and says it differently once it is open.
            QVERIFY2(!spec.label.isEmpty() && !spec.what.isEmpty(), where.constData());
            QVERIFY2(spec.openLabel.startsWith(QStringLiteral("Close ")), where.constData());
            QVERIFY2(spec.openLabel != spec.label, where.constData());
        }
        QCOMPARE(types.size(), buttons.size());     // one pane type each
        QCOMPARE(actions.size(), buttons.size());   // one action each
        QCOMPARE(glyphs.size(), buttons.size());    // and four glyphs you can tell apart
        QVERIFY(types.contains(QStringLiteral("actions")));
        QVERIFY(types.contains(QStringLiteral("sessions")));
        QVERIFY(types.contains(QStringLiteral("board")));
        QVERIFY(types.contains(QStringLiteral("options")));
    }

    // A lit button is the pane's own band, firmed up for a 26 px button: legible with colours on,
    // and still unmistakable with them off, because which panes are open is information.
    void aLitButtonIsLegibleAndUnmistakable() {
        const auto themes = shippedThemes();
        QVERIFY(themes.size() >= 5);
        for (const auto &spec : themes) {
            const Tokens t = tokensOf(spec);
            for (const ToolButton &button : toolButtons())
                for (ColourMode mode : {ColourMode::ByType, ColourMode::ByGroup, ColourMode::Off}) {
                    const TypeStyle band = typeStyle(button.paneType, mode, t);
                    const OpenButtonStyle open = openButtonStyle(band, false);
                    const OpenButtonStyle hover = openButtonStyle(band, true);
                    const QByteArray where = (spec.id + QLatin1Char(' ') + button.paneType
                                              + QLatin1Char(' ') + colourModeId(mode)).toUtf8();
                    // §14: the glyph keeps its 3:1 on whatever ground it lands on.
                    QVERIFY2(contrast(open.ink, open.fill) >= 3.0, where.constData());
                    QVERIFY2(contrast(hover.ink, hover.fill) >= 3.0, where.constData());
                    // Lit reads against the title bar, which is the theme's background, in every
                    // mode — including "off", where the band itself would be too faint to see.
                    QVERIFY2(contrast(open.fill, t.background) >= 1.12, where.constData());
                    QVERIFY2(contrast(open.line, t.background) >= 1.5, where.constData());
                    // ... without becoming a block of colour in the title bar.
                    QVERIFY2(contrast(open.fill, t.background) < 2.6, where.constData());
                    // Open and open+hover are different chips, and hover is the firmer one.
                    QVERIFY2(hover.fill != open.fill, where.constData());
                    QVERIFY2(contrast(hover.fill, t.background) > contrast(open.fill, t.background), where.constData());
                    // The keyboard-focus ring is visible on both of them, and on the plain bar.
                    QVERIFY2(contrast(focusRing(open.fill, t), open.fill) >= 3.0, where.constData());
                    QVERIFY2(contrast(focusRing(t.background, t), t.background) >= 3.0, where.constData());
                }
        }
    }

    // With pane colours off every lit button looks the same, because the bands do; with them on,
    // two buttons share a ground exactly when their panes' bands share one. The light says "open",
    // the colour says which pane, and a retinted pane type moves the button with it.
    void litFollowsThePaneColourSetting() {
        const Tokens t = tokensOf(relay::theme::builtinDark());
        const QList<ToolButton> buttons = toolButtons();
        QSet<QRgb> off;
        for (const ToolButton &button : buttons)
            off.insert(openButtonStyle(typeStyle(button.paneType, ColourMode::Off, t), false).fill.rgb());
        QCOMPARE(off.size(), 1);
        for (ColourMode mode : {ColourMode::ByType, ColourMode::ByGroup})
            for (const ToolButton &a : buttons)
                for (const ToolButton &b : buttons) {
                    const TypeStyle bandA = typeStyle(a.paneType, mode, t), bandB = typeStyle(b.paneType, mode, t);
                    const QByteArray where = (a.paneType + QLatin1Char('/') + b.paneType).toUtf8();
                    QVERIFY2((openButtonStyle(bandA, false).fill == openButtonStyle(bandB, false).fill)
                                 == (bandA.fill == bandB.fill), where.constData());
                }
        // A hue is a hue: lit-with-colours is never the neutral of lit-with-colours-off.
        for (const ToolButton &button : buttons)
            QVERIFY(openButtonStyle(typeStyle(button.paneType, ColourMode::ByType, t), false).fill.rgb() != *off.cbegin());
    }
};

QTEST_GUILESS_MAIN(PaneStatusTests)
#include "panestatus_test.moc"
