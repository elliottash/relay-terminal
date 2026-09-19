// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The two small widgets that sit around a pane: ToolPane, the non-terminal pane (folder explorer,
// file preview, plan, transcript, Switchboard, settings), and PaneChrome, the button row and drag
// grip in every pane's top-right corner. PaneChrome asks the leaf it is parented to for the room it
// needs, which is the one thing here that has to know what a Pane is -- hence the include.

#include "Pane.h"

#include "FilePanes.h"
#include "BoardPane.h"
#include "ScreenPrompt.h"
#include "TurnTranscript.h"
#include "SettingsPane.h"
#include "SubagentTranscript.h"
#include "OutputLinks.h"
#include "PaneView.h"

#include "PaneStatus.h"
#include "PaneUsage.h"
#include "Theme.h"

#include <QApplication>
#include <QDynamicPropertyChangeEvent>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSettings>
#include <QTimer>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QShowEvent>
#include <QToolButton>
#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QTreeView>

#include <functional>

// ----- pane types and pane states: the painting (cards #SPBN, #XM0T) ----------------------------
// The rules (which type gets which tint, which state wins) are in src/PaneStatus.{h,cpp}; this is
// how they look. Everything reads the live theme tokens at paint time, so a theme switch needs no
// rebuild, and every glyph is painted, not typed, for the reason ChromeButton gives.
namespace relay::chrome {
namespace ps = relay::panestatus;

inline ps::Tokens tokens() {
    namespace t = relay::theme;
    return {t::Background, t::Text,    t::TextMuted, t::Shell, t::Agent,
            t::Success,    t::Warning, t::Error,     t::Action, t::Tool};
}

// "appearance/pane_colours": type (default), group or off. Read once and cached; the Options pane
// writes the key and then calls PaneChrome::refreshAll(), which reloads it.
inline ps::ColourMode &colourModeCache() {
    static ps::ColourMode mode = ps::colourModeFrom(QSettings().value(QStringLiteral("appearance/pane_colours")).toString());
    return mode;
}
inline ps::ColourMode colourMode() { return colourModeCache(); }
inline void reloadColourMode() {
    colourModeCache() = ps::colourModeFrom(QSettings().value(QStringLiteral("appearance/pane_colours")).toString());
}

inline ps::TypeStyle styleOf(const QWidget *leaf) {
    if (!leaf) return {};
    return ps::typeStyle(leaf->property("paneType").toString(), colourMode(), tokens(), leaf->property("paneLabel").toString());
}

// The pane's frame, so a band painted inside it meets the outline and the rounded corners exactly.
inline qreal paneRadius() { return relay::theme::active().flag(QStringLiteral("square")) ? 0.0 : 7.0; }

// A rectangle with only its top corners rounded: the top of a pane.
inline QPainterPath topBand(const QRectF &r, qreal radius) {
    QPainterPath path;
    radius = std::min(radius, r.height() / 2);
    path.moveTo(r.left(), r.bottom());
    path.lineTo(r.left(), r.top() + radius);
    path.arcTo(QRectF(r.left(), r.top(), 2 * radius, 2 * radius), 180, -90);
    path.lineTo(r.right() - radius, r.top());
    path.arcTo(QRectF(r.right() - 2 * radius, r.top(), 2 * radius, 2 * radius), 90, -90);
    path.lineTo(r.right(), r.bottom());
    path.closeSubpath();
    return path;
}

inline QPainterPath fourPointStar(const QPointF &c, qreal outer, qreal inner) {
    QPainterPath star;
    for (int i = 0; i < 8; ++i) {
        const qreal angle = -M_PI / 2 + i * M_PI / 4;
        const qreal r = i % 2 ? inner : outer;
        const QPointF p = c + QPointF(std::cos(angle), std::sin(angle)) * r;
        if (i == 0) star.moveTo(p); else star.lineTo(p);
    }
    star.closeSubpath();
    return star;
}

// A pane type's glyph, drawn for a 14 px box and scaled from there.
inline void paintTypeGlyph(QPainter &p, const QRectF &box, ps::Glyph glyph, const QColor &ink) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const qreal u = std::min(box.width(), box.height()) / 14.0;
    const QPointF c = box.center();
    auto at = [&](qreal x, qreal y) { return c + QPointF(x, y) * u; };
    const QPen pen(ink, std::max(1.0, 1.25 * u), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen); p.setBrush(Qt::NoBrush);
    switch (glyph) {
    case ps::Glyph::Switchboard:
        // Two rows of jacks, two of them patched.
        for (int row = 0; row < 2; ++row)
            for (int col = 0; col < 3; ++col) {
                const bool patched = (row == 0 && col == 0) || (row == 1 && col == 2);
                p.setBrush(patched ? QBrush(ink) : Qt::NoBrush);
                p.drawEllipse(at(-4.5 + col * 4.5, -2.6 + row * 5.2), 1.7 * u, 1.7 * u);
            }
        break;
    case ps::Glyph::Options: {
        // The gear, as on the title-bar button that opens Options (ChromeButton::paintGear): six
        // teeth around a ring, one outline, and the hub.
        constexpr int teeth = 6;
        const qreal inner = 4.6 * u, outer = 6.4 * u, half = M_PI / teeth;
        QPolygonF cog;
        for (int i = 0; i < teeth; ++i) {
            const qreal base = i * 2 * half;
            const qreal angle[4] = {base - half * 0.60, base - half * 0.34, base + half * 0.34, base + half * 0.60};
            const qreal radius[4] = {inner, outer, outer, inner};
            for (int k = 0; k < 4; ++k) cog << c + QPointF(std::cos(angle[k]), std::sin(angle[k])) * radius[k];
        }
        p.drawPolygon(cog);
        p.drawEllipse(c, 2.2 * u, 2.2 * u);
        break;
    }
    case ps::Glyph::Actions: {
        // A bolt: the pane that runs things.
        const QPolygonF bolt(QVector<QPointF>{at(1.5, -6.2), at(-4.2, 0.8), at(-0.2, 0.8), at(-1.5, 6.2), at(4.2, -0.8), at(0.2, -0.8)});
        p.setBrush(ink);
        p.drawPolygon(bolt);
        break;
    }
    case ps::Glyph::Sessions:
        // A list of conversations.
        for (int i = 0; i < 3; ++i) {
            const qreal y = -4.2 + i * 4.2;
            p.setBrush(ink);
            p.drawEllipse(at(-4.6, y), 1.1 * u, 1.1 * u);
            p.drawLine(at(-1.8, y), at(5.4, y));
        }
        break;
    case ps::Glyph::Subagent: {
        // A parent and the two agents it started.
        QPainterPath tree;
        tree.moveTo(at(-3.6, -3.2)); tree.lineTo(at(-3.6, 4.0)); tree.lineTo(at(1.6, 4.0));
        tree.moveTo(at(-3.6, 0.4)); tree.lineTo(at(1.6, 0.4));
        p.drawPath(tree);
        p.setBrush(ink);
        p.drawPath(fourPointStar(at(-3.6, -3.6), 2.9 * u, 1.0 * u));
        p.drawEllipse(at(3.6, 0.4), 1.6 * u, 1.6 * u);
        p.drawEllipse(at(3.6, 4.0), 1.6 * u, 1.6 * u);
        break;
    }
    case ps::Glyph::Turn: {
        // A speech bubble with two lines of text.
        QPainterPath bubble;
        bubble.addRoundedRect(QRectF(at(-6, -5.2), at(6, 3.0)), 2.2 * u, 2.2 * u);
        p.drawPath(bubble);
        p.drawLine(at(-3.2, 5.8), at(-1.2, 3.0));
        p.drawLine(at(-3.2, -2.2), at(3.2, -2.2));
        p.drawLine(at(-3.2, 0.2), at(1.2, 0.2));
        break;
    }
    case ps::Glyph::Remote:
        // ⇄: what you type goes out, what it prints comes back.
        p.drawLine(at(-5.5, -2.6), at(5.5, -2.6));
        p.drawPolyline(QPolygonF(QVector<QPointF>{at(2.6, -5.2), at(5.5, -2.6), at(2.6, 0.0)}));
        p.drawLine(at(5.5, 2.6), at(-5.5, 2.6));
        p.drawPolyline(QPolygonF(QVector<QPointF>{at(-2.6, 0.0), at(-5.5, 2.6), at(-2.6, 5.2)}));
        break;
    case ps::Glyph::Phone:
        p.drawRoundedRect(QRectF(at(-3.8, -6.2), at(3.8, 6.2)), 1.6 * u, 1.6 * u);
        p.setBrush(ink);
        p.drawEllipse(at(0, 3.9), 0.9 * u, 0.9 * u);
        break;
    case ps::Glyph::Tool:
        p.drawRoundedRect(QRectF(at(-5, -5), at(5, 5)), 2 * u, 2 * u);
        p.setBrush(ink);
        p.drawEllipse(c, 1.6 * u, 1.6 * u);
        break;
    case ps::Glyph::None: break;
    }
    p.restore();
}

