// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// A person's Review queue (#BX7B). The worker sends only an actionable priority on each
// row; the selected card supplies the goal, evidence and one question. The QA plan stays
// in the agent's card data, not in this pane.
#include "PaneView.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QFont>
#include <QPushButton>
#include <QRegularExpression>
#include <QStringList>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <functional>

namespace relay {

class ReviewPane final : public QWidget, public PaneView {
public:
    explicit ReviewPane(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName(QStringLiteral("reviewPane"));
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 10, 12, 10);
        layout->setSpacing(8);
        auto *heading = new QLabel(QStringLiteral("Review"), this);
        heading->setObjectName(QStringLiteral("reviewHeading"));
        QFont headingFont = heading->font();
        headingFont.setBold(true);
        headingFont.setPointSize(qMax(headingFont.pointSize(), 12));
        heading->setFont(headingFont);
        layout->addWidget(heading);
        m_status = new QLabel(QStringLiteral("Loading reviews…"), this);
        m_status->setObjectName(QStringLiteral("reviewStatus"));
        m_status->setWordWrap(true);
        layout->addWidget(m_status);
        m_list = new QListWidget(this);
        m_list->setObjectName(QStringLiteral("reviewQueue"));
        m_list->setMaximumHeight(190);
        layout->addWidget(m_list);
        m_detail = new QTextBrowser(this);
        m_detail->setObjectName(QStringLiteral("reviewDetail"));
        m_detail->setOpenLinks(false);
        layout->addWidget(m_detail, 1);
        m_question = new QLabel(this);
        m_question->setObjectName(QStringLiteral("reviewQuestion"));
        m_question->setWordWrap(true);
        m_question->hide();
        layout->addWidget(m_question);
        m_answer = new QLineEdit(this);
        m_answer->setObjectName(QStringLiteral("reviewAnswer"));
        m_answer->setPlaceholderText(QStringLiteral("Your answer for this revision"));
        m_answer->hide();
        layout->addWidget(m_answer);
        auto *actions = new QHBoxLayout;
        m_open = new QPushButton(QStringLiteral("Open card"), this);
        m_open->setObjectName(QStringLiteral("reviewOpenCard"));
        actions->addWidget(m_open);
        actions->addStretch(1);
        m_signOff = new QPushButton(QStringLiteral("Sign off…"), this);
        m_signOff->setObjectName(QStringLiteral("reviewSignOff"));
        m_signOff->hide();
        actions->addWidget(m_signOff);
        m_submit = new QPushButton(QStringLiteral("Record answer"), this);
        m_submit->setObjectName(QStringLiteral("reviewSubmit"));
        m_submit->hide();
        actions->addWidget(m_submit);
        layout->addLayout(actions);
        connect(m_list, &QListWidget::currentRowChanged, this, [this] { selectCurrent(); });
        connect(m_open, &QPushButton::clicked, this, [this] {
            if (onOpenCard && !m_selected.isEmpty()) onOpenCard(m_selected);
        });
        connect(m_submit, &QPushButton::clicked, this, [this] { recordAnswer(); });
        connect(m_answer, &QLineEdit::returnPressed, this, [this] { recordAnswer(); });
        connect(m_signOff, &QPushButton::clicked, this, [this] { signOff(); });
        m_open->setEnabled(false);
    }

    QString paneTitle() const override { return QStringLiteral("Review"); }
    void focusView() override { m_list->setFocus(); }
    void setHeaderRightInset(int) override {}
    std::function<void(const QJsonObject &)> onSend;
    std::function<void(const QString &)> onOpenCard;

    void requestList() { send({{"type", "board_open"}, {"id", "review-open"}}); }
    int queueSize() const { return m_list->count(); }
    QString selectedCard() const { return m_selected; }

