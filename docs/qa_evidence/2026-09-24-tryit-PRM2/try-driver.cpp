#include <QtWidgets>
#include <QtNetwork>
#include <functional>
#define private public
#include "RemoteShare.h"
#undef private
#include <iostream>
#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " << #x << '\n'; return 1; } } while (0)
// Renders the real RemoteShareDialog twice against a fake sidecar (a `cat` sink):
// once with live pairing credentials, once after the rendezvous refused with 429.
// Writes 01-pairing-ok.png and 02-after-429.png into the directory given as argv[1].
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("RemoteDeliveryTryIt");
    QCoreApplication::setApplicationName("RemoteDeliveryTryIt");
    relay::remotesettings::setAlwaysOn(true);
    auto &share = relay::RemoteShare::instance();
    QProcess sink;
    sink.start("cat", QStringList{});
    CHECK(sink.waitForStarted());
    share.m_process = &sink;
    relay::RemoteShareDialog dialog("p1");
    auto started = [&](QString base) { share.handle({{"t","started"},{"base",base}}); };
    auto online = [&](QString base) { share.handle({{"t","remote_state"},{"on",true},
        {"online",true},{"base",base},{"address","relay-terminal.ai"}}); };
    started("http://localhost");
    started("https://test.invalid");
    online("https://test.invalid");
    dialog.showPairCode("ABCD", "1234", 600);
    dialog.showPairing("https://test.invalid/#secret", {{1,0},{0,1}}, 600);
    QString out = QString::fromLocal8Bit(argv[1]);
    dialog.show(); app.processEvents();
    CHECK(dialog.m_pairCode == "ABCD");
    CHECK(dialog.grab().save(out + "/01-pairing-ok.png"));
    dialog.askPairCode();   // revokes the live code and waits for another
    app.processEvents();
    CHECK(dialog.m_pairWaiting);
    share.handle({{"t","error"},{"message","429 too many pairing rooms this hour"}});
    dialog.noPairCode();
    app.processEvents();
    CHECK(!dialog.m_pairWaiting);
    CHECK(dialog.m_pairNote->text().contains("429"));
    CHECK(!dialog.m_pairAgain->isHidden());
    CHECK(dialog.grab().save(out + "/02-after-429.png"));
    share.m_process = nullptr;
    sink.kill(); sink.waitForFinished();
    std::cout << "captured 01-pairing-ok.png and 02-after-429.png\n";
}
