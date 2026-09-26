// SPDX-License-Identifier: AGPL-3.0-or-later
// The Actions palette (cards #MAGP and #CMPA): a modal over the window with one search box, a
// three-arm Compass of group buttons, and a result list that shows Recent and For this pane before
// anything is typed.
//
// What is asserted is what the card's "Done means" asks of the widget itself: it opens, closes and
// opens again every time (#XAME was a menu that would not reopen); typing finds an item by the
// start of its label and by the `/slash` spelling in its aliases; the empty list is Recent then
// For this pane; Enter runs the row, reports its key and closes, Ctrl+Enter runs it and stays;
// each Compass group menu holds its assigned catalog items with their shortcuts; the keyboard
// moves from search into each arm and back; Esc, the opening chord and a click
// elsewhere close it; with an editor installed, a right-click on a row or a dropdown entry offers
// "Change shortcut…" and hands over the item's key. The window's catalog, recent store and contextual rows are the caller's,
// and are faked here.
#include "ActionPalette.h"
#include "PaletteCardSearch.h"

#include <QAbstractButton>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QScrollBar>
#include <QTest>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>

using namespace relay;

namespace {

ActionItem item(const QString &key, const QString &section, const QString &label, const QString &shortcut = {},
                const QString &aliases = {})
{
    ActionItem out;
    out.key = key;
    out.section = section;
    out.label = label;
    out.shortcut = shortcut;
    out.aliases = aliases;
    return out;
}

// The labels in the list, top to bottom, with a header written as "# Header".
QStringList rowsOf(const QListWidget *list)
{
    QStringList out;
    for (int i = 0; i < list->count(); ++i) {
        const QListWidgetItem *row = list->item(i);
        out << (row->flags() == Qt::NoItemFlags ? QStringLiteral("# ") : QString()) + row->text();
    }
    return out;
}

// A menu entry's label without the shortcut after the tab, and a submenu's by its title.
QString labelOf(const QAction *action)
{
    return action->text().section(QLatin1Char('\t'), 0, 0).replace(QStringLiteral("&&"), QStringLiteral("&"));
}

} // namespace

class ActionPaletteTests : public QObject {
    Q_OBJECT

    QWidget *m_window = nullptr;
    QLineEdit *m_elsewhere = nullptr;   // something else in the window, to click on and to hold focus
    ActionPalette *m_palette = nullptr;
    QStringList m_ran, m_chosen;
    bool m_fullCompass = false;

    QList<ActionItem> catalog()
    {
        QList<ActionItem> out;
        auto runs = [this](ActionItem it) {
            const QString key = it.key;
            it.run = [this, key] { m_ran << key; };
            return it;
        };
        out << runs(item(QStringLiteral("pane.restart"), QStringLiteral("Panes & tabs"), QStringLiteral("Restart shell"),
                         QStringLiteral("Ctrl+Shift+R")));
        out << runs(item(QStringLiteral("pane.split"), QStringLiteral("Panes & tabs"), QStringLiteral("Split right"),
                         QStringLiteral("Ctrl+Shift+D")));
        out << runs(item(QStringLiteral("board.open"), QStringLiteral("Board"), QStringLiteral("Board"),
                         QStringLiteral("Ctrl+Shift+B"), QStringLiteral("/board cards")));
        out << runs(item(QStringLiteral("files.open"), QStringLiteral("Files"), QStringLiteral("Open file…"),
                         QStringLiteral("Ctrl+O")));
        out << runs(item(QStringLiteral("agent.stop"), QStringLiteral("Agent"), QStringLiteral("Stop the agent"),
                         QStringLiteral("Esc")));
        ActionItem models = item(QStringLiteral("agent.model"), QStringLiteral("Agent"), QStringLiteral("Model"));
        models.children = [runs] {
            return QList<ActionItem>{runs(item(QStringLiteral("agent.model.glm"), QStringLiteral("Agent"), QStringLiteral("glm-5.3"))),
                                     runs(item(QStringLiteral("agent.model.opus"), QStringLiteral("Agent"), QStringLiteral("opus")))};
        };
        out << models;
        if (m_fullCompass) {
            out << runs(item(QStringLiteral("model.pick"), QStringLiteral("Models"), QStringLiteral("Pick model")));
            out << runs(item(QStringLiteral("sessions.open"), QStringLiteral("Sessions & Projects"), QStringLiteral("Sessions")));
            out << runs(item(QStringLiteral("terminal.clear"), QStringLiteral("Terminal"), QStringLiteral("Clear terminal")));
            out << runs(item(QStringLiteral("remote.join"), QStringLiteral("Remote and sharing"), QStringLiteral("Join remote")));
            out << runs(item(QStringLiteral("options.open"), QStringLiteral("Shortcuts"), QStringLiteral("Options")));
            out << runs(item(QStringLiteral("plugin.surprise"), QStringLiteral("Custom plugin"), QStringLiteral("Surprise plugin")));
        }
        return out;
    }

