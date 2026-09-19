// SPDX-License-Identifier: AGPL-3.0-or-later
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
#include "RelayMark.h"   // the app's own mark, painted: the state glyph and the tab icons
#include "PaneUsage.h"
#include "Theme.h"

#include <QApplication>
#include <QDateTime>
#include <QDynamicPropertyChangeEvent>
#include <QEvent>
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

// The Relay mark itself moved to src/RelayMark.h on 2026-09-19 (#4X53): the button on the
// "Relaying · …" line paints the same mark, and that line lives in Pane.h, which is included
// before this header. `relay::chrome::paintRelayMark` is unchanged and still lands here.

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
    // Everything inside is measured from the box, not from the painter's origin: the widget hands
    // in its own rect() (origin 0, 0) but a test — or any caller that draws the badge into a
    // larger surface — hands in a box further along, and absolute offsets put the star and the
    // number outside the chip they belong to.
    p.drawPath(fourPointStar(QPointF(box.left() + 7 + 6, box.center().y()), 5.0, 1.7));
    p.setFont(font);
    p.setPen(style.ink);
    const qreal textLeft = box.left() + 7 + 12 + 4;
    p.drawText(QRectF(textLeft, box.top(), box.right() - 6 - textLeft, box.height()),
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
    enum class Kind { Explorer, Preview, Plan, Subagent, Turn, Board, Settings, Info, Sessions, Diff, Sharing, Internals };

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
                                                   {"hidden", m_board->hiddenSections()},
                                                   {"sort", m_board->sortOrder()}}}};
        if (m_subagent) return m_subagent->node();
        // The Activity pane (card #QT8C) comes back open beside its owner, empty until the
        // next event; `owner` is the owner's scrollback id, the key the subagent pane uses too.
        if (m_kind == Kind::Internals)
            return {{"internals", QJsonObject{{"cwd", m_subagentCwd}, {"owner", property("internalsOwner").toString()}}}};
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
        case Kind::Internals: return QStringLiteral("internals");
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
        // Sharing moved here from the prompt-box strip (owner, 2026-09-19: it no longer fit
        // beside the model and the microphone). First in the row, before the ⓘ RelayWindow
        // adds: shared-or-not is the one pane state worth seeing from across the window.
        if (auto *pane = dynamic_cast<Pane *>(leaf)) buildShare(row, pane);
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
            m_state = state;
            // The glyph is the whole mark in this row since card #0STR: it blinks while the state
            // is live (#V8KT, #4E13) and its tooltip is where the state is spelled out. There was
            // a word beside it until #0STR, and changing the state re-elided the title around it;
            // the glyph is one fixed size in every state, so nothing in the row moves now.
            m_glyph->setToolTip(relay::panestatus::stateLabel(state));
            m_glyph->setProperty("paneState", relay::panestatus::stateName(state));
            m_glyph->setLive(relay::panestatus::isLive(state));
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

    // ----- the header's give-way ladder (owner, 2026-09-19) --------------------------------------
    // The Pane owns the title and the directory; PaneChrome owns everything else in that row. So
    // `Pane::updateHeader()` asks this half for what it would take at its natural width, hands the
    // lot to relay::panes::headerFit(), and gives the answer back here. Natural widths only: what a
    // chip is showing at this moment is never an input, because this answer decides it — an input
    // that read it would feed itself and the header would flicker between two rungs for ever.

    // What this chrome's share of the row wants, and which widgets that share is, so the Pane can
    // measure anything else in the row the ordinary way. Each visible widget's layout spacing is
    // spent whatever form it is in, so the spacing goes to `fixed` and the forms only carry widths.
    void measureHeader(relay::panes::HeaderWants &wants, QList<QWidget *> &mine) {
        auto *pane = dynamic_cast<Pane *>(parentWidget());
        QHBoxLayout *row = pane ? pane->headerLayout() : nullptr;
        if (!row) return;
        const int gap = row->spacing();
        const auto shown = [&mine](QWidget *widget) {
            if (widget) mine.append(widget);
            return widget && !widget->isHidden();
        };
        if (shown(m_glyph)) { wants.fixed += gap; wants.glyph = m_glyph->sizeHint().width(); }
        if (shown(m_subagentBadge)) { wants.fixed += gap; wants.badge = m_subagentBadge->sizeHint().width(); }
        if (shown(m_remoteChip)) {
            wants.fixed += gap;
            wants.ssh = m_remoteChip->fullWidth();
            wants.sshHost = m_remoteChip->hostWidth();
            wants.sshEllipsis = m_remoteChip->ellipsisWidth();
        }
        // The phone chip is in the row but not on the ladder: it is never asked to give way.
        if (shown(m_phoneChip)) { wants.fixed += gap; wants.chips += m_phoneChip->sizeHint().width(); }
        if (shown(m_usageChip)) {
            wants.fixed += gap;
            wants.usage = m_usageChip->fullWidth();
            wants.usageCpu = m_usageChip->cpuWidth();
        }
    }

    // What the ladder decided. Each widget's size hint follows the form it is given, so the row the
    // layout then builds is the row the ladder measured.
    void applyHeaderFit(const relay::panes::HeaderFit &fit) {
        if (m_remoteChip) {
            m_remoteChip->setForm(fit.ssh);
            m_remoteChip->setAllowedWidth(fit.sshPx);
        }
        if (m_usageChip) m_usageChip->setCpuOnly(fit.usage == relay::panes::UsageForm::CpuOnly);
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
            // A button whose tooltip is its live state (the share button) keeps it: the generic
            // label-plus-key text would replace it with nothing, knowing no label or key for it.
            if (b->property("liveTooltip").toBool()) continue;
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
        m_subagentBadge = new PaneSubagentBadge(this, pane);
        m_usageChip = new PaneUsageChip(pane);
        m_remoteChip = new PaneHeaderChip(relay::panestatus::Glyph::Remote);
        m_phoneChip = new PaneHeaderChip(relay::panestatus::Glyph::Phone);
        m_phoneChip->setText(QStringLiteral("phone"));
        m_phoneChip->setToolTip(QStringLiteral("Shared with your phone: it sees this pane and can type into it.\n"
                                               "The share button in the pane's top-right corner shows the code or stops it."));
        m_remoteChip->hide(); m_phoneChip->hide();
        row->insertWidget(0, m_glyph);
        // The subagent badge reads with the glyph: both are what this pane's agent is doing now
        // (card #YMSR). The safety chips (ssh, phone) keep their places to the right of them.
        row->insertWidget(1, m_subagentBadge);
        row->insertWidget(2, m_remoteChip);
        row->insertWidget(3, m_phoneChip);
        row->insertWidget(4, m_usageChip);
        m_backdrop = new RemoteBackdrop(pane);
        m_backdrop->hide();
        m_backdrop->lower();
        // The two halves of the header's give-way ladder meet here (measureHeader, applyHeaderFit).
        QPointer<PaneChrome> guard(this);
        pane->onHeaderWants = [guard](relay::panes::HeaderWants &wants, QList<QWidget *> &mine) {
            if (guard) guard->measureHeader(wants, mine);
        };
        pane->onHeaderFit = [guard](const relay::panes::HeaderFit &fit) {
            if (guard) guard->applyHeaderFit(fit);
        };
        header->installEventFilter(this);
        pane->installEventFilter(this);
    }

    // A terminal pane's share button (owner, 2026-09-19: it outgrew the prompt-box strip). The
    // pane keeps every bit of the share logic — Pane::shareChipPressed, the RemoteShare state —
    // and repaints this button through Pane::onShareChipChanged; the chrome owns only the button.
    void buildShare(QHBoxLayout *row, Pane *pane) {
        if (!row || !pane) return;
        m_share = new QToolButton(this);
        m_share->setObjectName(QStringLiteral("paneChromeButton"));
        m_share->setAutoRaise(true);
        m_share->setFocusPolicy(Qt::NoFocus);
        m_share->setCursor(Qt::PointingHandCursor);
        const QString icon = relay::theme::themeDataDir() + QStringLiteral("/icons/share.svg");
        if (QFileInfo::exists(icon)) m_share->setIcon(QIcon(icon)); else m_share->setText(QStringLiteral("↗"));
        m_share->setIconSize(QSize(13, 13));
        m_share->setProperty("liveTooltip", true);   // refreshTooltips() leaves its state text alone
        m_share->setAccessibleName(QStringLiteral("Share this pane with a phone"));
        connect(m_share, &QToolButton::clicked, pane, [pane] { pane->shareChipPressed(); });
        row->insertWidget(0, m_share);
        QPointer<PaneChrome> guard(this);
        pane->onShareChipChanged = [guard] { if (guard) guard->refreshShare(); };
        refreshShareFor(pane);
    }

    void refreshShare() { refreshShareFor(dynamic_cast<Pane *>(parentWidget())); }

    // The state the strip's chip used to wear (Pane::updateShareChip, before the move): the
    // agent's violet while shared, and a tooltip that says who is here rather than what the
    // button does.
    void refreshShareFor(Pane *pane) {
        if (!m_share || !pane) return;
        relay::RemoteShare &share = relay::RemoteShare::instance();
        const bool sharing = share.isSharing(pane->sessionToken());
        const int guests = share.sharingModel().guestsOn(pane->sessionToken());
        m_share->setProperty("dest", sharing ? QStringLiteral("agent") : QVariant());
        m_share->setToolTip(!sharing
            ? QStringLiteral("Share this pane with your phone, or invite someone to it")
            : guests == 0
                ? QStringLiteral("Shared — click for who is here, invites and what is waiting")
                : QStringLiteral("Shared with %1 · click for who is here and what is waiting")
                      .arg(guests == 1 ? QStringLiteral("one other person")
                                       : QStringLiteral("%1 other people").arg(guests)));
        m_share->style()->unpolish(m_share);
        m_share->style()->polish(m_share);
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
                    m_pulse->setSingleShot(true);
                    connect(m_pulse, &QTimer::timeout, this, [this] { update(); armPulse(); });
                }
                if (QApplication::cursorFlashTime() > 0) armPulse();
            } else if (m_pulse) {
                m_pulse->stop();
            }
            update();
        }
        // The phase is read off the wall clock, not counted per pane, and the timer is re-armed to
        // the next step boundary rather than left free-running. Two panes that went live seconds
        // apart used to blink seconds apart — three busy panes in a row each flashing on their own
        // beat is noise, not a signal — and now every live mark in every window is on the same step.
        // The grid itself is relay::panestatus::kPulseStepMs, which the tab's live dot reads too
        // (owner, 2026-09-19: one cadence), so the glyph and the dot above it are never out of step.
        static int phaseNow() { return relay::panestatus::pulsePhaseNow(); }
    protected:
        void paintEvent(QPaintEvent *) override {
            QPainter p(this);
            const relay::panestatus::Tokens t = relay::chrome::tokens();
            const relay::panestatus::State s = m_chrome->state();
            // The ground under a filled glyph's cut-out mark: the remote band when there is one.
            const QColor ground = m_chrome->remote() ? relay::panestatus::remoteStyle(t).fill : t.background;
            const bool blinking = m_pulse && m_pulse->isActive();
            relay::chrome::paintStateGlyph(p, QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), s, relay::panestatus::stateInk(s, t), ground,
                                           blinking ? relay::panestatus::pulseScale(phaseNow()) : 1.0);
        }
    private:
        void armPulse() { m_pulse->start(relay::panestatus::msToNextPulseStep()); }
        PaneChrome *m_chrome;
        QTimer *m_pulse = nullptr;
    };

    // "⇄ me@box" and "phone" in the title row: a glyph and a word on a small outlined chip.
    class PaneHeaderChip final : public QWidget {
    public:
        explicit PaneHeaderChip(relay::panestatus::Glyph glyph) : m_glyph(glyph) {
            setObjectName(glyph == relay::panestatus::Glyph::Remote ? QStringLiteral("paneRemoteChip") : QStringLiteral("panePhoneChip"));
            setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        }
        // The chip's own text, and — rung 4 of the ladder — the host on its own, which is what is
        // left of an "me@box" once the header cannot keep the chip at its 150 px floor. It is
        // derived here rather than passed in, so the phone chip, whose text has no `@` in it, has
        // one form and never gives way.
        void setText(const QString &text) {
            if (m_text == text) return;
            m_text = text;
            m_host = text.contains(QLatin1Char('@')) ? text.section(QLatin1Char('@'), -1) : text;
            updateGeometry(); update();
        }
        // Which of the two texts the chip paints, and the room the ladder gave it to paint it in.
        // The chip elides into that room itself, by whole glyphs, down to the glyph and a single
        // ellipsis — never half a letter of a host name.
        void setForm(relay::panes::SshForm form) { if (form == m_form) return; m_form = form; updateGeometry(); update(); }
        void setAllowedWidth(int pixels) { if (pixels == m_allowed) return; m_allowed = pixels; updateGeometry(); update(); }
        int fullWidth() const { return chipWidth(m_text); }
        int hostWidth() const { return chipWidth(m_host); }
        int ellipsisWidth() const { return chipWidth(QStringLiteral("…")); }
        // A phone chip that has to be noticed: painted as the remote-session chip is, because
        // "somebody else's keys are landing here" is the same warning.
        void setAlarm(bool alarm) { if (m_alarm == alarm) return; m_alarm = alarm; update(); }
        QSize sizeHint() const override {
            const int natural = m_form == relay::panes::SshForm::HostOnly ? hostWidth() : fullWidth();
            return {m_allowed > 0 ? std::min(natural, m_allowed) : natural, 18};
        }
        // Only a chip the ladder is driving may be squeezed at all, and never under the glyph and
        // one ellipsis. The phone chip is not on the ladder, so its minimum is what it shows.
        QSize minimumSizeHint() const override {
            return {m_allowed > 0 ? std::min(sizeHint().width(), ellipsisWidth()) : sizeHint().width(), 18};
        }
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
            // "me@box" loses its middle, so both the user and the machine survive; a host on its
            // own loses its tail, so the name that says which machine it is comes first.
            const bool host = m_form == relay::panes::SshForm::HostOnly;
            p.drawText(text, Qt::AlignLeft | Qt::AlignVCenter,
                       QFontMetrics(bold).elidedText(host ? m_host : m_text, host ? Qt::ElideRight : Qt::ElideMiddle,
                                                     int(text.width())));
        }
    private:
        int chipWidth(const QString &text) const {
            QFont bold = font(); bold.setWeight(QFont::DemiBold);
            return 7 + 12 + 5 + std::min(220, QFontMetrics(bold).horizontalAdvance(text)) + 8;
        }
        relay::panestatus::Glyph m_glyph;
        QString m_text, m_host;
        relay::panes::SshForm m_form = relay::panes::SshForm::UserAndHost;
        int m_allowed = 0;               // 0 = the ladder is not driving this chip
        bool m_alarm = false;
    };

    // The pane's resource meter (issues #D03W, #6BGA): "cpu 12% · mem 3%" in words, right of the
    // phone chip in the header row. Quiet on purpose — the body face, the header's muted ink for
    // the words, no band, colour only when a value is high — and absent while the pane costs
    // nothing worth reading, so an idle terminal looks exactly as it did before.
    //
    // It used to draw a 13 px processor die and a 13 px memory module before two bare percentages.
    // The owner, 2026-09-19: "the cpu / mem bar things are ugly and unintuitive. i think it should
    // be numbers." At that size the die's pins read as two stacks of bars and the module's legs as
    // a tiny bar chart, so the eye saw graphics where the meaning was entirely in the digits — and
    // the glyphs carried the only clue to which digit was which. The words say it instead, and
    // they are relay::usage::readingText(), the one string the tab label and the Sessions row and
    // the tooltips print too.
    class PaneUsageChip final : public QWidget {
    public:
        // `owner` is the pane whose header this chip sits in. It is passed in because the chip's
        // parent is that header widget, not the pane, so parentWidget() cannot find it — and
        // without it the title never learns that the chip appeared and never re-elides.
        explicit PaneUsageChip(Pane *owner) : m_owner(owner) {
            setObjectName(QStringLiteral("paneUsageChip"));
            setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            setAccessibleName(QStringLiteral("Pane CPU and memory"));
            // Nothing is known until the first sample lands, and a chip that is merely empty
            // still holds its ~58 px of header open. Start hidden, like the other chips.
            hide();
        }
        void setSample(const relay::usage::Sample &sample) {
            m_sample = sample;
            const bool show = relay::usage::worthShowing(sample) && relay::usage::metersEnabled();
            const QString next = show ? text() : QString();
            // isVisible() is recursive: every pane in a tab that is not the current one reports
            // false, so comparing against it would let this whole body run for every busy pane in
            // every background tab, 2.5 times a second. What this chip was last told is its own.
            if (show == m_shown && next == m_text) {
                // The ink warns at 60 % and 85 %, which a value can cross without the rounded
                // text moving ("60%" is 59.6 as well as 60.4), so a bucket change still repaints.
                if (m_inkBucket != inkBucket()) { m_inkBucket = inkBucket(); update(); }
                return;
            }
            m_shown = show;
            m_text = next;
            m_inkBucket = inkBucket();
            setToolTip(tooltipFor(sample));
            setVisible(show);
            updateGeometry();
            update();
            // Appearing or leaving changes what the title has to elide around.
            if (m_owner) m_owner->updateHeader();
        }
        // The chip itself stays the sum; its tooltip is where the sum is broken down, because
        // "30%" with nothing named is a number and not an answer. The busiest few processes get
        // a line each, the two roots named "shell" and "agent worker". A child that has already
        // exited still counts toward the sum, through its parent's cutime, and has no line —
        // there is no longer a process to name.
        static QString tooltipFor(const relay::usage::Sample &sample) {
            const QString breakdown = relay::usage::processBreakdown(sample);
            return QStringLiteral("This pane's share of this machine\n%1\n%2"
                                  "\nCounts the shell, the program it is running and this pane's agent "
                                  "worker, with their children. A remote pane measures the local ssh "
                                  "client, not the far machine.\n%3")
                .arg(relay::usage::describe(sample),
                     breakdown.isEmpty() ? QString()
                                         : QStringLiteral("\n") + breakdown + QStringLiteral("\n"),
                     relay::usage::memoryNote());
        }
        // The breakdown moves faster than the rounded number the chip paints, and setSample()
        // only rebuilds when that number moves — so the lines are written when the tooltip is
        // actually asked for, rather than spending a string per pane 2.5 times a second.
        bool event(QEvent *e) override {
            if (e->type() == QEvent::ToolTip) setToolTip(tooltipFor(m_sample));
            return QWidget::event(e);
        }
        // Rung 5 of the header's give-way ladder (relay::panes::headerFit), and the last rung there
        // is: the CPU half alone, with neither the memory half nor the separator, which is what
        // makes room for the ssh chip's host and the title's floor in a three-pane row.
        void setCpuOnly(bool cpuOnly) {
            if (cpuOnly == m_cpuOnly) return;
            m_cpuOnly = cpuOnly;
            updateGeometry();
            update();
        }
        int fullWidth() const { return widthFor(false); }
        int cpuWidth() const { return widthFor(true); }
        QSize sizeHint() const override { return {widthFor(m_cpuOnly), 18}; }
        QSize minimumSizeHint() const override { return sizeHint(); }
    protected:
        void paintEvent(QPaintEvent *) override {
            if (m_text.isEmpty()) return;
            const relay::panestatus::Tokens t = relay::chrome::tokens();
            QPainter p(this);
            p.setFont(font());
            const QFontMetrics metrics(font());
            const QList<relay::usage::ReadingPart> parts = relay::usage::readingParts(m_sample, m_cpuOnly);
            if (parts.isEmpty()) return;
            // Same guard as the state's word: a reading that has no room is left out whole
            // rather than clipped mid-digit. The ladder normally gets there first.
            int x = kPad;
            if (x + metrics.horizontalAdvance(relay::usage::readingText(m_sample, m_cpuOnly)) > width())
                return;
            for (const relay::usage::ReadingPart &part : parts) {
                const int advance = metrics.horizontalAdvance(part.text);
                p.setPen(part.value ? ink(t, part.percent) : t.muted);
                p.drawText(QRectF(x, 0, advance + 2, height()),
                           Qt::AlignLeft | Qt::AlignVCenter, part.text);
                x += advance;
            }
        }
    private:
        // The chip's own padding, left and right of the words.
        static constexpr int kPad = 7;
        // The warning and error inks at the thresholds, unchanged: a word stays muted whatever
        // it names, and only the number it labels warns.
        static QColor ink(const relay::panestatus::Tokens &t, double value) {
            return value >= 85 ? t.error : value >= 60 ? t.warning : t.muted;
        }
        // What the chip measures with the halves it would then show. Collapsed to CPU alone with
        // nothing in that half, there is nothing left to show, so it takes no room rather than
        // an empty box.
        int widthFor(bool cpuOnly) const {
            if (m_text.isEmpty()) return 0;
            const QString shown = relay::usage::readingText(m_sample, cpuOnly);
            if (shown.isEmpty()) return 0;
            return kPad + QFontMetrics(font()).horizontalAdvance(shown) + kPad;
        }
        QString text() const { return relay::usage::readingText(m_sample); }
        // Which colour band each half is in, as one comparable value.
        int inkBucket() const {
            const auto band = [](double v) { return v >= 85 ? 2 : v >= 60 ? 1 : 0; };
            return band(m_sample.cpuPercent) * 3 + band(m_sample.ramPercent);
        }
        Pane *m_owner = nullptr;
        relay::usage::Sample m_sample;
        QString m_text;      // "cpu 12% · mem 3%"; empty when the chip is hidden
        bool m_shown = false; // what this chip was last told, not the recursive isVisible()
        bool m_cpuOnly = false;  // the ladder's last rung: the CPU half and nothing else
        int m_inkBucket = 0;
    };

    // The subagent badge (card #YMSR): how many agents this pane's agent has running, right of the
    // state's word. Painted, not a QLabel, for the chips' reason — its ink follows the theme and
    // the ground it sits on — and hidden at zero, so a pane that has never started a subagent looks
    // exactly as it did before. A read-out, not a button: a click on the header moves the pane, and
    // the tooltip is where the key that opens the subagents pane is taught.
    class PaneSubagentBadge final : public QWidget {
    public:
        // `owner` is the pane whose header this badge sits in, passed in for the reason
        // PaneUsageChip gives: the layout reparents the badge to the header widget, so
        // parentWidget() is that header and never the Pane, and a dynamic_cast of it found
        // nothing — the title was never told to re-elide when the badge appeared or widened.
        PaneSubagentBadge(PaneChrome *chrome, Pane *owner) : m_chrome(chrome), m_owner(owner) {
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
            if (m_owner) m_owner->updateHeader();
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
        Pane *m_owner = nullptr;
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
    PaneSubagentBadge *m_subagentBadge = nullptr;
    PaneHeaderChip *m_remoteChip = nullptr, *m_phoneChip = nullptr;
    PaneUsageChip *m_usageChip = nullptr;
    QToolButton *m_share = nullptr;   // terminal panes: share this pane with a phone (was the strip's chip)
    RemoteBackdrop *m_backdrop = nullptr;
    relay::panestatus::State m_state = relay::panestatus::State::Idle;
    bool m_remote = false, m_phone = false, m_guestDriving = false;
    QString m_remoteHost;
};

