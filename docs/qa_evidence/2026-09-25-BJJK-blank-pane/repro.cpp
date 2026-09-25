#include "backend/VTermBackend.h"
#include "view/TerminalView.h"
#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>
using namespace relay;
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QFile f(argv[1]); f.open(QIODevice::ReadOnly);
    const QStringList all = QString::fromUtf8(f.readAll()).split('\n');
    const QString sep = QStringLiteral("\x1b_relay-prose\x1b\\");
    QStringList rows; QVector<QJsonObject> recs; bool trailer = false;
    for (const QString &l : all) { if (l == sep) { trailer = true; continue; }
        if (trailer) { if (!l.isEmpty()) recs << QJsonDocument::fromJson(l.toUtf8()).object(); } else rows << l; }
    const int startCols = argc > 2 ? atoi(argv[2]) : 79;
    const bool useProse = argc > 3 ? atoi(argv[3]) : 1;
    VTermBackend backend{QString::fromLocal8Bit(qgetenv("CORE"))};
    backend.resizeTerminal(60, startCols);
    backend.widget()->show();
    QTest::qWait(300);
    backend.startProgram("/bin/cat", {}, "/tmp");
    QByteArray out = "\r\x1b[2K";
    for (const QString &r : rows) out += r.toUtf8() + "\r\n";
    out += "PROMPT$ ";
    backend.writeToDisplay(out);
    QTest::qWait(200);
    if (useProse) for (const QJsonObject &o : recs) {
        QVector<FoldLine> lines;
        for (const auto &lv : o["lines"].toArray()) { FoldLine fl; fl.role = quint8(lv.toObject()["role"].toInt());
            for (const auto &sv : lv.toObject()["spans"].toArray()) { FoldSpan s; auto so = sv.toObject();
                s.text = so["text"].toString(); s.sgr = so["sgr"].toString(); s.bold = so["bold"].toBool(); s.dim = so["dim"].toBool(); fl.spans << s; }
            lines << fl; }
        backend.setProseBlock(o["uri"].toString(), lines, o["columns"].toInt());
    }
    for (int cols : {startCols, 29, 60, 76, 79, 100}) {
        backend.resizeTerminal(60, cols);
        QTest::qWait(250);
        QImage img = backend.view()->grab().toImage();
        const QColor bg = img.pixelColor(img.width()/2, img.height()-3);
        int n = 0; for (int y = 0; y < img.height(); ++y) for (int x = 0; x < img.width(); ++x) if (img.pixelColor(x,y) != bg) ++n;
        QStringList vis = backend.view()->visibleRowsText(); int nonempty = 0; for (auto &v : vis) if (!v.trimmed().isEmpty()) ++nonempty;
        printf("cols=%d inkpixels=%d visibleNonEmptyRows=%d/%d last=%s\n", cols, n, nonempty, int(vis.size()), qPrintable(vis.isEmpty()?QString():vis.last().left(40)));
        img.save(QString("/tmp/blankrepro/c%1-p%2.png").arg(cols).arg(useProse));
    }
    return 0;
}
