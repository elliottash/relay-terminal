// SPDX-License-Identifier: AGPL-3.0-or-later
// #7Z08: real composer events and worker lifecycle, including the dispatch/start race.
#pragma once

namespace cases {
void recallPromptCases() {
    const auto key = [](Pane &pane, int code, bool repeat = false) {
        auto *editor = pane.findChild<QPlainTextEdit *>(QStringLiteral("composerEditor"));
        CHECK(editor);
        QKeyEvent event(QEvent::KeyPress, code, Qt::NoModifier, QString(), repeat);
        QCoreApplication::sendEvent(editor, &event);
    };
    const auto lastAsk = [](const QList<QJsonObject> &sent) {
        QJsonObject ask;
        for (const auto &line : sent) if (line.value("type") == "ask") ask = line;
        return ask;
    };
    const auto cancels = [](const QList<QJsonObject> &sent) {
        int n = 0;
        for (const auto &line : sent) if (line.value("type") == "cancel") ++n;
        return n;
    };
    // Both keys work before any worker acknowledgement, between queued/start, and mid-turn.
    for (int shortcut : {int(Qt::Key_Up), int(Qt::Key_Escape)}) {
        for (int phase = 0; phase < 3; ++phase) {
            StubContext context;
            context.workspace = home->path();
            Pane pane(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
            QList<QJsonObject> sent;
            pane.onWorkerLine = [&](const QJsonObject &line) { sent << line; };
            pane.deliverWorkerEvent({{"event", "configured"}, {"model", "fixture"}});
            const QString original = QStringLiteral("Explain the recrod\nthen check @src/Pane.h without editing it.");
            pane.draftInComposer(original);
            key(pane, Qt::Key_Return);
            const QJsonObject ask = lastAsk(sent);
            CHECK(!ask.isEmpty());
            CHECK_EQ(pane.composerText(), QString());
            const auto queued = QJsonObject{{"event", "queued"}, {"id", "q1"}, {"request_id", ask.value("id")}};
            if (phase >= 1) pane.deliverWorkerEvent(queued);
            if (phase >= 2) {
                pane.deliverWorkerEvent({{"event", "agent_started"}, {"id", "q1"}});
                pane.showQuestion({{"id", "clarify"}, {"questions", QJsonArray{
                    QJsonObject{{"header", "Scope"}, {"question", "Which files?"}}}}});
                CHECK(pane.questionOpen());
            }
            key(pane, shortcut);
            CHECK(!pane.questionOpen());
            CHECK_EQ(cancels(sent), 1);
            CHECK_EQ(pane.composerText(), original);
            CHECK_EQ(lastAsk(sent), ask); // nothing resubmitted by recall
            const QString evidence = qEnvironmentVariable("RELAY_RECALL_EVIDENCE");
            if (!evidence.isEmpty() && phase == 2 && shortcut == Qt::Key_Up) {
                pane.resize(960, 640); pane.show();
                QApplication::processEvents();
                CHECK(pane.grab().save(evidence));
            }
            key(pane, shortcut, true);
            CHECK_EQ(cancels(sent), 1);
            CHECK_EQ(pane.composerText(), original);
            if (phase == 0) pane.deliverWorkerEvent(queued);
            if (phase < 2) pane.deliverWorkerEvent({{"event", "agent_started"}, {"id", "q1"}});
            CHECK_EQ(pane.composerText(), original); // late start cannot erase the restored draft
            const QString edited = QStringLiteral("Explain the record, without editing it.");
            pane.draftInComposer(edited);
            key(pane, Qt::Key_Return); // correction sent while old cancellation is still pending
            CHECK_EQ(lastAsk(sent).value("text").toString(), edited);
            CHECK_EQ(lastAsk(sent).value("when").toString(), QStringLiteral("interrupt"));
            pane.deliverWorkerEvent({{"event", "agent_finished"}, {"id", "q1"}, {"outcome", "cancelled"}});
            CHECK_EQ(pane.composerText(), QString());
        }
    }

    // Enter, Up, edit, Enter can all precede the first start event. Its late events must
    // not replace the correction, even if the correction was acknowledged first.
    {
        StubContext context;
        context.workspace = home->path();
        Pane pane(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
        QList<QJsonObject> sent;
        pane.onWorkerLine = [&](const QJsonObject &line) { sent << line; };
        pane.deliverWorkerEvent({{"event", "configured"}, {"model", "fixture"}});
        pane.draftInComposer(QStringLiteral("the mistake")); key(pane, Qt::Key_Return);
        const auto oldAsk = lastAsk(sent);
        key(pane, Qt::Key_Up);
        pane.draftInComposer(QStringLiteral("the correction")); key(pane, Qt::Key_Return);
        const auto newAsk = lastAsk(sent);
        CHECK(oldAsk.value("id") != newAsk.value("id"));
        CHECK_EQ(newAsk.value("when").toString(), QStringLiteral("interrupt"));
        pane.deliverWorkerEvent({{"event", "queued"}, {"id", "old"}, {"request_id", oldAsk.value("id")}});
        pane.deliverWorkerEvent({{"event", "queued"}, {"id", "new"}, {"request_id", newAsk.value("id")}});
        pane.deliverWorkerEvent({{"event", "agent_started"}, {"id", "old"}});
        pane.deliverWorkerEvent({{"event", "agent_finished"}, {"id", "old"}, {"outcome", "cancelled"}});
        key(pane, Qt::Key_Up);
        CHECK_EQ(pane.composerText(), QStringLiteral("the correction"));
    }

    // A worker-backed card console recalls the full local text, never the server preview.
    StubContext context;
    context.workspace = home->path(); context.takeSubmit = true;
    Pane pane(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    QList<QJsonObject> sent;
    pane.onWorkerLine = [&](const QJsonObject &line) { sent << line; };
    const QString original = QStringLiteral("Please explain the recrod and its context. ") + QString(160, QLatin1Char('x'));
    pane.draftInComposer(original); key(pane, Qt::Key_Return); pane.draftInComposer({});
    pane.deliverWorkerEvent({{"event", "queued"}, {"id", "card1"}, {"surface", "stub"}});
    pane.deliverWorkerEvent({{"event", "agent_started"}, {"id", "card1"}});
    pane.draftInComposer(QStringLiteral("my newer draft"));
    key(pane, Qt::Key_Up);
    CHECK_EQ(cancels(sent), 0);
    key(pane, Qt::Key_Escape);
    CHECK_EQ(cancels(sent), 1);
    CHECK_EQ(pane.composerText(), QStringLiteral("my newer draft"));
    pane.draftInComposer({});
    key(pane, Qt::Key_Up);
    CHECK_EQ(pane.composerText(), original);
    CHECK_EQ(sent.last().value("surface").toString(), QStringLiteral("stub"));
    pane.deliverWorkerEvent({{"event", "agent_finished"}, {"id", "card1"}, {"outcome", "cancelled"}});
    CHECK_EQ(pane.composerText(), original);

    pane.draftInComposer({}); sent.clear();
    key(pane, Qt::Key_Up);
    CHECK_EQ(cancels(sent), 0); // completed turn uses normal history, never cancellation

    // A queued follow-up takes priority over the active turn, without cancelling that turn.
    pane.deliverWorkerEvent({{"event", "agent_started"}, {"id", "external"}});
    const QString followup = QStringLiteral("Check the examples too");
    pane.draftInComposer(followup); key(pane, Qt::Key_Return); pane.draftInComposer({});
    pane.deliverWorkerEvent({{"event", "queued"}, {"id", "q2"}, {"surface", "stub"}});
    pane.deliverWorkerEvent(queueChanged({workerRow(QStringLiteral("q2"), followup, QStringLiteral("stub"))}));
    sent.clear(); key(pane, Qt::Key_Up);
    CHECK_EQ(pane.composerText(), followup);
    CHECK_EQ(cancels(sent), 0);
    CHECK_EQ(pane.queuedPrompts(), 0);

    // Existing queued recall wins over the active turn.
    aLineThisConsoleSentComesBackWithUpAsAnUnsentDraft();
}

// Once the running turn has made a tool call, the combined undo is gone: Up brings the words
// back and stops nothing, Esc stops the turn and brings nothing back.
void recallAfterAToolCallCases()
{
    const auto key = [](Pane &pane, int code) {
        auto *editor = pane.findChild<QPlainTextEdit *>(QStringLiteral("composerEditor"));
        CHECK(editor);
        QKeyEvent event(QEvent::KeyPress, code, Qt::NoModifier, QString(), false);
        QCoreApplication::sendEvent(editor, &event);
    };
    const auto cancels = [](const QList<QJsonObject> &sent) {
        int n = 0;
        for (const auto &line : sent) if (line.value("type") == "cancel") ++n;
        return n;
    };
    StubContext context;
    context.workspace = home->path();
    Pane pane(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    QList<QJsonObject> sent;
    pane.onWorkerLine = [&](const QJsonObject &line) { sent << line; };
    pane.deliverWorkerEvent({{"event", "configured"}, {"model", "fixture"}});
    const QString original = QStringLiteral("Rewrite the release notes");
    pane.draftInComposer(original); key(pane, Qt::Key_Return); pane.draftInComposer({});
    const QJsonObject ask = [&] {
        QJsonObject a;
        for (const auto &line : sent) if (line.value("type") == "ask") a = line;
        return a;
    }();
    CHECK(!ask.isEmpty());
    pane.deliverWorkerEvent({{"event", "queued"}, {"id", "q1"}, {"request_id", ask.value("id")}});
    pane.deliverWorkerEvent({{"event", "agent_started"}, {"id", "q1"}});
    pane.deliverWorkerEvent({{"event", "tool_started"}, {"call_id", "c1"}, {"turn_id", "q1"},
                             {"tool", "run_command"}, {"label", QStringLiteral("ls")}});
    sent.clear();
    key(pane, Qt::Key_Up);
    CHECK_EQ(pane.composerText(), original);
    CHECK_EQ(cancels(sent), 0);  // a turn that has acted is not stopped by Up
    pane.draftInComposer({});
    key(pane, Qt::Key_Escape);
    CHECK_EQ(cancels(sent), 1);  // Esc still stops the turn...
    CHECK(pane.composerText().isEmpty());  // ... and puts no prompt back in the composer
    pane.deliverWorkerEvent({{"event", "agent_finished"}, {"id", "q1"}, {"outcome", "cancelled"}});
    CHECK(pane.composerText().isEmpty());
}
} // namespace cases
