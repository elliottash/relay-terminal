// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PaneLayout.h"

#include <QLayout>
#include <QMargins>
#include <QSplitter>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace relay::panes {

bool towardStart(Direction direction) { return direction == Direction::Left || direction == Direction::Up; }

Qt::Orientation orientationFor(Direction direction) {
    return direction == Direction::Left || direction == Direction::Right ? Qt::Horizontal : Qt::Vertical;
}

Qt::Orientation dockOrientation(int anchorWidth) {
    return anchorWidth >= kDockBesideMinWidth ? Qt::Horizontal : Qt::Vertical;
}

int neighborIndex(const QRect &from, const QList<QRect> &candidates, Direction direction) {
    // A few pixels of slack, so a pane that starts level with this one still counts as beside it.
    constexpr int slack = 4;
    int best = -1;
    double bestScore = 0;
    for (int i = 0; i < candidates.size(); ++i) {
        const QRect &to = candidates.at(i);
        double gap = 0, offset = 0;
        switch (direction) {
        case Direction::Right:
            if (to.left() < from.right() - slack) continue;
            gap = to.left() - from.right();
            offset = std::abs(to.center().y() - from.center().y());
            break;
        case Direction::Left:
            if (to.right() > from.left() + slack) continue;
            gap = from.left() - to.right();
            offset = std::abs(to.center().y() - from.center().y());
            break;
        case Direction::Down:
            if (to.top() < from.bottom() - slack) continue;
            gap = to.top() - from.bottom();
            offset = std::abs(to.center().x() - from.center().x());
            break;
        case Direction::Up:
            if (to.bottom() > from.top() + slack) continue;
            gap = from.top() - to.bottom();
            offset = std::abs(to.center().x() - from.center().x());
            break;
        }
        // The nearest pane wins; among equally near ones, the one most in line with this pane.
        const double score = std::max(0.0, gap) * 4 + offset;
        if (best < 0 || score < bestScore) { bestScore = score; best = i; }
    }
    return best;
}

void swapInSplitter(QSplitter *splitter, QWidget *current, QWidget *neighbor) {
    if (!splitter || !current || !neighbor) return;
    const int to = splitter->indexOf(neighbor);
    if (to < 0 || splitter->indexOf(current) < 0) return;
    const QList<int> sizes = splitter->sizes();
    splitter->insertWidget(to, current);
    splitter->setSizes(sizes);
}

PlacementWindow::Response PlacementWindow::keyPress(int key, Qt::KeyboardModifiers modifiers, qint64 nowMs,
                                                    const QString &boundAction) {
    if (!m_armed) return {};
    // A modifier held down on its own is not yet a keystroke.
    if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta
        || key == Qt::Key_AltGr || key == Qt::Key_unknown || key == 0)
        return {};
    if (!armed(nowMs)) { m_armed = false; return {Action::Dismiss, Direction::Right}; }
    const auto mods = modifiers & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
    // A bare arrow places, and so does one with Ctrl still held (Shift may ride along, because
    // Ctrl+Shift+E leaves both down) — but only while the keymap leaves that chord free: a bound
    // shortcut keeps doing what it always does, so Alt+Left still focuses the pane to the left
    // and the konsole preset's Ctrl+Shift+Down still focuses below.
    const bool places = mods == Qt::NoModifier
        || ((mods == Qt::ControlModifier || mods == (Qt::ControlModifier | Qt::ShiftModifier))
            && boundAction.isEmpty());
    if (places) {
        switch (key) {
        case Qt::Key_Left: m_armed = false; return {Action::Place, Direction::Left};
        case Qt::Key_Up: m_armed = false; return {Action::Place, Direction::Up};
        case Qt::Key_Down: m_armed = false; return {Action::Place, Direction::Down};
        // Right means "yes, where it is". The window closes and the key is swallowed, so the
        // pane does not also scroll or move a cursor.
        case Qt::Key_Right: m_armed = false; return {Action::Place, Direction::Right};
        default: break;
        }
    }
    m_armed = false;
    return {Action::Dismiss, Direction::Right};
}

