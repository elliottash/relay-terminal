// SPDX-License-Identifier: GPL-3.0-or-later
// Pane types, pane states and remote sessions (cards #SPBN and #XM0T): the urgency order a tab
// uses, how a pane's facts become one state, the ssh/mosh/telnet destination, and that every
// tint keeps its glyph and its label legible in every shipped theme.
#include "PaneStatus.h"
#include "ThemeFile.h"

#include <QDir>
#include <QFile>
#include <QTest>

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
    return t;
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
        QCOMPARE(board.label, QStringLiteral("SWITCHBOARD"));
        QCOMPARE(board.glyph, Glyph::Switchboard);
        // A type nobody registered is a tool with its own name.
        const TypeStyle mystery = typeStyle(QStringLiteral("db-browser"), ColourMode::ByType, t);
        QVERIFY(mystery.band);
        QCOMPARE(mystery.label, QStringLiteral("DB BROWSER"));
        QCOMPARE(mystery.glyph, Glyph::Tool);
        QCOMPARE(typeStyle(QStringLiteral("sessions"), ColourMode::ByType, t, QStringLiteral("Resume")).label, QStringLiteral("RESUME"));
    }

    void byTypeDiffersByGroupShares() {
        const Tokens t = tokensOf(relay::theme::builtinDark());
        // Options and Actions were one Settings pane until 2026-09-18 and share a tint by type.
        const QStringList tools{QStringLiteral("board"), QStringLiteral("options"), QStringLiteral("sessions")};
        QSet<QRgb> byType, byGroup;
        for (const QString &type : tools) {
            byType.insert(typeStyle(type, ColourMode::ByType, t).fill.rgb());
            byGroup.insert(typeStyle(type, ColourMode::ByGroup, t).fill.rgb());
        }
        QCOMPARE(byType.size(), 3);
        QCOMPARE(byGroup.size(), 1);
        QCOMPARE(typeStyle(QStringLiteral("actions"), ColourMode::ByType, t).fill, typeStyle(QStringLiteral("options"), ColourMode::ByType, t).fill);
        QVERIFY(typeStyle(QStringLiteral("actions"), ColourMode::ByType, t).glyph != typeStyle(QStringLiteral("options"), ColourMode::ByType, t).glyph);
        QCOMPARE(typeStyle(QStringLiteral("actions"), ColourMode::ByGroup, t).fill.rgb(), *byGroup.cbegin());
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
};

QTEST_GUILESS_MAIN(PaneStatusTests)
#include "panestatus_test.moc"
