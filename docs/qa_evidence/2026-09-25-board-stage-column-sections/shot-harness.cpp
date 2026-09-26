#include "BoardPane.h"
#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimer>
#include <QThread>
#include <cstdio>
static QJsonObject config()
{
    return QJsonDocument::fromJson(R"({
      "columns": ["inbox", "discussing", "ready", "in-progress", "needs-qa", "done"],
      "column_statuses": {"inbox": ["inbox"], "discussing": ["discussing"], "ready": ["ready"],
        "in-progress": ["in-progress"], "needs-qa": ["needs-qa-llm", "needs-qa-human"], "done": ["done"]},
      "all_statuses": ["inbox", "discussing", "ready", "in-progress", "needs-qa-llm", "needs-qa-human", "done"],
      "tabs": [{"id": "features", "folder": "features"}],
      "autonomy": "auto"
    })").object();
}
static QJsonObject row(const char *id, const char *status, const char *title)
{
    return QJsonObject{{"id", id}, {"title", title}, {"type", "work"}, {"status", status},
                       {"tab", "features"}, {"rank", "i"}, {"created", "2026-09-20"},
                       {"updated", "2026-09-24T08:00:00Z"},
                       {"path", QStringLiteral("issues/features/") + id + ".md"}};
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    view.restoreGrouping(QStringLiteral("sections"));
    QJsonArray cards{row("AAA1", "inbox", "Stage is missing for most cards"),
                     row("BBB2", "discussing", "Python console pane"),
                     row("CCC3", "ready", "Models pane polish"),
                     row("DDD4", "in-progress", "Info button overlay"),
                     row("EEE5", "needs-qa-llm", "Board sortable viewed column"),
                     row("FFF6", "needs-qa-human", "Surface owner questions")};
    view.handleEvent(QJsonObject{{"event", "board"}, {"config", config()}, {"cards", cards},
                                 {"problems", QJsonArray{}}});
    view.setCollapsedSections(QJsonArray{});
    std::printf("grouping=%s\n", qPrintable(view.grouping()));
    view.show();
    const int widths[] = {1100, 800, 700, 620, 580, 520};
    for (const char *mode : {"sections", "flat"}) {
        if (QByteArray(mode) == "flat")
            view.restoreGrouping(QStringLiteral("flat"));
        for (int w : widths) {
            view.resize(w, 560);
            for (int i = 0; i < 20; ++i) {
                app.processEvents();
                QThread::msleep(20);
            }
            view.grab().save(QStringLiteral("%1/%2-%3.png").arg(argv[1]).arg(mode).arg(w));
        }
    }
    return 0;
}