PlacementWindow::Response PlacementWindow::mousePress(qint64 nowMs) {
    Q_UNUSED(nowMs);
    if (!m_armed) return {};
    m_armed = false;
    return {Action::Dismiss, Direction::Right};
}

// The give-way ladder (owner, 2026-09-19; the order and the reasoning are in PaneLayout.h).
//
// One pass, the elements served in the REVERSE of the give-way order — the usage chip first, the
// directory last. Each of them is served against the header minus what the elements ABOVE it in
// the order would take at their FULL width, and minus the floors of the elements below it. Serving
// the usage chip against "the header minus everybody else's floor" is exactly what "the usage chip
// gives way last" means, and the same line read the other way is what "the directory gives way
// first" means; written as a list of rungs instead it would have been a dozen thresholds to keep in
// step with one another. The thresholds do meet: the title reaches its floor at the same width at
// which the ssh chip starts to squeeze, and the ssh chip reaches its floor at the same width at
// which the usage chip collapses.
//
// Full widths and not granted ones, because an element that has just given way must not hand its
// pixels to one that gave way before it: a title fed the pixels of a chip that had just given way
// would grow as the pane narrowed and shrink as it widened — the give-way order run backwards. Those
// pixels are simply not spent, and the stretch in the middle of the header row takes them, which
// is what it does with the empty space a narrow header has anyway.
//
// Every grant is therefore a clamp of a width-plus-a-constant, so it never falls as the header
// grows: the elements come back in the reverse order for free, and no width has two answers.
HeaderFit headerFit(int headerWidth, const HeaderWants &wants) {
    const auto positive = [](int value) { return std::max(0, value); };
    const int titleWant = positive(wants.title);
    // A title narrower than the floor asks for what it is, so the directory is not kept out by room
    // the title could not use.
    const int titleFloor = std::min(titleWant, kTitleFloorPx);
    const int directoryWant = positive(wants.directory);
    const int directoryFloor = directoryWant > 0 ? std::min(directoryWant, kDirectoryFloorPx) : 0;
    // No form of the ssh chip is wider than the form above it, whatever the host is called: a host
    // whose own name is wider than the 150 px floor is elided into it rather than making the chip
    // grow at the moment it drops the `user@`, which would undo the rung above.
    const int sshFull = positive(wants.ssh);
    const int sshSqueezed = sshFull > 0 ? std::min(sshFull, kSshFloorPx) : 0;
    const int sshHost = std::min(sshSqueezed, positive(wants.sshHost));
    const int sshFloor = std::min(sshHost, positive(wants.sshEllipsis));
    const int usageFull = positive(wants.usage);
    const int usageFloor = usageFull > 0 ? std::min(usageFull, positive(wants.usageCpu)) : 0;

    // What the glyph, the badge, the chips that never shrink and the row itself have taken already.
    const int fixed = positive(wants.fixed) + positive(wants.glyph) + positive(wants.badge) + positive(wants.chips);
    const int room = headerWidth - fixed;

    HeaderFit fit;
    // 4. The usage chip: whole while everything that outlives it still has its floor, CPU alone
    // after that. The memory half and the separator go together — "12%/" is not a reading.
    // Its two forms are the only two widths it has — it paints a number, it does not elide one —
    // so the allowance only picks the form and the form then says the width.
    fit.usage = room - sshFloor - titleFloor >= usageFull ? UsageForm::CpuAndMemory : UsageForm::CpuOnly;
    fit.usagePx = fit.usage == UsageForm::CpuAndMemory ? usageFull : usageFloor;

    // 3. The ssh chip: "user@host" down to the 150 px floor, then the host alone, then the host
    // elided to one ellipsis. The chip elides its own text into what it is given; the form only
    // says which of the two texts it elides.
    fit.sshPx = std::clamp(sshFull, sshFloor, std::max(sshFloor, room - usageFull - titleFloor));
    fit.ssh = fit.sshPx >= sshSqueezed ? SshForm::UserAndHost : SshForm::HostOnly;

    // 2. The title, elided from the right down to its floor.
    const int forTitle = room - usageFull - sshFull;
    fit.title = std::clamp(titleWant, titleFloor, std::max(titleFloor, forTitle));

    // 1. The directory gives way first: it has what is left once everything else is whole, and goes
    // rather than shows a stub when that is less than a legible tail.
    const int forDirectory = forTitle - titleWant;
    fit.directory = directoryWant > 0 && forDirectory >= directoryFloor ? std::min(directoryWant, forDirectory) : 0;

    // What is left when everything has given way all it can: the caller cannot do anything about it
    // (the badge and the glyph stay whole), but a test can say when a header is past its last rung.
    fit.shortfall = std::max(0, usageFloor + sshFloor + titleFloor - room);
    return fit;
}

