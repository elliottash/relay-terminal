// SPDX-License-Identifier: GPL-3.0-or-later
// relay-vterm-spike: manual/xdotool harness for the Relay terminal engine.
//
//   relay-vterm-spike [--core ghostty|libvterm] [--cwd DIR] [--dump FILE] [--size COLSxROWS]
//                     [--font "Family,points"] [--folds] [-e PROGRAM ARGS...]
//
// --folds prints the concise tool-call lines the agent will print (#TK9C) and
// registers their detail: a short run, a red and green diff, and a 300-line
// listing. Click a line (or Ctrl+Shift+F) to unfold it in place.
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
#include <QTimer>

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
    bool folds = false;
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
        } else if (a[i] == QLatin1String("--folds")) {
            folds = true;
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

    // --folds: what an agent turn will look like once every tool call prints
    // one concise line (#TK9C). The lines go in as OSC 8 hyperlinks; the detail
    // is registered here, so clicking a line unfolds it without a worker.
    if (folds) {
        using relay::FoldLine;
        using relay::FoldSpan;
        auto span = [](const QString &text, const QColor &fg = QColor(), bool bold = false, bool dim = false) {
            FoldSpan s;
            s.text = text;
            s.fg = fg;
            s.bold = bold;
            s.dim = dim;
            return s;
        };
        auto oneLine = [&](const FoldSpan &s) {
            FoldLine l;
            l.spans << s;
            return l;
        };
        backend.setFoldPrefix(QStringLiteral("relay://call/"));
        QTimer::singleShot(400, [&] {
            auto anchor = [&](const QString &id, const QString &text) {
                backend.writeToDisplay(QByteArray("\x1b]8;;relay://call/demo/1/") + id.toUtf8() + "\x1b\\  "
                                       + text.toUtf8() + "\x1b]8;;\x1b\\\r\n");
            };
            backend.writeToDisplay("\x1b[1mrelay\x1b[0m  agent turn 1\r\n");
            anchor(QStringLiteral("run"), QStringLiteral("ran python script \u00B7 14 lines \u00B7 exit 0 \u00B7 1.2 s"));
            anchor(QStringLiteral("edit"), QStringLiteral("edited engine/view/FoldLayer.cpp \u00B7 +6 \u22122"));
            anchor(QStringLiteral("read"), QStringLiteral("read 300 lines of build.log"));
            backend.writeToDisplay("done.\r\n");

            QVector<FoldLine> run;
            run << oneLine(span(QStringLiteral("$ python3 -c 'print(sum(range(14)))'"), QColor(), true));
            for (int i = 0; i < 12; ++i)
                run << oneLine(span(QStringLiteral("row %1 of the script's output").arg(i)));
            run << oneLine(span(QStringLiteral("91")));
            backend.setFoldContent(QStringLiteral("relay://call/demo/1/run"), run);

            QVector<FoldLine> diff;
            diff << oneLine(span(QStringLiteral("engine/view/FoldLayer.cpp"), QColor(), false, true));
            diff << oneLine(span(QStringLiteral("@@ -120,6 +120,10 @@"), QColor(0x7f, 0xc1, 0xff)));
            diff << oneLine(span(QStringLiteral("-    const int usable = m_columns;"), QColor(0xe0, 0x6c, 0x75)));
            diff << oneLine(span(QStringLiteral("+    const int usable = m_columns - m_indent;"), QColor(0x98, 0xc3, 0x79)));
            diff << oneLine(span(QStringLiteral("+    // a hanging indent keeps a wrapped detail line readable"),
                                 QColor(0x98, 0xc3, 0x79)));
            FoldLine linked;
            linked.spans << span(QStringLiteral("open "), QColor(), false, true);
            FoldSpan path = span(QStringLiteral("engine/view/FoldLayer.cpp"));
            path.link = QDir::currentPath() + QStringLiteral("/engine/view/FoldLayer.cpp");
            linked.spans << path;
            diff << linked;
            backend.setFoldContent(QStringLiteral("relay://call/demo/1/edit"), diff);

            QVector<FoldLine> log;
            for (int i = 0; i < 300; ++i)
                log << oneLine(span(QStringLiteral("build.log:%1  compiling object %2 of 300").arg(i + 1).arg(i + 1),
                                    QColor(), false, i % 5 == 0));
            backend.setFoldContent(QStringLiteral("relay://call/demo/1/read"), log);
            // They start shut, so the first screenshot is the folded one.
            for (const QString &id : {QStringLiteral("run"), QStringLiteral("edit"), QStringLiteral("read")})
                backend.setFoldExpanded(QStringLiteral("relay://call/demo/1/") + id, false);
        });
    }
    return app.exec();
}
