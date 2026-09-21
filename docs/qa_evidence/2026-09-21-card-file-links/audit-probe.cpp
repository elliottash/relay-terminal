// SPDX-License-Identifier: AGPL-3.0-or-later
// TerminalView / VTermBackend tests on the offscreen platform: rendering,
// keyboard, mouse selection, links, IME, scrolling, accessibility, key mapping.
#include "backend/VTermBackend.h"
#include "session/TerminalSession.h"
#include "view/FaintInk.h"
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
#include <QWheelEvent>
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


class AuditTest : public QObject {
    Q_OBJECT
    static void prose(Term &t, const QString &markdown) {
        t.backend->resizeTerminal(14, 100);
        const QString anchor = QStringLiteral("relay://prose/audit/1");
        MarkdownAnsi md; md.setLinkAnchor(anchor);
        const QString rendered = md.feed(markdown) + md.finish();
        ProseCollector collector; collector.feed(rendered);
        WordWrap wrap; wrap.setColumns(100);
        t.backend->writeToDisplay((QStringLiteral("\x1b]8;;") + anchor + QStringLiteral("\x1b\\")
            + wrap.feed(rendered) + wrap.flush() + QStringLiteral("\x1b]8;;\x1b\\")).toUtf8());
        QTest::qWait(100);
        if (!qEnvironmentVariableIsSet("AUDIT_NATIVE_ROWS")) t.backend->setProseBlock(anchor, collector.take(), 100);
        QTest::qWait(100);
    }
    static TerminalView::Link hit(Term &t, const QString &needle) {
        const auto rows=t.view->visibleRowsText();
        for(int r=0;r<rows.size();++r) {
            const int c=rows[r].indexOf(needle);
            if(c>=0) return t.view->linkAtPoint(t.cellPoint(r,c+needle.size()/2));
        }
        return {};
    }
    static QString select(Term &t,int r1,int c1,int r2,int c2) {
        QTest::qWait(QApplication::doubleClickInterval()+50);
        const auto from=t.cellPoint(r1,c1), to=t.cellPoint(r2,c2);
        QTest::mousePress(t.view,Qt::LeftButton,Qt::NoModifier,from);
        QMouseEvent move(QEvent::MouseMove,to,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(t.view,&move);
        QTest::mouseRelease(t.view,Qt::LeftButton,Qt::NoModifier,to);
        return t.backend->selectedText();
    }
private slots:
    void missingLabelTargetStillAllowsKnownCard() {
        Term t(QStringLiteral("libvterm"),QStringLiteral("/bin/cat"));
        t.view->setCardLookup([](const QString &id,QString*){return id==QStringLiteral("GWXM");});
        prose(t,QStringLiteral("See [#GWXM](/no-such-card-audit.md).\n"));
        QCOMPARE(hit(t,QStringLiteral("#GWXM")).target,QStringLiteral("relay://card/GWXM"));
        t.backend->resizeTerminal(14,62); QTest::qWait(120);
        QCOMPARE(hit(t,QStringLiteral("#GWXM")).target,QStringLiteral("relay://card/GWXM"));
    }
    void copyingAfterWideCharacterSurvivesResize() {
        Term t(QStringLiteral("libvterm"),QStringLiteral("/bin/cat"));
        prose(t,QString::fromUtf8("中ABCDEF\n"));
        int row=t.rowOf(QString::fromUtf8("中ABCDEF")); QVERIFY(row>=0);
        QCOMPARE(select(t,row,2,row,4),QStringLiteral("ABC"));
        t.backend->resizeTerminal(14,62); QTest::qWait(120);
        row=t.rowOf(QString::fromUtf8("中ABCDEF")); QVERIFY(row>=0);
        QCOMPARE(select(t,row,2,row,4),QStringLiteral("ABC"));
    }
    void copyPreservesSpaceAtWrappedEdge() {
        Term t(QStringLiteral("libvterm"),QStringLiteral("/bin/cat"));
        const QString text=QStringLiteral("alpha beta gamma xyz delta");
        prose(t,text+QLatin1Char('\n'));
        int row=t.rowOf(text); QVERIFY(row>=0);
        QCOMPARE(select(t,row,0,row,text.size()-1),text);
        t.backend->resizeTerminal(14,20); QTest::qWait(120);
        qInfo()<<t.view->visibleRowsText();
        const int first=t.rowOf(QStringLiteral("alpha beta gamma xyz"));
        const bool native=qEnvironmentVariableIsSet("AUDIT_NATIVE_ROWS");
        const int last=t.rowOf(native ? QStringLiteral(" delta") : QStringLiteral("delta"));
        QVERIFY(first>=0 && last>=0);
        QCOMPARE(select(t,first,0,last,native ? 5 : 4),text);
    }
    void keyboardLinkSelectionVisibleAfterResize() {
        Term t(QStringLiteral("libvterm"),QStringLiteral("/bin/cat"));
        t.view->setCardLookup([](const QString &id,QString*){return id==QStringLiteral("GWXM");});
        prose(t,QStringLiteral("See #GWXM\n"));
        auto scheme=t.view->colorScheme();
        scheme.selection=QColor("#ff00fe"); t.view->setColorScheme(scheme);
        TerminalView::Link link;
        QVERIFY(t.view->stepLink(-1,&link));
        QCOMPARE(link.target,QStringLiteral("relay://card/GWXM"));
        const auto before=t.grab(); bool beforeSelected=false;
        for(int row=0;row<t.view->rows();++row)
            beforeSelected |= rowHasColor(before,row,t.view->cellHeight(),scheme.selection);
        QVERIFY2(beforeSelected,"baseline keyboard selection missing");
        t.view->endLinkWalk();
        t.backend->resizeTerminal(14,62); QTest::qWait(120);
        QVERIFY(t.view->stepLink(-1,&link));
        QCOMPARE(link.target,QStringLiteral("relay://card/GWXM"));
        const auto im=t.grab(); bool selected=false;
        for(int row=0;row<t.view->rows();++row)
            selected |= rowHasColor(im,row,t.view->cellHeight(),scheme.selection);
        QVERIFY2(selected,"keyboard-walk selection has no visible highlight after rewrap");
    }
};
QTEST_MAIN(AuditTest)
#include "probe.moc"
