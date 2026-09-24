// SPDX-License-Identifier: AGPL-3.0-or-later
// End-to-end composer/event interleavings for #XCXD; no provider or shell.
namespace cases {
namespace {
struct XcxdReviewPane {
    StubContext context;
    QList<QJsonObject> sent;
    Pane pane;
    XcxdReviewPane() : pane(home->path(), home->path(), true, relay::defaultEngineCore(), &context) {
        pane.onWorkerLine = [this](const QJsonObject &message) { sent.append(message); };
        pane.deliverWorkerEvent({{"event", "ready"}});
        pane.deliverWorkerEvent({{"event", "configured"}, {"model", "test"}});
        pane.deliverWorkerEvent({{"event", "agent_started"}, {"id", "running"}});
        sent.clear();
    }
    ~XcxdReviewPane() { pane.onWorkerLine = {}; }
    void enter() {
        auto *editor = pane.findChild<QPlainTextEdit *>(QStringLiteral("composerEditor"));
        CHECK(editor);
        if (!editor) return;
        QKeyEvent key(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QCoreApplication::sendEvent(editor, &key);
    }
    void queue(const QString &text) { pane.draftInComposer(text); enter(); }
    QList<QJsonObject> messages(const QString &type) const {
        QList<QJsonObject> result;
        for (const auto &message : sent) if (message.value("type") == type) result.append(message);
        return result;
    }
};
}

void xcxdReviewCases() {
    for (int count : {0, 1, 2, 4}) {
        XcxdReviewPane fixture;
        for (int i = 0; i < count; ++i) fixture.queue(QStringLiteral("prompt-%1").arg(i));
        CHECK_EQ(fixture.pane.queuedPrompts(), count);
        fixture.sent.clear();
        fixture.enter();
        const auto asks = fixture.messages(QStringLiteral("ask"));
        CHECK_EQ(asks.size(), count ? 1 : 0);
        if (asks.isEmpty()) continue;
        CHECK_EQ(asks.first().value("text").toString(), QStringLiteral("prompt-0"));
        CHECK_EQ(asks.first().value("when").toString(), QStringLiteral("steer"));
        const QString request = asks.first().value("id").toString();
        fixture.pane.deliverWorkerEvent({{"event", "steer_delivered"}, {"request_ids", QJsonArray{request}}});
        fixture.sent.clear();
        fixture.enter();
        fixture.enter();
        CHECK(fixture.messages(QStringLiteral("ask")).isEmpty());
        CHECK(fixture.messages(QStringLiteral("queue_unsteer")).isEmpty());
        CHECK_EQ(fixture.pane.queuedPrompts(), count - 1);
        fixture.queue(QStringLiteral("new draft"));
        fixture.sent.clear();
        fixture.enter();
        const auto fresh = fixture.messages(QStringLiteral("ask"));
        CHECK_EQ(fresh.size(), 1);
        if (!fresh.isEmpty()) CHECK_EQ(fresh.first().value("text").toString(),
                                      count > 1 ? QStringLiteral("prompt-1") : QStringLiteral("new draft"));
    }
    for (int extraEnters : {1, 2}) {
        XcxdReviewPane fixture;
        fixture.queue(QStringLiteral("A"));
        fixture.queue(QStringLiteral("B"));
        fixture.pane.setMode(QStringLiteral("auto"));
        fixture.pane.draftInComposer(QStringLiteral("new routed draft"));
        fixture.sent.clear();
        fixture.enter();
        auto routes = fixture.messages(QStringLiteral("route"));
        CHECK_EQ(routes.size(), 1);
        if (routes.isEmpty()) continue;
        const QString request = routes.last().value("id").toString();
        // The submitted text is still in the composer while routing is pending.
        CHECK_EQ(fixture.pane.composerText(), QStringLiteral("new routed draft"));
        for (int i = 0; i < extraEnters; ++i) fixture.enter();
        CHECK_EQ(fixture.messages(QStringLiteral("route")).size(), 1);
        fixture.sent.clear();
        fixture.pane.deliverWorkerEvent({{"event", "route"}, {"id", request},
                                        {"route", "agent"}, {"text", "new routed draft"}, {"valid", true}});
        for (int i = 0; i < 4; ++i) QCoreApplication::processEvents();
        const auto asks = fixture.messages(QStringLiteral("ask"));
        if (asks.size() != 1) {
            std::fprintf(stderr, "route replay extra=%d queued=%d busy=%d draft=%s\n", extraEnters, fixture.pane.queuedPrompts(), fixture.pane.agentBusy(), qPrintable(fixture.pane.composerText()));
            for (const auto &message : fixture.sent) std::fprintf(stderr, "%s\n", QJsonDocument(message).toJson(QJsonDocument::Compact).constData());
        }
        CHECK_EQ(asks.size(), 1);
        if (!asks.isEmpty()) {
            CHECK_EQ(asks.first().value("text").toString(), QStringLiteral("A"));
            CHECK_EQ(asks.first().value("when").toString(), QStringLiteral("steer"));
            const auto unsteers = fixture.messages(QStringLiteral("queue_unsteer"));
            CHECK_EQ(unsteers.size(), extraEnters - 1);
            if (!unsteers.isEmpty()) CHECK_EQ(unsteers.first().value("request"), asks.first().value("id"));
        }
        CHECK_EQ(fixture.pane.queuedPrompts(), 2);
    }
}
} // namespace cases