// The two-line title (owner, 2026-09-20; the rules are on the declaration in PaneLayout.h). The
// first word always starts the first line even if it alone is wider than `px` — a line that
// cannot hold its first word is what the one-line elide is for, so a split that would clip a
// word mid-glyph returns empty instead.
QStringList twoLineTitle(const QString &title, const QFontMetrics &metrics, int px) {
    if (px <= 0) return {};
    const QStringList words = title.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.size() < 2 || metrics.horizontalAdvance(words.first()) > px) return {};
    QString first, rest;
    for (int i = 0; i < words.size(); ++i) {
        const QString candidate = first.isEmpty() ? words.at(i) : first + QLatin1Char(' ') + words.at(i);
        if (!first.isEmpty() && metrics.horizontalAdvance(candidate) > px) {
            rest = QStringList(words.mid(i)).join(QLatin1Char(' '));
            break;
        }
        first = candidate;
    }
    if (rest.isEmpty()) return {};   // everything fitted the first line: nothing to wrap
    if (metrics.horizontalAdvance(rest) > px) rest = metrics.elidedText(rest, Qt::ElideRight, px);
    return {first, rest};
}

Direction dropEdge(const QPoint &local, const QSize &size) {
    const double fx = double(local.x()) / std::max(1, size.width());
    const double fy = double(local.y()) / std::max(1, size.height());
    const double left = fx, right = 1 - fx, top = fy, bottom = 1 - fy;
    const double nearest = std::min({left, right, top, bottom});
    if (nearest == left) return Direction::Left;
    if (nearest == right) return Direction::Right;
    if (nearest == top) return Direction::Up;
    return Direction::Down;
}

QList<QPointer<QSplitter>> enclosingSplitters(QWidget *pane) {
    QList<QPointer<QSplitter>> splitters;
    for (QWidget *widget = pane ? pane->parentWidget() : nullptr; widget; widget = widget->parentWidget())
        if (auto *splitter = qobject_cast<QSplitter *>(widget)) splitters.append(splitter);
    return splitters;
}

QList<QPointer<QSplitter>> splittersIn(QWidget *root) {
    QList<QPointer<QSplitter>> splitters;
    if (!root) return splitters;
    if (auto *splitter = qobject_cast<QSplitter *>(root)) {
        splitters.append(splitter);
        for (int i = 0; i < splitter->count(); ++i) splitters += splittersIn(splitter->widget(i));
        return splitters;
    }
    const auto children = root->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget *child : children) splitters += splittersIn(child);
    return splitters;
}

std::optional<Direction> moveToward(const QRect &pane, const QRect &anchor) {
    if (pane.right() <= anchor.left()) return Direction::Right;   // the anchor is on its right
    if (pane.left() >= anchor.right()) return Direction::Left;
    return std::nullopt;                                          // one above the other already
}

bool chordKeyKeepsWindow(int key, const QString &actionId) {
    if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta
        || key == Qt::Key_AltGr || key == Qt::Key_unknown || key == 0)
        return true;
    return actionId == QLatin1String("pane.moveLeft") || actionId == QLatin1String("pane.moveRight")
           || actionId == QLatin1String("pane.moveDown");
}

