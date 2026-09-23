// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Plain-Qt folder explorer and file preview widgets. No KDE dependencies are required, so these
// are the portable path for macOS and Windows later. KSyntaxHighlighting and Qt PDF are optional.
#include <QList>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QModelIndex>
#include <QVector>
#include <QWidget>
#include <functional>

#include "RemoteFiles.h"   // files and folders on the host a pane is logged into (#S5SH)

class QAbstractItemModel;
class QFileSystemModel;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QScrollArea;
class QStackedWidget;
class QShortcut;
class QStandardItemModel;
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
// onOpenFile. Ctrl+Enter on a file calls onEditFile instead (open it in Relay ready to edit),
// and Shift+Enter hands it to the desktop (onOpenExternal, card #SEJ2): Ctrl is the in-app
// variant, Shift the one that leaves the app, the same split the conversation list teaches.
// Backspace or Alt+Up goes to the parent folder. Right-click offers explorerMenu().
//
// The folder may be on the host a terminal pane is logged into (card #S5SH): `setRoot()` takes an
// `ssh://<host>/<path>/` the same way `FilePreview::open()` takes a file's URL, and then the rows
// come from one `stat` of that folder over the pane's own ssh connection instead of from
// QFileSystemModel. Everything the pane hands out — `root()`, `visiblePaths()`, `onOpenFile` —
// stays in that URL form, so the window opens what it is given without knowing which machine it
// is on. A remote folder is read only: Relay does not rename, delete or create on someone else's
// machine from here.
class FileExplorer : public QWidget {
public:
    explicit FileExplorer(const QString &root, QWidget *parent = nullptr);

    void setRoot(const QString &path);
    QString root() const { return m_root; }
    bool isRemote() const { return !m_remoteHost.isEmpty(); }
    QString remoteHost() const { return m_remoteHost; }
    // "filly:/etc" for a remote folder, the folder's own path for a local one: what a tab calls it.
    QString title() const;
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
    // Ctrl+Enter on a file: open it ready to edit (card #SEJ2). Folders ignore the modifier.
    std::function<void(const QString &)> onEditFile;
    // Shift+Enter on a row: the desktop opens it. Unset means this pane does it itself with
    // QDesktopServices; a test sets it to watch instead. Remote (ssh://) rows never fire it.
    std::function<void(const QString &)> onOpenExternal;
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
    // One Enter-family chord on a row: plain opens inside Relay, Ctrl+Enter opens ready to edit,
    // Shift+Enter hands to the desktop; a folder only navigates (card #SEJ2).
    void openFromKeyboard(const QModelIndex &index, Qt::KeyboardModifiers mods);
    void updateHeader();
    void hideUnmatchedFolders();
    // ----- one folder on another machine (#S5SH) -------------------------------------------
    void setRemoteRoot(const QString &url);
    void fillRemoteRows(const QVector<relay::remote::DirEntry> &entries, bool truncated);
    void remoteFailed(const QString &message);
    // The model the view is showing, and what a row in it is: a local row answers through
    // QFileSystemModel, a remote row through the listing the host sent.
    QAbstractItemModel *activeModel() const;
    QString pathAt(const QModelIndex &index) const;   // an absolute path, or an `ssh://` URL
    bool isDirAt(const QModelIndex &index) const;
    QString nameAt(const QModelIndex &index) const;
    QModelIndex indexOf(const QString &path) const;
    void showMenu(const QPoint &viewportPos);
    void runMenuAction(const QString &id, const QString &path);
    void createEntry(bool folder);
    void renameEntry(const QString &path);
    void deleteEntry(const QString &path);
    void select(const QString &path);

    QString m_root;          // a local path, or `ssh://host/path/` while remote
    QString m_remoteHost, m_remoteDir;
    bool m_showHidden = false;
    bool m_singleClick = true;
    Qt::KeyboardModifiers m_clickModifiers = Qt::NoModifier;
    QFileSystemModel *m_model = nullptr;
    QStandardItemModel *m_remoteModel = nullptr;
    relay::remote::RemoteDir *m_remoteList = nullptr;
    QTreeView *m_view = nullptr;
    QHBoxLayout *m_header = nullptr;
    QLabel *m_path = nullptr, *m_notice = nullptr;
    QLineEdit *m_filter = nullptr;
    QToolButton *m_up = nullptr, *m_hidden = nullptr;
};

// A preview of one file. The viewer is chosen by MIME type: text and code (with syntax
// highlighting when KSyntaxHighlighting is built in), Markdown (rendered or source), images
// (fit or 100%), PDF (when Qt PDF is built in), otherwise a file-info panel. A local text or
// Markdown file opens read only and turns into an editor on the header's ✎ button or on
// Ctrl+Enter from the explorer (card #SEJ2): Ctrl+S saves back to the same path (atomically,
// through QSaveFile), a ● in the title marks unsaved edits, and opening or reloading a dirty
// file asks first.
//
// A file on the host a terminal pane is logged into (card #S5SH) opens in this same pane, fetched
// over that pane's own ssh connection, and — owner, 2026-09-18, "editing allowed so it's equal to
// local text editing" — it is editable: Ctrl+S, a ● in the title while it is unsaved, and the save
// goes back over ssh. It is addressed as `ssh://<host>/<path>` (relay::remote::fileUrl), which is
// what open(), the pane title, the saved window layout and reload() all carry, so a remote file
// travels every route a local one does.
class FilePreview : public QWidget {
public:
    enum class Kind { None, Text, Markdown, Image, Pdf, Info };

