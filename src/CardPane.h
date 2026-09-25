// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// CardDetail: the Board's card page, one card in full — its sections, thread, actions and the
// wiring points (std::function members) its host sets. Moved verbatim out of src/BoardPane.cpp
// for #Y2BA step 2 so a card pane (ToolPane::Kind::Card) and the Board list's preview can both
// host it without this file. BoardView remains the host that sets every callback.

#include "AgentContext.h"   // relay::agent::Action, the card's action row
#include "AppPaths.h"       // dataRoot, relayPython
#include "BoardModel.h"     // relay::board::{VerifyPlan, sessionChip, statusTitle, ...}
#include "BoardSignals.h"
#include "CopyOnSelect.h"    // installCopyOnSelect: the page's read-only text copies on select
#include "RichEditor.h"      // the card page's section editors   // relay::board::{signalSectionOf, bodyWithoutSignalSection}
#include "ModelCatalog.h"   // nameOf: a comment's `model=` id prints as a name
#include "Theme.h"          // relay::theme tokens the page paints with

#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QList>
#include <QMap>
#include <QMouseEvent>
#include <QPair>
#include <QPainter>
#include <QPaintEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QString>
#include <QStringList>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>
#include <utility>

namespace relay {

// Action depth across the Board UI, shared by the list side (src/BoardPane.cpp) and the card
// page below: a refresh raised from inside an action's `run` is posted to the next event-loop
// turn, because the console's action-row rebuild frees the button whose `clicked` is still on
// the stack. Moved here with CardDetail (#Y2BA) so both sides count into the one depth.
inline int &boardActionDepth()
{
    static int depth = 0;
    return depth;
}
struct ActionGuard {
    ActionGuard() { ++boardActionDepth(); }
    ~ActionGuard() { --boardActionDepth(); }
    ActionGuard(const ActionGuard &) = delete;
    ActionGuard &operator=(const ActionGuard &) = delete;
};

// A two-colour blend, as the band's tint uses below. BoardPane.cpp keeps its own file-local
// `mix` for the list side; this one is the header's, renamed so the two never collide.
// The flag at the head of a card row (#VKFV): an empty ring when the card carries no priority —
// unlit hardware, like the jack rings of an empty board — and a filled disc in the priority's
// own colour otherwise. The colours are the board's material tokens (Theme.h), so a theme
// switch recolours every flag and a light theme never paints white on cream.
inline void drawPriorityFlag(QPainter &painter, const QRect &box, int priority)
{
    const QColor ink = priority < 0 ? theme::BoardPriorityLow
                     : priority == 1 ? theme::BoardPriorityOne
                     : priority == 2 ? theme::BoardPriorityTwo
                     : priority >= 3 ? theme::BoardPriorityThree
                                     : QColor();
    const QPointF middle(box.center());
    const QRectF circle(QPointF(middle.x() - 4.5, middle.y() - 4.5), QSizeF(9, 9));
    if (ink.isValid()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(ink);
        painter.drawEllipse(circle);
    } else {
        painter.setPen(QPen(theme::BoardMetalDim, 1.2));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(circle.adjusted(0.5, 0.5, -0.5, -0.5));
    }
}

// The same flag as a control of its own (#DPJB), for the card page: the detail's header draws it
// with the row's own function, and a click shifts it exactly as a click on the row's does — a left
// click raises, a right click lowers. `onStep` is wired to `BoardView::setCardPriority`, so the page
// and the row write through one path and neither can drift from the other (clamped −1…+3).
class PriorityFlagButton final : public QWidget {
public:
    explicit PriorityFlagButton(QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("boardCardFlag"));
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setFixedSize(20, 20);           // the row's 15 px box, with the click's own room
        setPriority(0);
    }

    int priority() const { return m_priority; }

    void setPriority(int value)
    {
        m_priority = value;
        // The row's own words, so the tooltip reads the same wherever the flag is clicked.
        setToolTip(QStringLiteral("Priority %1 — left-click raises the flag, right-click lowers it")
                       .arg(value > 0 ? QStringLiteral("+%1").arg(value)
                                      : value < 0 ? QStringLiteral("−1")
                                                  : QStringLiteral("0")));
        update();
    }

