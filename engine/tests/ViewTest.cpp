// SPDX-License-Identifier: GPL-3.0-or-later
// TerminalView / VTermBackend tests on the offscreen platform: rendering,
// keyboard, mouse selection, links, IME, scrolling, accessibility, key mapping.
#include "backend/VTermBackend.h"
#include "session/TerminalSession.h"
#include "view/KeyMapper.h"
#include "view/TerminalView.h"

#include <QAccessible>
#include <QFontDatabase>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

using namespace relay;

namespace {

struct Term {
    std::unique_ptr<VTermBackend> backend;
    TerminalView *view = nullptr;

    Term(const QString &core, const QString &program, const QStringList &args = {}, const QString &cwd = QString())
    {
        backend = std::make_unique<VTermBackend>(core);
        view = backend->view();
        backend->resizeTerminal(12, 50);
        backend->widget()->show();
        QVERIFY(QTest::qWaitForWindowExposed(backend->widget()));
        QVERIFY2(backend->startProgram(program, args, cwd.isEmpty() ? QDir::tempPath() : cwd),
                 qPrintable(backend->session()->errorString()));
        view->setFocus();
    }
    bool waitScreen(const QString &needle, int ms = 4000)
    {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            if (backend->screenText().contains(needle))
                return true;
            QTest::qWait(10);
        }
        return false;
    }
    QImage grab()
    {
        QTest::qWait(60); // let the frame timer paint
        return view->grab().toImage();
    }
};

int countNonBackground(const QImage &img, const QRect &r, const QColor &bg)
{
    int n = 0;
    for (int y = r.top(); y <= r.bottom() && y < img.height(); ++y)
        for (int x = r.left(); x <= r.right() && x < img.width(); ++x)
            if (img.pixelColor(x, y) != bg)
                ++n;
    return n;
}

} // namespace

class ViewTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase_data()
    {
        QTest::addColumn<QString>("core");
        for (const QString &c : availableVtCores())
            QTest::newRow(qPrintable(c)) << c;
    }

    void rendersTextAndColours()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        t.backend->writeToDisplay("Hello\r\n\x1b[41m    \x1b[0m\r\n");
        QVERIFY(t.waitScreen(QStringLiteral("Hello")));
        const QImage img = t.grab();
        const int cw = t.view->cellWidth(), ch = t.view->cellHeight();
        const QColor bg = t.view->colorScheme().background;
        QVERIFY(countNonBackground(img, QRect(2, 2, 5 * cw, ch), bg) > 20);           // glyphs
        QCOMPARE(img.pixelColor(2 + 2 * cw, 2 + ch + ch / 2).red() > 150, true);      // red background run
        QCOMPARE(countNonBackground(img, QRect(2 + 20 * cw, 2 + 5 * ch, 10 * cw, ch), bg), 0);
    }

    void boxDrawingJoinsAcrossCells()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        t.backend->writeToDisplay(QStringLiteral("──────").toUtf8());
        QVERIFY(t.waitScreen(QStringLiteral("──")));
        const QImage img = t.grab();
        const int cw = t.view->cellWidth(), ch = t.view->cellHeight();
        const QColor bg = t.view->colorScheme().background;
        // Some pixel row inside the first line is painted across all six cells without gaps.
        bool continuous = false;
        for (int y = 2; y < 2 + ch && !continuous; ++y) {
            bool all = true;
            for (int x = 2; x < 2 + 6 * cw; ++x)
                all &= img.pixelColor(x, y) != bg;
            continuous = all;
        }
        QVERIFY(continuous);
    }

    void colorEmoji()
    {
        QFETCH_GLOBAL(QString, core);
        if (!QFontDatabase().families().contains(QStringLiteral("Noto Color Emoji")))
            QSKIP("Noto Color Emoji not installed");
        Term t(core, QStringLiteral("/bin/cat"));
        t.backend->writeToDisplay(QStringLiteral("\U0001F389\U0001F44D\U0001F3FD").toUtf8());
        QVERIFY(t.waitScreen(QStringLiteral("\U0001F389")));
        const QImage img = t.grab();
        const int cw = t.view->cellWidth(), ch = t.view->cellHeight();
        int colored = 0;
        for (int y = 2; y < 2 + ch; ++y)
            for (int x = 2; x < 2 + 4 * cw; ++x) {
                const QColor c = img.pixelColor(x, y);
                const int mx = std::max({c.red(), c.green(), c.blue()}), mn = std::min({c.red(), c.green(), c.blue()});
                colored += (mx - mn) > 60;
            }
        QVERIFY2(colored > 30, qPrintable(QString::number(colored)));
    }

    void typingReachesProgram()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        QTest::keyClicks(t.view, QStringLiteral("abc"));
        QTest::keyClick(t.view, Qt::Key_Return);
        QVERIFY(t.waitScreen(QStringLiteral("abc\nabc")));
    }

    void ctrlCInterruptsForegroundJob()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/sh"), {QStringLiteral("-c"), QStringLiteral("sleep 30")});
        QSignalSpy finished(t.backend->session(), &TerminalSession::finished);
        QTest::qWait(150);
        QElapsedTimer timer;
        timer.start();
        QTest::keyClick(t.view, Qt::Key_C, Qt::ControlModifier);
        QVERIFY(finished.wait(2000));
        QVERIFY2(timer.elapsed() < 500, qPrintable(QString::number(timer.elapsed())));
    }

    void mouseSelectionAndCopy()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        t.backend->writeToDisplay("first second third\r\n");
        QVERIFY(t.waitScreen(QStringLiteral("third")));
        const int cw = t.view->cellWidth(), ch = t.view->cellHeight();
        const QPoint a(2 + 6 * cw + cw / 2, 2 + ch / 2), b(2 + 11 * cw + cw / 2, 2 + ch / 2);
        QTest::mousePress(t.view, Qt::LeftButton, Qt::NoModifier, a);
        QMouseEvent move(QEvent::MouseMove, b, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(t.view, &move);
        QTest::mouseRelease(t.view, Qt::LeftButton, Qt::NoModifier, b);
        QCOMPARE(t.backend->selectedText(), QStringLiteral("second"));
        // Double click selects a word.
        QTest::mouseDClick(t.view, Qt::LeftButton, Qt::NoModifier, QPoint(2 + 14 * cw, 2 + ch / 2));
        QCOMPARE(t.backend->selectedText(), QStringLiteral("third"));
        t.backend->selectAll();
        QVERIFY(t.backend->selectedText().contains(QStringLiteral("first second third")));
    }

    void ctrlClickLinksAndPaths()
    {
        QFETCH_GLOBAL(QString, core);
        QTemporaryDir dir;
        QFile f(dir.filePath(QStringLiteral("notes.txt")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        Term t(core, QStringLiteral("/bin/cat"), {}, dir.path());
        QSignalSpy links(t.view, &TerminalView::linkActivated);
        const QByteArray osc7 = "\x1b]7;file://" + QUrl::toPercentEncoding(dir.path(), "/") + "\x07";
        t.backend->writeToDisplay(osc7 + "see \x1b]8;;https://example.com/doc\x1b\\docs\x1b]8;;\x1b\\ and notes.txt:12:3 https://relay.test/x.\r\n");
        QVERIFY(t.waitScreen(QStringLiteral("relay.test")));
        QTest::qWait(60);
        const int cw = t.view->cellWidth(), ch = t.view->cellHeight();
        auto click = [&](int col) { QTest::mouseClick(t.view, Qt::LeftButton, Qt::ControlModifier, QPoint(2 + col * cw + cw / 2, 2 + ch / 2)); };
        click(5);  // "docs" (OSC 8)
        click(15); // notes.txt:12:3
        click(35); // plain URL, trailing '.' stripped
        QCOMPARE(links.size(), 3);
        QCOMPARE(links[0][0].toString(), QStringLiteral("https://example.com/doc"));
        QCOMPARE(links[1][0].toString(), QFileInfo(f).absoluteFilePath());
        QCOMPARE(links[1][1].toInt(), 12);
        QCOMPARE(links[1][2].toInt(), 3);
        QCOMPARE(links[2][0].toString(), QStringLiteral("https://relay.test/x"));
    }

    void wrappedUrlClick()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        QSignalSpy links(t.view, &TerminalView::linkActivated);
        // 50 columns: the URL starts on row 0 and continues on row 1.
        const QByteArray url = "https://example.com/a/very/long/path/that/wraps/around/the/edge";
        t.backend->writeToDisplay("see " + url + " ok\r\n");
        QVERIFY(t.waitScreen(QStringLiteral("edge")));
        QTest::qWait(60);
        const int cw = t.view->cellWidth(), ch = t.view->cellHeight();
        QTest::mouseClick(t.view, Qt::LeftButton, Qt::ControlModifier, QPoint(2 + 3 * cw + cw / 2, 2 + ch + ch / 2));
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0][0].toString(), QString::fromLatin1(url));
    }

    // Issue GWXM: Ctrl+Shift+L walks the links in the screen and the scrollback, newest
    // first, scrolling older ones back into view; Esc drops the highlight.
    void keyboardLinkWalk()
    {
        QFETCH_GLOBAL(QString, core);
        QTemporaryDir dir;
        for (const char *name : {"alpha.txt", "beta.txt", "gamma.txt"}) {
            QFile f(dir.filePath(QString::fromLatin1(name)));
            QVERIFY(f.open(QIODevice::WriteOnly));
        }
        Term t(core, QStringLiteral("/bin/cat"), {}, dir.path());
        const QByteArray osc7 = "\x1b]7;file://" + QUrl::toPercentEncoding(dir.path(), "/") + "\x07";
        QByteArray filler;
        for (int i = 0; i < 20; ++i)
            filler += "filler line " + QByteArray::number(i) + "\r\n";
        // alpha scrolls into the scrollback; beta and gamma stay on the screen.
        t.backend->writeToDisplay(osc7 + "alpha.txt:3:1\r\n" + filler + "beta.txt gamma.txt:7\r\n");
        QVERIFY(t.waitScreen(QStringLiteral("gamma.txt")));
        QTest::qWait(60);

        TerminalView::Link link;
        QVERIFY(t.view->stepLink(-1, &link));          // the newest link first
        QCOMPARE(link.target, QFileInfo(dir.filePath(QStringLiteral("gamma.txt"))).absoluteFilePath());
        QCOMPARE(link.line, 7);
        QVERIFY(t.view->linkWalkActive());
        QCOMPARE(t.view->linkWalkCount(), 3);
        QVERIFY(t.view->stepLink(-1, &link));
        QCOMPARE(QFileInfo(link.target).fileName(), QStringLiteral("beta.txt"));
        QVERIFY(t.view->stepLink(-1, &link));          // back into the scrollback
        QCOMPARE(QFileInfo(link.target).fileName(), QStringLiteral("alpha.txt"));
        QCOMPARE(link.line, 3);
        QVERIFY(!t.backend->selectedText().isEmpty()); // the link is highlighted
        QVERIFY(t.view->stepLink(-1, &link));          // and the walk wraps at the oldest
        QCOMPARE(QFileInfo(link.target).fileName(), QStringLiteral("gamma.txt"));
        QVERIFY(t.view->stepLink(1, &link));           // the arrows go the other way
        QCOMPARE(QFileInfo(link.target).fileName(), QStringLiteral("alpha.txt"));
        t.view->endLinkWalk();
        QVERIFY(!t.view->linkWalkActive());
    }

    // Issue YZTK: a plain left click follows a path, a click that drags still selects, and
    // a path that does not exist is not a link at all.
    void plainClickFollowsAPath()
    {
        QFETCH_GLOBAL(QString, core);
        QTemporaryDir dir;
        QFile f(dir.filePath(QStringLiteral("notes.txt")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("sub")));
        Term t(core, QStringLiteral("/bin/cat"), {}, dir.path());
        QSignalSpy links(t.view, &TerminalView::linkActivated);
        const QByteArray osc7 = "\x1b]7;file://" + QUrl::toPercentEncoding(dir.path(), "/") + "\x07";
        t.backend->writeToDisplay(osc7 + "notes.txt sub missing.txt\r\n");
        QVERIFY(t.waitScreen(QStringLiteral("missing")));
        QTest::qWait(60);
        const int cw = t.view->cellWidth(), ch = t.view->cellHeight();
        auto click = [&](int col) { QTest::mouseClick(t.view, Qt::LeftButton, Qt::NoModifier, QPoint(2 + col * cw + cw / 2, 2 + ch / 2)); };
        click(2);  // notes.txt
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0][0].toString(), QFileInfo(f).absoluteFilePath());
        click(11); // sub (a folder)
        QCOMPARE(links.size(), 2);
        QCOMPARE(QFileInfo(links[1][0].toString()).fileName(), QStringLiteral("sub"));
        click(18); // missing.txt does not exist
        QCOMPARE(links.size(), 2);
        QVERIFY(t.view->linkAtPoint(QPoint(2 + 18 * cw, 2 + ch / 2)).target.isEmpty());
        // Disarmed (an inactive Relay pane): a plain click no longer opens, Ctrl+click does.
        t.view->setPlainClickOpensLinks(false);
        click(2);
        QCOMPARE(links.size(), 2);
        QTest::mouseClick(t.view, Qt::LeftButton, Qt::ControlModifier, QPoint(2 + 2 * cw + cw / 2, 2 + ch / 2));
        QCOMPARE(links.size(), 3);
    }

    void pathTokenParsing()
    {
        QString path;
        int line = 0, col = 0;
        QVERIFY(TerminalView::splitPathToken(QStringLiteral("src/main.cpp:42:7"), &path, &line, &col));
        QCOMPARE(path, QStringLiteral("src/main.cpp"));
        QCOMPARE(line, 42);
        QCOMPARE(col, 7);
        QVERIFY(TerminalView::splitPathToken(QStringLiteral("a.py:9:"), &path, &line, &col));
        QCOMPARE(path, QStringLiteral("a.py"));
        QCOMPARE(line, 9);
        QCOMPARE(col, -1);
    }

    void scrollbackScrollingApi()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        QByteArray many;
        for (int i = 0; i < 100; ++i)
            many += "row " + QByteArray::number(i) + "\r\n";
        t.backend->writeToDisplay(many);
        QVERIFY(t.waitScreen(QStringLiteral("row 99")));
        QSignalSpy pos(t.view, &TerminalView::scrollPositionChanged);
        t.backend->scrollPages(-2);
        QTRY_VERIFY_WITH_TIMEOUT(!pos.isEmpty(), 2000);
        const int top = pos.last()[0].toInt();
        const int history = pos.last()[1].toInt();
        QVERIFY2(top < history, qPrintable(QStringLiteral("%1 %2").arg(top).arg(history)));
        t.backend->scrollToBottom();
        QTRY_VERIFY_WITH_TIMEOUT(pos.last()[0].toInt() == pos.last()[1].toInt(), 2000);
        QCOMPARE(t.backend->scrollbackText(2).size(), 2);
    }

    void inputMethod()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        t.backend->writeToDisplay("abc");
        QVERIFY(t.waitScreen(QStringLiteral("abc")));
        QTest::qWait(60);
        const QRect r = t.view->inputMethodQuery(Qt::ImCursorRectangle).toRect();
        QCOMPARE(r.left(), 2 + 3 * t.view->cellWidth());
        QCOMPARE(r.height(), t.view->cellHeight());
        QInputMethodEvent preedit(QStringLiteral("ni"), {});
        QApplication::sendEvent(t.view, &preedit);
        QInputMethodEvent commit;
        commit.setCommitString(QStringLiteral("你"));
        QApplication::sendEvent(t.view, &commit);
        QVERIFY(t.waitScreen(QStringLiteral("你")));
    }

    void accessibleText()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        t.backend->writeToDisplay("accessible line");
        QVERIFY(t.waitScreen(QStringLiteral("accessible")));
        QTest::qWait(60);
        QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(t.view);
        QVERIFY(iface);
        QCOMPARE(iface->role(), QAccessible::Terminal);
        QVERIFY(iface->textInterface());
        QVERIFY(iface->textInterface()->text(0, 15).startsWith(QStringLiteral("accessible line")));
    }

    void keyMapper()
    {
        KeyInput k;
        QVERIFY(mapKeyEvent(Qt::Key_A, Qt::ShiftModifier, QStringLiteral("A"), false, &k));
        QCOMPARE(k.codepoint, char32_t('a'));
        QCOMPARE(k.text, QStringLiteral("A"));
        QCOMPARE(int(k.modifiers), int(ModShift));
        QVERIFY(mapKeyEvent(Qt::Key_Up, Qt::ControlModifier, QString(), false, &k));
        QCOMPARE(int(k.key), int(Key::Up));
        QVERIFY(mapKeyEvent(Qt::Key_5, Qt::KeypadModifier, QStringLiteral("5"), false, &k));
        QCOMPARE(int(k.key), int(Key::Kp5));
        QVERIFY(!mapKeyEvent(Qt::Key_Shift, Qt::ShiftModifier, QString(), false, &k));
        QVERIFY(mapKeyEvent(Qt::Key_Backtab, Qt::ShiftModifier, QString(), false, &k));
        QCOMPARE(int(k.key), int(Key::Tab));
        // AltGr composition on Windows arrives as Ctrl+Alt with printable text.
        QVERIFY(mapKeyEvent(Qt::Key_At, Qt::ControlModifier | Qt::AltModifier, QStringLiteral("@"), false, &k));
        QCOMPARE(int(k.modifiers), int(ModNone));
        // Super/Cmd combinations are application shortcuts, not program input.
#if defined(Q_OS_MACOS)
        QVERIFY(!mapKeyEvent(Qt::Key_K, Qt::ControlModifier, QStringLiteral("k"), false, &k));
#else
        QVERIFY(!mapKeyEvent(Qt::Key_K, Qt::MetaModifier, QStringLiteral("k"), false, &k));
#endif
    }

    void hostShortcutFilter()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        t.view->setShortcutFilter([](const QKeyEvent *e) { return e->key() == Qt::Key_F12; });
        QKeyEvent f12(QEvent::KeyPress, Qt::Key_F12, Qt::NoModifier);
        QApplication::sendEvent(t.view, &f12);
        QVERIFY(!f12.isAccepted());
    }
};

QObject *makeViewTest()
{
    return new ViewTest;
}

#include "ViewTest.moc"
