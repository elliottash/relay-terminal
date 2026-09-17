// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Plain-Qt folder explorer and file preview widgets. No KDE dependencies are required, so these
// are the portable path for macOS and Windows later. KSyntaxHighlighting and Qt PDF are optional.
#include <QString>
#include <QTimer>
#include <QWidget>
#include <functional>

class QFileSystemModel;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QScrollArea;
class QStackedWidget;
class QTextBrowser;
class QToolButton;
class QTreeView;

namespace relay {

// A directory browser rooted at one folder. Enter or double-click opens: a folder navigates into
// it, a file calls onOpenFile. Backspace or Alt+Up goes to the parent folder.
class FileExplorer : public QWidget {
public:
    explicit FileExplorer(const QString &root, QWidget *parent = nullptr);

    void setRoot(const QString &path);
    QString root() const { return m_root; }
    void goUp();
    void setShowHidden(bool show);
    bool showHidden() const { return m_showHidden; }
    // Type-to-filter: a case-insensitive substring over names in the current folder.
    void setFilter(const QString &text);
    // Paths currently listed under the root, in view order. Mainly for tests.
    QStringList visiblePaths() const;
    // Opens the given row (index into visiblePaths()); returns false when out of range.
    bool activateRow(int row);
    QTreeView *view() const { return m_view; }
    QLineEdit *filterEdit() const { return m_filter; }

    std::function<void(const QString &)> onOpenFile;          // a file was opened
    std::function<void(const QString &)> onDirectoryChanged;  // the root folder changed

protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void activate(const QString &path);
    void updateHeader();
    void hideUnmatchedFolders();

    QString m_root;
    bool m_showHidden = false;
    QFileSystemModel *m_model = nullptr;
    QTreeView *m_view = nullptr;
    QLabel *m_path = nullptr;
    QLineEdit *m_filter = nullptr;
    QToolButton *m_up = nullptr, *m_hidden = nullptr;
};

// A read-only preview of one file. The viewer is chosen by MIME type: text and code (with syntax
// highlighting when KSyntaxHighlighting is built in), Markdown (rendered or source), images
// (fit or 100%), PDF (when Qt PDF is built in), otherwise a file-info panel.
class FilePreview : public QWidget {
public:
    enum class Kind { None, Text, Markdown, Image, Pdf, Info };

    explicit FilePreview(QWidget *parent = nullptr);
    ~FilePreview() override;

    // Returns false (and shows nothing) when the path is not a readable regular file.
    bool open(const QString &path);
    bool reload() { return open(m_path); }
    // Scroll a text preview to a 1-based line and highlight it (no-op for other kinds).
    void goToLine(int line);
    QString path() const { return m_path; }
    QString title() const;
    Kind kind() const { return m_kind; }
    // Truncation or refusal message, empty when the whole file is shown.
    QString notice() const { return m_notice; }
    // Plain text shown by the Text or Markdown source viewer. Mainly for tests.
    QString text() const;

    static constexpr qint64 kMaxTextBytes = 2 * 1024 * 1024;
    static constexpr qint64 kMaxImageBytes = 64 * 1024 * 1024;
    static bool hasSyntaxHighlighting();
    static bool hasPdfSupport();

    std::function<void(const QString &)> onTitleChanged;

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void showText(const QString &path, qint64 size);
    void showMarkdown(const QString &path, qint64 size);
    bool showImage(const QString &path, qint64 size);
    void showPdf(const QString &path);
    void showInfo(const QString &path, const QString &mime, const QString &message = QString());
    QString readCapped(const QString &path, qint64 size);
    void setNotice(const QString &text);
    void updateImage();
    void updateModeButton();
    void updateTitleText();

    QString m_path, m_notice;
    Kind m_kind = Kind::None;
    bool m_markdownSource = false, m_imageActualSize = false;
    QLabel *m_title = nullptr, *m_noticeLabel = nullptr, *m_info = nullptr, *m_image = nullptr;
    QToolButton *m_mode = nullptr, *m_reload = nullptr, *m_external = nullptr;
    QStackedWidget *m_stack = nullptr;
    QPlainTextEdit *m_textView = nullptr;
    QTextBrowser *m_markdownView = nullptr;
    QScrollArea *m_imageArea = nullptr;
    QWidget *m_pdfPage = nullptr, *m_infoPage = nullptr;
    struct Private;
    Private *d = nullptr;
};

}  // namespace relay