    std::function<void(int step)> onStep;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        drawPriorityFlag(painter, rect(), m_priority);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        const bool left = event->button() == Qt::LeftButton;
        if ((left || event->button() == Qt::RightButton) && onStep) {
            onStep(left ? 1 : -1);
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

private:
    int m_priority = 0;
};

// The room the list keeps at its right: its frame plus the width a vertical scrollbar takes,
inline QColor cardMix(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

class CardDetail final : public QWidget {
public:
    explicit CardDetail(QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("boardDetail"));
        setAttribute(Qt::WA_StyledBackground);
        setMinimumWidth(320);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 10, 12, 10);
        layout->setSpacing(8);

        auto *top = new QHBoxLayout;
        top->setSpacing(6);
        // The card's priority flag, at the head of the header exactly as at the head of a row
        // (#DPJB): the list's own control, so a card can be flagged from the page it is read on.
        m_flag = new PriorityFlagButton(this);
        m_flag->onStep = [this](int step) {
            if (onPriority)
                onPriority(step);
        };
        top->addWidget(m_flag);
        m_ref = new QLabel(this);
        m_ref->setObjectName(QStringLiteral("boardCardRef"));
        top->addWidget(m_ref);
        // The ⧉ beside it (#FT77): the sign a copyable id wears in the info pane. One click and
        // the reference is on the clipboard — the row's ⧉ does the same for its card.
        m_refCopy = new QToolButton(this);
        m_refCopy->setObjectName(QStringLiteral("boardCardRefCopy"));
        m_refCopy->setText(QStringLiteral("⧉"));
        m_refCopy->setToolTip(QStringLiteral("Copy #ID to the clipboard"));
        m_refCopy->setCursor(Qt::PointingHandCursor);
        m_refCopy->setFocusPolicy(Qt::NoFocus);
        top->addWidget(m_refCopy);
        top->addStretch();
        m_toPrompt = textButton(QStringLiteral("#ID → prompt (t)"),
                                QStringLiteral("Insert this card's #ID in the terminal's prompt (t)"));
        m_openFile = textButton(QStringLiteral("Open file (o)"),
                                QStringLiteral("Open the card's Markdown file in a pane (o)"));
        top->addWidget(m_toPrompt);
        top->addWidget(m_openFile);
        m_close = new QToolButton(this);
        m_close->setObjectName(QStringLiteral("boardCardClose"));
        m_close->setText(QStringLiteral("×"));
        m_close->setToolTip(QStringLiteral("Close the card (Esc)"));
        m_close->setCursor(Qt::PointingHandCursor);
        top->addWidget(m_close);
        layout->addLayout(top);

        // The title, and at its right the pencil that edits it (owner, #VZ69: "there should be a
        // prominent pencil edit button, rather than the small 'edit' button at the top"). It sits
        // on the thing it edits rather than among the card's other tools, and it is outlined in the
        // accent, so it is the one control on the card that cannot be missed. Its key stays in the
        // label (#QG60), and the pencil says what the words mean before they are read.
        auto *titleRow = new QHBoxLayout;
        titleRow->setSpacing(8);
        m_title = new QLabel(this);
        m_title->setWordWrap(true);
        m_title->setObjectName(QStringLiteral("boardCardTitle"));
        m_title->setCursor(Qt::IBeamCursor);
        m_title->setToolTip(QStringLiteral("Click to edit the title (e)"));
        m_title->installEventFilter(this);
        titleRow->addWidget(m_title, 1);
        // The same line, as a field: editing swaps the two so the title never jumps.
        m_titleEdit = new QLineEdit(this);
        m_titleEdit->setObjectName(QStringLiteral("boardCardTitleEdit"));
        m_titleEdit->setPlaceholderText(QStringLiteral("Title — one line"));
        m_titleEdit->setToolTip(QStringLiteral("Enter saves, Esc cancels"));
        m_titleEdit->installEventFilter(this);
        m_titleEdit->hide();
        titleRow->addWidget(m_titleEdit, 1);
        m_edit = new QToolButton(this);
        m_edit->setObjectName(QStringLiteral("boardEditPencil"));
        m_edit->setText(QStringLiteral("✎ Edit (e)"));
        m_edit->setToolTip(QStringLiteral("Edit the title and the issue text (e)"));
        m_edit->setCursor(Qt::PointingHandCursor);
        m_edit->setFocusPolicy(Qt::NoFocus);
        titleRow->addWidget(m_edit, 0, Qt::AlignTop);
        // ⤴ (#Y2BA): this card in a pane of its own beside the board, so a second card can be
        // open at the same time. Hidden on a page that already is its own pane (pinSolo).
        m_popOut = new QToolButton(this);
        m_popOut->setObjectName(QStringLiteral("boardCardPopOut"));
        m_popOut->setText(QStringLiteral("⤴ Own pane"));
        m_popOut->setToolTip(QStringLiteral("Open this card in its own pane (Shift+Enter on its row)"));
        m_popOut->setCursor(Qt::PointingHandCursor);
        m_popOut->setFocusPolicy(Qt::NoFocus);
        m_popOut->hide();   // shown once the view says it can open a pane (setPopOutVisible)
        titleRow->addWidget(m_popOut, 0, Qt::AlignTop);
        // The owner's delete (#CYM9): the one action that takes a card off the board rather
        // than closing it, so it asks first and its Undo window is the only soft landing.
        m_delete = new QToolButton(this);
        m_delete->setObjectName(QStringLiteral("boardCardDelete"));
        m_delete->setText(QStringLiteral("⌫ Delete (Del)"));
        m_delete->setToolTip(QStringLiteral("Delete this card and its thread, after a confirm (Del)"));
        m_delete->setCursor(Qt::PointingHandCursor);
        m_delete->setFocusPolicy(Qt::NoFocus);
        titleRow->addWidget(m_delete, 0, Qt::AlignTop);
        layout->addLayout(titleRow);

        auto *pickers = new QHBoxLayout;
        pickers->setSpacing(6);
        m_status = new QComboBox(this);
        m_status->setObjectName(QStringLiteral("boardPicker"));
        m_status->setToolTip(QStringLiteral("Status: the column the card sits in"));
        m_tab = new QComboBox(this);
        m_tab->setObjectName(QStringLiteral("boardPicker"));
        m_tab->setToolTip(QStringLiteral("Category: the folder the card's file lives in"));
        pickers->addWidget(m_status);
        pickers->addWidget(m_tab);
        pickers->addStretch();
        m_done = new QToolButton(this);
        m_done->setObjectName(QStringLiteral("boardCardDone"));
        m_done->setText(QStringLiteral("Done (d)"));
        m_done->setToolTip(QStringLiteral("Mark this card done (d)"));
        m_done->setCursor(Qt::PointingHandCursor);
        m_done->setFocusPolicy(Qt::NoFocus);
        pickers->addWidget(m_done);
        connect(m_done, &QToolButton::clicked, this, [this] {
            if (onModeHint)
                onModeHint(QStringLiteral("done"));
            if (onDone)
                onDone();
        });
        layout->addLayout(pickers);

        // The machine's own block on a promoted card (#AQ6X): the card's `## Signal` section, as a
        // bordered strip rather than a heading in the middle of the body. The signal owns those
        // words and rewrites them on every state change, so they are not the owner's text to read
        // in sequence — they are the state of the fault the card was opened for, and they belong
        // over the card, beside its pickers, not inside its prose. (Under the verify line is where
        // #7BM4's `## Tests` strip goes; the two sit one above the other, each on its own card.)
        m_signalStrip = new QFrame(this);
        m_signalStrip->setObjectName(QStringLiteral("boardEdit"));
        auto *signalBox = new QVBoxLayout(m_signalStrip);
        signalBox->setContentsMargins(8, 6, 8, 6);
        signalBox->setSpacing(2);
        auto *signalHint = new QLabel(QStringLiteral("Signal — the machine's own words, rewritten "
                                                     "on every change"), m_signalStrip);
        signalHint->setObjectName(QStringLiteral("boardEditHint"));
        signalBox->addWidget(signalHint);
        m_signalStripText = new QLabel(m_signalStrip);
        m_signalStripText->setWordWrap(true);
        m_signalStripText->setObjectName(QStringLiteral("boardSignalSection"));
        m_signalStripText->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        m_signalStrip->hide();
        signalBox->addWidget(m_signalStripText);
        layout->addWidget(m_signalStrip);

        m_meta = new QLabel(this);
        m_meta->setWordWrap(true);
        m_meta->setTextFormat(Qt::RichText);
        // Links only, no text selection: theme::polishWindow() renames every selectable QLabel
        // to `cwd`, which would take these labels out of their own stylesheet rules.
        m_meta->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
        m_meta->setObjectName(QStringLiteral("boardCardMeta"));
        layout->addWidget(m_meta);
        // The one-line labels field (#E0Y0): opened by the + in the labels row, it shows the
        // card's labels comma separated; Enter saves the whole list, Esc closes it.
        m_labelEdit = new QLineEdit(this);
        m_labelEdit->setObjectName(QStringLiteral("boardCardLabelEdit"));
        m_labelEdit->setPlaceholderText(QStringLiteral("labels, comma separated"));
        m_labelEdit->setClearButtonEnabled(true);
        m_labelEdit->installEventFilter(this);
        m_labelEdit->hide();
        layout->addWidget(m_labelEdit);

        // Cross-provider QA (#T71W): one line under the fields, in the same muted ink, naming the
        // verifier the worker recommends for this card and what it skipped to get there. It is
        // there only for a card in a QA lane whose `qa` block arrived with it.
        m_verifyLine = new QLabel(this);
        m_verifyLine->setWordWrap(true);
        m_verifyLine->setObjectName(QStringLiteral("boardCardVerifyLine"));
        m_verifyLine->hide();
        layout->addWidget(m_verifyLine);

        // The `## Tests` strip (#7BM4): what the card says proves it, when it was last checked,
        // and the one button that checks it again. Placed exactly where the verify line is —
        // above the body rather than inside it — because the body is one Markdown document the
        // card renders in one go, and a widget cannot live inside a QTextBrowser. On screen only
        // for a card that has the section at all: on any other card it would be a control for a
        // list that is not there.
        m_testsStrip = new QWidget(this);
        m_testsStrip->setObjectName(QStringLiteral("boardTestsStrip"));
        auto *testsBox = new QVBoxLayout(m_testsStrip);
        testsBox->setContentsMargins(0, 0, 0, 0);
        testsBox->setSpacing(4);
        auto *testsHead = new QHBoxLayout;
        testsHead->setSpacing(6);
        m_testsLine = new QLabel(m_testsStrip);
        m_testsLine->setObjectName(QStringLiteral("boardTestsLine"));
        m_testsLine->setWordWrap(true);
        m_testsLine->setTextFormat(Qt::RichText);
        m_testsLine->setTextInteractionFlags(Qt::NoTextInteraction);
        testsHead->addWidget(m_testsLine, 1);
        m_testsCheck = new QPushButton(QStringLiteral("Check"), m_testsStrip);
        m_testsCheck->setObjectName(QStringLiteral("boardTestsCheck"));
        m_testsCheck->setFocusPolicy(Qt::NoFocus);
        m_testsCheck->setToolTip(QStringLiteral("Check the tests this card names: which are gone, "
                                                "have never run, are skipped for ever, flaky or "
                                                "slow. It runs nothing."));
        testsHead->addWidget(m_testsCheck, 0);
        testsBox->addLayout(testsHead);
        auto *findingsScroll = new QScrollArea(m_testsStrip);
        findingsScroll->setWidgetResizable(true);
        findingsScroll->setFrameShape(QFrame::NoFrame);
        findingsScroll->setMinimumHeight(80);
        findingsScroll->setMaximumHeight(180);
        auto *findingsContent = new QWidget;
        findingsScroll->setWidget(findingsContent);
        m_testsFindings = findingsScroll;
        m_testsFindings->setObjectName(QStringLiteral("boardTestsFindings"));
        m_testsFindingsBox = new QVBoxLayout(findingsContent);
        m_testsFindingsBox->setSizeConstraint(QLayout::SetMinAndMaxSize);
        m_testsFindingsBox->setContentsMargins(0, 0, 0, 0);
        m_testsFindingsBox->setSpacing(2);
        m_testsFindings->hide();
        testsBox->addWidget(m_testsFindings);
        m_testsActions = new QWidget(m_testsStrip);
        m_testsActions->setObjectName(QStringLiteral("boardTestsActions"));
        m_testsActionsBox = new QHBoxLayout(m_testsActions);
        m_testsActionsBox->setContentsMargins(0, 0, 0, 0);
        m_testsActionsBox->setSpacing(6);
        m_testsActionsBox->addStretch(1);
        m_testsActions->hide();
        testsBox->addWidget(m_testsActions);
        // The `## Tests` editor, inside the strip rather than over the card: it is about the
        // list right above it, and the body stays readable while the list is rewritten. Two
        // things open it — "Replace retired check" on a check that is no longer in the project,
        // and the gate asking which checks prove a card that names none (#PR4Q).
        m_testsEdit = new QFrame(m_testsStrip);
        m_testsEdit->setObjectName(QStringLiteral("boardTestsEdit"));
        auto *testsEditBox = new QVBoxLayout(m_testsEdit);
        testsEditBox->setContentsMargins(0, 4, 0, 0);
        testsEditBox->setSpacing(4);
        m_testsEditHint = new QLabel(m_testsEdit);
        m_testsEditHint->setObjectName(QStringLiteral("boardTestsEditHint"));
        m_testsEditHint->setWordWrap(true);
        testsEditBox->addWidget(m_testsEditHint);
        m_testsEditor = new QPlainTextEdit(m_testsEdit);
        m_testsEditor->setObjectName(QStringLiteral("boardTestsEditor"));
        m_testsEditor->setPlaceholderText(
                QStringLiteral("One check per line, as you would type it: `ctest -R board`, "
                               "`tests/test_board.py::CardTests::test_roundtrip`, "
                               "manual: docs/qa_evidence/…"));
        m_testsEditor->setMaximumHeight(120);
        testsEditBox->addWidget(m_testsEditor);
        auto *testsEditButtons = new QHBoxLayout;
        testsEditButtons->setSpacing(6);
        m_testsEditNone = new QPushButton(QStringLiteral("None apply"), m_testsEdit);
        m_testsEditNone->setObjectName(QStringLiteral("boardReplyButton"));
        m_testsEditNone->setFocusPolicy(Qt::NoFocus);
        m_testsEditNone->setToolTip(QStringLiteral("Say on the thread that no check in this "
                                                   "project proves this card, and move it."));
        m_testsEditNone->hide();
        testsEditButtons->addWidget(m_testsEditNone);
        testsEditButtons->addStretch(1);
        m_testsEditCancel = new QPushButton(QStringLiteral("Cancel"), m_testsEdit);
        m_testsEditCancel->setObjectName(QStringLiteral("boardReplyButton"));
        m_testsEditCancel->setFocusPolicy(Qt::NoFocus);
        m_testsEditSave = new QPushButton(QStringLiteral("Save to `## Tests`"), m_testsEdit);
        m_testsEditSave->setObjectName(QStringLiteral("primary"));
        m_testsEditSave->setFocusPolicy(Qt::NoFocus);
        testsEditButtons->addWidget(m_testsEditCancel);
        testsEditButtons->addWidget(m_testsEditSave);
        testsEditBox->addLayout(testsEditButtons);
        m_testsEdit->hide();
        testsBox->addWidget(m_testsEdit);
        m_testsStrip->hide();
        layout->addWidget(m_testsStrip);
        connect(m_testsCheck, &QPushButton::clicked, this, [this] { checkTests(); });
        connect(m_testsEditCancel, &QPushButton::clicked, this, [this] { closeTestsEditor(); });
        connect(m_testsEditSave, &QPushButton::clicked, this, [this] { saveTestsEditor(); });
        connect(m_testsEditNone, &QPushButton::clicked, this, [this] {
            closeTestsEditor();
            if (onTestsNoneApply)
                onTestsNoneApply();
        });

        m_doc = new QTextBrowser(this);
        m_doc->setObjectName(QStringLiteral("boardCardDocument"));
        m_doc->setOpenLinks(false);      // a relative link would otherwise replace the card
        m_doc->document()->setDocumentMargin(12);
        m_doc->installEventFilter(this);
        m_doc->viewport()->installEventFilter(this);
        relay::installCopyOnSelect(m_doc);
        layout->addWidget(m_doc, 1);

        // The `## Try it` strip (#JNYN, protocol 31.10). Same reasoning and same place as the
        // Tests strip above it: the body is one Markdown document rendered in one go, and the two
        // things this section needs — a button that opens the staged thing, and one line to
        // answer in — are widgets, which cannot live inside a QTextBrowser. The section's own
        // words stay in the body where they are read; this is the part you *act* on.
        m_tryStrip = new QWidget(this);
        m_tryStrip->setObjectName(QStringLiteral("boardTryStrip"));
        auto *tryBox = new QVBoxLayout(m_tryStrip);
        tryBox->setContentsMargins(0, 0, 0, 0);
        tryBox->setSpacing(4);
        auto *tryHead = new QHBoxLayout;
        tryHead->setSpacing(6);
        m_tryLine = new QLabel(m_tryStrip);
        m_tryLine->setObjectName(QStringLiteral("boardTestsLine"));
        m_tryLine->setWordWrap(true);
        m_tryLine->setTextFormat(Qt::RichText);
        m_tryLine->setTextInteractionFlags(Qt::NoTextInteraction);
        tryHead->addWidget(m_tryLine, 1);
        m_tryOpen = new QPushButton(QStringLiteral("Open it"), m_tryStrip);
        m_tryOpen->setObjectName(QStringLiteral("boardTryOpen"));
        m_tryOpen->setFocusPolicy(Qt::NoFocus);
        m_tryOpen->setCursor(Qt::PointingHandCursor);
        tryHead->addWidget(m_tryOpen, 0);
        tryBox->addLayout(tryHead);
        // One line, and Enter sends it. Not the card's reply box: an answer is not a message to
        // the agent, and putting it there would start a turn instead of recording a verdict.
        m_tryAnswer = new QLineEdit(m_tryStrip);
        m_tryAnswer->setObjectName(QStringLiteral("boardTryAnswer"));
        m_tryAnswer->setPlaceholderText(QStringLiteral("What did you see? Enter to answer — the "
                                                       "expected result is revealed afterwards"));
        m_tryAnswer->setClearButtonEnabled(true);
        tryBox->addWidget(m_tryAnswer);
        m_tryStrip->hide();
        // Above the document and under the `## Tests` strip, which is where the card's
        // machine-read strips live. Built here, after the document, so this block sits in
        // its own part of the file rather than inside the Tests strip's.
        layout->insertWidget(layout->indexOf(m_doc), m_tryStrip);
        connect(m_tryOpen, &QPushButton::clicked, this, [this] { openTry(); });
        connect(m_tryAnswer, &QLineEdit::returnPressed, this, [this] { answerTry(); });

        // The Verify strip (#WFRA, the QA ladder): what of the card's `verify:` block is the
        // person's business, one line in the same place and face as the Try it strip above it.
        // Most of the block is the agent's work and the user does not see it directly (owner
        // steer 2026-09-23), so the strip is shown only when there is something for them in it
        // — a review, a sign-off, or a deferral — and hidden otherwise. No button and no
        // editing: the block is written by the agent at planning and by `board_update_card`,
        // and this is only where it is read.
        m_verifyPlanLine = new QLabel(this);
        m_verifyPlanLine->setObjectName(QStringLiteral("boardVerifyStrip"));
        m_verifyPlanLine->setWordWrap(true);
        m_verifyPlanLine->setTextFormat(Qt::RichText);
        m_verifyPlanLine->setTextInteractionFlags(Qt::NoTextInteraction);
        layout->insertWidget(layout->indexOf(m_doc), m_verifyPlanLine);

        // Find inside the open card (#9NBZ). The terminal's FindBar is not reused: it is built
        // around a backend's scrollback and a conversation, and the board links neither — the
        // card's document is the only text on this page that scrolls for screens. The strip
        // sits directly over the document, hidden until `openFind` asks for it, so an unread
        // card keeps its whole first screen.
        m_findStrip = new QWidget(this);
        m_findStrip->setObjectName(QStringLiteral("boardCardFindStrip"));
        auto *findLayout = new QHBoxLayout(m_findStrip);
        findLayout->setContentsMargins(8, 4, 8, 4);
        findLayout->setSpacing(6);
        m_findEdit = new QLineEdit(m_findStrip);
        m_findEdit->setObjectName(QStringLiteral("boardCardFind"));
        m_findEdit->setPlaceholderText(QStringLiteral("Find in this card"));
        m_findEdit->setClearButtonEnabled(true);
        m_findEdit->installEventFilter(this);
        m_findCount = new QLabel(m_findStrip);
        m_findCount->setObjectName(QStringLiteral("boardCardFindCount"));
        m_findPrevious = new QToolButton(m_findStrip);
        m_findPrevious->setObjectName(QStringLiteral("boardFindPrev"));
        m_findPrevious->setText(QStringLiteral("↑"));
        m_findPrevious->setAutoRaise(true);
        m_findPrevious->setToolTip(QStringLiteral("Previous match (Shift+Enter)"));
        m_findNext = new QToolButton(m_findStrip);
        m_findNext->setObjectName(QStringLiteral("boardFindNext"));
        m_findNext->setText(QStringLiteral("↓"));
        m_findNext->setAutoRaise(true);
        m_findNext->setToolTip(QStringLiteral("Next match (Enter)"));
        m_findClose = new QToolButton(m_findStrip);
        m_findClose->setObjectName(QStringLiteral("boardFindClose"));
        m_findClose->setText(QStringLiteral("×"));
        m_findClose->setAutoRaise(true);
        m_findClose->setToolTip(QStringLiteral("Close (Esc)"));
        findLayout->addWidget(m_findEdit, 1);
        findLayout->addWidget(m_findCount);
        findLayout->addWidget(m_findPrevious);
        findLayout->addWidget(m_findNext);
        findLayout->addWidget(m_findClose);
        m_findStrip->hide();
        layout->insertWidget(layout->indexOf(m_doc), m_findStrip);
        connect(m_findEdit, &QLineEdit::textChanged, this, [this] { refreshFind(); });
        connect(m_findNext, &QToolButton::clicked, this, [this] { findStep(false); });
        connect(m_findPrevious, &QToolButton::clicked, this, [this] { findStep(true); });
        connect(m_findClose, &QToolButton::clicked, this, [this] { closeFind(); });

        // Editing the card's own words (`## Issue`). It takes the document's place rather than
        // opening beside it, so the card is either being read or being written, never both.
        m_editFrame = new QFrame(this);
        m_editFrame->setObjectName(QStringLiteral("boardEdit"));
        auto *editLayout = new QVBoxLayout(m_editFrame);
        editLayout->setContentsMargins(8, 6, 8, 6);
        editLayout->setSpacing(4);
        auto *editHint = new QLabel(QStringLiteral("Issue — Ctrl+Enter saves, Esc cancels"), m_editFrame);
        editHint->setObjectName(QStringLiteral("boardEditHint"));
        editLayout->addWidget(editHint);
        m_issueEdit = new QPlainTextEdit(m_editFrame);
        m_issueEdit->setObjectName(QStringLiteral("boardIssueEditor"));
        m_issueEdit->setPlaceholderText(QStringLiteral("What the card is about, in your own words"));
        m_issueEdit->installEventFilter(this);
        editLayout->addWidget(m_issueEdit, 1);
        auto *editButtons = new QHBoxLayout;
        editButtons->setSpacing(6);
        editButtons->addStretch(1);
        m_cancelEdit = new QPushButton(QStringLiteral("Cancel (Esc)"), m_editFrame);
        m_cancelEdit->setObjectName(QStringLiteral("boardReplyButton"));
        m_cancelEdit->setToolTip(QStringLiteral("Leave the card as it is (Esc)"));
        m_saveEdit = new QPushButton(QStringLiteral("Save (Ctrl+Enter)"), m_editFrame);
        m_saveEdit->setObjectName(QStringLiteral("primary"));
        m_saveEdit->setToolTip(QStringLiteral("Write the title and the issue to the card file (Ctrl+Enter)"));
        editButtons->addWidget(m_cancelEdit);
        editButtons->addWidget(m_saveEdit);
        editLayout->addLayout(editButtons);
        m_editFrame->hide();
        layout->addWidget(m_editFrame, 1);

        m_error = new QLabel(this);
        m_error->setObjectName(QStringLiteral("boardCardError"));
        m_error->setWordWrap(true);
        m_error->hide();
        layout->addWidget(m_error);

        // ---- the actions and the box are the console's now (card #AGNT step 6) ---------------
        //
        // Until 2026-09-20 this page built its own row of buttons and its own reply frame. Both
        // are the agent surface, and the owner's sentence — "an agent interface is the prompt
        // box" — makes the surface one thing: the console draws the action row from
        // `CardContext::actions()`, left-aligned above its busy line, each button wearing its
        // letter, and its composer *is* the reply box. What stays here is what is not the
        // conversation: the strip that says a card turn is running and stops it.
        //
        // The strip is the board's word for the card's turn: it names the mode and carries the ✕
        // (owner, #VZ69: "'stop' button isnt intuitive, it should be stop planning i guess, or
        // there should be an X next to 'agent planning'"). A card turn is an ordinary console
        // turn since card #CTRN — the transcript below draws it, and its ✕ sends `board_cancel`
        // addressed to this card — and the owner kept the strip anyway (decision 4): the console's
        // own busy line speaks for the conversation, this one speaks for the card.
        m_replyFrame = new QFrame(this);
        m_replyFrame->setObjectName(QStringLiteral("boardReply"));
        auto *replyLayout = new QVBoxLayout(m_replyFrame);
        replyLayout->setContentsMargins(8, 6, 8, 6);
        replyLayout->setSpacing(4);
        m_busyStrip = new QWidget(m_replyFrame);
        m_busyStrip->setObjectName(QStringLiteral("boardBusyStrip"));
        auto *busyRow = new QHBoxLayout(m_busyStrip);
        busyRow->setContentsMargins(0, 0, 0, 4);
        busyRow->setSpacing(6);
        m_busyLabel = new QLabel(m_busyStrip);
        m_busyLabel->setObjectName(QStringLiteral("boardBusyLabel"));
        busyRow->addWidget(m_busyLabel, 1);
        // What the turn is doing this second was a second label here until card #CTRN, elided to
        // one line ("reading Pane.h", "step 4/256") because a Plan turn is minutes of silent tool
        // calls before its first word. The tool rows in the transcript below say it row by row and
        // in full now, so the line has lost its reason to exist and the strip is label + ✕
        // (owner decision 4 on #CTRN).
        m_stop = new QToolButton(m_busyStrip);
        m_stop->setObjectName(QStringLiteral("boardStop"));
        m_stop->setCursor(Qt::PointingHandCursor);
        m_stop->setFocusPolicy(Qt::NoFocus);
        busyRow->addWidget(m_stop, 0);
        m_busyStrip->hide();
        replyLayout->addWidget(m_busyStrip);
        // Where the console goes once the view has one. Empty until then, and a card page with
        // no console (a test, relay-board on its own) shows the strip and nothing else.
        m_consoleHost = new QWidget(m_replyFrame);
        m_consoleHost->setObjectName(QStringLiteral("boardCardConsoleHost"));
        m_consoleBox = new QVBoxLayout(m_consoleHost);
        m_consoleBox->setContentsMargins(0, 0, 0, 0);
        m_consoleBox->setSpacing(0);
        replyLayout->addWidget(m_consoleHost);
        // The board's notice floats over this widget and has to stay clear of the controls, so
        // whoever places it is told when their height moves. It moves a good deal more than it
        // used to: the box is a console the window embeds after the page is built, and the
        // splitter sizes the card page again a turn later.
        m_replyFrame->installEventFilter(this);
        layout->addWidget(m_replyFrame);
        setModeTips();

        connect(m_stop, &QToolButton::clicked, this, [this] {
            if (m_busy && onCancel)
                onCancel();
        });
        connect(m_edit, &QToolButton::clicked, this, [this] {
            if (onEditHint)
                onEditHint();
            beginEdit(false);
        });
        connect(m_popOut, &QToolButton::clicked, this, [this] {
            if (onPopOut)
                onPopOut();
        });
        connect(m_delete, &QToolButton::clicked, this, [this] {
            if (onDeleteHint)
                onDeleteHint();
            remove();
        });
        connect(m_saveEdit, &QPushButton::clicked, this, [this] { saveEdit(); });
        connect(m_cancelEdit, &QPushButton::clicked, this, [this] { cancelEdit(); });
        connect(m_close, &QToolButton::clicked, this, [this] { if (onClose) onClose(); });
        connect(m_refCopy, &QToolButton::clicked, this, [this] {
            // The ⧉ (#FT77): the same copy the row's ⧉ makes.
            if (onCopyId)
                onCopyId(m_id);
            if (onCopyIdHint)
                onCopyIdHint();
        });
        connect(m_toPrompt, &QToolButton::clicked, this, [this] { if (onToPrompt) onToPrompt(); });
        connect(m_openFile, &QToolButton::clicked, this, [this] { if (onOpenPath) onOpenPath(m_path); });
        connect(m_meta, &QLabel::linkActivated, this, [this](const QString &link) {
            const QUrl url(link);
            // The labels editor (#E0Y0): × removes one label, + opens the labels field.
            // Both write through the same hash-checked onEdit patch the title saves with.
            if (url.scheme() == QStringLiteral("tagx")) {
                removeLabel(url.path().isEmpty() ? url.host() : url.path());
                return;
            }
            if (url.scheme() == QStringLiteral("tagadd")) {
                beginLabels();
                return;
            }
            if (url.scheme() == QStringLiteral("card")) {
                if (onOpenCard)
                    onOpenCard((url.path().isEmpty() ? url.host() : url.path()).toUpper());
                return;
            }
            if (url.scheme() == QStringLiteral("relay-commit")
                || url.scheme() == QStringLiteral("relay-commits")) {
                const QString hashes = url.path();
                static const QRegularExpression safe(QStringLiteral("^[0-9a-fA-F]+(?: [0-9a-fA-F]+)*$"));
                if (safe.match(hashes).hasMatch() && onOpenCommits)
                    onOpenCommits(QStringLiteral("git show --no-ext-diff ") + hashes);
                return;
            }
            // The claim chip (#R9G7) is the thread's own `relay-pane:` anchor, so it goes to the
            // same handler: a click on the session reveals the pane that took the card.
            if (url.scheme() == QStringLiteral("relay-pane")) {
                if (onFocusPane)
                    onFocusPane(url.path());
                return;
            }
            if (url.scheme() == QStringLiteral("tag")) {   // the labels row copies (#3ZAP)
                if (onCopyTag)
                    onCopyTag(url.path().isEmpty() ? url.host() : url.path());
                return;
            }
            if (onOpenPath)
                onOpenPath(link);
        });
        connect(m_doc, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
            // The thread's pane links (#HKAP): relay-pane:<session token> reveals that pane —
            // before the external branch, or the desktop would be asked to open it. Any other
            // scheme is a real URL; a bare path opens like a link in the body.
            if (url.scheme() == QStringLiteral("relay-pane")) {
                if (onFocusPane)
                    onFocusPane(url.path());
                return;
            }
            // A label copies its filter term; a `#ID` that names a card zooms to it (#3ZAP).
            if (url.scheme() == QStringLiteral("tag")) {
                if (onCopyTag)
                    onCopyTag(url.path().isEmpty() ? url.host() : url.path());
                return;
            }
            if (url.scheme() == QStringLiteral("card")) {
                if (onOpenCard)
                    onOpenCard((url.path().isEmpty() ? url.host() : url.path()).toUpper());
                return;
            }
            if (!url.scheme().isEmpty() && url.scheme() != QStringLiteral("file")) {
                QDesktopServices::openUrl(url);
                return;
            }
            if (onOpenPath)
                onOpenPath(url.isLocalFile() ? url.toLocalFile() : url.path());
        });
        connect(m_status, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
            if (!m_loading && onMove)
                onMove(QStringLiteral("status"), m_status->itemData(index).toString());
        });
        connect(m_tab, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
            if (!m_loading && onMove)
                onMove(QStringLiteral("tab"), m_tab->itemData(index).toString());
        });
    }

    // The agent console for this card (card #AGNT step 6), made by the window and handed down by
    // the view. It takes the reply frame's place under the busy strip, and **its composer is this
    // page's reply box**: `m_reply` is that editor from here on, so the drafts, the history, the
    // restore-on-refusal and the Esc walk below go on reading the one box the owner types in.
    //
    // The three actions are the console's row now, from `CardContext::actions()`. What is left
    // here is the busy strip, which belongs to the card turn rather than to the conversation.
    void setConsole(QWidget *console, RichEditor *editor)
    {
        if (console == nullptr || m_consoleBox == nullptr)
            return;
        m_consoleBox->addWidget(console);
        // The console's pane header is not drawn here, and not because this page hides it: a
        // console draws none at all (`Pane::applyContextHeader`). Its transcript is hidden until
        // it has something to show, which `ensureCardConsole` asks for — so the page reads
        // thread → action row → box, which is what the owner approved.
        //
        // And the frame steps back: the console brings the pane's own `QFrame#composer`, so this
        // page's `boardReply` frame would draw a border inside a border. src/Theme.cpp keys the
        // difference on this property; the widget stays because the busy strip is in it.
        if (m_replyFrame != nullptr) {
            m_replyFrame->setProperty("hasConsole", true);
            m_replyFrame->style()->unpolish(m_replyFrame);
            m_replyFrame->style()->polish(m_replyFrame);
        }
        // …and so does the console's **own** outer frame, which is the one that was still drawn.
        // `RelayWindow::wireAgentConsole` ends in `theme::polishWindow`, and `Pane` declares no
        // `Q_OBJECT`, so its `metaObject()->className()` is "QWidget" and that function names
        // every console `pane` — which is what gives a console a terminal pane's face wherever it
        // is embedded. On the Switchboard's list page, in Options and in Sessions that is right:
        // the transcript stands inside it and the prompt box inside that, exactly as in a pane.
        // On a card the transcript is hidden until it is used (above), so the pane frame hugged
        // the composer frame eight pixels out and the owner was looking at a border inside a
        // border (`docs/qa_evidence/2026-09-21-agents-are-consoles/punch/b02-card.png`). The
        // property says which case this is and src/Theme.cpp keys the frame on it; the console is
        // repolished by hand because a dynamic property does not restyle itself.
        console->setProperty("cardConsole", true);
        console->style()->unpolish(console);
        console->style()->polish(console);
        m_reply = editor;
        if (m_reply == nullptr)
            return;
        // The frame lights up when the box has the keyboard, exactly as it did when the editor
        // was this page's own widget; the console's own frame is inside it.
        m_reply->installEventFilter(this);
    }

    // The three chords, offered to this page by the console before it routes the line
    // (`relay::agent::Context::submit`, card #AGNT step 5). Ctrl+Shift+Enter is the composer's
    // "terminal, never the model" chord; on a card it means the same thing — a note on the thread
    // with no model call. Enter discusses, Ctrl+Enter plans, with what was typed as the owner's
    // note. Always handled: a card's Enter must travel as the `board_ask` of 19.10, which writes
    // the thread and advances the stage, never as an ordinary console `ask`.
    //
    // The text is read out of the reply box rather than taken as an argument because that box is
    // the console's own composer (`setConsole`), and `submit`/`plan` clear it themselves — which
    // is also what tells the pane there is nothing left for it to clear.
    bool submitFromConsole(const QString &route)
    {
        if (route == QStringLiteral("shell"))
            submit(QString());              // Ctrl+Shift+Enter: a note, with no model call
        else if (route == QStringLiteral("agent"))
            plan();                          // Ctrl+Enter
        else
            submit(QStringLiteral("discuss"));   // "auto": plain Enter
        return true;
    }
    RichEditor *replyEditor() const { return m_reply; }
    // The console embedded in this page, or null before the view has one.
    QWidget *console() const
    {
        return m_consoleBox != nullptr && m_consoleBox->count() > 0
                   ? m_consoleBox->itemAt(0)->widget()
                   : nullptr;
    }

    // Whether the cursor is in the reply box, for the keys that belong to a prompt box (#PK5Q).
    bool replyHasFocus() const { return m_reply != nullptr && m_reply->hasFocus(); }

    // `mode` is "discuss" or "plan" for an agent turn (protocol 19.10), empty for a plain comment.
    std::function<void(const QString &text, const QString &mode)> onReply;
    // Execute: hand the card to a terminal pane. `note` is what was in the reply box.
    std::function<void(const QString &note, bool background)> onExecute;
    // Verify (#T71W): hand the card to a terminal pane on the *recommended verifier*, which is a
    // different provider family from the one that implemented it. `note` is the reply box again.
    std::function<void(const QString &note)> onVerify;
    // Resume card (#FYEY): what Execute does, but the pane's first prompt is the card's orphan
    // listing (`land.py orphans`) and the landing steps, so the salvage reads `## Done means`
    // before landing anything. `note` is the reply box again.
    std::function<void(const QString &task, const QString &note)> onResume;
    // A `relay-pane:` anchor in the thread names a pane by session token (#HKAP). The card's
    // own claim chip is the same anchor through the same handler (#R9G7).
    std::function<void(const QString &token)> onFocusPane;
    // Whether the pane a card was claimed by is still open (#R9G7): read as the card is shown,
    // so the page picks up a closed pane at its next re-read. Unset means live.
    std::function<bool(const QString &token)> paneExists;
    // The header copy button preserves the card reference (#S53Z).
    std::function<void(const QString &id)> onCopyId;
    // A meta label copies its label: filter term (#S53Z).
    std::function<void(const QString &tag)> onCopyTag;
    // A click on the ⧉ beside the ref is the slow path: `y` is its key (#FT77, the RELAY.md hint
    // rule).
    std::function<void()> onCopyIdHint;
    // A `#ID` in the card's own words or the thread names another card on this board: zoom to
    // it, the way the cleanup panel's `card:` anchors do (#3ZAP).
    std::function<void(const QString &id)> onOpenCard;
    // Whether a word after a `#` names a card on this board (#3ZAP): the board decides whether
    // `#K7Q2` is a card reference — unknown words remain plain text.
    std::function<bool(const QString &id)> hasCard;
    std::function<void(const QString &mode)> onModeHint;   // a mode button was clicked, not keyed
    std::function<void()> onClose, onCancel, onToPrompt, onEscape;
    std::function<void()> onPopOut;          // ⤴: this card in its own pane (#Y2BA)
    void setPopOutVisible(bool visible) { m_popOut->setVisible(visible); }
    std::function<void(const QString &what, const QString &value)> onMove;
    // One click on the card page's priority flag (#DPJB): +1 for a left click, −1 for a right
    // one, exactly as the row's flag reports it. The view clamps and writes `board_priority`.
    std::function<void(int step)> onPriority;
    // A path relative to the workspace (the card file) or to the card (a link in its body).
    std::function<void(const QString &path)> onOpenPath;
    std::function<void(const QString &command)> onOpenCommits;
    // A `board_update` patch and the hash the edit started from, so the worker can refuse a
    // write over a file that changed meanwhile. The GUI never writes the file itself.
    std::function<void(const QJsonObject &patch, const QString &baseHash)> onEdit;
    std::function<void()> onEditHint;        // the Edit button was clicked, not the key
    // Delete (#CYM9): the trash button or the Del key was confirmed; the view sends board_delete.
    std::function<void()> onDone;
    std::function<void()> onDelete;
    std::function<void()> onDeleteHint;      // the Delete button was clicked, not the key

    // `~QWidget` deletes the children while this object's connections are still live, so a
    // `land.py orphans` ask still running (#FYEY) would emit `finished` from `~QProcess` into the
    // lambda above, which writes members already destroyed — heap corruption, and every Board
    // test that opened a card aborted at teardown. Cut it loose first.
    ~CardDetail() override
    {
        if (m_orphansProc) {
            m_orphansProc->disconnect(this);
            m_orphansProc->kill();
            m_orphansProc->waitForFinished(1000);
        }
    }

    QString cardId() const { return m_id; }
    // The reverse side of the Linked panel (#EE42, #9FX8): `board_links`'s `reverse` rows for this
    // card, drawn as a "Linked from" block under Links. Only what the card's own front matter
    // and `reverse` block have not already named: the cards that mention this one, were
    // discovered from it or supersede it, and how many case rows name it. An answer about a card
    // no longer showing is dropped.
    void setBacklinks(const QString &cardId, const QJsonArray &reverse)
    {
        if (cardId.isEmpty() || cardId != m_id)
            return;
        static const QList<QPair<QString, QString>> kShown = {
            {QStringLiteral("mentioned_in"), QStringLiteral("mentioned in")},
            {QStringLiteral("discovered"), QStringLiteral("discovered")},
            {QStringLiteral("superseded_by"), QStringLiteral("superseded by")}};
        QStringList parts;
        int cases = 0;
        QSet<QString> seen;
        for (const auto &[key, label] : kShown) {
            QStringList ids;
            for (const QJsonValue &value : reverse) {
                const QJsonObject row = value.toObject();
                if (row.value(QStringLiteral("name")).toString() != key)
                    continue;
                const QString from = row.value(QStringLiteral("from")).toString();
                if (!from.startsWith(QLatin1Char('#')))
                    continue;
                const QString id = from.mid(1);
                // Named already — by the Links block above, or by an earlier kind here.
                if (id == m_id || seen.contains(id)
                    || m_metaBase.contains(QStringLiteral("\"card:%1\"").arg(id)))
                    continue;
                seen.insert(id);
                ids << QStringLiteral("<a href=\"card:%1\">#%1</a>").arg(id.toHtmlEscaped());
            }
            if (ids.size() > 8)   // the panel links; it does not mirror the board
                ids = ids.mid(0, 8) << QStringLiteral("+%1 more").arg(ids.size() - 8);
            if (!ids.isEmpty())
                parts << QStringLiteral("%1 %2").arg(label, ids.join(QStringLiteral(", ")));
        }
        for (const QJsonValue &value : reverse)
            cases += value.toObject().value(QStringLiteral("name")).toString() == QStringLiteral("cases");
        if (cases > 0)
            parts << QStringLiteral("%1 case row%2").arg(cases).arg(cases == 1 ? QString() : QStringLiteral("s"));
        m_backlinks = parts.join(QStringLiteral(" · "));
        renderMeta();
    }
    QString backlinksText() const { return m_backlinks; }
    // The flag the view just wrote (#DPJB), shown at once so the disc turns under the click
    // rather than waiting for the worker's `board_changed`.
    void setPriority(int value) { m_flag->setPriority(value); }
    QString path() const { return m_path; }
    bool editing() const { return m_editing; }
    QString hash() const { return m_hash; }
    QString title() const { return m_title->text(); }
    QJsonObject front() const { return m_front; }
    QString status() const { return m_statusValue; }
    bool hasPlan() const { return m_sections.contains(QStringLiteral("Plan"), Qt::CaseInsensitive); }
    bool hasAcceptance() const { return !m_front.value(QStringLiteral("acceptance")).toString().trimmed().isEmpty(); }
    // The `## Signal` section this card is showing as a strip (#AQ6X), or empty on a card that has
    // none — which is every card but a promoted signal's.
    QString signalStrip() const { return m_signalStrip->isHidden() ? QString() : m_signalSection; }
    bool busy() const { return m_busy; }
    // The worker's `qa` block for this card, as it arrived (#T71W). Empty for a card it sent none
    // for — an old worker, or a card with no `implemented_by` yet.
    QJsonObject qa() const { return m_qa; }
    // Whether this card is in a verify lane at all (#3XZV): `needs-verification`, the
    // implementer's checklist being checked, or one of the QA lanes after it. That, and a `qa`
    // block, is what puts the verify line and its button on screen.
    bool inVerifyLane() const
    {
        return m_statusValue == QStringLiteral("needs-verification")
               || m_statusValue.startsWith(QStringLiteral("needs-qa"));
    }
    // Whether there is something to open: the worker found a verifier that is not the implementer
    // and is actually on this machine.
    bool hasVerifier() const { return !board::verifyRunner(m_qa).isEmpty(); }

    // ---- the `## Tests` strip and its Check (#7BM4) ----------------------------------------
    //
    // Whether the card lists the tests that prove it. `sections` arrives with the card, so this
    // is the same question `hasPlan()` asks, about the other machine-read section.
    bool hasTests() const
    {
        return m_sections.contains(QStringLiteral("Tests"), Qt::CaseInsensitive);
    }
    // One `tests_*` request for the tab's board worker (protocol 31.1); the view stamps it with
    // a request id and sends it. The GUI never writes the card itself: Check's dated block and
    // the lines "Add the tests this card's commits touched" appends are both the worker's
    // writes, so an agent's check and the owner's leave the same artefact.
    std::function<void(const QJsonObject &message)> onTestsRequest;
    // "None apply" in the gate's question: the view records the answer on the thread and sends
    // the move that was refused again (#PR4Q).
    std::function<void()> onTestsNoneApply;
    // The gate asked this card which checks prove it, and the strip is asking. Whether the box
    // is the question or the ordinary `## Tests` edit.
    bool m_testsAsking = false;

    // Press Check. Silent about its own progress beyond the button's label: the answer is a
    // deterministic fold over discovery and history, so it comes back in well under a second.
    void checkTests()
    {
        if (m_id.isEmpty() || !onTestsRequest || !hasTests())
            return;
        m_testsCheck->setEnabled(false);
        m_testsCheck->setText(QStringLiteral("Checking…"));
        onTestsRequest(QJsonObject{{QStringLiteral("type"), QStringLiteral("tests_check")},
                                   {QStringLiteral("card"), m_id}});
    }

    // ---- Try it: the button, the section and the answer box (#JNYN, protocol 31.10) ---------
    //
    // Three surfaces, and no more: **Try it** on the action row starts the turn, the strip above
    // the body opens what it staged and takes the one-line answer, and the board's notice area
    // carries the run. The expected result is deliberately *not* here — it is sealed in the
    // evidence directory until the answer is in, which is the whole point of the feature
    // (Codex's review §B: "Give the problem without the answer").
    bool hasTryIt() const
    {
        return m_sections.contains(QStringLiteral("Try it"), Qt::CaseInsensitive);
    }
    // From needs-verification on: before that there is nothing built to try. It is the same lane
    // test Verify uses plus the lanes a card reaches after QA, because a card in Done is exactly
    // the one somebody may want to open and look at again.
    bool inTryLane() const
    {
        return inVerifyLane() || m_statusValue == QStringLiteral("needs-review")
               || m_statusValue == QStringLiteral("done");
    }
    // One `try_*` request for the tab's board worker (31.10); the view stamps it with a request
    // id and sends it, exactly as `onTestsRequest` does. The GUI never writes the section: the
    // turn writes `## Try it` and `try_answer` writes the verdict, the reveal and `## Human QA`,
    // so a person's answer and an agent's leave the same artefact.
    std::function<void(const QJsonObject &message)> onTryRequest;
    // What the section's "open" line names, when the person presses the button: a command to run
    // in a terminal pane, or a path/`relay://` link for the ordinary opener.
    std::function<void(const QString &target, const QString &kind)> onOpenTry;

    // Press Try it. The run itself takes minutes and reports in the board's notice area, so all
    // this does is send and say that it went.
    void tryIt()
    {
        if (m_id.isEmpty() || m_editing || !onTryRequest)
            return;
        onTryRequest(QJsonObject{{QStringLiteral("type"), QStringLiteral("try_run")},
                                 {QStringLiteral("card"), m_id}});
    }

    void openTry()
    {
        const TrySummary summary = readTryIt(m_body);
        if (summary.open.isEmpty() || !onOpenTry)
            return;
        onOpenTry(summary.open, summary.kind);
    }

    void answerTry()
    {
        const QString text = m_tryAnswer->text().trimmed();
        if (m_id.isEmpty() || text.isEmpty() || !onTryRequest)
            return;
        m_tryAnswer->clear();
        m_tryAnswer->setEnabled(false);          // re-enabled by the card's next render
        onTryRequest(QJsonObject{{QStringLiteral("type"), QStringLiteral("try_answer")},
                                 {QStringLiteral("card"), m_id},
                                 {QStringLiteral("answer"), text}});
    }

    // Find in the open card (#9NBZ). Reached from `find.inView` through BoardView::openFind;
    // it searches only this page's document, because the list page's find is the filter and
    // the conversation has its own.
    void openFind()
    {
        // Prefill from what the document has selected, as an editor's find does: Ctrl+F over
        // a word is almost always a search for that word. A selection that spans a line break
        // is a copy, not a needle, and is left out.
        const QString selected = m_doc->textCursor().selectedText();
        if (!selected.isEmpty() && !selected.contains(QChar(0x2029)))
            m_findEdit->setText(selected);   // textChanged runs the search itself
        m_findStrip->show();
        m_findEdit->setFocus();
        m_findEdit->selectAll();
    }

    void closeFind()
    {
        m_findStrip->hide();
        m_doc->setFocus();   // Esc gave up the search, not the card
    }

    // What the strip draws after every edit of the needle: the first match from the top and
    // how many there are. Called again by `render` when a new card lands under an open strip.
    void refreshFind()
    {
        const QString needle = m_findEdit->text();
        if (needle.isEmpty()) {
            m_findCount->clear();
            return;
        }
        // Start every search at the top of the card: where the last needle left the cursor is
        // not a place this one knows about.
        QTextCursor top = m_doc->textCursor();
        top.movePosition(QTextCursor::Start);
        m_doc->setTextCursor(top);
        const bool found = findStep(false);
        const int total = m_doc->toPlainText().count(needle, Qt::CaseInsensitive);
        m_findCount->setText(found ? (total == 1 ? QStringLiteral("1 match")
                                                : QStringLiteral("%1 matches").arg(total))
                                   : QStringLiteral("No matches"));
    }

    bool findStep(bool backwards)
    {
        const QString needle = m_findEdit->text();
        if (needle.isEmpty())
            return false;
        const QTextDocument::FindFlags flags =
            backwards ? QTextDocument::FindBackward : QTextDocument::FindFlags();
        // Case-insensitive by leaving FindCaseSensitively off, as the terminal's find is.
        if (m_doc->find(needle, flags))
            return true;
        // Wrap around the ends: the last match on the page is not the end of the search, and
        // neither is the first the beginning.
        QTextCursor wrap = m_doc->textCursor();
        wrap.movePosition(backwards ? QTextCursor::End : QTextCursor::Start);
        m_doc->setTextCursor(wrap);
        return m_doc->find(needle, flags);
    }

    // What the strip draws, read out of the section the turn wrote. The same shape-not-grammar
    // reading `relay_core.tryit_protocol.parse_section` does on the worker side, kept to the two
    // things a widget needs: the one line to open, and whether the seal has been broken.
    struct TrySummary {
        QString open;        // the command, path or relay:// link
        QString kind;        // "command", "path" or "link"
        QString question;
        bool revealed = false;
    };
    // One `## ` section's text, by heading. Its own copy rather than `readTests`'s inlined
    // version: the Tests strip is another card's work, and two readers of the same three lines
    // cost less than two sessions in one function.
    static QString sectionOf(const QString &body, const QString &want)
    {
        static const QRegularExpression heading(QStringLiteral("^##[ \\t]+(.+?)[ \\t]*$"),
                                                QRegularExpression::MultilineOption);
        int start = -1, end = body.size();
        QRegularExpressionMatchIterator headings = heading.globalMatch(body);
        while (headings.hasNext()) {
            const QRegularExpressionMatch match = headings.next();
            if (start >= 0) {
                end = match.capturedStart();
                break;
            }
            if (match.captured(1).trimmed().compare(want, Qt::CaseInsensitive) == 0)
                start = match.capturedEnd();
        }
        return start < 0 ? QString() : body.mid(start, end - start);
    }

    static TrySummary readTryIt(const QString &body)
    {
        TrySummary out;
        const QString section = sectionOf(body, QStringLiteral("Try it"));
        static const QRegularExpression code(QStringLiteral("`([^`]+)`"));
        const QStringList lines = section.split(QLatin1Char('\n'));
        for (const QString &raw : lines) {
            const QString line = raw.trimmed();
            if (line.isEmpty())
                continue;
            // `Expected: …` names the sealed file until somebody answers; afterwards the same
            // prefix carries the expected result itself. The file name is what tells them apart.
            if (line.startsWith(QStringLiteral("Expected:"))) {
                if (!line.contains(QStringLiteral("expected.md")))
                    out.revealed = true;
                continue;
            }
            if (out.open.isEmpty()) {
                const QRegularExpressionMatch match = code.match(line);
                if (match.hasMatch()) {
                    const QString candidate = match.captured(1).trimmed();
                    if (candidate.startsWith(QStringLiteral("relay://"))) {
                        out.open = candidate;
                        out.kind = QStringLiteral("link");
                    } else if (QRegularExpression(QStringLiteral("^[A-Za-z]:[\\\\/]")).match(candidate).hasMatch()
                               || candidate.startsWith(QStringLiteral("\\\\"))
                               || !candidate.contains(QLatin1Char(' '))) {
                        out.open = candidate;
                        out.kind = QStringLiteral("path");
                    } else {
                        out.open = candidate;
                        out.kind = QStringLiteral("command");
                    }
                    continue;
                }
            }
            if (out.question.isEmpty() && line.endsWith(QLatin1Char('?'))) {
                // The strip draws the question as a sentence, so the list marker the section
                // wrote it under ("3. Did it stop you?") and any bold markers come off first —
                // the same tidy `tryit_protocol._plain` does on the worker side.
                static const QRegularExpression marker(
                    QStringLiteral("^(?:[-*+]\\s+|\\d+[.)]\\s+)"));
                out.question = line;
                out.question.remove(marker);
                out.question.remove(QStringLiteral("**"));
            }
        }
        return out;
    }

    // The strip's own line, and whether the strip is there at all.
    void showTryIt()
    {
        const bool has = hasTryIt();
        m_tryStrip->setVisible(has);
        if (!has)
            return;
        const TrySummary summary = readTryIt(m_body);
        QString text = QStringLiteral("<span style=\"color:%1\">Review</span>")
                           .arg(theme::TextMuted.name());
        if (summary.revealed) {
            text += QStringLiteral(" <span style=\"color:%1\">· answered — the expected result is "
                                   "under the section</span>").arg(theme::TextMuted.name());
        } else if (!summary.question.isEmpty()) {
            QString question = summary.question;
            if (question.size() > 96)
                question = question.left(95) + QChar(0x2026);
            // Amber is "a person should look at this" everywhere in Relay, and an unanswered
            // question on a card is exactly that.
            text += QStringLiteral(" <span style=\"color:%1\">· %2</span>")
                        .arg(theme::Warning.name(), question.toHtmlEscaped());
        }
        m_tryLine->setText(text);
        m_tryOpen->setVisible(!summary.open.isEmpty());
        m_tryOpen->setToolTip(summary.open.isEmpty()
                                  ? QString()
                                  : QStringLiteral("Open the staged review: %1").arg(summary.open));
        m_tryAnswer->setEnabled(!m_id.isEmpty());
        m_tryAnswer->setVisible(!summary.revealed || !summary.question.isEmpty());
    }

    // The Verify strip's line (#WFRA): `board::verifyStripText` says the words, this only inks
    // them, in the muted colour throughout — the strip is a note about the card, not a call to
    // arms. Hidden when the plan leaves nothing for a person (owner steer 2026-09-23): a card
    // with no block at all and a fully machine-verified card look the same, which is the point.
    void showVerifyPlan(const QJsonValue &block)
    {
        const board::VerifyPlan plan = board::VerifyPlan::fromJson(block);
        const QString text = board::verifyStripText(plan);
        m_verifyPlanLine->setText(text.isEmpty() ? QString()
                                                 : QStringLiteral("<span style=\"color:%1\">%2</span>")
                                                       .arg(theme::TextMuted.name(),
                                                            text.toHtmlEscaped()));
        m_verifyPlanLine->setVisible(!text.isEmpty());
        m_verifyPlanLine->setToolTip(text.isEmpty() ? QString() : board::verifyPlanDetail(plan));
    }

    // A `tests_check` answer (31.2): the findings as clickable rows and at most three action
    // buttons, in the shape the helper panel's own findings list uses. Empty findings are one
    // short sentence and nothing else — the card's Check is silent when nothing moved.
    void showCheck(const QJsonObject &event)
    {
        m_testsCheck->setEnabled(true);
        m_testsCheck->setText(QStringLiteral("Check"));
        if (!hasTests())
            return;
        clearCheck();
        m_checkFiles = event.value(QStringLiteral("files")).toObject();
        m_checkIds = event.value(QStringLiteral("ids")).toArray();
        m_checkFailing = event.value(QStringLiteral("failing")).toArray();
        m_checkStatuses = event.value(QStringLiteral("statuses")).toArray();
        const QJsonArray findings = event.value(QStringLiteral("findings")).toArray();
        auto *head = new QLabel(m_testsFindings);
        head->setObjectName(QStringLiteral("boardTestsFinding"));
        head->setWordWrap(true);
        if (findings.isEmpty() && m_checkStatuses.isEmpty()) {
            head->setText(QStringLiteral("<span style=\"color:%1\">Check: nothing moved — every "
                                         "test this card names is collected, has run and "
                                         "passed.</span>").arg(theme::TextMuted.name()));
            m_testsFindingsBox->addWidget(head);
            m_testsFindings->show();
            showTests();
            return;
        }
        // The status line first: what each listed check says about *this* card (#PR4Q). The
        // findings under it are advisory — a flake, a slow test, a history that was reset — and
        // are read after the answer, not instead of it.
        head->setText(statusHeadText(m_checkStatuses, findings.size(),
                                     event.value(QStringLiteral("revision")).toString()));
        m_testsFindingsBox->addWidget(head);
        for (const QJsonValue &value : m_checkStatuses)
            addStatusRow(value.toObject());
        for (const QJsonValue &value : findings)
            addFindingRow(value.toObject());
        m_testsFindings->show();
        for (const QJsonValue &value : event.value(QStringLiteral("actions")).toArray())
            addActionButton(value.toString());
        m_testsActions->setVisible(m_testsActionsBox->count() > 1);
        showTests();
    }

    // A `tests_suggest` answer: the worker has appended what it found (or said why it found
    // nothing). The card re-reads itself from `board_changed`, so the strip only says what
    // happened in the line it already owns.
    void showSuggest(const QJsonObject &event)
    {
        const QString message = event.value(QStringLiteral("message")).toString();
        if (message.isEmpty())
            return;
        auto *row = new QLabel(m_testsFindings);
        row->setObjectName(QStringLiteral("boardTestsFinding"));
        row->setWordWrap(true);
        row->setText(QStringLiteral("<span style=\"color:%1\">%2</span>")
                         .arg(theme::TextMuted.name(), message.toHtmlEscaped()));
        m_testsFindingsBox->addWidget(row);
        m_testsFindings->show();
    }

    // Delete (#CYM9): hand the card to the view, which asks the one confirm every path asks
    // (asking here too would double-confirm: button -> onDelete -> deleteCard -> a second
    // dialog nobody answered). Not while a turn runs on the card — the worker would only
    // refuse it — and not mid-edit: what is on screen would be deleted underneath the person
    // typing into it.
    void remove()
    {
        if (m_id.isEmpty() || m_editing || !onDelete)
            return;
        if (m_busy) {
            showError(QStringLiteral("The agent is still answering on #%1. Stop it, or wait for "
                                     "it, before deleting the card.").arg(m_id));
            return;
        }
        onDelete();
    }

    // Plan: a turn that writes the card's `## Plan` (protocol 19.10). Words in the reply box go
    // with it as the owner's note; an empty box is fine — the card is the brief.
    void plan()
    {
        if (panePlanning()) {          // `p` on a card a pane is planning reveals it (#48S3)
            if (onFocusPane)
                onFocusPane(m_sessionToken);
            return;
        }
        // **Not** refused while a turn runs: a Plan pressed during a Discuss queues as a `plan`
        // item, and the mode rides the queue item, so the brief and the stage constraint are the
        // queued turn's rather than the running one's (card #CTRN, Planning notes 5).
        if (m_id.isEmpty() || m_editing || !onReply)
            return;
        const QString text = takeReply();
        m_error->hide();
        onReply(text, QStringLiteral("plan"));
    }

    // Refine (#6W9X): one `board_ask` of mode "refine". Like a Plan it needs no words, and a
    // press during a running turn queues behind it.
    void refine()
    {
        if (m_id.isEmpty() || m_editing || !onReply)
            return;
        const QString text = takeReply();
        m_error->hide();
        onReply(text, QStringLiteral("refine"));
    }

    // Execute: hand the card to a terminal pane's agent. A card with neither a plan nor an
    // acceptance line asks once, here on the card rather than in a dialog: the pane's agent would
    // be working from the issue text alone. The second press (or `r`) goes ahead.
    void execute(bool background = true)
    {
        if (paneExecuting()) {         // `r` on a card a pane is running reveals it (#48S3)
            if (onFocusPane)
                onFocusPane(m_sessionToken);
            return;
        }
        if (m_id.isEmpty() || m_editing || !onExecute)
            return;
        if (m_busy) {
            showError(QStringLiteral("The agent is still answering on #%1. Stop it, or wait for it, "
                                     "before handing the card to a pane.").arg(m_id));
            return;
        }
        if (!hasPlan() && !hasAcceptance() && m_executeArmed != m_id) {
            m_executeArmed = m_id;
            showError(QStringLiteral("#%1 has no plan and no acceptance yet, so the pane's agent "
                                     "would work from the issue alone. Run again (r) to hand "
                                     "it over as it is, or Plan (p) first.").arg(m_id));
            return;
        }
        m_executeArmed.clear();
        const QString note = takeReply();
        m_error->hide();
        onExecute(note, background);
    }

    // Verify (#T71W): hand the card to a pane on the recommended verifier. Unlike Execute it
    // changes no status — the card stays in its QA lane until the verifier's verdict moves it —
    // and there is nothing to arm: the checklist on the card is the brief, and a card with no
    // recommendation says so rather than opening a pane on nobody.
    // The card is in a pane's hands right now (#48S3): a session token whose pane is still open,
    // and a status that still says the pane is working on it. Once the card moves on or the pane
    // closes, the claim is only a record again. Execute, Plan and Verify each ask about their own
    // status, so their buttons name the pane instead of offering a second hand-off.
    bool paneInStatus(const QStringList &statuses) const
    {
        return m_sessionLive && statuses.contains(m_statusValue);
    }
    bool paneExecuting() const
    {
        return paneInStatus({QStringLiteral("executing"),
                             QStringLiteral("in-progress")});
    }
    bool panePlanning() const
    {
        return paneInStatus({QStringLiteral("planning")});
    }
    // A verifier pane claims the card in one of the QA lanes; Needs verification itself is the
    // card landed with nobody checking yet.
    bool paneVerifying() const
    {
        return m_sessionLive && m_statusValue.startsWith(QStringLiteral("needs-qa"));
    }

    void verify()
    {
        if (paneVerifying()) {         // `v` on a card a pane is verifying reveals it (#48S3)
            if (onFocusPane)
                onFocusPane(m_sessionToken);
            return;
        }
        if (m_id.isEmpty() || m_editing || !onVerify)
            return;
        if (m_busy) {
            showError(QStringLiteral("The agent is still answering on #%1. Stop it, or wait for it, "
                                     "before handing the card to a verifier.").arg(m_id));
            return;
        }
        if (!inVerifyLane()) {
            showError(QStringLiteral("#%1 has not landed for verification yet. Move it to Needs "
                                     "verification when it lands.").arg(m_id));
            return;
        }
        if (!hasVerifier()) {
            const QString why = board::verifyLine(m_qa);
            showError(why.isEmpty()
                          ? QStringLiteral("No verifier is available for #%1 yet.").arg(m_id)
                          : why);
            return;
        }
        const QString note = takeReply();
        m_error->hide();
        onVerify(note);
    }

    void setChoices(const QStringList &statuses, const QList<QPair<QString, QString>> &tabs)
    {
        m_loading = true;
        m_status->clear();
        for (const QString &status : statuses)
            m_status->addItem(board::statusTitle(status), status);
        m_tab->clear();
        for (const auto &tab : tabs)
            m_tab->addItem(tab.second, tab.first);
        m_loading = false;
    }

    void show(const QJsonObject &card)
    {
        m_loading = true;
        const QString id = card.value(QStringLiteral("card_id")).toString();
        const bool sameCard = id == m_id;
        if (!sameCard) {
            // Each card keeps its own unsent reply, so following the selection never carries a
            // draft over to the next card.
            if (m_reply != nullptr && !m_id.isEmpty())
                m_drafts.insert(m_id, m_reply->toPlainText());
            if (m_reply != nullptr) {
                // The console changes its disk draft key when the card context changes below.
                // Do not write this card's text under the previous card's key in the meantime.
                const QSignalBlocker blocker(m_reply);
                m_reply->setPlainText(m_drafts.take(id));
            }
            else
                m_drafts.remove(id);
            m_error->hide();
            m_labelEdit->hide();   // the labels field (#E0Y0) belongs to the previous card
            m_executeArmed.clear();
        }
        m_id = id;
        m_path = card.value(QStringLiteral("path")).toString();
        const QJsonObject front = card.value(QStringLiteral("front")).toObject();
        m_front = front;
        m_statusValue = card.value(QStringLiteral("status")).toString();
        m_done->setEnabled(m_statusValue != QStringLiteral("done"));
        m_sections.clear();
        for (const QJsonValue &heading : card.value(QStringLiteral("sections")).toArray())
            m_sections << heading.toString();
        const QString title = card.value(QStringLiteral("title")).toString();
        // What an edit writes over, and what it would have to be saved against. A card read
        // again while it is being edited (the file changed under us, or our own write was
        // refused) hands the editor a new hash and says so, rather than throwing the text away.
        const QString hash = card.value(QStringLiteral("hash")).toString();
        const QString issue = card.value(QStringLiteral("issue")).toString();
        if (m_editing && sameCard && (title != m_title->text() || issue != m_issue))
            showError(QStringLiteral("This card changed on disk while you were editing it. "
                                     "Save writes your text over that version (the thread keeps "
                                     "the old one); Esc drops your edit."));
        m_hash = hash;
        m_issue = issue;
        if (!m_editing || !sameCard) {
            m_issueEdit->setPlainText(issue);
            m_titleEdit->setText(title);
        }
        // `m_id` is set, so the flag is this card's; the row read the same key (#VKFV/#DPJB).
        m_flag->setPriority(front.value(QStringLiteral("priority")).toInt());
        m_ref->setText(QStringLiteral("#") + m_id);
        m_title->setText(title);
        const int status = m_status->findData(card.value(QStringLiteral("status")).toString());
        if (status >= 0)
            m_status->setCurrentIndex(status);
        const int tab = m_tab->findData(card.value(QStringLiteral("tab")).toString());
        if (tab >= 0)
            m_tab->setCurrentIndex(tab);
        m_tab->setVisible(m_tab->count() > 1);
        const QString sessionToken = front.value(QStringLiteral("session")).toString().trimmed();
        // Kept for the action row (#48S3): while this token names a pane that is still open and
        // the status still says the pane is building the card, Execute is not on offer.
        m_sessionToken = sessionToken;
        m_sessionLive = !sessionToken.isEmpty() && (!paneExists || paneExists(sessionToken));
        QString meta = metaText(front, card.value(QStringLiteral("tasks")).toArray(), m_path,
                                      sessionToken.isEmpty() || !paneExists
                                          || paneExists(sessionToken));
        const auto cardLink = [this](const QString &raw) {
            const QString id = raw.trimmed().remove(QLatin1Char('#')).toUpper();
            if (id.isEmpty()) return QString();
            if (hasCard && !hasCard(id))
                return QStringLiteral("#%1 (missing)").arg(id.toHtmlEscaped());
            return QStringLiteral("<a href=\"card:%1\">#%2</a>")
                .arg(id.toHtmlEscaped(), id.toHtmlEscaped());
        };
        QStringList extra;
        const QJsonArray children = card.value(QStringLiteral("children")).toArray();
        if (!children.isEmpty()) {
            int done = 0;
            QStringList rows;
            for (const QJsonValue &value : children) {
                const QJsonObject child = value.toObject();
                done += child.value(QStringLiteral("done")).toBool() ? 1 : 0;
                rows << QStringLiteral("%1 %2 · %3")
                            .arg(cardLink(child.value(QStringLiteral("id")).toString()),
                                 child.value(QStringLiteral("title")).toString().toHtmlEscaped(),
                                 child.value(QStringLiteral("status")).toString().toHtmlEscaped());
            }
            extra << QStringLiteral("<b>Children %1/%2</b><br>%3")
                         .arg(done).arg(children.size()).arg(rows.join(QStringLiteral("<br>")));
        }
        QStringList relations;
        const QJsonObject reverse = card.value(QStringLiteral("reverse")).toObject();
        const QString parent = reverse.value(QStringLiteral("child_of")).toString();
        if (!parent.isEmpty()) relations << QStringLiteral("child of %1").arg(cardLink(parent));
        const auto forward = [&](const QString &key, const QString &label) {
            const QJsonValue value = front.value(key);
            const QJsonArray ids = value.isArray() ? value.toArray() : QJsonArray{value};
            for (const QJsonValue &id : ids)
                if (id.isString() && !id.toString().isEmpty())
                    relations << QStringLiteral("%1 %2").arg(label, cardLink(id.toString()));
        };
        forward(QStringLiteral("blocked_by"), QStringLiteral("blocked by"));
        forward(QStringLiteral("duplicate_of"), QStringLiteral("duplicate of"));
        for (const QString &key : {QStringLiteral("blocks"), QStringLiteral("duplicated_by"),
                                   QStringLiteral("related_from")}) {
            const QString label = key == QStringLiteral("blocks") ? QStringLiteral("blocks")
                : key == QStringLiteral("duplicated_by") ? QStringLiteral("duplicated by")
                : QStringLiteral("related from");
            for (const QJsonValue &row : reverse.value(key).toArray())
                relations << QStringLiteral("%1 %2").arg(label,
                    cardLink(row.toObject().value(QStringLiteral("id")).toString()));
        }
        const QJsonObject storedLinks = front.value(QStringLiteral("links")).toObject();
        for (const QJsonValue &id : storedLinks.value(QStringLiteral("related")).toArray())
            relations << QStringLiteral("related %1").arg(cardLink(id.toString()));
        // The rest of the Linked panel (#EA37 (c), #9FX8): the `server:` this card builds
        // (#G9ZD) as plain text — opening the skill page is the Skills tab's job — and the
        // plan/evidence paths of `links.*`, which no other line shows.
        const QString server = front.value(QStringLiteral("server")).toString().trimmed();
        if (!server.isEmpty())
            relations << QStringLiteral("served by <b>%1</b>").arg(server.toHtmlEscaped());
        const auto linkPaths = [&storedLinks, &relations](const char *key) {
            QStringList paths;
            for (const QJsonValue &value : storedLinks.value(QLatin1String(key)).toArray()) {
                const QString path = value.toString().trimmed();
                if (!path.isEmpty())
                    paths << path.toHtmlEscaped();
            }
            if (!paths.isEmpty())
                relations << QStringLiteral("%1 %2").arg(QLatin1String(key),
                                                         paths.join(QStringLiteral(", ")));
        };
        linkPaths("plans");
        linkPaths("evidence");
        // The `#ID`s the body mentions that the relations above have not already named (#EA37
        // (c)): scanned out of the text, capped, and linked like every other card reference.
        const QRegularExpression mentionedIds(QStringLiteral("#([A-Z0-9]{4})(?![A-Z0-9])"));
        QStringList mentioned;
        auto mentionIterator = mentionedIds.globalMatch(issue);
        while (mentionIterator.hasNext()) {
            const QString id = mentionIterator.next().captured(1);
            if (id == m_id)
                continue;
            const bool alreadyNamed = relations.join(QLatin1Char(' '))
                                          .contains(QStringLiteral("\"card:%1\"").arg(id));
            if (alreadyNamed || mentioned.contains(id))
                continue;
            mentioned << id;
            relations << QStringLiteral("mentions %1").arg(cardLink(id));
            if (mentioned.size() == 6)
                break;
        }
        if (!relations.isEmpty())
            extra << QStringLiteral("<b>Links</b><br>%1").arg(relations.join(QStringLiteral(" · ")));
        const QJsonArray commits = card.value(QStringLiteral("commits")).toArray();
        if (!commits.isEmpty()) {
            QStringList rows, hashes;
            for (const QJsonValue &value : commits) {
                const QJsonObject commit = value.toObject();
                const QString hash = commit.value(QStringLiteral("hash")).toString();
                hashes << hash;
                rows << QStringLiteral("<a href=\"relay-commit:%1\">%1</a> %2 %3 "
                                       "<span style=\"color:%4\">%5 · %6</span>")
                            .arg(hash.toHtmlEscaped(),
                                 commit.value(QStringLiteral("date")).toString().toHtmlEscaped(),
                                 commit.value(QStringLiteral("subject")).toString().toHtmlEscaped(),
                                 theme::TextMuted.name(),
                                 commit.value(QStringLiteral("author")).toString().toHtmlEscaped(),
                                 commit.value(QStringLiteral("signature")).toString().toHtmlEscaped());
            }
            extra << QStringLiteral("<b>Commits</b> · <a href=\"relay-commits:%1\">diff of these %2 commits</a><br>%3")
                         .arg(hashes.join(QStringLiteral("%20"))).arg(commits.size())
                         .arg(rows.join(QStringLiteral("<br>")));
        }
        if (!extra.isEmpty())
            meta += (meta.isEmpty() ? QString() : QStringLiteral("<br>"))
                    + extra.join(QStringLiteral("<br>"));
        // A different card drops the last one's reverse links; the same card re-read keeps them
        // until the `board_links` answer asked for below comes back (#EE42).
        if (!sameCard)
            m_backlinks.clear();
        m_metaBase = meta;
        renderMeta();
        m_qa = card.value(QStringLiteral("qa")).toObject();
        showVerify();
        m_openFile->setEnabled(!m_path.isEmpty());
        m_body = board::bodyWithoutTitle(card.value(QStringLiteral("body")).toString(), title);
        // The Tests strip (#7BM4). A different card starts with no findings on screen: a check
        // answers about one card, and carrying its rows to the next one would be a lie about a
        // card nobody has checked. The same card re-read (the worker wrote the dated block, or
        // someone edited the section) keeps them and picks up the new header line.
        if (!sameCard)
            clearCheck();
        showTests();
        showTryIt();
        showVerifyPlan(card.value(QStringLiteral("verify")));

        m_entries.clear();
        const QJsonArray thread = card.value(QStringLiteral("thread")).toArray();
        for (const QJsonValue &value : thread)
            m_entries << value.toObject();
        m_threadTotal = qMax(card.value(QStringLiteral("thread_total")).toInt(), int(m_entries.size()));
        // The `## Signal` strip (#AQ6X): read from the body the card arrived with, so a card that
        // stops being a promotion — the section removed — loses the strip at its next read.
        m_signalSection = board::signalSectionOf(card.value(QStringLiteral("body")).toString());
        m_signalStripText->setText(m_signalSection);
        // The block and its frame are shown and hidden together, so "is the strip up?" has one
        // answer whichever of the two is asked — including by a test, which cannot ask a widget
        // whose parent is hidden whether it is itself.
        m_signalStripText->setVisible(!m_signalSection.isEmpty());
        m_signalStrip->setVisible(!m_signalSection.isEmpty());
        // Said once: the strip has those words now, so the body below is the card's own.
        if (!m_signalSection.isEmpty())
            m_body = board::bodyWithoutSignalSection(m_body);
        setModeTips();
        render(sameCard ? Scroll::Keep : Scroll::Top);
        m_loading = false;
    }

    // A settled thread entry the worker wrote (`board_thread_appended`, 19.10). This is the
    // **only** way anything reaches the thread view since card #CTRN: the turn itself is drawn by
    // the card console's transcript below — the thinking fold, the tool rows, the answer as it
    // streams — and what lands here is the record, with the `model=` and `turn=` provenance the
    // worker put on it (owner decision 2). Nothing is drawn twice.
    void appendEntry(const QJsonObject &entry)
    {
        m_entries << entry;
        ++m_threadTotal;
        render(Scroll::Bottom);
    }

    // While a Discuss or a Plan runs on this card, the strip over the box names it and stops it,
    // and the buttons that would start another turn wait (#VZ69). Turns run per card (protocol
    // 19.16), so the board can be showing #A while #B is planning: coming back to a card that is
    // still working is `setBusy(true, mode)` again, and the answer so far is in that card's own
    // console, where it never left.
    void setBusy(bool busy, const QString &mode = QString())
    {
        m_busy = busy;
        m_busyMode = busy ? (mode.isEmpty() ? QStringLiteral("discuss") : mode) : QString();
        setModeTips();
        render(busy ? Scroll::Bottom : Scroll::Keep);
    }

    void showError(const QString &text)
    {
        // The line takes height from the document; keep the thread's end (the question that
        // failed) in view if that is where the reader was.
        QScrollBar *bar = m_doc->verticalScrollBar();
        const bool atBottom = bar->value() >= bar->maximum() - 8;
        m_error->setText(text);
        m_error->setVisible(!text.isEmpty());
        if (atBottom)
            QTimer::singleShot(0, m_doc, [bar] { bar->setValue(bar->maximum()); });
    }

    void focusReply() { if (m_reply != nullptr) m_reply->setFocus(); }
    // What is in the reply box, taken out of it and remembered in its history — what Plan,
    // Execute, Verify and a submitted line each do with the owner's note. Empty, and a no-op,
    // on a card page with no console.
    QString takeReply()
    {
        if (m_reply == nullptr)
            return QString();
        const QString text = m_reply->toPlainText().trimmed();
        if (!text.isEmpty())
            m_reply->remember(text);
        m_reply->clear();
        return text;
    }
    void focusDocument() { m_doc->setFocus(); }
    // How much of the card's bottom is controls (the reply box, or the editor's Save row). The
    // board's notice floats over this widget when a card has the pane to itself, and a cleanup's
    // progress line stays up for minutes, so it has to be placed clear of them.
    int controlsHeight() const
    {
        const QWidget *bottom = m_editFrame->isHidden() ? static_cast<QWidget *>(m_replyFrame)
                                                        : static_cast<QWidget *>(m_editFrame);
        int height = bottom->isHidden() ? 0 : bottom->height();
        if (!m_error->isHidden())
            height += m_error->height() + 6;   // the refusal line belongs to them
        return height;
    }
    // An ask that was refused before it reached the model (19.9's busy rule): submit() has already
    // emptied the box, so the words go back into it rather than being lost with the refusal.
    void restoreReply(const QString &text)
    {
        if (m_reply == nullptr)
            return;
        if (m_reply->toPlainText().trimmed().isEmpty())
            m_reply->setPlainText(text);
        m_reply->setFocus();
    }

    // ---- editing the card's own words (the title and `## Issue`)
    //
    // Both at once and in one write, because they are one thought: the card says what the issue
    // is, and its title is that in a line. The document becomes the editor, the reply box stands
    // down, and Save sends one `board_update` patch hash-checked against the file we read.
    // `selectIssue` is the card that was just created from the quick-add field: its `## Issue` is
    // the one line that was typed there, and that line is the *title*, not the issue (owner,
    // #VZ69). Offering it selected says so without throwing it away — the first keystroke
    // replaces it, and Ctrl+Enter or Esc leaves the card exactly as the field made it.
    void beginEdit(bool titleFirst, bool selectIssue = false)
    {
        if (m_id.isEmpty())
            return;
        if (!m_editing) {
            m_editing = true;
            m_titleEdit->setText(m_title->text());
            m_issueEdit->setPlainText(m_issue);
            m_title->hide();
            m_titleEdit->show();
            m_doc->hide();
            m_editFrame->show();
            m_replyFrame->hide();    // the action row is inside it, and goes down with it
            m_edit->setEnabled(false);
            m_popOut->setEnabled(false);   // the edit stays in this pane until it is saved
            m_error->hide();
        }
        if (titleFirst) {
            m_titleEdit->setFocus();
            m_titleEdit->selectAll();
        } else {
            m_issueEdit->setFocus();
            if (selectIssue)
                m_issueEdit->selectAll();
            else
                m_issueEdit->moveCursor(QTextCursor::End);
        }
    }

    // Leave edit mode: after a save the worker took, or on Esc / Cancel.
    void endEdit()
    {
        if (!m_editing)
            return;
        m_editing = false;
        m_titleEdit->hide();
        m_title->show();
        m_editFrame->hide();
        m_doc->show();
        m_replyFrame->show();
        m_edit->setEnabled(true);
        m_popOut->setEnabled(true);
        m_error->hide();
        m_doc->setFocus();
    }

    void cancelEdit()
    {
        m_titleEdit->setText(m_title->text());
        m_issueEdit->setPlainText(m_issue);
        endEdit();
    }

    void saveEdit()
    {
        if (!m_editing || !onEdit)
            return;
        const QString title = m_titleEdit->text().trimmed();
        const QString issue = m_issueEdit->toPlainText().trimmed();
        if (title.isEmpty()) {
            showError(QStringLiteral("A card needs a title."));
            m_titleEdit->setFocus();
            return;
        }
        QJsonObject patch;
        if (title != m_title->text())
            patch.insert(QStringLiteral("title"), title);
        if (issue != m_issue.trimmed())
            patch.insert(QStringLiteral("replace_section"),
                         QJsonObject{{QStringLiteral("heading"), board::issueHeading()},
                                     {QStringLiteral("text"), issue}});
        if (patch.isEmpty()) {          // nothing was changed: the same as cancelling
            endEdit();
            return;
        }
        onEdit(patch, m_hash);
    }

    // The card's labels, however the front matter carries them (an array or one string).
    QStringList currentLabels() const
    {
        QStringList labels;
        const QJsonValue value = m_front.value(QStringLiteral("labels"));
        if (value.isArray())
            for (const QJsonValue &entry : value.toArray())
                labels << entry.toString();
        else if (value.isString())
            labels << value.toString();
        labels.removeAll(QString());
        return labels;
    }

    // × on a label in the meta line (#E0Y0): take that one word off the card and leave the
    // rest of the front matter to the worker, exactly as saveEdit writes the title.
    void removeLabel(const QString &raw)
    {
        const QString label = QUrl::fromPercentEncoding(raw.toUtf8());
        QStringList labels = currentLabels();
        if (!labels.removeOne(label) || !onEdit)
            return;
        QJsonObject fields;
        fields.insert(QStringLiteral("labels"), QJsonArray::fromStringList(labels));
        QJsonObject patch;
        patch.insert(QStringLiteral("fields"), fields);
        onEdit(patch, m_hash);
    }

    // + at the end of the labels row (#E0Y0): the card's labels, comma separated, edited
    // in place under the meta line.
    void beginLabels()
    {
        if (m_hash.isEmpty())
            return;
        m_labelEdit->setText(currentLabels().join(QStringLiteral(", ")));
        m_labelEdit->show();
        m_labelEdit->raise();
        m_labelEdit->setFocus();
        m_labelEdit->end(false);
    }

    // Enter on the labels field: split on commas, drop empties and duplicates, and save
    // the list only when it actually changed.
    void applyLabels()
    {
        QStringList labels;
        const QStringList words = m_labelEdit->text().split(QLatin1Char(','));
        for (QString word : words) {
            word = word.trimmed();
            if (!word.isEmpty() && !labels.contains(word))
                labels << word;
        }
        m_labelEdit->hide();
        if (!onEdit || labels == currentLabels())
            return;
        QJsonObject fields;
        fields.insert(QStringLiteral("labels"), QJsonArray::fromStringList(labels));
        QJsonObject patch;
        patch.insert(QStringLiteral("fields"), fields);
        onEdit(patch, m_hash);
    }

    // The bottom of the card is a different height now: the notice that floats over it has to be
    // placed again.
    std::function<void()> onControlsResized;

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (object == m_replyFrame && event->type() == QEvent::Resize && onControlsResized)
            onControlsResized();
        // The accent border a pane's prompt box wears while that pane is live. Here it is the box
        // the cursor is in, exactly as on the helper panel — the two are the same component and
        // read the same `relayActive` property. Repolished by hand: a dynamic property does not
        // restyle itself.
        if (object == m_reply && m_replyFrame != nullptr
            && (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut)) {
            m_replyFrame->setProperty("relayActive", event->type() == QEvent::FocusIn);
            m_replyFrame->style()->unpolish(m_replyFrame);
            m_replyFrame->style()->polish(m_replyFrame);
        }
        if (object == m_reply && event->type() == QEvent::KeyPress
            && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            if (onEscape)
                onEscape();
            return true;
        }
        // Click the title, or double-click the text, to edit them; `e` on the document does the
        // same from the keyboard. Esc anywhere in the editor leaves the card as it was.
        if (object == m_title && event->type() == QEvent::MouseButtonRelease && !m_editing
            && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            if (onEditHint)
                onEditHint();
            beginEdit(true);
            return true;
        }
        // `m_doc` is still null while the widgets above it are being built and their first
        // show() runs through this filter.
        if (m_doc && object == m_doc->viewport() && event->type() == QEvent::MouseButtonDblClick
            && !m_editing && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            if (onEditHint)
                onEditHint();
            beginEdit(false);
            return true;
        }
        if (event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            const auto mods = key->modifiers() & ~Qt::KeypadModifier;
            const bool enter = key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
            // The find strip's own keys (#9NBZ): Esc closes it, Enter steps through the
            // matches (backwards with Shift), and the arrows do the same so the strip works
            // one-handed. Everything else — typing, the clear button's Tab — stays Qt's.
            if (object == m_findEdit) {
                if (key->key() == Qt::Key_Escape) {
                    closeFind();
                    return true;
                }
                if (enter) {
                    findStep(mods & Qt::ShiftModifier);
                    return true;
                }
                if (key->key() == Qt::Key_Down && mods == Qt::NoModifier) {
                    findStep(false);
                    return true;
                }
                if (key->key() == Qt::Key_Up && mods == Qt::NoModifier) {
                    findStep(true);
                    return true;
                }
                return QWidget::eventFilter(object, event);
            }
            if (object == m_doc && !m_editing && mods == Qt::NoModifier
                && key->text() == QStringLiteral("e")) {
                beginEdit(false);
                return true;
            }
            // `p` plans, `r` runs, `d` marks the card done.
            if (object == m_doc && !m_editing && mods == Qt::NoModifier) {
                if (key->text() == QStringLiteral("p")) {
                    plan();
                    return true;
                }
                if (key->text() == QStringLiteral("f")) {   // #6W9X
                    refine();
                    return true;
                }
                if (key->text() == QStringLiteral("r")) {
                    execute();
                    return true;
                }
                if (key->text() == QStringLiteral("v")) {   // #T71W
                    verify();
                    return true;
                }
                if (key->text() == QStringLiteral("d")) {
                    if (onDone)
                        onDone();
                    return true;
                }
                if (key->key() == Qt::Key_Delete) {   // #CYM9
                    remove();
                    return true;
                }
            }
            // The labels field (#E0Y0) saves on Enter and closes on Esc, like the title's.
            if (object == m_labelEdit) {
                if (key->key() == Qt::Key_Escape) {
                    m_labelEdit->hide();
                    return true;
                }
                if (enter && mods == Qt::NoModifier) {
                    applyLabels();
                    return true;
                }
                return QWidget::eventFilter(object, event);
            }
            if (m_editing && (object == m_titleEdit || object == m_issueEdit)) {
                if (key->key() == Qt::Key_Escape) {
                    cancelEdit();
                    return true;
                }
                // One line saves on Enter, as the quick-add field does; the issue text needs
                // Enter for its own newlines, so there it is Ctrl+Enter.
                if (enter && (mods == Qt::ControlModifier
                              || (object == m_titleEdit && mods == Qt::NoModifier))) {
                    saveEdit();
                    return true;
                }
                if (object == m_titleEdit && key->key() == Qt::Key_Down && mods == Qt::NoModifier) {
                    m_issueEdit->setFocus();
                    return true;
                }
            }
        }
        return QWidget::eventFilter(object, event);
    }

