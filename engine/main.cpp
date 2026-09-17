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
