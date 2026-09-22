// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QWidget>
#include <functional>

class QLabel;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

namespace relay::globals {
// The shared manager hosts this page and routes its requests through its existing worker.
class GlobalsPane final : public QWidget {
public:
    explicit GlobalsPane(QWidget *parent = nullptr);
    std::function<void(const QJsonObject &)> onRequest;
    std::function<void()> onInterview;
    void handleEvent(const QJsonObject &event);
    void refresh();
    void setWorkspace(const QString &workspace);
    void focusSearch();
    QString agentScreen() const;
private:
    QString request(QJsonObject body);
    QString identity(const QJsonObject &record) const;
    void rebuild();
    void selectRecord();
    void display(const QJsonObject &record);
    void newRecord(const QString &kind);
    void updateButtons();
    bool protectDraft();
    QLineEdit *m_search;
    QComboBox *m_section;
    QLabel *m_intro;
    QPushButton *m_interview;
    QListWidget *m_list;
    QLabel *m_source;
    QLabel *m_notice;
    QPlainTextEdit *m_editor;
    QPushButton *m_save;
    QPushButton *m_cancel;
    QPushButton *m_retire;
    QPushButton *m_newMemory;
    QPushButton *m_newAlias;
    QJsonArray m_records;
    QJsonObject m_record;
    QString m_workspace, m_original, m_selected, m_listRequest, m_getRequest, m_writeRequest;
    bool m_dirty = false;
    bool m_loading = false;
};
} // namespace relay::globals
