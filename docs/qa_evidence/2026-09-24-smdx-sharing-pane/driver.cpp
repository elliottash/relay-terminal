// SPDX-License-Identifier: AGPL-3.0-or-later
// Drives the real SharingView (librelay-sharing) through the states card #SMDX names and saves a
// PNG of each. No sidecar and no window: the model is fed the same JSON the sidecar sends, and the
// hooks are recorded so the run also says which calls each state made.
#include "SharingPane.h"

#include <QApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTextStream>

using namespace relay::sharing;

static QJsonObject json(const char *text) { return QJsonDocument::fromJson(text).object(); }
static QJsonArray items(const char *text) { return QJsonDocument::fromJson(text).array(); }

static QrMatrix fakeQr(int n = 29) {
    QrMatrix m;
    for (int y = 0; y < n; ++y) {
        QVector<int> row;
        for (int x = 0; x < n; ++x) {
            bool finder = (x < 7 && y < 7) || (x >= n - 7 && y < 7) || (x < 7 && y >= n - 7);
            bool ring = finder && (x % (n - 1) == 0 || y % (n - 1) == 0 || x == 6 || y == 6 || x == n - 7 || y == n - 7
                                   || ((x >= 2 && x <= 4) || (x >= n - 5 && x <= n - 3)) && ((y >= 2 && y <= 4) || (y >= n - 5 && y <= n - 3)));
            row << (finder ? (ring ? 1 : 0) : ((x * 7 + y * 13 + x * y) % 5 < 2 ? 1 : 0));
        }
        m << row;
    }
    return m;
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const QString out = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral(".");
    QStringList log;

    Model model;
    SharingView view;
    view.setModel(&model);
    view.resize(620, 760);
    view.show();

    auto shot = [&](const QString &name) {
        QApplication::processEvents();
        QApplication::processEvents();
        view.grab().save(QDir(out).filePath(name + QStringLiteral(".png")));
        log << name;
    };

    Service service;
    service.running = true; service.alwaysOn = true; service.online = true;
    service.base = QStringLiteral("https://join.relay-terminal.ai");
    service.onlineBase = service.base;
    service.note = QStringLiteral("Links go through relay-terminal.ai, which carries only ciphertext it cannot read; anyone with a link can reach the door, and you admit each person.");
    service.addressLabel = QStringLiteral("relay-terminal.ai");
    model.setRemote(true, QStringLiteral("relay-terminal.ai"), true, 1, QString());
    model.setDevices(items(R"([
        {"id":"d1","name":"iPhone","platform":"iOS Safari","online":true,"capability":"full","password_entry":false},
        {"id":"d2","name":"iPad","platform":"iPadOS Safari","online":false,"capability":"view","password_entry":true}])"));
    model.setSharedPanes({{QStringLiteral("p1"), QStringLiteral("build"), QString()},
                          {QStringLiteral("p2"), QStringLiteral("logs"), QString()},
                          {QStringLiteral("p3"), QStringLiteral("tests"), QStringLiteral("tab-1")}});
    view.setService(service);
    view.setAddresses(items(R"([
        {"kind":"hosted","value":"relay-terminal.ai","label":"relay-terminal.ai — works from anywhere, no certificate warning","where":"anywhere","current":true},
        {"kind":"lan","value":"192.168.1.20","label":"192.168.1.20 — reachable from this Wi-Fi","where":"this Wi-Fi","current":false}])"));

    auto scope = [](Scope::Kind kind, const QString &id, const QString &title, const QString &tab,
                    const QString &tabTitle, int panes, bool current) {
        Scope s; s.kind = kind; s.id = id; s.title = title; s.tab = tab; s.tabTitle = tabTitle;
        s.panes = panes; s.current = current; return s;
    };
    view.onScopes = [&] {
        return QList<Scope>{
            scope(Scope::Kind::Pane, "p1", "build", "tab-1", "relay-terminal", 0, true),
            scope(Scope::Kind::Pane, "p2", "logs", "tab-1", "relay-terminal", 0, false),
            scope(Scope::Kind::Pane, "p3", "tests", "tab-1", "relay-terminal", 0, false),
            scope(Scope::Kind::Pane, "p4", "paper", "tab-2", "thesis", 0, false),
            scope(Scope::Kind::Tab, "tab-1", "relay-terminal", "tab-1", "relay-terminal", 3, true),
            scope(Scope::Kind::Tab, "tab-2", "thesis", "tab-2", "thesis", 1, false),
            scope(Scope::Kind::All, "all-tabs", "Everything", "all-tabs", QString(), 4, false)};
    };
    view.onPairRequest = [&] { log << "  onPairRequest"; };
    view.onPairCodeRequest = [&] { log << "  onPairCodeRequest"; };
    view.onPairCodeRevoke = [&](const QString &code) { log << "  onPairCodeRevoke " + code; };
    view.onPairAnswer = [&](int id, bool allow, const QString &cap) { log << QStringLiteral("  onPairAnswer %1 %2 %3").arg(id).arg(allow).arg(cap); };
    view.onCreateInvite = [&](const Scope &s, const QString &role, int expires, int uses) {
        log << QStringLiteral("  onCreateInvite kind=%1 id=%2 role=%3 expires=%4 uses=%5").arg(int(s.kind)).arg(s.id, role).arg(expires).arg(uses);
    };
    view.onCreateCode = [&](const Scope &s, const QString &role) { log << QStringLiteral("  onCreateCode id=%1 role=%2").arg(s.id, role); };
    view.onRemoteSwitch = [&](bool on) { log << QStringLiteral("  onRemoteSwitch %1").arg(on); };

    // 1. People, nothing shared with anyone: the empty sentence and Invite…, no pane rows.
    view.focusPane(QStringLiteral("p1"));
    view.refresh();
    shot(QStringLiteral("01-people-empty"));

    // 2. "Share this pane…" on build: the form open on that pane, then the link that came back.
    view.startInvite(scope(Scope::Kind::Pane, "p1", "build", "tab-1", "relay-terminal", 0, true));
    shot(QStringLiteral("02-people-invite-form-on-build"));
    view.showInvite(QStringLiteral("https://join.relay-terminal.ai/join#v=1&d=CqCsWVh5ruysZd-yy9VSzBFJEr"), fakeQr(), QStringLiteral("editor"), 1, 86400);
    shot(QStringLiteral("03-people-link-made"));
    view.showCode(QStringLiteral("BQRT"), QStringLiteral("4829"), 600);
    shot(QStringLiteral("04-people-meeting-code"));

    // 3. A guest on a tab-scoped share, an invite live on build, and a knock waiting.
    model.setParticipants(items(R"([{"id":"a1","name":"alice","platform":"Chrome","role":"editor","panes":["p3"],"expires":86000,"online":true,"fingerprint":"AB12 CD34"}])"),
                          items(R"([{"id":"inv-77aa","role":"viewer","panes":["p1"],"uses":1,"expires":3600}])"));
    model.addKnock(json(R"({"t":"knock","participant":"b2","name":"bob","platform":"Firefox","code":"48213","fingerprint":"EF56 7890","peer":"203.0.113.5","role":"editor","pane":"p1","panes":["p1"],"invite":"inv-77aa"})"),
                   QDateTime::currentMSecsSinceEpoch());
    view.refresh();
    shot(QStringLiteral("05-people-knock-guest-and-invite"));

    // 4. Devices, idle: the switch, the address, the two devices, Add a device….
    view.showPage(SharingView::Page::Devices);
    shot(QStringLiteral("06-devices-idle"));

    // 5. Add a device…: the QR, the typed code, Copy link, the three captions.
    view.startPairing();
    view.showPairing(QStringLiteral("https://join.relay-terminal.ai/pair#v=1&secret=…"), fakeQr(), 299);
    view.showPairCode(QStringLiteral("RPFU"), QStringLiteral("9647"), 600);
    shot(QStringLiteral("07-devices-pairing-offer"));

    // 6. A phone asks: the approval card, Refuse holding the focus.
    DeviceAsk ask; ask.id = 3; ask.name = QStringLiteral("iPhone"); ask.platform = QStringLiteral("iOS Relay");
    ask.fingerprint = QStringLiteral("9C4F 22A1 0B7E"); ask.code = QStringLiteral("48213"); ask.peer = QStringLiteral("203.0.113.9");
    view.showAsk(ask);
    view.focusView();
    QApplication::processEvents();
    log << QStringLiteral("  focus after ask: %1").arg(QApplication::focusWidget() ? QApplication::focusWidget()->property("text").toString() : QStringLiteral("(none)"));
    shot(QStringLiteral("08-devices-ask"));

    // 7. Leaving Devices withdraws the offer.
    view.showPage(SharingView::Page::People);
    shot(QStringLiteral("09-people-again-offer-withdrawn"));

    QFile file(QDir(out).filePath(QStringLiteral("driver.log")));
    file.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream(&file) << log.join(QLatin1Char('\n')) << '\n';
    return 0;
}