// The Relay mark, one ink: the cord chevron, the dash and the seated tip, redrawn from
// data/icons/org.relayterminal.Relay-symbolic.svg (16x16) into the glyph box. It is the live
// states' glyph (card #4E13: "a blue blinking relay icon (terminal program running) or purple
// blinking relay icon (agent working)") — the app saying its own name where work is happening.
// `ground` fills the ring's hole, as the other filled shapes do for their cut marks.
inline void paintRelayMark(QPainter &p, const QRectF &box, const QColor &ink, const QColor &ground) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const qreal u = std::min(box.width(), box.height()) / 16.0;
    auto at = [&](qreal x, qreal y) { return box.topLeft() + QPointF(x, y) * u; };
    // The cord: a > shape, stroked with round ends as the icon's own is.
    p.setPen(QPen(ink, 1.9 * u, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPolyline(QPolygonF(QVector<QPointF>{at(2.6, 3.6), at(6.4, 8.0), at(2.6, 12.4)}));
    // The dash.
    p.setPen(Qt::NoPen);
    p.setBrush(ink);
    p.drawRoundedRect(QRectF(at(7.2, 7.05), at(11.0, 8.95)), 0.95 * u, 0.95 * u);
    // The seated tip: a filled ring with its hole in the ground's colour.
    p.drawEllipse(at(12.9, 8.0), 2.35 * u, 2.35 * u);
    p.setBrush(ground);
    p.drawEllipse(at(12.9, 8.0), 1.15 * u, 1.15 * u);
    p.restore();
}

// A pane state's glyph. Each has its own shape so it reads without colour: a ring (idle), the
// Relay mark for work happening now (running, working, subagents — card #4E13), a prompt chevron
// (a suggested command), a tick (done), a disc with a cross (failed) and a diamond with an
// exclamation mark (needs you). `ground` is what is under it, for the marks cut into a filled shape.
// A live state blinks by `scale` (pulseScale, cards #V8KT, #4E13) — a scale, never an opacity, so
// the ink keeps its contrast at every step of the pulse.
inline void paintStateGlyph(QPainter &p, const QRectF &box, ps::State state, const QColor &ink, const QColor &ground,
                            qreal scale = 1.0) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF sized = scale >= 1.0 ? box
        : QRectF(box.center() - QPointF(box.width(), box.height()) * scale / 2,
                 QSizeF(box.width(), box.height()) * scale);
    const qreal u = std::min(sized.width(), sized.height()) / 14.0;
    const QPointF c = sized.center();
    auto at = [&](qreal x, qreal y) { return c + QPointF(x, y) * u; };
    QPen pen(ink, std::max(1.0, 1.4 * u), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen); p.setBrush(Qt::NoBrush);
    switch (state) {
    case ps::State::Idle:
        p.drawEllipse(c, 3.6 * u, 3.6 * u);
        break;
    case ps::State::Running:
    case ps::State::Working:
    case ps::State::Subagents:
        // Work happening now is the product's own mark, in the work's colour (#4E13). The
        // subagent badge beside it says how many; the word beside it says whose work it is.
        p.restore();
        paintRelayMark(p, sized, ink, ground);
        p.save();
        break;
    case ps::State::Recommends:
        p.drawPolyline(QPolygonF(QVector<QPointF>{at(-5, -4), at(-1, 0), at(-5, 4)}));
        p.drawLine(at(1, 4.2), at(5.5, 4.2));
        break;
    case ps::State::Done:
        pen.setWidthF(std::max(1.2, 1.8 * u)); p.setPen(pen);
        p.drawPolyline(QPolygonF(QVector<QPointF>{at(-4.8, 0.4), at(-1.4, 3.8), at(5.0, -3.6)}));
        break;
    case ps::State::Failed:
        p.setPen(Qt::NoPen); p.setBrush(ink);
        p.drawEllipse(c, 6.0 * u, 6.0 * u);
        p.setPen(QPen(ground, std::max(1.2, 1.6 * u), Qt::SolidLine, Qt::RoundCap));
        p.drawLine(at(-2.5, -2.5), at(2.5, 2.5));
        p.drawLine(at(2.5, -2.5), at(-2.5, 2.5));
        break;
    case ps::State::NeedsYou:
        p.setPen(Qt::NoPen); p.setBrush(ink);
        p.drawPolygon(QPolygonF(QVector<QPointF>{at(0, -6.6), at(6.6, 0), at(0, 6.6), at(-6.6, 0)}));
        p.setPen(QPen(ground, std::max(1.2, 1.7 * u), Qt::SolidLine, Qt::RoundCap));
        p.drawLine(at(0, -3.2), at(0, 0.8));
        p.drawPoint(at(0, 3.3));
        break;
    }
    p.restore();
}

// The subagent badge (card #YMSR): the agent's violet chip, the agent's own four-point star and
// the count beside it, in a `box` of the badge's own size. A free function like the two glyphs
// above, so a rendering test can paint one and measure it rather than trust its geometry; `radius`
// comes from the theme's square flag, as the chips' does. An empty text paints nothing at all —
// "if applicable" is the badge's first rule, and the widget above it is hidden then too.
// The star is the mark the state glyph uses for agent work (State::Working, and the subagents mark
// with its dots), so the number reads as agent work even with the colour gone.
inline void paintSubagentBadge(QPainter &p, const QRectF &box, const QString &text, const ps::BadgeStyle &style,
                               qreal radius, const QFont &font) {
    if (text.isEmpty()) return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(style.line, 1));
    p.setBrush(style.fill);
    p.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    p.setPen(Qt::NoPen);
    p.setBrush(style.ink);
    p.drawPath(fourPointStar(QPointF(7 + 6, box.center().y()), 5.0, 1.7));
    p.setFont(font);
    p.setPen(style.ink);
    p.drawText(QRectF(7 + 12 + 4, box.top(), box.width() - (7 + 12 + 4) - 6, box.height()),
               Qt::AlignLeft | Qt::AlignVCenter, text);
    p.restore();
}

