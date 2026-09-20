// SPDX-License-Identifier: AGPL-3.0-or-later
#include "BoardSignals.h"

#include "Theme.h"

#include <QComboBox>
#include <QDateEdit>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSet>
#include <QTextDocument>
#include <QTimeZone>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace relay {
namespace board {
namespace {

// The order the rows come in (research R11, minus the "new to my card" term, which needs a card):
// regressed first — it was fixed and came back, which is the loudest thing a signal can say —
// then broken before flaky, then the one that has failed most, then the key so the list never
// shuffles under the eye between two events.
int kindRank(const QString &kind)
{
    if (kind == QStringLiteral("build"))
        return 0;      // one cause, many failures: it is why the rest are not evaluated
    if (kind == QStringLiteral("run"))
        return 1;
    if (kind == QStringLiteral("group"))
        return 2;
    if (kind == QStringLiteral("broken"))
        return 3;
    return 4;          // flaky, and anything a newer worker invents
}

bool before(const Signal &a, const Signal &b)
{
    if (a.regressed != b.regressed)
        return a.regressed;
    const int left = kindRank(a.kind), right = kindRank(b.kind);
    if (left != right)
        return left < right;
    if (a.count != b.count)
        return a.count > b.count;
    return a.key < b.key;
}

QDateTime parseStamp(const QString &stamp)
{
    if (stamp.trimmed().isEmpty())
        return QDateTime();
    QDateTime at = QDateTime::fromString(stamp.trimmed(), Qt::ISODateWithMs);
    if (!at.isValid())
        at = QDateTime::fromString(stamp.trimmed(), Qt::ISODate);
    if (!at.isValid())
        return QDateTime();
    if (at.timeSpec() == Qt::LocalTime && stamp.trimmed().endsWith(QLatin1Char('Z')))
        at.setTimeZone(QTimeZone::utc());
    return at;
}

}  // namespace

Signal Signal::fromJson(const QJsonObject &object)
{
    Signal signal;
    signal.key = object.value(QStringLiteral("key")).toString();
    signal.source = object.value(QStringLiteral("source")).toString();
    signal.kind = object.value(QStringLiteral("kind")).toString();
    signal.state = object.value(QStringLiteral("state")).toString();
    signal.firstSeen = object.value(QStringLiteral("first_seen")).toString();
    signal.lastSeen = object.value(QStringLiteral("last_seen")).toString();
    signal.count = object.value(QStringLiteral("count")).toInt();
    signal.fingerprint = object.value(QStringLiteral("fingerprint")).toString();
    signal.regressed = object.value(QStringLiteral("regressed")).toBool();
    signal.stale = object.value(QStringLiteral("stale")).toBool();
    signal.session = object.value(QStringLiteral("session")).toString();
    signal.card = object.value(QStringLiteral("card")).toString();
    signal.fixedIn = object.value(QStringLiteral("fixed_in")).toString();
    signal.excerpt = object.value(QStringLiteral("excerpt")).toString();
    const QJsonObject dismissed = object.value(QStringLiteral("dismissed")).toObject();
    signal.dismissedReason = dismissed.value(QStringLiteral("reason")).toString();
    signal.dismissedUntil = dismissed.value(QStringLiteral("until")).toString();
    for (const QJsonValue &member : object.value(QStringLiteral("members")).toArray())
        signal.members << member.toString();
    return signal;
}

bool Signal::isGroup() const
{
    return kind == QStringLiteral("group") || kind == QStringLiteral("run")
           || kind == QStringLiteral("build");
}

QStringList dismissReasons()
{
    return {QStringLiteral("environmental"), QStringLiteral("flaky-known"),
            QStringLiteral("wont-fix"), QStringLiteral("expected")};
}

QString dismissReasonTitle(const QString &reason)
{
    if (reason == QStringLiteral("environmental"))
        return QStringLiteral("Environmental");
    if (reason == QStringLiteral("flaky-known"))
        return QStringLiteral("Known flaky");
    if (reason == QStringLiteral("wont-fix"))
        return QStringLiteral("Won't fix");
    if (reason == QStringLiteral("expected"))
        return QStringLiteral("Expected");
    return reason;
}

QString signalsFoldTitle(int count)
{
    return count == 1 ? QStringLiteral("1 signal") : QStringLiteral("%1 signals").arg(count);
}

QString signalsFoldTip(bool collapsed)
{
    // The word "signal" says nothing on its own, so the row's tooltip is where it is defined —
    // and it is defined by what the machine does with it, not by where it is stored.
    return QStringLiteral("Failures a machine opened and will close: failing tests, broken "
                          "builds. %1")
            .arg(collapsed ? QStringLiteral("Enter or → to show them.")
                           : QStringLiteral("Enter or ← to put them away."));
}

QString dismissedFoldTitle(int count)
{
    return count == 1 ? QStringLiteral("1 dismissed") : QStringLiteral("%1 dismissed").arg(count);
}

QString dismissedFoldTip(bool collapsed)
{
    return QStringLiteral("Signals put aside with a reason, until the dismissal expires — every "
                          "one does. %1")
            .arg(collapsed ? QStringLiteral("Enter or → to show them.")
                           : QStringLiteral("Enter or ← to put them away."));
}

QString signalKindWord(const QString &kind)
{
    if (kind == QStringLiteral("broken"))
        return QStringLiteral("broken");
    if (kind == QStringLiteral("flaky"))
        return QStringLiteral("flaky");
    if (kind == QStringLiteral("group"))
        return QStringLiteral("group");
    if (kind == QStringLiteral("run"))
        return QStringLiteral("run");
    if (kind == QStringLiteral("build"))
        return QStringLiteral("build");
    return kind;
}

QString signalStateWord(const QString &state)
{
    if (state.isEmpty())
        return QString();
    QString word = state;
    word[0] = word.at(0).toUpper();
    return word;
}

QString signalCountWord(int count)
{
    return count > 0 ? QStringLiteral("×%1").arg(count) : QString();
}

QString signalAge(const QString &stamp, const QDateTime &now)
{
    const QDateTime at = parseStamp(stamp);
    if (!at.isValid())
        return QString();
    const qint64 seconds = at.secsTo(now);
    if (seconds < 0)
        return QStringLiteral("just now");
    if (seconds < 60)
        return QStringLiteral("just now");
    if (seconds < 3600)
        return QStringLiteral("%1 min ago").arg(seconds / 60);
    const QDate day = at.toLocalTime().date(), today = now.toLocalTime().date();
    if (day == today)
        return QStringLiteral("%1 h ago").arg(seconds / 3600);
    if (day.addDays(1) == today)
        return QStringLiteral("yesterday");
    const QLocale c = QLocale::c();
    return day.year() == today.year() ? c.toString(day, QStringLiteral("MMM d"))
                                      : c.toString(day, QStringLiteral("MMM d yyyy"));
}

QString dismissalExpiry(const QString &until, const QDateTime &now)
{
    const QDateTime at = parseStamp(until);
    QDate day = at.isValid() ? at.toLocalTime().date()
                             : QDate::fromString(until.trimmed(), Qt::ISODate);
    if (!day.isValid())
        return QString();
    const qint64 days = now.toLocalTime().date().daysTo(day);
    if (days < 0)
        return QStringLiteral("expired");
    if (days == 0)
        return QStringLiteral("expires today");
    if (days == 1)
        return QStringLiteral("expires tomorrow");
    return QStringLiteral("expires in %1 days").arg(days);
}

QStringList signalMarks(const Signal &signal, const QDateTime &now)
{
    QStringList marks;
    // Two small muted words, in the order they matter: it came back, and nothing has run it
    // lately. Neither is a state — a regressed signal is `open` — so neither wears a pill.
    if (signal.regressed)
        marks << QStringLiteral("regressed");
    if (signal.stale)
        marks << QStringLiteral("stale");
    if (signal.dismissed()) {
        if (!signal.dismissedReason.isEmpty())
            marks << dismissReasonTitle(signal.dismissedReason).toLower();
        const QString expiry = dismissalExpiry(signal.dismissedUntil, now);
        if (!expiry.isEmpty())
            marks << expiry;
    }
    return marks;
}

QString signalRowLine(const Signal &signal, const QDateTime &now)
{
    QStringList parts{signalKindWord(signal.kind), signal.key};
    const QString count = signalCountWord(signal.count);
    if (!count.isEmpty())
        parts << count;
    const QString age = signalAge(signal.lastSeen, now);
    if (!age.isEmpty())
        parts << age;
    parts << signalMarks(signal, now);
    return parts.join(QStringLiteral(" · "));
}

QString signalSectionOf(const QString &body)
{
    // The machine's own block on a promoted card. It is rewritten on every state change, so this
    // reads it by heading and takes everything to the next `## ` — the worker may add fields
    // without the strip having to learn them.
    static const QRegularExpression heading(
            QStringLiteral("^##[ \\t]+signal[ \\t]*$"),
            QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
    const QRegularExpressionMatch match = heading.match(body);
    if (!match.hasMatch())
        return QString();
    const int from = match.capturedEnd();
    static const QRegularExpression next(QStringLiteral("^##[ \\t]+"),
                                         QRegularExpression::MultilineOption);
    const QRegularExpressionMatch end = next.match(body, from);
    const QString text = end.hasMatch() ? body.mid(from, end.capturedStart() - from)
                                        : body.mid(from);
    return text.trimmed();
}

QString bodyWithoutSignalSection(const QString &body)
{
    static const QRegularExpression heading(
            QStringLiteral("^##[ \\t]+signal[ \\t]*$"),
            QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
    const QRegularExpressionMatch match = heading.match(body);
    if (!match.hasMatch())
        return body;
    static const QRegularExpression next(QStringLiteral("^##[ \\t]+"),
                                         QRegularExpression::MultilineOption);
    const QRegularExpressionMatch end = next.match(body, match.capturedEnd());
    QString out = body.left(match.capturedStart());
    if (end.hasMatch())
        out += body.mid(end.capturedStart());
    // Two blank lines where a section was removed read as a gap nobody typed.
    return out.replace(QRegularExpression(QStringLiteral("\\n{3,}")), QStringLiteral("\n\n"));
}

// ------------------------------------------------------------------------ the state

bool SignalsState::take(const QString &type, const QJsonObject &event)
{
    if (type != QStringLiteral("signals_changed") && type != QStringLiteral("signals"))
        return false;
    m_seen = true;
    m_open.clear();
    m_dismissed.clear();
    m_promoted.clear();
    m_byKey.clear();
    m_pending = event.value(QStringLiteral("pending_count")).toInt();
    QList<Signal> open;
    for (const QJsonValue &value : event.value(QStringLiteral("open")).toArray()) {
        const Signal signal = Signal::fromJson(value.toObject());
        if (signal.key.isEmpty())
            continue;
        m_byKey.insert(signal.key, signal);
        if (signal.dismissed())
            m_dismissed << signal;          // a worker that sends one in `open` is taken at its word
        else
            open << signal;
    }
    for (const QJsonValue &value : event.value(QStringLiteral("dismissed")).toArray()) {
        const Signal signal = Signal::fromJson(value.toObject());
        if (signal.key.isEmpty() || m_byKey.contains(signal.key))
            continue;
        m_byKey.insert(signal.key, signal);
        m_dismissed << signal;
    }
    for (const QJsonValue &value : event.value(QStringLiteral("promoted")).toArray()) {
        const Signal signal = Signal::fromJson(value.toObject());
        if (!signal.key.isEmpty())
            m_promoted << signal;
    }
    // One list, one order (R11), and then one rule on top of it: **a group is followed by the keys
    // it names**, wherever the sort put it, so a run of twenty failures with one fingerprint reads
    // as one thing with twenty parts rather than twenty things. The group's own order is kept for
    // its members — it grouped them — and a member the event did not send as a row of its own is
    // simply not drawn. Members are taken out of their own place in the list, never listed twice.
    std::sort(open.begin(), open.end(), before);
    QSet<QString> claimed;
    for (const Signal &signal : std::as_const(open))
        if (signal.isGroup())
            for (const QString &key : signal.members)
                claimed.insert(key);
    for (const Signal &signal : std::as_const(open)) {
        if (!signal.isGroup() && claimed.contains(signal.key))
            continue;                     // it comes out under its group, below
        m_open << signal;
        if (!signal.isGroup())
            continue;
        for (const QString &key : signal.members)
            for (const Signal &member : std::as_const(open))
                if (member.key == key && !member.isGroup()) {
                    m_open << member;
                    break;
                }
    }
    // Soonest to expire first: a dismissal about to run out is the one thing in this list that is
    // about to become work again (R12: the only other thing that reaches a human unasked).
    std::sort(m_dismissed.begin(), m_dismissed.end(), [](const Signal &a, const Signal &b) {
        if (a.dismissedUntil != b.dismissedUntil)
            return a.dismissedUntil < b.dismissedUntil;
        return a.key < b.key;
    });
    return true;
}

void SignalsState::clear()
{
    m_open.clear();
    m_dismissed.clear();
    m_promoted.clear();
    m_byKey.clear();
    m_pending = 0;
    m_seen = false;
}

const Signal *SignalsState::signalFor(const QString &key) const
{
    const auto at = m_byKey.constFind(key);
    return at == m_byKey.constEnd() ? nullptr : &at.value();
}

QList<Row> SignalsState::rows(bool open, bool dismissedOpen) const
{
    QList<Row> out;
    if (m_open.isEmpty() && m_dismissed.isEmpty())
        return out;                  // no row says "no signals": nothing is the absence of a row
    // The members a group stands for, so a member row is indented under its group.
    QSet<QString> memberOf;
    for (const Signal &signal : m_open)
        if (signal.isGroup())
            for (const QString &key : signal.members)
                memberOf.insert(key);
    if (!m_open.isEmpty()) {
        Row fold;
        fold.kind = Row::SignalFold;
        fold.count = int(m_open.size());
        fold.title = signalsFoldTitle(fold.count);
        fold.collapsed = !open;
        out << fold;
        if (open) {
            for (const Signal &signal : m_open) {
                Row row;
                row.kind = Row::Signal;
                row.signalKey = signal.key;
                row.title = signal.key;
                row.count = signal.count;
                row.indent = memberOf.contains(signal.key) ? 1 : 0;
                out << row;
            }
        }
    }
    if (m_dismissed.isEmpty())
        return out;
    // The dismissed toggle is the last row of the block, open or closed: it is the quietest thing
    // here and it never comes before work that is still work.
    Row fold;
    fold.kind = Row::DismissedFold;
    fold.count = int(m_dismissed.size());
    fold.title = dismissedFoldTitle(fold.count);
    fold.collapsed = !dismissedOpen;
    out << fold;
    if (!dismissedOpen)
        return out;
    for (const Signal &signal : m_dismissed) {
        Row row;
        row.kind = Row::Signal;
        row.signalKey = signal.key;
        row.title = signal.key;
        row.count = signal.count;
        row.indent = 1;              // under the toggle that holds them, as a group's members are
        out << row;
    }
    return out;
}

// ------------------------------------------------------------------------ the detail

namespace {

QToolButton *actionButton(QWidget *parent, const QString &text, const QString &tip)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("boardCardButton"));
    button->setText(text);
    button->setToolTip(tip);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::StrongFocus);
    return button;
}

}  // namespace

