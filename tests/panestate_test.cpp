// SPDX-License-Identifier: AGPL-3.0-or-later
// relay::panestate: the v1 `pane_state` message a phone draws (docs/REMOTE-PROTOCOL.md section 16),
// the actions each queue row offers, the desktop-minted ids, and the coalescing publisher.
#include "PaneState.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

using namespace relay::panestate;

namespace {
Row steer(const QString &id, const QString &text, const QString &state = QStringLiteral("waiting")) {
    return Row{QStringLiteral("steer:") + id, QStringLiteral("steer"), text, state, false};
}
Row entry(int id, const QString &kind, const QString &text, const QString &state = QStringLiteral("queued"),
          bool written = false) {
    return Row{QStringLiteral("entry:%1").arg(id), kind, text, state, written};
}
Inputs busyPane() {
    Inputs in;
    in.pane = QStringLiteral("tok-1");
    in.busy = true;
    in.clock = QStringLiteral("thinking · 12 s · step 1/256 · Esc stops");
    in.thinkingVisible = true;
    in.thinkingHeader = QStringLiteral("Thinking… · fake · 12 s");
    in.thinkingText = QStringLiteral("Let me look at the readme.");
    in.running = QStringLiteral("✦ please plan this out");
    in.rows = {steer(QStringLiteral("steer-3"), QStringLiteral("check the readme")),
               entry(4, QStringLiteral("agent"), QStringLiteral("then run the tests")),
               entry(5, QStringLiteral("command"), QStringLiteral("git status"))};
    in.queueHint = queueHint(false, false, false);
    in.modelLabel = QStringLiteral("fake · local");
    in.choices = {{QStringLiteral("preset:kimi-k2"), QStringLiteral("Kimi K2"), false},
                  {QStringLiteral("role:main"), QStringLiteral("Main · fake"), true}};
    in.mode = QStringLiteral("auto");
    in.placeholder = QStringLiteral("Shell commands or agent prompts…");
    in.contextLabel = QStringLiteral("96% left");
    in.percentLeft = 96;
    in.allowanceLabel = QStringLiteral("Free · 73% left");
    in.allowanceLeft = 73;
    in.allowanceDetail = QStringLiteral("182,400 of 250,000 tokens today · resets at 02:00");
    in.theme = QStringLiteral("relay-dark");
    in.sessions = {{QStringLiteral("0f3a-session"), QStringLiteral("Thinking copy test"), QStringLiteral("14:02"), true, true}};
    in.canNew = false;
    return in;
}
QStringList strings(const QJsonValue &value) {
    QStringList out;
    for (const QJsonValue &item : value.toArray()) out << item.toString();
    return out;
}
}   // namespace

