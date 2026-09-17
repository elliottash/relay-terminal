// SPDX-License-Identifier: GPL-3.0-or-later
// relay-vterm-spike: manual/xdotool harness for the Relay terminal engine.
//
//   relay-vterm-spike [--core ghostty|libvterm] [--cwd DIR] [--dump FILE] [--size COLSxROWS]
//                     [--font "Family,points"] [-e PROGRAM ARGS...]
//
// Ctrl+Shift+D appends debugDump() (core, sizes, alt screen, cursor, scrollback,
// screen text) to --dump FILE (default stdout). Backend callbacks (links, title,
// cwd, alt screen, prompt marks, bell, exit) are logged to stderr and the dump file.
// The name is kept so the xdotool scripts in docs/qa_evidence keep working.
#include "backend/VTermBackend.h"
#include "session/TerminalSession.h"
#include "view/TerminalView.h"

#include <QApplication>
#include <QDir>
#include <QFile>

#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QString core;
    QString cwd = QDir::currentPath();
    QString dumpPath;
    QString program;
    QStringList args;
    QString fontSpec;
    int cols = 100, rows = 30;

    const QStringList a = app.arguments();
    for (int i = 1; i < a.size(); ++i) {
        if (a[i] == QLatin1String("--core") && i + 1 < a.size())
            core = a[++i];
        else if (a[i] == QLatin1String("--cwd") && i + 1 < a.size())
            cwd = a[++i];
        else if (a[i] == QLatin1String("--dump") && i + 1 < a.size())
            dumpPath = a[++i];
        else if (a[i] == QLatin1String("--font") && i + 1 < a.size())
            fontSpec = a[++i];
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
        std::fputs(qPrintable(s), stderr);
        if (dumpPath.isEmpty())
            return;
        QFile f(dumpPath);
        if (f.open(QIODevice::Append))
            f.write(s.toUtf8());
    };

    relay::VTermBackend backend(core);
    QWidget *w = backend.widget();
    if (!fontSpec.isEmpty()) {
        QFont f(fontSpec.section(QLatin1Char(','), 0, 0));
        f.setPointSize(fontSpec.section(QLatin1Char(','), 1, 1).toInt() > 0 ? fontSpec.section(QLatin1Char(','), 1, 1).toInt() : 11);
        backend.setTerminalFont(f);
    }
    w->setWindowTitle(QStringLiteral("relay-vterm-spike (%1)").arg(backend.session()->coreName()));
    backend.resizeTerminal(rows, cols);

    backend.onLinkActivated = [&](const QString &t, int line, int col) {
        emitLine(QStringLiteral("CALLBACK linkActivated %1 line=%2 col=%3\n").arg(t).arg(line).arg(col));
    };
    backend.onTitleChanged = [&](const QString &t) {
        w->setWindowTitle(t);
        emitLine(QStringLiteral("CALLBACK titleChanged %1\n").arg(t));
    };
    backend.onCwdChanged = [&](const QString &p) { emitLine(QStringLiteral("CALLBACK cwdChanged %1\n").arg(p)); };
    backend.onAltScreenChanged = [&](bool on) { emitLine(QStringLiteral("CALLBACK altScreenChanged %1\n").arg(on)); };
    backend.onPromptMark = [&](char k, int code) {
        emitLine(QStringLiteral("CALLBACK promptMark %1 exit=%2\n").arg(QLatin1Char(k)).arg(code));
    };
    backend.onBell = [&] { emitLine(QStringLiteral("CALLBACK bell\n")); };
    backend.onFinished = [&](int code) {
        emitLine(QStringLiteral("CALLBACK finished %1\n").arg(code));
        app.exit(code);
    };
    QObject::connect(backend.view(), &relay::TerminalView::dumpRequested, [&] {
        emitLine(QStringLiteral("===== DUMP =====\n") + backend.view()->debugDump());
    });

    if (!backend.startProgram(program, args, cwd)) {
        std::fprintf(stderr, "failed to start %s: %s\n", qPrintable(program), qPrintable(backend.session()->errorString()));
        return 1;
    }
    w->show();
    backend.focusWidget()->setFocus();
    return app.exec();
}