SignalDetail::SignalDetail(QWidget *parent) : QWidget(parent)
{
    // Its own name, not the card page's `boardDetail`: `QObject::findChild` looks at every child
    // of a level before it recurses, so a label named like the card's — at depth 3 here against
    // the card's depth 4 — is what a test asking the view for `boardCardError` would find.
    setObjectName(QStringLiteral("boardSignalDetail"));
    setMinimumWidth(320);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(8);

    auto *top = new QHBoxLayout;
    top->setSpacing(6);
    m_key = new QLabel(this);
    m_key->setObjectName(QStringLiteral("boardSignalKey"));
    m_key->setWordWrap(true);
    QFont keyFont = m_key->font();
    keyFont.setPointSizeF(keyFont.pointSizeF() * 1.15);
    keyFont.setWeight(QFont::DemiBold);
    keyFont.setFamily(QFontDatabase::systemFont(QFontDatabase::FixedFont).family());
    m_key->setFont(keyFont);
    top->addWidget(m_key, 1);
    m_close = new QToolButton(this);
    m_close->setObjectName(QStringLiteral("boardCardClose"));   // the × of a page, one look
    m_close->setText(QStringLiteral("×"));
    m_close->setToolTip(QStringLiteral("Back to the board (Esc)"));
    m_close->setCursor(Qt::PointingHandCursor);
    QObject::connect(m_close, &QToolButton::clicked, this, [this] {
        if (onClose)
            onClose();
    });
    top->addWidget(m_close, 0, Qt::AlignTop);
    layout->addLayout(top);

    // Kind, state, when it was first and last seen, how often — the signal's whole front matter,
    // in the muted ink the card page's fields use.
    m_fields = new QLabel(this);
    m_fields->setObjectName(QStringLiteral("boardSignalFields"));
    m_fields->setWordWrap(true);
    m_fields->setTextFormat(Qt::RichText);
    m_fields->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    QObject::connect(m_fields, &QLabel::linkActivated, this, [this](const QString &link) {
        if (link.startsWith(QStringLiteral("relay-pane:")) && onFocusPane)
            onFocusPane(link.mid(11));
    });
    layout->addWidget(m_fields);

    m_marks = new QLabel(this);
    m_marks->setObjectName(QStringLiteral("boardSignalMarks"));
    m_marks->setWordWrap(true);
    m_marks->hide();
    layout->addWidget(m_marks);

    // The promoted card, as the `#ID` link the card's own `## Signal` section points back with:
    // the two halves of a promotion are one click from each other (decision 6).
    m_card = new QLabel(this);
    m_card->setObjectName(QStringLiteral("boardSignalCard"));
    m_card->setTextFormat(Qt::RichText);
    m_card->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    m_card->hide();
    QObject::connect(m_card, &QLabel::linkActivated, this, [this](const QString &link) {
        if (link.startsWith(QStringLiteral("card:")) && onOpenCard)
            onOpenCard(link.mid(5));
    });
    layout->addWidget(m_card);

    m_members = new QLabel(this);
    m_members->setObjectName(QStringLiteral("boardSignalMembers"));
    m_members->setWordWrap(true);
    m_members->hide();
    layout->addWidget(m_members);

    m_excerptHead = new QLabel(QStringLiteral("What failed"), this);
    m_excerptHead->setObjectName(QStringLiteral("boardEditHint"));
    layout->addWidget(m_excerptHead);
    // The failure's own words, monospaced and read-only: it is output, not prose, and a wrapped
    // assertion is unreadable.
    m_excerpt = new QPlainTextEdit(this);
    m_excerpt->setObjectName(QStringLiteral("boardSignalExcerpt"));
    m_excerpt->setReadOnly(true);
    m_excerpt->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_excerpt->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(m_excerpt, 1);

    m_error = new QLabel(this);
    m_error->setObjectName(QStringLiteral("boardSignalError"));
    m_error->setWordWrap(true);
    m_error->hide();
    layout->addWidget(m_error);

    // Dismiss's form, in the page rather than over it (owner's taste: panes and rows, never
    // floating strips). It is closed until Dismiss is pressed, and Dismiss then submits it.
    m_form = new QFrame(this);
    m_form->setObjectName(QStringLiteral("boardEdit"));
    auto *formLayout = new QVBoxLayout(m_form);
    formLayout->setContentsMargins(8, 6, 8, 6);
    formLayout->setSpacing(4);
    auto *formHint = new QLabel(QStringLiteral("Dismiss — every dismissal expires"), m_form);
    formHint->setObjectName(QStringLiteral("boardEditHint"));
    formLayout->addWidget(formHint);
    auto *formRow = new QHBoxLayout;
    formRow->setSpacing(6);
    m_reason = new QComboBox(m_form);
    // Not `boardPicker`, which the card page's status and category combos wear: a test that walks
    // every picker in the view must not find the dismissal's reason among them.
    m_reason->setObjectName(QStringLiteral("boardSignalReason"));
    for (const QString &reason : dismissReasons())
        m_reason->addItem(dismissReasonTitle(reason), reason);
    m_reason->setToolTip(QStringLiteral("Why: an agent may only write Environmental or Known "
                                        "flaky, and for at most seven days; the other two, and a "
                                        "longer expiry, are yours."));
    formRow->addWidget(m_reason);
    m_comment = new QLineEdit(m_form);
    m_comment->setObjectName(QStringLiteral("boardSignalComment"));
    m_comment->setPlaceholderText(QStringLiteral("Comment — what makes it not work, in one line"));
    formRow->addWidget(m_comment, 1);
    m_until = new QDateEdit(m_form);
    m_until->setObjectName(QStringLiteral("boardSignalUntil"));
    m_until->setCalendarPopup(true);
    m_until->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_until->setToolTip(QStringLiteral("Until: the day the dismissal expires and the signal is "
                                        "work again."));
    formRow->addWidget(m_until);
    formLayout->addLayout(formRow);
    auto *formButtons = new QHBoxLayout;
    formButtons->addStretch();
    QToolButton *cancel = actionButton(m_form, QStringLiteral("Cancel"),
                                       QStringLiteral("Leave the signal as it is (Esc)"));
    QToolButton *confirm = actionButton(m_form, QStringLiteral("Dismiss"),
                                        QStringLiteral("Put the signal aside with this reason "
                                                       "until the date above"));
    QObject::connect(cancel, &QToolButton::clicked, this, [this] { closeDismiss(); });
    QObject::connect(confirm, &QToolButton::clicked, this, [this] { submitDismiss(); });
    formButtons->addWidget(cancel);
    formButtons->addWidget(confirm);
    formLayout->addLayout(formButtons);
    m_form->hide();
    layout->addWidget(m_form);

    auto *actions = new QHBoxLayout;
    actions->setSpacing(6);
    m_claim = actionButton(this, QStringLiteral("Claim"),
                           QStringLiteral("Take the signal: this pane is working on it, and "
                                          "another session asking is told who has it"));
    m_release = actionButton(this, QStringLiteral("Release"),
                             QStringLiteral("Give the signal back: it is unclaimed again and any "
                                            "idle agent may pick it up"));
    m_dismiss = actionButton(this, QStringLiteral("Dismiss"),
                             QStringLiteral("Put it aside with a reason and an expiry"));
    m_promote = actionButton(this, QStringLiteral("Promote"),
                             QStringLiteral("Open a bug card for it, linked both ways"));
    QObject::connect(m_claim, &QToolButton::clicked, this, [this] { claim(); });
    QObject::connect(m_release, &QToolButton::clicked, this, [this] { release(); });
    QObject::connect(m_dismiss, &QToolButton::clicked, this, [this] {
        dismissOpen() ? submitDismiss() : openDismiss();
    });
    QObject::connect(m_promote, &QToolButton::clicked, this, [this] { promote(); });
    actions->addWidget(m_claim);
    actions->addWidget(m_release);
    actions->addWidget(m_dismiss);
    actions->addWidget(m_promote);
    actions->addStretch();
    layout->addLayout(actions);
}

