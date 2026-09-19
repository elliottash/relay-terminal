// SPDX-License-Identifier: GPL-3.0-or-later
// Relay-to-Relay (src/RemotePane.h): the screen painter's cell grid from `screen_snapshot` and
// `screen_diff`, the scrollback column app/screen.js keeps, the key table, and a pane drawn from
// `pane_state` offering only what each row lists — plus control, compose and the sidecar contract,
// through a fake viewer script.
#include "RemotePane.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

using namespace relay;
using namespace relay::remoteview;

namespace {

QJsonArray run(const QString &text, double fg = 0, double bg = 0, int attrs = 0)
{
    return QJsonArray{text, fg, bg, attrs};
}

QJsonObject line(int row, const QJsonArray &segs)
{
    return {{"row", row}, {"segs", segs}};
}

QString rowText(const QVector<Cell> &cells)
{
    QString out;
    for (const Cell &cell : cells) if (!cell.tail) out += cell.text;
    return out;
}

constexpr double palette(int index) { return double((1u << 24) | quint32(index)); }
constexpr double rgb(quint32 value) { return double((2u << 24) | value); }

QJsonObject snapshot(int rows, int cols, int base)
{
    QJsonArray lines;
    lines.append(line(0, QJsonArray{run(QStringLiteral("$ ls"), palette(2), 0, Bold)}));
    lines.append(line(1, QJsonArray{run(QStringLiteral("a.txt  "), 0, 0), run(QStringLiteral("b"), rgb(0x112233), palette(4), Underline)}));
    return {{"t", "screen_snapshot"}, {"pane", "p1"}, {"rows", rows}, {"cols", cols}, {"alt", false},
            {"cursor", QJsonObject{{"row", 2}, {"col", 2}, {"visible", true}}}, {"base", base}, {"history", base},
            {"lines", lines}};
}

QJsonObject historyReply(qint64 from, int count, const QString &id = QString())
{
    QJsonArray lines;
    for (int i = 0; i < count; ++i)
        lines.append(line(int(from + i), QJsonArray{run(QStringLiteral("old %1").arg(from + i))}));
    QJsonObject reply{{"t", "history"}, {"pane", "p1"}, {"from_row", double(from)}, {"lines", lines}, {"more", from > 0}};
    if (!id.isEmpty()) reply.insert(QStringLiteral("id"), id);
    return reply;
}

QJsonObject paneState(qint64 seq, bool busy)
{
    QJsonArray rows{
        QJsonObject{{"id", "steer:s1"}, {"kind", "steer"}, {"label", "↪ next tool call  ✦ check the <b>readme</b>"},
                    {"state", "waiting"}, {"actions", QJsonArray{"remove", "to_queue", "send_now"}}},
        QJsonObject{{"id", "entry:4"}, {"kind", "agent"}, {"label", "✦ then run the tests"}, {"state", "queued"},
                    {"actions", QJsonArray{"edit", "steer", "down"}}},
        QJsonObject{{"id", "entry:5"}, {"kind", "command"}, {"label", "$ make"}, {"state", "queued"},
                    {"actions", QJsonArray{}}},
    };
    return {{"t", "pane_state"}, {"v", 1}, {"pane", "p1"}, {"seq", double(seq)},
            {"turn", QJsonObject{{"phase", busy ? "thinking" : "idle"}, {"clock", "thinking · 12 s"}, {"busy", busy}}},
            {"thinking", QJsonObject{{"visible", true}, {"header", "Thinking… · fake · 12 s"}, {"tail", "the last words"}}},
            {"queue", QJsonObject{{"paused", false}, {"running", QJsonObject{{"label", "✦ please plan"}}}, {"rows", rows},
                                  {"hint", "↑ select a row"}}},
            {"model", QJsonObject{{"label", "fake · local"},
                                  {"choices", QJsonArray{QJsonObject{{"id", "m1"}, {"label", "Kimi K2 · Main"}, {"current", false}},
                                                         QJsonObject{{"id", "m2"}, {"label", "fake · local"}, {"current", true}}}}}},
            {"composer", QJsonObject{{"mode", "auto"}, {"placeholder", "Ask or run…"}, {"modes", QJsonArray{"auto", "shell", "agent"}}}},
            {"context", QJsonObject{{"label", "12% left"}, {"percent_left", 12}}},
            {"sessions", QJsonObject{{"can_new", true}, {"can_open", true},
                                     {"rows", QJsonArray{QJsonObject{{"id", "s1"}, {"title", "this one"}, {"when", "14:02"}, {"current", true}},
                                                         QJsonObject{{"id", "s2"}, {"title", "an older one"}, {"when", "13:00"}, {"current", false}}}}}}};
}

struct Sent {
    QList<QJsonObject> messages;
    RemotePane::Sink sink() { return [this](const QJsonObject &m) { messages.append(m); }; }
    QList<QJsonObject> of(const QString &type) const {
        QList<QJsonObject> out;
        for (const QJsonObject &m : messages) if (m.value(QStringLiteral("t")).toString() == type) out.append(m);
        return out;
    }
};

} // namespace

class RemotePaneTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);   // hint counts stay out of ~
        // The join dialog remembers the name and server; that stays out of ~ too.
        QVERIFY(m_settings.isValid());
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, m_settings.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }
    // ---- the painter's grid ----------------------------------------------------------------------
    void snapshotThenDiffGivesTheCells()
    {
        ScreenModel model;
        const ScreenModel::Applied first = model.apply(snapshot(3, 10, 40));
        QVERIFY(first.snapshot);
        QCOMPARE(first.rows.size(), 3);
        QCOMPARE(model.cols(), 10);
        QCOMPARE(model.liveBase(), 40);

        QVector<Cell> row0 = model.cellsAt(0);
        QCOMPARE(row0.size(), 10);
        QCOMPARE(rowText(row0), QStringLiteral("$ ls      "));
        QCOMPARE(row0.at(0).fg, quint32(palette(2)));
        QVERIFY(row0.at(0).attrs & Bold);
        QVector<Cell> row1 = model.cellsAt(1);
        QCOMPARE(row1.at(7).text, QStringLiteral("b"));
        QCOMPARE(row1.at(7).fg, quint32(rgb(0x112233)));
        QCOMPARE(row1.at(7).bg, quint32(palette(4)));
        QVERIFY(row1.at(7).attrs & Underline);
        QVERIFY(!row1.at(6).attrs);
        // The cursor is on the empty third row, column 2.
        QVector<Cell> row2 = model.cellsAt(2);
        QVERIFY(row2.at(2).cursor);
        QCOMPARE(std::count_if(row2.begin(), row2.end(), [](const Cell &c) { return c.cursor; }), 1);

        // A diff changes one row and moves the cursor; the untouched row stays.
        const QJsonObject diff{{"t", "screen_diff"}, {"pane", "p1"}, {"base", 41},
                               {"cursor", QJsonObject{{"row", 1}, {"col", 3}, {"visible", true}}},
                               {"lines", QJsonArray{line(1, QJsonArray{run(QStringLiteral("中x"), palette(1), 0, Reverse)})}}};
        const ScreenModel::Applied second = model.apply(diff);
        QVERIFY(!second.snapshot);
        QCOMPARE(second.rows, QVector<int>{1});
        QCOMPARE(model.liveBase(), 41);
        row1 = model.cellsAt(1);
        // A wide glyph takes two cells, the second a tail; the next glyph is in column 2.
        QCOMPARE(row1.at(0).text, QStringLiteral("中"));
        QVERIFY(row1.at(1).tail);
        QCOMPARE(row1.at(2).text, QStringLiteral("x"));
        QVERIFY(row1.at(2).attrs & Reverse);
        QVERIFY(row1.at(3).cursor);
        QVERIFY(!model.cellsAt(2).at(2).cursor);
        QCOMPARE(rowText(model.cellsAt(0)), QStringLiteral("$ ls      "));
    }

    void colorsAreTheThemesThenXterms()
    {
        QVector<QColor> ansi(16, QColor(Qt::magenta));
        ansi[1] = QColor(0x10, 0x20, 0x30);
        QCOMPARE(colorOf(quint32(palette(1)), ansi), QColor(0x10, 0x20, 0x30));
        QCOMPARE(colorOf(quint32(palette(16)), ansi), QColor(0, 0, 0));
        QCOMPARE(colorOf(quint32(palette(231)), ansi), QColor(255, 255, 255));
        QCOMPARE(colorOf(quint32(palette(232)), ansi), QColor(8, 8, 8));
        QCOMPARE(colorOf(quint32(rgb(0xabcdef)), ansi), QColor(0xab, 0xcd, 0xef));
        QVERIFY(!colorOf(0, ansi).isValid());   // default: the terminal's own
    }

    void concealedTextIsBlank()
    {
        const QVector<Cell> cells = cellsOf(segsOf(QJsonArray{run(QStringLiteral("secret"), 0, 0, Conceal)}), 8);
        QCOMPARE(rowText(cells), QStringLiteral("        "));
    }

    // ---- scrollback, as screen.js pages it ---------------------------------------------------------
    void historyPagesJoinAtBaseAndCloseTheSeam()
    {
        ScreenModel model;
        QVERIFY(!model.nextRequest(0, 24).want);   // nothing before the first frame
        model.apply(snapshot(3, 10, 200));
        // The first page ends at `base`, not at the newest row the desktop holds.
        ScreenModel::Request request = model.nextRequest(0, 24);
        QVERIFY(request.want);
        QCOMPARE(request.before, 200);
        QCOMPARE(request.count, ScreenModel::kPage);
        QVERIFY(!model.nextRequest(0, 24).want);   // one in flight at a time

        ScreenModel::Inserted inserted = model.applyHistory(historyReply(120, 80));
        QVERIFY(inserted.top);
        QCOMPARE(inserted.added, 80);
        QCOMPARE(model.historyTop(), 120);
        QCOMPARE(model.historyBottom(), 200);
        QCOMPARE(model.columnRows(), 83);
        QCOMPARE(rowText(model.cellsAt(0)).trimmed(), QStringLiteral("old 120"));
        QCOMPARE(rowText(model.cellsAt(80)).trimmed(), QStringLiteral("$ ls"));

        // Output while the reader is up in the history: the live block starts further down.
        model.apply(QJsonObject{{"t", "screen_diff"}, {"base", 205}, {"lines", QJsonArray{}}});
        QCOMPARE(model.gapRows(), 5);
        request = model.nextRequest(10, 24);
        QVERIFY(request.want);   // the seam first, and a small gap is closed at once
        QCOMPARE(request.before, 205);
        QCOMPARE(request.count, 5);
        inserted = model.applyHistory(historyReply(200, 5));
        QVERIFY(!inserted.top);
        QCOMPARE(inserted.added, 5);
        QCOMPARE(model.gapRows(), 0);
        QCOMPARE(model.historyBottom(), 205);

        // Near the top, the next page up; far from it, nothing.
        QVERIFY(!model.nextRequest(60, 24).want);
        request = model.nextRequest(5, 24);
        QVERIFY(request.want);
        QCOMPARE(request.before, 120);
        // A page already held is not painted twice.
        inserted = model.applyHistory(historyReply(100, 30));
        QCOMPARE(inserted.added, 20);
        QCOMPARE(model.historyTop(), 100);

        // The scrollback shrank under what is held (a clear): the rows are no longer those rows.
        model.apply(QJsonObject{{"t", "screen_diff"}, {"base", 3}, {"lines", QJsonArray{}}});
        QCOMPARE(model.historySize(), 0);
        QCOMPARE(model.historyTop(), -1);
    }

    void aSnapshotOfNewGeometryDropsHistory()
    {
        ScreenModel model;
        model.apply(snapshot(3, 10, 50));
        model.nextRequest(0, 3);
        model.applyHistory(historyReply(0, 50));
        QCOMPARE(model.historySize(), 50);
        QVERIFY(!model.more());
        model.apply(snapshot(3, 10, 50));   // same geometry: a repaint, the join holds
        QCOMPARE(model.historySize(), 50);
        const ScreenModel::Applied applied = model.apply(snapshot(5, 12, 50));
        QVERIFY(applied.broke);
        QCOMPARE(model.historySize(), 0);
    }

    void trimKeepsTheNewestAndOnlyWhatIsOffScreen()
    {
        ScreenModel model;
        model.apply(snapshot(3, 10, 5000));
        for (qint64 before = 5000; model.historySize() < ScreenModel::kMax + 100; before -= 200) {
            model.nextRequest(0, 3);
            model.applyHistory(historyReply(before - 200, 200));
        }
        QCOMPARE(model.historySize(), ScreenModel::kMax + 200);
        QCOMPARE(model.trim(10), 0);   // the reader is at the top: nothing above them goes
        const int dropped = model.trim(1000);
        QCOMPARE(dropped, 200);
        QCOMPARE(model.historyBottom(), 5000);
        QVERIFY(model.more());
    }

    // ---- keys --------------------------------------------------------------------------------
    void keyTable()
    {
        QCOMPARE(keyBytes(Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r")), QByteArray("\r"));
        QCOMPARE(keyBytes(Qt::Key_Up, Qt::NoModifier, {}), QByteArray("\x1b[A"));
        QCOMPARE(keyBytes(Qt::Key_Left, Qt::AltModifier, {}), QByteArray("\x1b\x1b[D"));
        QCOMPARE(keyBytes(Qt::Key_C, Qt::ControlModifier, QStringLiteral("\x03")), QByteArray("\x03"));
        QCOMPARE(keyBytes(Qt::Key_Space, Qt::ControlModifier, {}), QByteArray(1, '\0'));
        QCOMPARE(keyBytes(Qt::Key_A, Qt::NoModifier, QStringLiteral("a")), QByteArray("a"));
        QCOMPARE(keyBytes(Qt::Key_B, Qt::AltModifier, QStringLiteral("b")), QByteArray("\x1b" "b"));
        QCOMPARE(keyBytes(Qt::Key_F5, Qt::NoModifier, {}), QByteArray("\x1b[15~"));
        QCOMPARE(keyBytes(Qt::Key_unknown, Qt::NoModifier, QStringLiteral("é")), QByteArray("é"));
        QVERIFY(keyBytes(Qt::Key_Shift, Qt::ShiftModifier, {}).isEmpty());
        QVERIFY(keyBytes(Qt::Key_T, Qt::ControlModifier | Qt::ShiftModifier, {}).isEmpty());   // the app's
    }

    // ---- pane_state → widgets ------------------------------------------------------------------
    void onlyTheListedActionsAreOffered()
    {
        Sent sent;
        RemotePane pane(QStringLiteral("p1"), QStringLiteral("shell"), QStringLiteral("desk"), sent.sink());
        pane.setCapability(QStringLiteral("full"), {QStringLiteral("screen"), QStringLiteral("history"), QStringLiteral("pane_state")});
        pane.handle(paneState(3, true));

        QVERIFY(pane.rowHasRemoveButton(QStringLiteral("steer:s1")));
        QVERIFY(!pane.rowHasRemoveButton(QStringLiteral("entry:4")));
        QVERIFY(!pane.rowHasRemoveButton(QStringLiteral("entry:5")));
        QCOMPARE(pane.rowMenuActions(QStringLiteral("steer:s1")),
                 (QStringList{QStringLiteral("send_now"), QStringLiteral("to_queue"), QStringLiteral("remove")}));
        QCOMPARE(pane.rowMenuActions(QStringLiteral("entry:4")),
                 (QStringList{QStringLiteral("edit"), QStringLiteral("steer"), QStringLiteral("down")}));
        QVERIFY(pane.rowMenuActions(QStringLiteral("entry:5")).isEmpty());

        // Asking for one the row does not list sends nothing.
        sent.messages.clear();
        pane.triggerRowAction(QStringLiteral("entry:4"), QStringLiteral("remove"));
        pane.triggerRowAction(QStringLiteral("entry:5"), QStringLiteral("edit"));
        QVERIFY(sent.messages.isEmpty());
        pane.triggerRowAction(QStringLiteral("entry:4"), QStringLiteral("steer"));
        pane.triggerRowAction(QStringLiteral("steer:s1"), QStringLiteral("remove"));
        pane.triggerRowAction(QStringLiteral("steer:s1"), QStringLiteral("send_now"));
        pane.triggerRowAction(QStringLiteral("entry:4"), QStringLiteral("edit"));
        QCOMPARE(sent.messages.size(), 4);
        QCOMPARE(sent.messages.at(0), (QJsonObject{{"t", "queue_move"}, {"row", "entry:4"}, {"to", "steer"}, {"pane", "p1"}}));
        QCOMPARE(sent.messages.at(1), (QJsonObject{{"t", "queue_remove"}, {"row", "steer:s1"}, {"pane", "p1"}}));
        QCOMPARE(sent.messages.at(2), (QJsonObject{{"t", "queue_send_now"}, {"row", "steer:s1"}, {"pane", "p1"}}));
        QCOMPARE(sent.messages.at(3), (QJsonObject{{"t", "queue_edit"}, {"row", "entry:4"}, {"pane", "p1"}}));

        // Clicking the × sends the withdrawal.
        sent.messages.clear();
        auto *rows = pane.findChild<QListWidget *>(QStringLiteral("remoteRows"));
        QVERIFY(rows);
        QCOMPARE(rows->count(), 3);
        QToolButton *x = nullptr;
        for (QToolButton *button : rows->findChildren<QToolButton *>(QStringLiteral("remoteRowRemove")))
            if (button->property("rowId").toString() == QLatin1String("steer:s1")) x = button;
        QVERIFY(x);
        x->click();
        QCOMPARE(sent.of(QStringLiteral("queue_remove")).size(), 1);

        // Labels are plain text: the markup in one is drawn, not interpreted.
        const auto labels = rows->findChildren<QLabel *>(QStringLiteral("remoteRowLabel"));
        bool sawMarkup = false;
        for (QLabel *label : labels) {
            QCOMPARE(label->textFormat(), Qt::PlainText);
            sawMarkup = sawMarkup || label->text().contains(QStringLiteral("<b>readme</b>"));
        }
        QVERIFY(sawMarkup);

        // The queue_edit answer lands in this pane's own prompt box.
        pane.handle(QJsonObject{{"t", "queue_edit_text"}, {"pane", "p1"}, {"row", "entry:4"}, {"text", "then run the tests"}});
        QCOMPARE(pane.promptBox()->toPlainText(), QStringLiteral("then run the tests"));

        // An older state does not overwrite a newer one; a state without rows clears them.
        QJsonObject empty = paneState(2, false);
        empty.insert(QStringLiteral("queue"), QJsonObject{{"rows", QJsonArray{}}});
        pane.handle(empty);
        QCOMPARE(rows->count(), 3);
        empty.insert(QStringLiteral("seq"), 9);
        pane.handle(empty);
        QCOMPARE(rows->count(), 0);
        // Another pane's state is not this one's.
        QJsonObject other = paneState(10, true);
        other.insert(QStringLiteral("pane"), QStringLiteral("p2"));
        pane.handle(other);
        QCOMPARE(rows->count(), 0);
    }

    void modelAndConversations()
    {
        Sent sent;
        RemotePane pane(QStringLiteral("p1"), QStringLiteral("shell"), QStringLiteral("desk"), sent.sink());
        pane.setCapability(QStringLiteral("full"), {});
        pane.handle(paneState(1, false));
        QCOMPARE(pane.modelMenuLabels(), (QStringList{QStringLiteral("Kimi K2 · Main"), QStringLiteral("fake · local")}));
        sent.messages.clear();
        pane.pickModel(0);
        QCOMPARE(sent.messages.value(0), (QJsonObject{{"t", "model_pick"}, {"choice", "m1"}, {"pane", "p1"}}));

        QCOMPARE(pane.conversationMenuLabels(),
                 (QStringList{QStringLiteral("New conversation"), QStringLiteral("this one   14:02"), QStringLiteral("an older one   13:00")}));
        sent.messages.clear();
        pane.triggerConversation(1);   // the current one: never opened again
        QVERIFY(sent.messages.isEmpty());
        pane.triggerConversation(2);
        pane.triggerConversation(0);
        QCOMPARE(sent.messages.value(0), (QJsonObject{{"t", "conversation_open"}, {"session", "s2"}, {"pane", "p1"}}));
        QCOMPARE(sent.messages.value(1), (QJsonObject{{"t", "conversation_new"}, {"pane", "p1"}}));

        // A partner's state has no sessions block and no choices: nothing to pick, nothing to open.
        QJsonObject partner = paneState(2, false);
        partner.remove(QStringLiteral("sessions"));
        partner.insert(QStringLiteral("model"), QJsonObject{{"label", "fake · local"}});
        pane.handle(partner);
        QVERIFY(pane.modelMenuLabels().isEmpty());
        QVERIFY(pane.conversationMenuLabels().isEmpty());
        sent.messages.clear();
        pane.pickModel(0);
        pane.triggerConversation(0);
        QVERIFY(sent.messages.isEmpty());
    }

    void composeRoutesAndQueuesWhileBusy()
    {
        Sent sent;
        RemotePane pane(QStringLiteral("p1"), QStringLiteral("shell"), QStringLiteral("desk"), sent.sink());
        pane.handle(paneState(1, false));
        sent.messages.clear();
        pane.promptBox()->setPlainText(QStringLiteral("ls -la"));
        QTest::keyClick(pane.promptBox(), Qt::Key_Return);
        QList<QJsonObject> composed = sent.of(QStringLiteral("compose"));
        QCOMPARE(composed.size(), 1);
        QCOMPARE(composed.at(0).value(QStringLiteral("text")).toString(), QStringLiteral("ls -la"));
        QCOMPARE(composed.at(0).value(QStringLiteral("when")).toString(), QStringLiteral("now"));
        QCOMPARE(composed.at(0).value(QStringLiteral("agent")), QJsonValue(false));   // the desktop routes it
        QVERIFY(!composed.at(0).value(QStringLiteral("msg_id")).toString().isEmpty());
        QVERIFY(pane.promptBox()->toPlainText().isEmpty());

        // Busy: Enter queues. Without routing modes, no agent:false.
        QJsonObject busy = paneState(2, true);
        busy.insert(QStringLiteral("composer"), QJsonObject{{"mode", "agent"}, {"modes", QJsonArray{"agent"}}});
        pane.handle(busy);
        sent.messages.clear();
        pane.promptBox()->setPlainText(QStringLiteral("and then?"));
        QTest::keyClick(pane.promptBox(), Qt::Key_Return);
        composed = sent.of(QStringLiteral("compose"));
        QCOMPARE(composed.size(), 1);
        QCOMPARE(composed.at(0).value(QStringLiteral("when")).toString(), QStringLiteral("queue"));
        QVERIFY(!composed.at(0).contains(QStringLiteral("agent")));
        // Enter on the empty box makes it a steer (it has not appeared as a row yet: by text).
        QTest::keyClick(pane.promptBox(), Qt::Key_Return);
        composed = sent.of(QStringLiteral("compose"));
        QCOMPARE(composed.size(), 2);
        QCOMPARE(composed.at(1).value(QStringLiteral("when")).toString(), QStringLiteral("steer"));
        // Ctrl+Enter: now, interrupting.
        pane.promptBox()->setPlainText(QStringLiteral("stop that"));
        QTest::keyClick(pane.promptBox(), Qt::Key_Return, Qt::ControlModifier);
        composed = sent.of(QStringLiteral("compose"));
        QCOMPARE(composed.last().value(QStringLiteral("when")).toString(), QStringLiteral("now"));

        // A view-only state has no composer: no prompt box.
        QJsonObject viewer = paneState(5, false);
        viewer.remove(QStringLiteral("composer"));
        pane.show();
        pane.handle(viewer);
        QVERIFY(!pane.promptBox()->isVisible());
    }

    // ---- control, as the phone follows it ------------------------------------------------------
    void keysNeedTheKeyboardAndTheOwnersKeyTakesItBack()
    {
        Sent sent;
        RemotePane pane(QStringLiteral("p1"), QStringLiteral("shell"), QStringLiteral("desk"), sent.sink());
        pane.setCapability(QStringLiteral("full"), {QStringLiteral("screen")});
        pane.handle(snapshot(3, 10, 0));
        sent.messages.clear();

        // Watching: a key sends nothing and says why.
        QTest::keyClick(pane.screen(), Qt::Key_L);
        QVERIFY(sent.of(QStringLiteral("keys")).isEmpty());
        QVERIFY(pane.noteText().contains(QStringLiteral("Take the keyboard")));

        pane.requestControl();
        QCOMPARE(sent.of(QStringLiteral("control_request")).size(), 1);
        // The handoff that answers our own request names this device.
        pane.handle(QJsonObject{{"t", "control"}, {"pane", "p1"}, {"holder", "owner"}, {"name", "desk"}, {"device", "dev-me"}});
        QVERIFY(pane.driving());
        QCOMPARE(pane.myDevice(), QStringLiteral("dev-me"));
        QTest::keyClicks(pane.screen(), QStringLiteral("ls"));
        QTest::keyClick(pane.screen(), Qt::Key_Return);
        QTest::keyClick(pane.screen(), Qt::Key_C, Qt::ControlModifier);
        const QList<QJsonObject> keys = sent.of(QStringLiteral("keys"));
        QCOMPARE(keys.size(), 4);
        QByteArray typed;
        for (const QJsonObject &k : keys) typed += QByteArray::fromBase64(k.value(QStringLiteral("bytes")).toString().toLatin1());
        QCOMPARE(typed, QByteArray("ls\r\x03"));
        QCOMPARE(keys.at(0).value(QStringLiteral("pane")).toString(), QStringLiteral("p1"));

        // The owner's own keystroke at the desktop: the holder is the owner with no device.
        pane.handle(QJsonObject{{"t", "control"}, {"pane", "p1"}, {"holder", "owner"}, {"name", "desk"}});
        QVERIFY(!pane.driving());
        QVERIFY(pane.noteText().contains(QStringLiteral("took the keyboard back")));
        sent.messages.clear();
        QTest::keyClick(pane.screen(), Qt::Key_X);
        QVERIFY(sent.of(QStringLiteral("keys")).isEmpty());

        // Taken again, then another of the owner's devices has it: not us.
        pane.requestControl();
        pane.handle(QJsonObject{{"t", "control"}, {"pane", "p1"}, {"holder", "owner"}, {"device", "dev-me"}});
        QVERIFY(pane.driving());
        pane.handle(QJsonObject{{"t", "control"}, {"pane", "p1"}, {"holder", "owner"}, {"device", "dev-phone"}});
        QVERIFY(!pane.driving());
        // A control for another pane changes nothing here.
        pane.handle(QJsonObject{{"t", "control"}, {"pane", "p2"}, {"holder", "owner"}, {"device", "dev-me"}});
        QVERIFY(!pane.driving());

        // A view device is never offered the keyboard.
        Sent other;
        RemotePane viewOnly(QStringLiteral("p1"), QString(), QString(), other.sink());
        viewOnly.setCapability(QStringLiteral("view"), {});
        viewOnly.requestControl();
        QVERIFY(other.of(QStringLiteral("control_request")).isEmpty());
    }

    void historyRequestsCarryAnIdAndStaleOnesAreDropped()
    {
        Sent sent;
        RemotePane pane(QStringLiteral("p1"), QStringLiteral("shell"), QStringLiteral("desk"), sent.sink());
        pane.resize(600, 400);
        pane.setCapability(QStringLiteral("full"), {QStringLiteral("screen"), QStringLiteral("history")});
        pane.handle(snapshot(3, 10, 100));
        const QList<QJsonObject> asks = sent.of(QStringLiteral("history_get"));
        QCOMPARE(asks.size(), 1);
        QCOMPARE(asks.at(0).value(QStringLiteral("before_row")).toDouble(), 100.0);
        const QString id = asks.at(0).value(QStringLiteral("id")).toString();
        QVERIFY(!id.isEmpty());
        pane.handle(historyReply(20, 80, QStringLiteral("stale")));
        QCOMPARE(pane.screen()->model().historySize(), 0);
        pane.handle(historyReply(20, 80, id));
        QCOMPARE(pane.screen()->model().historySize(), 80);
        // The reader is still at the live end after the page went in above.
        QVERIFY(pane.screen()->atBottom());
    }

    // ---- the sidecar contract -----------------------------------------------------------------
    void viewerSpeaksTheContract()
    {
        QTemporaryDir dir;
        const QString script = dir.filePath(QStringLiteral("fake_viewer.py"));
        const QString log = dir.filePath(QStringLiteral("in.jsonl"));
        QFile file(script);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QStringLiteral(R"PY(
import json, sys
log = open(%1, "a")
def out(o):
    sys.stdout.write(json.dumps(o) + "\n"); sys.stdout.flush()
for raw in sys.stdin:
    m = json.loads(raw); log.write(raw); log.flush()
    if m["t"] == "connect":
        out({"t": "status", "state": "unpaired", "message": ""})
    elif m["t"] == "pair":
        out({"t": "status", "state": "pairing"})
        out({"t": "code", "code": "12345"})
        out({"t": "paired", "desktop": "desk", "fingerprint": "ab:cd"})
        out({"t": "welcome", "capability": "full", "features": ["screen", "history", "pane_state"], "device": "dev-laptop"})
        out({"t": "status", "state": "connected"})
        out({"t": "message", "message": {"t": "panes", "items": [{"id": "p1", "title": "shell", "cwd": "/w"}]}})
    elif m["t"] == "open":
        out({"t": "message", "message": {"t": "screen_snapshot", "pane": m["pane"], "rows": 2, "cols": 5,
             "base": 0, "cursor": {"row": 0, "col": 0}, "lines": [{"row": 0, "segs": [["hi", 0, 0, 0]]}]}})
    elif m["t"] == "stop":
        break
)PY").arg(QString::fromLatin1(QJsonDocument(QJsonArray{log}).toJson(QJsonDocument::Compact)).mid(1).chopped(1)).toUtf8());
        file.close();
        qputenv("RELAY_REMOTE_VIEWER", script.toLocal8Bit());

        QPointer<RemotePane> opened;
        auto *dialog = new RemotePaneDialog([&opened](RemotePane *pane) { opened = pane; });
        dialog->show();
        auto *link = dialog->findChild<QLineEdit *>(QStringLiteral("remotePairLink"));
        QVERIFY(link);
        QTRY_VERIFY(link->isVisible());
        link->setText(QStringLiteral("https://desk.example/pair#v=1&s=secret"));
        dialog->findChild<QPushButton *>(QStringLiteral("remotePair"))->click();
        auto *list = dialog->findChild<QListWidget *>(QStringLiteral("remotePanes"));
        QTRY_COMPARE(list->count(), 1);
        QTRY_VERIFY(list->isVisible());
        QCOMPARE(RemoteViewer::instance().desktop(), QStringLiteral("desk"));
        dialog->findChild<QPushButton *>(QStringLiteral("remoteOpen"))->click();
        QVERIFY(opened);
        delete dialog;   // the window places the pane; the dialog is gone
        QTRY_VERIFY(opened->screen()->model().hasFrame());
        QCOMPARE(opened->paneTitle(), QStringLiteral("shell · desk"));
        QCOMPARE(opened->myDevice(), QStringLiteral("dev-laptop"));
        QCOMPARE(RemoteViewer::instance().openPanes(), 1);
        delete opened.data();
        QTRY_VERIFY(!RemoteViewer::instance().running());

        QFile in(log);
        QVERIFY(in.open(QIODevice::ReadOnly));
        QStringList kinds;
        QJsonObject pair;
        for (const QByteArray &raw : in.readAll().split('\n')) {
            if (raw.trimmed().isEmpty()) continue;
            const QJsonObject m = QJsonDocument::fromJson(raw).object();
            const QString t = m.value(QStringLiteral("t")).toString();
            kinds << (t == QLatin1String("send") ? QStringLiteral("send:") + m.value(QStringLiteral("message")).toObject().value(QStringLiteral("t")).toString() : t);
            if (t == QLatin1String("pair")) pair = m;
        }
        QCOMPARE(pair.value(QStringLiteral("platform")).toString(), QStringLiteral("Relay"));
        QCOMPARE(pair.value(QStringLiteral("url")).toString(), QStringLiteral("https://desk.example/pair#v=1&s=secret"));
        QVERIFY(!pair.value(QStringLiteral("name")).toString().isEmpty());
        QCOMPARE(kinds.mid(0, 2), (QStringList{QStringLiteral("connect"), QStringLiteral("pair")}));
        QVERIFY(kinds.contains(QStringLiteral("open")));
        QVERIFY(kinds.contains(QStringLiteral("close")));
        QCOMPARE(kinds.last(), QStringLiteral("stop"));
        qunsetenv("RELAY_REMOTE_VIEWER");
    }

    // ---- a guest: somebody else's share, joined with a code and PIN ------------------------------
    void guestPaneHidesTheOwnersControls()
    {
        Sent sent;
        RemotePane pane(QStringLiteral("p1"), QStringLiteral("shell"), QStringLiteral("Ana"), sent.sink());
        pane.setCapability(QStringLiteral("guest"), {});
        pane.setRole(QStringLiteral("viewer"));
        QVERIFY(pane.guest());
        // A viewer: no composer, no asking to type, and a line that says whose pane it is.
        QVERIFY(!pane.findChild<QWidget *>(QStringLiteral("remoteComposer"))->isVisibleTo(&pane));
        QVERIFY(!pane.findChild<QPushButton *>(QStringLiteral("remoteTake"))->isVisibleTo(&pane));
        QCOMPARE(pane.driveText(), QStringLiteral("Watching Ana's pane"));

        // An editor: the prompt box, but none of the owner's strip — even if a pane_state came.
        pane.setRole(QStringLiteral("editor"));
        pane.handle(paneState(1, true));
        QVERIFY(pane.findChild<QWidget *>(QStringLiteral("remoteComposer"))->isVisibleTo(&pane));
        QVERIFY(pane.promptBox()->isVisibleTo(&pane));
        QVERIFY(!pane.findChild<QToolButton *>(QStringLiteral("remoteModel"))->isVisibleTo(&pane));
        QVERIFY(!pane.findChild<QToolButton *>(QStringLiteral("remoteSessions"))->isVisibleTo(&pane));
        QVERIFY(!pane.findChild<QWidget *>(QStringLiteral("remoteQueue"))->isVisibleTo(&pane));
        QVERIFY(pane.modelMenuLabels().isEmpty());
        QVERIFY(pane.conversationMenuLabels().isEmpty());
        QCOMPARE(pane.driveText(), QStringLiteral("Ana's pane · You can ask to type; your prompts wait for Ana"));
        auto *take = pane.findChild<QPushButton *>(QStringLiteral("remoteTake"));
        QVERIFY(take->isVisibleTo(&pane));
        QCOMPARE(take->text(), QStringLiteral("Ask to type"));

        // A prompt queues for the owner's approval, never routed to the shell, and says so once.
        pane.promptBox()->setPlainText(QStringLiteral("run the tests"));
        pane.sendPrompt();
        QCOMPARE(sent.of(QStringLiteral("compose")).size(), 1);
        const QJsonObject compose = sent.of(QStringLiteral("compose")).at(0);
        QCOMPARE(compose.value(QStringLiteral("when")).toString(), QStringLiteral("queue"));
        QVERIFY(!compose.contains(QStringLiteral("agent")));
        QVERIFY(pane.noteText().contains(QStringLiteral("approve")));

        // Asking to type is a request: the keyboard is ours only when `control` names us.
        pane.handle({{"t", "participants"}, {"pane", "p1"},
                     {"items", QJsonArray{QJsonObject{{"id", "g7"}, {"name", "me"}, {"role", "editor"}, {"you", true}}}}});
        pane.requestControl();
        QCOMPARE(sent.of(QStringLiteral("control_request")).size(), 1);
        QVERIFY(!pane.driving());
        QCOMPARE(pane.driveText(), QStringLiteral("Asked Ana to let you type…"));
        pane.handle({{"t", "control"}, {"pane", "p1"}, {"holder", "participant:g7"}, {"name", "me"}});
        QVERIFY(pane.driving());
        pane.handle({{"t", "control"}, {"pane", "p1"}, {"holder", "owner"}, {"name", "Ana"}});
        QVERIFY(!pane.driving());
        QVERIFY(pane.noteText().contains(QStringLiteral("took the keyboard back")));

        // Demoted in the participants list: the composer goes.
        pane.handle({{"t", "participants"}, {"pane", "p1"},
                     {"items", QJsonArray{QJsonObject{{"id", "g7"}, {"name", "me"}, {"role", "viewer"}, {"you", true}}}}});
        QCOMPARE(pane.role(), QStringLiteral("viewer"));
        QVERIFY(!pane.findChild<QWidget *>(QStringLiteral("remoteComposer"))->isVisibleTo(&pane));
    }

    void endedGuestPaneIsReadOnly()
    {
        Sent sent;
        RemotePane pane(QStringLiteral("p1"), QStringLiteral("shell"), QStringLiteral("Ana"), sent.sink());
        pane.setCapability(QStringLiteral("guest"), {});
        pane.setRole(QStringLiteral("editor"));
        pane.handle(snapshot(3, 10, 0));
        pane.markEnded(QStringLiteral("Ana ended your access."));
        QVERIFY(pane.ended());
        QCOMPARE(pane.noteText(), QStringLiteral("Ana ended your access."));
        QCOMPARE(pane.driveText(), QStringLiteral("Ana ended your access."));
        QVERIFY(!pane.findChild<QWidget *>(QStringLiteral("remoteComposer"))->isVisibleTo(&pane));
        QVERIFY(!pane.findChild<QPushButton *>(QStringLiteral("remoteTake"))->isVisibleTo(&pane));
        const int before = int(sent.messages.size());
        pane.requestControl();
        pane.promptBox()->setPlainText(QStringLiteral("hello"));
        pane.sendPrompt();
        pane.handle({{"t", "control"}, {"pane", "p1"}, {"holder", "participant:g7"}});
        QCOMPARE(int(sent.messages.size()), before);
        QVERIFY(!pane.driving());
        // A reconnect does not paper over the reason.
        pane.setConnection(QStringLiteral("connected"), QString());
        QCOMPARE(pane.noteText(), QStringLiteral("Ana ended your access."));
    }

    void joinDialogJoinsAndFollowsTheScope()
    {
        QTemporaryDir dir;
        const QString log = dir.filePath(QStringLiteral("in.jsonl"));
        const QString script = writeGuestViewer(dir, log);
        qputenv("RELAY_REMOTE_VIEWER", script.toLocal8Bit());

        struct Placed { QPointer<RemotePane> pane; bool first; };
        QList<Placed> placed;
        auto *dialog = new JoinDialog(QStringLiteral("bqrt"), [&placed](RemotePane *pane, bool first) {
            placed.append({pane, first});
        });
        QPointer<JoinDialog> alive(dialog);
        dialog->show();
        auto *code = dialog->findChild<QLineEdit *>(QStringLiteral("joinCode"));
        auto *pin = dialog->findChild<QLineEdit *>(QStringLiteral("joinPin"));
        auto *name = dialog->findChild<QLineEdit *>(QStringLiteral("joinName"));
        auto *join = dialog->findChild<QPushButton *>(QStringLiteral("joinButton"));
        QCOMPARE(code->text(), QStringLiteral("BQRT"));
        QCOMPARE(pin->echoMode(), QLineEdit::Password);
        QVERIFY(!name->text().isEmpty());
        QVERIFY(!dialog->findChild<QLineEdit *>(QStringLiteral("joinServer"))->isVisibleTo(dialog));
        QVERIFY(!join->isEnabled());
        name->setText(QStringLiteral("Bea"));
        pin->setText(QStringLiteral("4829"));
        QVERIFY(join->isEnabled());
        join->click();
        // Page two: the knock and the code the owner sees.
        auto *check = dialog->findChild<QLabel *>(QStringLiteral("joinCheckCode"));
        QTRY_COMPARE(check->text(), QStringLiteral("12345"));
        // Who the host is is not known before they let us in.
        QTRY_COMPARE(dialog->findChild<QLabel *>(QStringLiteral("joinWait"))->text(),
                     QStringLiteral("Waiting for the person sharing to let you in. They see the code"));
        // The fake admits on a line of its own: every pane in scope is placed, the first as first.
        RemoteViewer::guest().send({{"t", "admit"}});
        QTRY_COMPARE(placed.size(), 2);
        QTRY_VERIFY(!alive || !alive->isVisible());
        delete alive.data();   // the window would have; the session outlives it
        QCOMPARE(placed.at(0).first, true);
        QCOMPARE(placed.at(1).first, false);
        QCOMPARE(placed.at(0).pane->paneId(), QStringLiteral("p1"));
        QCOMPARE(placed.at(1).pane->paneId(), QStringLiteral("p2"));
        QVERIFY(placed.at(0).pane->guest());
        QCOMPARE(placed.at(0).pane->role(), QStringLiteral("editor"));
        QTRY_VERIFY(placed.at(0).pane->screen()->model().hasFrame());
        QCOMPARE(QSettings().value(QStringLiteral("remote/joinName")).toString(), QStringLiteral("Bea"));
        QVERIFY(GuestSession::current());
        QCOMPARE(RemoteViewer::guest().openPanes(), 2);
        // The owner's own device viewer was never started.
        QVERIFY(!RemoteViewer::instance().running());

        // A tab shared whole grows: the new pane is placed, not as first.
        RemoteViewer::guest().send({{"t", "grow"}});
        QTRY_COMPARE(placed.size(), 3);
        QCOMPARE(placed.at(2).pane->paneId(), QStringLiteral("p3"));
        QCOMPARE(placed.at(2).first, false);
        // ...and shrinks: the pane that left is marked, not closed.
        RemoteViewer::guest().send({{"t", "shrink"}});
        QTRY_VERIFY(placed.at(0).pane->ended());
        QCOMPARE(placed.at(0).pane->noteText(), QStringLiteral("No longer shared with you."));
        QVERIFY(!placed.at(1).pane->ended());

        // The owner ends it: every pane says so and goes read-only.
        RemoteViewer::guest().send({{"t", "end"}});
        QTRY_VERIFY(placed.at(1).pane->ended());
        QVERIFY(placed.at(2).pane->ended());
        QCOMPARE(placed.at(1).pane->noteText(), QStringLiteral("Ana ended your access."));
        QVERIFY(!placed.at(1).pane->findChild<QWidget *>(QStringLiteral("remoteComposer"))->isVisibleTo(placed.at(1).pane));
        QTRY_VERIFY(!GuestSession::current());

        for (const Placed &p : placed) delete p.pane.data();
        QTRY_VERIFY(!RemoteViewer::guest().running());
        const QList<QJsonObject> in = readLog(log);
        QJsonObject joinLine;
        for (const QJsonObject &m : in) if (m.value(QStringLiteral("t")).toString() == QLatin1String("join")) joinLine = m;
        QCOMPARE(joinLine.value(QStringLiteral("code")).toString(), QStringLiteral("BQRT"));
        QCOMPARE(joinLine.value(QStringLiteral("pin")).toString(), QStringLiteral("4829"));
        QCOMPARE(joinLine.value(QStringLiteral("name")).toString(), QStringLiteral("Bea"));
        QCOMPARE(joinLine.value(QStringLiteral("platform")).toString(), QStringLiteral("Relay"));
        QCOMPARE(joinLine.value(QStringLiteral("rendezvous")).toString(), QString::fromLatin1(JoinDialog::kDefaultServer));
        QCOMPARE(in.first().value(QStringLiteral("argv")).toString(), QStringLiteral("--guest"));
        qunsetenv("RELAY_REMOTE_VIEWER");
    }

    void joinDialogWrongPinGoesBack()
    {
        QTemporaryDir dir;
        const QString script = writeGuestViewer(dir, dir.filePath(QStringLiteral("in.jsonl")));
        qputenv("RELAY_REMOTE_VIEWER", script.toLocal8Bit());
        int placed = 0;
        auto *dialog = new JoinDialog(QStringLiteral("BQRT"), [&placed](RemotePane *pane, bool) { ++placed; delete pane; });
        dialog->show();
        auto *pin = dialog->findChild<QLineEdit *>(QStringLiteral("joinPin"));
        auto *error = dialog->findChild<QLabel *>(QStringLiteral("joinError"));
        auto *pages = dialog->findChild<QStackedWidget *>();
        QWidget *form = pages->currentWidget();
        pin->setText(QStringLiteral("1111"));   // the fake's wrong PIN
        dialog->findChild<QPushButton *>(QStringLiteral("joinButton"))->click();
        QVERIFY(pages->currentWidget() != form);
        QTRY_COMPARE(pages->currentWidget(), form);
        QVERIFY(pin->text().isEmpty());
        QVERIFY(error->isVisibleTo(dialog));
        QCOMPARE(error->text(), QStringLiteral("That PIN is not right."));
        QCOMPARE(placed, 0);
        // The status that follows the refusal leaves the form where it is.
        QTest::qWait(100);
        QCOMPARE(pages->currentWidget(), form);
        delete dialog;
        QTRY_VERIFY(!RemoteViewer::guest().running());
        qunsetenv("RELAY_REMOTE_VIEWER");
    }

private:
    QTemporaryDir m_settings;

    // A guest viewer that knocks, is admitted on `admit`, grows and shrinks the scope on `grow` and
    // `shrink`, ends on `end`, and refuses the PIN 1111.
    static QString writeGuestViewer(QTemporaryDir &dir, const QString &log)
    {
        const QString script = dir.filePath(QStringLiteral("fake_guest_viewer.py"));
        QFile file(script);
        file.open(QIODevice::WriteOnly);
        file.write(QStringLiteral(R"PY(
import json, sys
log = open(%1, "a")
log.write(json.dumps({"t": "argv", "argv": " ".join(sys.argv[1:])}) + "\n"); log.flush()
def out(o):
    sys.stdout.write(json.dumps(o) + "\n"); sys.stdout.flush()
def panes(ids):
    out({"t": "message", "message": {"t": "panes", "items": [{"id": i, "title": "t-" + i} for i in ids]}})
for raw in sys.stdin:
    m = json.loads(raw); log.write(raw); log.flush()
    t = m["t"]
    if t == "join":
        out({"t": "status", "state": "joining"})
        if m["pin"] == "1111":
            out({"t": "error", "message": "That PIN is not right.", "reason": "wrong_pin"})
            out({"t": "status", "state": "unjoined"})
            continue
        out({"t": "status", "state": "knocking", "message": ""})
        out({"t": "code", "code": "12345"})
    elif t == "admit":
        out({"t": "joined", "desktop": "Ana", "role": "editor", "panes": ["p1", "p2"], "expires": 1900000000})
        out({"t": "welcome", "capability": "guest", "role": "editor", "features": ["screen", "history"], "participant": "g7"})
        out({"t": "status", "state": "connected"})
        panes(["p1", "p2"])
    elif t == "grow":
        panes(["p1", "p2", "p3"])
    elif t == "shrink":
        panes(["p2", "p3"])
    elif t == "end":
        out({"t": "ended", "message": "Ana ended your access."})
    elif t == "open":
        out({"t": "message", "message": {"t": "screen_snapshot", "pane": m["pane"], "rows": 2, "cols": 5,
             "base": 0, "cursor": {"row": 0, "col": 0}, "lines": [{"row": 0, "segs": [["hi", 0, 0, 0]]}]}})
    elif t == "stop":
        break
)PY").arg(QString::fromLatin1(QJsonDocument(QJsonArray{log}).toJson(QJsonDocument::Compact)).mid(1).chopped(1)).toUtf8());
        return script;
    }

    static QList<QJsonObject> readLog(const QString &path)
    {
        QList<QJsonObject> out;
        QFile in(path);
        if (!in.open(QIODevice::ReadOnly)) return out;
        for (const QByteArray &raw : in.readAll().split('\n'))
            if (!raw.trimmed().isEmpty()) out.append(QJsonDocument::fromJson(raw).object());
        return out;
    }
};

QTEST_MAIN(RemotePaneTest)
#include "remotepane_test.moc"
