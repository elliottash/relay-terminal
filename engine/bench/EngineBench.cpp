// SPDX-License-Identifier: AGPL-3.0-or-later
// relay-engine-bench: headless emulator-core throughput through relay::VtCore.
//
//   relay-engine-bench [--core NAME] [--size COLSxROWS] [--scrollback N] [--frames] [--repeat N] FILE
//
// Reads FILE into memory, then times feeding it in 64 KiB chunks. --frames also
// builds a ViewportFrame after every chunk (what the GUI does per repaint at
// most, so it is an upper bound on render-state cost). Prints MiB/s and the
// peak RSS of the process (the input buffer is included; after_load_kb is the
// peak right after loading).
#include "core/VtCore.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QStringList>

#include <cstdio>
#if defined(Q_OS_UNIX)
#include <sys/resource.h>
#endif

static long maxRssKb()
{
#if defined(Q_OS_UNIX)
    struct rusage ru {};
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss;
#else
    return -1; // TODO(windows): GetProcessMemoryInfo
#endif
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QString core;
    int cols = 100, rows = 30, scrollback = 10000, repeat = 1;
    bool frames = false;
    QString file;
    const QStringList a = app.arguments();
    for (int i = 1; i < a.size(); ++i) {
        if (a[i] == QLatin1String("--core") && i + 1 < a.size())
            core = a[++i];
        else if (a[i] == QLatin1String("--size") && i + 1 < a.size()) {
            const QStringList p = a[++i].split(QLatin1Char('x'));
            cols = p.value(0).toInt();
            rows = p.value(1).toInt();
        } else if (a[i] == QLatin1String("--scrollback") && i + 1 < a.size())
            scrollback = a[++i].toInt();
        else if (a[i] == QLatin1String("--repeat") && i + 1 < a.size())
            repeat = a[++i].toInt();
        else if (a[i] == QLatin1String("--frames"))
            frames = true;
        else if (a[i] == QLatin1String("--list")) {
            std::printf("%s\n", qPrintable(relay::availableVtCores().join(QLatin1Char(' '))));
            return 0;
        } else
            file = a[i];
    }
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "usage: relay-engine-bench [--core %s] [--size COLSxROWS] [--scrollback N] [--frames] FILE\n",
                     qPrintable(relay::availableVtCores().join(QLatin1Char('|'))));
        return 2;
    }
    const QByteArray data = f.readAll();
    const long afterLoad = maxRssKb();

    for (int run = 0; run < repeat; ++run) {
        auto vt = relay::createVtCore(core, rows, cols);
        if (!vt) {
            std::fprintf(stderr, "unknown core %s\n", qPrintable(core));
            return 2;
        }
        vt->setScrollbackLines(scrollback);
        quint64 replyBytes = 0;
        vt->events.reply = [&](const char *, size_t n) { replyBytes += n; };
        relay::ViewportFrame frame;
        QElapsedTimer t;
        t.start();
        for (qint64 off = 0; off < data.size(); off += 65536) {
            const qint64 n = std::min<qint64>(65536, data.size() - off);
            vt->feed(data.constData() + off, size_t(n));
            if (frames)
                vt->updateFrame(&frame, false);
        }
        vt->updateFrame(&frame, true);
        const qint64 ns = t.nsecsElapsed();
        const double ms = ns / 1e6;
        std::printf("core=%s bytes=%lld ms=%.0f MiBps=%.1f maxrss_kb=%ld after_load_kb=%ld history=%d frames=%d last_row=\"%s\"\n",
                    vt->name(), (long long)data.size(), ms, data.size() / 1048576.0 / (ms / 1000.0), maxRssKb(), afterLoad,
                    vt->historyRows(), frames ? 1 : 0,
                    qPrintable(frame.lines.empty() ? QString() : frame.lines.back().text().left(24)));
        std::fflush(stdout);
    }
    return 0;
}
