// The shape src/Isolation.h had before #GMCF decision 5: the probe run and waited on inline.
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QStandardPaths>
#include <cstdio>
static bool available() {
    static int state = -1;
    if (state < 0) {
        state = 0;
        const QString tool = QStandardPaths::findExecutable(QStringLiteral("systemd-run"));
        if (!tool.isEmpty()) {
            QProcess probe;
            probe.start(tool, {QStringLiteral("--user"), QStringLiteral("--scope"), QStringLiteral("--quiet"), QStringLiteral("--"), QStringLiteral("true")});
            if (probe.waitForFinished(3000) && probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0) state = 1;
            else probe.kill();
        }
    }
    return state == 1;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    qputenv("PATH", QByteArray(argv[1]));
    QElapsedTimer t; t.start();
    const bool a = available();
    printf("old available=%d waited_ms=%lld\n", a ? 1 : 0, (long long)t.elapsed());
    return 0;
}
