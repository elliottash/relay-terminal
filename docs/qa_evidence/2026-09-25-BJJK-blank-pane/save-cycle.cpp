#include "backend/VTermBackend.h"
#include "view/TerminalView.h"
#include <QApplication>
#include <QFile>
#include <QRegularExpression>
#include <QtTest>
using namespace relay;
// Print a clean single-copy text at 29 cols, save it the way Pane does (history + screen), replay
// the save into a fresh backend at other widths, and count how often each prose anchor's first row appears.
static int copies(const QStringList &rows, const QString &needle) { int n=0; for (auto &r: rows) if (r.contains(needle)) ++n; return n; }
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QFile f(argv[1]); f.open(QIODevice::ReadOnly);
    QStringList rows = QString::fromUtf8(f.readAll()).split('\n');
    const int h = argc > 2 ? atoi(argv[2]) : 40;
    QList<int> widths = {29, 80, 29, 60};
    for (int cycle = 0; cycle < widths.size(); ++cycle) {
        VTermBackend b{QString::fromLocal8Bit(qgetenv("CORE"))};
        b.resizeTerminal(h, widths[cycle]);
        b.widget()->show(); QTest::qWait(100);
        b.startProgram("/bin/cat", {}, "/tmp");
        QByteArray out = "\r\x1b[2K"; for (auto &r : rows) out += r.toUtf8() + "\r\n"; out += "PROMPT$ ";
        b.writeToDisplay(out); QTest::qWait(300);
        b.resizeTerminal(h, widths[(cycle+1)%widths.size()]); QTest::qWait(300);
        if (qEnvironmentVariableIsSet("SCROLLUP")) { b.view()->scrollToTop(); QTest::qWait(100); }
        QStringList hist = b.formattedScrollbackText(5000);
        QStringList screen = b.formattedScreenText().split('\n');
        rows = hist + screen;
        while (!rows.isEmpty() && rows.last().trimmed().isEmpty()) rows.removeLast();
        printf("cycle %d: hist=%d screen=%d  'I have the ground truth' x%d  'Previous conversation' x%d  PROMPT x%d\n", cycle, int(hist.size()), int(screen.size()),
            copies(rows, "I have the ground truth"), copies(rows, "Previous conversation"), copies(rows, "PROMPT$"));
    }
}