    explicit FilePreview(QWidget *parent = nullptr);
    ~FilePreview() override;

    // A local absolute path, or `ssh://host/path` for a file on a host a pane is logged into.
    // Returns false (and shows nothing) when a local path is not a readable regular file; a
    // remote file is fetched asynchronously, so true only means the fetch started.
    bool open(const QString &path);
    bool reload() { return open(m_path); }

    // ----- files on a remote host (#S5SH) ---------------------------------------------------
    bool isRemote() const { return !m_remoteHost.isEmpty(); }
    QString remoteHost() const { return m_remoteHost; }
    QString remotePath() const { return m_remotePath; }
    // Unsaved edits. False while the pane is read only, which a local file is until
    // startEditing().
    bool isDirty() const;
    // True while the pane is an editor rather than a read-only preview.
    bool isEditable() const { return m_editable; }
    // Turn the preview into an editor (the ✎ button, or Ctrl+Enter from the explorer, card
    // #SEJ2). A Markdown file is edited as source, so the source view comes up first. A no-op
    // for a file that cannot be edited here (image, PDF, info) or already is.
    void startEditing();
    // Write the buffer back — to the host over ssh for a remote file, atomically to its own
    // path for a local one (Ctrl+S and the Save button). Returns false when there is nothing
    // to save or no connection to save over; a remote save itself lands later.
    bool save();
    // Scroll a text preview to a 1-based line and highlight it (no-op for other kinds).
    void goToLine(int line);
    // Word wrap on or off (Alt+Z, files.toggleWrap): the same switch the header button moves.
    // Returns false for a file whose view does not wrap (image, PDF, rendered Markdown), so the
    // key can fall through to another preview pane instead of doing nothing silently.
    bool toggleWrap();
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
    // Past this, a text file is shown plain: colouring it would cost more than it is worth, and
    // no reader waits that long for it (#MDSG). It only turns anything away for a file on a
    // host, which is the one text path kMaxTextBytes does not already cap.
    static constexpr qint64 kMaxHighlightBytes = 2 * 1024 * 1024;

    // ----- syntax highlighting (#MDSG) ------------------------------------------------------
    // Highlighting is optional at build time, and when it is built in it runs a slice at a time
    // after the text is on screen rather than all at once before it. These three say what state
    // that is in, mainly so a test can wait for the end of it.
    static bool syntaxHighlightingBuiltIn();
    // Blocks coloured so far; 0 when this file has no highlighter (no definition, too big, or
    // not a text file).
    int highlightedBlocks() const;
    // True while there is more of the open file to colour.
    bool highlighting() const;

    std::function<void(const QString &)> onTitleChanged;
    // A link to a local file or folder was clicked in the rendered Markdown. The preview never
    // follows it itself (issue S1JP): the host opens a pane for it and this one keeps its file.
    // Without a host, the link is handed to the desktop instead.
    std::function<void(const QString &)> onOpenLink;
    // "Next time: Ctrl+S" after the Save button is clicked (WARP.md, "Shortcut hints"). The pane
    // has no toast of its own, so the hint goes in its notice line; the registry decides whether
    // it may be shown at all.

protected:
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    bool openRemote(const QString &url);
    // The host's bytes, shown the way the same file would be shown from this disk: Markdown
    // rendered (and editable as source), images, PDF where Qt PDF is built in, text in the
    // editor, anything else as a file-info panel.
    void showRemoteContent(const QByteArray &content);
    void showRemoteText(const QByteArray &content, bool markdown);
    bool showRemoteImage(const QByteArray &content);
    bool showRemotePdf(const QByteArray &content);
    void showRemoteInfo(const QString &mime, const QString &message = QString());
    void remoteFailed(const QString &message, int conflict);
    void setEditable(bool on);
    // The ✎ button shows only while a local text or Markdown file sits read only (#SEJ2).
    void updateEditButton();
    void watchForReconnect();
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
    // #S5SH: the host and the path on it, empty for a local file; the fetch/save worker; and the
    // line a click asked for, which a remote file cannot go to until its bytes arrive.
    QString m_remoteHost, m_remotePath;
    bool m_editable = false;
    bool m_teachSaveShortcut = false;   // the Save button was clicked: teach Ctrl+S when it lands
    int m_pendingLine = 0;
    relay::remote::RemoteFile *m_remote = nullptr;
    QTimer *m_reconnect = nullptr;
    QHBoxLayout *m_header = nullptr;
    QLabel *m_title = nullptr, *m_noticeLabel = nullptr, *m_info = nullptr, *m_image = nullptr, *m_hostChip = nullptr;
    QToolButton *m_mode = nullptr, *m_reload = nullptr, *m_external = nullptr, *m_save = nullptr, *m_edit = nullptr;
    QToolButton *m_wrap = nullptr;
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
