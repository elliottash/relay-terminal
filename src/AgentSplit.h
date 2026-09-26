#pragma once

#include <QEvent>
#include <QPainter>
#include <QSplitter>
#include <QSplitterHandle>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <functional>

namespace relay {

// ----- the divider between an artifact and its docked agent (card #ZPHJ) ------------------------
//
// The owner, 2026-09-25: "make the split with the artifact agent moveable. so you can see more of
// the thread / COT if you want, without popping it out." Until then the file panes capped their
// docked agent at 45 % of the pane and the Card page capped its console at 40 %, in plain vertical
// layouts nobody could move. This is the one splitter all three use: the artifact on top (the
// editor, the preview, the card's document), the agent under it.
//
// What it adds to a QSplitter:
// - **A share, not pixels.** The agent's part of the height while it is open, 0..1, kept across
//   resizes and saved with the pane's layout leaf. Unset (-1) is the pane's default.
// - **Only a drag is remembered.** `splitterMoved` is emitted by the handle and never by
//   `setSizes`, so the sizing done here and by restore cannot save a folded or zero size.
// - **Folded.** The file agent folds to one "✦ Agent" row and a Card page has no console before
//   the window builds one: then the agent gets its own size hint, the handle does not drag, and the
//   share is left alone, so unfolding comes back to the size that was chosen.
class AgentSplit : public QSplitter {
public:
    static constexpr double kMinShare = 0.05;
    static constexpr double kMaxShare = 0.95;

    explicit AgentSplit(double defaultShare, QWidget *parent = nullptr)
        : QSplitter(Qt::Vertical, parent), m_default(defaultShare) {
        setObjectName(QStringLiteral("agentSplit"));
        setChildrenCollapsible(false);
        setHandleWidth(6);
        connect(this, &QSplitter::splitterMoved, this, [this](int, int) {
            if (remember() && onUserMoved)
                onUserMoved();
        });
    }

    void setContent(QWidget *content) {
        insertWidget(0, content);
        setStretchFactor(0, 1);
    }
    void setAgent(QWidget *agent) {
        addWidget(agent);
        setStretchFactor(indexOf(agent), 0);
        agent->installEventFilter(this);
        updateHandle();
    }

    static bool validShare(double share) {
        return std::isfinite(share) && share >= kMinShare && share <= kMaxShare;
    }
    // The chosen share, or -1 while the pane still has its default.
    double agentShare() const { return m_share; }
    double effectiveShare() const { return m_share > 0 ? m_share : m_default; }
    // A saved share; anything out of range is the default again.
    void setAgentShare(double share) {
        m_share = validShare(share) ? share : -1;
        apply();
    }

    bool agentFolded() const { return m_folded; }
    void setAgentFolded(bool folded) {
        if (m_folded == folded)
            return;
        m_folded = folded;
        updateHandle();
        apply();
    }

    // A drag moved the divider; the window saves its layout (debounced) from here.
    std::function<void()> onUserMoved;

    // Put the agent at its share (open) or its size hint (folded). Programmatic, so never saved.
    void apply() {
        if (count() < 2)
            return;
        QWidget *content = widget(0), *agent = widget(1);
        if (content->isHidden() || agent->isHidden())
            return;
        const QList<int> now = sizes();
        const int total = now.value(0) + now.value(1);
        if (total <= 0)
            return;
        int want = m_folded ? std::max(agent->sizeHint().height(), agent->minimumSizeHint().height())
                            : int(std::lround(effectiveShare() * total));
        want = std::clamp(want, 0, total);
        if (now.value(1) != want)
            setSizes({total - want, want});
    }

protected:
    QSplitterHandle *createHandle() override { return new Handle(orientation(), this); }

    void resizeEvent(QResizeEvent *event) override {
        QSplitter::resizeEvent(event);
        apply();
    }
    void showEvent(QShowEvent *event) override {
        QSplitter::showEvent(event);
        apply();
    }
    // The agent shown again, or a folded agent whose row changed size: sized once the event is
    // done, not inside it.
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (count() > 1 && watched == widget(1)
            && (event->type() == QEvent::Show || (m_folded && event->type() == QEvent::LayoutRequest))
            && !m_pending) {
            m_pending = true;
            QTimer::singleShot(0, this, [this] {
                m_pending = false;
                apply();
            });
        }
        return QSplitter::eventFilter(watched, event);
    }

private:
    // A short grip in the middle of the handle, so the divider reads as something to take hold of
    // and not as a gap. None while folded: that handle does not move.
    class Handle : public QSplitterHandle {
    public:
        Handle(Qt::Orientation orientation, QSplitter *parent) : QSplitterHandle(orientation, parent) {}

    protected:
        void paintEvent(QPaintEvent *event) override {
            QSplitterHandle::paintEvent(event);
            if (!isEnabled())
                return;
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing);
            QColor grip = palette().color(underMouse() ? QPalette::Highlight : QPalette::WindowText);
            grip.setAlpha(underMouse() ? 200 : 90);
            painter.setPen(Qt::NoPen);
            painter.setBrush(grip);
            const int length = std::min(36, width() / 3);
            const int thick = std::max(2, std::min(3, height() - 2));
            painter.drawRoundedRect(QRectF((width() - length) / 2.0, (height() - thick) / 2.0, length, thick),
                                    thick / 2.0, thick / 2.0);
        }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        void enterEvent(QEnterEvent *event) override
#else
        void enterEvent(QEvent *event) override
#endif
        {
            QSplitterHandle::enterEvent(event);
            update();
        }
        void leaveEvent(QEvent *event) override {
            QSplitterHandle::leaveEvent(event);
            update();
        }
    };

    bool remember() {
        if (count() < 2 || m_folded)
            return false;
        const QList<int> now = sizes();
        const int total = now.value(0) + now.value(1);
        if (total <= 0 || now.value(1) <= 0)
            return false;
        m_share = std::clamp(double(now.value(1)) / total, kMinShare, kMaxShare);
        return true;
    }
    void updateHandle() {
        if (count() < 2)
            return;
        QSplitterHandle *grip = handle(1);
        grip->setEnabled(!m_folded);
        grip->setCursor(m_folded ? Qt::ArrowCursor : Qt::SplitVCursor);
        grip->update();
    }

    double m_default = 0.4;
    double m_share = -1;
    bool m_folded = false;
    bool m_pending = false;
};

}  // namespace relay
