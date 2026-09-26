// Card #7EWF: the pane's top-right row is ⓘ ☰ new-pane ×, the ☰ holds the less frequent moves,
// the ⓘ opens conversation info as an overlay over the pane, and the header offers the session
// ID's copy button where the "auto" badge used to be. Included by consolemode_test.cpp, which
// already builds real panes; PaneChrome.h comes in here and nowhere else in that file.
#include "PaneChrome.h"

#include <QClipboard>

namespace cases {

void chromePress(QWidget *widget, QPoint at)
{
    QMouseEvent press(QEvent::MouseButtonPress, at, widget->mapToGlobal(at), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(widget, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, at, widget->mapToGlobal(at), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(widget, &release);
}

QStringList chromeMenuActions(PaneChrome &chrome)
{
    QStringList actions;
    for (const auto &entry : chrome.menuEntries()) actions << entry.first;
    return actions;
}

void theChromeRowIsInfoMenuNewPaneClose()
{
    Pane pane(home->path(), home->path(), true);
    pane.resize(700, 400);
    auto *chrome = new PaneChrome(&pane);
    // The chain chip sits above the row in the same column; the ⓘ went missing when a widget
    // there made the window's layout walk miss the row. The chrome owns it now.
    chrome->setChain({QStringLiteral("shell"), QStringLiteral("notes.md")}, 0);
    CHECK(chrome->infoButton() != nullptr);
    QHBoxLayout *row = chrome->buttonRow();
    CHECK(row != nullptr);
    QList<QWidget *> buttons;
    for (int i = 0; row && i < row->count(); ++i)
        if (QWidget *w = row->itemAt(i)->widget()) buttons << w;
    CHECK_EQ(buttons.size(), 4);
    if (buttons.size() == 4) {
        CHECK(buttons[0] == chrome->infoButton());
        CHECK(buttons[1] == chrome->menuButton());
        CHECK_EQ(buttons[2]->accessibleName(), QStringLiteral("New pane"));
        CHECK_EQ(buttons[2]->property("action").toString(), QStringLiteral("pane.newByMouse"));
        CHECK_EQ(buttons[3]->property("action").toString(), QStringLiteral("pane.close"));
    }
    // The moves that left the row are in the ☰, a terminal pane's including the background.
    CHECK_EQ(chromeMenuActions(*chrome), (QStringList{QStringLiteral("pane.moveToNewTab"),
                                                     QStringLiteral("pane.moveToBackground"),
                                                     QStringLiteral("pane.dimToggle")}));
    // Each entry shows its live key in the menu's shortcut column, and a pick runs through
    // onAction, which is what teaches that key.
    QStringList ran;
    chrome->onAction = [&ran](const QString &action) { ran << action; };
    QMenu *menu = chrome->buildMenu();
    const QString dimKeys = Keymap::instance().shortcutText(QStringLiteral("pane.dimToggle"));
    CHECK(!dimKeys.isEmpty());
    QAction *dim = nullptr;
    for (QAction *action : menu->actions())
        if (action->data().toString() == QStringLiteral("pane.dimToggle")) dim = action;
    CHECK(dim != nullptr);
    if (dim) {
        CHECK(dim->text().endsWith(QLatin1Char('\t') + dimKeys));
        CHECK(!dim->isChecked());
        dim->trigger();
    }
    CHECK_EQ(ran, QStringList{QStringLiteral("pane.dimToggle")});
    delete menu;
    // Dimmed by hand, the entry says how to undo it and is ticked.
    chrome->dimming.manual = 1;
    const QString dimLabel = chrome->menuEntries().last().second;
    CHECK_EQ(dimLabel, QStringLiteral("Restore automatic dimming"));
    menu = chrome->buildMenu();
    CHECK(menu->actions().last()->isChecked());
    delete menu;
}

void aToolPanesMenuHasNoBackground()
{
    QWidget leaf;
    leaf.resize(500, 300);
    auto *chrome = new PaneChrome(&leaf);
    CHECK(chrome->infoButton() == nullptr);   // no agent, no conversation info
    CHECK_EQ(chromeMenuActions(*chrome), (QStringList{QStringLiteral("pane.moveToNewTab"),
                                                     QStringLiteral("pane.dimToggle")}));
}

void theHeaderOffersTheSessionIdInsteadOfAnAutoBadge()
{
    Pane pane(home->path(), home->path(), true);
    pane.resize(700, 400);
    pane.show();
    CHECK(pane.findChild<QLabel *>(QStringLiteral("paneAuto")) == nullptr);
    auto *copy = pane.findChild<QToolButton *>(QStringLiteral("paneSessionCopy"));
    CHECK(copy != nullptr);
    if (!copy) return;
    CHECK(!copy->isVisibleTo(&pane));   // no session yet: nothing to copy
    const QString id = QStringLiteral("7ewf0a1b2c3d4e5f60718293a4b5c6d7");
    pane.deliverWorkerEvent({{"event", "reset"}, {"session_id", id}});
    CHECK(copy->isVisibleTo(&pane));
    CHECK(copy->toolTip().contains(id));
    QApplication::processEvents();
    // Right after the title, not pushed to the far end of the row.
    auto *title = pane.findChild<QLabel *>(QStringLiteral("paneTitle"));
    CHECK(title != nullptr);
    if (title) CHECK(copy->geometry().left() - title->geometry().right() < 24);
    if (const QString shotDir = qEnvironmentVariable("RELAY_SHOT_DIR"); !shotDir.isEmpty() && pane.headerWidget())
        pane.headerWidget()->grab().save(shotDir + QStringLiteral("/header-session-copy.png"));
    QApplication::clipboard()->clear();
    copy->click();
    CHECK_EQ(QApplication::clipboard()->text(), id);
    pane.deliverWorkerEvent({{"event", "reset"}});
    CHECK(!copy->isVisibleTo(&pane));
}

void theInfoOverlayAnswersFromThePanesWorker()
{
    Pane pane(home->path(), home->path(), true);
    pane.resize(800, 500);
    pane.show();
    QToolButton anchor(&pane);
    anchor.setGeometry(760, 4, 22, 22);
    anchor.show();
    auto *overlay = new relay::sessioninfo::InfoOverlay(&pane, &anchor, pane.sessionToken().left(8));
    pane.bindInfoOverlay(overlay);
    int closed = 0;
    overlay->onClosed = [&closed] { ++closed; };
    overlay->open();
    CHECK(overlay->isVisible());
    CHECK(overlay->owns(QStringLiteral("info-overlay-1")));
    pane.deliverWorkerEvent({{"event", "session_info"}, {"id", "info-overlay-1"}, {"kind", "session"},
                             {"live", true}, {"title", "Chrome row"}, {"model", "glm-5"},
                             {"session_id", "7ewf0a1b2c3d4e5f60718293a4b5c6d7"},
                             {"usage", QJsonObject{{"prompt_tokens", 1200}, {"total_tokens", 1500}, {"requests", 2}}},
                             {"history", QJsonArray{QJsonObject{{"turn", 1}, {"prompt", "secret history line"}}}}});
    const QString html = overlay->html();
    CHECK(html.contains(QStringLiteral("Chrome row")));
    CHECK(html.contains(QStringLiteral("7ewf0a1b2c3d4e5f60718293a4b5c6d7")));
    CHECK(html.contains(QStringLiteral("pane ") + pane.sessionToken().left(8)));
    CHECK(!html.contains(QStringLiteral("Cost")));
    CHECK(!html.contains(QStringLiteral("History")));
    CHECK(!html.contains(QStringLiteral("secret history line")));
    // Under the ⓘ, and inside the pane.
    CHECK(overlay->geometry().top() >= anchor.geometry().bottom());
    CHECK(pane.rect().contains(overlay->geometry()));
    // A click on the ⓘ is left to the ⓘ (it toggles); a click elsewhere in the pane closes it.
    chromePress(&anchor, QPoint(5, 5));
    CHECK(overlay->isVisible());
    chromePress(&pane, QPoint(20, 300));
    CHECK(!overlay->isVisible());
    CHECK_EQ(closed, 1);
    // Esc closes it too, and toggle() is the ⓘ's and Alt+I's open-or-close.
    overlay->toggle();
    CHECK(overlay->isVisible());
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(overlay, &escape);
    CHECK(!overlay->isVisible());
    CHECK_EQ(closed, 2);
    const QString shotDir = qEnvironmentVariable("RELAY_SHOT_DIR");
    if (!shotDir.isEmpty()) {
        overlay->open();
        pane.deliverWorkerEvent({{"event", "session_info"}, {"id", "info-overlay-3"}, {"kind", "session"},
                                 {"live", true}, {"title", "Chrome row"}, {"model", "glm-5"},
                                 {"session_id", "7ewf0a1b2c3d4e5f60718293a4b5c6d7"},
                                 {"context", QJsonObject{{"used_tokens", 41200}, {"window", 200000}, {"percent", 20.6}}},
                                 {"turns", 4}, {"created", double(QDateTime::currentSecsSinceEpoch() - 900)},
                                 {"usage", QJsonObject{{"prompt_tokens", 41200}, {"total_tokens", 43800}, {"requests", 4}}}});
        QApplication::processEvents();
        pane.grab().save(shotDir + QStringLiteral("/info-overlay.png"));
        overlay->close();
    }
}

void paneInfoChromeCases()
{
    theChromeRowIsInfoMenuNewPaneClose();
    aToolPanesMenuHasNoBackground();
    theHeaderOffersTheSessionIdInsteadOfAnAutoBadge();
    theInfoOverlayAnswersFromThePanesWorker();
}

}  // namespace cases
