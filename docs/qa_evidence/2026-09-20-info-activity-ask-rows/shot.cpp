#include "SessionInfo.h"
#include "AgentInternalsView.h"
#include <QApplication>
#include <QJsonObject>
#include <QPixmap>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    {
        relay::sessioninfo::InfoView view;
        view.onAskOwner = [](const QString &) {};
        view.onRequest = [&view](const QJsonObject &r) {
            QJsonObject e{{"id", r.value("id")}, {"kind", "session"}, {"live", true},
                          {"title", "Relay work"}, {"model", "glm-5"}, {"provider", "GLM Coding Plan (glm)"},
                          {"turns", 7},
                          {"context", QJsonObject{{"used_tokens", 41200}, {"window", 200000}, {"percent", 20.6}}},
                          {"usage", QJsonObject{{"prompt_tokens", 120000}, {"completion_tokens", 8000},
                                                {"total_tokens", 128000}, {"requests", 34}}}};
            view.setInfo(e);
        };
        view.resize(560, 420);
        view.show();
        view.showLiveSession();
        app.processEvents();
        view.grab().save(QString::fromLatin1(argv[1]) + "/info-ask-row.png");
    }
    {
        relay::AgentInternalsView view;
        view.onAskOwner = [](const QString &) {};
        view.resize(560, 420);
        view.show();
        view.beginTurn("t1", "fix the build and then the tests");
        view.setThinking("t1", "b1", "Looking at the CMake error first.", true, 4200);
        view.toolResult(QJsonObject{{"call_id", "c1"}, {"turn_id", "t1"}, {"tool", "run_command"},
                                    {"ok", true},
                                    {"label", QJsonObject{{"kind", "run"}, {"title", "ran pytest"}, {"ok", true}}}});
        view.beginTurn("t2", "now the docs");
        app.processEvents();
        view.grab().save(QString::fromLatin1(argv[1]) + "/activity-ask-row.png");
    }
    return 0;
}
