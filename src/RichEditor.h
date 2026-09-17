// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QPlainTextEdit>
#include <QStringList>
#include <functional>

class RichEditor : public QPlainTextEdit {
public:
    explicit RichEditor(QWidget *parent = nullptr);
    // Destination: "auto", "shell", or "agent". Submission never executes here.
    std::function<void(const QString &)> onSubmit;
    std::function<void()> onNative;
    void remember(const QString &text);
protected:
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void insertFromMimeData(const QMimeData *source) override;
private:
    QStringList m_history;
    QString m_draft;
    int m_historyIndex = 0;
    bool m_preedit = false;
};
