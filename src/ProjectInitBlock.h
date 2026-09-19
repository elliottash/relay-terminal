// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The "Initialize a project and create a Switchboard here?" question, drawn inline in a pane.
//
// It is a row of the pane's own column, directly under the terminal, exactly where the thinking
// panel and the queue strip live: it takes real layout space, so the terminal shrinks and the shell
// reflows into what is left. The owner's rule of 2026-09-18 — new surfaces are panes or inline in
// the pane, never floating overlays and never modal dialogs — is why this is not a QDialog, and why
// the agent turn that raised the question is never blocked by it.
//
// The widget is dumb on purpose. It draws a `projectinit::Question` and reports one of three
// answers with the boxes that were ticked; every rule about when to ask, what the lines say and
// what a tick imports is in `src/ProjectInit.h`, which is pure and tested. Nothing here touches the
// filesystem, the registry or the worker.
#include "ProjectInit.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>

#include <functional>

namespace relay {

class ProjectInitBlock final : public QFrame {
public:
    enum class Answer { Yes, No, NotNow };

    // `answered(answer, kinds)` runs once: `kinds` are the import kinds of the ticked boxes
    // (empty for No and Not now). The block hides itself first, so a handler may delete it.
    std::function<void(Answer, const QStringList &)> answered;

    explicit ProjectInitBlock(QWidget *parent = nullptr) : QFrame(parent)
    {
        setObjectName(QStringLiteral("projectInit"));
        setAttribute(Qt::WA_StyledBackground);
        setFocusPolicy(Qt::StrongFocus);
        // Wide content must not widen the pane's minimum, for the queue strip's reason (#G152): a
        // findings line is longer than a pane in a three-pane split, and a minimum that wide would
        // make the splitter redistribute every pane in the row.
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_column = new QVBoxLayout(this);
        m_column->setContentsMargins(12, 8, 12, 8);
        m_column->setSpacing(3);
    }

    const projectinit::Question &question() const { return m_question; }

    // Draw a question and show the block. Every checkbox starts unticked: saying yes to a
    // Switchboard is not saying yes to an import (docs/PROJECT-INIT-AND-IMPORT.md section 1).
    // Not called `show()`: that would hide QWidget's, which this calls at the end.
    void ask(const projectinit::Question &question)
    {
        m_question = question;
        m_boxes.clear();
        while (QLayoutItem *item = m_column->takeAt(0)) {
            if (QWidget *widget = item->widget()) widget->deleteLater();
            delete item;
        }
        addLabel(question.title, QStringLiteral("projectInitTitle"));
        addLabel(question.project, QStringLiteral("projectInitPath"));
        if (!question.folderLine.isEmpty()) addLabel(question.folderLine, QStringLiteral("projectInitNote"));

        if (!question.imports.isEmpty()) {
            addLabel(QStringLiteral("Import these as cards"), QStringLiteral("projectInitHeading"));
            for (const projectinit::Finding &finding : question.imports) {
                auto *box = new QCheckBox(finding.text, this);
                box->setObjectName(QStringLiteral("projectInitBox"));
                box->setChecked(false);                 // never pre-ticked
                box->setFocusPolicy(Qt::StrongFocus);   // Tab walks the boxes
                m_column->addWidget(box);
                m_boxes.append(box);
            }
        }
        if (!question.notes.isEmpty()) {
            addLabel(QStringLiteral("Also here, not imported"), QStringLiteral("projectInitHeading"));
            for (const projectinit::Finding &finding : question.notes)
                addLabel(QStringLiteral("·  ") + finding.text, QStringLiteral("projectInitNote"));
        }

        auto *row = new QHBoxLayout;
        row->setContentsMargins(0, 4, 0, 0);
        row->setSpacing(8);
        row->addStretch(1);
        m_notNow = button(QStringLiteral("Not now"), QStringLiteral("Esc · asked again the next time you open the Switchboard, /card or /init"));
        m_no = button(QStringLiteral("No"), QStringLiteral("N · remembered: Relay stops asking about this project"));
        m_yes = button(QStringLiteral("Yes"), QStringLiteral("Y · creates the folder and nothing else"));
        m_yes->setObjectName(QStringLiteral("projectInitYes"));
        row->addWidget(m_notNow);
        row->addWidget(m_no);
        row->addWidget(m_yes);
        auto *holder = new QWidget(this);
        holder->setLayout(row);
        m_column->addWidget(holder);
        connect(m_yes, &QPushButton::clicked, this, [this] { answer(Answer::Yes); });
        connect(m_no, &QPushButton::clicked, this, [this] { answer(Answer::No); });
        connect(m_notNow, &QPushButton::clicked, this, [this] { answer(Answer::NotNow); });

        show();
        m_yes->setFocus(Qt::OtherFocusReason);
    }

    // The import kinds of the boxes that are ticked right now.
    QStringList tickedKinds() const
    {
        QList<bool> ticked;
        for (const QCheckBox *box : m_boxes) ticked.append(box && box->isChecked());
        return projectinit::importKinds(m_question.imports, ticked);
    }

    // y / n / Esc, wherever the focus is inside the block. Space and Enter still work on whatever
    // has the focus, so Tab-to-a-box then Space ticks it and Tab-to-Yes then Enter accepts.
    void keyPressEvent(QKeyEvent *event) override
    {
        switch (event->key()) {
        case Qt::Key_Y: answer(Answer::Yes); return;
        case Qt::Key_N: answer(Answer::No); return;
        case Qt::Key_Escape: answer(Answer::NotNow); return;
        default: break;
        }
        QFrame::keyPressEvent(event);
    }

private:
    QLabel *addLabel(const QString &text, const QString &name)
    {
        auto *label = new QLabel(text, this);
        label->setObjectName(name);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        m_column->addWidget(label);
        return label;
    }

    QPushButton *button(const QString &text, const QString &tip)
    {
        auto *push = new QPushButton(text, this);
        push->setObjectName(QStringLiteral("projectInitButton"));
        push->setToolTip(tip);
        push->setCursor(Qt::PointingHandCursor);
        return push;
    }

    void answer(Answer which)
    {
        const QStringList kinds = which == Answer::Yes ? tickedKinds() : QStringList();
        hide();
        if (answered) answered(which, kinds);
    }

    projectinit::Question m_question;
    QVBoxLayout *m_column = nullptr;
    QList<QCheckBox *> m_boxes;
    QPushButton *m_yes = nullptr, *m_no = nullptr, *m_notNow = nullptr;
};

}  // namespace relay