// The tab's icon: the most urgent state among its terminals, with a red corner mark when one of
// them is in a remote session (the ⇄ itself when nothing more urgent is going on). A tab with no
// terminal shows its first special pane's type glyph instead. On top of that, the tab's live mark
// (cards #V8KT, #4E13): when work is happening in the tab (`live`) the icon blinks if it is
// itself that work, and otherwise carries a blinking corner dot in the work's own colour — the
// terminal's blue for a command, the agent's violet for agent work — so a news icon (done, needs
// you) never hides that a sibling pane is busy.
inline QIcon tabIcon(bool hasTerminal, ps::State state, bool remote, ps::Glyph typeGlyph, const QColor &typeInk, qreal dpr,
                     ps::State live = ps::State::Idle, int phase = -1) {
    const int size = 16;
    QPixmap pixmap(QSize(size, size) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    const ps::Tokens t = tokens();
    const QRectF box(1, 1, size - 2, size - 2);
    bool drewStateGlyph = false;
    if (!hasTerminal) {
        if (typeGlyph == ps::Glyph::None) { p.end(); return {}; }
        paintTypeGlyph(p, box, typeGlyph, typeInk);
    } else if (remote && ps::urgency(state) <= ps::urgency(ps::State::Running)) {
        paintTypeGlyph(p, box, ps::Glyph::Remote, t.error);
    } else {
        drewStateGlyph = true;
        const bool blinks = live != ps::State::Idle && state == live;
        paintStateGlyph(p, box, state, ps::stateInk(state, t), t.background, blinks ? ps::pulseScale(phase) : 1.0);
        if (remote) {
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(t.background, 1.5));
            p.setBrush(t.error);
            p.drawEllipse(QPointF(size - 3.5, size - 3.5), 3.0, 3.0);
        }
    }
    if (hasTerminal && live != ps::State::Idle && !(drewStateGlyph && state == live)) {
        // The ssh mark keeps its corner; the live dot takes the one across from it.
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(t.background, 1.2));
        p.setBrush(ps::stateInk(live, t));
        const QPointF at(remote ? QPointF(3.5, size - 3.5) : QPointF(size - 3.5, size - 3.5));
        const qreal r = 2.8 * ps::pulseScale(phase);
        p.drawEllipse(at, r, r);
    }
    p.end();
    return QIcon(pixmap);
}
}  // namespace relay::chrome

// The header band of a special pane (#SPBN): a low-strength tint of the type's colour, its glyph
// and its name, in the weight of a terminal's title. The pane chrome's buttons sit on
// its right, so the band is the pane's header in the way a terminal's title row is. It is a plain
// widget, so pressing on it and dragging moves the pane (RelayWindow::toolHeaderDrag).
class PaneTypeBand final : public QWidget {
public:
    explicit PaneTypeBand(QWidget *leaf) : QWidget(leaf), m_leaf(leaf) {
        setObjectName(QStringLiteral("paneTypeBand"));
        setFixedHeight(26);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setCursor(Qt::OpenHandCursor);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        const relay::panestatus::TypeStyle style = relay::chrome::styleOf(m_leaf);
        if (!style.band) return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = rect();
        p.fillPath(relay::chrome::topBand(r, relay::chrome::paneRadius()), style.fill);
        p.setPen(QPen(style.line, 1));
        p.drawLine(QPointF(r.left(), r.bottom() - 0.5), QPointF(r.right(), r.bottom() - 0.5));
        const QRectF glyph(9, (height() - 14) / 2.0, 14, 14);
        relay::chrome::paintTypeGlyph(p, glyph, style.glyph, style.ink);
        // The pane's name in the same face and weight as a terminal's title (QLabel#paneTitle), in
        // sentence case: an 8pt letter-spaced mono caps label was the hardest text on screen to
        // read (owner, 2026-09-18; docs/ARCHITECTURE.md, "Legible text").
        QFont font = relay::theme::legible(QApplication::font(), relay::theme::BodyPt);
        font.setWeight(QFont::DemiBold);
        p.setFont(font);
        p.setPen(style.text);
        const QRectF text(glyph.right() + 7, 0, width() - glyph.right() - 7 - m_rightInset, height());
        p.drawText(text, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(font).elidedText(style.label, Qt::ElideRight, int(text.width())));
    }

public:
    void setRightInset(int pixels) { if (m_rightInset != pixels) { m_rightInset = pixels; update(); } }

private:
    QWidget *m_leaf;
    int m_rightInset = 0;
};

// A non-terminal pane: a folder explorer or a file preview. Lives in the same splitter layout
// as terminal panes and is saved and restored as {"explorer": {"path"}} or {"preview": {"path"}}.
class ToolPane final : public QWidget {
public:
    enum class Kind { Explorer, Preview, Plan, Subagent, Turn, Board, Settings, Info, Sessions, Diff, Sharing };

    ToolPane(Kind kind, const QString &path, bool planActions = true) : m_kind(kind) {
        setObjectName(QStringLiteral("pane"));
        setAttribute(Qt::WA_StyledBackground);
        auto *layout = new QVBoxLayout(this); layout->setContentsMargins(1, 1, 1, 1);
        if (kind == Kind::Plan) {
            // Editable Markdown: agent plans (with Execute buttons) and documents such as relay.md.
            m_plan = new relay::PlanEditor;
            m_plan->setPlanActions(planActions);
            layout->addWidget(m_plan);
            m_plan->open(path);
        } else if (kind == Kind::Explorer) {
            m_explorer = new relay::FileExplorer(path);
            layout->addWidget(m_explorer);
        } else {
            m_preview = new relay::FilePreview;
            layout->addWidget(m_preview);
            m_preview->open(path);
        }
    }

    // subagents UI: one pane per main pane, a tab per subagent (card #WD83). Saved with its tabs'
    // text; the agents themselves end with the worker.
    ToolPane(relay::SubagentTabsView *view, const QString &cwd) : m_kind(Kind::Subagent), m_subagent(view), m_subagentCwd(cwd) {
        setObjectName(QStringLiteral("pane"));
        setAttribute(Qt::WA_StyledBackground);
        auto *layout = new QVBoxLayout(this); layout->setContentsMargins(1, 1, 1, 1);
        layout->addWidget(view);
    }

    // The Switchboard: cards, threads and the card detail view (protocol 17). Saved and restored
    // by workspace and tab, not by path.
    ToolPane(relay::BoardView *view, const QString &cwd) : m_kind(Kind::Board), m_board(view), m_subagentCwd(cwd) {
        setObjectName(QStringLiteral("pane"));
        setAttribute(Qt::WA_StyledBackground);
        auto *layout = new QVBoxLayout(this); layout->setContentsMargins(1, 1, 1, 1);
        layout->addWidget(view);
    }
    relay::BoardView *board() const { return m_board; }

