// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QColor>
#include <QPainter>
#include <QWidget>
#include <algorithm>

namespace relay::dimming {

// Manual intent survives focus and attention overrides. Completion never cancels it.
struct State {
    int manual = -1; // -1 follows automatic/focus mode; 0 explicitly stays bright
    bool active = false, revealed = false, working = false;
    bool attention = false, completed = false;
    int wheelRemainder = 0;

    void observe(bool focused, bool busy, bool needsYou, bool done) {
        if (focused != active) revealed = focused;
        // Submitting work while still focused must hide it, until deliberately re-entered.
        if (busy && !working) revealed = false;
        active = focused;
        working = busy;
        attention = needsYou;
        completed = done;
    }
    int amount(bool automatic, bool focus, int strength, bool includeActive = false) const {
        if (attention) return 0;
        const bool autoDim = automatic && working && (!active || includeActive);
        // Opted-in working panes stay dim even when entered. Manual reveal still wins.
        if (revealed && !(manual < 0 && autoDim)) return 0;
        if (manual >= 0) return std::clamp(manual, 0, 95);
        if (completed) return 0;
        return (autoDim || (focus && !active))
                   ? std::clamp(strength, 0, 95) : 0;
    }
    void toggle(int strength) {
        manual = manual > 0 ? -1 : std::clamp(strength, 0, 95);
        revealed = manual < 0 && active;
    }
    void adjust(int delta, int current) {
        manual = std::clamp(current + delta, 0, 95);
        revealed = false;
    }
    int wheelSteps(const QPoint &delta) {
        // Qt/X11 translates Alt+vertical-wheel into horizontal angle deltas.
        // Prefer vertical when both are present so a diagonal gesture counts once.
        wheelRemainder += delta.y() ? delta.y() : delta.x();
        const int steps = wheelRemainder / 120;
        wheelRemainder %= 120;
        return steps;
    }
};

// An input-transparent layer leaves rendering, selection and execution underneath intact.
class Overlay final : public QWidget {
public:
    explicit Overlay(QWidget *leaf) : QWidget(leaf) {
        setObjectName(QStringLiteral("paneDimOverlay"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
        hide();
    }
    void refresh(int amount, const QColor &background, int headerHeight) {
        const QColor target = background.lightnessF() > 0.5 ? QColor(Qt::white) : QColor(Qt::black);
        const QRect bounds(0, headerHeight, parentWidget()->width(),
                           std::max(0, parentWidget()->height() - headerHeight));
        if (m_amount != amount || m_color != target || geometry() != bounds) {
            m_amount = amount; m_color = target;
            setGeometry(bounds);
            update();
        }
        setVisible(amount > 0);
        if (amount > 0) raise();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        QColor fill = m_color;
        fill.setAlphaF(m_amount / 100.0);
        painter.fillRect(rect(), fill);
    }
private:
    int m_amount = 0;
    QColor m_color;
};
} // namespace relay::dimming