    void handleEvent(const QJsonObject &event) {
        const QString kind = event.value(QStringLiteral("event")).toString();
        if (kind == QStringLiteral("board")) {
            m_rows.clear();
            upsert(event.value(QStringLiteral("cards")).toArray());
            redraw();
        } else if (kind == QStringLiteral("board_cards")) {
            upsert(event.value(QStringLiteral("cards")).toArray());
            redraw();
        } else if (kind == QStringLiteral("board_changed")) {
            for (const QJsonValue &value : event.value(QStringLiteral("removed")).toArray())
                m_rows.remove(value.toString());
            upsert(event.value(QStringLiteral("upserts")).toArray());
            redraw();
        } else if (kind == QStringLiteral("board_card")
                   && event.value(QStringLiteral("id")).toString().startsWith(QStringLiteral("review-card-"))
                   && event.value(QStringLiteral("card_id")).toString() == m_selected) {
            m_card = event;
            showCard();
        } else if (kind == QStringLiteral("board_written")
                   && event.value(QStringLiteral("id")).toString().startsWith(QStringLiteral("review-write-"))) {
            m_status->setText(QStringLiteral("Recorded for this revision."));
            m_answer->clear();
            fetchSelected();
        } else if (kind == QStringLiteral("error")
                   && event.value(QStringLiteral("id")).toString().startsWith(QStringLiteral("review-"))) {
            m_status->setText(event.value(QStringLiteral("text")).toString());
            fetchSelected(); // A stale hash gets a fresh card; the person's draft stays.
        }
    }

private:
    static QString section(const QString &body, const QString &heading) {
        const QRegularExpression head(QStringLiteral("(?m)^##\\s+%1\\s*$")
                                          .arg(QRegularExpression::escape(heading)));
        const auto match = head.match(body);
        if (!match.hasMatch()) return {};
        const int begin = match.capturedEnd();
        const auto next = QRegularExpression(QStringLiteral("(?m)^##\\s+")).match(body, begin);
        return body.mid(begin, next.hasMatch() ? next.capturedStart() - begin : -1).trimmed();
    }
    static QString firstOpenQuestion(const QString &human) {
        const QStringList lines = human.split(QLatin1Char('\n'));
        const QRegularExpression numbered(QStringLiteral("^\\s*\\d+[.)]\\s+(.+)$"));
        for (int i = 0; i < lines.size(); ++i) {
            const auto match = numbered.match(lines[i]);
            if (!match.hasMatch()) continue;
            bool answered = false;
            for (int j = i + 1; j < lines.size() && !numbered.match(lines[j]).hasMatch(); ++j)
                answered |= lines[j].trimmed().startsWith(QStringLiteral("Answer:"));
            if (!answered) return match.captured(1).trimmed();
        }
        return {};
    }
    void send(QJsonObject request) { if (onSend) onSend(request); }
    void upsert(const QJsonArray &cards) {
        for (const QJsonValue &value : cards) {
            const QJsonObject row = value.toObject();
            const QString id = row.value(QStringLiteral("id")).toString();
            if (!id.isEmpty()) m_rows.insert(id, row);
        }
    }
    void redraw() {
        QList<QJsonObject> due;
        for (const QJsonObject &row : m_rows)
            if (row.value(QStringLiteral("review_priority")).toInt() > 0) due << row;
        std::sort(due.begin(), due.end(), [](const QJsonObject &a, const QJsonObject &b) {
            const int x = a.value(QStringLiteral("review_priority")).toInt();
            const int y = b.value(QStringLiteral("review_priority")).toInt();
            return x == y ? a.value(QStringLiteral("id")).toString() < b.value(QStringLiteral("id")).toString()
                          : x > y;
        });
        const QString selected = m_selected;
        m_list->blockSignals(true);
        m_list->clear();
        int selectedRow = -1;
        for (const QJsonObject &row : due) {
            const QString id = row.value(QStringLiteral("id")).toString();
            auto *item = new QListWidgetItem(QStringLiteral("#%1  %2")
                                                 .arg(id, row.value(QStringLiteral("title")).toString()), m_list);
            item->setData(Qt::UserRole, id);
            if (id == selected) selectedRow = m_list->count() - 1;
        }
        m_list->blockSignals(false);
        const int rowHeight = m_list->sizeHintForRow(0) > 0 ? m_list->sizeHintForRow(0) : 24;
        m_list->setFixedHeight(qMin(5, qMax(1, m_list->count())) * rowHeight + 8);
        m_status->setText(due.isEmpty() ? QStringLiteral("Nothing needs your review.")
                                      : QStringLiteral("%1 card%2 %3 your judgement.")
                                            .arg(due.size())
                                            .arg(due.size() == 1 ? QString() : QStringLiteral("s"))
                                            .arg(due.size() == 1 ? QStringLiteral("needs") : QStringLiteral("need")));
        if (selectedRow >= 0) m_list->setCurrentRow(selectedRow);
        else if (!due.isEmpty()) m_list->setCurrentRow(0);
        else { m_selected.clear(); m_card = {}; showCard(); }
    }
    void selectCurrent() {
        const auto *item = m_list->currentItem();
        const QString id = item ? item->data(Qt::UserRole).toString() : QString();
        if (id == m_selected) return;
        m_selected = id;
        m_card = {};
        showCard();
        fetchSelected();
    }
    void fetchSelected() {
        if (!m_selected.isEmpty())
            send({{"type", "board_card_get"}, {"id", QStringLiteral("review-card-%1").arg(m_selected)},
                  {"card", m_selected}});
    }
    void showCard() {
        const bool loaded = !m_selected.isEmpty()
                            && m_card.value(QStringLiteral("card_id")).toString() == m_selected;
        m_open->setEnabled(loaded);
        if (!loaded) {
            m_detail->setMarkdown(m_selected.isEmpty() ? QString() : QStringLiteral("Loading card…"));
            m_question->hide(); m_answer->hide(); m_submit->hide(); m_signOff->hide();
            return;
        }
        const QString body = m_card.value(QStringLiteral("body")).toString();
        const QJsonObject front = m_card.value(QStringLiteral("front")).toObject();
        const QJsonObject verify = front.value(QStringLiteral("verify")).toObject();
        QStringList parts;
        parts << QStringLiteral("## %1").arg(m_card.value(QStringLiteral("title")).toString());
        for (const QString &heading : {QStringLiteral("Issue"), QStringLiteral("Done means"),
                                       QStringLiteral("Execution Summary"), QStringLiteral("Verdict"),
                                       QStringLiteral("Try it")}) {
            const QString content = section(body, heading);
            const QString label = heading == QStringLiteral("Issue") ? QStringLiteral("Goal")
                                  : heading == QStringLiteral("Done means") ? QStringLiteral("What to check")
                                  : heading == QStringLiteral("Execution Summary") ? QStringLiteral("Result")
                                  : heading == QStringLiteral("Try it") ? QStringLiteral("Staged review")
                                  : heading;
            if (!content.isEmpty()) parts << QStringLiteral("### %1\n%2").arg(label, content);
        }
        const QJsonArray evidence = front.value(QStringLiteral("links")).toObject()
                                        .value(QStringLiteral("evidence")).toArray();
        if (!evidence.isEmpty()) {
            parts << QStringLiteral("### Evidence");
            for (const QJsonValue &path : evidence) parts << QStringLiteral("- %1").arg(path.toString());
        }
        m_detail->setMarkdown(parts.join(QStringLiteral("\n\n")));
        const QString human = section(body, QStringLiteral("Human QA"));
        const QString open = firstOpenQuestion(human);
        const bool hasQuestion = QRegularExpression(QStringLiteral("(?m)^\\s*\\d+[.)]\\s+"))
                                     .match(human).hasMatch();
        const bool answerNeeded = verify.value(QStringLiteral("human")).toString() == QStringLiteral("required")
            && (!hasQuestion || !open.isEmpty());
        m_question->setText(!open.isEmpty() ? open : verify.value(QStringLiteral("criteria")).toString());
        m_question->setVisible(answerNeeded);
        m_answer->setVisible(answerNeeded);
        m_submit->setVisible(answerNeeded);
        const QString receipts = section(body, QStringLiteral("Verdict")) + QLatin1Char('\n')
                                 + section(body, QStringLiteral("Execution Summary"));
        const QString signOff = verify.value(QStringLiteral("sign_off")).toString();
        m_signOff->setVisible(!signOff.isEmpty() && signOff != QStringLiteral("none")
                              && !receipts.contains(QRegularExpression(QStringLiteral("(?m)^\\s*(?:-\\s*)?Receipt:"))));
        m_signOff->setText(QStringLiteral("Sign off: %1…").arg(signOff));
    }
    void recordAnswer() {
        const QString answer = m_answer->text().trimmed();
        if (answer.isEmpty() || m_card.isEmpty()) return;
        const QString body = m_card.value(QStringLiteral("body")).toString();
        QString human = section(body, QStringLiteral("Human QA"));
        if (human.isEmpty())
            human = QStringLiteral("1. Does this meet the stated criterion: %1?")
                        .arg(m_card.value(QStringLiteral("front")).toObject()
                                 .value(QStringLiteral("verify")).toObject()
                                 .value(QStringLiteral("criteria")).toString());
        const QString revision = m_card.value(QStringLiteral("hash")).toString().left(12);
        const QString answerLine = QStringLiteral("    Answer: %1 (reviewed revision %2)")
                                       .arg(answer.simplified(), revision);
        QStringList lines = human.split(QLatin1Char('\n'));
        const QRegularExpression numbered(QStringLiteral("^\\s*\\d+[.)]\\s+"));
        bool inserted = false;
        for (int i = 0; i < lines.size() && !inserted; ++i) {
            if (!numbered.match(lines[i]).hasMatch()) continue;
            int j = i + 1;
            while (j < lines.size() && !numbered.match(lines[j]).hasMatch()) ++j;
            bool answered = false;
            for (int k = i + 1; k < j; ++k)
                answered |= lines[k].trimmed().startsWith(QStringLiteral("Answer:"));
            if (!answered) { lines.insert(j, answerLine); inserted = true; }
        }
        if (!inserted) return;
        send({{"type", "board_update"}, {"id", QStringLiteral("review-write-%1").arg(m_selected)},
              {"card", m_selected}, {"base_hash", m_card.value(QStringLiteral("hash")).toString()},
              {"patch", QJsonObject{{"replace_section",
                                      QJsonObject{{"heading", "Human QA"},
                                                  {"text", lines.join(QLatin1Char('\n'))}}}}}});
    }
    void signOff() {
        if (m_card.isEmpty()) return;
        const QString kind = m_card.value(QStringLiteral("front")).toObject()
                                 .value(QStringLiteral("verify")).toObject()
                                 .value(QStringLiteral("sign_off")).toString();
        if (QMessageBox::question(this, QStringLiteral("Confirm sign-off"),
                                  QStringLiteral("Sign off #%1 for %2?").arg(m_selected, kind))
            != QMessageBox::Yes) return;
        const QString receipt = QStringLiteral("Receipt: owner signed off %1 for revision %2 at %3")
                                    .arg(kind, m_card.value(QStringLiteral("hash")).toString().left(12),
                                         QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        send({{"type", "board_update"}, {"id", QStringLiteral("review-write-%1").arg(m_selected)},
              {"card", m_selected}, {"base_hash", m_card.value(QStringLiteral("hash")).toString()},
              {"patch", QJsonObject{{"append_section",
                                      QJsonObject{{"heading", "Execution Summary"}, {"text", receipt}}}}}});
    }

    QHash<QString, QJsonObject> m_rows;
    QString m_selected;
    QJsonObject m_card;
    QLabel *m_status = nullptr, *m_question = nullptr;
    QListWidget *m_list = nullptr;
    QTextBrowser *m_detail = nullptr;
    QLineEdit *m_answer = nullptr;
    QPushButton *m_open = nullptr, *m_submit = nullptr, *m_signOff = nullptr;
};

} // namespace relay
