// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// The "Ask" row at the bottom of the Info (ⓘ) and Activity panes, and the questions it drafts
// (card #FEJQ, step 8).
//
// Those two panes are the only ones in the helper system with no helper agent of their own. The
// owner, 2026-09-20: "Info and Activity get no helper of their own: they are about the pane's own
// agent, so that agent gets read tools over its session info and activity and an 'Ask' row on
// those panes prefills its composer". So there is nothing here that talks to a worker: a click
// hands the question to the owning pane through the view's `onAskOwner`, the pane puts it at its
// composer's cursor and focuses it, and the person presses Enter — or does not. A **draft**,
// never a send, exactly as a clicked Check finding drafts `board::fixRequest` into the
// Switchboard's composer (src/BoardPane.h, owner 2026-09-19: "draft you confirm").
//
// The wording of every question lives in this one header, and both panes call it, so the Info row
// and the Activity row cannot drift apart the way two hand-written strings would. The questions
// are written for the pane agent's own read tools (`session_info`, `activity`) but never name
// them: a question that reads like a question is one the person can edit before sending, and the
// agent picks its own tool.
#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QList>
#include <QPalette>
#include <QString>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

#include <algorithm>
#include <functional>

namespace relay::askrow {

// ----- the wording -------------------------------------------------------------------------
//
// Every figure in a question is the figure the pane is showing at that moment, so the question
// the person sends and the screen they are looking at say the same number.

// "42 s", "3 min 20 s" — how long a turn took, in the words the row uses on its button and in the
// question, so the two cannot disagree.
inline QString howLong(qint64 milliseconds)
{
    if (milliseconds < 1000)
        return QStringLiteral("under a second");
    const qint64 seconds = (milliseconds + 500) / 1000;
    if (seconds < 60)
        return QStringLiteral("%1 s").arg(seconds);
    const qint64 minutes = seconds / 60;
    const qint64 rest = seconds % 60;
    return rest == 0 ? QStringLiteral("%1 min").arg(minutes)
                     : QStringLiteral("%1 min %2 s").arg(minutes).arg(rest);
}

// Info: the context meter. `usedOfWindow` is the pane's own "41.2k / 200.0k" cell, left empty when
// the worker reported no window.
inline QString contextQuestion(double percent, const QString &usedOfWindow = QString())
{
    QString question = QStringLiteral("Why is my context at %1% — what is taking the room?")
                           .arg(percent, 0, 'f', 1);
    if (!usedOfWindow.isEmpty())
        question += QStringLiteral(" (%1 tokens used.)").arg(usedOfWindow);
    return question;
}

// Info: the history table, in one ask.
inline QString summaryQuestion()
{
    return QStringLiteral("Summarise what this session has done so far — the turns in order, and "
                          "what came of each.");
}

// Info: the tokens and cost rows.
inline QString costliestTurnQuestion()
{
    return QStringLiteral("Which turn cost the most, and why?");
}

// Activity: the turn whose rule is last in the log.
inline QString lastTurnQuestion(qint64 milliseconds)
{
    return QStringLiteral("Why did the last turn take %1 — what took the time?")
        .arg(howLong(milliseconds));
}

// Activity: the tool rows.
inline QString slowestToolsQuestion()
{
    return QStringLiteral("Which tool calls were slowest this session, and what were they doing?");
}

// Activity: the turn the reader is looking at. The number is the one this pane counts, which is
// not the session's own when the pane was opened mid-conversation — so the request's first line
// goes with it, and the agent can find the turn either way.
inline QString turnQuestion(int number, const QString &request)
{
    const QString line = request.trimmed().section(QLatin1Char('\n'), 0, 0).left(120);
    return line.isEmpty()
               ? QStringLiteral("Explain what you did in turn %1: which tools you ran, and why.")
                     .arg(number)
               : QStringLiteral("Explain what you did in turn %1 (“%2”): which tools you ran, and "
                                "why.")
                     .arg(number)
                     .arg(line);
}

// ----- the row -----------------------------------------------------------------------------

// One ready question: what the chip says (it carries the live figure, so the reader sees it
// before clicking) and the text the click drafts.
struct Question {
    QString label;
    QString text;
    // So a redraw can ask whether the chips actually moved before it rebuilds them. A member,
    // not a free function: argument-dependent lookup inside QList's own operator== finds only
    // what is in this namespace.
    bool operator==(const Question &other) const
    {
        return label == other.label && text == other.text;
    }
};

// The row itself: a title, a muted detail saying what a click does, and the question chips under
// it. Two lines rather than one, because a pane beside a terminal is narrow and three chips and a
// sentence do not fit across it; it still reads as one block at the foot of the pane.
//
// The row is a plain QWidget with everything inline: it is compiled into both the Info pane's
// library and the Activity pane's, and two copies of an out-of-line symbol in one binary is one
// copy too many.
class AskRow final : public QWidget {
public:
    AskRow(const QString &title, const QString &detail, QWidget *parent = nullptr)
        : QWidget(parent), m_detailText(detail)
    {
        setObjectName(QStringLiteral("askRow"));
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 2, 0, 0);
        layout->setSpacing(3);
        auto *head = new QHBoxLayout;
        head->setSpacing(6);
        m_title = new QLabel(title, this);
        m_title->setObjectName(QStringLiteral("askRowTitle"));
        m_title->setTextFormat(Qt::PlainText);
        head->addWidget(m_title, 0);
        m_detail = new QLabel(detail, this);
        m_detail->setObjectName(QStringLiteral("dialogHint"));
        m_detail->setTextFormat(Qt::PlainText);
        // Muted from the palette rather than from a stylesheet rule: this row lives in two panes
        // with two object names above it, and the detail must read as an aside in both.
        QPalette muted = m_detail->palette();
        muted.setColor(QPalette::WindowText, QApplication::palette().color(QPalette::PlaceholderText));
        m_detail->setPalette(muted);
        head->addWidget(m_detail, 1);
        layout->addLayout(head);
        m_chips = new QHBoxLayout;
        m_chips->setSpacing(6);
        m_chips->setContentsMargins(0, 0, 0, 0);
        layout->addLayout(m_chips);
    }

