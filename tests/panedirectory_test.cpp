// SPDX-License-Identifier: AGPL-3.0-or-later
// Pane addresses and the cross-pane directory (card #R5TC, protocol section 37): handles are
// minted once and never reused, every refusal has its code, a busy pane gets a note, an idle one
// is woken, a sender whose turn was a wake cannot wake, and notify_when_idle fires once.
#include "PaneAddress.h"
#include "PaneDirectory.h"

#include <QTest>

using namespace relay;
using panedir::Directory;
using panedir::Note;
using panedir::PaneHooks;

namespace {
struct FakePane {
    QString title, workspace;
    bool busy = false, configured = true, guest = false;
    QList<Note> received;
    // What the pane does with an idle note: wake unless the sender may not wake it.
    QString decide(const Note &note) const {
        if (busy) return panedir::kDelivered;
        return note.mayWake ? panedir::kWoke : panedir::kNoWake;
    }
    PaneHooks hooks() {
        PaneHooks h;
        h.title = [this] { return title; };
        h.workspace = [this] { return workspace; };
        h.busy = [this] { return busy; };
        h.configured = [this] { return configured; };
        h.guestInFront = [this] { return guest; };
        h.deliver = [this](const Note &note) { received.append(note); return decide(note); };
        return h;
    }
};
}   // namespace

class PaneDirectoryTest : public QObject {
    Q_OBJECT
private slots:
    void init() { Directory::instance().resetForTest(); }

    void handles_are_minted_in_order_and_never_reused() {
        const int a = paneaddress::mint();
        const int b = paneaddress::mint();
        QCOMPARE(b, a + 1);
        FakePane one, two;
        Directory &dir = Directory::instance();
        const int h1 = dir.add(QStringLiteral("tok1"), one.hooks());
        dir.remove(QStringLiteral("tok1"));
        const int h2 = dir.add(QStringLiteral("tok2"), two.hooks());
        QVERIFY(h2 > h1);
        QCOMPARE(dir.add(QStringLiteral("tok2"), two.hooks()), h2);   // re-registering keeps it
    }

    void label_and_parse() {
        QCOMPARE(paneaddress::label(3), QStringLiteral("p3"));
        QVERIFY(paneaddress::label(0).isEmpty());
        QCOMPARE(paneaddress::parse(QStringLiteral("p3")), 3);
        QCOMPARE(paneaddress::parse(QStringLiteral(" P12 ")), 12);
        QCOMPARE(paneaddress::parse(QStringLiteral("3")), 3);
        QCOMPARE(paneaddress::parse(QStringLiteral("pane 4")), 4);
        QCOMPARE(paneaddress::parse(QStringLiteral("pane p4")), 4);
        QCOMPARE(paneaddress::parse(QStringLiteral("p2 · \"Release notes\"")), 2);
        QCOMPARE(paneaddress::parse(QStringLiteral("p0")), 0);
        QCOMPARE(paneaddress::parse(QStringLiteral("Release notes")), 0);
        QCOMPARE(paneaddress::parse(QStringLiteral("#K7Q2")), 0);
        QCOMPARE(paneaddress::parse(QStringLiteral("p3; rm -rf /")), 0);
    }

