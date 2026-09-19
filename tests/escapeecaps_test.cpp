// SPDX-License-Identifier: GPL-3.0-or-later
//
// The opt-in cap on the programs that leave their pane (card #Y4RX): the drop-in paths systemd
// searches for a truncated unit name, the text written, and that nothing without Relay's marker is
// ever written over or removed. Everything runs against a temporary config root; the real
// ~/.config/systemd/user is never touched by this test.

#include "EscapeeCaps.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class EscapeeCapsTest : public QObject {
    Q_OBJECT
private slots:
    void paths_are_the_prefix_dropins_systemd_searches();
    void install_writes_both_and_says_what_it_wrote();
    void text_carries_the_caps_and_the_machine_wide_warning();
    void remove_takes_back_exactly_what_relay_wrote();
    void a_file_without_the_marker_is_never_touched();
    void install_is_idempotent();
};

void EscapeeCapsTest::paths_are_the_prefix_dropins_systemd_searches() {
    // man systemd.unit: for a dashed unit name the truncated `foo-.scope.d/` directory is searched
    // too, so one file covers every tmux-spawn-<uuid> and every Chrome app scope.
    QCOMPARE(escapees::unitNames(), QStringList({QStringLiteral("tmux-spawn-.scope"),
                                                 QStringLiteral("app-com.google.Chrome-.scope")}));
    QCOMPARE(escapees::dropInPath(QStringLiteral("/c"), QStringLiteral("tmux-spawn-.scope")),
             QStringLiteral("/c/systemd/user/tmux-spawn-.scope.d/relay.conf"));
    QCOMPARE(escapees::dropInPath(QStringLiteral("/c"), QStringLiteral("app-com.google.Chrome-.scope")),
             QStringLiteral("/c/systemd/user/app-com.google.Chrome-.scope.d/relay.conf"));
}

void EscapeeCapsTest::install_writes_both_and_says_what_it_wrote() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(!escapees::installed(root.path()));
    QStringList skipped;
    const QStringList written = escapees::install(root.path(), {QStringLiteral("8G"), QStringLiteral("2G")}, &skipped);
    QCOMPARE(written.size(), 2);
    QVERIFY(skipped.isEmpty());
    QVERIFY(escapees::installed(root.path()));
    for (const QString &path : written) QVERIFY(QFile::exists(path));
}

void EscapeeCapsTest::text_carries_the_caps_and_the_machine_wide_warning() {
    const QString text = escapees::dropInText({QStringLiteral("8G"), QStringLiteral("512M")});
    QVERIFY(text.startsWith(escapees::marker()));
    QVERIFY(text.contains(QStringLiteral("[Scope]")));
    QVERIFY(text.contains(QStringLiteral("MemoryMax=8G")));
    QVERIFY(text.contains(QStringLiteral("MemorySwapMax=512M")));
    QVERIFY(text.contains(QStringLiteral("machine-wide")));
    QVERIFY(escapees::ours(text));
    QVERIFY(!escapees::ours(QStringLiteral("[Scope]\nMemoryMax=1G\n")));
}

void EscapeeCapsTest::remove_takes_back_exactly_what_relay_wrote() {
    QTemporaryDir root;
    escapees::install(root.path(), {QStringLiteral("8G"), QStringLiteral("2G")});
    QStringList skipped;
    const QStringList removed = escapees::removeAll(root.path(), &skipped);
    QCOMPARE(removed.size(), 2);
    QVERIFY(skipped.isEmpty());
    QVERIFY(!escapees::installed(root.path()));
    for (const QString &unit : escapees::unitNames()) {
        QVERIFY(!QFile::exists(escapees::dropInPath(root.path(), unit)));
        // The empty directory goes too, so turning the option off leaves nothing behind.
        QVERIFY(!QDir(escapees::dropInDir(root.path(), unit)).exists());
    }
    // Removing again is not an error and reports nothing.
    QVERIFY(escapees::removeAll(root.path()).isEmpty());
}

void EscapeeCapsTest::a_file_without_the_marker_is_never_touched() {
    QTemporaryDir root;
    const QString unit = escapees::unitNames().first();
    const QString path = escapees::dropInPath(root.path(), unit);
    QVERIFY(QDir().mkpath(escapees::dropInDir(root.path(), unit)));
    const QString mine = QStringLiteral("[Scope]\nMemoryMax=3G\n");
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(mine.toUtf8());
    }
    QStringList skipped;
    const QStringList written = escapees::install(root.path(), {QStringLiteral("8G"), QStringLiteral("2G")}, &skipped);
    QCOMPARE(skipped, QStringList({path}));
    QCOMPARE(written, QStringList({escapees::dropInPath(root.path(), escapees::unitNames().at(1))}));
    QFile kept(path);
    QVERIFY(kept.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(QString::fromUtf8(kept.readAll()), mine);
    kept.close();
    // Nor is it removed when the option goes off again.
    skipped.clear();
    const QStringList removed = escapees::removeAll(root.path(), &skipped);
    QCOMPARE(skipped, QStringList({path}));
    QCOMPARE(removed.size(), 1);
    QVERIFY(QFile::exists(path));
    QVERIFY(!escapees::installed(root.path()));
}

void EscapeeCapsTest::install_is_idempotent() {
    QTemporaryDir root;
    escapees::install(root.path(), {QStringLiteral("8G"), QStringLiteral("2G")});
    const QStringList again = escapees::install(root.path(), {QStringLiteral("16G"), QStringLiteral("4G")});
    QCOMPARE(again.size(), 2);
    QFile file(escapees::dropInPath(root.path(), escapees::unitNames().first()));
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString text = QString::fromUtf8(file.readAll());
    QVERIFY(text.contains(QStringLiteral("MemoryMax=16G")));      // a new limit replaces the old file
    QVERIFY(!text.contains(QStringLiteral("MemoryMax=8G")));
}

QTEST_GUILESS_MAIN(EscapeeCapsTest)
#include "escapeecaps_test.moc"
