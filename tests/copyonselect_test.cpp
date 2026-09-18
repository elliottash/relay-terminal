// SPDX-License-Identifier: GPL-3.0-or-later
// Copy on highlight, the shared filter every read-only pane surface installs (src/CopyOnSelect.h).
#include "CopyOnSelect.h"
#include "DiffView.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>
#include <QTextBrowser>

namespace {

// A left-button release over the widget the filter watches. The filter is on the viewport of a
// scroll area and on the widget itself otherwise, which is where a real mouse release lands too.
void release(QWidget *widget)
{
    QWidget *target = widget;
    if (auto *area = qobject_cast<QAbstractScrollArea *>(widget))
        target = area->viewport();
    QMouseEvent event(QEvent::MouseButtonRelease, QPointF(1, 1), QPointF(1, 1), Qt::LeftButton,
                      Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(target, &event);
    // installCopyOnSelect() copies after the widget has settled its own cursor.
    QCoreApplication::processEvents();
}

void selectAll(QPlainTextEdit *edit)
{
    QTextCursor cursor = edit->textCursor();
    cursor.select(QTextCursor::Document);
    edit->setTextCursor(cursor);
}

}   // namespace

class CopyOnSelectTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("RelayTerminalTest"));
        QCoreApplication::setApplicationName(QStringLiteral("CopyOnSelectTest"));
    }

    void init()
    {
        QSettings().clear();
        QGuiApplication::clipboard()->clear(QClipboard::Clipboard);
    }

    // The setting is off out of the box, as the terminal's always was.
    void settingIsOffByDefault()
    {
        QVERIFY(!relay::copyOnSelectEnabled());
        QSettings().setValue(QStringLiteral("terminal/copy_on_select"), true);
        QVERIFY(relay::copyOnSelectEnabled());
    }

    // The key is `terminal/copy_on_select` although the feature is no longer terminal-only:
    // renaming it would turn the setting off for everyone who had switched it on.
    void settingKeepsItsTerminalEraKey()
    {
        QSettings settings;
        settings.setValue(QStringLiteral("terminal/copy_on_select"), true);
        settings.sync();
        QVERIFY(relay::copyOnSelectEnabled());
    }

    void highlightingReadOnlyTextCopiesIt()
    {
        QSettings().setValue(QStringLiteral("terminal/copy_on_select"), true);
        QPlainTextEdit view;
        view.setReadOnly(true);
        view.setPlainText(QStringLiteral("tool: read_file\npath: /x.py"));
        relay::installCopyOnSelect(&view);
        selectAll(&view);
        release(&view);
        QCOMPARE(QGuiApplication::clipboard()->text(),
                 QStringLiteral("tool: read_file\npath: /x.py"));
    }

    // Nothing happens with the setting off, which is how it ships.
    void nothingIsCopiedWhileTheSettingIsOff()
    {
        QPlainTextEdit view;
        view.setReadOnly(true);
        view.setPlainText(QStringLiteral("secret"));
        relay::installCopyOnSelect(&view);
        selectAll(&view);
        release(&view);
        QVERIFY(QGuiApplication::clipboard()->text().isEmpty());
    }

    // A keyboard-only selection is not a highlight: only the mouse release copies, exactly as
    // the terminal behaves. Ctrl+C is still the way to copy what the keyboard selected.
    void aKeyboardSelectionAloneCopiesNothing()
    {
        QSettings().setValue(QStringLiteral("terminal/copy_on_select"), true);
        QPlainTextEdit view;
        view.setReadOnly(true);
        view.setPlainText(QStringLiteral("selected by keyboard"));
        relay::installCopyOnSelect(&view);
        selectAll(&view);
        QCoreApplication::processEvents();
        QVERIFY(QGuiApplication::clipboard()->text().isEmpty());
    }

    // The prompt box, the composer, the plan editor and the settings inputs are where the user
    // types; taking the clipboard when they select something to overtype would lose their paste.
    void anEditableFieldIsNeverCopiedFrom()
    {
        QSettings().setValue(QStringLiteral("terminal/copy_on_select"), true);
        QPlainTextEdit editor;
        editor.setPlainText(QStringLiteral("a draft"));
        relay::installCopyOnSelect(&editor);
        selectAll(&editor);
        release(&editor);
        QVERIFY(QGuiApplication::clipboard()->text().isEmpty());

        // The file preview is the same widget in two states: a preview until setEditable(true).
        editor.setReadOnly(true);
        selectAll(&editor);
        release(&editor);
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("a draft"));
    }

    // The info pane, the conversation preview, the board card and the cleanup panel are all
    // QTextBrowsers; their block separator is U+2029 and must come out as a newline.
    void aTextBrowserCopiesRealNewlines()
    {
        QSettings().setValue(QStringLiteral("terminal/copy_on_select"), true);
        QTextBrowser body;
        body.setPlainText(QStringLiteral("Session\nModel: relay"));
        relay::installCopyOnSelect(&body);
        body.selectAll();
        release(&body);
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("Session\nModel: relay"));
        QVERIFY(!QGuiApplication::clipboard()->text().contains(QChar::ParagraphSeparator));
    }

    // The requests panel's detail, the sharing pane's prompt and the explorer's path line.
    void aSelectableLabelCopiesItsSelection()
    {
        QSettings().setValue(QStringLiteral("terminal/copy_on_select"), true);
        QLabel label(QStringLiteral("/home/elliott/repos/relay-terminal"));
        label.setTextInteractionFlags(Qt::TextSelectableByMouse);
        relay::installCopyOnSelect(&label);
        label.setSelection(0, label.text().size());
        release(&label);
        QCOMPARE(QGuiApplication::clipboard()->text(),
                 QStringLiteral("/home/elliott/repos/relay-terminal"));
    }

    // The share dialog's invite link is a read-only QLineEdit rather than a label.
    void aReadOnlyLineEditCopiesItsSelection()
    {
        QSettings().setValue(QStringLiteral("terminal/copy_on_select"), true);
        QLineEdit field(QStringLiteral("https://relay.example/invite#k"));
        field.setReadOnly(true);
        relay::installCopyOnSelect(&field);
        field.selectAll();
        release(&field);
        QCOMPARE(QGuiApplication::clipboard()->text(),
                 QStringLiteral("https://relay.example/invite#k"));
    }

    // Nothing highlighted, nothing copied -- a click that clears the selection must not wipe
    // what the user had on the clipboard already.
    void anEmptySelectionLeavesTheClipboardAlone()
    {
        QSettings().setValue(QStringLiteral("terminal/copy_on_select"), true);
        QGuiApplication::clipboard()->setText(QStringLiteral("kept"));
        QPlainTextEdit view;
        view.setReadOnly(true);
        view.setPlainText(QStringLiteral("nothing highlighted here"));
        relay::installCopyOnSelect(&view);
        release(&view);
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("kept"));
    }

    // A surface that says so out loud -- the pane toasts the character count.
    void theSurfaceIsToldWhatWasCopied()
    {
        QSettings().setValue(QStringLiteral("terminal/copy_on_select"), true);
        QString seen;
        QPlainTextEdit view;
        view.setReadOnly(true);
        view.setPlainText(QStringLiteral("reasoning"));
        relay::installCopyOnSelect(&view, [&seen](const QString &text) { seen = text; });
        selectAll(&view);
        release(&view);
        QCOMPARE(seen, QStringLiteral("reasoning"));
    }

    // Installing twice must not copy twice or leave two filters behind.
    void installingTwiceInstallsOneFilter()
    {
        QPlainTextEdit view;
        relay::installCopyOnSelect(&view);
        relay::installCopyOnSelect(&view);
        QCOMPARE(view.findChildren<QObject *>(relay::CopyOnSelectFilter::markerName()).size(), 1);
    }

    // A real pane surface, wired where it is built: the diff view a tool call opens.
    void theDiffViewCopiesOnHighlight()
    {
        QSettings().setValue(QStringLiteral("terminal/copy_on_select"), true);
        relay::DiffView view;
        view.setDiff(QStringLiteral("x.py"),
                     QStringLiteral("--- a/x.py\n+++ b/x.py\n@@ -1 +1 @@\n-old = 1\n+new = 1\n"));
        auto *text = view.findChild<QPlainTextEdit *>(QStringLiteral("diffText"));
        QVERIFY(text != nullptr);
        QVERIFY(text->findChild<QObject *>(relay::CopyOnSelectFilter::markerName()) != nullptr);
        selectAll(text);
        release(text);
        QVERIFY(QGuiApplication::clipboard()->text().contains(QStringLiteral("new = 1")));
    }
};

QTEST_MAIN(CopyOnSelectTests)
#include "copyonselect_test.moc"
