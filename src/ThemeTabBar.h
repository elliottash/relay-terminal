// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// The window's tab strip and its per-tab theme marks (owner, 2026-09-20: "can we color the other
// inactive tabs with their respective themes", of the clickable tab headers). A tab owns a theme
// through its page's "relayTheme" property (docs/ARCHITECTURE.md, "themes per tab"), and the tab
// in front decides what the whole window wears — so while another tab is in front, every inactive
// tab that owns a different theme is washed in that theme's accent and carries a 2px strip of it
// along the bottom, echoing the selected tab's accent underline (src/Theme.cpp,
// QTabBar::tab:selected). A layout where tabs have their own themes — the random or the cycling
// new-tab mode, or /theme used tab by tab — then reads at a glance: the header is the theme.
//
// A tab with no theme of its own follows the default and is drawn like any other; a tab whose
// theme is the one in front needs no mark, because everything around it already says it.

#include "Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QSettings>
#include <QTabBar>
#include <QTabWidget>

class ThemeTabBar final : public QTabBar {
public:
    using QTabBar::QTabBar;

protected:
    void paintEvent(QPaintEvent *event) override {
        QTabBar::paintEvent(event);
        // The same key RelayWindow::perTabThemes() reads. With per-tab themes off a page's
        // "relayTheme" is inert — applyTabTheme() ignores it — so the strip ignores it too,
        // or the mark would name a theme the tab will never show.
        if (!QSettings().value(QStringLiteral("theme/per_tab"), true).toBool()) return;
        auto *tabs = qobject_cast<QTabWidget *>(parentWidget());
        if (!tabs) return;
        QPainter p(this);
        for (int i = 0; i < count(); ++i) {
            if (i == currentIndex()) continue;
            const QWidget *page = tabs->widget(i);
            const QString id = page ? page->property("relayTheme").toString() : QString();
            if (id.isEmpty() || id == relay::theme::activeThemeId()) continue;
            const relay::theme::ThemeSpec spec = relay::theme::specFor(id);
            if (spec.id != id) continue;   // the tab's theme is no longer installed
            QColor accent = spec.uiColor(QStringLiteral("accent"));
            if (!accent.isValid()) continue;
            const QRect r = tabRect(i);
            // The wash is quiet so the tab's own ground and ink stay readable; the strip says
            // the theme out loud, at the weight the selected tab gives its own accent. Both are
            // the accent and nothing else: it is the one token every theme has that exists to be
            // recognised, and it keeps the row from becoming a patchwork of theme backgrounds.
            QColor wash(accent);
            wash.setAlphaF(0.16);
            QColor strip(accent);
            strip.setAlphaF(0.85);
            // QTabBar::tab rounds only the top corners (6px), so clip the wash to a path whose
            // rounded bottom lies below the rect: rounded top, square bottom.
            QPainterPath clip;
            clip.addRoundedRect(QRectF(r).adjusted(0, 0, 0, 6), 6, 6);
            p.setClipPath(clip);
            p.fillRect(r, wash);
            p.setClipPath({});
            p.fillRect(QRect(r.left(), r.bottom() - 1, r.width(), 2), strip);
        }
    }
};

// QTabWidget::setTabBar is protected, so the window's tab widget is a class only to install one.
class WindowTabWidget final : public QTabWidget {
public:
    explicit WindowTabWidget(QWidget *parent = nullptr) : QTabWidget(parent) {
        setTabBar(new ThemeTabBar(this));
    }
};