private:
    enum class Scroll { Top, Keep, Follow, Bottom };

    static QToolButton *textButton(const QString &text, const QString &tip)
    {
        auto *button = new QToolButton;
        button->setObjectName(QStringLiteral("boardTextButton"));
        button->setText(text);
        button->setToolTip(tip);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        return button;
    }

    void submit(const QString &mode)
    {
        if (m_reply == nullptr)
            return;
        const QString text = m_reply->toPlainText().trimmed();
        // A second Enter while the card is working used to do nothing at all — the words stayed
        // in the box and the worker would have refused them with `board_busy` anyway. A card has
        // a queue of its own since card #CTRN, so the prompt goes and runs when the turn before
        // it ends: the Issue #AGNT was filed for, arriving on the last surface that did not have
        // it. What it has **not** got yet is a row on screen while it waits: the console's §12
        // strip draws the pane's own client-side queue, and a card's prompt travels as
        // `board_ask` to the worker's, so the row needs `src/Pane.h` — which no step of #CTRN
        // may open (the plan's Risks 8). The live drive records it.
        if (text.isEmpty() || !onReply)
            return;
        m_reply->remember(text);
        m_reply->clear();
        m_error->hide();
        onReply(text, mode);
    }

public:
    // ---- the action row, as `CardContext::actions()` answers it (card #AGNT step 6) ----------
    //
    // Plan, Execute and Verify are the three things the agent can do here that need no typing
    // (#PBX1, and the owner's sentence this card is built on). They used to be three
    // `QPushButton`s this page built and re-labelled; they are now three `relay::agent::Action`s
    // and the console draws them — left-aligned above its busy line, buttons and nothing else,
    // each wearing its letter, `leaves` on the two that hand the card to a pane. Every word,
    // every letter, every enabled rule and every "a pane already holds this card" label (#48S3)
    // is the same; what changed is who paints them.
    //
    // Built fresh on every ask, which is `Context::actions()`'s contract: the row is rebuilt from
    // this list whenever `setModeTips()` says something moved.
    QList<relay::agent::Action> cardActions() const
    {
        QList<relay::agent::Action> actions;
        auto *self = const_cast<CardDetail *>(this);

        relay::agent::Action plan;
        plan.key = QStringLiteral("boardReplyButton");   // the object name the theme and the tests know
        plan.letter = QStringLiteral("p");
        if (panePlanning()) {
            // A pane is already planning the card (#48S3): the button names it and the click
            // reveals the pane instead of starting a second plan.
            plan.label = QStringLiteral("Planning (%1)").arg(m_sessionToken.left(8));
            plan.letter.clear();          // the label already carries the token, not a key
            plan.tooltip = QStringLiteral("A pane is already planning this card — the click reveals it");
        } else {
            plan.label = QStringLiteral("Plan");
            // Offered while a turn runs: it queues behind it (card #CTRN). Execute and Verify
            // below still wait, because they hand the card to a terminal pane rather than asking
            // this card's agent for another turn.
            plan.tooltip = QStringLiteral("The agent reads the code and writes the card's plan; it "
                                          "changes no code and no other card. Anything typed goes "
                                          "with it (p, or Ctrl+Enter)");
        }
        plan.run = [self] {
            const ActionGuard guard;
            if (self->panePlanning()) {
                if (self->onFocusPane)
                    self->onFocusPane(self->m_sessionToken);
                return;
            }
            if (self->onModeHint)
                self->onModeHint(QStringLiteral("plan"));
            self->plan();
        };
        actions << plan;

        // Refine (#6W9X): the agent checks the *request* before anyone plans it — the same or a
        // fixed-before card anywhere on the board, closed ones too, a sharper statement of the
        // ask, related links and a missing Done means. It writes no issue text, no plan and no
        // code; the worker refuses the call if it tries. Queues behind a running turn, like Plan.
        relay::agent::Action refine;
        refine.key = QStringLiteral("boardRefine");
        refine.letter = QStringLiteral("f");
        refine.label = QStringLiteral("Refine");
        refine.tooltip = QStringLiteral("The agent checks the request: finds the same or a fixed-before "
                                        "card, sharpens the ask, fills related links and Done means. "
                                        "It changes no issue text, no plan and no code. Anything typed "
                                        "goes with it (f)");
        refine.run = [self] {
            const ActionGuard guard;
            if (self->onModeHint)
                self->onModeHint(QStringLiteral("refine"));
            self->refine();
        };
        actions << refine;

        relay::agent::Action execute;
        execute.key = QStringLiteral("boardExecute");
        execute.letter = QStringLiteral("r");
        execute.leaves = true;            // it hands the card to a pane: the accent outline
        if (paneExecuting()) {
            // A pane already holds the card (#48S3): the button names it — the same eight
            // characters every other surface shows of the token — and the click reveals the
            // pane instead of handing the card to a second one.
            execute.label = QStringLiteral("Running (%1)").arg(m_sessionToken.left(8));
            execute.letter.clear();
            execute.tooltip = QStringLiteral("A pane is already running this card — the click reveals it");
        } else {
            execute.label = QStringLiteral("Run");
            execute.enabled = !m_busy;
            execute.tooltip = QStringLiteral("Run the card in the background; its agent builds it and the card moves to In progress (r)");
        }
        execute.run = [self] {
            const ActionGuard guard;
            if (self->paneExecuting()) {
                if (self->onFocusPane)
                    self->onFocusPane(self->m_sessionToken);
                return;
            }
            if (self->onModeHint)
                self->onModeHint(QStringLiteral("execute"));
            self->execute();
        };
        actions << execute;
        if (!paneExecuting()) {
            relay::agent::Action inPane = execute;
            inPane.key = QStringLiteral("boardRunInPane");
            inPane.letter.clear();
            inPane.label = QStringLiteral("Run in pane");
            inPane.tooltip = QStringLiteral("Run the card in a visible terminal pane beside the Board");
            inPane.run = [self] { const ActionGuard guard; self->execute(false); };
            actions << inPane;
        }

        // Verify is on the row only in a QA lane (#T71W): on any other card it would be a control
        // for a question nobody has asked yet. That is the visibility the old button had, and an
        // action the row must not draw is an action the list must not carry.
        if (inVerifyLane()) {
            relay::agent::Action verify;
            verify.key = QStringLiteral("boardVerify");
            verify.letter = QStringLiteral("v");
            verify.leaves = true;
            if (paneVerifying()) {
                verify.label = QStringLiteral("Verifying (%1)").arg(m_sessionToken.left(8));
                verify.letter.clear();
                verify.tooltip = QStringLiteral("A pane is already verifying this card — the click reveals it");
            } else {
                verify.label = QStringLiteral("Verify");
                verify.enabled = !m_busy && hasVerifier();
                const QString note = board::verifyNote(m_qa);
                const QString line = board::verifyLine(m_qa);
                verify.tooltip = hasVerifier()
                    ? QStringLiteral("Hand the card to a new terminal pane on %1, from a different "
                                     "provider family than the one that implemented it; it runs "
                                     "the checklist (v)").arg(board::verifyLabel(m_qa))
                    : QStringLiteral("No verifier is available for this card: %1")
                          .arg(!note.isEmpty() ? note
                               : line.isEmpty() ? QStringLiteral("the board has not said who should check it")
                                                : line);
            }
            verify.run = [self] {
                const ActionGuard guard;
                if (self->paneVerifying()) {
                    if (self->onFocusPane)
                        self->onFocusPane(self->m_sessionToken);
                    return;
                }
                if (self->onModeHint)
                    self->onModeHint(QStringLiteral("verify"));
                self->verify();
            };
            actions << verify;
        }

        // Try it (#JNYN), beside Verify and on the same rule: it is a control for a question
        // nobody has asked until there is something built to open, so it appears from
        // needs-verification on. It does **not** leave the board — the turn runs on this
        // worker and reports in the notice area — so it wears no accent outline.
        if (inTryLane()) {
            relay::agent::Action tryIt;
            tryIt.key = QStringLiteral("boardTryIt");
            tryIt.letter = QStringLiteral("y");
            tryIt.label = m_tryRunning ? QStringLiteral("Preparing review…") : QStringLiteral("Stage review");
            tryIt.enabled = !m_busy && !m_tryRunning;
            tryIt.tooltip = m_tryRunning
                ? QStringLiteral("Review staging is already running on this card — the board's notice "
                                 "line has its progress, and Stop is there")
                : (hasTryIt()
                       ? QStringLiteral("Stage this card's situation again and rewrite `## Try "
                                        "it` — the open line, one task and one question (y)")
                       : QStringLiteral("Stage this card's situation, do the mechanical steps, "
                                        "and leave you one task and one question. The expected "
                                        "result stays sealed until you answer (y)"));
            tryIt.run = [self] {
                const ActionGuard guard;
                self->tryIt();
            };
            actions << tryIt;
        }

        // Resume card (#FYEY), only when `land.py orphans` listed this card — refreshOrphans
        // asked when the page opened. A session working this card died holding uncommitted
        // hunks; the action is what Run does, with the orphan listing and the landing steps as
        // the pane's first prompt, so the new agent reads `## Done means` before it lands or
        // abandons anything.
        if (!m_orphans.isEmpty()) {
            relay::agent::Action resume;
            resume.key = QStringLiteral("boardResume");
            resume.leaves = true;   // it hands the card to a pane, like Run does
            resume.label = QStringLiteral("Resume card");
            resume.enabled = !m_busy;
            resume.tooltip = QStringLiteral(
                "A session working this card ended with uncommitted hunks; open a pane whose "
                "first prompt lists them, to land or abandon them");
            resume.run = [self] {
                const ActionGuard guard;
                if (self->onModeHint)
                    self->onModeHint(QStringLiteral("execute"));
                self->resume();
            };
            actions << resume;
        }
        return actions;
    }

    // Resume card (#FYEY). `land.py orphans --json` lists, per card, the land sessions that
    // died — their pane closed or went stale — still holding uncommitted hunks. The page asks
    // for it once when it opens (and again on demand, through refreshOrphans); if this card is
    // listed, the action row carries Resume card: what Run does, with the orphan listing and
    // the landing steps as the pane's first prompt. A script that is missing, fails, or names
    // another card means no action.
    struct OrphanClaim
    {
        QString path;
        int hunks = 0;
        int snapshotAgeMinutes = 0;
    };
    struct OrphanSession
    {
        QString session;
        QString owner;
        QList<OrphanClaim> claims;
    };

    // Ask the data root's `land.py orphans --json` once, asynchronously. Another ask in flight
    // is dropped; a failure — nonzero exit, crash, unreadable JSON — leaves the list empty, and
    // an empty list is a hidden action, never a broken one.
    void refreshOrphans()
    {
        m_orphans.clear();
        if (m_orphansProc) {
            m_orphansProc->disconnect(this);
            m_orphansProc->kill();
            m_orphansProc->deleteLater();
            m_orphansProc = nullptr;
        }
        const QString script = landScript();
        if (m_id.isEmpty() || script.isEmpty()) {
            if (onActionsChanged)
                onActionsChanged();
            return;
        }
        auto *proc = new QProcess(this);
        m_orphansProc = proc;
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [this, proc](int, QProcess::ExitStatus) {
            if (m_orphansProc == proc)
                m_orphansProc = nullptr;
            proc->deleteLater();
            if (proc->exitStatus() == QProcess::NormalExit && proc->exitCode() == 0)
                m_orphans = parseOrphans(proc->readAllStandardOutput(), cardId());
            if (onActionsChanged)
                onActionsChanged();
        });
        proc->start(relayPython(), {script, QStringLiteral("orphans"), QStringLiteral("--json")});
    }

    // The orphan listing as the resumed pane's first prompt (#FYEY): one line per dead
    // session's claim — session, path, hunks, snapshot age — then the landing steps the plan's
    // wording gives.
    QString resumeTask() const
    {
        QStringList lines;
        lines << QStringLiteral(
            "These are this card's uncommitted hunks from a session that ended; read `## Done "
            "means`, then `land.py who` and `land.py commit <session>` to land them or `land.py "
            "abandon <session>` to drop them.");
        for (const OrphanSession &session : m_orphans) {
            for (const OrphanClaim &claim : session.claims) {
                QString line = QStringLiteral("- session %1: %2 — %3 hunk(s), snapshot %4 min old")
                                   .arg(session.session, claim.path)
                                   .arg(claim.hunks)
                                   .arg(claim.snapshotAgeMinutes);
                if (!session.owner.isEmpty())
                    line += QStringLiteral(" (owner %1)").arg(session.owner);
                lines << line;
            }
        }
        return lines.join(QLatin1Char('\n'));
    }

    void resume()
    {
        if (paneExecuting()) {         // the card is in a live pane's hands; reveal it (#48S3)
            if (onFocusPane)
                onFocusPane(m_sessionToken);
            return;
        }
        if (m_id.isEmpty() || m_editing || !onResume)
            return;
        if (m_busy) {
            showError(QStringLiteral("The agent is still answering on #%1. Stop it, or wait for "
                                     "it, before resuming the card's orphans.")
                          .arg(m_id));
            return;
        }
        const QString note = takeReply();
        m_error->hide();
        onResume(resumeTask(), note);
    }

    // The data root's `scripts/land.py`, or empty when there is none to ask (no action, quietly).
    static QString landScript()
    {
        try {
            const QString script = dataRoot() + QStringLiteral("/scripts/land.py");
            return QFileInfo::exists(script) ? script : QString();
        } catch (const std::exception &) {
            return {};
        }
    }

    // The sessions of the one card this page is showing, out of the `orphans --json` answer.
    static QList<OrphanSession> parseOrphans(const QByteArray &json, const QString &id)
    {
        QList<OrphanSession> out;
        const QJsonDocument doc = QJsonDocument::fromJson(json);
        if (!doc.isObject())
            return out;
        const QJsonArray cards = doc.object().value(QStringLiteral("cards")).toArray();
        for (const QJsonValue &cardValue : cards) {
            const QJsonObject card = cardValue.toObject();
            if (card.value(QStringLiteral("card")).toString() != QStringLiteral("#") + id)
                continue;
            const QJsonArray sessions = card.value(QStringLiteral("sessions")).toArray();
            for (const QJsonValue &sessionValue : sessions) {
                const QJsonObject sessionObject = sessionValue.toObject();
                OrphanSession session;
                session.session = sessionObject.value(QStringLiteral("session")).toString();
                session.owner = sessionObject.value(QStringLiteral("owner")).toString();
                const QJsonArray claims = sessionObject.value(QStringLiteral("claims")).toArray();
                for (const QJsonValue &claimValue : claims) {
                    const QJsonObject claimObject = claimValue.toObject();
                    OrphanClaim claim;
                    claim.path = claimObject.value(QStringLiteral("path")).toString();
                    claim.hunks = claimObject.value(QStringLiteral("hunks")).toInt();
                    claim.snapshotAgeMinutes =
                        claimObject.value(QStringLiteral("snapshot_age_minutes")).toInt();
                    session.claims << claim;
                }
                if (!session.claims.isEmpty())
                    out << session;
            }
        }
        return out;
    }

    // Whether a Try it turn is in flight on this card: the view says so from the `tryit` events,
    // and the row is rebuilt, so a second press cannot start a second staging.
    void setTryRunning(bool running)
    {
        if (m_tryRunning == running)
            return;
        m_tryRunning = running;
        if (onActionsChanged)
            onActionsChanged();
    }
    bool tryRunning() const { return m_tryRunning; }

    // Something the action row or the busy strip would now answer differently: the card went
    // busy, a pane claimed it, the QA block arrived. The row is the console's, so the host is
    // told and it is the context that says so — there is one path, and it is this one.
    std::function<void()> onActionsChanged;

    // The strip over the reply box while a turn runs, and the row above it, redrawn. Nothing on
    // the row changes its label into "Stop" any more: a button that turned into "Stop" was the
    // thing the owner could not read (#VZ69). The verb is the board's own, "Switchboarding", with
    // a spaced dot before the status (owner, 2026-09-19: 'it says "Switchboarding · [status]..."'),
    // after the pane's "Relaying · …" line.
    void setModeTips()
    {
        m_delete->setEnabled(!m_busy);
        const bool planning = m_busyMode == QStringLiteral("plan");
        const bool refining = m_busyMode == QStringLiteral("refine");   // #6W9X
        m_busyLabel->setText(planning   ? QStringLiteral("✦ Switchboarding · planning…")
                             : refining ? QStringLiteral("✦ Switchboarding · refining…")
                                        : QStringLiteral("✦ Switchboarding · discussing…"));
        m_stop->setText(planning   ? QStringLiteral("✕ Stop planning")
                        : refining ? QStringLiteral("✕ Stop refining")
                                   : QStringLiteral("✕ Stop discussing"));
        m_stop->setToolTip(planning
                               ? QStringLiteral("Stop the agent before it finishes the plan. "
                                                "Anything it has already written to the card stays.")
                               : QStringLiteral("Stop the agent's reply. Anything it has already "
                                                "written to the card stays."));
        m_busyStrip->setVisible(m_busy);
        if (onActionsChanged)
            onActionsChanged();
    }

