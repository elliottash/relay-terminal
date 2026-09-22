#include <QtWidgets>
#include <QtNetwork>
#include <functional>
#define private public
#include "RemoteShare.h"
#undef private
#include "SharingPane.h"
#include <iostream>
#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " << #x << '\n'; return 1; } } while (0)
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("RemoteDeliveryTest");
    QCoreApplication::setApplicationName("RemoteDeliveryTest");
    relay::remotesettings::setAlwaysOn(true);
    auto &share = relay::RemoteShare::instance();
    QProcess sink;
    sink.start("cat", QStringList{});
    CHECK(sink.waitForStarted());
    share.m_process = &sink;
    relay::RemoteShareDialog dialog("p1");
    auto drain = [&] {
        sink.waitForBytesWritten(1000);
        sink.waitForReadyRead(1000);
        return sink.readAllStandardOutput();
    };
    auto started = [&](QString base) { share.handle({{"t","started"},{"base",base}}); };
    auto online = [&](QString base) { share.handle({{"t","remote_state"},{"on",true},
        {"online",true},{"base",base},{"address","relay-terminal.ai"}}); };
    started("http://localhost");
    CHECK(!dialog.m_pairWaiting);
    started("https://test.invalid");
    online("https://test.invalid");
    QByteArray sent = drain();
    CHECK(sent.count("\"t\":\"pair\"") == 1);
    CHECK(sent.count("\"t\":\"pair_code\"") == 1);
    dialog.showPairCode("ABCD", "1234", 600);
    dialog.showPairing("https://test.invalid/#secret", {{1,0},{0,1}}, 600);
    for (int i=0;i<4;++i) { started("https://test.invalid"); online("https://test.invalid"); }
    CHECK(drain().isEmpty());
    CHECK(dialog.m_pairCode == "ABCD");
    share.handle({{"t","stopped"}});
    CHECK(dialog.m_pairCode.isEmpty());
    CHECK(dialog.m_pairingLink.isEmpty());
    CHECK(!dialog.m_pairCopy->isEnabled());
    started("http://localhost");
    CHECK(!dialog.m_pairWaiting);
    started("https://test.invalid"); online("https://test.invalid");
    sent = drain();
    CHECK(sent.count("\"t\":\"pair\"") == 1);
    CHECK(sent.count("\"t\":\"pair_code\"") == 1);
    share.handle({{"t","error"},{"message","429 too many pairing rooms this hour"}});
    CHECK(!dialog.m_pairWaiting);
    CHECK(dialog.m_pairNote->text().contains("429"));
    CHECK(!dialog.m_pairAgain->isHidden());
    dialog.noPairCode();
    CHECK(dialog.m_pairNote->text().contains("429"));
    dialog.show(); app.processEvents();
    CHECK(dialog.grab().save(QString::fromLocal8Bit(argv[1]) + "/pairing.png"));
    dialog.hide();
    relay::sharing::Model model;
    model.setRemote(true,"relay-terminal.ai",true,0);
    model.setSharedPanes({{"p1","build"},{"p2","logs"}});
    relay::sharing::SharingView view;
    view.setModel(&model); view.refresh(); view.resize(900,650); view.show(); app.processEvents();
    CHECK(view.grab().save(QString::fromLocal8Bit(argv[1]) + "/sharing.png"));
    share.m_process = nullptr;
    sink.kill(); sink.waitForFinished();
    std::cout << "PASS: first settled start requests two rooms; repeated announcements request none; off/on clears and remints; 429 remains visible with retry. Sharing view rendered.\n";
}