void SignalDetail::showSignal(const Signal &signal, const QDateTime &now)
{
    const bool same = signal.key == m_signal.key && !m_signal.key.isEmpty();
    m_signal = signal;
    m_now = now.isValid() ? now : QDateTime::currentDateTimeUtc();
    if (!same) {
        closeDismiss();
        clearError();
    }
    render();
    QWidget::show();
}

void SignalDetail::render()
{
    m_key->setText(m_signal.key);
    QStringList fields;
    fields << signalKindWord(m_signal.kind).toHtmlEscaped();
    if (!m_signal.state.isEmpty())
        fields << signalStateWord(m_signal.state).toHtmlEscaped();
    if (!m_signal.source.isEmpty() && m_signal.source != m_signal.kind)
        fields << m_signal.source.toHtmlEscaped();
    const QString count = signalCountWord(m_signal.count);
    if (!count.isEmpty())
        fields << count;
    const QString first = signalAge(m_signal.firstSeen, m_now);
    if (!first.isEmpty())
        fields << QStringLiteral("first seen %1").arg(first.toHtmlEscaped());
    const QString last = signalAge(m_signal.lastSeen, m_now);
    if (!last.isEmpty())
        fields << QStringLiteral("last seen %1").arg(last.toHtmlEscaped());
    if (!m_signal.fixedIn.isEmpty())
        fields << QStringLiteral("fixed in %1").arg(m_signal.fixedIn.left(12).toHtmlEscaped());
    // The claim, as the chip a card wears (#R9G7) and a link to that pane, so the board and the
    // signal say the same thing about who has it.
    if (!m_signal.session.isEmpty()) {
        const bool live = !paneExists || paneExists(m_signal.session);
        const QString chip = sessionChip(m_signal.session, live).toHtmlEscaped();
        fields << (live ? QStringLiteral("<a href=\"relay-pane:%1\" style=\"color:%2;"
                                          "text-decoration:none\">%3</a>")
                                  .arg(m_signal.session.toHtmlEscaped(), theme::Link.name(), chip)
                        : QStringLiteral("<span style=\"color:%1\">%2</span>")
                                  .arg(theme::TextMuted.name(), chip));
    }
    m_fields->setText(QStringLiteral("<span style=\"color:%1\">%2</span>")
                              .arg(theme::TextMuted.name(),
                                   fields.join(QStringLiteral(" · "))));

    const QStringList marks = signalMarks(m_signal, m_now);
    // Amber is "a human should look at this" everywhere in Relay, and a signal that was fixed and
    // came back, or that nothing has run for a week, is exactly that.
    m_marks->setText(marks.isEmpty() ? QString()
                                     : QStringLiteral("<span style=\"color:%1\">%2</span>")
                                               .arg(theme::Warning.name(),
                                                    marks.join(QStringLiteral(" · ")).toHtmlEscaped()));
    m_marks->setTextFormat(Qt::RichText);
    m_marks->setVisible(!marks.isEmpty());

    m_card->setText(m_signal.card.isEmpty()
                            ? QString()
                            : QStringLiteral("<span style=\"color:%1\">Promoted to </span>"
                                             "<a href=\"card:%2\" style=\"color:%3;"
                                             "text-decoration:none\">#%2</a>")
                                      .arg(theme::TextMuted.name(),
                                           m_signal.card.toHtmlEscaped(), theme::Link.name()));
    m_card->setVisible(!m_signal.card.isEmpty());

    m_members->setTextFormat(Qt::RichText);
    m_members->setText(m_signal.members.isEmpty()
                               ? QString()
                               : QStringLiteral("<span style=\"color:%1\">Stands for %2: %3</span>")
                                         .arg(theme::TextMuted.name())
                                         .arg(m_signal.members.size())
                                         .arg(m_signal.members.join(QStringLiteral(", "))
                                                      .toHtmlEscaped()));
    m_members->setVisible(!m_signal.members.isEmpty());

    m_excerpt->setPlainText(m_signal.excerpt.isEmpty()
                                    ? QStringLiteral("(the run recorded no output for this key)")
                                    : m_signal.excerpt);
    m_claim->setEnabled(m_signal.session.isEmpty());
    m_release->setEnabled(!m_signal.session.isEmpty());
    m_promote->setEnabled(m_signal.card.isEmpty());
    m_promote->setToolTip(m_signal.card.isEmpty()
                                  ? QStringLiteral("Open a bug card for it, linked both ways")
                                  : QStringLiteral("Already promoted to #%1").arg(m_signal.card));
}

