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
    void remember(const QString &text);
    const QStringList &history() const { return m_history; }
    // True unless Up/Down is browsing history (Down on the last line then has nothing to do).
    bool atDraft() const { return m_historyIndex == m_history.size(); }

    // Ghost text: a dim suggestion drawn after the cursor when it sits at the end of the text.
    // Grow with the text instead of standing empty: one line when idle, up to maxLines, then scroll.
    void setAutoHeight(int minLines, int maxLines);
    void setGhost(const QString &remainder);
    QString ghost() const { return m_ghost; }
    // Accept the whole suggestion, or only up to the end of the next word. False if none.
    bool acceptGhost(bool wholeSuggestion);
protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void insertFromMimeData(const QMimeData *source) override;
private:
    QStringList m_history;
    QString m_draft;
    int m_historyIndex = 0;
    bool m_preedit = false;
    QString m_ghost;
    int m_minLines = 0, m_maxLines = 0;
};