    QList<ActionItem> forThisPane()
    {
        QList<ActionItem> all = catalog();
        return {all.at(4), all.at(0)};   // Stop the agent, Restart shell
    }

private slots:
    void init()
    {
        m_ran.clear();
        m_chosen.clear();
        m_fullCompass = false;
        m_window = new QWidget;
        m_window->resize(1000, 700);
        auto *layout = new QVBoxLayout(m_window);
        m_elsewhere = new QLineEdit(m_window);
        layout->addWidget(m_elsewhere);
        layout->addStretch(1);
        m_palette = new ActionPalette(
            m_window, [this] { return catalog(); }, [this] { return forThisPane(); },
            [] { return QStringList{QStringLiteral("files.open"), QStringLiteral("board.open"), QStringLiteral("gone.away"),
                                    QStringLiteral("agent.model.opus")}; },
            [this](const QString &key) { m_chosen << key; });
        m_window->show();
        m_window->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(m_window));
        m_elsewhere->setFocus();
    }

    void cleanup()
    {
        delete m_window;
        m_window = nullptr;
        m_palette = nullptr;
    }

    // #XAME: a surface that would not open a second time. Three rounds, each closed a different way.
    void opensClosesAndReopens()
    {
        for (int round = 0; round < 3; ++round) {
            m_palette->open();
            QVERIFY(m_palette->isOpen());
            QVERIFY(m_palette->isVisible());
            QTRY_VERIFY(m_palette->searchBox()->hasFocus());
            QVERIFY(m_palette->width() <= 800);
            QVERIFY(m_palette->height() <= m_window->height() * 6 / 10);
            QCOMPARE(m_palette->x(), (m_window->width() - m_palette->width()) / 2);
            QTest::keyClicks(m_palette->searchBox(), QStringLiteral("zz"));
            if (round == 0) QTest::keyClick(m_palette->searchBox(), Qt::Key_Escape);
            else if (round == 1) m_palette->toggle();
            else m_palette->close();
            QVERIFY(!m_palette->isOpen());
            QVERIFY(!m_palette->isVisible());
            QTRY_VERIFY(m_elsewhere->hasFocus());   // focus goes back where it was
        }
        // Opened again, it starts clean: no query left over from the last time.
        m_palette->toggle();
        QVERIFY(m_palette->isOpen());
        QVERIFY(m_palette->searchBox()->text().isEmpty());
    }

    void clickElsewhereCloses()
    {
        m_palette->open();
        // Opened scrolled to the top, so the first header shows; a click on it is inside and changes nothing.
        QCOMPARE(m_palette->list()->verticalScrollBar()->value(), 0);
        QCOMPARE(m_palette->list()->itemAt(2, 2)->text(), QStringLiteral("Recent"));
        QTest::mouseClick(m_palette->list()->viewport(), Qt::LeftButton, {}, QPoint(2, 2));
        QVERIFY(m_palette->isOpen());
        QVERIFY(m_ran.isEmpty());
        QTest::mouseClick(m_elsewhere, Qt::LeftButton);
        QVERIFY(!m_palette->isOpen());
        // A click on a row is Enter on it.
        m_palette->open();
        QVERIFY(m_palette->isOpen());
        QListWidget *list = m_palette->list();
        QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, list->visualItemRect(list->item(2)).center());
        QCOMPARE(m_ran, QStringList{QStringLiteral("board.open")});
        QCOMPARE(m_chosen, QStringList{QStringLiteral("board.open")});
        QVERIFY(!m_palette->isOpen());
    }

    void openingChordCloses()
    {
        m_palette->setToggleKeys({QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P)});
        m_palette->open();
        QTest::keyClick(m_palette->searchBox(), Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(!m_palette->isOpen());
    }

    void emptyQueryShowsRecentThenForThisPane()
    {
        m_palette->open();
        const QStringList rows = rowsOf(m_palette->list());
        // A recent key the catalog no longer has is skipped; one inside a submenu is found there.
        const QStringList head{QStringLiteral("# Recent"), QStringLiteral("Open file…"), QStringLiteral("Board"),
                               QStringLiteral("Model › opus"), QStringLiteral("# For this pane"),
                               QStringLiteral("Stop the agent"), QStringLiteral("Restart shell")};
        QCOMPARE(rows.mid(0, head.size()), head);
        // Then every group in catalog order, so the empty list reaches everything.
        QVERIFY(rows.indexOf(QStringLiteral("# Agent")) > rows.indexOf(QStringLiteral("# For this pane")));
        QVERIFY(rows.indexOf(QStringLiteral("# Board")) > rows.indexOf(QStringLiteral("# Panes")));
        QVERIFY(rows.contains(QStringLiteral("Model  ›")));
        QCOMPARE(m_palette->currentKey(), QStringLiteral("files.open"));   // the highlight skips the header
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Down);
        QCOMPARE(m_palette->currentKey(), QStringLiteral("files.open"));   // enters the results
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Down);
        QCOMPARE(m_palette->currentKey(), QStringLiteral("board.open"));
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Down);
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Down);
        QCOMPARE(m_palette->currentKey(), QStringLiteral("agent.stop"));   // over the "For this pane" header
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Up);
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Up);
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Up);
        QCOMPARE(m_palette->currentKey(), QStringLiteral("files.open"));
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Up);   // returns home from the first result
        QCOMPARE(m_palette->currentKey(), QStringLiteral("files.open"));
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Up);   // from home, enter the upper arm
        QTRY_VERIFY(m_palette->groupButtons().at(1)->hasFocus());
    }

    void typingFindsByLabelStart()
    {
        m_palette->open();
        QTest::keyClicks(m_palette->searchBox(), QStringLiteral("rest"));
        QCOMPARE(m_palette->currentKey(), QStringLiteral("pane.restart"));
        QCOMPARE(m_palette->list()->currentItem()->text(), QStringLiteral("Restart shell"));
    }

    void conversationResultsPrecedeActionsAndIgnoreStaleReplies()
    {
        QString requested;
        m_palette->setConversationSearch([&requested](const QString &query) { requested = query; });
        m_palette->open();
        QTest::keyClicks(m_palette->searchBox(), QStringLiteral("rest"));
        QTRY_COMPARE(requested, QStringLiteral("rest"));
        ActionItem conversation = item(QStringLiteral("conversation:one"), QStringLiteral("Conversations"),
                                       QStringLiteral("Restore session"));
        conversation.run = [this] { m_ran << QStringLiteral("conversation:one"); };
        m_palette->setConversationResults(QStringLiteral("old"), {conversation});
        QCOMPARE(m_palette->currentKey(), QStringLiteral("pane.restart"));
        m_palette->setConversationResults(QStringLiteral("rest"), {conversation});
        QCOMPARE(rowsOf(m_palette->list()).mid(0, 3),
                 (QStringList{QStringLiteral("# Conversations"), QStringLiteral("Restore session"),
                              QStringLiteral("Restart shell")}));
        QCOMPARE(m_palette->currentKey(), QStringLiteral("conversation:one"));
        const QString capture = qEnvironmentVariable("RELAY_PALETTE_CAPTURE");
        if (!capture.isEmpty()) QVERIFY(m_palette->grab().save(capture));
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Return);
        QCOMPARE(m_ran, QStringList{QStringLiteral("conversation:one")});
    }

    void exactCardLookupReadsClaimWithoutMatchingThreadMentions()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(QDir(temp.path()).mkpath(QStringLiteral("features")));
        QVERIFY(QDir(temp.path()).mkpath(QStringLiteral("threads")));
        QFile thread(temp.path() + QStringLiteral("/threads/WM4K.md"));
        QVERIFY(thread.open(QIODevice::WriteOnly));
        thread.write("---\nid: WM4K\n---\n# Wrong thread\n");
        thread.close();
        QFile card(temp.path() + QStringLiteral("/features/card.md"));
        QVERIFY(card.open(QIODevice::WriteOnly));
        card.write("---\nid: WM4K\nsession: pane-token-123\nstatus: executing\n---\n# Search active conversations\n");
        card.close();
        QCOMPARE(palettecards::code(QStringLiteral(" #wm4k ")), QStringLiteral("WM4K"));
        QVERIFY(palettecards::code(QStringLiteral("1234")).isEmpty());
        QVERIFY(palettecards::code(QStringLiteral("WM4")).isEmpty());
        const auto hit = palettecards::find(temp.path(), QStringLiteral("WM4K"));
        QVERIFY(hit.has_value());
        QCOMPARE(hit->title, QStringLiteral("Search active conversations"));
        QCOMPARE(hit->session, QStringLiteral("pane-token-123"));
        QVERIFY(!palettecards::find(temp.path(), QStringLiteral("WXYZ")).has_value());
    }

    void cardCodeShowsPaneAndBoardChoices()
    {
        m_palette->open();
        QTest::keyClicks(m_palette->searchBox(), QStringLiteral("#WM4K"));
        ActionItem pane = item(QStringLiteral("pane:token"), QStringLiteral("Cards"),
                               QStringLiteral("Claiming pane · Research"));
        pane.run = [this] { m_ran << QStringLiteral("pane:token"); };
        ActionItem card = item(QStringLiteral("card:WM4K"), QStringLiteral("Cards"),
                               QStringLiteral("#WM4K · Search active conversations"));
        card.run = [this] { m_ran << QStringLiteral("card:WM4K"); };
        m_palette->setCardResults(QStringLiteral("#WM4K"), {pane, card});
        QCOMPARE(rowsOf(m_palette->list()).mid(0, 3),
                 (QStringList{QStringLiteral("# Cards"), QStringLiteral("Claiming pane · Research"),
                              QStringLiteral("#WM4K · Search active conversations")}));
        const QString capture = qEnvironmentVariable("RELAY_PALETTE_CARD_CAPTURE");
        if (!capture.isEmpty()) QVERIFY(m_palette->grab().save(capture));
        QCOMPARE(m_palette->currentKey(), QStringLiteral("pane:token"));
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Down);
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Down);
        QCOMPARE(m_palette->currentKey(), QStringLiteral("card:WM4K"));
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Return);
        QCOMPARE(m_ran, QStringList{QStringLiteral("card:WM4K")});
    }

    void typingFindsBySlashAlias()
    {
        m_palette->open();
        QTest::keyClicks(m_palette->searchBox(), QStringLiteral("/board"));
        QCOMPARE(m_palette->currentKey(), QStringLiteral("board.open"));
    }

    void typingReachesIntoSubmenus()
    {
        m_palette->open();
        QTest::keyClicks(m_palette->searchBox(), QStringLiteral("opus"));
        QCOMPARE(m_palette->currentKey(), QStringLiteral("agent.model.opus"));
        QCOMPARE(m_palette->list()->currentItem()->text(), QStringLiteral("Model › opus"));
    }

    void nothingMatchesSaysSo()
    {
        m_palette->open();
        QTest::keyClicks(m_palette->searchBox(), QStringLiteral("qqqq"));
        QCOMPARE(rowsOf(m_palette->list()), QStringList{QStringLiteral("# Nothing matches “qqqq”.")});
        QVERIFY(m_palette->currentKey().isEmpty());
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Return);   // runs nothing, stays open
        QVERIFY(m_ran.isEmpty());
        QVERIFY(m_palette->isOpen());
    }

    void enterRunsReportsAndCloses()
    {
        m_palette->open();
        QTest::keyClicks(m_palette->searchBox(), QStringLiteral("rest"));
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Return);
        QCOMPARE(m_ran, QStringList{QStringLiteral("pane.restart")});
        QCOMPARE(m_chosen, QStringList{QStringLiteral("pane.restart")});
        QVERIFY(!m_palette->isOpen());
    }

    void ctrlEnterRunsAndStaysOpen()
    {
        m_palette->open();
        QTest::keyClicks(m_palette->searchBox(), QStringLiteral("split"));
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(m_ran, QStringList{QStringLiteral("pane.split")});
        QCOMPARE(m_chosen, QStringList{QStringLiteral("pane.split")});
        QVERIFY(m_palette->isOpen());
        QCOMPARE(m_palette->searchBox()->text(), QStringLiteral("split"));   // the query stays too
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Enter, Qt::ControlModifier);   // the keypad's Enter too
        QCOMPARE(m_ran.size(), 2);
    }

    // Ctrl+Alt+E's "New pane" chooser (#83YV): the palette opens on a submenu the person never
    // entered, so typing filters it, Enter runs a child, and Esc closes instead of climbing.
    void openSubmenuStartsInsideItAndEscCloses()
    {
        const ActionItem models = catalog().at(5);
        m_palette->openSubmenu(models);
        QVERIFY(m_palette->isOpen());
        QCOMPARE(rowsOf(m_palette->list()), (QStringList{QStringLiteral("# Model"), QStringLiteral("glm-5.3"), QStringLiteral("opus")}));
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Escape);
        QVERIFY(!m_palette->isOpen());
        m_palette->openSubmenu(models);
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Backspace);   // empty box: closes too
        QVERIFY(!m_palette->isOpen());
        m_palette->openSubmenu(models);
        QTest::keyClicks(m_palette->searchBox(), QStringLiteral("opus"));
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Return);
        QCOMPARE(m_ran, QStringList{QStringLiteral("agent.model.opus")});
        QVERIFY(!m_palette->isOpen());
        m_palette->open();   // an ordinary open afterwards still climbs out of a submenu
        QTest::keyClicks(m_palette->searchBox(), QStringLiteral("model"));
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Return);
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Escape);
        QVERIFY(m_palette->isOpen());
    }

    void aSubmenuListsItsChildren()
    {
        m_palette->open();
        QTest::keyClicks(m_palette->searchBox(), QStringLiteral("model"));
        QCOMPARE(m_palette->currentKey(), QStringLiteral("agent.model"));
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Return);
        QVERIFY(m_palette->isOpen());
        QVERIFY(m_chosen.isEmpty());   // opening a submenu is not a choice
        QCOMPARE(rowsOf(m_palette->list()), (QStringList{QStringLiteral("# Model"), QStringLiteral("glm-5.3"), QStringLiteral("opus")}));
        QVERIFY(m_palette->searchBox()->text().isEmpty());
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Escape);   // Esc leaves the submenu first
        QVERIFY(m_palette->isOpen());
        QCOMPARE(rowsOf(m_palette->list()).value(0), QStringLiteral("# Recent"));
        QTest::keyClicks(m_palette->searchBox(), QStringLiteral("model"));
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Return);
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Down);
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Down);
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Return);
        QCOMPARE(m_chosen, QStringList{QStringLiteral("agent.model.opus")});
        QVERIFY(!m_palette->isOpen());
    }

    void eachGroupMenuHoldsItsSection()
    {
        m_palette->open();
        const QList<QAbstractButton *> buttons = m_palette->groupButtons();
        QCOMPARE(buttons.size(), 5);
        const QStringList sections{QStringLiteral("Agent"), QStringLiteral("Models"), QStringLiteral("Panes"),
                                   QStringLiteral("Files"), QStringLiteral("Board")};
        const QList<QStringList> expected{{QStringLiteral("Stop the agent")},
                                          {QStringLiteral("Model")},
                                          {QStringLiteral("Restart shell"), QStringLiteral("Split right")},
                                          {QStringLiteral("Open file…")},
                                          {QStringLiteral("Board")}};
        for (int i = 0; i < buttons.size(); ++i) {
            // `&` is drawn, not read as a mnemonic: "Panes & tabs", never "Panes _tabs".
            QCOMPARE(buttons.at(i)->text().replace(QStringLiteral("&&"), QStringLiteral("&")), sections.at(i) + QStringLiteral(" ▾"));
            QMenu *menu = m_palette->groupMenu(i);
            QVERIFY(menu != nullptr);
            QStringList labels;
            for (const QAction *action : menu->actions()) labels << labelOf(action);
            QCOMPARE(labels, expected.at(i));
        }
        // The shortcut rides on the right of the entry, and a submenu is a real submenu.
        QMenu *panes = m_palette->groupMenu(2);
        QCOMPARE(panes->actions().at(0)->text(), QStringLiteral("Restart shell\tCtrl+Shift+R"));
        QMenu *agent = m_palette->groupMenu(1);
        QMenu *models = agent->actions().at(0)->menu();
        QVERIFY(models != nullptr);
        emit models->aboutToShow();
        QCOMPARE(models->actions().size(), 2);
        // Choosing an entry runs it, reports it and closes the palette.
        m_palette->groupMenu(4)->actions().at(0)->trigger();
        QCOMPARE(m_ran, QStringList{QStringLiteral("board.open")});
        QCOMPARE(m_chosen, QStringList{QStringLiteral("board.open")});
        QVERIFY(!m_palette->isOpen());
    }

    void tabWalksTheCompassGroups()
    {
        m_palette->open();
        QTRY_VERIFY(m_palette->searchBox()->hasFocus());
        const QList<QAbstractButton *> buttons = m_palette->groupButtons();
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Tab);
        QTRY_VERIFY(buttons.at(0)->hasFocus());
        QTest::keyClick(buttons.at(0), Qt::Key_Tab);
        QTRY_VERIFY(buttons.at(1)->hasFocus());
        QTest::keyClick(buttons.at(1), Qt::Key_Tab);
        QTRY_VERIFY(buttons.at(2)->hasFocus());
        QTest::keyClick(buttons.at(2), Qt::Key_Escape);   // back to the search box, still open
        QVERIFY(m_palette->isOpen());
        QTRY_VERIFY(m_palette->searchBox()->hasFocus());
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Tab);   // back to the button it left
        QTRY_VERIFY(buttons.at(2)->hasFocus());
        QTest::keyClicks(buttons.at(2), QStringLiteral("bo"));   // typing goes to the search box
        QCOMPARE(m_palette->searchBox()->text(), QStringLiteral("bo"));
        QTRY_VERIFY(m_palette->searchBox()->hasFocus());
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Escape);
        QVERIFY(!m_palette->isOpen());
    }

    void compassArmsAndCaretEdges()
    {
        m_fullCompass = true;
        m_palette->open();
        const QList<QAbstractButton *> buttons = m_palette->groupButtons();
        QCOMPARE(buttons.size(), 9);
        QMenu *options = m_palette->groupMenu(8);
        QCOMPARE(options->actions().size(), 2); // unfamiliar sections remain reachable
        QCOMPARE(labelOf(options->actions().at(1)), QStringLiteral("Surprise plugin"));
        QLineEdit *search = m_palette->searchBox();
        QTest::keyClick(search, Qt::Key_Up);
        QTRY_VERIFY(buttons.at(1)->hasFocus());
        const QPoint agent = buttons.at(0)->mapTo(m_palette, buttons.at(0)->rect().center());
        const QPoint models = buttons.at(1)->mapTo(m_palette, buttons.at(1)->rect().center());
        const QPoint sessions = buttons.at(2)->mapTo(m_palette, buttons.at(2)->rect().center());
        QVERIFY(agent.x() < models.x() && models.x() < sessions.x());
        QVERIFY(qAbs(agent.y() - models.y()) <= 2 && qAbs(models.y() - sessions.y()) <= 2);
        QTest::keyClick(buttons.at(1), Qt::Key_Left);
        QTRY_VERIFY(buttons.at(0)->hasFocus());
        QTest::keyClick(buttons.at(0), Qt::Key_Right);
        QTRY_VERIFY(buttons.at(1)->hasFocus());
        QTest::keyClick(buttons.at(1), Qt::Key_Right);
        QTRY_VERIFY(buttons.at(2)->hasFocus());
        QTest::keyClick(buttons.at(2), Qt::Key_Right);
        QVERIFY(buttons.at(2)->hasFocus());
        QTest::keyClick(buttons.at(2), Qt::Key_Escape);
        QTRY_VERIFY(search->hasFocus());

        QTest::keyClick(search, Qt::Key_Left);
        QTRY_VERIFY(buttons.at(3)->hasFocus());
        QTest::keyClick(buttons.at(3), Qt::Key_Left);
        QTRY_VERIFY(buttons.at(4)->hasFocus());
        QTest::keyClick(buttons.at(4), Qt::Key_Left);
        QTRY_VERIFY(buttons.at(5)->hasFocus());
        QTest::keyClick(buttons.at(5), Qt::Key_Right);
        QTRY_VERIFY(buttons.at(4)->hasFocus());
        QTest::keyClick(buttons.at(4), Qt::Key_Right);
        QTest::keyClick(buttons.at(3), Qt::Key_Right);
        QTRY_VERIFY(search->hasFocus());

        QTest::keyClicks(search, QStringLiteral("abc"));
        search->setCursorPosition(1);
        QTest::keyClick(search, Qt::Key_Right);
        QVERIFY(search->hasFocus());
        QCOMPARE(search->cursorPosition(), 2);
        QTest::keyClick(search, Qt::Key_Right);
        QCOMPARE(search->cursorPosition(), 3);
        QTest::keyClick(search, Qt::Key_Right);
        QTRY_VERIFY(buttons.at(6)->hasFocus());
        QTest::keyClick(buttons.at(6), Qt::Key_Right);
        QTRY_VERIFY(buttons.at(7)->hasFocus());
        QTest::keyClick(buttons.at(7), Qt::Key_Right);
        QTRY_VERIFY(buttons.at(8)->hasFocus());
        QTest::keyClicks(buttons.at(8), QStringLiteral("d"));
        QTRY_VERIFY(search->hasFocus());
        QCOMPARE(search->text(), QStringLiteral("abcd"));
    }

    void compassCompactsInANarrowWindow()
    {
        m_fullCompass = true;
        m_window->resize(450, 700);
        m_palette->open();
        QCoreApplication::processEvents();
        QVERIFY(m_palette->width() <= 418);
        QVERIFY(m_palette->height() <= 420);
        QVERIFY(m_palette->searchBox()->isVisible());
        for (QAbstractButton *button : m_palette->groupButtons()) {
            QVERIFY(button->isVisible());
            QVERIFY(m_palette->rect().contains(button->mapTo(m_palette, button->rect().center())));
        }
    }

    void downOnAButtonDropsItsMenu()
    {
        m_palette->open();
        const QList<QAbstractButton *> buttons = m_palette->groupButtons();
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Tab);
        QTRY_VERIFY(buttons.at(0)->hasFocus());
        QTest::keyClick(buttons.at(0), Qt::Key_Down);
        QMenu *menu = m_palette->findChild<QMenu *>(QStringLiteral("actionPaletteMenu"));
        QVERIFY(menu != nullptr);
        QTRY_VERIFY(menu->isVisible());
        QCOMPARE(labelOf(menu->activeAction()), QStringLiteral("Stop the agent"));
        menu->hide();
    }

    // Right-click on a row: "Change shortcut…" closes the palette and hands the row's key to the
    // caller, which opens Options › Keyboard at that action. Nothing runs.
    // Focus that moves on in the window closes the palette and stays where it went (live drive,
    // 2026-09-22: Ctrl+Shift+A opened the Board and the palette stayed painted over it).
    void focusMovingOnCloses()
    {
        auto *other = new QLineEdit(m_window);
        m_window->layout()->addWidget(other);
        other->show();
        m_palette->open();
        QTRY_VERIFY(m_palette->searchBox()->hasFocus());
        other->setFocus(Qt::OtherFocusReason);
        QTRY_VERIFY(!m_palette->isOpen());
        QVERIFY(!m_palette->isVisible());
        QVERIFY(other->hasFocus());   // not handed back to where the palette was opened from
        m_palette->open();            // and it opens again
        QTRY_VERIFY(m_palette->searchBox()->hasFocus());
    }

    // The keyboard's context-menu event in the search box (what the Menu key becomes on X11)
    // offers the highlighted row's "Change shortcut…", not the line edit's Undo/Cut menu.
    void menuKeyInTheSearchBoxOffersChangeShortcut()
    {
        QStringList edited;
        m_palette->setEditShortcut([&edited](const QString &key) { edited << key; });
        m_palette->open();
        QTest::keyClicks(m_palette->searchBox(), QStringLiteral("rest"));
        // Were the line edit's own menu to open, it would block in exec(): close it so the test fails.
        QTimer::singleShot(1500, [] { if (QWidget *popup = QApplication::activePopupWidget()) popup->close(); });
        const QPoint at(4, 4);
        QContextMenuEvent menuKey(QContextMenuEvent::Keyboard, at, m_palette->searchBox()->mapToGlobal(at));
        QApplication::sendEvent(m_palette->searchBox(), &menuKey);
        QTRY_VERIFY(m_palette->shortcutMenu() != nullptr && m_palette->shortcutMenu()->isVisible());
        m_palette->shortcutMenu()->actions().at(0)->trigger();
        QCOMPARE(edited, QStringList{QStringLiteral("pane.restart")});
        QVERIFY(!m_palette->isOpen());
    }

    void rightClickOnARowOffersChangeShortcut()
    {
        QStringList edited;
        m_palette->setEditShortcut([&edited](const QString &key) { edited << key; });
        m_palette->open();
        QListWidget *list = m_palette->list();
        const QPoint header = list->visualItemRect(list->item(0)).center();   // "Recent": no offer
        QContextMenuEvent onHeader(QContextMenuEvent::Mouse, header, list->viewport()->mapToGlobal(header));
        QApplication::sendEvent(list->viewport(), &onHeader);
        QVERIFY(m_palette->shortcutMenu() == nullptr);
        const QPoint row = list->visualItemRect(list->item(2)).center();      // "Board"
        QContextMenuEvent onRow(QContextMenuEvent::Mouse, row, list->viewport()->mapToGlobal(row));
        QApplication::sendEvent(list->viewport(), &onRow);
        QMenu *menu = m_palette->shortcutMenu();
        QVERIFY(menu != nullptr);
        QTRY_VERIFY(menu->isVisible());
        QCOMPARE(menu->actions().size(), 1);
        QCOMPARE(menu->actions().at(0)->text(), QStringLiteral("Change shortcut…"));
        QVERIFY(m_palette->isOpen());
        menu->actions().at(0)->trigger();
        QCOMPARE(edited, QStringList{QStringLiteral("board.open")});
        QVERIFY(!m_palette->isOpen());
        QVERIFY(m_ran.isEmpty());
        QVERIFY(m_chosen.isEmpty());
        // The Menu key offers the same for the highlighted row.
        m_palette->open();
        QTest::keyClicks(m_palette->searchBox(), QStringLiteral("rest"));
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Menu);
        QTRY_VERIFY(m_palette->shortcutMenu() != nullptr && m_palette->shortcutMenu()->isVisible());
        m_palette->shortcutMenu()->actions().at(0)->trigger();
        QCOMPARE(edited.last(), QStringLiteral("pane.restart"));
        QVERIFY(!m_palette->isOpen());
    }

    // The same on a dropdown entry, where a right *release* would otherwise run the entry.
    void rightClickInADropdownOffersChangeShortcut()
    {
        QStringList edited;
        m_palette->setEditShortcut([&edited](const QString &key) { edited << key; });
        m_palette->open();
        QTest::keyClick(m_palette->searchBox(), Qt::Key_Tab);
        QTRY_VERIFY(m_palette->groupButtons().at(0)->hasFocus());
        QTest::keyClick(m_palette->groupButtons().at(0), Qt::Key_Tab);
        QTRY_VERIFY(m_palette->groupButtons().at(1)->hasFocus());
        QTest::keyClick(m_palette->groupButtons().at(1), Qt::Key_Tab);
        QTRY_VERIFY(m_palette->groupButtons().at(2)->hasFocus());
        QTest::keyClick(m_palette->groupButtons().at(2), Qt::Key_Down);
        QMenu *dropdown = m_palette->findChild<QMenu *>(QStringLiteral("actionPaletteMenu"));
        QVERIFY(dropdown != nullptr);
        QTRY_VERIFY(dropdown->isVisible());
        const QRect entry = dropdown->actionGeometry(dropdown->actions().at(1));   // "Split right"
        QTest::mouseClick(dropdown, Qt::RightButton, {}, entry.center());
        QVERIFY(m_ran.isEmpty());   // the release did not run it
        QMenu *menu = m_palette->shortcutMenu();
        QVERIFY(menu != nullptr);
        QTRY_VERIFY(menu->isVisible());
        menu->actions().at(0)->trigger();
        QCOMPARE(edited, QStringList{QStringLiteral("pane.split")});
        QVERIFY(!m_palette->isOpen());
        QVERIFY(!dropdown->isVisible());
        QVERIFY(m_ran.isEmpty());
    }

    // With no editor installed there is no offer: a right-click on a row does nothing at all.
    void noEditorNoOffer()
    {
        m_palette->open();
        QListWidget *list = m_palette->list();
        const QPoint row = list->visualItemRect(list->item(2)).center();
        QContextMenuEvent onRow(QContextMenuEvent::Mouse, row, list->viewport()->mapToGlobal(row));
        QApplication::sendEvent(list->viewport(), &onRow);
        QVERIFY(m_palette->shortcutMenu() == nullptr);
        QVERIFY(m_palette->isOpen());
        QVERIFY(m_ran.isEmpty());
    }

    // What ran may change what the rows say, so a Ctrl+Enter reads the catalog again.
    void stayOpenRereadsTheCatalog()
    {
        int reads = 0;
        auto *palette = new ActionPalette(
            m_window, [&reads] {
                ++reads;
                ActionItem it = item(QStringLiteral("x"), QStringLiteral("S"), QStringLiteral("Toggle x"));
                it.checked = reads % 2 == 0;
                it.run = [] {};
                return QList<ActionItem>{it};
            },
            {}, {}, {});
        palette->open();
        QCOMPARE(palette->list()->item(1)->text(), QStringLiteral("Toggle x"));
        QTest::keyClick(palette->searchBox(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(palette->list()->item(1)->text(), QStringLiteral("✓  Toggle x"));
        delete palette;
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    ActionPaletteTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "actionpalette_test.moc"