void SignalDetail::showError(const QString &text)
{
    // Red, and its own ink rather than `QLabel#boardCardError`'s: see the object names above.
    m_error->setTextFormat(Qt::RichText);
    m_error->setText(text.isEmpty() ? QString()
                                    : QStringLiteral("<span style=\"color:%1\">%2</span>")
                                              .arg(theme::Error.name(), text.toHtmlEscaped()));
    m_error->setVisible(!text.isEmpty());
}

void SignalDetail::clearError()
{
    m_error->clear();
    m_error->hide();
}

QString SignalDetail::error() const
{
    // The words, not the markup showError wrapped them in.
    if (m_error->isHidden())
        return QString();
    QTextDocument document;
    document.setHtml(m_error->text());
    return document.toPlainText();
}

void SignalDetail::claim()
{
    clearError();
    if (onClaim)
        onClaim();
}

void SignalDetail::release()
{
    clearError();
    if (onRelease)
        onRelease();
}

void SignalDetail::promote()
{
    clearError();
    if (onPromote)
        onPromote();
}

void SignalDetail::openDismiss()
{
    clearError();
    // Seven days is the agents' ceiling (decision 7) and the sensible default for the owner too:
    // a dismissal that outlives the reason for it is how a red test becomes permanent.
    if (!m_until->date().isValid() || m_until->date() < QDate::currentDate())
        m_until->setDate(QDate::currentDate().addDays(7));
    m_form->show();
    m_comment->setFocus();
}

