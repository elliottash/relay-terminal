#pragma once

#include <QApplication>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QTabBar>
#include <QTextEdit>
#include <QVariant>
#include <QWidget>

namespace relay::paneTabs {

// The bar must remain below its host. Only compare the owner pointer; never
// dereference a stored pointer, so deleting/replacing a bar needs no cleanup.
inline void registerTabs(QWidget *host, QTabBar *bar)
{
    if (!host || !bar || (host != bar && !host->isAncestorOf(bar))) return;
    host->setProperty("paneTabNavigationHost", true);
    bar->setProperty("paneTabNavigationOwner", QVariant::fromValue<QObject *>(host));
}

inline bool handle(QWidget *focused, QKeyEvent *event)
{
    if (!focused || !event ||
        (event->type() != QEvent::ShortcutOverride && event->type() != QEvent::KeyPress))
        return false;
    if (event->key() != Qt::Key_Tab && event->key() != Qt::Key_Backtab) return false;
    if (event->modifiers() != Qt::NoModifier && event->modifiers() != Qt::ShiftModifier)
        return false;
    // Completion menus and native popups own their navigation, even when the
    // focus widget remains the line edit that opened them.
    if (QApplication::activePopupWidget()) return false;
    for (QWidget *widget = focused; widget; widget = widget->parentWidget()) {
        if (widget->property("paneTabKeysReserved").toBool() ||
            widget->windowType() == Qt::Popup) return false;
        if (auto *editor = qobject_cast<QPlainTextEdit *>(widget)) {
            if (!editor->isReadOnly()) return false;
        }
        if (auto *editor = qobject_cast<QTextEdit *>(widget)) {
            if (!editor->isReadOnly()) return false;
        }
    }
    const int step = (event->key() == Qt::Key_Backtab ||
                      event->modifiers() == Qt::ShiftModifier) ? -1 : 1;
    for (QWidget *host = focused; host; host = host->parentWidget()) {
        if (!host->property("paneTabNavigationHost").toBool()) continue;
        auto bars = host->findChildren<QTabBar *>();
        if (auto *ownBar = qobject_cast<QTabBar *>(host)) bars.prepend(ownBar);
        for (QTabBar *bar : bars) {
            if (bar->property("paneTabNavigationOwner").value<QObject *>() != host ||
                !bar->isVisible() || !bar->isEnabled()) continue;
            int available = 0;
            for (int i = 0; i < bar->count(); ++i)
                if (bar->isTabEnabled(i) && bar->isTabVisible(i)) ++available;
            if (available < 2) continue;
            const int count = bar->count();
            int index = bar->currentIndex();
            for (int tried = 0; tried < count; ++tried) {
                index = (index + step + count) % count;
                if (!bar->isTabEnabled(index) || !bar->isTabVisible(index)) continue;
                event->accept();
                if (event->type() == QEvent::KeyPress) bar->setCurrentIndex(index);
                return true;
            }
        }
    }
    return false;
}

} // namespace relay::paneTabs