private:
    // The verify line and its button, from the `qa` block the card arrived with (#T71W). Both are
    // on screen only while the card is in a QA lane: on any other card the recommendation would be
    // an answer to a question nobody has asked yet.
    void showVerify()
    {
        const QString line = inVerifyLane() ? board::verifyLine(m_qa) : QString();
        const QString note = inVerifyLane() ? board::verifyNote(m_qa) : QString();
        // Amber is "a human should look at this" everywhere in Relay, and that is exactly what a
        // missing verifier or a same-lineage one is. With a recommendation the line itself stays
        // in the fields' muted ink and only the warning is amber; with none, the whole line is.
        const QString ink = hasVerifier() ? theme::TextMuted.name() : theme::Warning.name();
        QString html = line.isEmpty()
                           ? QString()
                           : QStringLiteral("<span style=\"color:%1\">%2</span>")
                                 .arg(ink, line.toHtmlEscaped());
        if (!html.isEmpty() && hasVerifier() && !note.isEmpty())
            html += QStringLiteral(" <span style=\"color:%1\">· %2</span>")
                        .arg(theme::Warning.name(), note.toHtmlEscaped());
        m_verifyLine->setText(html);
        m_verifyLine->setVisible(!html.isEmpty());
        // Verify itself is a row action now (cardActions()), so all this has to do is say that
        // the row would answer differently — the lane, the recommendation or the note has moved.
        if (onActionsChanged)
            onActionsChanged();
    }

    // ---- the `## Tests` strip (#7BM4) ------------------------------------------------------

    //: What the card's `## Tests` section says, read out of the body the card arrived with.
    struct TestsSummary {
        int listed = 0;       // lines that name a test, before the first `### Check` block
        QString stamp;        // the newest `### Check <YYYY-MM-DD HH:MM>`, "" when never checked
        QString verdict;      // that block's first line — "no findings", or the first finding
        QString statuses;     // "2 passed · 1 failed · 1 not applicable", counted out of it
    };

    // The section, read the way the format defines it (BOARD-FORMAT, protocol 31.5): one
    // test per Markdown list line, then the worker's dated `### Check` blocks. The count is the
    // list lines before the first block, so a check never inflates the number of tests a card
    // is said to name. The worker is the authority on which of those lines really name a test —
    // this is the header line over a section the reader can see for themselves.
    static TestsSummary readTests(const QString &body)
    {
        TestsSummary out;
        static const QRegularExpression heading(QStringLiteral("^##[ \\t]+(.+?)[ \\t]*$"),
                                                QRegularExpression::MultilineOption);
        int start = -1, end = body.size();
        QRegularExpressionMatchIterator headings = heading.globalMatch(body);
        while (headings.hasNext()) {
            const QRegularExpressionMatch match = headings.next();
            if (start >= 0) {
                end = match.capturedStart();
                break;
            }
            if (match.captured(1).trimmed().compare(QStringLiteral("Tests"),
                                                    Qt::CaseInsensitive) == 0)
                start = match.capturedEnd();
        }
        if (start < 0)
            return out;
        const QString section = body.mid(start, end - start);
        static const QRegularExpression check(
            QStringLiteral("^###[ \\t]+Check[ \\t]+(\\d{4}-\\d{2}-\\d{2}[ \\t]+\\d{2}:\\d{2})[ \\t]*$"),
            QRegularExpression::MultilineOption);
        int listEnd = section.size();
        QRegularExpressionMatchIterator blocks = check.globalMatch(section);
        while (blocks.hasNext()) {
            const QRegularExpressionMatch match = blocks.next();
            if (out.stamp.isEmpty())
                listEnd = match.capturedStart();
            out.stamp = match.captured(1).simplified();
            out.verdict.clear();
            QMap<QString, int> counts;
            const QStringList rest = section.mid(match.capturedEnd()).split(QLatin1Char('\n'));
            for (const QString &line : rest) {
                const QString text = line.trimmed();
                if (text.isEmpty())
                    continue;
                if (text.startsWith(QStringLiteral("###")))
                    break;
                if (out.verdict.isEmpty())
                    out.verdict = text;
                // `- <status> · <test> — <message>`: one line per listed check (#PR4Q).
                for (const QString &kind : {QStringLiteral("passed"), QStringLiteral("failed"),
                                            QStringLiteral("missing-evidence"),
                                            QStringLiteral("not-applicable")})
                    if (text.startsWith(QStringLiteral("- ") + kind + QStringLiteral(" ")))
                        counts[kind] += 1;
            }
            QStringList parts;
            for (const QString &kind : {QStringLiteral("passed"), QStringLiteral("failed"),
                                        QStringLiteral("missing-evidence"),
                                        QStringLiteral("not-applicable")})
                if (counts.value(kind) > 0)
                    parts << QStringLiteral("%1 %2").arg(counts.value(kind))
                                 .arg(kind == QStringLiteral("missing-evidence")
                                          ? QStringLiteral("missing evidence")
                                      : kind == QStringLiteral("not-applicable")
                                          ? QStringLiteral("not applicable")
                                          : kind);
            out.statuses = parts.join(QStringLiteral(" · "));
        }
        if (out.verdict.startsWith(QLatin1Char('-')) || out.verdict.startsWith(QLatin1Char('*')))
            out.verdict = out.verdict.mid(1).trimmed();
        const QStringList lines = section.left(listEnd).split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            const QString text = line.trimmed();
            if (text.size() < 2)
                continue;
            const QChar bullet = text.at(0);
            if (bullet != QLatin1Char('-') && bullet != QLatin1Char('*') && bullet != QLatin1Char('+'))
                continue;
            if (!text.mid(1).trimmed().isEmpty())
                ++out.listed;
        }
        return out;
    }

    // The strip's own line, and whether the strip is there at all.
    void showTests()
    {
        const bool has = hasTests();
        m_testsStrip->setVisible(has || m_testsAsking);
        if (!has) {
            clearCheck();
            if (m_testsAsking) {
                // The card names no checks and the gate has just asked which ones prove it.
                m_testsLine->setText(QStringLiteral("<span style=\"color:%1\">Tests</span>&nbsp;"
                                                    "<span style=\"color:%2\">none named</span>")
                                         .arg(theme::TextMuted.name(), theme::Warning.name()));
                m_testsCheck->setEnabled(false);
            }
            return;
        }
        const TestsSummary summary = readTests(m_body);
        QString text = QStringLiteral("<span style=\"color:%1\">Tests</span>&nbsp;%2 listed")
                           .arg(theme::TextMuted.name())
                           .arg(summary.listed);
        if (summary.stamp.isEmpty()) {
            // Amber is "a human should look at this" everywhere in Relay, and a card whose tests
            // nobody has checked is exactly that — it is the state the gate refuses to land.
            text += QStringLiteral(" <span style=\"color:%1\">· never checked</span>")
                        .arg(theme::Warning.name());
        } else {
            text += QStringLiteral(" <span style=\"color:%1\">· checked %2</span>")
                        .arg(theme::TextMuted.name(), summary.stamp.toHtmlEscaped());
            if (!summary.statuses.isEmpty()) {
                // The four statuses, counted out of the block the worker wrote, so the line says
                // what the card says without a check having to be pressed first (#PR4Q).
                const bool clean = !summary.statuses.contains(QStringLiteral("failed"))
                                   && !summary.statuses.contains(QStringLiteral("missing"));
                text += QStringLiteral(" <span style=\"color:%1\">· %2</span>")
                            .arg(clean ? theme::TextMuted.name() : theme::Warning.name(),
                                 summary.statuses.toHtmlEscaped());
            } else if (!summary.verdict.isEmpty()) {
                const bool clean = summary.verdict.startsWith(QStringLiteral("no findings"),
                                                              Qt::CaseInsensitive);
                // The whole verdict is under the strip when a check has just run and in the
                // body either way, so the header line takes only as much of it as keeps the
                // line one line.
                QString verdict = summary.verdict;
                if (verdict.size() > 64)
                    verdict = verdict.left(63) + QChar(0x2026);
                text += QStringLiteral(" <span style=\"color:%1\">· %2</span>")
                            .arg(clean ? theme::TextMuted.name() : theme::Warning.name(),
                                 verdict.toHtmlEscaped());
            }
        }
        m_testsLine->setText(text);
        m_testsCheck->setEnabled(!m_id.isEmpty() && m_testsCheck->text() == QStringLiteral("Check"));
    }

    // Put the last answer away: a new card, or a card with the section gone, must not be read
    // under someone else's findings.
    void clearCheck()
    {
        while (QLayoutItem *item = m_testsFindingsBox->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        m_testsFindings->hide();
        while (m_testsActionsBox->count() > 1) {        // 0 is the stretch that right-aligns them
            QLayoutItem *item = m_testsActionsBox->takeAt(1);
            delete item->widget();
            delete item;
        }
        m_testsActions->hide();
        m_checkFiles = QJsonObject();
        m_checkIds = QJsonArray();
        m_checkFailing = QJsonArray();
        m_checkStatuses = QJsonArray();
    }

    // "3 passed · 1 failed · 1 not applicable · 2 notices" — the header over the rows, in the
    // words the card's own `### Check` status uses, so the strip and the card body agree.
    static QString statusHeadText(const QJsonArray &statuses, int findings,
                                  const QString &revision)
    {
        QStringList parts;
        for (const QString &kind : {QStringLiteral("passed"), QStringLiteral("failed"),
                                    QStringLiteral("missing-evidence"),
                                    QStringLiteral("not-applicable")}) {
            int count = 0;
            for (const QJsonValue &value : statuses)
                if (value.toObject().value(QStringLiteral("status")).toString() == kind)
                    ++count;
            if (count > 0)
                parts << QStringLiteral("%1 %2").arg(count).arg(kind == QStringLiteral("missing-evidence")
                                                                   ? QStringLiteral("missing evidence")
                                                               : kind == QStringLiteral("not-applicable")
                                                                   ? QStringLiteral("not applicable")
                                                                   : kind);
        }
        if (findings > 0)
            parts << QStringLiteral("%1 finding%2").arg(findings)
                         .arg(findings == 1 ? QString() : QStringLiteral("s"));
        QString text = QStringLiteral("Check · ") + parts.join(QStringLiteral(" · "));
        if (!revision.isEmpty())
            text += QStringLiteral(" · revision %1").arg(revision.left(12));
        return QStringLiteral("<span style=\"color:%1\">%2</span>")
                .arg(theme::TextMuted.name(), text.toHtmlEscaped());
    }

    // One listed check's status: the word, the test, and what decided it. Red for a failure,
    // amber for missing evidence (a human has to do something), muted for a retired check and
    // green for a pass. A pass another machine produced carries the one affordance that takes
    // it: "Use this existing result", which records the acceptance on the card.
    void addStatusRow(const QJsonObject &row)
    {
        const QString status = row.value(QStringLiteral("status")).toString();
        const QString test = row.value(QStringLiteral("test")).toString();
        const QString message = row.value(QStringLiteral("message")).toString();
        const QColor ink = status == QStringLiteral("failed")           ? theme::Error
                           : status == QStringLiteral("missing-evidence") ? theme::Warning
                           : status == QStringLiteral("passed")           ? theme::Success
                                                                          : theme::TextMuted;
        auto *holder = new QWidget(m_testsFindings);
        auto *box = new QHBoxLayout(holder);
        box->setContentsMargins(0, 0, 0, 0);
        box->setSpacing(6);
        auto *label = new QLabel(holder);
        label->setObjectName(QStringLiteral("boardTestsStatus"));
        label->setWordWrap(true);
        label->setTextFormat(Qt::RichText);
        label->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
        const QString file = m_checkFiles.value(test).toString();
        const QString name = test.isEmpty() ? QStringLiteral("this card") : test;
        const QString head =
                file.isEmpty()
                    ? QStringLiteral("<span style=\"color:%1\">%2 · %3</span>")
                          .arg(ink.name(), status.toHtmlEscaped(), name.toHtmlEscaped())
                    : QStringLiteral("<a href=\"%1\" style=\"color:%2;text-decoration:none\">%3 · "
                                     "%4</a>")
                          .arg(file.toHtmlEscaped(), ink.name(), status.toHtmlEscaped(),
                               name.toHtmlEscaped());
        label->setText(head + QStringLiteral(" · %1").arg(message.toHtmlEscaped()));
        if (!file.isEmpty()) {
            label->setCursor(Qt::PointingHandCursor);
            label->setToolTip(QStringLiteral("Open %1").arg(file));
            connect(label, &QLabel::linkActivated, this, [this](const QString &path) {
                if (onOpenPath)
                    onOpenPath(path);
            });
        }
        box->addWidget(label, 1);
        const QJsonArray evidence = row.value(QStringLiteral("evidence")).toArray();
        if (row.value(QStringLiteral("use_existing")).toBool() && !evidence.isEmpty()) {
            const QJsonObject newest = evidence.first().toObject();
            auto *use = new QPushButton(QStringLiteral("Use this existing result"), holder);
            use->setObjectName(QStringLiteral("boardTestsUseResult"));
            use->setFocusPolicy(Qt::NoFocus);
            use->setToolTip(QStringLiteral("Record that run %1 from %2 proves this card at this "
                                           "revision. It runs nothing.")
                                .arg(newest.value(QStringLiteral("run_id")).toString(),
                                     newest.value(QStringLiteral("host")).toString()));
            const QString run = newest.value(QStringLiteral("run_id")).toString();
            connect(use, &QPushButton::clicked, this, [this, test, run] {
                if (m_id.isEmpty() || !onTestsRequest || run.isEmpty())
                    return;
                onTestsRequest(QJsonObject{{QStringLiteral("type"), QStringLiteral("tests_accept")},
                                           {QStringLiteral("card"), m_id},
                                           {QStringLiteral("id"), test},
                                           {QStringLiteral("run_id"), run}});
            });
            box->addWidget(use, 0);
        }
        m_testsFindingsBox->addWidget(holder);
    }

    // One finding: severity, the test, the message — the findings shape the helper panel drew
    // before the console replaced it, and a click on a test whose source the worker named opens
    // that file through the card's own opener.
    void addFindingRow(const QJsonObject &finding)
    {
        const QString test = finding.value(QStringLiteral("test")).toString();
        const QString severity = finding.value(QStringLiteral("severity")).toString();
        const QString message = finding.value(QStringLiteral("message")).toString();
        const QColor ink = severity == QStringLiteral("failure")   ? theme::Error
                           : severity == QStringLiteral("warning") ? theme::Warning
                                                                   : theme::TextMuted;
        auto *row = new QLabel(m_testsFindings);
        row->setObjectName(QStringLiteral("boardTestsFinding"));
        row->setWordWrap(true);
        row->setTextFormat(Qt::RichText);
        // Links only, no text selection: theme::polishWindow() renames every selectable QLabel
        // to `cwd`, which would take these rows out of their own stylesheet rules (m_meta and
        // the helper panel's findings carry the same comment).
        row->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
        const QString file = m_checkFiles.value(test).toString();
        const QString name = test.isEmpty() ? QStringLiteral("this card") : test;
        const QString head =
                file.isEmpty()
                    ? QStringLiteral("<span style=\"color:%1\">%2 · %3</span>")
                          .arg(ink.name(), severity.toHtmlEscaped(), name.toHtmlEscaped())
                    : QStringLiteral("<a href=\"%1\" style=\"color:%2;text-decoration:none\">%3 · "
                                     "%4</a>")
                          .arg(file.toHtmlEscaped(), ink.name(), severity.toHtmlEscaped(),
                               name.toHtmlEscaped());
        row->setText(head + QStringLiteral(" · %1").arg(message.toHtmlEscaped()));
        if (!file.isEmpty()) {
            row->setCursor(Qt::PointingHandCursor);
            row->setToolTip(QStringLiteral("Open %1").arg(file));
            connect(row, &QLabel::linkActivated, this, [this](const QString &path) {
                if (onOpenPath)
                    onOpenPath(path);
            });
        }
        m_testsFindingsBox->addWidget(row);
    }

    // At most three, exactly the ones the worker offered: the check decides what can be done
    // about what it found, and the strip does not invent a fourth.
    void addActionButton(const QString &label)
    {
        if (label.isEmpty() || m_testsActionsBox->count() >= 4)
            return;
        auto *button = new QPushButton(label, m_testsActions);
        button->setObjectName(QStringLiteral("boardReplyButton"));
        button->setFocusPolicy(Qt::NoFocus);
        button->setProperty("testsAction", label);
        connect(button, &QPushButton::clicked, this, [this, label] { runTestsAction(label); });
        m_testsActionsBox->addWidget(button);
    }

    void runTestsAction(const QString &label)
    {
        if (m_id.isEmpty())
            return;
        if (label.startsWith(QStringLiteral("Run these"))) {
            if (m_checkIds.isEmpty() || !onTestsRequest) {
                showError(QStringLiteral("Nothing in #%1's `## Tests` resolves to a test this "
                                         "machine can run.").arg(m_id));
                return;
            }
            onTestsRequest(QJsonObject{{QStringLiteral("type"), QStringLiteral("tests_run")},
                                       {QStringLiteral("ids"), m_checkIds}});
            return;
        }
        if (label.startsWith(QStringLiteral("Replace retired check"))) {
            openTestsEditor(false);
            return;
        }
        if (label.startsWith(QStringLiteral("Add the tests"))) {
            if (onTestsRequest)
                onTestsRequest(QJsonObject{{QStringLiteral("type"), QStringLiteral("tests_suggest")},
                                           {QStringLiteral("card"), m_id}});
            return;
        }
        if (label.startsWith(QStringLiteral("Open the failing one"))) {
            const QString id = m_checkFailing.isEmpty() ? QString()
                                                        : m_checkFailing.first().toString();
            const QString file = m_checkFiles.value(id).toString();
            if (file.isEmpty() || !onOpenPath) {
                showError(QStringLiteral("The failing test's source is not known here: the "
                                         "worker did not name a file for %1.")
                              .arg(id.isEmpty() ? QStringLiteral("it") : id));
                return;
            }
            onOpenPath(file);
            return;
        }
        // Anything else the worker offers — today, "Remove the tests that are gone" — edits the
        // section by hand, and the card is never rewritten from here: it goes into the reply box
        // as a request for the agent, the way the helper panel's findings draft a fix.
        restoreReply(QStringLiteral("%1 in #%2's `## Tests` section.").arg(label, m_id));
    }

public:
    // ---- the `## Tests` editor (#PR4Q) ------------------------------------------------------
    //
    // `askWhichChecks` is the gate's question on a card that names no checks: the box starts
    // empty, and "None apply" is beside Save because *that* is an answer too — it goes on the
    // thread, and the move it was asked about goes through. Otherwise the box starts on the
    // section as it stands, which is how a retired check is replaced.
    void openTestsEditor(bool askWhichChecks)
    {
        if (m_id.isEmpty())
            return;
        m_testsAsking = askWhichChecks;
        QStringList retired;
        for (const QJsonValue &value : m_checkStatuses) {
            const QJsonObject row = value.toObject();
            if (row.value(QStringLiteral("retired")).toBool())
                retired << row.value(QStringLiteral("invocation")).toString();
        }
        const QString what = retired.isEmpty()
                                 ? QStringLiteral("the checks this card names.")
                                 : QStringLiteral("%1 is not in the project any more.")
                                           .arg(retired.join(QStringLiteral(", ")).toHtmlEscaped());
        m_testsEditHint->setText(
                askWhichChecks
                    ? QStringLiteral("<span style=\"color:%1\">Which checks prove #%2? One per "
                                     "line. They go in `## Tests`, and Check judges them next "
                                     "time this card moves.</span>")
                              .arg(theme::Warning.name(), m_id)
                    : QStringLiteral("<span style=\"color:%1\">`## Tests` for #%2 — %3 Replace "
                                     "it below, one check per line.</span>")
                              .arg(theme::TextMuted.name(), m_id, what));
        m_testsEditor->setPlainText(askWhichChecks ? QString() : testsSectionText(m_body));
        m_testsEditNone->setVisible(askWhichChecks);
        // The rows go while the list is being rewritten: the box is about to replace them, the
        // hint above it names what was wrong, and the strip stays one strip instead of pushing
        // the card's own words off the page.
        m_testsFindings->hide();
        m_testsActions->hide();
        m_testsEdit->show();
        m_testsStrip->show();
        m_testsEditor->setFocus();
        if (onControlsResized)
            onControlsResized();
    }

private:
    void closeTestsEditor()
    {
        m_testsEdit->hide();
        m_testsAsking = false;
        m_testsFindings->setVisible(m_testsFindingsBox->count() > 0);
        m_testsActions->setVisible(m_testsActionsBox->count() > 1);
        showTests();
        if (onControlsResized)
            onControlsResized();
    }

    // The card is never written from here: the patch goes down the same `board_update` path the
    // `## Issue` editor uses, so one writer owns the card file and the thread records the edit.
    void saveTestsEditor()
    {
        if (m_id.isEmpty() || !onEdit)
            return;
        const QString text = m_testsEditor->toPlainText().trimmed();
        if (text.isEmpty()) {
            showError(QStringLiteral("Name at least one check, or press None apply."));
            m_testsEditor->setFocus();
            return;
        }
        const QString heading = QStringLiteral("Tests");
        QJsonObject patch;
        patch.insert(hasTests() ? QStringLiteral("replace_section")
                                : QStringLiteral("append_section"),
                     QJsonObject{{QStringLiteral("heading"), heading},
                                 {QStringLiteral("text"), text}});
        closeTestsEditor();
        onEdit(patch, m_hash);
    }

    // The section's own lines, without the `### Check` status under them: the status is the
    // worker's and is rewritten on every check, so it is not something to hand a person to edit.
    static QString testsSectionText(const QString &body)
    {
        static const QRegularExpression heading(QStringLiteral("^##[ \\t]+(.+?)[ \\t]*$"),
                                                QRegularExpression::MultilineOption);
        int start = -1, end = body.size();
        QRegularExpressionMatchIterator headings = heading.globalMatch(body);
        while (headings.hasNext()) {
            const QRegularExpressionMatch match = headings.next();
            if (start >= 0) {
                end = match.capturedStart();
                break;
            }
            if (match.captured(1).trimmed().compare(QStringLiteral("Tests"),
                                                    Qt::CaseInsensitive) == 0)
                start = match.capturedEnd();
        }
        if (start < 0)
            return QString();
        QString section = body.mid(start, end - start);
        static const QRegularExpression check(
            QStringLiteral("^###[ \\t]+Check[ \\t]+\\d{4}-\\d{2}-\\d{2}"),
            QRegularExpression::MultilineOption);
        const QRegularExpressionMatch first = check.match(section);
        if (first.hasMatch())
            section = section.left(first.capturedStart());
        return section.trimmed();
    }

    // Two short lines of facts under the pickers, keys muted, the file a link that opens it.
    static QString metaText(const QJsonObject &front, const QJsonArray &tasks, const QString &path,
                            bool sessionLive = true)
    {
        QStringList parts;
        const auto item = [](const QString &key, const QString &value) {
            return QStringLiteral("<span style=\"color:%1\">%2</span>&nbsp;%3")
                .arg(theme::TextMuted.name(), key.toHtmlEscaped(), value.toHtmlEscaped());
        };
        const auto add = [&](const char *key, const QString &label) {
            const QJsonValue value = front.value(QLatin1String(key));
            if (value.isString() && !value.toString().isEmpty())
                parts << item(label, value.toString());
            else if (value.isArray() && !value.toArray().isEmpty()) {
                QStringList items;
                for (const QJsonValue &entry : value.toArray())
                    items << entry.toString();
                parts << item(label, items.join(QStringLiteral(", ")));
            }
        };
        // Labels render as bare words and copy a filter term (#S53Z): the muted key
        // stays the meta's, the words take the pane's link colour. Each word is followed
        // by a muted × that removes it and the row ends in a + that opens the labels
        // field (#E0Y0); the word itself still only copies.
        QStringList labelWords;
        const QJsonValue labelsValue = front.value(QLatin1String("labels"));
        if (labelsValue.isArray())
            for (const QJsonValue &entry : labelsValue.toArray())
                labelWords << entry.toString();
        else if (labelsValue.isString())
            labelWords << labelsValue.toString();
        labelWords.removeAll(QString());
        {
            QStringList shown;
            for (const QString &label : std::as_const(labelWords))
                shown << QStringLiteral(
                                 "<a href=\"tag:%1\" style=\"color:%2\">%3</a>"
                                 "&nbsp;<a href=\"tagx:%1\" title=\"remove label\" "
                                 "style=\"color:%4;text-decoration:none\">×</a>")
                                 .arg(QString::fromUtf8(QUrl::toPercentEncoding(label)),
                                      theme::Link.name(), label.toHtmlEscaped(),
                                      theme::TextMuted.name());
            shown << QStringLiteral("<a href=\"tagadd:\" title=\"edit labels\" "
                                    "style=\"color:%1;text-decoration:none\">+</a>")
                         .arg(theme::Link.name());
            parts << QStringLiteral("<span style=\"color:%1\">labels</span>&nbsp;%2")
                         .arg(theme::TextMuted.name(), shown.join(QStringLiteral(", ")));
        }
        add("owner", QStringLiteral("owner"));
        add("assignee", QStringLiteral("assignee"));
        const QString resolution = front.value(QStringLiteral("resolution")).toString();
        if (!resolution.isEmpty()) {
            QString closed = resolution.toHtmlEscaped();
            const QString duplicate = front.value(QStringLiteral("duplicate_of")).toString();
            if (!duplicate.isEmpty())
                closed += QStringLiteral(" of <a href=\"card:%1\">#%2</a>")
                              .arg(duplicate.toHtmlEscaped(), duplicate.toHtmlEscaped());
            parts << QStringLiteral("<span style=\"color:%1\">resolution</span>&nbsp;%2")
                         .arg(theme::TextMuted.name(), closed);
        }
        // Which pane claimed the card (#R9G7), beside who it is assigned to: the same chip the
        // row wears, and — while that pane is open — the same `relay-pane:` anchor the thread's
        // "Executing (xxxxxxxx)" entry carries, in the link colour, so a click reveals the pane.
        // A pane that has gone keeps its token, muted, with "closed" and no link: the claim is
        // still the record of who took the card.
        const QString session = front.value(QStringLiteral("session")).toString().trimmed();
        if (!session.isEmpty()) {
            const QString chip = board::sessionChip(session, sessionLive).toHtmlEscaped();
            parts << QStringLiteral("<span style=\"color:%1\">session</span>&nbsp;%2")
                         .arg(theme::TextMuted.name(),
                              sessionLive
                                  ? QStringLiteral("<a href=\"relay-pane:%1\" style=\"color:%2\">%3</a>")
                                        .arg(QString::fromUtf8(QUrl::toPercentEncoding(session)),
                                             theme::Link.name(), chip)
                                  : QStringLiteral("<span style=\"color:%1\">%2</span>")
                                        .arg(theme::TextMuted.name(), chip));
        }
        add("waiting_on", QStringLiteral("waiting on"));
        add("milestone", QStringLiteral("milestone"));
        const QJsonValue due = front.value(QStringLiteral("due"));
        if (due.isObject()) {
            const QJsonObject obj = due.toObject();
            parts << item(QStringLiteral("due"), obj.value(QStringLiteral("date")).toString());
        } else if (due.isString()) {
            parts << item(QStringLiteral("due"), due.toString());
        }
        add("snooze", QStringLiteral("snooze until"));
        add("component", QStringLiteral("component"));
        add("implemented_by", QStringLiteral("implemented by"));
        // Who closed it out of a QA lane, stamped by the worker (#T71W). Beside the implementer,
        // because the pair is the point: two different models.
        add("verified_by", QStringLiteral("verified by"));
        if (!tasks.isEmpty()) {
            int done = 0;
            for (const QJsonValue &task : tasks)
                done += task.toObject().value(QStringLiteral("done")).toBool() ? 1 : 0;
            parts << item(QStringLiteral("tasks"), QStringLiteral("%1/%2").arg(done).arg(tasks.size()));
        }
        const QJsonObject links = front.value(QStringLiteral("links")).toObject();
        for (auto it = links.begin(); it != links.end(); ++it) {
            if (it.key() == QStringLiteral("commits") || !it.value().isArray()
                || it.value().toArray().isEmpty())
                continue;
            QStringList items;
            for (const QJsonValue &entry : it.value().toArray())
                items << entry.toString();
            parts << item(it.key(), items.join(QStringLiteral(", ")));
        }
        QString html = parts.join(QStringLiteral(" &nbsp;·&nbsp; "));
        const QString acceptance = front.value(QStringLiteral("acceptance")).toString();
        if (!acceptance.isEmpty())
            html += (html.isEmpty() ? QString() : QStringLiteral("<br>"))
                    + item(QStringLiteral("acceptance"), acceptance);
        if (!path.isEmpty())
            html += (html.isEmpty() ? QString() : QStringLiteral("<br>"))
                    + QStringLiteral("<a href=\"%1\" style=\"color:%2\">%3</a>")
                          .arg(path.toHtmlEscaped(), theme::TextMuted.name(), path.toHtmlEscaped());
        return html;
    }

    // Markdown headings come out of QTextDocument at browser sizes (an H1 is ~2x the text), which
    // shouts inside a side panel. Bring them down to a document scale and give them air above.
    static void tuneHeadings(QTextDocument *doc, qreal base)
    {
        static const qreal scale[] = {1.0, 1.3, 1.15, 1.05, 1.0, 1.0, 1.0};
        for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
            const int level = block.blockFormat().headingLevel();
            if (level <= 0)
                continue;
            QTextCursor cursor(block);
            cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            QTextCharFormat format;
            format.setProperty(QTextFormat::FontSizeAdjustment, 0);
            format.setFontPointSize(base * scale[qMin(level, 6)]);
            format.setFontWeight(QFont::DemiBold);
            cursor.mergeCharFormat(format);
            QTextBlockFormat blockFormat = block.blockFormat();
            blockFormat.setTopMargin(level <= 2 ? 14 : 10);
            blockFormat.setBottomMargin(4);
            cursor.setBlockFormat(blockFormat);
        }
    }

    void insertMarkdown(QTextCursor &cursor, const QString &markdown, qreal base)
    {
        QTextDocument part;
        part.setDefaultFont(m_doc->document()->defaultFont());
        part.setMarkdown(markdown);
        tuneHeadings(&part, base);
        const int from = cursor.position();
        cursor.insertFragment(QTextDocumentFragment(&part));
        linkifyTags(cursor.document(), from, cursor.position());
    }

    // Only known card references become links (#S53Z); #label stays plain text.
    static const QRegularExpression &tagPattern()
    {
        static const QRegularExpression pattern(
            QStringLiteral("(?<![A-Za-z0-9_-])#[A-Za-z0-9][A-Za-z0-9_-]*"));
        return pattern;
    }

    // The anchor a tag wears, over whatever format the words around it carry.
    QTextCharFormat tagFormat(const QTextCharFormat &base, const QString &word) const
    {
        if (!hasCard || !hasCard(word))
            return base;
        QTextCharFormat link = base;
        link.setAnchor(true);
        link.setAnchorHref(QStringLiteral("card:") + word.toUpper());
        link.setFontUnderline(true);
        link.setForeground(theme::Link);
        return link;
    }

    // `text` inserted with known card references as anchors, so a plain thread line — an
    // event, a note, a hand-off — carries the same affordance the Markdown bodies get.
    void insertTagged(QTextCursor &cursor, const QString &text, const QTextCharFormat &format)
    {
        int at = 0;
        QRegularExpressionMatchIterator it = tagPattern().globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            const QString found = match.captured(0);
            if (match.capturedStart() > at)
                cursor.insertText(text.mid(at, match.capturedStart() - at), format);
            cursor.insertText(found, tagFormat(format, found.mid(1)));
            at = match.capturedEnd();
        }
        if (at < text.size())
            cursor.insertText(text.mid(at), format);
    }

    // Overlay card-reference anchors on a stretch of already-rendered Markdown: the body after
    // setMarkdown(), or the range a thread comment's fragment landed in. What is already an
    // anchor — a Markdown link's label, a pane link — and what is code, fenced or inline, keeps
    // its meaning: `[#bug](http://x)` stays an http link and `#include` stays source.
    void linkifyTags(QTextDocument *doc, int from, int to)
    {
        for (QTextBlock block = doc->findBlock(from);
             block.isValid() && block.position() <= to; block = block.next()) {
            const QTextBlockFormat blockFormat = block.blockFormat();
            if (blockFormat.property(QTextFormat::BlockCodeLanguage).isValid()
                || blockFormat.property(QTextFormat::BlockCodeFence).isValid())
                continue;
            for (QTextBlock::iterator piece = block.begin(); !piece.atEnd(); ++piece) {
                const QTextFragment fragment = piece.fragment();
                if (!fragment.isValid())
                    continue;
                const QTextCharFormat base = fragment.charFormat();
                if (!base.anchorHref().isEmpty())
                    continue;
                if (base.fontFamilies().toStringList().contains(QStringLiteral("monospace"),
                                                               Qt::CaseInsensitive))
                    continue;   // a code span, not prose
                const QString text = fragment.text();
                QRegularExpressionMatchIterator it = tagPattern().globalMatch(text);
                while (it.hasNext()) {
                    const QRegularExpressionMatch match = it.next();
                    const int start = fragment.position() + match.capturedStart();
                    const int end = fragment.position() + match.capturedEnd();
                    if (start < from || end > to)
                        continue;
                    QTextCursor apply(doc);
                    apply.setPosition(start);
                    apply.setPosition(end, QTextCursor::KeepAnchor);
                    apply.setCharFormat(tagFormat(base, match.captured(0).mid(1)));
                }
            }
        }
    }

    // A hairline the width of the document: an empty block two pixels tall, painted in the border
    // ink. QTextDocument has no rule of its own that a theme can colour — Markdown's `---` takes
    // the palette's — and a row of box-drawing characters would wrap and be copied with the text.
    void insertRule(QTextCursor &cursor, int topMargin)
    {
        QTextBlockFormat rule;
        rule.setTopMargin(topMargin);
        rule.setBottomMargin(0);
        rule.setLineHeight(2, QTextBlockFormat::FixedHeight);
        rule.setBackground(theme::Border);
        QTextCharFormat hairline;
        hairline.setFontPointSize(1);
        cursor.insertBlock(rule, hairline);
    }

    void insertLine(QTextCursor &cursor, const QString &text, const QTextCharFormat &format,
                    int topMargin)
    {
        QTextBlockFormat block;
        block.setTopMargin(topMargin);
        block.setBottomMargin(2);
        cursor.insertBlock(block, format);
        insertTagged(cursor, text, format);
    }

    // The body, then the thread under a small heading. Events (moves, status changes) are one
    // muted line each; comments get an author line and their Markdown.
    void render(Scroll scroll)
    {
        QScrollBar *bar = m_doc->verticalScrollBar();
        const int was = bar->value();
        const bool atBottom = was >= bar->maximum() - 8;
        QTextDocument *doc = m_doc->document();
        const qreal base = m_doc->font().pointSizeF() > 0 ? m_doc->font().pointSizeF() : 10.0;
        doc->setMarkdown(m_body.trimmed().isEmpty() ? QStringLiteral("*No description.*") : m_body);
        tuneHeadings(doc, base);
        linkifyTags(doc, 0, doc->characterCount());   // the card's own words first (#3ZAP)

        QTextCursor cursor(doc);
        cursor.movePosition(QTextCursor::End);
        QTextCharFormat muted;
        muted.setForeground(theme::TextMuted);
        muted.setFontPointSize(qMax(theme::FloorPt, base * 0.9));
        QTextCharFormat heading = muted;
        heading.setFontWeight(QFont::DemiBold);
        heading.setFontLetterSpacing(105);
        const int shown = int(m_entries.size());
        QString threadTitle = m_threadTotal > 0 ? QStringLiteral("THREAD · %1").arg(m_threadTotal)
                                                : QStringLiteral("THREAD");
        if (m_threadTotal > shown)
            threadTitle += QStringLiteral("  (last %1)").arg(shown);
        // The card's own words end here and the conversation about them begins, and the reader has
        // to see the seam (owner, #VZ69: "there should be a clearer dematcation between the issue
        // and the convo thread"). A heading in bolder ink was not it: the thread gets a rule the
        // width of the document and then its own ground under the heading, so the two halves of
        // the card are two surfaces rather than one column of text with a louder line in it.
        insertRule(cursor, 22);
        QTextBlockFormat band;
        band.setTopMargin(0);
        band.setBottomMargin(10);
        band.setLeftMargin(8);
        band.setBackground(cardMix(theme::Surface, theme::Text, 0.07));
        cursor.insertBlock(band, heading);
        cursor.insertText(threadTitle, heading);
        if (m_entries.isEmpty() && !m_busy)
            insertLine(cursor, QStringLiteral("No replies yet. Enter in the box below discusses the "
                                              "card with the agent, Ctrl+Enter has it Plan the work, "
                                              "f has it Refine the request first, "
                                              "and Ctrl+Shift+Enter leaves a comment for whoever "
                                              "picks it up."), muted, 4);

        const QDateTime now = QDateTime::currentDateTimeUtc();
        for (const QJsonObject &entry : std::as_const(m_entries)) {
            const QJsonObject attrs = entry.value(QStringLiteral("attrs")).toObject();
            const QString author = entry.value(QStringLiteral("author")).toString(
                attrs.value(QStringLiteral("author")).toString());
            const QString kind = entry.value(QStringLiteral("kind")).toString(
                attrs.value(QStringLiteral("kind")).toString());
            const QString text = entry.value(QStringLiteral("text")).toString().trimmed();
            const QString age = board::entryAge(entry.value(QStringLiteral("entry_id")).toString(), now);
            if (kind == QStringLiteral("event")) {
                QString line = text;
                if (line.startsWith(QStringLiteral("- ")))
                    line = line.mid(2);
                insertLine(cursor, line + (age.isEmpty() ? QString() : QStringLiteral("  · ") + age),
                           muted, 8);
                continue;
            }
            QTextCharFormat who;
            who.setFontWeight(QFont::DemiBold);
            who.setForeground(author == QStringLiteral("agent") ? theme::Agent : theme::Text);
            insertLine(cursor, author == QStringLiteral("agent") ? QStringLiteral("✦ agent") : author,
                       who, 14);
            QStringList extra;
            // The model the turn ran on, by name (card #MDL1, rule 1): the attribute records the
            // id the API took, and app/board.js names it the same way on the phone.
            const QString model = relay::models::nameOf(attrs.value(QStringLiteral("model")).toString());
            // The mode first, so the history reads "Plan · …", "Discuss · …" (#XS6Q).
            const QString mode = board::modeTitle(attrs.value(QStringLiteral("mode")).toString(
                entry.value(QStringLiteral("mode")).toString()));
            if (!mode.isEmpty())
                extra << mode;
            if (!model.isEmpty())
                extra << model;
            if (!kind.isEmpty() && kind != QStringLiteral("comment"))
                extra << kind;
            if (!age.isEmpty())
                extra << age;
            if (!extra.isEmpty())
                cursor.insertText(QStringLiteral("  ") + extra.join(QStringLiteral(" · ")), muted);
            // An entry that handed the card to a pane links to it (#HKAP): its whole first
            // line — "Executing (xxxxxxxx) · …" — is an anchor on that pane's session token,
            // in the palette's link colour so it reads as clickable before it is clicked.
            // Anything after the first line (the owner's note on the hand-off) is ordinary
            // body text; entries without a token keep the Markdown path.
            const QString paneToken = attrs.value(QStringLiteral("pane_token")).toString();
            if (paneToken.isEmpty()) {
                cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
                insertMarkdown(cursor, board::threadMarkdown(text, kind), base);
            } else {
                QTextCharFormat link;
                link.setForeground(theme::Link);
                link.setAnchor(true);
                link.setAnchorHref(QStringLiteral("relay-pane:") + paneToken);
                const int split = text.indexOf(QLatin1Char('\n'));
                insertLine(cursor, split < 0 ? text : text.left(split), link, 2);
                if (split >= 0)
                    insertMarkdown(cursor, board::threadMarkdown(text.mid(split).trimmed(), kind), base);
            }
        }
        // The running turn draws **nothing** here (card #CTRN, owner decision 4): the answer as
        // it streams, the thinking fold and the tool rows are the card console's transcript, the
        // way every other console draws a turn. Until this card the same bytes were drawn twice —
        // once in the transcript, once here as a live tail that collapsed every tool call into
        // one elided progress line — and neither drawing was the pane's. What settles into the
        // thread is the entry the worker writes when the turn ends, through `appendEntry`.

        switch (scroll) {
        case Scroll::Top:
            // Also once the new document is laid out: a card opened while the panel was hidden
            // is laid out on show, and that would otherwise leave it scrolled part way down.
            m_doc->moveCursor(QTextCursor::Start);
            bar->setValue(0);
            QTimer::singleShot(0, m_doc, [bar] { bar->setValue(0); });
            break;
        case Scroll::Keep:
            bar->setValue(was);
            break;
        case Scroll::Follow:
            bar->setValue(atBottom ? bar->maximum() : was);
            break;
        case Scroll::Bottom:
            bar->setValue(bar->maximum());
            break;
        }
        // A new card landed under an open find strip (#9NBZ): its count and cursor describe a
        // document that no longer exists, so the search runs again over the new one.
        if (!m_findStrip->isHidden() && !m_findEdit->text().isEmpty())
            refreshFind();
    }

    // The meta label is the card's own lines plus the reverse links `board_links` answered
    // with (#EE42), which arrive after the card and are drawn without re-reading it.
    void renderMeta()
    {
        QString text = m_metaBase;
        if (!m_backlinks.isEmpty())
            text += (text.isEmpty() ? QString() : QStringLiteral("<br>"))
                    + QStringLiteral("<b>Linked from</b><br>%1").arg(m_backlinks);
        m_meta->setText(text);
        m_meta->setVisible(!text.isEmpty());
    }

    QString m_metaBase;    // the meta block as `show()` built it
    QString m_backlinks;   // the "Linked from" line, empty until `board_links` answers

    PriorityFlagButton *m_flag = nullptr;   // the card page's priority flag (#DPJB)
    QLabel *m_ref = nullptr, *m_title = nullptr, *m_meta = nullptr, *m_error = nullptr;
    QToolButton *m_refCopy = nullptr;   // the ⧉ beside the ref (#FT77)
    QLabel *m_verifyLine = nullptr;   // the cross-provider QA recommendation (#T71W)
    QLineEdit *m_labelEdit = nullptr;   // the one-line labels field (#E0Y0)
    // The `## Tests` strip (#7BM4): the header line, the Check button, the findings rows the
    // last `tests_check` drew, and the action buttons it offered. `m_checkFiles`,
    // `m_checkIds` and `m_checkFailing` are that answer's three machine keys (31.2), kept so
    // the buttons act on what was actually found rather than re-deriving it from the body.
    QWidget *m_testsStrip = nullptr;
    QLabel *m_testsLine = nullptr;
    QPushButton *m_testsCheck = nullptr;
    QWidget *m_testsFindings = nullptr;
    QVBoxLayout *m_testsFindingsBox = nullptr;
    QWidget *m_testsActions = nullptr;
    QHBoxLayout *m_testsActionsBox = nullptr;
    QJsonObject m_checkFiles;
    QJsonArray m_checkIds, m_checkFailing;
    // The per-check statuses of the last answer (#PR4Q): `passed`, `failed`, `missing-evidence`
    // or `not-applicable` per listed test, each with the executions that decided it.
    QJsonArray m_checkStatuses;
    // The inline `## Tests` editor: "Replace retired check" opens it on the section as it
    // stands, and the gate's "which checks prove this card?" opens it empty.
    QFrame *m_testsEdit = nullptr;
    QLabel *m_testsEditHint = nullptr;
    QPlainTextEdit *m_testsEditor = nullptr;
    QPushButton *m_testsEditSave = nullptr, *m_testsEditCancel = nullptr, *m_testsEditNone = nullptr;
    QComboBox *m_status = nullptr, *m_tab = nullptr;
    QToolButton *m_close = nullptr, *m_toPrompt = nullptr, *m_openFile = nullptr, *m_edit = nullptr;
    QToolButton *m_popOut = nullptr;
    QToolButton *m_done = nullptr;
    QToolButton *m_delete = nullptr;
    QTextBrowser *m_doc = nullptr;
    // The console's composer, not this page's widget: `setConsole` points this at the box inside
    // the agent console the view embeds below (card #AGNT step 6). Null until then, and null for
    // ever on a card page with no console at all, which is why every use of it is guarded — a
    // board driven by a test still opens, edits and moves cards with no agent surface in it.
    RichEditor *m_reply = nullptr;
    QWidget *m_consoleHost = nullptr;
    QVBoxLayout *m_consoleBox = nullptr;
    // The strip over the reply box while a turn runs: "✦ Switchboarding · planning…" and the ✕ that
    // stops it (#VZ69). Hidden the rest of the time.
    QWidget *m_busyStrip = nullptr;
    QLabel *m_busyLabel = nullptr;
    QToolButton *m_stop = nullptr;
    QFrame *m_replyFrame = nullptr, *m_editFrame = nullptr;
    // The `## Try it` strip (#JNYN): the header line with the unanswered question, the button
    // that opens what the turn staged, and the one line the person answers in. Nothing of the
    // run is kept here — the section on the card is the state, and the notice area is the run.
    QWidget *m_tryStrip = nullptr;
    QLabel *m_tryLine = nullptr;
    QPushButton *m_tryOpen = nullptr;
    QLineEdit *m_tryAnswer = nullptr;
    bool m_tryRunning = false;        // a Try it turn is in flight on this card
    // Resume card (#FYEY): what `land.py orphans --json` last answered for this card, and the
    // ask in flight, if any. Empty means the card is not listed — or nothing was asked, or the
    // ask failed — so the action row hides Resume card.
    QList<OrphanSession> m_orphans;
    QPointer<QProcess> m_orphansProc;
    // The Verify strip (#WFRA): the card's `verify:` block as one line under the Try it strip.
    QLabel *m_verifyPlanLine = nullptr;
    // Find in the open card (#9NBZ): the strip over the document, hidden until `openFind`.
    QWidget *m_findStrip = nullptr;
    QLineEdit *m_findEdit = nullptr;
    QLabel *m_findCount = nullptr;
    QToolButton *m_findPrevious = nullptr, *m_findNext = nullptr, *m_findClose = nullptr;
    QLineEdit *m_titleEdit = nullptr;
    QPlainTextEdit *m_issueEdit = nullptr;
    QPushButton *m_saveEdit = nullptr, *m_cancelEdit = nullptr;
    QList<QJsonObject> m_entries;
    QHash<QString, QString> m_drafts;
    QString m_body, m_id, m_path;
    // The card as the worker last handed it over: the hash an edit is written against, and the
    // `## Issue` text an edit starts from and is compared with.
    QString m_hash, m_issue;
    QJsonObject m_front;
    QString m_sessionToken;           // the pane that claimed the card, while it is on screen
    bool m_sessionLive = false;       // and whether that pane is still open (#48S3)
    QJsonObject m_qa;                 // the worker's verifier recommendation for this card (#T71W)
    QString m_statusValue;
    QStringList m_sections;           // the body's `## ` headings, for "has it a plan?"
    QString m_busyMode;               // "discuss" or "plan" while a turn runs
    QString m_executeArmed;           // the card that was warned it has no plan or acceptance
    // The promoted card's `## Signal` strip (#AQ6X) and the section it is showing.
    QFrame *m_signalStrip = nullptr;
    QLabel *m_signalStripText = nullptr;
    QString m_signalSection;
    int m_threadTotal = 0;
    bool m_loading = false, m_busy = false, m_editing = false;
};

}  // namespace relay