void SignalDetail::closeDismiss()
{
    m_form->hide();
}

bool SignalDetail::dismissOpen() const
{
    return !m_form->isHidden();
}

QString SignalDetail::dismissReason() const
{
    return m_reason->currentData().toString();
}

QString SignalDetail::dismissComment() const
{
    return m_comment->text().trimmed();
}

QString SignalDetail::dismissUntil() const
{
    return m_until->date().toString(Qt::ISODate);
}

void SignalDetail::setDismissReason(const QString &reason)
{
    const int at = m_reason->findData(reason);
    if (at >= 0)
        m_reason->setCurrentIndex(at);
}

void SignalDetail::setDismissComment(const QString &comment)
{
    m_comment->setText(comment);
}

void SignalDetail::setDismissUntil(const QString &isoDate)
{
    const QDate day = QDate::fromString(isoDate, Qt::ISODate);
    if (day.isValid())
        m_until->setDate(day);
}

void SignalDetail::submitDismiss()
{
    if (!dismissOpen()) {
        openDismiss();
        return;
    }
    // A dismissal with no comment is a red test nobody can account for in a month's time. The
    // worker refuses one too; saying so here saves the round trip.
    if (dismissComment().isEmpty()) {
        showError(QStringLiteral("Say why in the comment: a dismissal with no reason written down "
                                 "is a failing test nobody can account for later."));
        m_comment->setFocus();
        return;
    }
    clearError();
    // The form closes on the way out: the dismissal is written, and a form left open invites a
    // second one. A refusal re-opens nothing — it lands on the error line above.
    closeDismiss();
    if (onDismiss)
        onDismiss(dismissReason(), dismissComment(), dismissUntil());
}

void SignalDetail::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        // Esc closes the form first and the page second, so one key never does two things at once.
        if (dismissOpen())
            closeDismiss();
        else if (onEscape)
            onEscape();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

}  // namespace board
}  // namespace relay