QList<int> sizesAfterDock(const QList<int> &sizes, int anchorIndex, int anchorFloor) {
    if (anchorIndex < 0 || anchorIndex >= sizes.size()) return {};
    const int share = sizes.at(anchorIndex);
    if (share <= 0) return {};
    // Without a floor the anchor keeps half the share (the floor of the two halves, the newcomer
    // the ceil). A floor (card #BXCN) raises what the anchor keeps — up to the floor, or its
    // whole share when it is already narrower than that — and what it did not give the newcomer
    // comes proportionally from the panes beside it, so docking beside a Switchboard pane narrows
    // them and not the board's split.
    const int half = share / 2;
    const int keep = std::max(half, std::min(share, std::max(0, anchorFloor)));
    const int give = share - keep;
    const int wants = share - half;                 // the newcomer's half, the ceil of the two
    qint64 others = 0;
    for (int i = 0; i < sizes.size(); ++i)
        if (i != anchorIndex) others += std::max(0, sizes.at(i));
    qint64 lift = std::min<qint64>(wants - give, others);
    if (give + lift <= 0) {
        // Nothing anywhere for a floor to protect: divide the share alone, as before.
        QList<int> plain;
        plain.reserve(sizes.size() + 1);
        for (int i = 0; i < sizes.size(); ++i) {
            if (i != anchorIndex) { plain.append(sizes.at(i)); continue; }
            plain.append(half);
            plain.append(share - half);
        }
        return plain;
    }
    QList<int> out;
    out.reserve(sizes.size() + 1);
    int widestSizes = -1, widestOut = -1;
    qint64 keptTotal = 0;
    for (int i = 0; i < sizes.size(); ++i) {
        if (i != anchorIndex) {
            if (lift > 0 && others > 0) {
                out.append(int((qint64(sizes.at(i)) * (others - lift) + others / 2) / others));
                if (widestSizes < 0 || sizes.at(i) > sizes.at(widestSizes)) {
                    widestSizes = i;
                    widestOut = out.size() - 1;
                }
            } else {
                out.append(sizes.at(i));
            }
            keptTotal += out.last();
            continue;
        }
        out.append(keep);
        out.append(int(give + lift));
    }
    // The proportional scaling rounds; the widest of the panes that paid absorbs what is left
    // over, so the list still sums to what the splitter had. The anchor and newcomer are exact.
    if (widestOut >= 0)
        out[widestOut] += int(others - lift) - int(keptTotal);
    return out;
}

QList<int> sizesAfterEqualize(const QList<int> &sizes, const QList<int> &floors) {
    if (sizes.isEmpty() || floors.size() != sizes.size()) return {};
    qint64 total = 0, floorTotal = 0;
    for (int i = 0; i < sizes.size(); ++i) {
        total += std::max(0, sizes.at(i));
        floorTotal += std::max(0, floors.at(i));
    }
    if (total <= 0) return {};
    const int n = sizes.size();
    const auto plainEqual = [&]() {
        QList<int> equal;
        equal.reserve(n);
        for (int i = 0; i < n; ++i) equal.append(int(total / n));
        for (int i = 0; i < int(total % n); ++i) equal[i] += 1;   // floor then ceil, like a halving
        return equal;
    };
    // Unaffordable floors (their sum eats the total) equalize plainly rather than squeeze the
    // page out of shape: two Switchboard panes in one narrow tab each get their equal share.
    if (floorTotal >= total) return plainEqual();
    const qint64 equal = total / n;
    QList<bool> pinned;
    pinned.reserve(n);
    qint64 pinnedTotal = 0;
    int restCount = 0;
    for (int i = 0; i < n; ++i) {
        const bool pin = qint64(std::max(0, floors.at(i))) > equal;
        pinned.append(pin);
        if (pin) pinnedTotal += std::max(0, floors.at(i));
        else ++restCount;
    }
    // Only reachable with floors below the equal share everywhere: nothing to pin.
    if (restCount == 0) return plainEqual();
    // The pinned entries take their floor; the rest divide what is left equally, and so land at
    // or above the equal share, which already covers their own floors.
    const qint64 restTotal = total - pinnedTotal;
    const qint64 restShare = restTotal / restCount;
    QList<int> out;
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.append(pinned.at(i) ? std::max(0, floors.at(i)) : int(restShare));
    qint64 left = restTotal - restShare * restCount;
    for (int i = 0; left > 0 && i < n; ++i)
        if (!pinned.at(i)) { out[i] += 1; --left; }
    return out;
}