    // The Actions pane or the Options pane (src/SettingsPane.h; its mode says which). Transient: not saved with the layout (node() is empty).
    ToolPane(relay::SettingsPane *view, const QString &cwd) : m_kind(Kind::Settings), m_settingsView(view), m_subagentCwd(cwd) {
        setObjectName(QStringLiteral("pane"));
        setAttribute(Qt::WA_StyledBackground);
        auto *layout = new QVBoxLayout(this); layout->setContentsMargins(1, 1, 1, 1);
        layout->addWidget(view);
    }
    relay::SettingsPane *settings() const { return m_settingsView; }

    // A finished agent turn: tool calls and transcript, opened from the inline summary line.
    ToolPane(relay::TurnTranscriptView *view, const QString &cwd) : m_kind(Kind::Turn), m_turn(view), m_subagentCwd(cwd) {
        setObjectName(QStringLiteral("pane"));
        setAttribute(Qt::WA_StyledBackground);
        auto *layout = new QVBoxLayout(this); layout->setContentsMargins(1, 1, 1, 1);
        layout->addWidget(view);
    }
    relay::TurnTranscriptView *turn() const { return m_turn; }

    // One unified diff, read only: the write or edit whose diff is more than 12 changed lines
    // (#TK9C, protocol section 23.6). Transient -- the diff is a moment in an agent turn, not a
    // file on disk, so node() saves nothing and a reopened window does not bring it back.
    ToolPane(relay::DiffView *view, const QString &cwd) : m_kind(Kind::Diff), m_diff(view), m_subagentCwd(cwd) {
        setObjectName(QStringLiteral("pane"));
        setAttribute(Qt::WA_StyledBackground);
        auto *layout = new QVBoxLayout(this); layout->setContentsMargins(1, 1, 1, 1);
        layout->addWidget(view);
    }
    relay::DiffView *diff() const { return m_diff; }

    // Views that only need a title, focus and the header inset (relay::PaneView): the ⓘ
    // conversation info (Kind::Info) and the session manager (Kind::Sessions). Transient.
    ToolPane(Kind kind, QWidget *view, relay::PaneView *hosted, const QString &cwd) : m_kind(kind), m_hosted(hosted), m_subagentCwd(cwd) {
        setObjectName(QStringLiteral("pane"));
        setAttribute(Qt::WA_StyledBackground);
        auto *layout = new QVBoxLayout(this); layout->setContentsMargins(1, 1, 1, 1);
        layout->addWidget(view);
    }
    relay::PaneView *hosted() const { return m_hosted; }

    Kind kind() const { return m_kind; }
    relay::FileExplorer *explorer() const { return m_explorer; }
    relay::FilePreview *preview() const { return m_preview; }
    relay::PlanEditor *plan() const { return m_plan; }
    relay::SubagentTabsView *subagent() const { return m_subagent; }
    QString path() const { return (m_subagent || m_turn || m_diff || m_board || m_settingsView || m_hosted) ? QString() : m_explorer ? m_explorer->root() : m_plan ? m_plan->path() : m_preview->path(); }
    // A preview of a file on another machine (#S5SH) has no folder here: its path is an
    // `ssh://host/path`, and the directory part of it names nothing on this disk.
    QString cwd() const { return (m_subagent || m_turn || m_diff || m_board || m_settingsView || m_hosted) ? m_subagentCwd
                                 : m_explorer ? (m_explorer->isRemote() ? QString() : m_explorer->root())
                                 : (m_preview && m_preview->isRemote()) ? QString() : QFileInfo(path()).absolutePath(); }
    QString title() const {
        if (m_settingsView) return m_settingsView->mode() == relay::SettingsPane::Mode::Actions ? QStringLiteral("Actions") : QStringLiteral("Options");
        if (m_board) return m_board->title();
        if (m_hosted) return m_hosted->paneTitle();
        if (m_subagent) return m_subagent->title();
        if (m_turn) return m_turn->title();
        if (m_diff) return m_diff->title();
        if (m_plan) return (m_plan->isDirty() ? QStringLiteral("● ") : QString()) + m_plan->title();
        // "nginx.conf" in a tab would be indistinguishable from this machine's: a file on a host
        // is named by its host and its whole path, with the ● of an unsaved edit (#S5SH).
        if (m_preview && m_preview->isRemote()) return m_preview->title();
        if (m_explorer && m_explorer->isRemote()) return m_explorer->title();
        const QString name = QFileInfo(path()).fileName();
        return name.isEmpty() ? path() : name;
    }
    QJsonObject node() const {
        // The rows board has no tabs to remember; what it keeps is which sections are collapsed and
        // which are unticked in the section checkboxes.
        if (m_board) return {{"board", QJsonObject{{"workspace", m_board->workspace()},
                                                   {"collapsed", m_board->collapsedSections()},
                                                   {"hidden", m_board->hiddenSections()}}}};
        if (m_subagent) return m_subagent->node();
        if (m_turn || m_diff || m_settingsView || m_hosted) return {};
        if (m_plan) return {{"plan", QJsonObject{{"path", path()}}}};
        return {{m_explorer ? "explorer" : "preview", QJsonObject{{"path", path()}}}};
    }
    void focusInput() {
        if (m_settingsView) m_settingsView->focusSearch();
        else if (m_board) m_board->focusInput();
        else if (m_hosted) m_hosted->focusView();
        else if (m_subagent) m_subagent->focusInput();
        else if (m_turn) m_turn->focusInput();
        else if (m_diff) m_diff->focusInput();
        else if (m_plan) m_plan->editor()->setFocus(Qt::OtherFocusReason);
        else if (m_explorer) m_explorer->view()->setFocus(Qt::OtherFocusReason);
        else m_preview->setFocus(Qt::OtherFocusReason);
    }

    // ----- pane type and header band (#SPBN) ------------------------------------------------
    // The `paneType` property is the whole contract (docs/ARCHITECTURE.md, "Pane types"): set it
    // on the pane and the band appears, tinted and labelled. A pane that has not set one gets the
    // default for its kind when it is first polished. `paneLabel` overrides the band's text.
    QString defaultPaneType() const {
        switch (m_kind) {
        case Kind::Board: return QStringLiteral("board");
        case Kind::Sharing: return QStringLiteral("sharing");
        case Kind::Settings:
            return m_settingsView && m_settingsView->mode() == relay::SettingsPane::Mode::Actions ? QStringLiteral("actions") : QStringLiteral("options");
        case Kind::Subagent: return QStringLiteral("subagent");
        case Kind::Turn: return QStringLiteral("turn");
        case Kind::Explorer: return QStringLiteral("explorer");
        case Kind::Preview: return QStringLiteral("preview");
        case Kind::Plan: return QStringLiteral("plan");
        case Kind::Info: return QStringLiteral("info");
        case Kind::Sessions: return QStringLiteral("sessions");
        default: return {};   // a kind added later is plain until it sets paneType itself
        }
    }
    PaneTypeBand *band() const { return m_band; }
    bool bandShown() const { return m_band && !m_band->isHidden(); }
    std::function<void()> onBandChanged;   // PaneChrome re-places its buttons on the band