    void roster_lists_the_others_in_handle_order_with_state() {
        FakePane a{QStringLiteral("A"), QStringLiteral("/w/a")}, b{QStringLiteral("B"), QStringLiteral("/w/b")},
            c{QStringLiteral("C"), QStringLiteral("/w/c")}, d{QStringLiteral("D"), QStringLiteral("/w/d")};
        b.busy = true; c.configured = false; d.guest = true;
        Directory &dir = Directory::instance();
        dir.add(QStringLiteral("a"), a.hooks());
        const int hb = dir.add(QStringLiteral("b"), b.hooks());
        dir.add(QStringLiteral("c"), c.hooks());
        dir.add(QStringLiteral("d"), d.hooks());
        const QJsonArray rows = dir.roster(QStringLiteral("a"));
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows[0].toObject().value("pane").toString(), paneaddress::label(hb));
        QCOMPARE(rows[0].toObject().value("title").toString(), QStringLiteral("B"));
        QCOMPARE(rows[0].toObject().value("workspace").toString(), QStringLiteral("/w/b"));
        QCOMPARE(rows[0].toObject().value("state").toString(), QStringLiteral("busy"));
        QCOMPARE(rows[1].toObject().value("state").toString(), QStringLiteral("no agent"));
        QCOMPARE(rows[2].toObject().value("state").toString(), QStringLiteral("guest"));
    }

    void every_refusal_has_its_code() {
        FakePane a{QStringLiteral("A")}, b{QStringLiteral("B")}, c{QStringLiteral("C")}, d{QStringLiteral("D")},
            gone{QStringLiteral("Gone")};
        c.configured = false; d.guest = true;
        Directory &dir = Directory::instance();
        const int ha = dir.add(QStringLiteral("a"), a.hooks());
        const int hb = dir.add(QStringLiteral("b"), b.hooks());
        const int hc = dir.add(QStringLiteral("c"), c.hooks());
        const int hd = dir.add(QStringLiteral("d"), d.hooks());
        const int hg = dir.add(QStringLiteral("g"), gone.hooks());
        dir.remove(QStringLiteral("g"));
        auto code = [&](const QString &to, const QString &text = QStringLiteral("hi")) {
            return dir.send(QStringLiteral("a"), to, text, false, true).code;
        };
        QCOMPARE(code(QStringLiteral("p999999")), panedir::kUnknownPane);
        QCOMPARE(code(QStringLiteral("Release notes")), panedir::kUnknownPane);
        QCOMPARE(code(paneaddress::label(ha)), panedir::kSelf);
        QCOMPARE(code(paneaddress::label(hg)), panedir::kClosed);
        QCOMPARE(code(paneaddress::label(hc)), panedir::kNotConfigured);
        QCOMPARE(code(paneaddress::label(hd)), panedir::kNotSupported);
        QCOMPARE(code(paneaddress::label(hb), QStringLiteral("  \n")), panedir::kEmpty);
        QCOMPARE(dir.send(QStringLiteral("nobody"), paneaddress::label(hb), QStringLiteral("hi"), false, true).code,
                 panedir::kNotSupported);
        dir.setEnabled(false);
        QCOMPARE(code(paneaddress::label(hb)), panedir::kDisabled);
        QVERIFY(b.received.isEmpty() && c.received.isEmpty() && d.received.isEmpty());
        // A refusal carries the directory, so the model can correct itself without another call.
        dir.setEnabled(true);
        const panedir::Result r = dir.send(QStringLiteral("a"), QStringLiteral("p999999"), QStringLiteral("x"), false, true);
        QVERIFY(!r.ok);
        QCOMPARE(r.toJson().value("panes").toArray().size(), 3);
    }

    void idle_wakes_busy_delivers_and_a_woken_sender_cannot_wake() {
        FakePane a{QStringLiteral("A"), QStringLiteral("/w/a")}, b{QStringLiteral("B")};
        Directory &dir = Directory::instance();
        const int ha = dir.add(QStringLiteral("a"), a.hooks());
        const int hb = dir.add(QStringLiteral("b"), b.hooks());
        panedir::Result r = dir.send(QStringLiteral("a"), paneaddress::label(hb), QStringLiteral("hello"), false, true);
        QVERIFY(r.ok);
        QCOMPARE(r.outcome, panedir::kWoke);
        QCOMPARE(r.to, hb);
        QCOMPARE(b.received.size(), 1);
        QCOMPARE(b.received[0].from, ha);
        QCOMPARE(b.received[0].fromTitle, QStringLiteral("A"));
        QCOMPARE(b.received[0].fromWorkspace, QStringLiteral("/w/a"));
        QCOMPARE(b.received[0].text, QStringLiteral("hello"));
        b.busy = true;
        QCOMPARE(dir.send(QStringLiteral("a"), paneaddress::label(hb), QStringLiteral("x"), false, true).outcome,
                 panedir::kDelivered);
        b.busy = false;
        r = dir.send(QStringLiteral("a"), paneaddress::label(hb), QStringLiteral("x"), false, false);
        QCOMPARE(r.outcome, panedir::kNoWake);
        QVERIFY(!b.received.last().mayWake);
        QVERIFY(r.message.contains(QStringLiteral("cannot wake")));
    }

    void chain_a_b_c_does_not_wake_c() {
        // A (person) wakes B; B's woken turn sends to C with mayWake=false, as the pane passes it.
        FakePane a, b, c;
        Directory &dir = Directory::instance();
        dir.add(QStringLiteral("a"), a.hooks());
        const int hb = dir.add(QStringLiteral("b"), b.hooks());
        const int hc = dir.add(QStringLiteral("c"), c.hooks());
        QCOMPARE(dir.send(QStringLiteral("a"), paneaddress::label(hb), QStringLiteral("go"), false, true).outcome,
                 panedir::kWoke);
        QCOMPARE(dir.send(QStringLiteral("b"), paneaddress::label(hc), QStringLiteral("go on"), false, false).outcome,
                 panedir::kNoWake);
    }

    void notify_when_idle_fires_once() {
        FakePane a, b;
        b.busy = true;
        Directory &dir = Directory::instance();
        const int ha = dir.add(QStringLiteral("a"), a.hooks());
        const int hb = dir.add(QStringLiteral("b"), b.hooks());
        Q_UNUSED(ha);
        const panedir::Result r =
            dir.send(QStringLiteral("a"), paneaddress::label(hb), QStringLiteral("tell me"), true, true);
        QVERIFY(r.message.contains(QStringLiteral("do not poll")));
        dir.send(QStringLiteral("a"), paneaddress::label(hb), QStringLiteral("again"), true, true);   // no double
        b.busy = false;
        dir.wentIdle(QStringLiteral("b"));
        QCOMPARE(a.received.size(), 1);
        QVERIFY(a.received[0].idle);
        QCOMPARE(a.received[0].from, hb);
        dir.wentIdle(QStringLiteral("b"));
        QCOMPARE(a.received.size(), 1);
    }

    void closing_drops_subscriptions() {
        FakePane a, b;
        Directory &dir = Directory::instance();
        dir.add(QStringLiteral("a"), a.hooks());
        const int hb = dir.add(QStringLiteral("b"), b.hooks());
        b.busy = true;
        dir.send(QStringLiteral("a"), paneaddress::label(hb), QStringLiteral("x"), true, true);
        dir.remove(QStringLiteral("a"));
        dir.wentIdle(QStringLiteral("b"));
        QVERIFY(a.received.isEmpty());
    }

    void printable_strips_what_could_forge_chrome() {
        const QString in = QStringLiteral("ok\x1b[31mred\r\n‮evil\u0007\tend");
        const QString out = Directory::printable(in);
        QVERIFY(!out.contains(QChar(0x1b)));
        QVERIFY(!out.contains(QChar(0x202E)));
        QVERIFY(!out.contains(QChar(0x07)));
        QVERIFY(!out.contains('\r'));
        QVERIFY(out.contains('\n') && out.contains('\t'));
        QVERIFY(Directory::printable(QString(40000, 'x')).toUtf8().size() <= 16 * 1024);
    }
};

QTEST_GUILESS_MAIN(PaneDirectoryTest)
#include "panedirectory_test.moc"
