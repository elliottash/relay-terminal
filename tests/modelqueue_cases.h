// SPDX-License-Identifier: AGPL-3.0-or-later
// Card #7QH0: a model picked while the agent works is queued as a `/model <name>` row and walks
// the prompt's ladder — queued, Enter → at the next tool call (`set_model {when: "steer"}`),
// Enter again → now (`when: "now"`). Offscreen, against a console Pane with no shell; the worker
// is played by `deliverWorkerEvent` and `onWorkerLine` records what the pane sends.
// Parent: include after the CHECK macros, StubContext and `home` exist; call
// cases::modelQueueCases() from the default test main and from --model-queue-only.
#pragma once

namespace cases {

inline QJsonObject modelPresets() {
    return QJsonObject{{"event", "presets"}, {"presets", QJsonArray{
        QJsonObject{{"id", "p-old"}, {"label", "Old"}, {"base_url", "http://127.0.0.1:1/v1"}, {"model", "old"}},
        QJsonObject{{"id", "p-fable"}, {"label", "Fable"}, {"base_url", "http://127.0.0.1:2/v1"}, {"model", "fable"}},
        QJsonObject{{"id", "p-kimi"}, {"label", "Kimi"}, {"base_url", "http://127.0.0.1:3/v1"}, {"model", "kimi"}}}}};
}

// Configured on p-old, as a pane that picked it: the chip's preset is what a pick compares with.
inline void configureOnOld(Pane &pane) {
    pane.deliverWorkerEvent(QJsonObject{{"event", "configured"}, {"model", "old"}, {"preset", "p-old"}});
    pane.deliverWorkerEvent(QJsonObject{{"event", "model_changed"}, {"model", "old"}, {"preset", "p-old"}, {"applies", "now"}});
}

inline QStringList rowSummary(const Pane &pane) {
    QStringList rows;
    for (const Pane::QueueRow &row : pane.queueRows())
        if (row.kind != QStringLiteral("running")) rows << row.kind + QLatin1Char('|') + row.state;
    return rows;
}

inline QList<QJsonObject> setModels(const QList<QJsonObject> &sent) {
    QList<QJsonObject> out;
    for (const QJsonObject &message : sent)
        if (message.value("type").toString() == QStringLiteral("set_model")) out << message;
    return out;
}

inline void aModelPickedMidTurnQueuesAndWalksTheLadder() {
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    QList<QJsonObject> sent;
    console.onWorkerLine = [&sent](const QJsonObject &message) { sent << message; };
    console.deliverWorkerEvent(modelPresets());
    configureOnOld(console);
    console.deliverWorkerEvent(QJsonObject{{"event", "agent_started"}, {"id", "running"}});
    auto *editor = console.findChild<QPlainTextEdit *>(QStringLiteral("composerEditor"));
    CHECK(editor != nullptr);
    if (!editor) return;
    const auto enter = [&] {
        QKeyEvent key(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QCoreApplication::sendEvent(editor, &key);
    };
    // A prompt waits already; the pick joins the queue behind it and nothing is sent yet.
    console.draftInComposer(QStringLiteral("queued before the pick"));
    enter();
    sent.clear();
    console.selectModel(QStringLiteral("p-fable"));
    CHECK(setModels(sent).isEmpty());
    CHECK_EQ(rowSummary(console), QStringList({"agent|queued", "model|queued"}));
    QString preview;
    for (const Pane::QueueRow &row : console.queueRows()) if (row.kind == QStringLiteral("model")) preview = row.preview;
    CHECK(preview.startsWith(QStringLiteral("/model ")));
    // A second pick replaces the queued one in its place; it never stacks.
    console.selectModel(QStringLiteral("p-kimi"));
    CHECK_EQ(rowSummary(console), QStringList({"agent|queued", "model|queued"}));
    CHECK(setModels(sent).isEmpty());

    // Enter again: the pick, not the older prompt, goes in at the next tool call.
    enter();
    QList<QJsonObject> models = setModels(sent);
    CHECK_EQ(models.size(), 1);
    if (models.size() != 1) return;
    CHECK_EQ(models.first().value("when").toString(), QStringLiteral("steer"));
    CHECK_EQ(models.first().value("model").toString(), QStringLiteral("kimi"));
    CHECK_EQ(rowSummary(console), QStringList({"model|waiting", "agent|queued"}));
    CHECK_EQ(console.queuedPrompts(), 1);   // the prompt stays queued

    // Enter twice: interrupt and switch now.
    sent.clear();
    enter();
    models = setModels(sent);
    CHECK_EQ(models.size(), 1);
    if (models.size() != 1) return;
    CHECK_EQ(models.first().value("when").toString(), QStringLiteral("now"));
    CHECK_EQ(rowSummary(console), QStringList({"model|interrupting", "agent|queued"}));

    // The worker lands it as the stopped turn ends: the row goes, and the queue is not paused.
    console.deliverWorkerEvent(QJsonObject{{"event", "agent_finished"}, {"id", "running"}, {"outcome", "cancelled"}});
    console.deliverWorkerEvent(QJsonObject{{"event", "model_applied"}, {"model", "kimi"}, {"at", "turn_end"}});
    CHECK_EQ(rowSummary(console), QStringList({"agent|queued"}));
}

inline void aQueuedModelRunsInItsTurnAndHoldsTheLaneUntilAnswered() {
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    QList<QJsonObject> sent;
    console.onWorkerLine = [&sent](const QJsonObject &message) { sent << message; };
    console.deliverWorkerEvent(modelPresets());
    configureOnOld(console);
    console.deliverWorkerEvent(QJsonObject{{"event", "agent_started"}, {"id", "running"}});
    auto *editor = console.findChild<QPlainTextEdit *>(QStringLiteral("composerEditor"));
    CHECK(editor != nullptr);
    if (!editor) return;
    console.selectModel(QStringLiteral("p-fable"));
    console.draftInComposer(QStringLiteral("runs on fable"));
    QKeyEvent key(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(editor, &key);
    CHECK_EQ(rowSummary(console), QStringList({"model|queued", "agent|queued"}));

    // The turn ends: the switch goes first, idle (no `when`), and the prompt waits for its answer.
    sent.clear();
    console.deliverWorkerEvent(QJsonObject{{"event", "agent_finished"}, {"id", "running"}, {"outcome", "done"}});
    console.deliverWorkerEvent(QJsonObject{{"event", "queue_changed"}, {"running", QJsonValue()}, {"paused", false},
                                           {"items", QJsonArray{}}, {"steering", QJsonArray{}}});
    QCoreApplication::processEvents();
    QList<QJsonObject> models = setModels(sent);
    CHECK_EQ(models.size(), 1);
    if (models.size() != 1) return;
    CHECK(!models.first().contains("when"));
    CHECK_EQ(models.first().value("model").toString(), QStringLiteral("fable"));
    const auto asks = [&sent] {
        int n = 0;
        for (const QJsonObject &message : sent) if (message.value("type").toString() == QStringLiteral("ask")) ++n;
        return n;
    };
    CHECK_EQ(asks(), 0);
    CHECK_EQ(rowSummary(console), QStringList({"agent|queued"}));

    // Answered: the prompt behind it goes, on the new model.
    console.deliverWorkerEvent(QJsonObject{{"event", "model_changed"}, {"id", models.first().value("id")},
                                           {"model", "fable"}, {"preset", "p-fable"}, {"applies", "now"}});
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    CHECK_EQ(asks(), 1);
    CHECK(rowSummary(console).isEmpty());
}

inline void pickingTheModelInForceDropsAQueuedSwitch() {
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    QList<QJsonObject> sent;
    console.onWorkerLine = [&sent](const QJsonObject &message) { sent << message; };
    console.deliverWorkerEvent(modelPresets());
    configureOnOld(console);
    console.deliverWorkerEvent(QJsonObject{{"event", "agent_started"}, {"id", "running"}});
    console.selectModel(QStringLiteral("p-fable"));
    CHECK_EQ(rowSummary(console), QStringList({"model|queued"}));
    console.selectModel(QStringLiteral("p-old"));   // the chip never moved, so this is "stay"
    CHECK(rowSummary(console).isEmpty());
    CHECK(setModels(sent).isEmpty());
}

inline void aSteeredModelIsWithdrawnWithItsX() {
    StubContext context;
    context.workspace = home->path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    QList<QJsonObject> sent;
    console.onWorkerLine = [&sent](const QJsonObject &message) { sent << message; };
    console.deliverWorkerEvent(modelPresets());
    configureOnOld(console);
    console.deliverWorkerEvent(QJsonObject{{"event", "agent_started"}, {"id", "running"}});
    auto *editor = console.findChild<QPlainTextEdit *>(QStringLiteral("composerEditor"));
    CHECK(editor != nullptr);
    if (!editor) return;
    console.selectModel(QStringLiteral("p-fable"));
    QKeyEvent key(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(editor, &key);
    CHECK_EQ(rowSummary(console), QStringList({"model|waiting"}));
    sent.clear();
    CHECK(console.removeRow(QStringLiteral("model-steer")));
    CHECK_EQ(sent.size(), 1);
    if (sent.size() == 1) CHECK_EQ(sent.first().value("type").toString(), QStringLiteral("model_withdraw"));
    CHECK_EQ(rowSummary(console), QStringList({"model|withdrawing"}));
    console.deliverWorkerEvent(QJsonObject{{"event", "model_switch_withdrawn"}, {"withdrawn", true}, {"model", "fable"},
                                           {"current_model", "old"}, {"preset", "p-old"}});
    CHECK(rowSummary(console).isEmpty());
}

inline void modelQueueCases() {
    aModelPickedMidTurnQueuesAndWalksTheLadder();
    aQueuedModelRunsInItsTurnAndHoldsTheLaneUntilAnswered();
    pickingTheModelInForceDropsAQueuedSwitch();
    aSteeredModelIsWithdrawnWithItsX();
}

}  // namespace cases
