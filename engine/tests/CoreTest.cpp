// SPDX-License-Identifier: GPL-3.0-or-later
// Screen-model tests: feed byte sequences into every available VtCore and
// assert cells, text, modes and events.
#include "core/VtCore.h"

#include <QtTest>

using namespace relay;

namespace {

struct Harness {
    std::unique_ptr<VtCore> vt;
    QByteArray replies;
    QStringList titles;
    QList<QPair<QString, QString>> cwds;
    QList<bool> alt;
    QList<std::tuple<PromptMark, int, int>> marks;
    int bells = 0;
    QList<QPair<QString, QByteArray>> clips;
    QList<QPair<QString, QString>> notes;

    explicit Harness(const QString &core, int rows = 5, int cols = 20)
    {
        vt = createVtCore(core, rows, cols);
        vt->resize(rows, cols, 8, 16);
        vt->events.reply = [this](const char *d, size_t n) { replies.append(d, int(n)); };
        vt->events.titleChanged = [this](const QString &t) { titles << t; };
        vt->events.cwdChanged = [this](const QString &p, const QString &h) { cwds << qMakePair(p, h); };
        vt->events.altScreenChanged = [this](bool a) { alt << a; };
        vt->events.promptMark = [this](PromptMark k, int row, int code) { marks << std::make_tuple(k, row, code); };
        vt->events.bell = [this] { ++bells; };
        vt->events.clipboardWrite = [this](const QString &t, const QByteArray &d) { clips << qMakePair(t, d); };
        vt->events.notification = [this](const QString &t, const QString &b) { notes << qMakePair(t, b); };
    }
    void feed(const QByteArray &b) { vt->feed(b.constData(), size_t(b.size())); }
    QString row(int r) const { return vt->screenText().split(QLatin1Char('\n')).value(r); }
    ViewportFrame frame()
    {
        ViewportFrame f;
        vt->updateFrame(&f, true);
        return f;
    }
};

} // namespace

class CoreTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase_data()
    {
        QTest::addColumn<QString>("core");
        for (const QString &c : availableVtCores())
            QTest::newRow(qPrintable(c)) << c;
    }

    void plainTextAndCursor()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        h.feed("hello\r\nworld");
        QCOMPARE(h.row(0), QStringLiteral("hello"));
        QCOMPARE(h.row(1), QStringLiteral("world"));
        const CursorState c = h.vt->activeCursor();
        QCOMPARE(c.row, 1);
        QCOMPARE(c.col, 5);
    }

    void sgrAttributesAndColours()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        h.vt->setColors(0xd8d8d8, 0x1c1e24, nullptr);
        h.feed("\x1b[1;4;31mA\x1b[0m\x1b[38;2;1;2;3;48;2;4;5;6mB\x1b[0mC\x1b[7mD");
        ViewportFrame f = h.frame();
        const Line &l = f.lines[0];
        QVERIFY(l.cells[0].attrs & AttrBold);
        QVERIFY(l.cells[0].attrs & AttrUnderline);
        QCOMPARE(CellColor::kind(l.cells[0].fg), CellColor::Rgb);
        QCOMPARE(l.cells[1].fg, CellColor::rgb(1, 2, 3));
        QCOMPARE(l.cells[1].bg, CellColor::rgb(4, 5, 6));
        QCOMPARE(CellColor::kind(l.cells[2].fg), CellColor::Default);
        QCOMPARE(CellColor::kind(l.cells[2].bg), CellColor::Default);
        QVERIFY(l.cells[3].attrs & AttrReverse);
    }

    void wideAndCombiningCharacters()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        h.feed(QStringLiteral("a漢b éx").toUtf8());
        ViewportFrame f = h.frame();
        const Line &l = f.lines[0];
        QCOMPARE(l.cells[1].width, uint8_t(2));
        QCOMPARE(l.cells[2].ch, kWideTail);
        QCOMPARE(l.cells[3].ch, char32_t('b'));
        QCOMPARE(l.cellText(l.cells[5]), QStringLiteral("é"));
        QCOMPARE(l.cells[6].ch, char32_t('x'));
        QCOMPARE(h.row(0), QStringLiteral("a漢b éx"));
    }

    void emojiClusters()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        // thumbs up + skin tone, then a flag (regional indicators J P), then x
        h.feed(QStringLiteral("\U0001F44D\U0001F3FD\U0001F1EF\U0001F1F5x").toUtf8());
        ViewportFrame f = h.frame();
        const Line &l = f.lines[0];
        QCOMPARE(l.cells[0].width, uint8_t(2));
        QCOMPARE(l.cellText(l.cells[0]), QStringLiteral("\U0001F44D\U0001F3FD"));
        QCOMPARE(l.cells[1].ch, kWideTail);
        QCOMPARE(l.cellText(l.cells[2]), QStringLiteral("\U0001F1EF\U0001F1F5"));
        QCOMPARE(l.cells[4].ch, char32_t('x'));
    }

    void titleBellNotification()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        h.feed("\x1b]2;my title\x07\x07\x1b]777;notify;Build;done\x1b\\");
        QCOMPARE(h.titles.value(0), QStringLiteral("my title"));
        QCOMPARE(h.vt->title(), QStringLiteral("my title"));
        QCOMPARE(h.bells, 1);
        QCOMPARE(h.notes.size(), 1);
        QCOMPARE(h.notes[0].first, QStringLiteral("Build"));
        QCOMPARE(h.notes[0].second, QStringLiteral("done"));
    }

    void osc7WorkingDirectory()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        h.feed("\x1b]7;file://myhost/tmp/some%20dir\x07");
        QCOMPARE(h.cwds.size(), 1);
        QCOMPARE(h.cwds[0].first, QStringLiteral("/tmp/some dir"));
        QCOMPARE(h.cwds[0].second, QStringLiteral("myhost"));
    }

    void osc8Hyperlinks()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        h.feed("ab\x1b]8;;https://example.com/x\x1b\\link\x1b]8;;\x1b\\cd");
        QCOMPARE(h.row(0), QStringLiteral("ablinkcd"));
        QCOMPARE(h.vt->hyperlinkAt(0, 2), QStringLiteral("https://example.com/x"));
        QCOMPARE(h.vt->hyperlinkAt(0, 5), QStringLiteral("https://example.com/x"));
        QCOMPARE(h.vt->hyperlinkAt(0, 1), QString());
        QCOMPARE(h.vt->hyperlinkAt(0, 6), QString());
        ViewportFrame f = h.frame();
        QVERIFY(f.lines[0].cells[3].link != 0);
        QCOMPARE(f.lines[0].cells[7].link, uint32_t(0));
    }

    void osc133PromptMarks()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        h.feed("\x1b]133;A\x07$ \x1b]133;B\x07ls\r\n\x1b]133;C\x07out\r\n\x1b]133;D;3\x07\x1b]133;A\x07$ ");
        QCOMPARE(h.marks.size(), 5);
        QCOMPARE(std::get<0>(h.marks[0]), MarkPromptStart);
        QCOMPARE(std::get<1>(h.marks[0]), 0);
        QCOMPARE(std::get<0>(h.marks[1]), MarkCommandStart);
        QCOMPARE(std::get<0>(h.marks[2]), MarkOutputStart);
        QCOMPARE(std::get<1>(h.marks[2]), 1);
        QCOMPARE(std::get<0>(h.marks[3]), MarkCommandFinished);
        QCOMPARE(std::get<2>(h.marks[3]), 3);
        QCOMPARE(std::get<1>(h.marks[4]), 2);
        // Split across feed() calls.
        Harness s(core);
        s.feed("\x1b]13");
        s.feed("3;D;");
        s.feed("42\x1b");
        s.feed("\\");
        QCOMPARE(s.marks.size(), 1);
        QCOMPARE(std::get<2>(s.marks[0]), 42);
        ViewportFrame f = h.frame();
        QVERIFY(f.lines[0].marks & MarkPromptStart);
    }

    void promptJump()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 4, 20);
        h.feed("\x1b]133;A\x07$ one\r\n");
        for (int i = 0; i < 10; ++i)
            h.feed("output\r\n");
        h.feed("\x1b]133;A\x07$ two\r\n");
        for (int i = 0; i < 10; ++i)
            h.feed("more\r\n");
        QVERIFY(h.vt->scrollToPrompt(-1));
        ViewportFrame f = h.frame();
        QCOMPARE(f.lines[0].text(), QStringLiteral("$ two"));
        QVERIFY(h.vt->scrollToPrompt(-1));
        f = h.frame();
        QCOMPARE(f.lines[0].text(), QStringLiteral("$ one"));
    }

    void altScreen()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        h.feed("shell");
        h.feed("\x1b[?1049h\x1b[Hfull screen");
        QVERIFY(h.vt->altScreen());
        QCOMPARE(h.alt, QList<bool>{true});
        QCOMPARE(h.row(0), QStringLiteral("full screen"));
        h.feed("\x1b[?1049l");
        QVERIFY(!h.vt->altScreen());
        QCOMPARE(h.alt, (QList<bool>{true, false}));
        QCOMPARE(h.row(0), QStringLiteral("shell"));
        // Enter and leave within one chunk still reports both transitions.
        Harness quick(core);
        quick.feed("\x1b[?1049hfull\x1b[?1049l");
        QCOMPARE(quick.alt, (QList<bool>{true, false}));
    }

    void scrollbackAndViewport()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 3, 10);
        for (int i = 1; i <= 20; ++i)
            h.feed(QByteArray::number(i) + (i < 20 ? "\r\n" : ""));
        QCOMPARE(h.vt->historyRows(), 17);
        QCOMPARE(h.vt->historyText(3), (QStringList{QStringLiteral("15"), QStringLiteral("16"), QStringLiteral("17")}));
        QCOMPARE(h.row(0), QStringLiteral("18"));
        QVERIFY(h.vt->viewportAtBottom());
        h.vt->scrollViewport(-5);
        QVERIFY(!h.vt->viewportAtBottom());
        ViewportFrame f = h.frame();
        QCOMPARE(f.lines[0].text(), QStringLiteral("13"));
        QCOMPARE(f.viewportTop, 12);
        QCOMPARE(f.historyRows, 17);
        // New output keeps the scrolled viewport on the same content.
        h.feed("\r\n21\r\n22");
        f = h.frame();
        QCOMPARE(f.lines[0].text(), QStringLiteral("13"));
        h.vt->scrollViewportToBottom();
        f = h.frame();
        QCOMPARE(f.lines[2].text(), QStringLiteral("22"));
        h.vt->clearScrollback();
        QCOMPARE(h.vt->historyRows(), 0);
    }

    void reflowOnResize()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 4, 10);
        h.feed("0123456789abcdefghij\r\n");      // wraps into two rows
        for (int i = 0; i < 6; ++i)
            h.feed("line\r\n");                  // push the wrapped line into scrollback
        h.feed("end");
        h.vt->resize(4, 20, 8, 16);
        QStringList all = h.vt->historyText(100);
        all += h.vt->screenText().split(QLatin1Char('\n'));
        QVERIFY2(all.contains(QStringLiteral("0123456789abcdefghij")), qPrintable(all.join(QLatin1Char('|'))));
        h.vt->resize(4, 5, 8, 16);
        all = h.vt->historyText(100);
        all += h.vt->screenText().split(QLatin1Char('\n'));
        QVERIFY2(all.contains(QStringLiteral("01234")) && all.contains(QStringLiteral("fghij")), qPrintable(all.join(QLatin1Char('|'))));
    }

    // A rows-only resize (the reasoning panel opening under the terminal) must leave the cursor
    // where it was when the row above it is exactly full. libvterm's reflow moved it to that
    // row's last column, so the next text erased that column and overwrote the line below.
    void rowsOnlyResizeKeepsCursorBelowAFullRow()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 8, 10);
        h.feed("prompt\r\n0123456789\r\nabcd");
        h.vt->resize(5, 10, 8, 16);
        h.vt->resize(8, 10, 8, 16);
        h.feed("ef\r\nnext");
        QCOMPARE(h.row(1), QStringLiteral("0123456789"));
        QCOMPARE(h.row(2), QStringLiteral("abcdef"));
        QCOMPARE(h.row(3), QStringLiteral("next"));
    }

    void selectionText()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 4, 20);
        h.feed("hello world foo\r\nsecond line");
        h.vt->selectionBegin(0, 6, SelectionUnit::Cell, false);
        h.vt->selectionExtend(1, 5);
        QVERIFY(h.vt->hasSelection());
        QCOMPARE(h.vt->selectedText(), QStringLiteral("world foo\nsecond"));
        ViewportFrame f = h.frame();
        QCOMPARE(int(f.lines[0].selectionStart), 6);
        QCOMPARE(int(f.lines[1].selectionEnd), 5);
        h.vt->selectionBegin(0, 7, SelectionUnit::Word, false);
        QCOMPARE(h.vt->selectedText(), QStringLiteral("world"));
        // Double-click drag extends by whole words in both directions.
        h.vt->selectionExtend(0, 13);
        QCOMPARE(h.vt->selectedText(), QStringLiteral("world foo"));
        h.vt->selectionExtend(0, 1);
        QCOMPARE(h.vt->selectedText(), QStringLiteral("hello world"));
        h.vt->selectionBegin(1, 2, SelectionUnit::Line, false);
        QCOMPARE(h.vt->selectedText(), QStringLiteral("second line"));
        h.vt->selectionClear();
        QVERIFY(!h.vt->hasSelection());
    }

    void searchScrollback()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 3, 20);
        for (int i = 0; i < 30; ++i)
            h.feed(i % 10 == 0 ? "needle here\r\n" : "hay\r\n");
        QCOMPARE(h.vt->searchSet(QStringLiteral("needle")), 3);
        const int idx = h.vt->searchStep(true);
        QVERIFY(idx >= 0);
        ViewportFrame f = h.frame();
        bool highlighted = false;
        for (const Line &l : f.lines)
            highlighted |= !l.highlights.empty();
        QVERIFY(highlighted);
        QCOMPARE(h.vt->searchSet(QString()), 0);
    }

    void deviceAttributesReply()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        h.feed("\x1b[c");
        QVERIFY2(h.replies.startsWith("\x1b[?"), h.replies.toHex().constData());
        h.replies.clear();
        h.feed("\x1b[6n");
        QCOMPARE(h.replies, QByteArray("\x1b[1;1R"));
    }

    void keyEncoding()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        auto key = [&](Key k, uint8_t mods = ModNone) {
            h.replies.clear();
            KeyInput in;
            in.key = k;
            in.modifiers = mods;
            h.vt->sendKey(in);
            return h.replies;
        };
        auto text = [&](char32_t cp, const QString &t, uint8_t mods = ModNone) {
            h.replies.clear();
            KeyInput in;
            in.codepoint = cp;
            in.text = t;
            in.modifiers = mods;
            h.vt->sendKey(in);
            return h.replies;
        };
        QCOMPARE(key(Key::Up), QByteArray("\x1b[A"));
        QCOMPARE(key(Key::Enter), QByteArray("\r"));
        QCOMPARE(key(Key::Backspace), QByteArray("\x7f"));
        QCOMPARE(key(Key::F5), QByteArray("\x1b[15~"));
        QCOMPARE(key(Key::Right, ModCtrl), QByteArray("\x1b[1;5C"));
        QCOMPARE(text('c', QStringLiteral("\x03"), ModCtrl), QByteArray("\x03"));
        QCOMPARE(text('x', QStringLiteral("x"), ModAlt), QByteArray("\x1bx"));
        QCOMPARE(text('a', QStringLiteral("A"), ModShift), QByteArray("A"));
        QCOMPARE(text(0x00e9, QStringLiteral("é")), QStringLiteral("é").toUtf8());
        h.feed("\x1b[?1h"); // application cursor keys
        QCOMPARE(key(Key::Up), QByteArray("\x1bOA"));
        // Key releases produce nothing unless a program asked for them (kitty protocol).
        h.replies.clear();
        KeyInput release;
        release.key = Key::Up;
        release.release = true;
        h.vt->sendKey(release);
        QVERIFY(h.replies.isEmpty());
    }

    void pasteAndFocus()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        h.vt->paste(QStringLiteral("a\nb"));
        QCOMPARE(h.replies, QByteArray("a\rb"));
        h.replies.clear();
        h.feed("\x1b[?2004h");
        QVERIFY(h.vt->bracketedPaste());
        h.vt->paste(QStringLiteral("x"));
        QCOMPARE(h.replies, QByteArray("\x1b[200~x\x1b[201~"));
        // A paste cannot end the bracket early or smuggle control codes.
        h.replies.clear();
        h.vt->paste(QStringLiteral("a\x1b[20\x1b[201~1~b\x03"));
        QCOMPARE(h.replies.count("\x1b[201~"), 1);
        QVERIFY(h.replies.endsWith("\x1b[201~"));
        QVERIFY(!h.replies.contains('\x03'));
        h.replies.clear();
        h.vt->focusChanged(true);
        QVERIFY(h.replies.isEmpty());
        h.feed("\x1b[?1004h");
        h.vt->focusChanged(false);
        QCOMPARE(h.replies, QByteArray("\x1b[O"));
    }

    void mouseReporting()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        QCOMPARE(h.vt->mouseTracking(), MouseTracking::None);
        h.feed("\x1b[?1000h\x1b[?1006h");
        QCOMPARE(h.vt->mouseTracking(), MouseTracking::Click);
        MouseInput m;
        m.action = MouseInput::Action::Press;
        m.button = MouseButton::Left;
        m.row = 2;
        m.col = 3;
        h.vt->sendMouse(m);
        QCOMPARE(h.replies, QByteArray("\x1b[<0;4;3M"));
    }

    void osc52ClipboardIsOptIn()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        h.feed("\x1b]52;c;aGVsbG8=\x07");
        QVERIFY(h.clips.isEmpty());
        h.vt->setClipboardWriteAllowed(true);
        h.feed("\x1b]52;c;aGVsbG8=\x07");
        QCOMPARE(h.clips.size(), 1);
        QCOMPARE(h.clips[0].second, QByteArray("hello"));
    }

    void cursorShape()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        h.feed("\x1b[6 q");
        ViewportFrame f = h.frame();
        QCOMPARE(int(f.cursor.shape), int(CursorShape::Bar));
        h.feed("\x1b[4 q");
        f = h.frame();
        QCOMPARE(int(f.cursor.shape), int(CursorShape::Underline));
        h.feed("\x1b[?25l");
        f = h.frame();
        QVERIFY(!f.cursor.visible);
    }

    void dirtyTracking()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 5, 20);
        ViewportFrame f;
        QVERIFY(h.vt->updateFrame(&f, false));
        QVERIFY(!h.vt->updateFrame(&f, false));
        h.feed("\x1b[3;1Hx");
        QVERIFY(h.vt->updateFrame(&f, false));
        QVERIFY(f.dirty[2]);
        QCOMPARE(f.lines[2].text(), QStringLiteral("x"));
    }
};

QObject *makeCoreTest()
{
    return new CoreTest;
}

#include "CoreTest.moc"
