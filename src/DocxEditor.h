// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QJsonArray>
#include <QList>
#include <QString>
#include <QWidget>
#include <functional>

class QScrollArea;
class QToolButton;
class QTextEdit;
class QVBoxLayout;

namespace relay {

// An embedded editor for the simple paragraphs of a local DOCX. Complex Word objects stay in
// the file and appear read-only here. The Python bridge edits only anchored paragraphs in the
// original package, so headers, footnotes, media, comments and relationships survive a save.
class DocxEditor : public QWidget {
public:
    explicit DocxEditor(QWidget *parent = nullptr);
    bool open(const QString &path, QString *error);
    bool save(QString *error);
    bool isDirty() const { return m_dirty; }
    QString plainText() const;
    std::function<void(bool)> onDirtyChanged;

private:
    bool eventFilter(QObject *watched, QEvent *event) override;
    struct Paragraph {
        int id = -1;
        QTextEdit *editor = nullptr;
        bool editable = false;
    };
    bool runBridge(const QString &action, const QByteArray &input, QJsonObject *answer, QString *error);
    QJsonArray runsFor(const QTextEdit *editor) const;
    void markDirty();
    void setActive(QTextEdit *editor);
    void syncToolbar();

    QString m_path, m_sha;
    bool m_dirty = false;
    QTextEdit *m_active = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_page = nullptr;
    QVBoxLayout *m_paragraphs = nullptr;
    QToolButton *m_bold = nullptr, *m_italic = nullptr, *m_underline = nullptr;
    QList<Paragraph> m_items;
};

}  // namespace relay