    // The draft goes here; the view forwards it to its own `onAskOwner`. Unset means there is
    // nobody to draft into, and the view hides the whole row.
    std::function<void(const QString &text)> onAsk;

    // The chips. Their labels carry live figures — the Activity row's "Last turn · 42 s" moves
    // while a turn runs — so a button is never deleted to change its text: the buttons are made
    // once for a given count and rewritten in place after that. A row of buttons deleted and
    // remade under the pointer flickers and swallows the click that is already in flight.
    void setQuestions(const QVector<Question> &questions)
    {
        if (questions == m_questions)
            return;
        const bool sameCount = questions.size() == m_questions.size();
        m_questions = questions;
        if (!sameCount) {
            while (QLayoutItem *item = m_chips->takeAt(0)) {
                if (QWidget *widget = item->widget()) {
                    // Out of the layout is not off the screen: a deleteLater()d child keeps its
                    // old geometry and goes on painting there until the event loop gets round to
                    // it, which drew a ghost chip across this row's title. Disown it now.
                    widget->hide();
                    widget->setParent(nullptr);
                    widget->deleteLater();
                }
                delete item;
            }
            m_buttons.clear();
            for (int at = 0; at < m_questions.size(); ++at) {
                auto *chip = new QToolButton(this);
                chip->setObjectName(QStringLiteral("stripChip"));
                chip->setCursor(Qt::PointingHandCursor);
                chip->setFocusPolicy(Qt::TabFocus);
                // The question is read out of m_questions when the click happens, not captured
                // here, so rewriting a chip's text never leaves it drafting the old one.
                QObject::connect(chip, &QToolButton::clicked, this, [this, at] {
                    if (onAsk && at < m_questions.size())
                        onAsk(m_questions.at(at).text);
                });
                m_chips->addWidget(chip);
                m_buttons.append(chip);
            }
            m_chips->addStretch(1);
        }
        for (int at = 0; at < m_buttons.size(); ++at)
            m_buttons.at(at)->setText(m_questions.at(at).label);
        applyAvailability();
    }

    // Available: the chips are live. Unavailable: they are disabled and `why` says so, in the
    // tooltip and in the row's own detail line — a greyed button with no reason beside it is the
    // thing a reader files a bug about.
    void setAvailable(bool available, const QString &why = QString())
    {
        m_available = available;
        m_why = why;
        applyAvailability();
    }

    bool available() const { return m_available; }
    // For the tests and for a screenshot: what the chips say, in order.
    QStringList chipLabels() const
    {
        QStringList out;
        for (const QToolButton *chip : m_buttons)
            out << chip->text();
        return out;
    }
    // The draft the chip at `index` would hand over, without clicking it.
    QString draftAt(int index) const
    {
        return index >= 0 && index < m_questions.size() ? m_questions.at(index).text : QString();
    }
    QList<QToolButton *> chips() const { return m_buttons; }

private:
    void applyAvailability()
    {
        m_detail->setText(m_available || m_why.isEmpty() ? m_detailText : m_why);
        setToolTip(m_available ? QString() : m_why);
        for (int at = 0; at < m_buttons.size(); ++at) {
            QToolButton *chip = m_buttons.at(at);
            chip->setEnabled(m_available);
            chip->setToolTip(m_available
                                 ? m_questions.at(at).text
                                       + QStringLiteral("\n\nDrafts this in the prompt box of the "
                                                        "pane this one belongs to. It is not sent.")
                                 : m_why);
        }
    }

    QLabel *m_title = nullptr;
    QLabel *m_detail = nullptr;
    QHBoxLayout *m_chips = nullptr;
    QList<QToolButton *> m_buttons;
    QVector<Question> m_questions;
    QString m_detailText, m_why;
    bool m_available = true;
};

}  // namespace relay::askrow
