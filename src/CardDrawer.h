// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// CardDrawer.h — the read-only card drawer a terminal pane docks under its header (#6BY7).
// The pane's claims chip toggles it; `setCard` takes exactly what the tab's board helper
// answers to `board_card_get`, and the drawer never reads a card file itself. No Q_OBJECT,
// public std::function callbacks — house style for headers included from Pane.h.

#include "BoardModel.h"

#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QTextBrowser>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>

class CardDrawer final : public QFrame {
public:
    explicit CardDrawer(QWidget *parent = nullptr) : QFrame(parent) {
        setObjectName(QStringLiteral("cardDrawer"));
        setAttribute(Qt::WA_StyledBackground);   // the theme's #cardDrawer background rule
        setFrameShape(QFrame::NoFrame);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(8, 6, 8, 6);
        root->setSpacing(4);

        auto *head = new QHBoxLayout;
        head->setSpacing(6);
        m_stage = new QLabel(this);
        m_stage->setObjectName(QStringLiteral("cardDrawerStage"));
        m_idLabel = new QLabel(this);
        m_idLabel->setObjectName(QStringLiteral("cardDrawerId"));
        m_title = new QLabel(this);
        m_title->setObjectName(QStringLiteral("cardDrawerTitle"));
        m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
        QFont titleFont = m_title->font();
        titleFont.setBold(true);
        m_title->setFont(titleFont);
        head->addWidget(m_stage);
        head->addWidget(m_idLabel);
        head->addWidget(m_title, 1);
        m_done = new QPushButton(QStringLiteral("Done"), this);
        m_done->setObjectName(QStringLiteral("cardDrawerDone"));
        m_done->setToolTip(QStringLiteral("Move this card to done (the Board's gates still apply)"));
        connect(m_done, &QPushButton::clicked, this, [this] {
            if (onDone && !m_cardId.isEmpty()) onDone(m_cardId);
        });
        m_openBoard = new QPushButton(QStringLiteral("Open in Board"), this);
        m_openBoard->setObjectName(QStringLiteral("cardDrawerOpen"));
        connect(m_openBoard, &QPushButton::clicked, this, [this] {
            if (onOpenBoard && !m_cardId.isEmpty()) onOpenBoard(m_cardId);
        });
        m_close = new QToolButton(this);
        m_close->setObjectName(QStringLiteral("cardDrawerClose"));
        m_close->setText(QStringLiteral("✕"));
        m_close->setToolTip(QStringLiteral("Close the card drawer (the chip reopens it)"));
        connect(m_close, &QToolButton::clicked, this, [this] { if (onClose) onClose(); });
        head->addWidget(m_done);
        head->addWidget(m_openBoard);
        head->addWidget(m_close);
        root->addLayout(head);

        // Worker refusals (a gated Done, a vanished card) render here rather than in a dialog.
        m_notice = new QLabel(this);
        m_notice->setObjectName(QStringLiteral("cardDrawerNotice"));
        m_notice->setWordWrap(true);
        m_notice->hide();
        root->addWidget(m_notice);

        // Read-only: no editing surface exists in the drawer, and links never leave the app —
        // a click copies the URL as text instead of opening anything.
        m_body = new QTextBrowser(this);
        m_body->setObjectName(QStringLiteral("cardDrawerBody"));
        m_body->setOpenExternalLinks(false);
        m_body->setReadOnly(true);
        m_body->setFrameShape(QFrame::NoFrame);
        connect(m_body, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
            m_body->setSource(QUrl());  // stay on the card; the URL goes to the clipboard
            QApplication::clipboard()->setText(url.toString());
            showNotice(QStringLiteral("Copied link: %1").arg(url.toString()));
        });
        root->addWidget(m_body, 1);
    }

    // The `board_card` helper answer: card_id, status, title, body, body_truncated.
    void setCard(const QJsonObject &event) {
        m_cardId = event.value(QStringLiteral("card_id")).toString().toUpper();
        const QString status = event.value(QStringLiteral("status")).toString();
        const QString title = event.value(QStringLiteral("title")).toString();
        m_stage->setText(relay::board::statusTitle(status));
        m_stage->setVisible(!status.isEmpty());
        m_idLabel->setText(m_cardId.isEmpty() ? QString() : QStringLiteral("#") + m_cardId);
        m_title->setText(title);
        // The Board's Done is a move; a card already done has nothing left to ask of the worker.
        const bool isDone = status.compare(QStringLiteral("done"), Qt::CaseInsensitive) == 0;
        m_done->setVisible(!isDone);
        QString body = event.value(QStringLiteral("body")).toString();
        if (event.value(QStringLiteral("body_truncated")).toBool())
            body += QStringLiteral("\n\n*(body truncated — Open in Board shows the whole card)*");
        m_body->setMarkdown(renderBody(body));
        m_notice->hide();
    }

    QString cardId() const { return m_cardId; }
    bool doneHidden() const { return !m_done->isVisibleTo(this); }

    void showNotice(const QString &text) {
        m_notice->setText(text);
        m_notice->setVisible(!text.isEmpty());
    }

    // Card id → the pane sends `board_move` through the same helper channel.
    std::function<void(const QString &)> onDone;
    // Card id → the chip's old behaviour: open the Board on this card.
    std::function<void(const QString &)> onOpenBoard;
    std::function<void()> onClose;

private:
    // The drawer shows the card document without its `# ` title heading (the header row already
    // names the card), and renders `- [x]` task items as ticks rather than editable checkboxes.
    static QString renderBody(const QString &body) {
        QString text = body;
        const QRegularExpression heading(QStringLiteral("^(\\s*)- \\[([ xX])\\]\\s*"),
                                         QRegularExpression::MultilineOption);
        QString out;
        QRegularExpressionMatchIterator it = heading.globalMatch(text);
        int at = 0;
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            out += text.mid(at, m.capturedStart() - at);
            const bool ticked = m.captured(2).compare(QStringLiteral("x"), Qt::CaseInsensitive) == 0;
            out += m.captured(1) + (ticked ? QStringLiteral("✓ ") : QStringLiteral("☐ "));
            at = m.capturedStart() + m.capturedLength();
        }
        out += text.mid(at);
        const QRegularExpression titleLine(QStringLiteral("^# ([^#].*)$"),
                                           QRegularExpression::MultilineOption);
        out.remove(titleLine);
        return out;
    }

    QString m_cardId;
    QLabel *m_stage = nullptr;
    QLabel *m_idLabel = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_notice = nullptr;
    QPushButton *m_done = nullptr;
    QPushButton *m_openBoard = nullptr;
    QToolButton *m_close = nullptr;
    QTextBrowser *m_body = nullptr;
};
