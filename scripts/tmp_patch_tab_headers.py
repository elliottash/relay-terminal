#!/usr/bin/env python3
"""Remove the per-tab theme swatch and the tab-header detach button (RelayWindow.h)."""
import sys
sys.path.insert(0, "scripts")
from tmp_exact_patch import apply

SWATCH_FILTER = '''    bool eventFilter(QObject *object, QEvent *event) override {
        // Theme swatches on the tabs: let the bar paint itself, then paint over it.
        if (object == m_tabs->tabBar() && event->type() == QEvent::Paint && !m_paintingTabSwatches
            && perTabThemes() && m_tabs->count() > 1) {
            m_paintingTabSwatches = true;
            QCoreApplication::sendEvent(object, event);
            m_paintingTabSwatches = false;
            paintTabSwatches();
            return true;
        }
        // Tab labels (issue JRWQ): double click a tab to name it by hand; Esc leaves it alone.'''

SWATCH_FILTER_NEW = '''    bool eventFilter(QObject *object, QEvent *event) override {
        // Tab labels (issue JRWQ): double click a tab to name it by hand; Esc leaves it alone.'''

SWATCH_PAINT = '''    // Each tab wears its theme as a swatch at its left edge: the theme's terminal ground over its
    // accent, outlined in its own strong border so a charcoal swatch still shows on charcoal
    // chrome. Painted over the tab bar after it has painted itself.
    void paintTabSwatches() {
        QTabBar *bar = m_tabs->tabBar();
        QPainter p(bar);
        p.setRenderHint(QPainter::Antialiasing);
        for (int i = 0; i < bar->count(); ++i) {
            QString id = tabThemeOf(m_tabs->widget(i));
            if (id.isEmpty()) id = relay::theme::startupThemeId();
            const relay::theme::ThemeSpec spec = relay::theme::specFor(id);
            const QRect tab = bar->tabRect(i);
            const QRectF swatch(tab.left() + 0.5, tab.top() + 6.5, 6, qMax(8, tab.height() - 11));
            const QColor ground = spec.terminalBackground.isValid() ? spec.terminalBackground : spec.uiColor(QStringLiteral("background"));
            QPainterPath shape; shape.addRoundedRect(swatch, 2, 2);
            p.save();
            p.setClipPath(shape);
            p.fillRect(swatch, ground);
            p.fillRect(QRectF(swatch.left(), swatch.bottom() - swatch.height() * 0.34, swatch.width(), swatch.height() * 0.34 + 1),
                       spec.uiColor(QStringLiteral("accent")));
            p.restore();
            p.setPen(QPen(spec.uiColor(QStringLiteral("border_strong")), 1));
            p.setBrush(Qt::NoBrush);
            p.drawPath(shape);
        }
    }

    QJsonObject serializeTab(int index) const {'''

SWATCH_PAINT_NEW = '''    QJsonObject serializeTab(int index) const {'''

SWATCH_FLAG = '''    bool m_paintingTabSwatches = false;
'''

SWATCH_OPTION_OLD = '''                                     QStringLiteral("/light, /dark and /theme change the tab you are in, switching tabs switches "
                                                    "the theme, and each tab shows its theme as a swatch"),'''

SWATCH_OPTION_NEW = '''                                     QStringLiteral("/light, /dark and /theme change the tab you are in; switching tabs switches "
                                                    "the theme"),'''

DETACH_BLOCK = '''        // A "move to new window" button on each tab, visible on the hovered tab.
        for (int i = 0; i < bar->count(); ++i) {
            // The tab's left slot holds a box (tabLeftBox): this button first, then the project
            // chip of an attached tab (#916B).
            QWidget *box = tabLeftBox(i);
            auto *detach = box->findChild<QToolButton *>(QStringLiteral("tabDetachButton"), Qt::FindDirectChildrenOnly);
            if (!detach) {
                detach = new QToolButton(box);
                detach->setObjectName(QStringLiteral("tabDetachButton"));
                detach->setText(QStringLiteral("\u29c9"));
                detach->setAutoRaise(true);
                detach->setFocusPolicy(Qt::NoFocus);
                detach->setFixedSize(18, 18);
                connect(detach, &QToolButton::clicked, this, [this, detach] {
                    QTabBar *tabs = m_tabs->tabBar();
                    for (int j = 0; j < tabs->count(); ++j)
                        if (tabs->tabButton(j, QTabBar::LeftSide) == detach->parentWidget()) { moveTabToNewWindow(j); break; }
                    const QString keys = Keymap::instance().shortcutText(QStringLiteral("tab.moveToNewWindow"));
                    hint(QStringLiteral("tab.detach.mouse"), keys.isEmpty() ? QStringLiteral("Tip: \u201cMove tab to new window\u201d is in the palette; bind a key in keybindings.json")
                                                                          : relay::ShortcutHints::nextTime(keys, QStringLiteral("move tab to new window")));
                });
                if (auto *row = qobject_cast<QHBoxLayout *>(box->layout())) row->insertWidget(0, detach);
                detach->show();
                relayoutTabLeftBox(i);
            }
            const QString keys = Keymap::instance().shortcutText(QStringLiteral("tab.moveToNewWindow"));
            detach->setToolTip(keys.isEmpty() ? QStringLiteral("Move tab to new window") : QStringLiteral("Move tab to new window  (%1)").arg(keys));
            const bool hovered = bar->tabAt(bar->mapFromGlobal(QCursor::pos())) == i && bar->underMouse();
            // Keep the space reserved so tabs do not jump; only the glyph appears on hover.
            detach->setEnabled(m_tabs->count() > 1);
            detach->setProperty("hovered", hovered);
            detach->setText(hovered ? QStringLiteral("\u29c9") : QString());

            // Relay's own close cross.'''

DETACH_BLOCK_NEW = '''        for (int i = 0; i < bar->count(); ++i) {
            const bool hovered = bar->tabAt(bar->mapFromGlobal(QCursor::pos())) == i && bar->underMouse();

            // Relay's own close cross.'''

CHIP_COMMENT_OLD = '''    // detaches. An unattached tab shows nothing at all: there is no "not attached" state to
    // advertise, because unattached is the ordinary state. It lives in the tab's left box beside
    // the \u29c9 "move to new window" button (placeTabBarControls), so it moves with the tab and goes
    // with it.'''

CHIP_COMMENT_NEW = '''    // detaches. An unattached tab shows nothing at all: there is no "not attached" state to
    // advertise, because unattached is the ordinary state. It lives in the tab's left box
    // (tabLeftBox), so it moves with the tab and goes with it.'''

CHIP_ADD_OLD = '''        box->layout()->addWidget(chip);   // after the \u29c9 button, when that has been made'''

CHIP_ADD_NEW = '''        box->layout()->addWidget(chip);'''

apply("src/RelayWindow.h", [
    (SWATCH_FILTER, SWATCH_FILTER_NEW),
    (SWATCH_PAINT, SWATCH_PAINT_NEW),
    (SWATCH_FLAG, ""),
    (SWATCH_OPTION_OLD, SWATCH_OPTION_NEW),
    (DETACH_BLOCK, DETACH_BLOCK_NEW),
    (CHIP_COMMENT_OLD, CHIP_COMMENT_NEW),
    (CHIP_ADD_OLD, CHIP_ADD_NEW),
])