    // Shows, hides or repaints the band after the type, the label or the colour setting changed.
    void refreshBand() {
        const bool want = relay::chrome::styleOf(this).band;
        if (want && !m_band) {
            auto *box = qobject_cast<QVBoxLayout *>(layout());
            if (!box) { QTimer::singleShot(0, this, [this] { refreshBand(); }); return; }
            m_band = new PaneTypeBand(this);
            box->insertWidget(0, m_band);
        }
        if (m_band) { m_band->setVisible(want); m_band->update(); }
        if (onBandChanged) onBandChanged();
    }

protected:
    bool event(QEvent *event) override {
        if (event->type() == QEvent::Polish && !property("paneType").isValid())
            setProperty("paneType", defaultPaneType());   // arrives below as a property change
        if (event->type() == QEvent::DynamicPropertyChange) {
            const QByteArray name = static_cast<QDynamicPropertyChangeEvent *>(event)->propertyName();
            if (name == "paneType" || name == "paneLabel") refreshBand();
        }
        return QWidget::event(event);
    }

private:
    PaneTypeBand *m_band = nullptr;
    Kind m_kind;
    relay::FileExplorer *m_explorer = nullptr;
    relay::FilePreview *m_preview = nullptr;
    relay::PlanEditor *m_plan = nullptr;
    relay::SubagentTabsView *m_subagent = nullptr;
    relay::TurnTranscriptView *m_turn = nullptr;
    relay::DiffView *m_diff = nullptr;
    relay::BoardView *m_board = nullptr;
    relay::SettingsPane *m_settingsView = nullptr;
    relay::PaneView *m_hosted = nullptr;
    QString m_subagentCwd;
};

// ----- pane chrome: button row and drag handle -----------------------------------------------
// A small overlay in each pane's top-right corner. The three a person reaches for — new pane,
// move to a tab of its own, close — are on screen in every pane at all times (owner, 2026-09-17:
// buttons that appear only under the mouse are buttons you have to go looking for). There is one
// new-pane button, which puts the pane on the right (card #803C). Dragging a pane's header moves it.
class PaneChrome final : public QFrame {
public:
    std::function<void(const QString &action)> onAction;

    explicit PaneChrome(QWidget *leaf) : QFrame(leaf) {
        setObjectName(QStringLiteral("paneChrome"));
        setAttribute(Qt::WA_StyledBackground);
        auto *row = new QHBoxLayout(this); row->setContentsMargins(3, 2, 3, 2); row->setSpacing(1);
        // The + makes it obvious that these open a new pane (a new shell and chat), not a layout
        // toggle. Every button is here at all times: the row no longer grows, lifts onto a tile or
        // rearranges itself under the pointer (owner, 2026-09-18). The drag grip is gone with the
        // hover row — pressing anywhere on the header moves the pane.
        // One "new pane" button, not one per side (owner, 2026-09-18, card #803C). It makes the
        // pane on the right at once, like Ctrl+E; the mouse is already in hand, so the pane is
        // placed by dragging its header. Its tooltip shows the key (pane.splitRight).
        button(row, QStringLiteral("⊞"), QStringLiteral("pane.newByMouse"), QStringLiteral("New pane (drag its header to place it)"))
            ->setProperty("keysFrom", QStringLiteral("pane.splitRight"));
        button(row, QStringLiteral("⇱"), QStringLiteral("pane.moveToNewTab"), QStringLiteral("Move to new tab"));
        button(row, QStringLiteral("×"), QStringLiteral("pane.close"), QStringLiteral("Close pane"));
        // The header gives up exactly this much room for good, so the title and the folder line
        // never re-elide.
        adjustSize();
        m_fullWidth = width();
        // A special pane's buttons sit on its type band (#SPBN); a terminal gets its status glyph
        // and the remote-session marks in its title row (#XM0T).
        if (auto *tool = dynamic_cast<ToolPane *>(leaf)) {
            QPointer<PaneChrome> guard(this);
            tool->onBandChanged = [guard] { if (guard) guard->place(); };
        }
        if (auto *pane = dynamic_cast<Pane *>(leaf)) buildStatus(pane);
    }

    void place() {
        auto *leaf = parentWidget();
        adjustSize();
        // Buttons added after construction (the sessions ⓘ, say) widen the room the header keeps.
        m_fullWidth = std::max(m_fullWidth, width());
        int y = 4;
        if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->bandShown()) {
            PaneTypeBand *band = tool->band();
            band->setFixedHeight(std::max(24, height() + 2));
            band->setRightInset(m_fullWidth + 10);
            const int top = band->geometry().isValid() && band->y() > 0 ? band->y() : 1;
            y = top + (band->height() - height()) / 2;
        }
        move(leaf->width() - width() - 6, y);
        raise();
        syncHeaderInset();
        placeBackdrop();
    }

    // Repaints every pane's type band, status glyph and remote marks in every window: after
    // "appearance/pane_colours" changed (the Options pane calls this), or anything else that
    // changes how they look without a theme switch.
    static void refreshAll() {
        relay::chrome::reloadColourMode();
        for (QWidget *top : QApplication::topLevelWidgets())
            for (QWidget *widget : top->findChildren<QWidget *>()) {
                if (auto *tool = dynamic_cast<ToolPane *>(widget)) tool->refreshBand();
                else if (auto *chrome = dynamic_cast<PaneChrome *>(widget)) { chrome->place(); chrome->update(); }
            }
    }

    // ----- status (#XM0T) and remote session (#SPBN), terminal panes only ------------------------
    // RelayWindow::refreshPaneStatus() calls this on its poll. `seenSerial` / `watchedSince` are
    // the window's bookkeeping for "news the user has not looked at yet".
    quint64 seenSerial = 0;
    qint64 watchedSince = 0;
    relay::panestatus::State state() const { return m_state; }
    bool remote() const { return m_remote; }

    void setStatus(relay::panestatus::State state, const QString &remoteCommand, bool phone) {
        if (!m_glyph) return;
        const bool remote = !remoteCommand.isEmpty();
        const QString host = remote ? relay::panestatus::remoteHost(remoteCommand) : QString();
        const QString program = remote ? QFileInfo(remoteCommand.section(' ', 0, 0)).fileName() : QString();
        if (state != m_state) {
            const bool wasLive = relay::panestatus::isLive(m_state);
            m_state = state;
            m_glyph->setToolTip(relay::panestatus::stateLabel(state));
            m_glyph->setProperty("paneState", relay::panestatus::stateName(state));
            m_glyph->setLive(relay::panestatus::isLive(state));   // the live glyph blinks (#V8KT, #4E13)
            m_word->setState(state);                              // ... and says its word
            // The word takes room from the title, so the title re-elides around it.
            if (relay::panestatus::isLive(state) != wasLive)
                if (auto *pane = dynamic_cast<Pane *>(parentWidget())) pane->updateHeader();
        }
        if (remote != m_remote || host != m_remoteHost) {
            m_remote = remote; m_remoteHost = host;
            m_remoteChip->setText(host.isEmpty() ? program : host);
            m_remoteChip->setToolTip(QStringLiteral("Remote session · %1\nWhat you type here goes to %2, not this machine.")
                                         .arg(remoteCommand, host.isEmpty() ? QStringLiteral("another machine") : host));
            m_remoteChip->setVisible(remote);
            m_backdrop->setVisible(remote || m_guestDriving);
            if (auto *pane = dynamic_cast<Pane *>(parentWidget())) {
                pane->setProperty("remoteSession", remote ? host : QString());
                pane->updateHeader();   // the title gives the chip its room
            }
            placeBackdrop();
        }
        if (phone != m_phone) {
            m_phone = phone;
            m_phoneChip->setVisible(phone);
            if (auto *pane = dynamic_cast<Pane *>(parentWidget())) pane->updateHeader();
        }
    }

    // The pane's resource meter (issue #D03W). Called by the same 400 ms poll that calls
    // setStatus(); the chip hides itself when the pane is using nothing worth a read.
    void setUsage(const relay::usage::Sample &sample) {
        if (m_usageChip) m_usageChip->setSample(sample);
    }

    // How many subagents this pane's agent has running, as the badge beside the state's word
    // (card #YMSR). Same poll; the badge hides itself at zero, which is every pane that has never
    // started one.
    void setSubagents(int live) {
        if (m_subagentBadge) m_subagentBadge->setCount(live);
    }

    // Multiplayer (#W5N2, docs/REMOTE-PROTOCOL.md section 10.3). The chip beside the pane's title
    // is the one thing always on screen while a pane is shared, so it is where "and two other
    // people are watching" and "alice has the keyboard" have to be said. A guest driving gets the
    // remote session's own treatment — the error hue on the chip and the hatched band across the
    // title row — because it means the same thing: the keys landing here are not yours.
    void setSharing(const QString &text, const QString &tooltip, bool guestDriving) {
        if (!m_phoneChip) return;
        m_phoneChip->setText(text.isEmpty() ? QStringLiteral("phone") : text);
        m_phoneChip->setToolTip(tooltip);
        if (guestDriving == m_guestDriving) return;
        m_guestDriving = guestDriving;
        m_phoneChip->setAlarm(guestDriving);
        m_backdrop->setVisible(m_remote || m_guestDriving);
        placeBackdrop();
        if (auto *pane = dynamic_cast<Pane *>(parentWidget())) pane->updateHeader();
    }

    // Pane title (issue JRWQ): the header's right-hand directory must not end up under these
    // buttons, so the header gives up exactly the room the full row takes.
    void syncHeaderInset() {
        if (auto *pane = dynamic_cast<Pane *>(parentWidget()))
            pane->setHeaderRightInset(isVisible() ? m_fullWidth + 10 : 0);
        // The Switchboard's first row is its tab bar, which these buttons would otherwise cover.
        // So is a preview's header, whose view button names the format and so is wide enough to
        // reach them ("Source (MD)", issue #VXTF), and an explorer's folder line.
        else if (auto *tool = dynamic_cast<ToolPane *>(parentWidget()); tool) {
            // On a type band the buttons are in the band, and the view's first row has it all.
            const int inset = isVisible() && !tool->bandShown() ? m_fullWidth + 4 : 0;
            if (tool->board()) tool->board()->setHeaderRightInset(inset);
            else if (tool->preview()) tool->preview()->setHeaderRightInset(inset);
            else if (tool->explorer()) tool->explorer()->setHeaderRightInset(inset);
            // Settings' search row and a subagent transcript's title row are their first rows too.
            else if (tool->settings()) tool->settings()->setHeaderRightInset(inset);
            else if (tool->subagent()) tool->subagent()->setHeaderRightInset(inset);
            else if (tool->hosted()) tool->hosted()->setHeaderRightInset(inset);
        }
    }

