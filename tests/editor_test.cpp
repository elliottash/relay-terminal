// SPDX-License-Identifier: GPL-3.0-or-later
#include "RichEditor.h"
#include <QApplication>
#include <QClipboard>
#include <QInputMethodEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QTest>
#include <QTextCursor>

class EditorTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void nativeSelectionAndUndo() {
        RichEditor editor; editor.resize(600, 150); editor.show(); editor.setFocus();
        QTest::keyClicks(&editor, "hello world");
        QTest::keyClick(&editor, Qt::Key_Left, Qt::ShiftModifier | Qt::ControlModifier);
        QCOMPARE(editor.textCursor().selectedText(), QStringLiteral("world"));
        QTest::keyClick(&editor, Qt::Key_Backspace);
        QCOMPARE(editor.toPlainText(), QStringLiteral("hello "));
        QTest::keyClick(&editor, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(editor.toPlainText(), QStringLiteral("hello world"));
        QTest::keyClick(&editor, Qt::Key_A, Qt::ControlModifier);
        QCOMPARE(editor.textCursor().selectedText(), QStringLiteral("hello world"));
    }
    void submissionAndMultiline() {
        RichEditor editor; QStringList routes;
        editor.onSubmit = [&routes](const QString &route) { routes.append(route); };
        QTest::keyClicks(&editor, "echo one");
        QTest::keyClick(&editor, Qt::Key_Return, Qt::ShiftModifier);
        QTest::keyClicks(&editor, "echo two");
        QCOMPARE(editor.toPlainText(), QStringLiteral("echo one\necho two"));
        QVERIFY(routes.isEmpty());
        QTest::keyClick(&editor, Qt::Key_Return);
        QTest::keyClick(&editor, Qt::Key_Return, Qt::ControlModifier);
        QTest::keyClick(&editor, Qt::Key_Return, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(routes, QStringList({"auto", "agent", "shell"}));
    }
    void keypadEnterMatchesReturn() {
        RichEditor editor; QStringList routes;
        editor.onSubmit = [&routes](const QString &route) { routes.append(route); };
        QTest::keyClicks(&editor, "echo hi");
        // The numpad's Enter arrives as Key_Enter with KeypadModifier; it must submit like
        // Return, not fall through to a newline, and its Ctrl/Ctrl+Shift forms must work too.
        QTest::keyClick(&editor, Qt::Key_Enter, Qt::KeypadModifier);
        QCOMPARE(routes, QStringList({"auto"}));
        QCOMPARE(editor.toPlainText(), QStringLiteral("echo hi"));
        QTest::keyClick(&editor, Qt::Key_Enter, Qt::KeypadModifier | Qt::ShiftModifier);
        QCOMPARE(editor.toPlainText(), QStringLiteral("echo hi\n"));
        QVERIFY(routes.size() == 1);
        QTest::keyClick(&editor, Qt::Key_Enter, Qt::KeypadModifier | Qt::ControlModifier);
        QTest::keyClick(&editor, Qt::Key_Enter, Qt::KeypadModifier | Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(routes, QStringList({"auto", "agent", "shell"}));
    }
    void shiftClickSelection() {
        RichEditor editor; editor.resize(600, 150); editor.setPlainText(QStringLiteral("hello world")); editor.show();
        QTest::qWait(30);
        auto cursor = editor.textCursor(); cursor.setPosition(0);
        const auto start = editor.cursorRect(cursor).center();
        cursor.setPosition(5); const auto end = editor.cursorRect(cursor).center();
        QTest::mouseClick(editor.viewport(), Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseClick(editor.viewport(), Qt::LeftButton, Qt::ShiftModifier, end);
        QCOMPARE(editor.textCursor().selectedText(), QStringLiteral("hello"));
    }
    void mouseDragSelection() {
        RichEditor editor; editor.resize(600, 150); editor.setPlainText(QStringLiteral("hello world")); editor.show();
        QTest::qWait(30);
        auto cursor = editor.textCursor(); cursor.setPosition(0);
        const auto start = editor.cursorRect(cursor).center();
        cursor.setPosition(5); const auto end = editor.cursorRect(cursor).center();
        QTest::mousePress(editor.viewport(), Qt::LeftButton, Qt::NoModifier, start);
        QMouseEvent move(QEvent::MouseMove, QPointF(end), QPointF(editor.viewport()->mapToGlobal(end)),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(editor.viewport(), &move);
        QTest::mouseRelease(editor.viewport(), Qt::LeftButton, Qt::NoModifier, end);
        QCOMPARE(editor.textCursor().selectedText(), QStringLiteral("hello"));
    }
    void pasteNeverSubmits() {
        RichEditor editor; int submissions = 0;
        editor.onSubmit = [&submissions](const QString &) { ++submissions; };
        QApplication::clipboard()->setText(QStringLiteral("one\ntwo\n")); editor.paste();
        QCOMPARE(editor.toPlainText(), QStringLiteral("one\ntwo\n")); QCOMPARE(submissions, 0);
    }
    void imeDoesNotPrematurelySubmit() {
        RichEditor editor; int submissions = 0;
        editor.onSubmit = [&submissions](const QString &) { ++submissions; };
        QInputMethodEvent preedit(QStringLiteral("compose"), {});
        QApplication::sendEvent(&editor, &preedit);
        QTest::keyClick(&editor, Qt::Key_Return);
        QCOMPARE(submissions, 0);
    }
    void historyPreservesDraft() {
        RichEditor editor; editor.remember(QStringLiteral("git status"));
        editor.setPlainText(QStringLiteral("unfinished draft"));
        QTest::keyClick(&editor, Qt::Key_Up);
        QCOMPARE(editor.toPlainText(), QStringLiteral("git status"));
        QTest::keyClick(&editor, Qt::Key_Down);
        QCOMPARE(editor.toPlainText(), QStringLiteral("unfinished draft"));
    }
    void ghostTextAcceptsWholeOrWord() {
        RichEditor editor;
        editor.setPlainText(QStringLiteral("git"));
        editor.moveCursor(QTextCursor::End);
        editor.setGhost(QStringLiteral(" commit -m wip"));
        QVERIFY(editor.acceptGhost(false));
        QCOMPARE(editor.toPlainText(), QStringLiteral("git commit"));
        QCOMPARE(editor.ghost(), QStringLiteral(" -m wip"));
        QVERIFY(editor.acceptGhost(true));
        QCOMPARE(editor.toPlainText(), QStringLiteral("git commit -m wip"));
        QVERIFY(editor.ghost().isEmpty());
        QVERIFY(!editor.acceptGhost(true));
    }
    // Image context (issue EM1E): a paste or a drop the pane claims as an image becomes `@path`
    // tokens instead of text, and everything the pane does not claim still pastes as text.
    void pastedImagesBecomeAttachmentTokens() {
        RichEditor editor;
        int calls = 0;
        bool sawDrop = false;
        editor.onImageMime = [&](const QMimeData *data, bool dropped) -> QStringList {
            ++calls;
            sawDrop = dropped;
            return data->hasImage() ? QStringList{QStringLiteral("@/tmp/shot.png")} : QStringList{};
        };
        QMimeData *image = new QMimeData;
        QImage picture(4, 4, QImage::Format_RGB32);
        picture.fill(Qt::blue);
        image->setImageData(picture);
        QApplication::clipboard()->setMimeData(image);
        editor.setPlainText(QStringLiteral("look at"));
        editor.moveCursor(QTextCursor::End);
        editor.paste();
        QCOMPARE(calls, 1);
        QVERIFY(!sawDrop);
        QCOMPARE(editor.toPlainText(), QStringLiteral("look at @/tmp/shot.png "));
    }
    void aPasteWithoutAnImageStillPastesText() {
        RichEditor editor;
        editor.onImageMime = [](const QMimeData *, bool) { return QStringList{}; };
        QMimeData *text = new QMimeData;
        text->setText(QStringLiteral("git status"));
        QApplication::clipboard()->setMimeData(text);
        editor.paste();
        QCOMPARE(editor.toPlainText(), QStringLiteral("git status"));
    }
    void attachmentTokensNeedNoSpaceAfterABlankBox() {
        RichEditor editor;
        editor.onImageMime = [](const QMimeData *, bool) {
            return QStringList{QStringLiteral("@/tmp/a.png"), QStringLiteral("@\"/tmp/b c.png\"")};
        };
        QMimeData *text = new QMimeData;
        text->setText(QStringLiteral("ignored"));
        QApplication::clipboard()->setMimeData(text);
        editor.paste();
        QCOMPARE(editor.toPlainText(), QStringLiteral("@/tmp/a.png @\"/tmp/b c.png\" "));
    }
    void upInsideMultilineTextMovesCursor() {
        RichEditor editor; editor.remember(QStringLiteral("git status"));
        editor.setPlainText(QStringLiteral("line one\nline two"));
        editor.moveCursor(QTextCursor::End);
        QTest::keyClick(&editor, Qt::Key_Up);
        QCOMPARE(editor.toPlainText(), QStringLiteral("line one\nline two"));
        QCOMPARE(editor.textCursor().blockNumber(), 0);
    }
};
QTEST_MAIN(EditorTests)
#include "editor_test.moc"
