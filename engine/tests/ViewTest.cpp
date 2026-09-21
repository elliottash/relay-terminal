// SPDX-License-Identifier: AGPL-3.0-or-later
// TerminalView / VTermBackend tests on the offscreen platform: rendering,
// keyboard, mouse selection, links, IME, scrolling, accessibility, key mapping.
#include "backend/VTermBackend.h"
#include "session/TerminalSession.h"
#include "view/KeyMapper.h"
#include "view/TerminalView.h"
#include "WordWrap.h"
#include "MarkdownAnsi.h"
#include "view/ProseSpans.h"

#include <QAccessible>
#include <QFontDatabase>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

using namespace relay;

namespace {

// A fold's detail as the host builds it: one span per line, default colours.
QVector<FoldLine> foldBody(const QStringList &texts)
{
    QVector<FoldLine> out;
    for (const QString &t : texts) {
        FoldSpan s;
        s.text = t;
        FoldLine l;
        l.spans << s;
        out << l;
    }
    return out;
}

struct Term {
    std::unique_ptr<VTermBackend> backend;
    TerminalView *view = nullptr;
    QStringList links;

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
        QObject::connect(view, &TerminalView::linkActivated, view,
                         [this](const QString &target, int, int) { links << target; });
    }
    // Three lines of output, the middle one an OSC 8 fold anchor. Its first
    // cell is a placeholder the view overpaints with the chevron.
    void anchoredLines()
    {
        view->setFoldPrefix(QStringLiteral("relay://call/"));
        // Enough output to fill the screen, so the anchor sits where a real
        // one does: some way up from the prompt, with history above it.
        for (int i = 0; i < 14; ++i)
            backend->writeToDisplay(QByteArray("filler ") + QByteArray::number(i) + "\r\n");
        backend->writeToDisplay("before\r\n");
        backend->writeToDisplay("\x1b]8;;relay://call/p/1/a\x1b\\* ran python\x1b]8;;\x1b\\\r\n");
        backend->writeToDisplay("after\r\n");
        QTest::qWait(120);
    }
    int rowOf(const QString &text) const { return view->visibleRowsText().indexOf(text); }
    QPoint cellPoint(int row, int col) const
    {
        return QPoint(2 + col * view->cellWidth() + view->cellWidth() / 2,
                      2 + row * view->cellHeight() + view->cellHeight() / 2);
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

// Does any pixel of this screen row carry exactly this colour? The search
// highlights are flat fills, so an exact compare is right.
bool rowHasColor(const QImage &img, int screenRow, int cellHeight, const QColor &c)
{
    const int top = 2 + screenRow * cellHeight;
    for (int y = top; y < top + cellHeight && y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (img.pixelColor(x, y) == c)
                return true;
    return false;
}

int countNonBackground(const QImage &img, const QRect &r, const QColor &bg)
{
    int n = 0;
    for (int y = r.top(); y <= r.bottom() && y < img.height(); ++y)
        for (int x = r.left(); x <= r.right() && x < img.width(); ++x)
            if (img.pixelColor(x, y) != bg)
                ++n;
    return n;
}

// ---- prose re-wrap (#R2WQ) ----
//
// The rows the pane prints for one block of its own text, and the rows the view
// must lay out for the same block at any other width: WordWrap's bytes, with
// the terminal's own autowrap applied to the words it leaves whole. SGR runs
// take no width; a cursor-forward indent is the blank cells it leaves.
QString shownProse(const QString &rendered)
{
    static const QRegularExpression sgr(QStringLiteral("\\x1b\\[[0-9;]*m"));
    static const QRegularExpression forward(QStringLiteral("\\x1b\\[(\\d+)C"));
    QString text = QString(rendered).remove(sgr);
    for (auto m = forward.match(text); m.hasMatch(); m = forward.match(text))
        text.replace(m.capturedStart(), m.capturedLength(), QString(m.captured(1).toInt(), QLatin1Char(' ')));
    return text;
}

QStringList proseRows(const QString &rendered, int columns)
{
    relay::WordWrap w;
    w.setColumns(columns);
    QStringList out;
    QStringList byteRows = shownProse(w.feed(rendered) + w.flush()).split(QLatin1Char('\n'));
    if (!byteRows.isEmpty() && byteRows.last().isEmpty())
        byteRows.removeLast();
    for (const QString &row : byteRows) {
        QString line;
        int used = 0;
        for (const QChar c : row) {
            const int width = relay::WordWrap::cellWidth(c.unicode());
            if (used > 0 && used + width > columns) {
                out << line;
                line.clear();
                used = 0;
            }
            line += c;
            used += width;
        }
        out << line;
    }
    return out;
}

QVector<FoldLine> proseLines(const QStringList &texts)
{
    QVector<FoldLine> out;
    for (const QString &t : texts) {
        FoldSpan s;
        s.text = t;
        FoldLine l;
        l.spans << s;
        out << l;
    }
    return out;
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

    // #SFZC: a bare folder word in Relay's own prose (a relay://prose/ run, #R2WQ) is not a
    // link — not under the pointer, not at rest, not for the walk — while the same word in
    // program output keeps linking (an `ls` row), and in prose a folder named with its slash
    // and a bare file name still link.
    void proseBareFolderWordsAreNotLinks()
    {
        QFETCH_GLOBAL(QString, core);
        QTemporaryDir dir;
        { QFile f(dir.filePath(QStringLiteral("notes.txt"))); QVERIFY(f.open(QIODevice::WriteOnly)); }
        QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("tests")));
        Term t(core, QStringLiteral("/bin/cat"), {}, dir.path());
        t.backend->resizeTerminal(12, 100);
        ColorScheme scheme = t.view->colorScheme();
        scheme.link = QColor(0x12, 0x34, 0xab); // a colour nothing else on the screen has
        t.view->setColorScheme(scheme);
        const QByteArray osc7 = "\x1b]7;file://" + QUrl::toPercentEncoding(dir.path(), "/") + "\x07";
        // Row 0 is program output. Rows 1-2 are prose, one relay://prose/ run over both:
        // the bare folder word is row 1, the forms that keep linking are row 2.
        t.backend->writeToDisplay(osc7 + QByteArray("tests\r\n"));
        t.backend->writeToDisplay(QByteArray("\x1b[97m\x1b]8;;relay://prose/t/9\x1b\\")
                                  + QByteArray("the tests folder\r\n")
                                  + QByteArray("see tests/ and notes.txt\r\n")
                                  + QByteArray("\x1b]8;;\x1b\\\x1b[0m"));
        QVERIFY(t.waitScreen(QStringLiteral("notes.txt")));
        QTest::qWait(60);
        const int cw = t.view->cellWidth(), ch = t.view->cellHeight();
        const QString folder = QFileInfo(dir.filePath(QStringLiteral("tests"))).absoluteFilePath();
        // Hover: the program row's bare word links; the prose word does not; the slashed
        // folder and the file name on the prose row do.
        QCOMPARE(t.view->linkAtPoint(QPoint(2 + 2 * cw + cw / 2, 2 + ch / 2)).target, folder);
        QVERIFY(t.view->linkAtPoint(QPoint(2 + 6 * cw + cw / 2, 2 + ch + ch / 2)).target.isEmpty());
        QCOMPARE(t.view->linkAtPoint(QPoint(2 + 6 * cw + cw / 2, 2 + 2 * ch + ch / 2)).target, folder);
        QVERIFY(t.view->linkAtPoint(QPoint(2 + 19 * cw + cw / 2, 2 + 2 * ch + ch / 2))
                    .target.endsWith(QStringLiteral("notes.txt")));
        // The walk steps the three links, not the prose word.
        TerminalView::Link link;
        QVERIFY(t.view->stepLink(-1, &link));
        QCOMPARE(t.view->linkWalkCount(), 3);
        QVERIFY(link.target.endsWith(QStringLiteral("notes.txt")));
        t.view->endLinkWalk();
        // At rest: the bare folder word wears the link colour on the program row and does
        // not on the prose row; the slashed form on the prose row does.
        const QImage img = t.grab();
        QVERIFY2(rowHasColor(img, 0, ch, scheme.link), "the program row's folder word lost the link colour");
        QVERIFY2(!rowHasColor(img, 1, ch, scheme.link), "a bare folder word in prose is coloured as a link");
        QVERIFY2(rowHasColor(img, 2, ch, scheme.link), "the slashed folder in prose lost the link colour");
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

    // ---- folds (#TK9C): the detail of a tool call, unfolded inside the grid

    void foldOpensUnderItsAnchorAndShutsAgain()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        t.anchoredLines();
        const QString uri = QStringLiteral("relay://call/p/1/a");
        QVERIFY(t.view->expandedFolds().isEmpty());

        t.view->setFoldContent(uri, foldBody({QStringLiteral("python -c 'print(1)'"), QStringLiteral("1")}));
        QTest::qWait(80);
        QCOMPARE(t.view->expandedFolds(), QStringList{uri});
        QVERIFY(t.view->foldExpanded(uri));
        QStringList rows = t.view->visibleRowsText();
        const int anchor = rows.indexOf(QStringLiteral("* ran python"));
        QVERIFY(anchor >= 0);
        QCOMPARE(rows.value(anchor + 1).trimmed(), QStringLiteral("python -c 'print(1)'"));
        QCOMPARE(rows.value(anchor + 2).trimmed(), QStringLiteral("1"));
        QCOMPARE(rows.value(anchor + 3), QStringLiteral("after"));
        QVERIFY(rows.value(anchor + 1).startsWith(QStringLiteral("   "))); // the block's indent

        // Shut again: the rows go away and the real ones close up.
        t.view->setFoldExpanded(uri, false);
        QTest::qWait(80);
        rows = t.view->visibleRowsText();
        QCOMPARE(rows.value(rows.indexOf(QStringLiteral("* ran python")) + 1), QStringLiteral("after"));
        QVERIFY(!t.view->foldExpanded(uri));
    }

    void anUnknownFoldAsksTheHostForItsDetail()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        t.anchoredLines();
        QStringList asked;
        t.view->onFoldRequested = [&asked](const QString &uri) { asked << uri; };
        // A plain left click on the anchor line.
        const QPoint p = t.cellPoint(t.rowOf(QStringLiteral("* ran python")), 4);
        QTest::mouseClick(t.view, Qt::LeftButton, Qt::NoModifier, p);
        QTest::qWait(60);
        QCOMPARE(asked, QStringList{QStringLiteral("relay://call/p/1/a")});
        // The click did not open the URI as a link.
        QVERIFY(t.links.isEmpty());

        // Once the host answers, the same click shuts and opens it again.
        t.view->setFoldContent(asked.first(), foldBody({QStringLiteral("detail")}));
        QTest::qWait(80);
        QVERIFY(t.view->foldExpanded(asked.first()));
        QTest::mouseClick(t.view, Qt::LeftButton, Qt::NoModifier, t.cellPoint(t.rowOf(QStringLiteral("* ran python")), 4));
        QTest::qWait(60);
        QVERIFY(!t.view->foldExpanded(asked.first()));
        QCOMPARE(asked.size(), 1); // it had content, so the host was not asked again
        QVERIFY(t.links.isEmpty());
    }