protected:
    void showEvent(QShowEvent *event) override { QFrame::showEvent(event); syncHeaderInset(); }
    void hideEvent(QHideEvent *event) override { QFrame::hideEvent(event); syncHeaderInset(); }

public:
    void refreshTooltips() {
        for (auto *b : findChildren<QToolButton *>()) {
            const QString keysFrom = b->property("keysFrom").toString();
            const QString keys = Keymap::instance().shortcutText(keysFrom.isEmpty() ? b->property("action").toString() : keysFrom);
            b->setToolTip(b->property("label").toString() + (keys.isEmpty() ? QString() : QStringLiteral("  (") + keys + ')'));
        }
    }

private:
    QToolButton *button(QHBoxLayout *row, const QString &glyph, const QString &action, const QString &label) {
        auto *b = new QToolButton;
        b->setObjectName(QStringLiteral("paneChromeButton"));
        b->setText(glyph); b->setAutoRaise(true); b->setFocusPolicy(Qt::NoFocus);
        b->setProperty("action", action); b->setProperty("label", label);
        connect(b, &QToolButton::clicked, this, [this, action] { if (onAction) onAction(action); });
        row->addWidget(b);
        return b;
    }

    void buildStatus(Pane *pane) {
        QHBoxLayout *row = pane->headerLayout();
        QWidget *header = pane->headerWidget();
        if (!row || !header) return;
        m_glyph = new PaneStateGlyph(this);
        m_word = new PaneStateWord(this);
        m_subagentBadge = new PaneSubagentBadge(this);
        m_usageChip = new PaneUsageChip;
        m_remoteChip = new PaneHeaderChip(relay::panestatus::Glyph::Remote);
        m_phoneChip = new PaneHeaderChip(relay::panestatus::Glyph::Phone);
        m_phoneChip->setText(QStringLiteral("phone"));
        m_phoneChip->setToolTip(QStringLiteral("Shared with your phone: it sees this pane and can type into it.\n"
                                               "The share chip under the prompt box shows the code or stops it."));
        m_remoteChip->hide(); m_phoneChip->hide();
        row->insertWidget(0, m_glyph);
        row->insertWidget(1, m_word);
        // The subagent badge reads with the state's word: both are what this pane's agent is doing
        // now (card #YMSR). The safety chips (ssh, phone) keep their places to the right of them.
        row->insertWidget(2, m_subagentBadge);
        row->insertWidget(3, m_remoteChip);
        row->insertWidget(4, m_phoneChip);
        row->insertWidget(5, m_usageChip);
        m_backdrop = new RemoteBackdrop(pane);
        m_backdrop->hide();
        m_backdrop->lower();
        header->installEventFilter(this);
        pane->installEventFilter(this);
    }

    // The remote band covers the pane's title row, inside the frame, down to half the gap below it.
    void placeBackdrop() {
        if (!m_backdrop || m_backdrop->isHidden()) return;
        auto *pane = dynamic_cast<Pane *>(parentWidget());
        QWidget *header = pane ? pane->headerWidget() : nullptr;
        if (!header) return;
        const int inset = relay::theme::active().flag(QStringLiteral("bevel")) ? 2 : 1;
        m_backdrop->setGeometry(inset, inset, pane->width() - 2 * inset, header->geometry().bottom() + 4 - inset);
        m_backdrop->lower();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Move || event->type() == QEvent::LayoutRequest)
            if (watched == parentWidget() || (m_backdrop && watched != this)) placeBackdrop();
        return QFrame::eventFilter(watched, event);
    }

