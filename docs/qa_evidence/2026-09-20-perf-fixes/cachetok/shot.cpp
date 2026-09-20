// SPDX-License-Identifier: AGPL-3.0-or-later
// #GMCF decision 5: the two surfaces that now say what the provider's prefix cache served.
// Built and run as docs/qa_evidence/2026-09-20-info-activity-ask-rows/README.md describes.
#include "AgentInternalsView.h"
#include "SessionInfo.h"
#include <QApplication>
#include <QJsonObject>
#include <QPixmap>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    {
        relay::AgentInternalsView view;
        view.onAskOwner = [](const QString &) {};
        view.resize(560, 300);
        view.show();
        // The two turns of the live Local-tier run in README.md: cold, then the same prefix again.
        view.beginTurn("t1", "Reply with the single word ok.");
        view.noteUsage(QJsonObject{{"prompt_tokens", 726}, {"completion_tokens", 31},
                                   {"cached_tokens", 0}});
        view.beginTurn("t2", "Reply with the single word yes.");
        view.noteUsage(QJsonObject{{"prompt_tokens", 726}, {"completion_tokens", 30},
                                   {"cached_tokens", 709}});
        // A provider that counts its cache writes separately says both (Anthropic, codex).
        view.beginTurn("t3", "now the docs");
        view.noteUsage(QJsonObject{{"prompt_tokens", 21323}, {"completion_tokens", 167},
                                   {"cached_tokens", 13689}, {"cache_write_tokens", 7624}});
        // …and one that says nothing about caching claims nothing.
        view.beginTurn("t4", "and the tests");
        view.noteUsage(QJsonObject{{"prompt_tokens", 4210}, {"completion_tokens", 96}});
        app.processEvents();
        view.grab().save(QString::fromLatin1(argv[1]) + "/activity-cached-tokens.png");
    }
    {
        relay::sessioninfo::InfoView view;
        view.onAskOwner = [](const QString &) {};
        view.onRequest = [&view](const QJsonObject &r) {
            QJsonObject e{{"id", r.value("id")}, {"kind", "session"}, {"live", true},
                          {"title", "Relay work"}, {"model", "bonsai-2-27b"}, {"provider", "Local (local:bonsai)"},
                          {"turns", 7},
                          {"context", QJsonObject{{"used_tokens", 41200}, {"window", 200000}, {"percent", 20.6}}},
                          {"usage", QJsonObject{{"prompt_tokens", 120000}, {"completion_tokens", 8000},
                                                {"total_tokens", 128000}, {"requests", 34},
                                                {"cached_tokens", 96000}}}};
            view.setInfo(e);
        };
        view.resize(560, 420);
        view.show();
        view.showLiveSession();
        app.processEvents();
        view.grab().save(QString::fromLatin1(argv[1]) + "/info-cached-tokens.png");
    }
    return 0;
}
