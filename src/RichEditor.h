// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QColor>
#include <QPlainTextEdit>
#include <QTimer>
#include <QStringList>
#include <functional>

class RichEditor : public QPlainTextEdit {
public:
    explicit RichEditor(QWidget *parent = nullptr);
    ~RichEditor() override;
    // Destination: "auto", "shell", or "agent". Submission never executes here.
    std::function<void(const QString &)> onSubmit;
    // Image context (issue EM1E): a paste or a drop carrying a picture becomes `@path` tokens
    // instead of text. The pane supplies this, because it knows where a pasted image is written and
    // it shows the shortcut hint; an empty result means "no image here" and the text path runs.
    std::function<QStringList(const QMimeData *, bool dropped)> onImageMime;
    void remember(const QString &text);
    const QStringList &history() const { return m_history; }
    // True unless Up/Down is browsing history (Down on the last line then has nothing to do).
    bool atDraft() const { return m_historyIndex == m_history.size(); }

    // Keep this box's history in a file instead of only in this session (src/PromptHistory.h):
    // what is in the file now is loaded, and every later remember() is appended to it. One file
    // serves every prompt box, so a browse that starts here first takes in whatever the panes
    // beside it have added. A box that never calls this — the Switchboard's reply box — keeps the
    // session-only history it always had.
    void useHistoryFile(const QString &path);
    // Re-read the file when it has changed since the last read. Called when a browse begins.
    void refreshHistory();
    // "Clear prompt history": the file is gone, so every box open on it drops its copy at once.
    // A box part-way through a browse would otherwise keep offering what was just forgotten until
    // it came back to its draft. What is in a box is left alone: it is the person's text now.
    static void forgetHistory(const QString &path);

    // Ghost text: a dim suggestion drawn after the cursor when it sits at the end of the text.
    // Grow with the text instead of standing empty: one line when idle, up to maxLines, then scroll.
    void setAutoHeight(int minLines, int maxLines);
    // The placeholder shrinks with the pane instead of wrapping onto a second line.
    void updatePlaceholder();
    // Other users of the editor (the Switchboard's reply box) hint at their own job. Longest
    // first; updatePlaceholder() picks the longest that fits.
    void setPlaceholders(const QStringList &candidates);
    // Relay paints the caret itself: Qt draws the built-in one in the stylesheet's text colour,
    // which cannot follow the destination (terminal, agent) per keystroke.
    void setCaretColor(const QColor &color);
    void setGhost(const QString &remainder);
    QString ghost() const { return m_ghost; }
    // Accept the whole suggestion, or only up to the end of the next word. False if none.
    bool acceptGhost(bool wholeSuggestion);
protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void insertFromMimeData(const QMimeData *source) override;
    bool canInsertFromMimeData(const QMimeData *source) const override;
    void dropEvent(QDropEvent *event) override;
private:
    // Inserts `@path` tokens for attached images, separated from whatever is already typed.
    void insertAttachments(const QStringList &tokens);
    bool m_dropping = false;
    QStringList m_history;
    QStringList m_placeholders;
    QString m_draft;
    int m_historyIndex = 0;
    QString m_historyPath;
    // Size and mtime of the file at the last read; "?" until one has happened, which no real
    // stamp spells, and empty for "there is no file".
    QString m_historyStamp = QStringLiteral("?");
    bool m_historySeen = false;      // the file existed at the last look: an empty read now means cleared
    int m_historyMax = 200;          // raised to prompthistory::kMaxEntries once a file is attached
    bool m_preedit = false;
    QString m_ghost;
    int m_minLines = 0, m_maxLines = 0;
    QColor m_caret;
    QTimer *m_caretBlink = nullptr;
    bool m_caretOn = true;
};