class PaneStateTest : public QObject {
    Q_OBJECT
private slots:
    // ----- the shape ---------------------------------------------------------------------------
    void the_message_has_every_v1_field() {
        Tokens choices(QLatin1Char('m')), sessions(QLatin1Char('s'));
        const QJsonObject state = build(42, busyPane(), choices, sessions);
        QCOMPARE(state.value("t").toString(), QStringLiteral("pane_state"));
        QCOMPARE(state.value("v").toInt(), 1);
        QCOMPARE(state.value("pane").toString(), QStringLiteral("tok-1"));
        QCOMPARE(state.value("seq").toInt(), 42);
        QCOMPARE(state.value("theme").toString(), QStringLiteral("relay-dark"));
        for (const char *key : {"turn", "thinking", "queue", "model", "composer", "context", "sessions"})
            QVERIFY2(state.value(key).isObject(), key);
        const QJsonObject turn = state.value("turn").toObject();
        QCOMPARE(turn.value("phase").toString(), QStringLiteral("thinking"));
        QCOMPARE(turn.value("clock").toString(), QStringLiteral("thinking · 12 s · step 1/256 · Esc stops"));
        QVERIFY(turn.value("busy").toBool());
        const QJsonObject thinking = state.value("thinking").toObject();
        QVERIFY(thinking.value("visible").toBool());
        QCOMPARE(thinking.value("header").toString(), QStringLiteral("Thinking… · fake · 12 s"));
        QCOMPARE(thinking.value("tail").toString(), QStringLiteral("Let me look at the readme."));
        const QJsonObject queue = state.value("queue").toObject();
        QCOMPARE(queue.value("paused").toBool(), false);
        QCOMPARE(queue.value("running").toObject().value("label").toString(), QStringLiteral("✦ please plan this out"));
        QCOMPARE(queue.value("hint").toString(), QStringLiteral("↑ select a row · Ctrl+↑↓ move · Shift+Del remove"));
        const QJsonArray rows = queue.value("rows").toArray();
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows[0].toObject().value("id").toString(), QStringLiteral("steer:steer-3"));
        QCOMPARE(rows[0].toObject().value("label").toString(), QStringLiteral("↪ next tool call  ✦ check the readme"));
        QCOMPARE(rows[1].toObject().value("label").toString(), QStringLiteral("✦ then run the tests"));
        QCOMPARE(rows[2].toObject().value("label").toString(), QStringLiteral("$ git status"));
        QCOMPARE(rows[2].toObject().value("kind").toString(), QStringLiteral("command"));
        const QJsonObject composer = state.value("composer").toObject();
        QCOMPARE(composer.value("mode").toString(), QStringLiteral("auto"));
        QCOMPARE(strings(composer.value("modes")), (QStringList{"auto", "shell", "agent"}));
        QCOMPARE(state.value("context").toObject().value("percent_left").toInt(), 96);
        const QJsonObject allowance = state.value("allowance").toObject();
        QCOMPARE(allowance.value("label").toString(), QStringLiteral("Free · 73% left"));
        QCOMPARE(allowance.value("percent_left").toInt(), 73);
        QCOMPARE(allowance.value("warn").toBool(), false);
        QCOMPARE(allowance.value("detail").toString(),
                 QStringLiteral("182,400 of 250,000 tokens today · resets at 02:00"));
        QCOMPARE(state.value("sessions").toObject().value("can_new").toBool(), false);
    }
    void nothing_running_is_null_not_an_empty_label() {
        Tokens choices(QLatin1Char('m')), sessions(QLatin1Char('s'));
        Inputs in;
        in.pane = QStringLiteral("tok");
        const QJsonObject state = build(1, in, choices, sessions);
        QVERIFY(state.value("queue").toObject().value("running").isNull());
        QCOMPARE(state.value("turn").toObject().value("phase").toString(), QStringLiteral("idle"));
        QCOMPARE(state.value("turn").toObject().value("clock").toString(), QString());
        QVERIFY(state.value("context").toObject().value("percent_left").isNull());
        QCOMPARE(state.value("queue").toObject().value("hint").toString(), QString());
    }
    void no_allowance_when_there_is_none_to_show() {
        // Not on the hosted preset, or no quota figure yet: no object at all, so a view that drew
        // one takes its chip off (the pane moved to a provider with a key).
        Tokens choices(QLatin1Char('m')), sessions(QLatin1Char('s'));
        Inputs in;
        in.pane = QStringLiteral("tok");
        in.contextLabel = QStringLiteral("96% left");
        in.percentLeft = 96;
        QVERIFY(!build(1, in, choices, sessions).contains(QStringLiteral("allowance")));
        in.allowanceLabel = QStringLiteral("Free · 73% left");   // and with one, it is there
        QVERIFY(build(1, in, choices, sessions).contains(QStringLiteral("allowance")));
    }
    void the_theme_is_published_by_id_or_not_at_all() {
        // The phone's pane follows the desktop's theme (owner, 2026-09-19): the id goes in the
        // message, and a pane with no theme to name publishes no field rather than an empty one.
        Tokens choices(QLatin1Char('m')), sessions(QLatin1Char('s'));
        Inputs in;
        in.pane = QStringLiteral("tok");
        QVERIFY(!build(1, in, choices, sessions).contains(QStringLiteral("theme")));
        in.theme = QStringLiteral("   ");
        QVERIFY(!build(1, in, choices, sessions).contains(QStringLiteral("theme")));
        in.theme = QStringLiteral("relay-light");
        QCOMPARE(build(1, in, choices, sessions).value("theme").toString(), QStringLiteral("relay-light"));
    }
    void the_allowance_warns_at_ten_percent_and_below() {
        Tokens choices(QLatin1Char('m')), sessions(QLatin1Char('s'));
        Inputs in;
        in.pane = QStringLiteral("tok");
        in.allowanceLabel = QStringLiteral("Free · %1% left");
        for (const int left : {8, 10}) {
            in.allowanceLeft = left;
            const QJsonObject allowance = build(1, in, choices, sessions).value("allowance").toObject();
            QCOMPARE(allowance.value("percent_left").toInt(), left);
            QCOMPARE(allowance.value("warn").toBool(), true);
        }
        in.allowanceLeft = 11;
        QCOMPARE(build(1, in, choices, sessions).value("allowance").toObject().value("warn").toBool(), false);
        in.allowanceLeft = -1;   // unknown: null, and never warned
        const QJsonObject allowance = build(1, in, choices, sessions).value("allowance").toObject();
        QVERIFY(allowance.value("percent_left").isNull());
        QCOMPARE(allowance.value("warn").toBool(), false);
    }
    void the_phase_follows_the_turn() {
        Inputs in;
        QCOMPARE(phase(in), QStringLiteral("idle"));
        in.busy = true;
        QCOMPARE(phase(in), QStringLiteral("thinking"));
        in.toolRunning = true;
        QCOMPARE(phase(in), QStringLiteral("tool"));
        in.waiting = true;
        QCOMPARE(phase(in), QStringLiteral("waiting"));
        in.busy = false; in.toolRunning = false;
        QCOMPARE(phase(in), QStringLiteral("waiting"));
    }

    // ----- caps ----------------------------------------------------------------------------------
    void labels_tails_and_lists_are_capped() {
        Tokens choices(QLatin1Char('m')), sessions(QLatin1Char('s'));
        Inputs in;
        in.pane = QStringLiteral("tok");
        in.modelLabel = QString(1000, QLatin1Char('x'));
        in.thinkingText = QString(5000, QLatin1Char('y')) + QStringLiteral("END");
        for (int i = 0; i < 100; ++i) in.rows << entry(i + 1, QStringLiteral("agent"), QStringLiteral("prompt %1").arg(i));
        for (int i = 0; i < 80; ++i) in.sessions << Session{QStringLiteral("s-%1").arg(i), QStringLiteral("t"), QStringLiteral("now"), false, false};
        const QJsonObject state = build(1, in, choices, sessions);
        QCOMPARE(state.value("model").toObject().value("label").toString().size(), kLabelMax);
        QVERIFY(state.value("model").toObject().value("label").toString().endsWith(QChar(0x2026)));
        const QString tail = state.value("thinking").toObject().value("tail").toString();
        QCOMPARE(tail.size(), kTailMax);
        QVERIFY(tail.endsWith(QStringLiteral("END")));   // the newest reasoning, not the oldest
        QCOMPARE(state.value("queue").toObject().value("rows").toArray().size(), kRowsMax);
        QCOMPARE(state.value("sessions").toObject().value("rows").toArray().size(), kSessionsMax);
        // The last row shown is not the last row there is: it can still move down.
        const QJsonArray rows = state.value("queue").toObject().value("rows").toArray();
        QVERIFY(strings(rows.last().toObject().value("actions")).contains(QStringLiteral("down")));
    }

    // ----- the actions each row offers -----------------------------------------------------------
    void a_waiting_steer_can_be_removed_edited_requeued_or_sent_now() {
        QCOMPARE(rowActions(steer("a", "x"), true, -1, 0),
                 (QStringList{"remove", "edit", "to_queue", "send_now"}));
        // Send now interrupts the running turn; with none running there is nothing to interrupt.
        QCOMPARE(rowActions(steer("a", "x"), false, -1, 0), (QStringList{"remove", "edit", "to_queue"}));
    }
    void a_steer_on_its_way_out_or_on_the_desktop_offers_nothing() {
        QVERIFY(rowActions(steer("a", "x", "withdrawing"), true, -1, 0).isEmpty());
        QVERIFY(rowActions(steer("a", "x", "editing"), true, -1, 0).isEmpty());
    }
    void a_queued_prompt_moves_and_steers_like_the_keys_say() {
        const Row head = entry(1, "agent", "a");
        QCOMPARE(rowActions(head, true, 0, 3), (QStringList{"remove", "edit", "steer", "down"}));
        QCOMPARE(rowActions(head, true, 1, 3), (QStringList{"remove", "edit", "steer", "up", "down"}));
        QCOMPARE(rowActions(head, true, 2, 3), (QStringList{"remove", "edit", "steer", "up"}));
        QCOMPARE(rowActions(head, false, 0, 1), (QStringList{"remove", "edit"}));
    }
    void a_command_is_never_a_steer() {
        QCOMPARE(rowActions(entry(1, "command", "ls"), true, 0, 2), (QStringList{"remove", "edit", "down"}));
    }
    void a_row_relay_wrote_is_neither_edited_nor_steered() {
        QCOMPARE(rowActions(entry(1, "agent", "fix request", "queued", true), true, 0, 1), (QStringList{"remove"}));
    }
    void a_row_being_edited_on_the_desktop_is_left_alone() {
        QVERIFY(rowActions(entry(1, "agent", "a", "editing"), true, 0, 2).isEmpty());
    }
    void a_paused_row_is_still_actionable() {
        QCOMPARE(rowActions(entry(1, "agent", "a", "paused"), false, 0, 1), (QStringList{"remove", "edit"}));
    }
    void actions_for_finds_a_row_by_its_id() {
        const Inputs in = busyPane();
        QCOMPARE(actionsFor(in, QStringLiteral("entry:5")), (QStringList{"remove", "edit", "up"}));
        QCOMPARE(actionsFor(in, QStringLiteral("steer:steer-3")), (QStringList{"remove", "edit", "to_queue", "send_now"}));
        QVERIFY(actionsFor(in, QStringLiteral("entry:99")).isEmpty());
        QVERIFY(actionsFor(in, QStringLiteral("running")).isEmpty());
    }
    void a_withdrawing_steer_says_so_in_its_label() {
        QCOMPARE(rowLabel(steer("a", "x", "withdrawing")), QStringLiteral("↪ next tool call  ✦ x  withdrawing…"));
    }

    // ----- ids -----------------------------------------------------------------------------------
    void no_preset_id_or_session_file_name_is_published() {
        Tokens choices(QLatin1Char('m')), sessions(QLatin1Char('s'));
        Inputs in = busyPane();
        in.sessions = {{QStringLiteral("/home/elliott/.local/share/relay/sessions/0f3a.json"), QStringLiteral("t"), QStringLiteral("now"), false, false}};
        const QByteArray text = QJsonDocument(build(1, in, choices, sessions)).toJson(QJsonDocument::Compact);
        QVERIFY2(!text.contains("kimi-k2"), text.constData());
        QVERIFY(!text.contains("preset:"));
        QVERIFY(!text.contains("role:"));
        QVERIFY(!text.contains("/home/"));
        QVERIFY(!text.contains("0f3a"));
        const QJsonObject state = build(2, in, choices, sessions);
        const QJsonArray published = state.value("model").toObject().value("choices").toArray();
        QCOMPARE(published[0].toObject().value("id").toString(), QStringLiteral("m1"));
        QCOMPARE(published[1].toObject().value("id").toString(), QStringLiteral("m2"));
        QCOMPARE(choices.keyFor(QStringLiteral("m1")), QStringLiteral("preset:kimi-k2"));
        QCOMPARE(state.value("sessions").toObject().value("rows").toArray()[0].toObject().value("id").toString(), QStringLiteral("s1"));
    }
    void ids_are_stable_per_key_and_never_reused() {
        Tokens tokens(QLatin1Char('m'));
        QCOMPARE(tokens.idFor("preset:a"), QStringLiteral("m1"));
        QCOMPARE(tokens.idFor("preset:b"), QStringLiteral("m2"));
        QCOMPARE(tokens.idFor("preset:a"), QStringLiteral("m1"));   // the same preset keeps its id
        QCOMPARE(tokens.keyFor("m2"), QStringLiteral("preset:b"));
        QCOMPARE(tokens.keyFor("m9"), QString());                    // an id nobody minted is nothing
        QCOMPARE(tokens.keyFor("preset:a"), QString());              // and a key is not an id
    }

    // ----- the publisher -------------------------------------------------------------------------
    void nothing_is_gathered_while_nobody_listens() {
        int gathered = 0, sent = 0;
        bool listening = false;
        Publisher publisher([&] { ++gathered; return busyPane(); }, [&](const QJsonObject &) { ++sent; },
                            [&] { return listening; }, 20);
        for (int i = 0; i < 50; ++i) publisher.changed();
        QTest::qWait(80);
        QCOMPARE(gathered, 0);
        QCOMPARE(sent, 0);
        QVERIFY(publisher.publishNow().isEmpty());
        listening = true;
        publisher.changed();
        QTRY_COMPARE(sent, 1);
    }
    void a_burst_of_changes_is_one_message_carrying_the_latest() {
        QList<QJsonObject> sent;
        QString tail;
        Publisher publisher([&] { Inputs in = busyPane(); in.thinkingText = tail; return in; },
                            [&](const QJsonObject &state) { sent << state; }, [] { return true; }, 100);
        for (int i = 0; i < 200; ++i) { tail += QStringLiteral("chunk %1 ").arg(i); publisher.changed(); }
        QTRY_COMPARE(sent.size(), 1);
        QVERIFY(sent[0].value("thinking").toObject().value("tail").toString().endsWith(QStringLiteral("chunk 199 ")));
        QCOMPARE(sent[0].value("seq").toInt(), 1);
        // Changes inside the next interval wait for it, and are again one message.
        QElapsedTimer clock; clock.start();
        tail += QStringLiteral("more");
        publisher.changed();
        tail += QStringLiteral(" and more");
        publisher.changed();
        QTRY_COMPARE(sent.size(), 2);
        QVERIFY2(clock.elapsed() >= 60, "the second message came inside the interval");
        QCOMPARE(sent[1].value("seq").toInt(), 2);
        QVERIFY(sent[1].value("thinking").toObject().value("tail").toString().endsWith(QStringLiteral("more and more")));
    }
    void an_unchanged_state_is_not_sent_again() {
        int sent = 0;
        Publisher publisher([] { return busyPane(); }, [&](const QJsonObject &) { ++sent; }, [] { return true; }, 10);
        publisher.changed();
        QTRY_COMPARE(sent, 1);
        publisher.changed();
        QTest::qWait(60);
        QCOMPARE(sent, 1);
        // Asked for, it is sent whatever changed: a phone that just focused the pane needs it.
        const QJsonObject now = publisher.publishNow();
        QCOMPARE(sent, 2);
        QCOMPARE(now.value("seq").toInt(), 2);
        QCOMPARE(now.value("pane").toString(), QStringLiteral("tok-1"));
    }
    // What the pane puts in `choices` is the presets that already have a stored key plus the
    // agent roles (Pane::remoteState), and `model_pick` checks that list again before switching
    // (Pane::remoteModelPick). This pins the half that lives here: whatever the pane offered, the
    // message carries an opaque token and the desktop's own label — never the key of the choice,
    // so a preset id cannot reach a phone through this field and cannot be guessed back.
    void a_choice_carries_a_token_and_a_label_but_never_its_key() {
        Inputs in = busyPane();
        in.choices = {Choice{QStringLiteral("preset:glm-coding"), QStringLiteral("GLM · Main"), true},
                      Choice{QStringLiteral("role:flash"), QStringLiteral("Flash · Kimi K2"), false}};
        Tokens choices(QLatin1Char('m')), sessions(QLatin1Char('s'));
        const QJsonObject state = build(7, in, choices, sessions);
        const QJsonArray rows = state.value("model").toObject().value("choices").toArray();
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows[0].toObject().value("id").toString(), QStringLiteral("m1"));
        QCOMPARE(rows[0].toObject().value("label").toString(), QStringLiteral("GLM · Main"));
        QCOMPARE(rows[0].toObject().keys(), QStringList({"current", "id", "label"}));
        const QString whole = QString::fromUtf8(QJsonDocument(state).toJson());
        QVERIFY(!whole.contains(QStringLiteral("glm-coding")));
        QVERIFY(!whole.contains(QStringLiteral("preset:")));
        QVERIFY(!whole.contains(QStringLiteral("role:")));
        QCOMPARE(choices.keyFor(QStringLiteral("m1")), QStringLiteral("preset:glm-coding"));
        QVERIFY(choices.keyFor(QStringLiteral("m9")).isEmpty());   // never minted here
    }

    void a_published_token_resolves_back_on_the_desktop() {
        Publisher publisher([] { return busyPane(); }, [](const QJsonObject &) {}, [] { return true; });
        const QJsonObject state = publisher.publishNow();
        const QString id = state.value("model").toObject().value("choices").toArray()[1].toObject().value("id").toString();
        QCOMPARE(publisher.choiceKey(id), QStringLiteral("role:main"));
        const QString session = state.value("sessions").toObject().value("rows").toArray()[0].toObject().value("id").toString();
        QCOMPARE(publisher.sessionKey(session), QStringLiteral("0f3a-session"));
        QCOMPARE(publisher.choiceKey(QStringLiteral("role:main")), QString());
    }
};

QTEST_GUILESS_MAIN(PaneStateTest)
#include "panestate_test.moc"
