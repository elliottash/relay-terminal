// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Plain-Qt folder explorer and file preview widgets. No KDE dependencies are required, so these
// are the portable path for macOS and Windows later. KSyntaxHighlighting and Qt PDF are optional.
#include <QList>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QWidget>
#include <functional>

class QFileSystemModel;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QScrollArea;
class QStackedWidget;
class QTextBrowser;
class QToolButton;
class QTreeView;

namespace relay {

// ----- right-click menus (issues #D60R, V9V1) ---------------------------------------------------
//
// The menu is described as data so the list — which entries appear for a folder, for a file and
// for the empty space below the rows, and which of them are greyed out — can be checked without a
// window (tests/filepanes_test.cpp). FileExplorer turns it into a QMenu.

enum class FileMenuTarget { None, File, Folder };

struct FileMenuItem {
    QString id;               // "openInternal", "openExternal", "openFolder", "navigate", …; "-" is a separator
    QString label;
    bool enabled = true;
    bool isSeparator() const { return id == QLatin1String("-"); }
};

// What the host can do with the clicked row. `writable` is the parent folder's permission, which
// decides whether new file / new folder / rename / delete are offered at all.
struct FileMenuHost {
    bool canNavigateTerminal = false;  // "Navigate here" is wired up
    bool canPreview = false;           // "Open in a preview pane" is wired up
    bool canSetWorkspace = false;      // "Set as agent workspace" is wired up
    bool writable = true;
};

// The three entries every menu leads with, appended in the owner's order (issue V9V1, 2026-09-18:
// "open internal at the top and open external second and open folder third"). Internal is a Relay
// pane, external is the desktop's default application, folder is the desktop's file manager.
void openEntries(QList<FileMenuItem> &items, FileMenuTarget target, const FileMenuHost &host);

// The entries for one right-click, in order, with separators as items whose id is "-". Never
// starts or ends with a separator and never has two in a row.
QList<FileMenuItem> explorerMenu(FileMenuTarget target, const FileMenuHost &host);

// The same three entries plus Copy path, for a right-click inside a preview pane. The viewer's
// own Copy / Select all follow them in the QMenu that FilePreview builds.
QList<FileMenuItem> previewMenu(const FileMenuHost &host);

// A directory browser rooted at one folder. Enter, or a click (double by default, single when
// "Open items with a single click" is on), opens: a folder navigates into it, a file calls
// onOpenFile. Backspace or Alt+Up goes to the parent folder. Right-click offers explorerMenu().
class FileExplorer : public QWidget {
public:
    explicit FileExplorer(const QString &root, QWidget *parent = nullptr);

    void setRoot(const QString &path);
    QString root() const { return m_root; }
    void goUp();
    void setShowHidden(bool show);
    // Type-to-filter: a case-insensitive substring over names in the current folder.
    void setFilter(const QString &text);
    // Paths currently listed under the root, in view order. Mainly for tests.
    QStringList visiblePaths() const;
    // Opens the given row (index into visiblePaths()); returns false when out of range.
    bool activateRow(int row);
    QTreeView *view() const { return m_view; }
    QLineEdit *filterEdit() const { return m_filter; }
    // Room the host's floating pane buttons need at the right of the header row, so the folder
    // line and the hidden-files button never end up underneath them (the same contract as a
    // terminal pane's header).
    void setHeaderRightInset(int pixels);

    // Dolphin-style opening (issue #0C7V). On by default; Ctrl+click and Shift+click never open,
    // they extend the selection, and a drag never opens either.
    void setSingleClick(bool on) { m_singleClick = on; }
    bool singleClick() const { return m_singleClick; }
    static bool singleClickDefault();   // the "files/single_click" setting, true when unset

    // The entries a right-click on `path` offers, with "" meaning the empty space below the rows.
    // What showMenu() builds its QMenu from, and what a test checks.
    QList<FileMenuItem> menuFor(const QString &path) const;

    std::function<void(const QString &)> onOpenFile;          // a file was opened
    std::function<void(const QString &)> onDirectoryChanged;  // the root folder changed
    std::function<void(const QString &)> onNavigateHere;      // move the terminal to this folder
    std::function<void(const QString &)> onOpenInPreview;     // open this file in a preview pane
    std::function<void(const QString &)> onSetWorkspace;      // make this folder the agent workspace
    std::function<void()> onCloseRequested;                   // the header folder was clicked again

protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void activate(const QString &path);
    void updateHeader();
    void hideUnmatchedFolders();
    void showMenu(const QPoint &viewportPos);
    void runMenuAction(const QString &id, const QString &path);
    void createEntry(bool folder);
    void renameEntry(const QString &path);
    void deleteEntry(const QString &path);
    void select(const QString &path);

    QString m_root;
    bool m_showHidden = false;
    bool m_singleClick = true;
    Qt::KeyboardModifiers m_clickModifiers = Qt::NoModifier;
    QFileSystemModel *m_model = nullptr;
    QTreeView *m_view = nullptr;
    QHBoxLayout *m_header = nullptr;
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
    // Markdown only: true while the source is on screen rather than the render. A .md file opens
    // rendered; goToLine() and the view button are the only things that turn this on.
    bool showingSource() const { return m_kind == Kind::Markdown && m_markdownSource; }
    // Truncation or refusal message, empty when the whole file is shown.
    QString notice() const { return m_notice; }
    // Plain text shown by the Text or Markdown source viewer. Mainly for tests.
    QString text() const;
    // Room the host's floating pane buttons need at the right of the header row, so the view
    // button ("Source (MD)") never ends up underneath them.
    void setHeaderRightInset(int pixels);
    // The entries a right-click in the preview offers, in order: what showMenu() builds its QMenu
    // from, and what a test checks. Empty while no file is open.
    QList<FileMenuItem> menu() const;

    static constexpr qint64 kMaxTextBytes = 2 * 1024 * 1024;
    static constexpr qint64 kMaxImageBytes = 64 * 1024 * 1024;

    std::function<void(const QString &)> onTitleChanged;
    // A link to a local file or folder was clicked in the rendered Markdown. The preview never
    // follows it itself (issue S1JP): the host opens a pane for it and this one keeps its file.
    // Without a host, the link is handed to the desktop instead.
    std::function<void(const QString &)> onOpenLink;

protected:
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    void followLink(const QUrl &url);
    bool showMenu(const QPoint &globalPos, QWidget *source);
    void runMenuAction(const QString &id);
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
    QHBoxLayout *m_header = nullptr;
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

// An editable Markdown document pane (agent plans, relay.md). Save with Ctrl+S or the Save button;
// a dot in the title marks unsaved edits. For plans, Execute / Execute in fresh context / Keep
// planning call back into the pane that wrote the plan; the caller saves first.
class PlanEditor : public QWidget {
public:
    explicit PlanEditor(QWidget *parent = nullptr);
    ~PlanEditor() override;

    bool open(const QString &path);
    bool save();
    bool isDirty() const;
    QString path() const { return m_path; }
    QString title() const;
    QString text() const;
    QPlainTextEdit *editor() const { return m_editor; }
    // Plans show the Execute buttons; other documents (relay.md) only Save and Reload.
    void setPlanActions(bool enabled);

    std::function<void(bool fresh)> onExecute;
    std::function<void()> onKeepPlanning;
    std::function<void(const QString &)> onTitleChanged;

private:
    void updateTitle();

    QString m_path;
    QLabel *m_title = nullptr, *m_notice = nullptr;
    QPlainTextEdit *m_editor = nullptr;
    QWidget *m_planActions = nullptr;
    struct Private;
    Private *d = nullptr;
};

}  // namespace relay