    void aFoldStaysUnderItsLineAcrossAResize()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        t.anchoredLines();
        const QString uri = QStringLiteral("relay://call/p/1/a");
        t.view->setFoldContent(uri, foldBody({QStringLiteral("one"), QStringLiteral("two")}));
        QTest::qWait(80);
        t.backend->resizeTerminal(14, 34);
        QTest::qWait(150);
        const QStringList rows = t.view->visibleRowsText();
        const int anchor = rows.indexOf(QStringLiteral("* ran python"));
        QVERIFY2(anchor >= 0, qPrintable(rows.join(QLatin1Char('|'))));
        QCOMPARE(rows.value(anchor + 1).trimmed(), QStringLiteral("one"));
        QCOMPARE(rows.value(anchor + 2).trimmed(), QStringLiteral("two"));
        QVERIFY(t.view->foldExpanded(uri));
    }

    void clearingTheScrollbackDropsTheFold()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        t.anchoredLines();
        const QString uri = QStringLiteral("relay://call/p/1/a");
        t.view->setFoldContent(uri, foldBody({QStringLiteral("one")}));
        QTest::qWait(80);
        QVERIFY(t.view->foldExpanded(uri));
        // Clear the screen and the scrollback: the anchor line is gone, so the
        // fold that hung under it goes too.
        t.backend->clear();
        t.backend->writeToDisplay("plain\r\n");
        QTest::qWait(900); // the anchor walk runs on its own slow heartbeat
        QVERIFY2(t.view->expandedFolds().isEmpty(), qPrintable(t.view->expandedFolds().join(QLatin1Char(','))));
    }

    void foldRowsCountInTheScrollRangeAndScrollOneByOne()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        t.anchoredLines();
        QSignalSpy spy(t.view, &TerminalView::scrollPositionChanged);
        QStringList body;
        for (int i = 0; i < 40; ++i)
            body << QStringLiteral("detail %1").arg(i);
        t.view->setFoldContent(QStringLiteral("relay://call/p/1/a"), foldBody(body));
        QTest::qWait(120);
        QVERIFY(!spy.isEmpty());
        const QList<QVariant> last = spy.last();
        QVERIFY2(last.at(1).toInt() >= 30, qPrintable(QString::number(last.at(1).toInt())));
        QVERIFY(t.view->viewportAtBottom());
        // At the bottom the newest output is still on screen: the fold pushed
        // the older rows up, not the prompt off.
        QVERIFY(t.view->visibleRowsText().contains(QStringLiteral("after")));

        // One wheel notch back moves the window by three visual rows, which are
        // rows of the fold, not of the output.
        const QStringList before = t.view->visibleRowsText();
        t.view->scrollLines(-1);
        QTest::qWait(60);
        const QStringList after = t.view->visibleRowsText();
        QVERIFY(!t.view->viewportAtBottom());
        QCOMPARE(after.value(1), before.value(0));
        t.view->scrollToBottom();
        QTest::qWait(60);
        QVERIFY(t.view->viewportAtBottom());
        QCOMPARE(t.view->visibleRowsText(), before);
    }

    void aFullScreenProgramHidesTheFolds()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        t.anchoredLines();
        t.view->setFoldContent(QStringLiteral("relay://call/p/1/a"), foldBody({QStringLiteral("one")}));
        QTest::qWait(80);
        QVERIFY(t.view->visibleRowsText().contains(QStringLiteral("   one")));
        t.backend->writeToDisplay("\x1b[?1049h"); // alternate screen
        QTest::qWait(80);
        QVERIFY(t.backend->altScreen());
        const QStringList alt = t.view->visibleRowsText();
        for (const QString &row : alt)
            QVERIFY2(!row.contains(QStringLiteral("one")), qPrintable(alt.join(QLatin1Char('|'))));
        t.backend->writeToDisplay("\x1b[?1049l");
        QTest::qWait(80);
        QVERIFY(t.view->visibleRowsText().contains(QStringLiteral("   one")));
    }

    // A real row is painted by the same path whether or not a block is open under it, and a
    // fold's own rows leave nothing behind them: the row's ink, its font and its glyph positions
    // are the same pixels either way (#TK9C). The row here is the one a tool-call line ends with
    // — muted grey stats — because that is the row a reader would notice restyling on first.
    void anOpenFoldLeavesTheRealRowsAloneWhenItIsPainted()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        t.view->setFoldPrefix(QStringLiteral("relay://call/"));
        for (int i = 0; i < 14; ++i)
            t.backend->writeToDisplay(QByteArray("filler ") + QByteArray::number(i) + "\r\n");
        t.backend->writeToDisplay("\x1b]8;;relay://call/p/1/a\x1b\\* ran python\x1b]8;;\x1b\\\r\n");
        // The muted remainder the pane prints after the title, in its own truecolour grey.
        t.backend->writeToDisplay("\x1b[38;2;150;150;150m15 lines exit 0\x1b[0m\r\n");
        QVERIFY(t.waitScreen(QStringLiteral("15 lines exit 0")));

        const QString stats = QStringLiteral("15 lines exit 0");
        const int cw = t.view->cellWidth(), ch = t.view->cellHeight();
        const auto rowPixels = [&](const QImage &img, int row) {
            return img.copy(QRect(2, 2 + row * ch, stats.size() * cw, ch));
        };
        const QImage shut = t.grab();
        const int rowShut = t.rowOf(stats);
        QVERIFY2(rowShut >= 0, qPrintable(t.view->visibleRowsText().join(QLatin1Char('|'))));
        const QImage before = rowPixels(shut, rowShut);

        t.view->setFoldContent(QStringLiteral("relay://call/p/1/a"),
                               foldBody({QStringLiteral("python -c 'print(1)'"), QStringLiteral("1")}));
        QTest::qWait(120);
        QVERIFY(t.view->foldExpanded(QStringLiteral("relay://call/p/1/a")));
        const QImage open = t.grab();
        const int rowOpen = t.rowOf(stats);
        QVERIFY2(rowOpen >= 0, qPrintable(t.view->visibleRowsText().join(QLatin1Char('|'))));
        // The block's last row is painted immediately before this one, so anything a fold row
        // left in the painter would land here.
        QCOMPARE(t.view->visibleRowsText().value(rowOpen - 1).trimmed(), QStringLiteral("1"));
        QVERIFY2(rowPixels(open, rowOpen) == before,
                 "an open fold changed how a real row is drawn (ink, font or position)");
    }

    void selectionCrossesTheFoldBoundaryInVisualOrder()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        t.anchoredLines();
        t.view->setFoldContent(QStringLiteral("relay://call/p/1/a"),
                               foldBody({QStringLiteral("one"), QStringLiteral("two")}));
        QTest::qWait(100);
        const int anchor = t.rowOf(QStringLiteral("* ran python"));
        QVERIFY(anchor >= 0);
        QCOMPARE(t.view->visibleRowsText().value(anchor + 3), QStringLiteral("after"));

        const QPoint a = t.cellPoint(anchor, 0);
        const QPoint b = t.cellPoint(anchor + 3, 20);
        QTest::mousePress(t.view, Qt::LeftButton, Qt::NoModifier, a);
        QMouseEvent move(QEvent::MouseMove, b, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(t.view, &move);
        QTest::mouseRelease(t.view, Qt::LeftButton, Qt::NoModifier, b);
        // Real rows and fold rows interleaved exactly as displayed; the fold
        // lines come back without their indent.
        QCOMPARE(t.backend->selectedText(),
                 QStringLiteral("* ran python\none\ntwo\nafter"));

        // A double click inside the fold picks a word out of it.
        QTest::mouseDClick(t.view, Qt::LeftButton, Qt::NoModifier, t.cellPoint(anchor + 2, 4));
        QCOMPARE(t.backend->selectedText(), QStringLiteral("two"));
    }

    void aLinkInsideAFoldOpens()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        t.anchoredLines();
        FoldSpan plain;
        plain.text = QStringLiteral("see ");
        FoldSpan linked;
        linked.text = QStringLiteral("the report");
        linked.link = QStringLiteral("https://relay.test/report");
        FoldLine line;
        line.spans << plain << linked;
        t.view->setFoldContent(QStringLiteral("relay://call/p/1/a"), QVector<FoldLine>{line});
        QTest::qWait(100);
        const int anchor = t.rowOf(QStringLiteral("* ran python"));
        QVERIFY(anchor >= 0);
        // The link sits at the block's indent + "see " (4 cells).
        const QPoint p = t.cellPoint(anchor + 1, 3 + 6);
        QCOMPARE(t.view->linkAtPoint(p).target, QStringLiteral("https://relay.test/report"));
        QTest::mouseClick(t.view, Qt::LeftButton, Qt::ControlModifier, p);
        QTest::qWait(40);
        QCOMPARE(t.links, QStringList{QStringLiteral("https://relay.test/report")});
    }

    // …and a *plain* click on it does too. Owner, 2026-09-19 (#K48R): "the open in pane link on the
    // thinking fold doesnt work" — the "open in pane" row of a fold is a FoldSpan link, and nobody
    // ctrl-clicks a word that is underlined and coloured like a link.
    void aPlainClickOnALinkInsideAFoldOpensIt()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        t.anchoredLines();
        FoldSpan plain;
        plain.text = QStringLiteral("see ");
        FoldSpan linked;
        linked.text = QStringLiteral("open in pane");
        linked.link = QStringLiteral("relay://turn/p/1");
        FoldLine line;
        line.spans << plain << linked;
        t.view->setFoldContent(QStringLiteral("relay://call/p/1/a"), QVector<FoldLine>{line});
        QTest::qWait(100);
        const int anchor = t.rowOf(QStringLiteral("* ran python"));
        QVERIFY(anchor >= 0);
        const QPoint p = t.cellPoint(anchor + 1, 3 + 6);
        QTest::mouseClick(t.view, Qt::LeftButton, Qt::NoModifier, p);
        QTest::qWait(40);
        QCOMPARE(t.links, QStringList{QStringLiteral("relay://turn/p/1")});
    }

    // A path in the output that resolves wears the link colour at rest — in the default ink and in
    // a plain one like the bright white the agent's prose is written in; a chromatic colour the
    // program chose is never overridden; the option turns it all off (owner, 2026-09-19).
    void aPathInTheOutputWearsTheLinkColourAtRest()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        // A short name: the screen is 50 columns and a path that wraps is two rows.
        QTemporaryDir dir(QDir::tempPath() + QStringLiteral("/lk-XXXXXX"));
        QVERIFY(dir.isValid());
        t.backend->resizeTerminal(12, 100);
        const QString file = dir.filePath(QStringLiteral("notes.txt"));
        { QFile f(file); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("x"); }
        ColorScheme scheme = t.view->colorScheme();
        scheme.link = QColor(0x12, 0x34, 0xab); // a colour nothing else on the screen has
        t.view->setColorScheme(scheme);
        const int ch = t.view->cellHeight();
        // Row 0: the path in the default foreground. Row 1: the same path painted red by the
        // program. Row 2: in bright white (SGR 97), the agent's prose ink.
        t.backend->writeToDisplay(("see " + file + "\r\n").toUtf8());
        t.backend->writeToDisplay(("\x1b[31m" + file + "\x1b[0m\r\n").toUtf8());
        t.backend->writeToDisplay(("\x1b[97m" + file + "\x1b[0m\r\n").toUtf8());
        QVERIFY(t.waitScreen(QStringLiteral("notes.txt")));
        QImage img = t.grab();
        QVERIFY2(rowHasColor(img, 0, ch, scheme.link), "a resolving path is not in the link colour");
        QVERIFY2(!rowHasColor(img, 1, ch, scheme.link), "the program's own red was overridden");
        QVERIFY(rowHasColor(img, 1, ch, QColor::fromRgb(scheme.palette[1])));
        QVERIFY2(rowHasColor(img, 2, ch, scheme.link), "a path in plain bright-white ink is not in the link colour");
        // A light theme's "bright white" is a warm near-black (IBM Beige: #14120d), whose HSV
        // saturation is high although it is plainly a grey: it must still count as plain ink.
        scheme.palette[15] = 0xff14120d;
        t.view->setColorScheme(scheme);
        img = t.grab();
        QVERIFY2(rowHasColor(img, 2, ch, scheme.link), "a path in a dark warm 'bright white' lost the link colour");
        t.view->setLinksColouredAtRest(false);
        img = t.grab();
        QVERIFY2(!rowHasColor(img, 0, ch, scheme.link), "the option is off but the path is still coloured");
        QVERIFY(!rowHasColor(img, 2, ch, scheme.link));
    }

    // A theme switch reaches text already on the screen: an indexed colour (SGR 97, the agent's
    // prose ink) is resolved through the palette the view has *now*, not the one the text was
    // written under. Owner's QA, 2026-09-19: a pane that started on IBM Beige and switched to
    // Dark Copper painted new prose in Beige's near-black ANSI 15 on Copper's charcoal.
    void anIndexedColourFollowsTheSchemeItIsPaintedUnder()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        ColorScheme scheme = t.view->colorScheme();
        scheme.palette[15] = 0xff14120d;   // IBM Beige's "bright white"
        scheme.palette[6] = 0xff0f5f5a;    // and its cyan
        t.view->setColorScheme(scheme);
        t.backend->writeToDisplay("\x1b[97mprose here\x1b[0m\r\n\x1b[36mcode here\x1b[0m\r\n");
        QVERIFY(t.waitScreen(QStringLiteral("code here")));
        const int ch = t.view->cellHeight();
        QImage img = t.grab();
        QVERIFY(rowHasColor(img, 0, ch, QColor(0x14, 0x12, 0x0d)));
        QVERIFY(rowHasColor(img, 1, ch, QColor(0x0f, 0x5f, 0x5a)));
        // Switch: Dark Copper's palette entries for the same two indices.
        scheme.palette[15] = 0xfff4efe9;
        scheme.palette[6] = 0xff56c8d8;
        t.view->setColorScheme(scheme);
        img = t.grab();
        QVERIFY2(rowHasColor(img, 0, ch, QColor(0xf4, 0xef, 0xe9)), "SGR 97 text kept the old palette after the switch");
        QVERIFY2(!rowHasColor(img, 0, ch, QColor(0x14, 0x12, 0x0d)), "the old bright white is still painted");
        QVERIFY2(rowHasColor(img, 1, ch, QColor(0x56, 0xc8, 0xd8)), "SGR 36 text kept the old palette after the switch");
        // And text written after the switch resolves through the new palette too.
        t.backend->writeToDisplay("\x1b[97mlater\x1b[0m\r\n");
        QVERIFY(t.waitScreen(QStringLiteral("later")));
        img = t.grab();
        QVERIFY2(rowHasColor(img, 2, ch, QColor(0xf4, 0xef, 0xe9)), "text written after the switch uses the old palette");
    }

    // Erase-to-end-of-line paints the current background to the pane's edge (background colour
    // erase): what the host relies on for the band behind a line the user typed.
    void eraseToEndOfLineCarriesTheBackground()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        t.backend->writeToDisplay("\x1b[48;2;10;20;30mhi\x1b[K\x1b[0m\r\nnext\r\n");
        QVERIFY(t.waitScreen(QStringLiteral("next")));
        const QImage img = t.grab();
        const int cw = t.view->cellWidth(), ch = t.view->cellHeight();
        QCOMPARE(img.pixelColor(2 + 20 * cw + cw / 2, 2 + ch / 2), QColor(10, 20, 30));
        QCOMPARE(img.pixelColor(2 + 45 * cw + cw / 2, 2 + ch / 2), QColor(10, 20, 30));
        QVERIFY(img.pixelColor(2 + 20 * cw + cw / 2, 2 + ch + ch / 2) != QColor(10, 20, 30));
    }

    // The shell's prompt row — the one OSC 133;A marks, where the shell echoes what the user
    // typed — sits on the scheme's prompt band, under every cell without a background of its own;
    // a cell that brought one keeps it, and no other row is banded.
    void thePromptRowSitsOnThePromptBand()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        ColorScheme scheme = t.view->colorScheme();
        scheme.promptBand = QColor(0x20, 0x40, 0x60);
        t.view->setColorScheme(scheme);
        t.backend->writeToDisplay("\x1b]133;A\x1b\\$ ls \x1b[41mred\x1b[0m\x1b]133;B\x1b\\\r\n\x1b]133;C\x1b\\output\r\n");
        QVERIFY(t.waitScreen(QStringLiteral("output")));
        const QImage img = t.grab();
        const int cw = t.view->cellWidth(), ch = t.view->cellHeight();
        QCOMPARE(img.pixelColor(2 + 30 * cw + cw / 2, 2 + ch / 2), scheme.promptBand);        // past the text
        QVERIFY(img.pixelColor(2 + 5 * cw + cw / 2, 2 + ch / 2).red() > 150);                  // the program's red stays
        QVERIFY(img.pixelColor(2 + 30 * cw + cw / 2, 2 + ch + ch / 2) != scheme.promptBand);   // the output row is plain
        scheme.promptBand = QColor();
        t.view->setColorScheme(scheme);
        QVERIFY(t.grab().pixelColor(2 + 30 * cw + cw / 2, 2 + ch / 2) != QColor(0x20, 0x40, 0x60));
    }

    // A row the host marked as typed by the user (OSC 7772) wears its role's band across the whole
    // grid and its role's ink on every cell without a colour of its own — and because the row holds
    // a role rather than a colour, a scheme switch recolours it where a written SGR never could
    // (owner, 2026-09-19: "the background highlights shift with theme changes").
    void aUserRowWearsItsRoleFromTheSchemeInForce()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        ColorScheme scheme = t.view->colorScheme();
        scheme.userAgentBand = QColor(0xab, 0x97, 0xf7); scheme.userAgentInk = QColor(0x10, 0x08, 0x30);
        scheme.userShellBand = QColor(0x45, 0xc8, 0xee); scheme.userShellInk = QColor(0x04, 0x20, 0x2a);
        t.view->setColorScheme(scheme);
        t.backend->writeToDisplay("\x1b]7772;agent\x1b\\\x1b[1m* fix the build\x1b[0m\r\nplain reply\r\n"
                                  "\x1b]7772;shell\x1b\\\x1b[1m! make\x1b[0m\r\n");
        QVERIFY(t.waitScreen(QStringLiteral("! make")));
        const int cw = t.view->cellWidth(), ch = t.view->cellHeight();
        const auto at = [&](const QImage &img, int row, int col) { return img.pixelColor(2 + col * cw + cw / 2, 2 + row * ch + ch / 2); };
        QImage img = t.grab();
        QCOMPARE(at(img, 0, 40), scheme.userAgentBand);                       // the band runs past the text
        QVERIFY(rowHasColor(img, 0, ch, scheme.userAgentInk));                // the words are the role's ink
        QVERIFY(at(img, 1, 40) != scheme.userAgentBand);                      // the reply under it is plain
        QVERIFY(!rowHasColor(img, 1, ch, scheme.userAgentInk));
        QCOMPARE(at(img, 2, 40), scheme.userShellBand);
        QVERIFY(rowHasColor(img, 2, ch, scheme.userShellInk));
        // The theme changes: same rows, new colours, nothing rewritten.
        scheme.userAgentBand = QColor(0x75, 0x00, 0xc3); scheme.userAgentInk = QColor(0xf6, 0xee, 0xff);
        scheme.userShellBand = QColor(); scheme.userShellInk = QColor(0x00, 0x49, 0xa9);   // "none": ink only
        t.view->setColorScheme(scheme);
        img = t.grab();
        QCOMPARE(at(img, 0, 40), scheme.userAgentBand);
        QVERIFY(rowHasColor(img, 0, ch, scheme.userAgentInk));
        QVERIFY(at(img, 2, 40) != QColor(0x45, 0xc8, 0xee));
        QVERIFY(rowHasColor(img, 2, ch, scheme.userShellInk));
    }

    // ---- find, with the open folds in the sequence (#TK9C)

    void findFindsTextOnlyAnOpenFoldHas()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        t.anchoredLines();
        const QString uri = QStringLiteral("relay://call/p/1/a");
        // "treasure" is nowhere in the real rows, so the core on its own finds
        // nothing: every one of these matches is the view's.
        t.view->setFoldContent(uri, foldBody({QStringLiteral("hidden treasure"), QStringLiteral("quiet")}));
        QTest::qWait(120);
        QCOMPARE(t.view->find(QStringLiteral("treasure"), true), 1);
        QCOMPARE(t.view->searchMatchCount(), 1);
        QCOMPARE(t.view->searchIndex(), 0);

        const int anchor = t.rowOf(QStringLiteral("* ran python"));
        QVERIFY2(anchor >= 0, qPrintable(t.view->visibleRowsText().join(QLatin1Char('|'))));
        const QImage img = t.grab();
        QVERIFY2(rowHasColor(img, anchor + 1, t.view->cellHeight(), t.view->colorScheme().searchCurrent),
                 "the selected match inside the fold is highlighted like the core's own");

        // Dropping the needle takes the highlight with it.
        t.view->find(QString(), true);
        QCOMPARE(t.view->searchMatchCount(), 0);
        QVERIFY(!rowHasColor(t.grab(), anchor + 1, t.view->cellHeight(), t.view->colorScheme().searchCurrent));
    }

    void findStepsBothSidesOfAFoldAndItsInsideInVisualOrder()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        t.view->setFoldPrefix(QStringLiteral("relay://call/"));
        t.backend->writeToDisplay("alpha mark\r\n");
        for (int i = 0; i < 12; ++i)
            t.backend->writeToDisplay(QByteArray("filler ") + QByteArray::number(i) + "\r\n");
        t.backend->writeToDisplay("\x1b]8;;relay://call/p/1/a\x1b\\* ran python\x1b]8;;\x1b\\\r\n");
        t.backend->writeToDisplay("omega mark\r\n");
        QTest::qWait(120);
        t.view->setFoldContent(QStringLiteral("relay://call/p/1/a"),
                               foldBody({QStringLiteral("inner mark one"), QStringLiteral("quiet"),
                                         QStringLiteral("inner mark two")}));
        QTest::qWait(150);

        // Visual order, oldest first: alpha (real), inner one, inner two (the
        // fold), omega (real). Counted from the newest that is 3, 2, 1, 0.
        QCOMPARE(t.view->find(QStringLiteral("mark"), true), 4);
        QCOMPARE(t.view->searchIndex(), 0);
        for (int want : {1, 2, 3, 0}) { // backwards, wrapping at the oldest
            QCOMPARE(t.view->searchStep(true), 4);
            QCOMPARE(t.view->searchIndex(), want);
        }
        for (int want : {3, 2, 1, 0}) { // and forwards again, wrapping at the newest
            QCOMPARE(t.view->searchStep(false), 4);
            QCOMPARE(t.view->searchIndex(), want);
        }

        // The selected match is on screen and is the only current one: with the
        // walk on a fold match no real row carries the current highlight.
        QCOMPARE(t.view->searchStep(true), 4);
        QCOMPARE(t.view->searchIndex(), 1); // inner mark two
        QTest::qWait(80);
        const QStringList rows = t.view->visibleRowsText();
        const int inner = rows.indexOf(QStringLiteral("   inner mark two"));
        QVERIFY2(inner >= 0, qPrintable(rows.join(QLatin1Char('|'))));
        const QImage img = t.grab();
        const QColor cur = t.view->colorScheme().searchCurrent;
        QVERIFY(rowHasColor(img, inner, t.view->cellHeight(), cur));
        for (int r = 0; r < rows.size(); ++r) {
            if (r != inner)
                QVERIFY2(!rowHasColor(img, r, t.view->cellHeight(), cur), qPrintable(rows.value(r)));
        }
    }

    void aMatchStraddlingAFoldLinesWrapIsFound()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        t.anchoredLines();
        // The grid is 50 columns and the block is indented 3, so a fold line
        // wraps after 47 cells. "wrap" sits across that wrap: cells 44..47.
        const QString line = QString(44, QLatin1Char('x')) + QStringLiteral("wrapped-needle")
            + QString(10, QLatin1Char('y'));
        t.view->setFoldContent(QStringLiteral("relay://call/p/1/a"), foldBody({line}));
        QTest::qWait(120);
        // One match, because a fold's *logical* line is what is searched.
        QCOMPARE(t.view->find(QStringLiteral("wrap"), true), 1);
        QTest::qWait(80);
        const int anchor = t.rowOf(QStringLiteral("* ran python"));
        QVERIFY2(anchor >= 0, qPrintable(t.view->visibleRowsText().join(QLatin1Char('|'))));
        const QImage img = t.grab();
        const QColor cur = t.view->colorScheme().searchCurrent;
        QVERIFY(rowHasColor(img, anchor + 1, t.view->cellHeight(), cur));
        QVERIFY(rowHasColor(img, anchor + 2, t.view->cellHeight(), cur)); // painted across the wrap
    }

    void togglingAFoldChangesTheMatchCount()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        t.anchoredLines();
        const QString uri = QStringLiteral("relay://call/p/1/a");
        t.backend->writeToDisplay("outer needle\r\n");
        QTest::qWait(80);
        t.view->setFoldContent(uri, foldBody({QStringLiteral("inner needle one"),
                                              QStringLiteral("inner needle two")}));
        QTest::qWait(150);
        QCOMPARE(t.view->find(QStringLiteral("needle"), true), 3);

        t.view->setFoldExpanded(uri, false);
        QTest::qWait(80);
        QCOMPARE(t.view->searchMatchCount(), 1); // a shut fold is not searched
        t.view->setFoldExpanded(uri, true);
        QTest::qWait(80);
        QCOMPARE(t.view->searchMatchCount(), 3);
        QCOMPARE(t.view->searchStep(true), 3);
    }

    void aResizeKeepsTheMatchesInsideTheFold()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        t.anchoredLines();
        t.view->setFoldContent(QStringLiteral("relay://call/p/1/a"),
                               foldBody({QStringLiteral("inner needle one"),
                                         QStringLiteral("inner needle two")}));
        QTest::qWait(150);
        QCOMPARE(t.view->find(QStringLiteral("needle"), true), 2);
        // Both cores reflow and the block rewraps: the matches are recomputed
        // on the new rows, and stepping still walks the two of them.
        t.backend->resizeTerminal(14, 34);
        QTest::qWait(250);
        QCOMPARE(t.view->searchMatchCount(), 2);
        QCOMPARE(t.view->searchStep(true), 2);
        QVERIFY(t.view->searchIndex() >= 0);
        QVERIFY(t.view->searchIndex() < 2);
    }

    void aFullScreenProgramSearchesTheRealRowsOnly()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        t.anchoredLines();
        t.view->setFoldContent(QStringLiteral("relay://call/p/1/a"),
                               foldBody({QStringLiteral("inner needle")}));
        QTest::qWait(150);
        QCOMPARE(t.view->find(QStringLiteral("needle"), true), 1);
        t.backend->writeToDisplay("\x1b[?1049h"); // the folds are neither painted nor searched
        QTest::qWait(100);
        QVERIFY(t.backend->altScreen());
        QCOMPARE(t.view->searchMatchCount(), 0);
        t.backend->writeToDisplay("\x1b[?1049l");
        QTest::qWait(100);
        QCOMPARE(t.view->searchMatchCount(), 1);
        QCOMPARE(t.view->searchStep(true), 1);
        QCOMPARE(t.view->searchIndex(), 0);
    }


    // #6W0Z: the pane's directory is a readlink and a stat of /proc/<pid>/cwd
    // and an acquisition of the core's mutex, and restLinkColumns() wanted it
    // for every painted row — 91 % of the GUI thread's statx during output.
    // One frame, however many link-bearing rows it paints, resolves it once.
    void theDirectoryIsResolvedOncePerFrameNotOncePerRow()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        QTemporaryDir dir(QDir::tempPath() + QStringLiteral("/cw-XXXXXX"));
        QVERIFY(dir.isValid());
        const QString file = dir.filePath(QStringLiteral("notes.txt"));
        { QFile f(file); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("x"); }
        t.backend->resizeTerminal(12, 100);
        for (int i = 0; i < 10; ++i)
            t.backend->writeToDisplay(("see " + file + "\r\n").toUtf8());
        QVERIFY(t.waitScreen(QStringLiteral("notes.txt")));
        QVERIFY(t.view->linksColouredAtRest());
        t.grab();   // settle

        // Five more such lines, each of which scrolls the screen and so
        // repaints every row on it.
        const quint64 before = t.view->directoryResolveCount();
        const quint64 rowsBefore = t.view->rowPaintCount();
        const quint64 paintsBefore = t.view->paintCount();
        for (int i = 0; i < 5; ++i) {
            t.backend->writeToDisplay(("see " + file + "\r\n").toUtf8());
            QTest::qWait(60);
        }
        QTest::qWait(150);
        const quint64 frames = t.view->paintCount() - paintsBefore;
        const quint64 rows = t.view->rowPaintCount() - rowsBefore;
        const quint64 resolves = t.view->directoryResolveCount() - before;
        QVERIFY(frames > 0);
        QVERIFY2(rows >= 4 * frames, qPrintable(QStringLiteral("%1 rows over %2 frames").arg(rows).arg(frames)));
        QVERIFY2(resolves <= frames,
                 qPrintable(QStringLiteral("%1 directory resolutions for %2 frames of %3 rows")
                                .arg(resolves).arg(frames).arg(rows)));
        // A repaint that pulls no new frame reuses the frame's answer.
        const quint64 still = t.view->directoryResolveCount();
        t.grab();
        QCOMPARE(t.view->directoryResolveCount(), still);
    }

    // #6W0Z: a fold or a prose block on screen used to turn every frame into a
    // repaint of every content row, because the row a real row is painted on
    // was worked out by repainting all of them. An agent pane always carries
    // anchors, so it never got the dirty-row path at all.
    void aFoldOnScreenStillPaintsOnlyTheRowsThatChanged()
    {
        QFETCH_GLOBAL(QString, core);
        // One character at a time into a pane with content on every row: the
        // rows painted per frame, with the fold layer doing each of its jobs.
        const auto rowsPerFrame = [](Term &t) {
            QTest::qWait(150);
            const quint64 paints = t.view->paintCount(), rows = t.view->rowPaintCount();
            for (int i = 0; i < 8; ++i) {
                t.backend->writeToDisplay("x");
                QTest::qWait(40);
            }
            QTest::qWait(150);
            const quint64 dp = t.view->paintCount() - paints;
            return dp == 0 ? 0.0 : double(t.view->rowPaintCount() - rows) / double(dp);
        };
        Term t(core, QStringLiteral("/bin/cat"));
        t.anchoredLines();
        const QString uri = QStringLiteral("relay://call/p/1/a");
        const double shut = rowsPerFrame(t);
        QVERIFY2(shut <= 2.5, qPrintable(QStringLiteral("shut: %1 rows per frame").arg(shut)));

        // Open: the block's own rows are on screen and the rows below it have
        // moved down, and a one-character write still costs its own row.
        t.view->setFoldContent(uri, foldBody({QStringLiteral("one"), QStringLiteral("two")}));
        QTest::qWait(120);
        QVERIFY(t.view->foldExpanded(uri));
        const double open = rowsPerFrame(t);
        QVERIFY2(open <= 3.0, qPrintable(QStringLiteral("open: %1 rows per frame").arg(open)));
        // ... and the fold is still where it belongs, painted from its own rows.
        QStringList rows = t.view->visibleRowsText();
        const int anchor = rows.indexOf(QStringLiteral("* ran python"));
        QVERIFY2(anchor >= 0, qPrintable(rows.join(QLatin1Char('|'))));
        QCOMPARE(rows.value(anchor + 1).trimmed(), QStringLiteral("one"));
        QCOMPARE(rows.value(anchor + 2).trimmed(), QStringLiteral("two"));

        // A fold opening, shutting or moving still repaints everything: the
        // rows below it are somewhere else afterwards.
        QTest::qWait(150);
        const quint64 rowsBefore = t.view->rowPaintCount();
        t.view->setFoldExpanded(uri, false);
        QTest::qWait(150);
        QVERIFY2(t.view->rowPaintCount() - rowsBefore >= 5,
                 "shutting a fold must repaint the rows it was covering");
        rows = t.view->visibleRowsText();
        QCOMPARE(rows.value(rows.indexOf(QStringLiteral("* ran python")) + 1), QStringLiteral("after"));
    }

    // The same for a prose block (#R2WQ) that has taken its rows over, which is
    // what an agent pane looks like at any width but the one it printed at.
    void aTakenOverProseBlockPaintsOnlyTheRowsThatChanged()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        t.view->setFoldPrefix(QStringLiteral("relay://call/"));
        for (int i = 0; i < 6; ++i)
            t.backend->writeToDisplay(QByteArray("filler ") + QByteArray::number(i) + "\r\n");
        t.backend->writeToDisplay("\x1b]8;;relay://prose/p/1\x1b\\hello prose\x1b]8;;\x1b\\\r\n");
        QVERIFY(t.waitScreen(QStringLiteral("hello prose")));
        t.view->setProseBlock(QStringLiteral("relay://prose/p/1"), foldBody({QStringLiteral("hello prose")}),
                              t.view->columns());
        QTest::qWait(120);
        // Narrower than it was printed at: the block is re-wrapped by the layer.
        t.backend->resizeTerminal(12, 40);
        QTest::qWait(200);
        QVERIFY(t.view->visibleRowsText().join(QLatin1Char('|')).contains(QStringLiteral("hello prose")));

        const quint64 paints = t.view->paintCount(), rows = t.view->rowPaintCount();
        for (int i = 0; i < 8; ++i) {
            t.backend->writeToDisplay("x");
            QTest::qWait(40);
        }
        QTest::qWait(150);
        const quint64 dp = t.view->paintCount() - paints;
        QVERIFY(dp > 0);
        const double perFrame = double(t.view->rowPaintCount() - rows) / double(dp);
        QVERIFY2(perFrame <= 2.5, qPrintable(QStringLiteral("%1 rows per frame").arg(perFrame)));
        QVERIFY(t.view->visibleRowsText().join(QLatin1Char('|')).contains(QStringLiteral("hello prose")));
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

    // #R2WQ: a reply printed at one pane width still reads as wrapped prose
    // after the pane is made narrower and wider again — no row ends mid-word,
    // no row is stranded at the old width, bullets still hang under their
    // text. The block is what the pane prints: pre-wrapped by the streaming
    // wrapper at 80, wrapped in an OSC 8 prose run, its logical lines handed
    // to the engine beside the bytes.
    void proseReflowsOnResize()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        t.backend->resizeTerminal(20, 80);
        t.view->setFoldPrefix(QStringLiteral("relay://call/"));
        for (int i = 0; i < 3; ++i)
            t.backend->writeToDisplay(QByteArray("filler ") + QByteArray::number(i) + "\r\n");
        QTest::qWait(80);

        const QString para = QStringLiteral(
            "This reply carries a stretch of inline code, enough words to wrap at eighty columns, "
            "and a long word like RELAY_ENGINE_WITH_GHOSTTY that not every width can keep whole.");
        const QString bullet = QStringLiteral(
            "- a bullet whose words run past the edge of the pane at both of the widths tried here");
        const QString rendered = QStringLiteral("\x1b[1m") + para.left(9)
            + QStringLiteral("\x1b[0m") + para.mid(9) + QStringLiteral("\n")
            + bullet + QStringLiteral("\n");
        relay::WordWrap w;
        w.setColumns(80);
        QByteArray bytes = QByteArrayLiteral("\x1b]8;;relay://prose/t/1\x1b\\");
        bytes += QString(w.feed(rendered) + w.flush()).replace(QLatin1Char('\n'), QStringLiteral("\r\n")).toUtf8();
        bytes += QByteArrayLiteral("\x1b]8;;\x1b\\");
        t.backend->writeToDisplay(bytes);
        // The block's logical lines, as the pane hands them over: the same
        // text before the wrapper, the bold run kept as a span.
        QVector<FoldLine> lines;
        {
            FoldLine first;
            FoldSpan bold;
            bold.text = para.left(9);
            bold.bold = true;
            bold.sgr = QStringLiteral("1");
            FoldSpan rest;
            rest.text = para.mid(9);
            first.spans << bold << rest;
            lines << first << proseLines({bullet});
        }
        t.backend->setProseBlock(QStringLiteral("relay://prose/t/1"), lines, 80);

        // What the wrapper would have printed at a width, right-trimmed the way
        // the grid trims its rows.
        const QString plain = para + QLatin1Char('\n') + bullet + QLatin1Char('\n');
        auto expectedAt = [&plain](int columns) {
            QStringList rows = proseRows(plain, columns);
            for (QString &row : rows)
                while (row.endsWith(QLatin1Char(' ')))
                    row.chop(1);
            return rows;
        };
        auto blockRows = [&t](int count) {
            QStringList rows = t.view->visibleRowsText().mid(3, count);
            for (QString &row : rows)
                while (row.endsWith(QLatin1Char(' ')))
                    row.chop(1);
            return rows;
        };
        auto waitRows = [&blockRows, &expectedAt](int columns) {
            const QStringList want = expectedAt(columns);
            QElapsedTimer since;
            since.start();
            while (since.elapsed() < 4000) {
                if (blockRows(want.size()) == want)
                    return true;
                QTest::qWait(50);
            }
            return blockRows(want.size()) == want;
        };

        // At the print width the layer stands aside: the printed rows show.
        QVERIFY2(waitRows(80), "rows at 80 are the wrapper's own");
        // Narrower: the block re-wraps between words, bullets hang.
        t.backend->resizeTerminal(20, 48);
        QVERIFY2(waitRows(48), "rows at 48 are what the wrapper would print at 48");
        const QStringList narrow = expectedAt(48);
        for (const QString &row : narrow.mid(2)) {
            if (row.startsWith(QLatin1String("  ")))
                QVERIFY2(row.startsWith(QLatin1String("  ")) && !row.trimmed().startsWith(QLatin1Char(' ')),
                         "bullet rows hang past the marker, not spaces in the cells");
        }
        // Wider than printed: the block re-flows to the wider measure.
        t.backend->resizeTerminal(20, 96);
        QVERIFY2(waitRows(96), "rows at 96 are what the wrapper would print at 96");
        // And back: byte-identical to what was printed.
        t.backend->resizeTerminal(20, 80);
        QVERIFY2(waitRows(80), "rows return to the printed ones at 80");
        // The prose run is not a link: the URI never shows, and the row's text
        // is not clickable.
        QVERIFY(t.view->visibleRowsText().filter(QStringLiteral("relay://prose")).isEmpty());
        QVERIFY(t.view->linkAtPoint(QPoint(2 + 10 * t.view->cellWidth(), 2 + 3 * t.view->cellHeight())).target.isEmpty());
    }

    // ---- a markdown link's label opens what it names (card #MDKN) --------------------------
    //
    // `[LABEL](target)` is painted in the link ink, so a person clicks the label and not the
    // `(target)` printed beside it. The label's cells carry an OSC 8 run of their own — the
    // block's anchor with the target as a `#l=` fragment — and the hit test resolves that
    // fragment through relay::links, so a label and the target beside it answer identically.
    //
    // The block is built the way the pane builds one: MarkdownAnsi with the anchor set, the
    // rendered text through the streaming wrapper into the grid, the same text before the
    // wrapper handed over as logical lines.
    void markdownLinkLabelsAreClickable()
    {
        QFETCH_GLOBAL(QString, core);
        QTemporaryDir dir;
        { QFile f(dir.filePath(QStringLiteral("notes.txt"))); QVERIFY(f.open(QIODevice::WriteOnly)); }
        Term t(core, QStringLiteral("/bin/cat"), {}, dir.path());
        t.backend->resizeTerminal(14, 100);
        t.view->setCardLookup([](const QString &id, QString *title) {
            if (id != QStringLiteral("K7Q2"))
                return false;
            if (title)
                *title = QStringLiteral("A card");
            return true;
        });
        t.backend->writeToDisplay("\x1b]7;file://" + QUrl::toPercentEncoding(dir.path(), "/") + "\x07");

        const QString anchor = QStringLiteral("relay://prose/t/5");
        const QString markdown = QStringLiteral(
            "Open [the theme row](option:general/theme) or [that talk](session:0f3a91cc).\n"
            "Also [card K7Q2](#K7Q2), [the notes](notes.txt:12) and [the site](https://x.org/a).\n");
        MarkdownAnsi md;
        md.setLinkAnchor(anchor);
        const QString rendered = md.feed(markdown) + md.finish();
        ProseCollector collector;
        collector.feed(rendered);
        const QVector<FoldLine> lines = collector.take();
        WordWrap wrap;
        wrap.setColumns(100);
        QByteArray bytes = QStringLiteral("\x1b]8;;%1\x1b\\").arg(anchor).toUtf8();
        bytes += QString(wrap.feed(rendered) + wrap.flush()).replace(QLatin1Char('\n'), QStringLiteral("\r\n")).toUtf8();
        bytes += QByteArrayLiteral("\x1b]8;;\x1b\\");
        t.backend->writeToDisplay(bytes);
        QVERIFY(t.waitScreen(QStringLiteral("the site")));
        t.backend->setProseBlock(anchor, lines, 100);
        QTest::qWait(80);

        const QString notes = QFileInfo(dir.filePath(QStringLiteral("notes.txt"))).absoluteFilePath();
        // The label, wherever the rows put it: the first row whose text holds it, and the column
        // that text starts at. The `(target)` printed after it is found the same way.
        auto hitOn = [&t](const QString &needle) {
            const QStringList rows = t.view->visibleRowsText();
            for (int r = 0; r < rows.size(); ++r) {
                const int col = rows.at(r).indexOf(needle);
                if (col < 0)
                    continue;
                return t.view->linkAtPoint(QPoint(2 + (col + needle.size() / 2) * t.view->cellWidth(),
                                                  2 + r * t.view->cellHeight() + t.view->cellHeight() / 2));
            }
            return TerminalView::Link();
        };
        auto checkAll = [&](const char *where) {
            const TerminalView::Link option = hitOn(QStringLiteral("the theme row"));
            QVERIFY2(option.target == QStringLiteral("relay://option/general/theme"),
                     qPrintable(QStringLiteral("%1: option label -> '%2'").arg(QLatin1String(where), option.target)));
            const TerminalView::Link session = hitOn(QStringLiteral("that talk"));
            QVERIFY2(session.target == QStringLiteral("relay://session/0f3a91cc"),
                     qPrintable(QStringLiteral("%1: session label -> '%2'").arg(QLatin1String(where), session.target)));
            const TerminalView::Link card = hitOn(QStringLiteral("card K7Q2"));
            QVERIFY2(card.target == QStringLiteral("relay://card/K7Q2"),
                     qPrintable(QStringLiteral("%1: card label -> '%2'").arg(QLatin1String(where), card.target)));
            // A card label offers what a card reference offers: the id and the board's title,
            // which is what the context menu is built from.
            QCOMPARE(card.card, QStringLiteral("K7Q2"));
            QCOMPARE(card.cardTitle, QStringLiteral("A card"));
            const TerminalView::Link path = hitOn(QStringLiteral("the notes"));
            QVERIFY2(path.target == notes,
                     qPrintable(QStringLiteral("%1: path label -> '%2'").arg(QLatin1String(where), path.target)));
            QCOMPARE(path.line, 12);
            const TerminalView::Link url = hitOn(QStringLiteral("the site"));
            QVERIFY2(url.target == QStringLiteral("https://x.org/a"),
                     qPrintable(QStringLiteral("%1: url label -> '%2'").arg(QLatin1String(where), url.target)));
            QVERIFY(url.url);
        };
        checkAll("at the print width");

        // The printed target is still text and still opens the same thing, which is what a
        // restored transcript — where OSC 8 is stripped — is left with.
        QCOMPARE(hitOn(QStringLiteral("(option:general/theme)")).target,
                 QStringLiteral("relay://option/general/theme"));
        // Prose is still prose: the anchor itself never shows and never opens anything.
        QVERIFY(t.view->visibleRowsText().filter(QStringLiteral("relay://prose")).isEmpty());
        QVERIFY(hitOn(QStringLiteral("Open")).target.isEmpty());

        // A click on a label opens it, by the same signal every other link travels on.
        {
            const QStringList rows = t.view->visibleRowsText();
            int row = -1, col = -1;
            for (int r = 0; r < rows.size() && row < 0; ++r) {
                col = rows.at(r).indexOf(QStringLiteral("that talk"));
                if (col >= 0)
                    row = r;
            }
            QVERIFY(row >= 0);
            QTest::mouseClick(t.view, Qt::LeftButton, Qt::NoModifier,
                              QPoint(2 + (col + 2) * t.view->cellWidth(),
                                     2 + row * t.view->cellHeight() + t.view->cellHeight() / 2));
            QCOMPARE(t.links.size(), 1);
            QCOMPARE(t.links.first(), QStringLiteral("relay://session/0f3a91cc"));
        }

        // Re-wrapped: away from the print width the view paints its own wrap of the logical
        // lines, so the label's target has to travel in the span, not in a cell.
        t.backend->resizeTerminal(14, 62);
        QTest::qWait(120);
        QVERIFY2(t.view->visibleRowsText().join(QLatin1Char('|')).contains(QStringLiteral("the theme row")),
                 "the block did not re-wrap into view");
        checkAll("re-wrapped at 62");
        t.backend->resizeTerminal(14, 100);
        QTest::qWait(120);
        checkAll("back at the print width");

        // Copying a label copies the label. The run is an escape sequence, not cells, so the
        // selection is the text on the screen — never the URI behind it, and never a `\x1b]8`.
        {
            const QStringList rows = t.view->visibleRowsText();
            int row = -1, col = -1;
            for (int r = 0; r < rows.size() && row < 0; ++r) {
                col = rows.at(r).indexOf(QStringLiteral("the theme row"));
                if (col >= 0)
                    row = r;
            }
            QVERIFY(row >= 0);
            const int cw = t.view->cellWidth(), ch = t.view->cellHeight();
            const QPoint from(2 + col * cw + cw / 2, 2 + row * ch + ch / 2);
            const QPoint to(2 + (col + 13) * cw + cw / 2, 2 + row * ch + ch / 2);
            QTest::mousePress(t.view, Qt::LeftButton, Qt::NoModifier, from);
            QMouseEvent move(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(t.view, &move);
            QTest::mouseRelease(t.view, Qt::LeftButton, Qt::NoModifier, to);
            QCOMPARE(t.backend->selectedText(), QStringLiteral("the theme row"));
            t.backend->selectAll();
            const QString all = t.backend->selectedText();
            QVERIFY2(!all.contains(QStringLiteral("#l=")), "the label's URI is in the copied text");
            QVERIFY2(!all.contains(QChar(0x1b)), "an escape is in the copied text");
            QVERIFY(all.contains(QStringLiteral("the theme row (option:general/theme)")));
        }
    }

    // A label can be the only thing on the block's first grid row, which cuts the block's OSC 8
    // run into pieces that do not start where the block does. The view merges the pieces by the
    // anchor they share, or the layer hides the wrong rows on a resize and the row is painted
    // twice (#MDKN).
    void aBlockThatOpensWithALabelStillKnowsItsFirstRow()
    {
        QFETCH_GLOBAL(QString, core);
        Term t(core, QStringLiteral("/bin/cat"));
        t.backend->resizeTerminal(14, 60);
        const QString anchor = QStringLiteral("relay://prose/t/6");
        // A label long enough that the `(target)` printed after it is pushed onto the next row:
        // the block's first grid row is then nothing but the label, so the run of cells carrying
        // the block's own anchor does not begin until row 1.
        const QString markdown = QStringLiteral(
            "[Open the terminal theme row in Options now](option:general/theme)\n"
            "A second line, long enough to wrap when the pane narrows a lot.\n");
        MarkdownAnsi md;
        md.setLinkAnchor(anchor);
        const QString rendered = md.feed(markdown) + md.finish();
        ProseCollector collector;
        collector.feed(rendered);
        const QVector<FoldLine> lines = collector.take();
        WordWrap wrap;
        wrap.setColumns(60);
        QByteArray bytes = QStringLiteral("\x1b]8;;%1\x1b\\").arg(anchor).toUtf8();
        bytes += QString(wrap.feed(rendered) + wrap.flush()).replace(QLatin1Char('\n'), QStringLiteral("\r\n")).toUtf8();
        bytes += QByteArrayLiteral("\x1b]8;;\x1b\\");
        t.backend->writeToDisplay(bytes);
        QVERIFY(t.waitScreen(QStringLiteral("second line")));
        t.backend->setProseBlock(anchor, lines, 60);
        QTest::qWait(80);
        auto rowsHolding = [](const QStringList &rows, const QString &needle) {
            int n = 0;
            for (const QString &r : rows)
                if (r.contains(needle))
                    ++n;
            return n;
        };
        QCOMPARE(rowsHolding(t.view->visibleRowsText(), QStringLiteral("theme row")), 1);
        // The premise: the label really is alone on the block's first row.
        const QStringList printed = t.view->visibleRowsText();
        for (const QString &r : printed) {
            if (!r.contains(QStringLiteral("theme row")))
                continue;
            QVERIFY2(!r.contains(QStringLiteral("(option:")),
                     qPrintable(QStringLiteral("the printed target did not wrap away: '%1'").arg(r)));
            break;
        }
        // Narrow: the layer takes the block's rows over. If its anchor started at the second
        // row, the first would be painted by the grid *and* by the layer.
        t.backend->resizeTerminal(14, 34);
        QTest::qWait(150);
        const QStringList narrow = t.view->visibleRowsText();
        QCOMPARE(rowsHolding(narrow, QStringLiteral("theme row")), 1);
        QVERIFY2(narrow.join(QLatin1Char('|')).contains(QStringLiteral("Open the terminal")),
                 qPrintable(narrow.join(QLatin1Char('|'))));
        // And the label is still the link it was, on the row the layer painted it on.
        int row = -1, col = -1;
        for (int r = 0; r < narrow.size() && row < 0; ++r) {
            col = narrow.at(r).indexOf(QStringLiteral("theme row"));
            if (col >= 0)
                row = r;
        }
        QVERIFY(row >= 0);
        QCOMPARE(t.view->linkAtPoint(QPoint(2 + (col + 3) * t.view->cellWidth(),
                                            2 + row * t.view->cellHeight() + t.view->cellHeight() / 2)).target,
                 QStringLiteral("relay://option/general/theme"));
    }
};

QObject *makeViewTest()
{
    return new ViewTest;
}

#include "ViewTest.moc"
