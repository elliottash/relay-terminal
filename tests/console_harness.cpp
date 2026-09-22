// SPDX-License-Identifier: AGPL-3.0-or-later
// A window with one no-shell console in it, and nothing else (card #AGNT).
//
//     relay-console-harness [workspace]
//
// A pane is the agent console and the terminal is one routing of what is typed in it, so a
// helper surface is the same `Pane` with a context whose spec says `shell: false`. This is the
// smallest thing that proves it: a `QMainWindow`, a context, and
// `new Pane(workspace, workspace, false, core, &context)`. No window manager, no tabs, no
// `RelayWindow` — which is the point, because it also shows that a console registers itself
// nowhere and can be embedded by a host that knows nothing about panes (steps 5-7).
// `RELAY_CONSOLE_STATUS=waiting` injects one live background subagent after the window opens; it
// is a deterministic visual fixture for #R3YN's neutral status line without needing a provider.
//
// It is not a test and runs no assertions; `tests/consolemode_test.cpp` is the gate. This is what
// `docs/qa_evidence/2026-09-20-agent-console-extraction/drive-console.sh` drives under Xvfb so
// the streaming, the thinking fold, the queue strip and Esc can be seen rather than asserted.

#include "Pane.h"
#include "Theme.h"

#include <QApplication>
#include <QMainWindow>
#include <QTimer>
#include <QVBoxLayout>

#include <cstdio>

namespace {

// What a Switchboard-shaped console is about: no shell, routing locked to the agent, the
// conversation kept where a helper's is (protocol 30.7), and two actions in the row above the
// box so the letters and the left alignment can be seen.
class HarnessContext final : public relay::agent::Context {
public:
    explicit HarnessContext(QString workspace) : m_workspace(std::move(workspace)) {}

    relay::agent::ContextSpec spec() const override {
        relay::agent::ContextSpec spec;
        spec.name = QStringLiteral("switchboard");
        spec.surface = QStringLiteral("harness");
        // "main" rather than "switchboard" on purpose: the role picks a provider and nothing
        // else, and the one thing under test here is the absence of a shell. A real board
        // console sends the helper role.
        spec.agentRole = QStringLiteral("main");
        spec.workspace = m_workspace;
        spec.scope = QStringLiteral("console");
        spec.persistScope = QStringLiteral("helper");
        spec.persistKey = QStringLiteral("harness");
        spec.shell = false;
        spec.routing = QStringLiteral("agent");
        return spec;
    }

    QString placeholder() const override { return QStringLiteral("Ask about this board…"); }

    QList<relay::agent::Action> actions() const override {
        return {{QStringLiteral("boardCheck"), QStringLiteral("k"), QStringLiteral("Check"),
                 QStringLiteral("Check the board for cards that disagree with the code"), false, true,
                 [] {}},
                {QStringLiteral("boardCleanUp"), QStringLiteral("u"), QStringLiteral("Clean up"),
                 QStringLiteral("Tidy the board"), false, true, [] {}}};
    }

private:
    QString m_workspace;
};

}  // namespace

int main(int argc, char **argv)
{
    // The same identity src/main.cpp gives the app, because QSettings is keyed by it: without
    // this the console reads a different relay.conf, finds no stored provider and falls back to
    // Relay Free — which is what the first live run did, and it looked like a broken console.
    QCoreApplication::setOrganizationName(QStringLiteral("RelayTerminal"));
    QCoreApplication::setApplicationName(QStringLiteral("relay"));
    QCoreApplication::setApplicationVersion(QStringLiteral(RELAY_VERSION));
    QApplication app(argc, argv);
    // The same theme the app applies, so what this draws is what a console looks like in Relay
    // rather than in Qt's default palette.
    relay::theme::applyTheme(app);
    const QString workspace = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::homePath();

    HarnessContext context(workspace);
    QMainWindow window;
    window.setObjectName(QStringLiteral("consoleHarness"));
    auto *central = new QWidget(&window);
    auto *column = new QVBoxLayout(central);
    column->setContentsMargins(0, 0, 0, 0);
    auto *console = new Pane(workspace, workspace, false, relay::defaultEngineCore(), &context);
    console->setObjectName(QStringLiteral("agentConsole"));
    column->addWidget(console);
    window.setCentralWidget(central);
    window.resize(1400, 900);
    window.show();
    if (qEnvironmentVariable("RELAY_CONSOLE_STATUS") == QStringLiteral("waiting")) {
        QTimer::singleShot(250, console, [console] {
            console->deliverWorkerEvent(QJsonObject{{"event", "subagent_started"}, {"id", "qa1"},
                                                    {"type", "explore"}, {"description", "visual fixture"},
                                                    {"background", true}});
        });
    }
    // RELAY_CONSOLE_DUMP=1 prints the console's widget tree once it has settled, which is how the
    // drive was debugged when the composer came out clipped. It is not part of any check.
    if (!qEnvironmentVariableIsEmpty("RELAY_CONSOLE_DUMP")) {
        QTimer::singleShot(qEnvironmentVariableIntValue("RELAY_CONSOLE_DUMP") * 1000, &app, [console] {
            for (QWidget *w : console->findChildren<QWidget *>()) {
                if (!w->isVisible()) continue;
                const QRect g = w->geometry();
                std::fprintf(stderr, "%-28s %-22s %4d,%4d %4dx%-4d vis=%d\n",
                             qPrintable(w->objectName()), w->metaObject()->className(),
                             g.x(), g.y(), g.width(), g.height(), int(w->isVisible()));
            }
            std::fflush(stderr);
        });
    }
    return app.exec();
}