bool fillsTheEdge(const QRect &pane, const QRect &page, Direction direction) {
    // The few pixels of slack neighbourIndex allows, so a pane that exactly reaches the page's
    // boundary is not read as falling short of it by a rounding pixel.
    constexpr int slack = 4;
    if (direction == Direction::Left || direction == Direction::Right)
        return pane.top() <= page.top() + slack && pane.bottom() >= page.bottom() - slack;
    return pane.left() <= page.left() + slack && pane.right() >= page.right() - slack;
}

QList<int> sizesAfterEdgeDock(const QList<int> &sizes, bool atStart) {
    qint64 total = 0;
    for (int size : sizes) total += size;
    if (sizes.isEmpty() || total <= 0) return {};
    const qint64 share = total / (sizes.size() + 1);
    const qint64 rest = total - share;
    QList<int> kept;
    kept.reserve(sizes.size());
    int widest = 0, keptTotal = 0;
    for (int i = 0; i < sizes.size(); ++i) {
        kept.append(int((qint64(sizes.at(i)) * rest + total / 2) / total));
        keptTotal += kept.last();
        if (sizes.at(i) > sizes.at(widest)) widest = i;
    }
    // Proportional scaling rounds. The widest pane absorbs what the rounding left over, where
    // one pixel is least visible, so the list still sums to what the splitter had.
    kept[widest] += int(rest) - keptTotal;
    QList<int> out;
    out.reserve(sizes.size() + 1);
    if (atStart) out.append(int(share));
    out += kept;
    if (!atStart) out.append(int(share));
    return out;
}

void restoreSizes(const QList<QPointer<QSplitter>> &splitters, const QList<QList<int>> &sizes) {
    for (int i = 0; i < splitters.size() && i < sizes.size(); ++i) {
        QSplitter *splitter = splitters.at(i);
        if (!splitter || splitter->count() != sizes.at(i).size()) continue;
        splitter->setSizes(sizes.at(i));
    }
}

// ----- what a pane's minimum width is made of (card #SDXE) ------------------------------------

int layoutMinimumWidth(const QWidget *widget) {
    if (!widget) return 0;
    const QSizePolicy::Policy policy = widget->sizePolicy().horizontalPolicy();
    int minimum = 0;
    // Qt's own rule (qSmartMinSize): a widget that may shrink is held up by its minimum size hint,
    // and one that may not — QSizePolicy::Fixed — by the larger of the two hints, which is to say
    // by the size hint it is asking for. Ignored means the layout owes it nothing.
    if (policy != QSizePolicy::Ignored)
        minimum = (policy & QSizePolicy::ShrinkFlag)
                      ? widget->minimumSizeHint().width()
                      : std::max(widget->sizeHint().width(), widget->minimumSizeHint().width());
    minimum = std::min(minimum, widget->maximumWidth());
    // An explicit minimumWidth() REPLACES the computed floor rather than raising it, which is why
    // setMinimumWidth(1) on a QLabel takes its whole text out of the pane's minimum.
    if (widget->minimumWidth() > 0) minimum = widget->minimumWidth();
    return std::max(0, minimum);
}

QString headerMinimumsLine(const QString &pane, int paneWidth, int paneMinimum, const QLayout *row) {
    QStringList parts;
    int total = 0;
    if (row) {
        const QMargins margins = row->contentsMargins();
        total = margins.left() + margins.right();
        int shown = 0;
        for (int i = 0; i < row->count(); ++i) {
            QWidget *widget = row->itemAt(i)->widget();
            if (!widget || widget->isHidden()) continue;
            const int minimum = layoutMinimumWidth(widget);
            total += minimum + (shown++ ? row->spacing() : 0);
            parts << QStringLiteral("%1=%2").arg(widget->objectName().isEmpty()
                                                     ? QString::fromLatin1(widget->metaObject()->className())
                                                     : widget->objectName())
                                            .arg(minimum);
        }
    }
    return QStringLiteral("pane \"%1\" w=%2 min=%3 header=%4 | %5")
        .arg(pane).arg(paneWidth).arg(paneMinimum).arg(total).arg(parts.join(QLatin1Char(' ')));
}

bool layoutLogEnabled() {
    static const bool on = qEnvironmentVariableIsSet("RELAY_LAYOUT_LOG");
    return on;
}

}  // namespace relay::panes
