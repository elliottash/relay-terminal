// SPDX-License-Identifier: GPL-3.0-or-later
// relay-vterm-spike: standalone harness for the libvterm engine spike.
//
//   relay-vterm-spike [--cwd DIR] [--dump FILE] [--size COLSxROWS] [-e PROGRAM ARGS...]
//
// Ctrl+Shift+D writes debugDump() (screenText, scrollbackText, altScreen, links)
// to --dump FILE (default: stdout). Link and path clicks are logged to stderr
// and appended to the dump file.
#include "VTermWidget.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QTextStream>

#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QString cwd = QDir::currentPath();
    QString dumpPath;
    QString program = QStringLiteral("/bin/bash");
    QStringList args;
    int cols = 100, rows = 30;

    const QStringList a = app.arguments();
    for (int i = 1; i < a.size(); ++i) {
        if (a[i] == QLatin1String("--cwd") && i + 1 < a.size())
            cwd = a[++i];
        else if (a[i] == QLatin1String("--dump") && i + 1 < a.size())
            dumpPath = a[++i];
        else if (a[i] == QLatin1String("--size") && i + 1 < a.size()) {
            const QStringList p = a[++i].split(QLatin1Char('x'));
            if (p.size() == 2) {
                cols = p[0].toInt();
                rows = p[1].toInt();
            }
        } else if (a[i] == QLatin1String("-e") && i + 1 < a.size()) {
            program = a[++i];
            args = a.mid(i + 1);
            break;
        }
    }

    auto emitLine = [dumpPath](const QString &s) {
        if (dumpPath.isEmpty()) {
            std::fputs(s.toUtf8().constData(), stdout);
            std::fflush(stdout);
            return;
        }
        QFile f(dumpPath);
        if (f.open(QIODevice::Append))
            f.write(s.toUtf8());
    };

    // --bench FILE [SCROLLBACK]: parse FILE through the widget without a PTY or
    // painting, to separate emulator cost from I/O and rendering.
    if (a.size() >= 3 && a[1] == QLatin1String("--bench")) {
        QFile f(a[2]);
        if (!f.open(QIODevice::ReadOnly))
            return 2;
        const QByteArray data = f.readAll();
        relay::VTermWidget bench;
        bench.resizeTerminal(30, 100);
        bench.setScrollbackLimit(a.size() >= 4 ? a[3].toInt() : 10000);
        QElapsedTimer t;
        t.start();
        for (qint64 off = 0; off < data.size(); off += 65536)
            bench.feedForBenchmark(data.constData() + off, std::min<qint64>(65536, data.size() - off));
        std::printf("bench bytes=%lld scrollback=%s ms=%lld MBps=%.1f\n", (long long)data.size(),
                    a.size() >= 4 ? qPrintable(a[3]) : "10000", (long long)t.elapsed(),
                    data.size() / 1048576.0 / (t.elapsed() / 1000.0));
        return 0;
    }

    relay::VTermWidget term;
    term.setWindowTitle(QStringLiteral("relay-vterm-spike"));
    term.resize(term.sizeForGrid(rows, cols));
    term.resizeTerminal(rows, cols);
    term.onPathActivated = [&](const QString &p) {
        std::fprintf(stderr, "pathActivated %s\n", qPrintable(p));
        emitLine(QStringLiteral("CALLBACK pathActivated %1\n").arg(p));
    };
    term.onLinkActivated = [&](const QString &u) {
        std::fprintf(stderr, "linkActivated %s\n", qPrintable(u));
        emitLine(QStringLiteral("CALLBACK linkActivated %1\n").arg(u));
    };
    term.onFinished = [&](int code) { app.exit(code); };
    QObject::connect(&term, &relay::VTermWidget::dumpRequested, [&] {
        emitLine(QStringLiteral("===== DUMP =====\n") + term.debugDump());
    });

    if (!term.startProgram(program, args, cwd)) {
        std::fprintf(stderr, "failed to start %s\n", qPrintable(program));
        return 1;
    }
    term.show();
    return app.exec();
}