private:
    // The state glyph at the start of a terminal's title row. A live state blinks (cards #V8KT,
    // #4E13): the glyph pulses on the waiting dots' own clock (Pane::refreshBackgroundWait), and
    // the desktop's reduce-motion signal — a cursor flash time of 0 — leaves it still at full size.
    class PaneStateGlyph final : public QWidget {
    public:
        explicit PaneStateGlyph(PaneChrome *chrome) : m_chrome(chrome) {
            setObjectName(QStringLiteral("paneStateGlyph"));
            setFixedSize(16, 16);
            setToolTip(relay::panestatus::stateLabel(relay::panestatus::State::Idle));
        }
        void setLive(bool live) {
            if (live) {
                if (!m_pulse) {
                    m_pulse = new QTimer(this);
                    m_pulse->setInterval(600);   // the waiting dots' clock: four steps, two levels
                    connect(m_pulse, &QTimer::timeout, this, [this] { ++m_phase; update(); });
                }
                if (QApplication::cursorFlashTime() > 0) m_pulse->start();
            } else if (m_pulse) {
                m_pulse->stop();
            }
            update();
        }
    protected:
        void paintEvent(QPaintEvent *) override {
            QPainter p(this);
            const relay::panestatus::Tokens t = relay::chrome::tokens();
            const relay::panestatus::State s = m_chrome->state();
            // The ground under a filled glyph's cut-out mark: the remote band when there is one.
            const QColor ground = m_chrome->remote() ? relay::panestatus::remoteStyle(t).fill : t.background;
            const bool blinking = m_pulse && m_pulse->isActive();
            relay::chrome::paintStateGlyph(p, QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), s, relay::panestatus::stateInk(s, t), ground,
                                           blinking ? relay::panestatus::pulseScale(m_phase) : 1.0);
        }
    private:
        PaneChrome *m_chrome;
        QTimer *m_pulse = nullptr;
        int m_phase = 0;
    };

    // The live state's word beside the glyph — "Command running", "Relaying…", "Subagents
    // working" — in the state's own colour (the terminal's blue, the agent's violet) lifted to
    // reading strength on the header's ground (card #V8KT). Painted, not a QLabel, for the same
    // reason the chips are: the colour follows the state.
    class PaneStateWord final : public QWidget {
    public:
        explicit PaneStateWord(PaneChrome *chrome) : m_chrome(chrome) {
            setObjectName(QStringLiteral("paneStateWord"));
            setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
            hide();
        }
        void setState(relay::panestatus::State state) {
            setToolTip(relay::panestatus::stateLabel(state));
            const QString text = relay::panestatus::isLive(state) ? relay::panestatus::stateLabel(state) : QString();
            if (text == m_text) return;
            m_text = text;
            updateGeometry(); update();
            setVisible(!m_text.isEmpty());   // Pane::updateHeader re-elides the title around it
        }
        QSize sizeHint() const override {
            QFont bold = font(); bold.setWeight(QFont::DemiBold);
            return {QFontMetrics(bold).horizontalAdvance(m_text) + 2, 18};
        }
    protected:
        void paintEvent(QPaintEvent *) override {
            if (m_text.isEmpty()) return;
            const relay::panestatus::Tokens t = relay::chrome::tokens();
            const QColor ground = m_chrome->remote() ? relay::panestatus::remoteStyle(t).fill : t.background;
            QPainter p(this);
            QFont bold = font(); bold.setWeight(QFont::DemiBold);
            p.setFont(bold);
            p.setPen(relay::panestatus::stateText(m_chrome->state(), ground, t));
            p.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter, m_text);
        }
    private:
        PaneChrome *m_chrome;
        QString m_text;
    };

    // "⇄ me@box" and "phone" in the title row: a glyph and a word on a small outlined chip.
    class PaneHeaderChip final : public QWidget {
    public:
        explicit PaneHeaderChip(relay::panestatus::Glyph glyph) : m_glyph(glyph) {
            setObjectName(glyph == relay::panestatus::Glyph::Remote ? QStringLiteral("paneRemoteChip") : QStringLiteral("panePhoneChip"));
            setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        }
        void setText(const QString &text) { if (m_text == text) return; m_text = text; updateGeometry(); update(); }
        // A phone chip that has to be noticed: painted as the remote-session chip is, because
        // "somebody else's keys are landing here" is the same warning.
        void setAlarm(bool alarm) { if (m_alarm == alarm) return; m_alarm = alarm; update(); }
        QSize sizeHint() const override {
            QFont bold = font(); bold.setWeight(QFont::DemiBold);
            const int text = std::min(220, QFontMetrics(bold).horizontalAdvance(m_text));
            return {7 + 12 + 5 + text + 8, 18};
        }
        // The host stays readable in a narrow pane: the title gives way first (Pane::updateHeader).
        QSize minimumSizeHint() const override { return {std::min(sizeHint().width(), 150), 18}; }
    protected:
        void paintEvent(QPaintEvent *) override {
            const relay::panestatus::Tokens t = relay::chrome::tokens();
            const bool remote = m_glyph == relay::panestatus::Glyph::Remote || m_alarm;
            const relay::panestatus::TypeStyle style = remote ? relay::panestatus::remoteStyle(t) : relay::panestatus::phoneStyle(t);
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing);
            const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
            const qreal radius = relay::chrome::paneRadius() > 0 ? 5 : 0;
            p.setPen(QPen(remote ? style.line : style.ink, 1));
            p.setBrush(remote ? relay::panestatus::mix(t.error, t.background, 0.26) : style.fill);
            p.drawRoundedRect(r, radius, radius);
            relay::chrome::paintTypeGlyph(p, QRectF(7, (height() - 12) / 2.0, 12, 12), m_glyph, remote ? style.text : style.ink);
            QFont bold = font(); bold.setWeight(QFont::DemiBold);
            p.setFont(bold);
            p.setPen(remote ? t.text : style.text);
            const QRectF text(7 + 12 + 5, 0, width() - (7 + 12 + 5) - 6, height());
            p.drawText(text, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(bold).elidedText(m_text, Qt::ElideMiddle, int(text.width())));
        }
    private:
        relay::panestatus::Glyph m_glyph;
        QString m_text;
        bool m_alarm = false;
    };

    // The pane's resource meter (issue #D03W): "12% 3%" with a die and a memory-module glyph,
    // right of the phone chip in the header row. Quiet on purpose — plain ink, no band, colours
    // only when a value is high — and absent while the pane costs nothing worth reading, so an
    // idle terminal looks exactly as it did before.
    class PaneUsageChip final : public QWidget {
    public:
        explicit PaneUsageChip() {
            setObjectName(QStringLiteral("paneUsageChip"));
            setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            setAccessibleName(QStringLiteral("Pane CPU and memory"));
        }
        void setSample(const relay::usage::Sample &sample) {
            m_sample = sample;
            const bool show = sample.valid && relay::usage::worthShowing(sample) && enabled();
            if (show == isVisible() && m_text == text()) { update(); return; }
            m_text = text();
            setToolTip(QStringLiteral("This pane's share of this machine\\n%1\\n\\n"
                                      "Counts the shell, the program it is running and this pane's agent "
                                      "worker, with their children. A remote pane measures the local ssh "
                                      "client, not the far machine.")
                           .arg(relay::usage::describe(sample)));
            setVisible(show);
            updateGeometry();
            update();
            // Appearing or leaving changes what the title has to elide around.
            if (auto *pane = dynamic_cast<Pane *>(parentWidget())) pane->updateHeader();
        }
        QSize sizeHint() const override {
            if (!isVisible() && m_text.isEmpty()) return {0, 0};
            const QFontMetrics metrics(font());
            return {7 + kGlyph + 4 + metrics.horizontalAdvance(section(0)) + 10 + kGlyph + 4
                        + metrics.horizontalAdvance(section(1)) + 7, 18};
        }
    protected:
        void paintEvent(QPaintEvent *) override {
            if (m_text.isEmpty()) return;
            const relay::panestatus::Tokens t = relay::chrome::tokens();
            QPainter p(this);
            p.setFont(font());
            p.setRenderHint(QPainter::Antialiasing);
            const QFontMetrics metrics(font());
            int x = 7;
            for (int i = 0; i < 2; ++i) {
                const double value = i == 0 ? m_sample.cpuPercent : m_sample.ramPercent;
                const QColor ink = value >= 85 ? t.error : value >= 60 ? t.warning : t.muted;
                p.setPen(QPen(ink, 1.2));
                if (i == 0) paintDie(p, QRectF(x, (height() - 12) / 2.0, kGlyph, kGlyph), ink);
                else paintModule(p, QRectF(x, (height() - 12) / 2.0, kGlyph, kGlyph), ink);
                x += kGlyph + 4;
                const QString label = section(i);
                p.setPen(ink);
                p.drawText(QRectF(x, 0, metrics.horizontalAdvance(label) + 2, height()),
                           Qt::AlignLeft | Qt::AlignVCenter, label);
                x += metrics.horizontalAdvance(label);
            }
        }
    private:
        static constexpr int kGlyph = 13;
        // A processor die: a square with a smaller square inside, pins on the sides.
        static void paintDie(QPainter &p, const QRectF &r, const QColor &ink) {
            p.setPen(QPen(ink, 1.1));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(r.center().x() - 3, r.center().y() - 3, 6, 6), 1.5, 1.5);
            for (int k = -1; k <= 1; ++k) {
                const qreal y = r.center().y() + k * 2.4;
                p.drawLine(QPointF(r.left() + 0.5, y), QPointF(r.center().x() - 3, y));
                p.drawLine(QPointF(r.center().x() + 3, y), QPointF(r.right() - 0.5, y));
            }
        }
        // A memory module: a body with pins along its bottom edge.
        static void paintModule(QPainter &p, const QRectF &r, const QColor &ink) {
            p.setPen(QPen(ink, 1.1));
            p.setBrush(Qt::NoBrush);
            const QRectF body(r.left() + 1, r.top() + 1.5, r.width() - 2, r.height() - 5.5);
            p.drawRoundedRect(body, 1.2, 1.2);
            for (int k = 0; k < 4; ++k) {
                const qreal x = body.left() + 1.5 + k * 2.6;
                if (x >= body.right() - 0.5) break;
                p.drawLine(QPointF(x, body.bottom()), QPointF(x, r.bottom() - 0.5));
            }
        }
        static bool enabled() {
            return QSettings().value(QStringLiteral("appearance/pane_usage"), true).toBool();
        }
        QString section(int i) const {
            return i == 0 ? m_text.section(QLatin1Char('/'), 0, 0) : m_text.section(QLatin1Char('/'), 1, 1);
        }
        QString text() const {
            if (!m_sample.valid || !relay::usage::worthShowing(m_sample)) return {};
            return relay::usage::formatPercent(m_sample.cpuPercent) + QLatin1Char('%') + QLatin1Char('/')
                 + relay::usage::formatPercent(m_sample.ramPercent) + QLatin1Char('%');
        }
        relay::usage::Sample m_sample;
        QString m_text;   // "12%/3%"; empty when the chip is hidden
    };

    // The subagent badge (card #YMSR): how many agents this pane's agent has running, right of the
    // state's word. Painted, not a QLabel, for the chips' reason — its ink follows the theme and
    // the ground it sits on — and hidden at zero, so a pane that has never started a subagent looks
    // exactly as it did before. A read-out, not a button: a click on the header moves the pane, and
    // the tooltip is where the key that opens the subagents pane is taught.
    class PaneSubagentBadge final : public QWidget {
    public:
        explicit PaneSubagentBadge(PaneChrome *chrome) : m_chrome(chrome) {
            setObjectName(QStringLiteral("paneSubagentBadge"));
            setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            setAccessibleName(QStringLiteral("Subagents running"));
            hide();
        }
        void setCount(int live) {
            const QString text = relay::panestatus::subagentBadgeText(live);
            // The key is read live, so a rebound one is right here too; comparing it as well means a
            // rebind while the count stands still still refreshes the tooltip.
            const QString keys = Keymap::instance().shortcutText(QStringLiteral("agent.subagentPane"));
            if (text == m_text && keys == m_keys) return;
            m_text = text; m_keys = keys;
            setToolTip(relay::panestatus::subagentBadgeTooltip(live, keys));
            setVisible(!m_text.isEmpty());
            updateGeometry();
            update();
            // Appearing, leaving or widening changes what the title has to elide around.
            if (auto *pane = dynamic_cast<Pane *>(parentWidget())) pane->updateHeader();
        }
        QSize sizeHint() const override {
            if (m_text.isEmpty()) return {0, 0};
            QFont bold = font(); bold.setWeight(QFont::DemiBold);
            return {7 + 12 + 4 + QFontMetrics(bold).horizontalAdvance(m_text) + 7, 18};
        }
        QSize minimumSizeHint() const override { return sizeHint(); }
    protected:
        void paintEvent(QPaintEvent *) override {
            if (m_text.isEmpty()) return;
            const relay::panestatus::Tokens t = relay::chrome::tokens();
            const QColor ground = m_chrome->remote() ? relay::panestatus::remoteStyle(t).fill : t.background;
            QFont bold = font(); bold.setWeight(QFont::DemiBold);
            QPainter p(this);
            relay::chrome::paintSubagentBadge(p, QRectF(rect()), m_text, relay::panestatus::subagentBadgeStyle(ground, t),
                                              relay::chrome::paneRadius() > 0 ? 5 : 0, bold);
        }
    private:
        PaneChrome *m_chrome;
        QString m_text;
        QString m_keys;
    };

    // The remote band behind a terminal's title row: the error hue, hatched, with a firm line under
    // it. Hatching is a texture no pane type uses, so it reads as "not here" even without colour.
    class RemoteBackdrop final : public QWidget {
    public:
        explicit RemoteBackdrop(QWidget *pane) : QWidget(pane) {
            setObjectName(QStringLiteral("paneRemoteBand"));
            setAttribute(Qt::WA_TransparentForMouseEvents);
        }
    protected:
        void paintEvent(QPaintEvent *) override {
            const relay::panestatus::Tokens t = relay::chrome::tokens();
            const relay::panestatus::TypeStyle style = relay::panestatus::remoteStyle(t);
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing);
            const QRectF r = rect();
            const QPainterPath band = relay::chrome::topBand(r, relay::chrome::paneRadius());
            p.fillPath(band, style.fill);
            p.setClipPath(band);
            const bool light = relay::panestatus::isLight(t.background);
            p.setPen(QPen(relay::panestatus::mix(t.error, t.background, light ? 0.17 : 0.24), 3));
            for (qreal x = -r.height(); x < r.width(); x += 11) p.drawLine(QPointF(x, r.height()), QPointF(x + r.height(), 0));
            p.setClipping(false);
            p.setPen(QPen(style.line, 1.5));
            p.drawLine(QPointF(r.left(), r.bottom() - 0.25), QPointF(r.right(), r.bottom() - 0.25));
        }
    };

    int m_fullWidth = 0;
    PaneStateGlyph *m_glyph = nullptr;
    PaneStateWord *m_word = nullptr;
    PaneSubagentBadge *m_subagentBadge = nullptr;
    PaneHeaderChip *m_remoteChip = nullptr, *m_phoneChip = nullptr;
    PaneUsageChip *m_usageChip = nullptr;
    RemoteBackdrop *m_backdrop = nullptr;
    relay::panestatus::State m_state = relay::panestatus::State::Idle;
    bool m_remote = false, m_phone = false, m_guestDriving = false;
    QString m_remoteHost;
};

