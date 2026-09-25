// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FilePanes.h"
#include "DocxEditor.h"
#include "CopyOnSelect.h"
#include "Hints.h"
#include "Keymap.h"
#include "RemoteFiles.h"
#include "Theme.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QPointer>
#include <QStandardItemModel>
#include <QTextBlock>
#include <QTextCursor>

#include <QSaveFile>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QTextDocument>
#include <QTextCharFormat>
#include <QTimer>
#include <QDateTime>
#include <QElapsedTimer>
#include <QResizeEvent>
#include <algorithm>
#include <utility>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFileSystemWatcher>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QListWidget>
#include <QImageReader>
#include <QInputDialog>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QMouseEvent>
#include <QProcess>
#include <QSettings>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

#ifdef RELAY_HAVE_SYNTAX_HIGHLIGHTING
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/SyntaxHighlighter>
#include <KSyntaxHighlighting/Theme>
#endif
#ifdef RELAY_HAVE_QTPDF
#include <QPdfDocument>
#include <QPdfView>
#endif

namespace relay {

namespace {
QToolButton *headerButton(const QString &text, const QString &tooltip) {
    auto *button = new QToolButton;
    button->setText(text);
    button->setToolTip(tooltip);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

QString humanSize(qint64 bytes) {
    return QLocale().formattedDataSize(bytes);
}

// "Open folder": ask the desktop's file manager to show the entry, so the file is selected in the
// folder it sits in. Falls back to opening the folder itself when no FileManager1 service answers.
void openContainingFolder(const QString &path) {
    const QString folder = QFileInfo(path).isDir() ? path : QFileInfo(path).absolutePath();
    if (!QProcess::startDetached(QStringLiteral("dbus-send"),
                                 {QStringLiteral("--session"), QStringLiteral("--print-reply"),
                                  QStringLiteral("--dest=org.freedesktop.FileManager1"),
                                  QStringLiteral("--type=method_call"), QStringLiteral("/org/freedesktop/FileManager1"),
                                  QStringLiteral("org.freedesktop.FileManager1.ShowItems"),
                                  QStringLiteral("array:string:") + QUrl::fromLocalFile(path).toString(),
                                  QStringLiteral("string:")}))
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

QString tildePath(const QString &path) {
    const QString home = QDir::homePath();
    if (path == home) return QStringLiteral("~");
    if (path.startsWith(home + QLatin1Char('/'))) return QStringLiteral("~") + path.mid(home.size());
    return path;
}
}  // namespace

// ----- right-click menus --------------------------------------------------------------------------

void openEntries(QList<FileMenuItem> &items, FileMenuTarget target, const FileMenuHost &host) {
    if (target != FileMenuTarget::None)
        items.append({QStringLiteral("openInternal"), QStringLiteral("Open internal"),
                      target == FileMenuTarget::Folder || host.canPreview});
    items.append({QStringLiteral("openExternal"), QStringLiteral("Open external"), true});
    items.append({QStringLiteral("openFolder"), QStringLiteral("Open folder"), true});
}

QList<FileMenuItem> previewMenu(const FileMenuHost &host) {
    // The preview shows one file, so its menu is the three open entries and the two copies; there
    // is no row that was clicked and nothing to create, rename or delete. "Open internal" reopens
    // the file in this pane, which is also how a Markdown file gets back to the rendered view.
    QList<FileMenuItem> items;
    openEntries(items, FileMenuTarget::File, host);
    items.append({QStringLiteral("-"), QString(), true});
    items.append({QStringLiteral("copyPath"), QStringLiteral("Copy path"), true});
    return items;
}

QList<FileMenuItem> explorerMenu(FileMenuTarget target, const FileMenuHost &host) {
    QList<FileMenuItem> items;
    auto add = [&items](const char *id, const QString &label, bool enabled = true) {
        items.append({QString::fromLatin1(id), label, enabled});
    };
    auto separate = [&items] {
        if (!items.isEmpty() && !items.last().isSeparator()) items.append({QStringLiteral("-"), QString(), true});
    };

    // The three the owner asked every entry to lead with, in their order (issue V9V1, owner
    // 2026-09-18: "when you right click it should say open internal at the top and open external
    // second and open folder third"). "Open internal" is a Relay pane — a preview for a file, this
    // explorer for a folder; "open external" is the desktop's default application; "open folder"
    // hands the entry to the desktop's file manager, which is what this menu used to call "Reveal
    // in file manager". The empty space below the rows offers the last two for the folder it is
    // showing, which is already open internally.
    openEntries(items, target, host);
    if (host.canNavigateTerminal) add("navigate", QStringLiteral("Navigate here"));

    if (target != FileMenuTarget::None) {
        separate();
        add("copyPath", QStringLiteral("Copy path"));
        add("copyRelativePath", QStringLiteral("Copy relative path"));
    }

    separate();
    add("newFile", QStringLiteral("New file…"), host.writable);
    add("newFolder", QStringLiteral("New folder…"), host.writable);
    if (target != FileMenuTarget::None) {
        add("rename", QStringLiteral("Rename…"), host.writable);
        add("delete", QStringLiteral("Delete…"), host.writable);
    }

    if (host.canSetWorkspace && target != FileMenuTarget::File) {
        separate();
        add("workspace", QStringLiteral("Set as agent workspace"));
    }

    while (!items.isEmpty() && items.last().isSeparator()) items.removeLast();
    return items;
}

// ----- FileExplorer ------------------------------------------------------------------------------

FileExplorer::FileExplorer(const QString &root, QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("fileExplorer"));
    setAttribute(Qt::WA_StyledBackground);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->setSpacing(6);

    auto *header = new QHBoxLayout;
    m_header = header;
    header->setSpacing(4);
    m_up = headerButton(QStringLiteral("↑"), QStringLiteral("Parent folder (Backspace)"));
    m_up->setObjectName(QStringLiteral("fileExplorerUp"));
    m_path = new QLabel;
    m_path->setObjectName(QStringLiteral("fileExplorerPath"));
    m_path->setTextFormat(Qt::PlainText);
    m_path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    relay::installCopyOnSelect(m_path);
    // Long paths elide instead of forcing the pane wide.
    m_path->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_path->setMinimumWidth(40);
    // Clicking the folder in the header closes the explorer again (issue #D60R): the same header
    // line that opened it from the terminal pane puts it away.
    m_path->setCursor(Qt::PointingHandCursor);
    m_path->installEventFilter(this);
    m_hidden = headerButton(QStringLiteral(".*"), QStringLiteral("Show hidden files"));
    m_hidden->setObjectName(QStringLiteral("fileExplorerHidden"));
    m_hidden->setCheckable(true);
    header->addWidget(m_up);
    header->addWidget(m_path, 1);
    header->addWidget(m_hidden);
    layout->addLayout(header);

    m_filter = new QLineEdit;
    m_filter->setObjectName(QStringLiteral("fileExplorerFilter"));
    m_filter->setPlaceholderText(QStringLiteral("Filter · ↓ to list · Enter opens"));
    m_filter->setClearButtonEnabled(true);
    layout->addWidget(m_filter);

    // Only a remote folder has anything to say here: that it is being read, or why it could not
    // be (#S5SH). The same line the preview pane uses, so the two panes read alike.
    m_notice = new QLabel;
    m_notice->setObjectName(QStringLiteral("filePreviewNotice"));
    m_notice->setWordWrap(true);
    m_notice->hide();
    layout->addWidget(m_notice);

    m_model = new QFileSystemModel(this);
    m_model->setReadOnly(true);
    m_model->setNameFilterDisables(false);
    m_model->setFilter(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::System);

    m_view = new QTreeView;
    m_view->setObjectName(QStringLiteral("fileExplorerView"));
    m_view->setModel(m_model);
    m_view->setRootIsDecorated(false);
    m_view->setItemsExpandable(false);
    m_view->setUniformRowHeights(true);
    m_view->setSortingEnabled(true);
    m_view->sortByColumn(0, Qt::AscendingOrder);
    // Ctrl+click and Shift+click extend the selection; they never open (issue #D60R).
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_singleClick = singleClickDefault();
    m_view->setColumnHidden(2, true);  // type
    m_view->header()->setStretchLastSection(false);
    m_view->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_view->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_view->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    layout->addWidget(m_view, 1);

    connect(m_up, &QToolButton::clicked, this, [this] { goUp(); });
    connect(m_hidden, &QToolButton::toggled, this, [this](bool on) { setShowHidden(on); });
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &text) { setFilter(text); });
    connect(m_view, &QTreeView::doubleClicked, this, [this](const QModelIndex &index) {
        if (m_singleClick) return;   // the single click already opened it
        if (index.isValid()) activate(pathAt(index));
    });
    // Dolphin-style single click (issue #0C7V). clicked() only fires when press and release land
    // on the same row without a drag, so dragging still selects; the modifiers are the ones from
    // the press, so Ctrl+click and Shift+click only extend the selection.
    connect(m_view, &QTreeView::clicked, this, [this](const QModelIndex &index) {
        if (!m_singleClick || !index.isValid()) return;
        if (m_clickModifiers & (Qt::ControlModifier | Qt::ShiftModifier)) return;
        activate(pathAt(index));
    });
    connect(m_view, &QTreeView::customContextMenuRequested, this, [this](const QPoint &at) { showMenu(at); });
    // Keep a selection once the folder finishes loading, so arrows and Enter work at once.
    connect(m_model, &QFileSystemModel::rowsInserted, this, [this] { hideUnmatchedFolders(); });
    connect(m_model, &QFileSystemModel::layoutChanged, this, [this] { hideUnmatchedFolders(); });
    connect(m_model, &QFileSystemModel::directoryLoaded, this, [this](const QString &path) {
        if (QDir(path) != QDir(m_root)) return;
        m_model->sort(0, Qt::AscendingOrder);
        const QModelIndex rootIndex = m_view->rootIndex();
        if (!m_view->currentIndex().isValid() && m_model->rowCount(rootIndex) > 0)
            m_view->setCurrentIndex(m_model->index(0, 0, rootIndex));
    });
    m_view->installEventFilter(this);
    m_view->viewport()->installEventFilter(this);
    m_filter->installEventFilter(this);
    // In the explorer list Left goes to the parent and Right enters the selected folder; these
    // local keys take precedence over the window's pane-navigation shortcuts (#2K7Q, #KYPR).
    // Alt+Up remains another way to go up. The filter is deliberately excluded: its arrows edit text.
    m_view->setProperty("relayLocalKeys", QStringList{QStringLiteral("Alt+Up"), QStringLiteral("Left"), QStringLiteral("Right")});
    m_filter->setProperty("relayLocalKeys", QStringList{QStringLiteral("Alt+Up")});
    setRoot(root.isEmpty() ? QDir::homePath() : root);
}

bool FileExplorer::singleClickDefault() {
    return QSettings().value(QStringLiteral("files/single_click"), true).toBool();
}

void FileExplorer::setRoot(const QString &path) {
    if (relay::remote::isFileUrl(path)) { setRemoteRoot(path); return; }
    const QFileInfo info(path);
    if (!info.isDir()) return;
    if (isRemote()) {
        // Back from a host to this machine: the file model takes the view over again.
        m_remoteHost.clear();
        m_remoteDir.clear();
        if (m_remoteList) m_remoteList->cancel();
        if (m_remoteModel) m_remoteModel->removeRows(0, m_remoteModel->rowCount());
        m_view->setModel(m_model);
        m_view->setSortingEnabled(true);
        m_notice->hide();
    }
    const QString canonical = info.canonicalFilePath().isEmpty() ? info.absoluteFilePath() : info.canonicalFilePath();
    const bool changed = canonical != m_root;
    m_root = canonical;
    m_model->setRootPath(m_root);
    m_view->setRootIndex(m_model->index(m_root));
    m_view->setCurrentIndex(QModelIndex());
    if (!m_filter->text().isEmpty()) {
        const QSignalBlocker block(m_filter);
        m_filter->clear();
        m_model->setNameFilters({});
    }
    updateHeader();
    if (changed && onDirectoryChanged) onDirectoryChanged(m_root);
}

void FileExplorer::goUp() {
    if (isRemote()) {
        const QString parent = relay::remote::parentPath(m_remoteDir);
        if (parent.isEmpty() || parent == m_remoteDir) return;
        setRemoteRoot(relay::remote::folderUrl(m_remoteHost, parent));
        return;
    }
    QDir dir(m_root);
    if (!dir.cdUp()) return;
    const QString previous = m_root;
    setRoot(dir.absolutePath());
    // Select the folder we came from once it is listed.
    const QModelIndex index = m_model->index(previous);
    if (index.isValid()) m_view->setCurrentIndex(index);
}

void FileExplorer::setShowHidden(bool show) {
    m_showHidden = show;
    if (m_hidden->isChecked() != show) { const QSignalBlocker block(m_hidden); m_hidden->setChecked(show); }
    QDir::Filters filters = QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::System;
    if (show) filters |= QDir::Hidden;
    m_model->setFilter(filters);
    // The host sends every entry once; which of them are shown is decided here, so toggling
    // hidden files costs no second listing.
    if (isRemote()) hideUnmatchedFolders();
}

void FileExplorer::setFilter(const QString &text) {
    if (m_filter->text() != text) { const QSignalBlocker block(m_filter); m_filter->setText(text); }
    QString needle = text.trimmed();
    // Wildcard characters would be interpreted by the model; match them literally as "any char".
    needle.replace(QLatin1Char('*'), QLatin1Char('?')).replace(QLatin1Char('['), QLatin1Char('?')).replace(QLatin1Char(']'), QLatin1Char('?'));
    m_model->setNameFilters(needle.isEmpty() ? QStringList() : QStringList{QLatin1Char('*') + needle + QLatin1Char('*')});
    hideUnmatchedFolders();
    const QStringList visible = visiblePaths();
    if (!visible.isEmpty()) m_view->setCurrentIndex(indexOf(visible.first()));
}

QStringList FileExplorer::visiblePaths() const {
    QStringList paths;
    const QModelIndex rootIndex = m_view->rootIndex();
    QAbstractItemModel *model = activeModel();
    for (int row = 0; row < model->rowCount(rootIndex); ++row)
        if (!m_view->isRowHidden(row, rootIndex)) paths << pathAt(model->index(row, 0, rootIndex));
    return paths;
}

// QFileSystemModel applies name filters to files only; folders stay visible. Hide folders
// that do not match the filter text in the view, and re-apply as rows load.
void FileExplorer::hideUnmatchedFolders() {
    const QString needle = m_filter->text().trimmed();
    const QModelIndex rootIndex = m_view->rootIndex();
    QAbstractItemModel *model = activeModel();
    for (int row = 0; row < model->rowCount(rootIndex); ++row) {
        const QModelIndex index = model->index(row, 0, rootIndex);
        const QString name = nameAt(index);
        // Remote rows are filtered here and nowhere else: the listing carries every entry, dot
        // files included, and the model does no filtering of its own.
        bool hide = !needle.isEmpty() && (isRemote() || isDirAt(index)) && !name.contains(needle, Qt::CaseInsensitive);
        if (isRemote() && !m_showHidden && name.startsWith(QLatin1Char('.'))) hide = true;
        m_view->setRowHidden(row, rootIndex, hide);
    }
}

bool FileExplorer::activateRow(int row) {
    // The rows on screen, not the rows in the model: with a filter on, or with the host's dot
    // files hidden, the two are not the same list (#S5SH).
    const QStringList visible = visiblePaths();
    if (row < 0 || row >= visible.size()) return false;
    activate(visible.at(row));
    return true;
}

void FileExplorer::activate(const QString &path) {
    // A remote row carries its answer in its own URL: a folder ends in `/` (#S5SH). Nothing here
    // asks the host a second time, and nothing asks this machine about the host's path.
    if (const relay::remote::FileRef ref = relay::remote::parseFileUrl(path); ref.ok) {
        if (ref.directory) setRemoteRoot(path);
        else if (onOpenFile) onOpenFile(path);
        return;
    }
    if (QFileInfo(path).isDir()) setRoot(path);
    else if (onOpenFile) onOpenFile(path);
}

void FileExplorer::openFromKeyboard(const QModelIndex &index, Qt::KeyboardModifiers mods) {
    if (!index.isValid()) return;
    const QString path = pathAt(index);
    const bool remote = relay::remote::parseFileUrl(path).ok;
    // A folder only ever navigates, and a remote file opens editable already, so Ctrl+Enter on
    // one is plain Enter. Shift+Enter is the chord that leaves the app (card #SEJ2), and the
    // desktop cannot open a file that is not on this machine, so a remote row ignores it.
    if (mods == Qt::NoModifier || isDirAt(index) || (remote && mods == Qt::ControlModifier)) {
        activate(path);
        return;
    }
    if (mods == Qt::ControlModifier) {
        if (onEditFile) onEditFile(path);
        else if (onOpenFile) onOpenFile(path);
        return;
    }
    if (mods == Qt::ShiftModifier && !remote) {
        if (onOpenExternal) onOpenExternal(path);
        else QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

void FileExplorer::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    // Narrow explorers keep the name column readable by hiding size and date.
    m_view->setColumnHidden(1, width() < 460);
    m_view->setColumnHidden(3, width() < 460);
    // Elide after the layout has given the label its final width.
    QTimer::singleShot(0, this, [this] { updateHeader(); });
}

void FileExplorer::setHeaderRightInset(int pixels) {
    if (!m_header || m_header->contentsMargins().right() == pixels) return;
    m_header->setContentsMargins(0, 0, pixels, 0);
    updateHeader();
}

void FileExplorer::updateHeader() {
    const QString shown = isRemote() ? title() : tildePath(m_root);
    m_path->setText(m_path->fontMetrics().elidedText(shown, Qt::ElideLeft, std::max(40, m_path->contentsRect().width() - 8)));
    m_path->setToolTip(isRemote() ? QStringLiteral("%1 · on %2").arg(m_remoteDir, m_remoteHost) : m_root);
    m_up->setEnabled(isRemote() ? m_remoteDir != QStringLiteral("/") : !QDir(m_root).isRoot());
}

QString FileExplorer::title() const {
    return isRemote() ? relay::remote::displayName(m_remoteHost, m_remoteDir) : m_root;
}

QList<FileMenuItem> FileExplorer::menuFor(const QString &path) const {
    if (isRemote()) {
        // Read only, on purpose: the entries that would act on this machine (open externally,
        // show the folder, navigate the terminal, make it the agent's workspace) mean nothing
        // for someone else's disk, and creating or deleting there is not this pane's job yet.
        QList<FileMenuItem> items;
        const relay::remote::FileRef ref = relay::remote::parseFileUrl(path);
        if (ref.ok)
            items << FileMenuItem{QStringLiteral("openInternal"),
                                  ref.directory ? QStringLiteral("Open folder here") : QStringLiteral("Open in a Relay pane"), true};
        items << FileMenuItem{QStringLiteral("copyPath"), QStringLiteral("Copy path on %1").arg(m_remoteHost),
                              ref.ok || !m_remoteDir.isEmpty()};
        return items;
    }
    FileMenuHost host;
    host.canNavigateTerminal = bool(onNavigateHere);
    host.canPreview = bool(onOpenInPreview);
    host.canSetWorkspace = bool(onSetWorkspace);
    const QFileInfo info(path.isEmpty() ? m_root : path);
    FileMenuTarget target = FileMenuTarget::None;
    if (!path.isEmpty()) target = info.isDir() ? FileMenuTarget::Folder : FileMenuTarget::File;
    // Creating, renaming and deleting all need the containing folder to be writable.
    const QString parent = target == FileMenuTarget::None ? m_root : info.absolutePath();
    host.writable = QFileInfo(parent).isWritable();
    return explorerMenu(target, host);
}

void FileExplorer::showMenu(const QPoint &viewportPos) {
    const QModelIndex index = m_view->indexAt(viewportPos);
    const QString path = index.isValid() ? pathAt(index) : QString();
    if (index.isValid() && !m_view->selectionModel()->isSelected(index))
        m_view->setCurrentIndex(index);
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    for (const FileMenuItem &item : menuFor(path)) {
        if (item.isSeparator()) { menu->addSeparator(); continue; }
        QAction *action = menu->addAction(item.label);
        action->setEnabled(item.enabled);
        const QString id = item.id;
        connect(action, &QAction::triggered, this, [this, id, path] { runMenuAction(id, path); });
    }
    menu->popup(m_view->viewport()->mapToGlobal(viewportPos));
}

void FileExplorer::runMenuAction(const QString &id, const QString &path) {
    const QString target = path.isEmpty() ? m_root : path;
    if (isRemote()) {
        const relay::remote::FileRef ref = relay::remote::parseFileUrl(target);
        if (id == QLatin1String("openInternal")) activate(target);
        else if (id == QLatin1String("copyPath")) QApplication::clipboard()->setText(ref.ok ? ref.path : m_remoteDir);
        return;
    }
    if (id == QLatin1String("openInternal")) {
        // Relay's own pane: a folder becomes this explorer's root, a file opens in a preview.
        if (QFileInfo(target).isDir()) setRoot(target);
        else if (onOpenInPreview) onOpenInPreview(target);
    } else if (id == QLatin1String("openExternal")) {
        // The slow path (the menu) teaches the fast one (Shift+Enter, card #SEJ2). The explorer has
        // no toast, so the hint goes in its notice line and clears itself unless replaced meanwhile.
        if (relay::ShortcutHints::instance().shouldShow(QStringLiteral("files.openExternalFromMenu"))) {
            const QString hint = relay::ShortcutHints::nextTime(QStringLiteral("Shift+Enter"),
                                                                QStringLiteral("open with the desktop's app"));
            m_notice->setText(hint);
            m_notice->show();
            QTimer::singleShot(8000, m_notice, [label = m_notice, hint] {
                if (label->text() == hint) label->hide();
            });
        }
        if (onOpenExternal) onOpenExternal(target);
        else QDesktopServices::openUrl(QUrl::fromLocalFile(target));
    } else if (id == QLatin1String("navigate")) {
        const QString directory = QFileInfo(target).isDir() ? target : QFileInfo(target).absolutePath();
        if (onNavigateHere) onNavigateHere(directory);
    } else if (id == QLatin1String("copyPath")) {
        QApplication::clipboard()->setText(target);
    } else if (id == QLatin1String("copyRelativePath")) {
        QApplication::clipboard()->setText(QDir(m_root).relativeFilePath(target));
    } else if (id == QLatin1String("openFolder")) {
        openContainingFolder(target);
    } else if (id == QLatin1String("newFile")) {
        createEntry(false);
    } else if (id == QLatin1String("newFolder")) {
        createEntry(true);
    } else if (id == QLatin1String("rename")) {
        renameEntry(target);
    } else if (id == QLatin1String("delete")) {
        deleteEntry(target);
    } else if (id == QLatin1String("workspace")) {
        const QString directory = QFileInfo(target).isDir() ? target : QFileInfo(target).absolutePath();
        if (onSetWorkspace) onSetWorkspace(directory);
    }
}

void FileExplorer::createEntry(bool folder) {
    bool ok = false;
    const QString name = QInputDialog::getText(this, folder ? QStringLiteral("New folder") : QStringLiteral("New file"),
                                               QStringLiteral("Name:"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    if (name.contains(QLatin1Char('/'))) {
        QMessageBox::warning(this, QStringLiteral("Relay"), QStringLiteral("A name cannot contain “/”."));
        return;
    }
    const QString path = QDir(m_root).filePath(name);
    if (QFileInfo::exists(path)) {
        QMessageBox::warning(this, QStringLiteral("Relay"), QStringLiteral("“%1” already exists.").arg(name));
        return;
    }
    bool made = false;
    if (folder) {
        made = QDir(m_root).mkdir(name);
    } else {
        QFile file(path);
        made = file.open(QIODevice::WriteOnly);
        if (made) file.close();
    }
    if (!made) {
        QMessageBox::warning(this, QStringLiteral("Relay"), QStringLiteral("Could not create “%1”.").arg(name));
        return;
    }
    select(path);
}

void FileExplorer::renameEntry(const QString &path) {
    const QFileInfo info(path);
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Rename"), QStringLiteral("New name:"),
                                               QLineEdit::Normal, info.fileName(), &ok).trimmed();
    if (!ok || name.isEmpty() || name == info.fileName()) return;
    if (name.contains(QLatin1Char('/'))) {
        QMessageBox::warning(this, QStringLiteral("Relay"), QStringLiteral("A name cannot contain “/”."));
        return;
    }
    const QString destination = QDir(info.absolutePath()).filePath(name);
    if (QFileInfo::exists(destination)) {
        QMessageBox::warning(this, QStringLiteral("Relay"), QStringLiteral("“%1” already exists.").arg(name));
        return;
    }
    if (!QFile::rename(path, destination)) {
        QMessageBox::warning(this, QStringLiteral("Relay"), QStringLiteral("Could not rename “%1”.").arg(info.fileName()));
        return;
    }
    select(destination);
}

void FileExplorer::deleteEntry(const QString &path) {
    const QFileInfo info(path);
    const bool directory = info.isDir();
    const QString question = directory
        ? QStringLiteral("Delete the folder “%1” and everything in it?\n\nThis cannot be undone.").arg(info.fileName())
        : QStringLiteral("Delete “%1”?\n\nThis cannot be undone.").arg(info.fileName());
    if (QMessageBox::warning(this, QStringLiteral("Delete"), question, QMessageBox::Cancel | QMessageBox::Yes,
                             QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    const bool removed = directory ? QDir(path).removeRecursively() : QFile::remove(path);
    if (!removed)
        QMessageBox::warning(this, QStringLiteral("Relay"), QStringLiteral("Could not delete “%1”.").arg(info.fileName()));
}

void FileExplorer::select(const QString &path) {
    const QModelIndex index = indexOf(path);
    if (index.isValid()) m_view->setCurrentIndex(index);
}

// ----- one folder on another machine (#S5SH) ----------------------------------------------------

QAbstractItemModel *FileExplorer::activeModel() const {
    return isRemote() && m_remoteModel ? static_cast<QAbstractItemModel *>(m_remoteModel)
                                       : static_cast<QAbstractItemModel *>(m_model);
}

QString FileExplorer::pathAt(const QModelIndex &index) const {
    if (!index.isValid()) return {};
    if (!isRemote()) return m_model->filePath(index);
    const QModelIndex first = index.sibling(index.row(), 0);
    const QString name = first.data(Qt::DisplayRole).toString();
    const QString path = relay::remote::childPath(m_remoteDir, name);
    return first.data(Qt::UserRole).toBool() ? relay::remote::folderUrl(m_remoteHost, path)
                                             : relay::remote::fileUrl(m_remoteHost, path);
}

bool FileExplorer::isDirAt(const QModelIndex &index) const {
    if (!index.isValid()) return false;
    if (!isRemote()) return m_model->isDir(index);
    return index.sibling(index.row(), 0).data(Qt::UserRole).toBool();
}

QString FileExplorer::nameAt(const QModelIndex &index) const {
    if (!index.isValid()) return {};
    if (!isRemote()) return m_model->fileName(index);
    return index.sibling(index.row(), 0).data(Qt::DisplayRole).toString();
}

QModelIndex FileExplorer::indexOf(const QString &path) const {
    if (!isRemote()) return m_model->index(path);
    const QModelIndex rootIndex = m_view->rootIndex();
    for (int row = 0; row < m_remoteModel->rowCount(rootIndex); ++row) {
        const QModelIndex index = m_remoteModel->index(row, 0, rootIndex);
        if (pathAt(index) == path) return index;
    }
    return {};
}

void FileExplorer::setRemoteRoot(const QString &url) {
    const relay::remote::FileRef ref = relay::remote::parseFileUrl(url);
    if (!ref.ok) return;
    const QString folder = ref.directory ? ref.path : relay::remote::parentPath(ref.path);
    const QString canonical = relay::remote::folderUrl(ref.host, folder);
    const bool changed = canonical != m_root;
    m_remoteHost = ref.host;
    m_remoteDir = folder;
    m_root = canonical;
    if (!m_remoteModel) {
        m_remoteModel = new QStandardItemModel(this);
        m_remoteModel->setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Size"),
                                                  QStringLiteral("Type"), QStringLiteral("Date Modified")});
    }
    if (m_view->model() != m_remoteModel) {
        m_view->setModel(m_remoteModel);
        // The rows arrive sorted (folders first, then names): letting the view re-sort them by
        // the text of a column would mix folders in among the files.
        m_view->setSortingEnabled(false);
        m_view->header()->setStretchLastSection(false);
        m_view->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_view->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        m_view->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        m_view->setColumnHidden(2, true);
    }
    m_remoteModel->removeRows(0, m_remoteModel->rowCount());
    m_view->setCurrentIndex(QModelIndex());
    if (!m_filter->text().isEmpty()) { const QSignalBlocker block(m_filter); m_filter->clear(); }
    updateHeader();
    m_notice->setText(QStringLiteral("Reading %1 from %2…").arg(folder, ref.host));
    m_notice->show();
    if (!m_remoteList) {
        m_remoteList = new relay::remote::RemoteDir(this);
        m_remoteList->onListed = [this](const QString &path, const QVector<relay::remote::DirEntry> &entries, bool truncated) {
            if (path != m_remoteDir) return;   // an older listing, overtaken by a newer one
            fillRemoteRows(entries, truncated);
        };
        m_remoteList->onFailed = [this](const QString &message) { remoteFailed(message); };
    }
    m_remoteList->setHost(ref.host, relay::remote::loginControlPath(ref.host));
    m_remoteList->list(folder);
    if (changed && onDirectoryChanged) onDirectoryChanged(m_root);
}

void FileExplorer::fillRemoteRows(const QVector<relay::remote::DirEntry> &entries, bool truncated) {
    m_remoteModel->removeRows(0, m_remoteModel->rowCount());
    for (const relay::remote::DirEntry &entry : entries) {
        auto *name = new QStandardItem(entry.name);
        name->setData(entry.directory, Qt::UserRole);
        name->setEditable(false);
        auto *size = new QStandardItem(entry.directory || entry.size < 0 ? QString() : humanSize(entry.size));
        size->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        size->setEditable(false);
        auto *type = new QStandardItem(entry.directory ? QStringLiteral("Folder") : QStringLiteral("File"));
        type->setEditable(false);
        auto *when = new QStandardItem(entry.mtime > 0
                                           ? QLocale().toString(QDateTime::fromSecsSinceEpoch(entry.mtime), QLocale::ShortFormat)
                                           : QString());
        when->setEditable(false);
        m_remoteModel->appendRow({name, size, type, when});
    }
    hideUnmatchedFolders();
    const QStringList visible = visiblePaths();
    if (!visible.isEmpty()) m_view->setCurrentIndex(indexOf(visible.first()));
    if (truncated)
        m_notice->setText(QStringLiteral("The first %1 entries of %2 on %3.")
                              .arg(entries.size()).arg(m_remoteDir, m_remoteHost));
    else if (entries.isEmpty())
        m_notice->setText(QStringLiteral("%1 is empty on %2.").arg(m_remoteDir, m_remoteHost));
    m_notice->setVisible(truncated || entries.isEmpty());
}

void FileExplorer::remoteFailed(const QString &message) {
    m_remoteModel->removeRows(0, m_remoteModel->rowCount());
    m_notice->setText(message);
    m_notice->show();
}

bool FileExplorer::eventFilter(QObject *object, QEvent *event) {
    if (object == m_path) {
        // A click on the folder line asks the host to close this pane again (issue #D60R).
        if (event->type() == QEvent::MouseButtonRelease
            && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton
            && !m_path->hasSelectedText() && onCloseRequested) {
            onCloseRequested();
            return true;
        }
        return QWidget::eventFilter(object, event);
    }
    // Remember the modifiers of the press, so the single-click opener can tell a plain click
    // from Ctrl+click or Shift+click (which extend the selection instead).
    if (object == m_view->viewport() && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick))
        m_clickModifiers = static_cast<QMouseEvent *>(event)->modifiers();
    if (event->type() != QEvent::KeyPress) return QWidget::eventFilter(object, event);
    auto *key = static_cast<QKeyEvent *>(event);
    const auto mods = key->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
    if (object == m_view) {
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            if (mods == Qt::NoModifier || mods == Qt::ControlModifier || mods == Qt::ShiftModifier) {
                openFromKeyboard(m_view->currentIndex(), mods);
                return true;
            }
        }
        if ((key->key() == Qt::Key_Backspace && mods == Qt::NoModifier)
            || (key->key() == Qt::Key_Up && mods == Qt::AltModifier)
            || (key->key() == Qt::Key_Left && mods == Qt::NoModifier)) {
            goUp();
            return true;
        }
        if (key->key() == Qt::Key_Right && mods == Qt::NoModifier) {
            const QModelIndex selected = m_view->currentIndex();
            if (selected.isValid() && isDirAt(selected)) activate(pathAt(selected));
            return true;
        }
        // The keyboard route to the right-click menu.
        if ((key->key() == Qt::Key_Menu && mods == Qt::NoModifier) || (key->key() == Qt::Key_F10 && mods == Qt::ShiftModifier)) {
            const QModelIndex index = m_view->currentIndex();
            const QRect row = index.isValid() ? m_view->visualRect(index) : QRect();
            showMenu(row.isValid() ? row.center() : QPoint(8, 8));
            return true;
        }
        // Printable keys start filtering, like a file manager's type-ahead.
        if (mods == Qt::NoModifier || mods == Qt::ShiftModifier) {
            const QString text = key->text();
            if (!text.isEmpty() && text.at(0).isPrint() && !text.at(0).isSpace()) {
                m_filter->setFocus(Qt::OtherFocusReason);
                m_filter->setText(m_filter->text() + text);
                return true;
            }
        }
    } else if (object == m_filter) {
        if (key->key() == Qt::Key_Down || key->key() == Qt::Key_PageDown) {
            m_view->setFocus(Qt::OtherFocusReason);
            // Always start at the first visible match: the previous selection may be filtered out.
            const QStringList visible = visiblePaths();
            if (!visible.isEmpty()) m_view->setCurrentIndex(m_model->index(visible.first()));
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            if (mods == Qt::NoModifier || mods == Qt::ControlModifier || mods == Qt::ShiftModifier) {
                openFromKeyboard(m_view->currentIndex(), mods);
                return true;
            }
        }
        if (key->key() == Qt::Key_Escape && !m_filter->text().isEmpty()) {
            m_filter->clear();
            return true;
        }
        if (key->key() == Qt::Key_Up && mods == Qt::AltModifier) { goUp(); return true; }
    }
    return QWidget::eventFilter(object, event);
}

// ----- highlighting a big file without freezing the pane (#MDSG) --------------------------------

#ifdef RELAY_HAVE_SYNTAX_HIGHLIGHTING
namespace {
// KSyntaxHighlighting colours the whole document the moment it is handed a definition, on the GUI
// thread: opening src/Pane.h (15,830 lines) froze the pane for 1,965 ms cold, 688 ms warm, and
// about 520 ms of the warm figure was the colouring alone. The text is what the reader is waiting
// for, so this shows it first and colours it afterwards, a few milliseconds at a time.
//
// Colouring has to run strictly top down — a line's state is the line before it, which is how a
// multi-line comment knows it is still inside one. So a block past the frontier is left alone:
// highlightBlock() returns without setting a format or a state, which also stops Qt's own
// "carry on while the state changes" from running past the frontier. Jumping to the end of a big
// file therefore shows it plain for a moment and then coloured, never coloured wrongly.
//
// What stops it: deleting the highlighter (what the panes do when they load another file) takes
// its timer with it. An edit is handled by QSyntaxHighlighter as usual for the blocks already
// coloured; lines added or removed above the frontier move it, because it counts blocks.
class LazyHighlighter final : public KSyntaxHighlighting::SyntaxHighlighter {
public:
    explicit LazyHighlighter(QTextDocument *document)
        : KSyntaxHighlighting::SyntaxHighlighter(document), m_blocks(document->blockCount()) {
        m_timer.setSingleShot(true);
        m_timer.setInterval(1);
        QObject::connect(&m_timer, &QTimer::timeout, this, [this] { slice(kSliceMs); });
        m_edits = QObject::connect(document, &QTextDocument::contentsChange, this,
                                   [this](int from, int, int) { edited(from); });
    }

    // ~QSyntaxHighlighter clears the formats off every block, which is an edit of the document,
    // which would call back into a half-destroyed object (the timer is gone by then). So the
    // document is let go of first.
    ~LazyHighlighter() override {
        QObject::disconnect(m_edits);
        m_timer.stop();
    }

    // Colour what is on screen now and queue the rest. Called once the document holds the text:
    // a file of a few hundred lines is finished here, before the pane is painted at all, so
    // nothing small flickers from plain to coloured.
    void start() {
        m_frontier = 0;
        m_blocks = document() ? document()->blockCount() : 0;
        slice(kEagerMs, kEagerBlocks);
    }

    // How much of the document is coloured, and whether more is still to come. The panes offer
    // both so a test can wait for the end of it.
    int highlightedBlocks() const { return m_frontier; }
    bool busy() const { return document() && m_frontier < document()->blockCount(); }

protected:
    void highlightBlock(const QString &text) override {
        if (currentBlock().blockNumber() >= m_frontier) return;   // not reached yet: leave it plain
        KSyntaxHighlighting::SyntaxHighlighter::highlightBlock(text);
    }

private:
    static constexpr int kSliceMs = 4;        // one slice of idle time, small enough not to be felt
    static constexpr int kEagerMs = 15;       // and what the first, synchronous one may cost
    static constexpr int kEagerBlocks = 400;  // comfortably more than a screenful at any font size

    void slice(int budgetMs, int maxBlocks = -1) {
        QTextDocument *document = this->document();
        if (!document) return;
        QElapsedTimer spent;
        spent.start();
        int done = 0;
        // One edit block around the whole slice. Without it the view is told the document has
        // changed once per block and re-lays it out each time, which cost more than the
        // colouring itself: 204 ms of work for src/Pane.h became 556 ms.
        QTextCursor cursor(document);
        cursor.beginEditBlock();
        while (m_frontier < document->blockCount() && spent.elapsed() < budgetMs
               && (maxBlocks < 0 || done < maxBlocks)) {
            const QTextBlock block = document->findBlockByNumber(m_frontier);
            if (!block.isValid()) { m_frontier = document->blockCount(); break; }
            ++m_frontier;                       // inside the frontier before it is coloured
            ++done;
            rehighlightBlock(block);
        }
        // Nothing was inserted or taken out — only the colours on those blocks changed — so the
        // "the document changed" signals the end of the edit block would send are held back:
        // QSyntaxHighlighter would answer them by reformatting the whole slice a second time, and
        // no other reader of them has anything to do. The layout is told directly, not by signal,
        // so the view still repaints.
        { const QSignalBlocker quiet(document); cursor.endEditBlock(); }
        m_blocks = document->blockCount();
        if (busy()) m_timer.start();
    }

    // The frontier is a block number, so lines put in or taken out above it move it; the blocks
    // below it are Qt's own business (it reformats what changed). Typing in the tail of a file
    // that is not coloured yet simply waits for the frontier, like the rest of the tail.
    void edited(int from) {
        QTextDocument *document = this->document();
        if (!document) return;
        const int now = document->blockCount();
        if (document->findBlock(from).blockNumber() < m_frontier)
            m_frontier = qBound(0, m_frontier + (now - m_blocks), now);
        m_blocks = now;
        if (busy() && !m_timer.isActive()) m_timer.start();
    }

    QMetaObject::Connection m_edits;
    QTimer m_timer;
    int m_frontier = 0;   // blocks 0 … m_frontier-1 are coloured; the rest are plain
    int m_blocks = 0;     // the block count as of the last slice, to move the frontier by an edit
};
}  // namespace
#endif


// ----- ArtifactDock: the agent docked under a file editor (card #PBZ4) ---------------------------
//
// SettingsPane's helper foot, member for member (src/SettingsPane.cpp, "the helper agent"): the
// pane owns the *context* and the row; the window, the only place a `Pane` can be made, turns the
// context into a console through `onCreateConsole` on the first expand. The object names are that
// foot's, so the theme draws all of them alike.

namespace {
// "Review before apply" is remembered per workspace — the tab's project, or the file's folder for
// a tab with none — under one QSettings group.
const QString kReviewGroup = QStringLiteral("artifact/reviewBeforeApply");
// Where task plugins are looked for (`FilePreview::setPluginSearch`); defined further down.
relay::agent::PluginSearch &pluginSearch();
}  // namespace

ArtifactDock::ArtifactDock(QWidget *parent) : QWidget(parent), m_context(new relay::agent::ArtifactContext) {
    setObjectName(QStringLiteral("boardChatPanel"));
    setAttribute(Qt::WA_StyledBackground);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ---- collapsed: one button at the bottom right ------------------------------------------
    m_askRow = new QWidget(this);
    m_askRow->setObjectName(QStringLiteral("boardChatAskRow"));
    auto *askLine = new QHBoxLayout(m_askRow);
    askLine->setContentsMargins(10, 4, 10, 4);
    askLine->setSpacing(6);
    askLine->addStretch(1);
    m_ask = new QToolButton(m_askRow);
    m_ask->setObjectName(QStringLiteral("boardChatAsk"));
    m_ask->setCursor(Qt::PointingHandCursor);
    m_ask->setFocusPolicy(Qt::StrongFocus);
    askLine->addWidget(m_ask, 0);
    outer->addWidget(m_askRow);
    connect(m_ask, &QToolButton::clicked, this, [this] {
        // The mouse, not the key: Alt+Q comes in through `focusHelper()` and teaches nothing.
        if (onHelperHint && !m_askHintId.isEmpty()) onHelperHint();
        focusHelper();
    });

    // ---- expanded: the head row, the change list, then the console --------------------------
    m_body = new QWidget(this);
    m_body->setObjectName(QStringLiteral("boardChatBody"));
    auto *body = new QVBoxLayout(m_body);
    body->setContentsMargins(10, 6, 10, 6);
    body->setSpacing(4);
    auto *headRow = new QHBoxLayout;
    headRow->setSpacing(6);
    m_head = new QLabel(m_body);
    m_head->setObjectName(QStringLiteral("boardChatHead"));
    m_head->setTextFormat(Qt::PlainText);
    headRow->addWidget(m_head, 0);
    headRow->addStretch(1);
    m_changesToggle = new QToolButton(m_body);
    m_changesToggle->setObjectName(QStringLiteral("artifactChanges"));
    m_changesToggle->setCheckable(true);
    m_changesToggle->setToolTip(QStringLiteral("What the agent changed in this file, turn by turn"));
    m_changesToggle->setFocusPolicy(Qt::NoFocus);
    m_changesToggle->hide();
    headRow->addWidget(m_changesToggle, 0);
    m_reviewToggle = new QToolButton(m_body);
    m_reviewToggle->setObjectName(QStringLiteral("artifactReview"));
    m_reviewToggle->setText(QStringLiteral("Review before apply"));
    m_reviewToggle->setCheckable(true);
    m_reviewToggle->setToolTip(QStringLiteral("On: the agent's edits to this file wait on the file's agent bar for "
                                              "Apply. Off: they land at once as undo steps. Remembered per project."));
    m_reviewToggle->setFocusPolicy(Qt::NoFocus);
    headRow->addWidget(m_reviewToggle, 0);
    auto *foldButton = new QToolButton(m_body);
    foldButton->setObjectName(QStringLiteral("boardChatFold"));
    foldButton->setText(QStringLiteral("⌄"));
    foldButton->setToolTip(QStringLiteral("Fold the agent back to one row. The conversation is kept — it opens where "
                                    "you left it."));
    foldButton->setCursor(Qt::PointingHandCursor);
    foldButton->setFocusPolicy(Qt::NoFocus);
    headRow->addWidget(foldButton, 0);
    body->addLayout(headRow);
    m_changesList = new QListWidget(m_body);
    m_changesList->setObjectName(QStringLiteral("artifactChangeList"));
    m_changesList->setFocusPolicy(Qt::NoFocus);
    m_changesList->setUniformItemSizes(true);
    m_changesList->hide();
    body->addWidget(m_changesList, 0);
    m_body->setVisible(false);
    outer->addWidget(m_body);

    connect(foldButton, &QToolButton::clicked, this, [this] { fold(); });
    connect(m_changesToggle, &QToolButton::toggled, this, [this](bool open) {
        m_changesList->setVisible(open && !m_changes.isEmpty());
        redrawChanges();
    });
    connect(m_reviewToggle, &QToolButton::toggled, this, [this](bool on) { setReviewBeforeApply(on); });
    connect(m_changesList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const int line = item->data(Qt::UserRole).toInt();
        if (line > 0 && onGoToLine) onGoToLine(line);
    });
    updateRow();
}

ArtifactDock::~ArtifactDock() {
    // The console's wrapper clears the context's callback when the console goes, so the console
    // must go first (~BoardView's rule): children die in ~QWidget, after this body.
    delete m_body;
    m_body = nullptr;
    delete m_context;
}

void ArtifactDock::setHelperWorkspace(const QString &workspace) {
    if (workspace == m_workspace) return;
    m_workspace = workspace;
    const bool on = QSettings().value(reviewKey(), false).toBool();
    const QSignalBlocker block(m_reviewToggle);
    m_reviewToggle->setChecked(on);
    if (on != m_review) {
        m_review = on;
        if (onReviewChanged) onReviewChanged();
    }
}

QString ArtifactDock::reviewKey() const {
    QString where = m_workspace;
    if (where.isEmpty() && !m_context->file().startsWith(QStringLiteral("ssh://")))
        where = QFileInfo(m_context->file()).absolutePath();
    // QSettings keys may not hold '/' as data: the path is hashed into one segment.
    const QByteArray digest = QCryptographicHash::hash(where.toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
    return kReviewGroup + QLatin1Char('/') + QString::fromLatin1(digest);
}

void ArtifactDock::setReviewBeforeApply(bool on) {
    QSettings().setValue(reviewKey(), on);
    {
        const QSignalBlocker block(m_reviewToggle);
        m_reviewToggle->setChecked(on);
    }
    if (on == m_review) return;
    m_review = on;
    if (onReviewChanged) onReviewChanged();
}

void ArtifactDock::setHelperShortcut(const QString &hintId, const QString &keys) {
    m_askHintId = hintId;
    m_askKeys = keys;
    updateRow();
}

void ArtifactDock::refreshTitle() {
    if (m_head) m_head->setText(m_context->title());
    // The switch follows the file when the tab has no project (the folder is the key then).
    const bool on = QSettings().value(reviewKey(), false).toBool();
    if (on != m_review) {
        m_review = on;
        const QSignalBlocker block(m_reviewToggle);
        m_reviewToggle->setChecked(on);
        if (onReviewChanged) onReviewChanged();
    }
    updateRow();
}

void ArtifactDock::focusHelper() {
    if (!onCreateConsole) return;
    ensureConsole();
    if (m_collapsed) {
        m_collapsed = false;
        applyCollapsed();
    }
    if (m_console.focusComposer) m_console.focusComposer();
}

void ArtifactDock::helperDraft(const QString &text) {
    focusHelper();
    if (m_console.draftInComposer) m_console.draftInComposer(text);
}

void ArtifactDock::fold() {
    if (m_collapsed) return;
    m_collapsed = true;
    applyCollapsed();
}

void ArtifactDock::ensureConsole() {
    if (m_console || !onCreateConsole || !m_body) return;
    // The plugin is looked up again now. A pane opens its file in its constructor, before the
    // window has told the file panes where plugins live, so the first file of a session was
    // resolved against no search at all and its console had no plugin: no plugin actions, no
    // `/` commands, no `plugin` on the wire. Everything that shows the plugin is drawn by the
    // console, and the console is built here, after the window has wired the pane.
    if (!m_context->file().isEmpty())
        m_context->setPlugin(relay::agent::pluginForFile(m_context->file(), pluginSearch()));
    // The context sends a plugin command's prompt through the console it now has.
    QPointer<ArtifactDock> self(this);
    m_context->sendPrompt = [self](const QString &prompt) {
        if (!self) return;
        self->focusHelper();
        if (self->m_console.submitPrompt) self->m_console.submitPrompt(prompt);
    };
    m_console = onCreateConsole(m_context, m_body);
    if (!m_console) return;
    if (auto *body = qobject_cast<QVBoxLayout *>(m_body->layout())) body->addWidget(m_console.widget, 1);
    m_console.widget->show();
    m_context->changed();   // the action row and the `/` popup now have somewhere to send to
    updateHeight();
}

void ArtifactDock::applyCollapsed() {
    if (m_askRow) m_askRow->setVisible(m_collapsed);
    if (m_collapsed && m_ask && m_body && m_body->isAncestorOf(QApplication::focusWidget()))
        m_ask->setFocus(Qt::OtherFocusReason);
    if (m_body) m_body->setVisible(!m_collapsed);
    if (m_console.setCollapsed) m_console.setCollapsed(m_collapsed);
    if (!m_collapsed) updateHeight();
}

void ArtifactDock::updateRow() {
    if (m_ask) {
        m_ask->setText(m_askKeys.isEmpty() ? QStringLiteral("✦ Agent") : QStringLiteral("✦ Agent (%1)").arg(m_askKeys));
        const QString about = m_context->file().isEmpty() ? QStringLiteral("this file")
                                                          : QFileInfo(m_context->file()).fileName();
        m_ask->setToolTip(m_askKeys.isEmpty() ? QStringLiteral("Ask the agent about %1.").arg(about)
                                              : QStringLiteral("Ask the agent about %1 (%2).").arg(about, m_askKeys));
    }
    if (m_head) m_head->setText(m_context->title());
}

// At most ~45 % of the host, and never so little that the transcript is a slot: the editor above
// is what the person came for.
void ArtifactDock::updateHeight() {
    if (!m_body) return;
    const QWidget *host = parentWidget();
    const int line = QFontMetrics(font()).lineSpacing();
    const int total = host ? host->height() : 600;
    const int cap = std::max(10 * line, total * 9 / 20);
    m_body->setMaximumHeight(cap);
    m_body->setMinimumHeight(std::min(cap, 14 * line));
}

void ArtifactDock::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    // An ask row that does nothing is worse than none: with no window to build a console (a
    // test, a library), there is no row.
    if (m_askRow) m_askRow->setVisible(m_collapsed && bool(onCreateConsole));
}

void ArtifactDock::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
}

void ArtifactDock::setChanges(const QList<Change> &changes) {
    m_changes = changes;
    redrawChanges();
}

void ArtifactDock::nameTurn(const QString &turnId, const QString &label) {
    if (turnId.isEmpty() || label.trimmed().isEmpty()) return;
    QString text = label.simplified();
    if (text.size() > 60) text = text.left(59) + QChar(0x2026);
    m_turnNames.insert(turnId, text);
    redrawChanges();
}

void ArtifactDock::redrawChanges() {
    if (!m_changesToggle || !m_changesList) return;
    QStringList turns;
    for (const Change &change : std::as_const(m_changes))
        if (!turns.contains(change.turnId)) turns << change.turnId;
    m_changesToggle->setVisible(!m_changes.isEmpty());
    m_changesToggle->setText(QStringLiteral("%1 %2 · %3 %4")
                                 .arg(m_changesToggle->isChecked() ? QStringLiteral("▾") : QStringLiteral("▸"))
                                 .arg(m_changes.size())
                                 .arg(m_changes.size() == 1 ? QStringLiteral("change") : QStringLiteral("changes"))
                                 .arg(turns.size() == 1 ? QStringLiteral("1 turn") : QStringLiteral("%1 turns").arg(turns.size())));
    m_changesList->clear();
    // Newest turn first; under each, its changes in the order they landed.
    for (qsizetype t = turns.size() - 1; t >= 0; --t) {
        const QString &turn = turns.at(t);
        const QString name = m_turnNames.value(turn);
        auto *head = new QListWidgetItem(
            name.isEmpty() ? (turn.isEmpty() ? QStringLiteral("Turn") : QStringLiteral("Turn %1").arg(turn.left(8)))
                           : QStringLiteral("Turn · “%1”").arg(name),
            m_changesList);
        QFont bold = head->font();
        bold.setBold(true);
        head->setFont(bold);
        head->setFlags(Qt::ItemIsEnabled);
        for (const Change &change : std::as_const(m_changes)) {
            if (change.turnId != turn) continue;
            auto *row = new QListWidgetItem(QStringLiteral("    ") + change.text, m_changesList);
            row->setData(Qt::UserRole, change.line);
            row->setToolTip(QStringLiteral("Go to line %1").arg(change.line));
        }
    }
    const int rows = std::min(8, m_changesList->count());
    m_changesList->setFixedHeight(rows * std::max(18, m_changesList->sizeHintForRow(0)) + 6);
    m_changesList->setVisible(m_changesToggle->isChecked() && !m_changes.isEmpty());
}

QStringList ArtifactDock::changeListRows() const {
    QStringList rows;
    for (int i = 0; m_changesList && i < m_changesList->count(); ++i) rows << m_changesList->item(i)->text().trimmed();
    return rows;
}

// ----- FilePreview -------------------------------------------------------------------------------

struct FilePreview::Private {
#ifdef RELAY_HAVE_SYNTAX_HIGHLIGHTING
    KSyntaxHighlighting::Repository repository;
    LazyHighlighter *highlighter = nullptr;
#endif
#ifdef RELAY_HAVE_QTPDF
    QPdfDocument *pdf = nullptr;
    QPdfView *pdfView = nullptr;
    // A PDF from a host has no file here to be read from; QPdfDocument reads a QIODevice for as
    // long as it is open, so the bytes and the buffer over them live as long as the pane (#S5SH).
    QByteArray pdfBytes;
    QBuffer *pdfBuffer = nullptr;
#endif
    QPixmap pixmap;

    // ----- the open file changing on disk (#F8R7) --------------------------------------------
    relay::merge::Snapshot base;       // what the buffer was loaded from / last merged or saved against
    relay::merge::Snapshot disk;       // the disk's version a waiting conflict is about
    relay::merge::Revision seen;       // the stat at the last look, so a folder event about a sibling costs nothing
    QFileSystemWatcher *watcher = nullptr;
    QTimer *settle = nullptr;          // coalesces a burst of watcher events into one look
    bool fileEvent = false;            // the file itself signalled since the last look: rehash whatever the stat says
    bool deleted = false;
    FilePreview::ConflictMode conflict = FilePreview::ConflictMode::None;
    QWidget *conflictBar = nullptr;
    QLabel *conflictText = nullptr;
    QToolButton *conflictMerge = nullptr, *conflictMine = nullptr, *conflictDisk = nullptr;

    // ----- a file on a host (#F8R7, task cb) ---------------------------------------------------
    // What the one RemoteFile is doing, so its answer goes to the right place: a fetch to show a
    // file, a stat poll, a fetch to reconcile a change the poll saw, a fetch after a save the host
    // refused, or a save.
    enum class RemoteOp { None, Open, Check, Refresh, SaveCheck, Save };
    RemoteOp remoteOp = RemoteOp::None;
    QTimer *remotePoll = nullptr;
    QString remoteSaving;              // the text a save to the host is writing
    bool remoteGone = false;           // the host says the file is gone: the next save puts it back
    // A patch to a clean buffer on a host is answered when its save lands (protocol §35).
    std::function<void(const QJsonObject &)> patchReply;
    QJsonObject patchResult;
    // Answer that patch now: saved, or not and why.
    void answerPatch(bool saved, const QString &error = QString(), const QString &sha = QString()) {
        auto reply = std::exchange(patchReply, nullptr);
        if (!reply) return;
        QJsonObject result = patchResult;
        result.insert(QStringLiteral("saved"), saved);
        if (!sha.isEmpty()) result.insert(QStringLiteral("sha256"), sha);
        if (!error.isEmpty()) result.insert(QStringLiteral("save_error"), error);
        reply(result);
    }

    // ----- agent edits to the buffer (#F8R7, task v5) -----------------------------------------
    struct AgentStep {
        QString before, after;         // the buffer either side of the step
        int undoSteps = 0;             // the document's undo depth right after it
    };
    QVector<FilePreview::AgentChange> agentChanges;
    QVector<AgentStep> agentSteps;     // one per change, same order
    QWidget *agentBar = nullptr;
    QLabel *agentText = nullptr;
    QToolButton *agentUndo = nullptr, *agentList = nullptr, *agentDismiss = nullptr;
    QTimer *agentFade = nullptr;       // takes the changed lines' highlight off again

    // ----- the docked agent (card #PBZ4) ---------------------------------------------------------
    ArtifactDock *dock = nullptr;
    QVector<QJsonObject> held;         // patches waiting for Apply while "Review before apply" is on
    QToolButton *agentApply = nullptr, *agentDiscard = nullptr;
};

namespace {
relay::agent::PluginSearch &pluginSearch() {
    static relay::agent::PluginSearch search;
    return search;
}
}  // namespace

namespace {
// ----- which files are open, for the agents' workers (#F8R7, protocol §35) ----------------------
QList<FilePreview *> &livePreviewList() {
    static QList<FilePreview *> list;
    return list;
}
struct BufferListener {
    QPointer<QObject> context;
    std::function<void()> changed;
};
QVector<BufferListener> &bufferListeners() {
    static QVector<BufferListener> listeners;
    return listeners;
}
bool buffersNotifyPending = false;

QString sha256Hex(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

// "disk" for a local file, the host's name for a remote one: where the other version lives.
QString sourceOf(const FilePreview *preview) {
    return preview->isRemote() ? preview->remoteHost() : QStringLiteral("disk");
}
}  // namespace

FilePreview::FilePreview(QWidget *parent) : QWidget(parent), d(new Private) {
    setObjectName(QStringLiteral("filePreview"));
    setAttribute(Qt::WA_StyledBackground);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->setSpacing(6);

    auto *header = new QHBoxLayout;
    m_header = header;
    header->setSpacing(4);
    m_title = new QLabel;
    m_title->setObjectName(QStringLiteral("filePreviewTitle"));
    m_title->setTextFormat(Qt::PlainText);
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_title->setMinimumWidth(40);
    m_mode = headerButton(QString(), QString());
    m_mode->setObjectName(QStringLiteral("filePreviewMode"));
    m_reload = headerButton(QStringLiteral("⟳"), QStringLiteral("Reload"));
    m_external = headerButton(QStringLiteral("↗"), QStringLiteral("Open with the default application"));
    // A file on another machine (#S5SH). The chip is next to the title, before every button, so
    // "this is not your disk" is read before anything is done to the file — and it stays there
    // while the file is edited, which is the whole point of it.
    m_hostChip = new QLabel;
    m_hostChip->setObjectName(QStringLiteral("filePreviewHost"));
    m_hostChip->setTextFormat(Qt::PlainText);
    m_hostChip->hide();
    m_save = headerButton(QStringLiteral("Save"), QStringLiteral("Save to the host (Ctrl+S)"));
    m_save->setObjectName(QStringLiteral("filePreviewSave"));
    m_save->hide();
    // The read-only preview's way into editing (card #SEJ2); updateEditButton() decides when it
    // shows. Its tooltip is set for a local file in updateEditButton() — "to the host" is a
    // remote save's wording.
    m_edit = headerButton(QStringLiteral("✎ Edit"), QStringLiteral("Edit this file here"));
    m_edit->setObjectName(QStringLiteral("filePreviewEdit"));
    m_edit->hide();
    // The hover names the key (files.toggleWrap) through the Keymap, so a rebind keeps the
    // tooltip honest; an unbound action names no key.
    {
        const QString wrapKeys = Keymap::instance().shortcutText(QStringLiteral("files.toggleWrap"));
        m_wrap = headerButton(QStringLiteral("Word wrap"),
                              wrapKeys.isEmpty() ? QStringLiteral("Wrap long lines to the pane width")
                                                 : QStringLiteral("Wrap long lines to the pane width (%1)").arg(wrapKeys));
    }
    m_wrap->setObjectName(QStringLiteral("filePreviewWrap"));
    m_wrap->setAccessibleName(QStringLiteral("Word wrap"));
    m_wrap->setCheckable(true);
    m_wrap->hide();
    // The slow path (clicking Word wrap) teaches the fast one (Alt+Z, files.toggleWrap).
    connect(m_wrap, &QToolButton::clicked, this, [this] {
        if (relay::ShortcutHints::instance().shouldShow(QStringLiteral("files.wrapToggle")))
            setNotice(relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(QStringLiteral("files.toggleWrap")),
                                                     QStringLiteral("toggle word wrap")));
    });
    header->addWidget(m_title, 1);
    header->addWidget(m_hostChip);
    header->addWidget(m_edit);
    header->addWidget(m_save);
    header->addWidget(m_mode);
    header->addWidget(m_wrap);
    header->addWidget(m_reload);
    header->addWidget(m_external);
    layout->addLayout(header);

    // The conflict bar (#F8R7): an external change the pane would not merge into unsaved edits
    // on its own, or a save that found the disk had moved. It stays until one of its buttons is
    // pressed; the buffer is not touched until then.
    d->conflictBar = new QWidget;
    d->conflictBar->setObjectName(QStringLiteral("filePreviewConflict"));
    {
        auto *bar = new QHBoxLayout(d->conflictBar);
        bar->setContentsMargins(0, 0, 0, 0);
        bar->setSpacing(4);
        d->conflictText = new QLabel;
        d->conflictText->setObjectName(QStringLiteral("filePreviewConflictText"));
        d->conflictText->setWordWrap(true);
        d->conflictText->setTextFormat(Qt::PlainText);
        d->conflictMerge = headerButton(QString(), QString());
        d->conflictMerge->setObjectName(QStringLiteral("filePreviewConflictMerge"));
        d->conflictMine = headerButton(QString(), QString());
        d->conflictMine->setObjectName(QStringLiteral("filePreviewConflictMine"));
        d->conflictDisk = headerButton(QString(), QString());
        d->conflictDisk->setObjectName(QStringLiteral("filePreviewConflictDisk"));
        bar->addWidget(d->conflictText, 1);
        bar->addWidget(d->conflictMerge);
        bar->addWidget(d->conflictMine);
        bar->addWidget(d->conflictDisk);
        connect(d->conflictMerge, &QToolButton::clicked, this, [this] { resolveConflict(Resolution::Merge); });
        connect(d->conflictMine, &QToolButton::clicked, this, [this] { resolveConflict(Resolution::KeepMine); });
        connect(d->conflictDisk, &QToolButton::clicked, this, [this] { resolveConflict(Resolution::TakeDisk); });
    }
    d->conflictBar->hide();
    layout->addWidget(d->conflictBar);

    // What an agent changed in this buffer (#F8R7, task v5): the newest change named, Undo for
    // it, and the list of every change since the file was opened. It stays until dismissed; the
    // changed lines themselves are highlighted for a few seconds.
    d->agentBar = new QWidget;
    d->agentBar->setObjectName(QStringLiteral("filePreviewAgent"));
    {
        auto *bar = new QHBoxLayout(d->agentBar);
        bar->setContentsMargins(0, 0, 0, 0);
        bar->setSpacing(4);
        d->agentText = new QLabel;
        d->agentText->setObjectName(QStringLiteral("filePreviewAgentText"));
        d->agentText->setTextFormat(Qt::PlainText);
        d->agentText->setWordWrap(true);
        d->agentUndo = headerButton(QStringLiteral("Undo"), QStringLiteral("Take the agent's latest change back out"));
        d->agentUndo->setObjectName(QStringLiteral("filePreviewAgentUndo"));
        d->agentList = headerButton(QStringLiteral("Changes"), QStringLiteral("Every change an agent made to this file here"));
        d->agentList->setObjectName(QStringLiteral("filePreviewAgentChanges"));
        d->agentList->setPopupMode(QToolButton::InstantPopup);
        auto *menu = new QMenu(d->agentList);
        d->agentList->setMenu(menu);
        connect(menu, &QMenu::aboutToShow, this, [this, menu] {
            menu->clear();
            for (qsizetype k = d->agentChanges.size() - 1; k >= 0; --k) {
                const AgentChange &change = d->agentChanges[k];
                QStringList parts{QLocale().toString(change.at.time(), QLocale::ShortFormat), change.intent,
                                  change.firstLine == change.lastLine ? QStringLiteral("line %1").arg(change.firstLine)
                                                                      : QStringLiteral("lines %1–%2").arg(change.firstLine).arg(change.lastLine)};
                if (!change.model.isEmpty()) parts << change.model;
                if (change.applied == QStringLiteral("merged")) parts << QStringLiteral("merged");
                if (!change.saved) parts << QStringLiteral("unsaved");
                const int line = change.firstLine;
                menu->addAction(parts.join(QStringLiteral(" · ")), this, [this, line] { goToLine(line); });
            }
        });
        d->agentDismiss = headerButton(QStringLiteral("×"), QStringLiteral("Hide"));
        d->agentDismiss->setObjectName(QStringLiteral("filePreviewAgentDismiss"));
        // Review before apply (#PBZ4, D1): what the agent proposed, waiting for the person.
        d->agentApply = headerButton(QStringLiteral("Apply"), QStringLiteral("Put the agent's proposed change in the buffer, as an undo step"));
        d->agentApply->setObjectName(QStringLiteral("filePreviewAgentApply"));
        d->agentDiscard = headerButton(QStringLiteral("Discard"), QStringLiteral("Drop the agent's proposed change"));
        d->agentDiscard->setObjectName(QStringLiteral("filePreviewAgentDiscard"));
        bar->addWidget(d->agentText, 1);
        bar->addWidget(d->agentApply);
        bar->addWidget(d->agentDiscard);
        bar->addWidget(d->agentUndo);
        bar->addWidget(d->agentList);
        bar->addWidget(d->agentDismiss);
        connect(d->agentApply, &QToolButton::clicked, this, [this] { applyHeldAgentChanges(); });
        connect(d->agentDiscard, &QToolButton::clicked, this, [this] { discardHeldAgentChanges(); });
        connect(d->agentUndo, &QToolButton::clicked, this, [this] { undoAgentChange(); });
        connect(d->agentDismiss, &QToolButton::clicked, this, [this] { d->agentBar->hide(); });
    }
    d->agentBar->hide();
    layout->addWidget(d->agentBar);
    d->agentFade = new QTimer(this);
    d->agentFade->setSingleShot(true);
    d->agentFade->setInterval(4000);
    connect(d->agentFade, &QTimer::timeout, this, [this] { m_textView->setExtraSelections({}); });

    // A file on a host has no watcher: while it is on screen the pane asks the host for its stat
    // every few seconds, and fetches it only when that moved (#F8R7, task cb).
    d->remotePoll = new QTimer(this);
    d->remotePoll->setInterval(5000);
    connect(d->remotePoll, &QTimer::timeout, this, [this] { if (isVisible()) checkRemote(); });
    livePreviewList().append(this);

    // The watcher watches the file and its folder: renaming a temporary over the file (how most
    // editors, git and QSaveFile write) takes the old inode and its watch with it, and only the
    // folder sees the new one arrive. Events come in bursts, and a look is a read and a hash, so
    // the first event starts a short timer and the rest ride on it.
    d->watcher = new QFileSystemWatcher(this);
    d->settle = new QTimer(this);
    d->settle->setSingleShot(true);
    d->settle->setInterval(80);
    connect(d->settle, &QTimer::timeout, this, [this] { checkDisk(); });
    connect(d->watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &) {
        d->fileEvent = true;
        if (!d->settle->isActive()) d->settle->start();
    });
    connect(d->watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString &) {
        if (!d->settle->isActive()) d->settle->start();
    });

    m_noticeLabel = new QLabel;
    m_noticeLabel->setObjectName(QStringLiteral("filePreviewNotice"));
    m_noticeLabel->setWordWrap(true);
    m_noticeLabel->hide();
    layout->addWidget(m_noticeLabel);

    m_stack = new QStackedWidget;
    layout->addWidget(m_stack, 1);

    m_textView = new QPlainTextEdit;
    m_textView->setObjectName(QStringLiteral("filePreviewText"));
    m_textView->setReadOnly(true);
    m_textView->setLineWrapMode(QPlainTextEdit::NoWrap);
    connect(m_wrap, &QToolButton::toggled, this, [this](bool wrap) {
        m_textView->setLineWrapMode(wrap ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    });
    m_textView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    // Only while it is a preview: setEditable() turns this pane into an editor, and
    // copyOnSelectText() copies nothing from a widget the user is typing in.
    relay::installCopyOnSelect(m_textView);
    m_stack->addWidget(m_textView);

    m_markdownView = new QTextBrowser;
    m_markdownView->setObjectName(QStringLiteral("filePreviewMarkdown"));
    // A link in the rendered Markdown used to be followed by the QTextBrowser itself, which
    // replaced the file in place — the header still named the old one, and Reload and ↗ still
    // acted on it, with no way back (issue S1JP, owner 2026-09-18: "if you click on another link
    // there, it should open a new pane"). The browser now never navigates; followLink() decides.
    m_markdownView->setOpenLinks(false);
    m_markdownView->setOpenExternalLinks(false);
    connect(m_markdownView, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) { followLink(url); });
    relay::installCopyOnSelect(m_markdownView);
    m_stack->addWidget(m_markdownView);

    m_docxView = new DocxEditor;
    m_docxView->setObjectName(QStringLiteral("filePreviewDocx"));
    m_stack->addWidget(m_docxView);
    m_docxView->onDirtyChanged = [this](bool) {
        updateTitleText();
        if (onTitleChanged) onTitleChanged(title());
    };

    m_imageArea = new QScrollArea;
    m_imageArea->setObjectName(QStringLiteral("filePreviewImageArea"));
    m_imageArea->setAlignment(Qt::AlignCenter);
    m_image = new QLabel;
    m_image->setObjectName(QStringLiteral("filePreviewImage"));
    m_image->setAlignment(Qt::AlignCenter);
    m_imageArea->setWidget(m_image);
    m_imageArea->setWidgetResizable(false);
    m_stack->addWidget(m_imageArea);

    m_pdfPage = new QWidget;
#ifdef RELAY_HAVE_QTPDF
    {
        auto *pdfLayout = new QVBoxLayout(m_pdfPage);
        pdfLayout->setContentsMargins(0, 0, 0, 0);
        d->pdf = new QPdfDocument(this);
        d->pdfView = new QPdfView;
        d->pdfView->setDocument(d->pdf);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        d->pdfView->setPageMode(QPdfView::PageMode::MultiPage);
        d->pdfView->setZoomMode(QPdfView::ZoomMode::FitToWidth);
#else
        d->pdfView->setPageMode(QPdfView::MultiPage);
        d->pdfView->setZoomMode(QPdfView::FitToWidth);
#endif
        pdfLayout->addWidget(d->pdfView);
    }
#endif
    m_stack->addWidget(m_pdfPage);

    m_infoPage = new QWidget;
    {
        auto *infoLayout = new QVBoxLayout(m_infoPage);
        infoLayout->setContentsMargins(16, 16, 16, 16);
        m_info = new QLabel;
        m_info->setObjectName(QStringLiteral("filePreviewInfo"));
        m_info->setTextFormat(Qt::PlainText);
        m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
        relay::installCopyOnSelect(m_info);
        m_info->setWordWrap(true);
        auto *open = new QPushButton(QStringLiteral("Open externally"));
        open->setObjectName(QStringLiteral("filePreviewOpenExternal"));
        connect(open, &QPushButton::clicked, this, [this] {
            if (!m_path.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(m_path));
        });
        infoLayout->addWidget(m_info);
        infoLayout->addWidget(open, 0, Qt::AlignLeft);
        infoLayout->addStretch(1);
    }
    m_stack->addWidget(m_infoPage);

    connect(m_reload, &QToolButton::clicked, this, [this] { reload(); });
    connect(m_external, &QToolButton::clicked, this, [this] {
        if (!m_path.isEmpty() && !isRemote()) QDesktopServices::openUrl(QUrl::fromLocalFile(m_path));
    });
    // Editing a remote file follows the editable pane this project already has (PlanEditor): the
    // Save button, Ctrl+S, and a ● in front of the title while there are unsaved edits.
    connect(m_save, &QToolButton::clicked, this, [this] {
        // The slow path teaches the fast one (RELAY.md, "Shortcut hints"). The registry keeps the
        // count and the cooldown, so it is shown a few times and then never again.
        m_teachSaveShortcut = relay::ShortcutHints::instance().shouldShow(QStringLiteral("files.remoteSave"));
        save();
    });
    // The slow path (clicking ✎) teaches the fast one (Ctrl+Enter in the explorer, card #SEJ2).
    connect(m_edit, &QToolButton::clicked, this, [this] {
        if (relay::ShortcutHints::instance().shouldShow(QStringLiteral("files.editFromExplorer")))
            setNotice(relay::ShortcutHints::nextTime(QStringLiteral("Ctrl+Enter"),
                                                     QStringLiteral("edit a file straight from the explorer")));
        startEditing();
    });
    auto *saveShortcut = new QShortcut(QKeySequence::Save, this);
    saveShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(saveShortcut, &QShortcut::activated, this, [this] { save(); });
    connect(m_textView->document(), &QTextDocument::modificationChanged, this, [this](bool) {
        updateTitleText();
        if (onTitleChanged) onTitleChanged(title());   // the ● reaches the tab as well as the header
        notifyOpenBuffers();                           // `dirty` is in the agents' list (#F8R7)
    });
    connect(m_mode, &QToolButton::clicked, this, [this] {
        if (m_kind == Kind::Markdown) {
            m_markdownSource = !m_markdownSource;
            // Coming back from the source of an editable document, the render is rebuilt from
            // what is in the editor now — otherwise it would still show the version that was
            // fetched (#S5SH; a local .md is read only, so nothing changes there).
            if (!m_markdownSource && m_editable) m_markdownView->setMarkdown(m_textView->toPlainText());
            m_stack->setCurrentWidget(m_markdownSource ? static_cast<QWidget *>(m_textView) : m_markdownView);
        } else if (m_kind == Kind::Image) {
            m_imageActualSize = !m_imageActualSize;
            updateImage();
        }
        updateModeButton();
    });
    m_mode->hide();
    m_reload->setEnabled(false);
    m_external->setEnabled(false);

    // Right-clicking anywhere in the preview offers the same three entries the explorer offers
    // (issue V9V1). The viewers keep their own Copy / Select all, which showMenu() appends under
    // them, so taking the menu over loses nothing. It has to be an event filter on the viewports
    // rather than a context-menu policy: QTextEdit's text control answers ContextMenu from inside
    // viewportEvent(), before the policy is looked at, and would put its own menu on top of this
    // one.
    for (QWidget *widget : {m_textView->viewport(), m_markdownView->viewport(),
                            static_cast<QWidget *>(m_image), static_cast<QWidget *>(m_info)})
        widget->installEventFilter(this);

    // The agent docked at the foot (card #PBZ4). The context asks the editor for the cursor, the
    // selection and the dirty flag fresh before every ask, so nothing here pushes them.
    d->dock = new ArtifactDock(this);
    d->dock->hide();
    {
        relay::agent::ArtifactContext *context = d->dock->context();
        QPointer<FilePreview> self(this);
        context->state = [self] {
            relay::agent::ArtifactState state;
            if (!self) return state;
            const QTextCursor cursor = self->m_textView->textCursor();
            state.line = cursor.blockNumber() + 1;
            state.column = cursor.positionInBlock() + 1;
            if (cursor.hasSelection()) {
                QTextCursor first(self->m_textView->document()), last(self->m_textView->document());
                first.setPosition(cursor.selectionStart());
                last.setPosition(cursor.selectionEnd());
                state.firstLine = first.blockNumber() + 1;
                state.lastLine = last.blockNumber() + 1;
                state.selection = cursor.selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
            }
            state.dirty = self->isDirty();
            state.editable = self->isEditable();
            state.mode = self->m_kind == Kind::Markdown
                             ? (self->showingSource() ? QStringLiteral("markdown source") : QStringLiteral("markdown, rendered"))
                             : QStringLiteral("text");
            return state;
        };
        context->save = [self] { return self && self->save(); };
        context->revert = [self] {
            if (!self || !self->isDirty()) return false;
            return self->reload();
        };
        context->goToLine = [self](int line) { if (self) self->goToLine(line); };
        context->onTurnFinished = [self](const relay::agent::TurnRecord &record) {
            if (!self) return;
            self->d->dock->nameTurn(record.turnId, record.prompt);
        };
        d->dock->onGoToLine = [self](int line) { if (self) self->goToLine(line); };
        d->dock->onReviewChanged = [self] { if (self) self->showAgentBar(); };
        connect(m_textView->document(), &QTextDocument::modificationChanged, this, [this](bool) {
            d->dock->context()->changed();   // Save and Revert light up with something to save
        });
    }
    layout->addWidget(d->dock, 0);
}

FilePreview::~FilePreview() {
    livePreviewList().removeAll(this);
    d->answerPatch(false, QStringLiteral("The pane was closed before the save to the host finished."));
    notifyOpenBuffers();
    // Both are children and would outlive `d` by a moment; neither may call back into it.
    delete d->watcher;
    delete d->settle;
    delete d->remotePoll;
    delete d->agentFade;
    // The dock and its console go while `d` is still here: its context's callbacks read it.
    delete d->dock;
    d->dock = nullptr;
    if (m_remote) m_remote->cancel();
    delete d;
}

QString FilePreview::title() const {
    // A remote file is named by its host and its whole path: "filly:/etc/nginx/nginx.conf". A bare
    // "nginx.conf" in a tab would be indistinguishable from this machine's, which is the one thing
    // this pane must never be (#S5SH).
    const QString name = isRemote() ? relay::remote::displayName(m_remoteHost, m_remotePath)
                       : m_path.isEmpty() ? QStringLiteral("Preview")
                                          : QFileInfo(m_path).fileName();
    return (isDirty() ? QStringLiteral("● ") : QString()) + name;
}

bool FilePreview::isDirty() const {
    if (m_kind == Kind::Docx) return m_docxView->isDirty();
    return m_editable && m_textView->document()->isModified();
}

QString FilePreview::text() const {
    if (m_kind == Kind::Docx) return m_docxView->plainText();
    return m_textView->toPlainText();
}

bool FilePreview::syntaxHighlightingBuiltIn() {
#ifdef RELAY_HAVE_SYNTAX_HIGHLIGHTING
    return true;
#else
    return false;
#endif
}

int FilePreview::highlightedBlocks() const {
#ifdef RELAY_HAVE_SYNTAX_HIGHLIGHTING
    return d->highlighter ? d->highlighter->highlightedBlocks() : 0;
#else
    return 0;
#endif
}

bool FilePreview::highlighting() const {
#ifdef RELAY_HAVE_SYNTAX_HIGHLIGHTING
    return d->highlighter && d->highlighter->busy();
#else
    return false;
#endif
}

bool FilePreview::open(const QString &path) {
    if (relay::remote::isFileUrl(path)) return openRemote(path);
    // Opening (or reloading) another file over this one is the local path that can lose an edit,
    // so it asks first — the same guard openRemote() has (#SEJ2).
    if (isDirty()
        && QMessageBox::warning(this, QStringLiteral("Reload"),
                                QStringLiteral("Throw away your unsaved edits to %1?").arg(QFileInfo(m_path).fileName()),
                                QMessageBox::Cancel | QMessageBox::Discard, QMessageBox::Cancel) != QMessageBox::Discard)
        return false;
    const QFileInfo info(path);
    if (path.isEmpty() || !info.exists() || !info.isFile() || !info.isReadable()) return false;
    setEditable(false);
    hideConflict();
    d->deleted = false;
    d->base = relay::merge::Snapshot();
    m_remoteHost.clear();
    m_remotePath.clear();
    m_hostChip->hide();
    m_pendingLine = 0;
    if (m_remote) m_remote->cancel();
    if (m_reconnect) m_reconnect->stop();
    d->remotePoll->stop();
    d->remoteOp = Private::RemoteOp::None;
    d->remoteGone = false;
    d->answerPatch(false, QStringLiteral("Another file was opened in the pane before the save finished."));
    // The agent's changes were to the text that was here; a different file starts a new list.
    if (info.absoluteFilePath() != m_path) {
        d->agentChanges.clear();
        d->agentSteps.clear();
        d->held.clear();
        d->agentBar->hide();
    }
    if (auto *button = findChild<QPushButton *>(QStringLiteral("filePreviewOpenExternal"))) button->show();
    const QString absolute = info.absoluteFilePath();
    const bool samePath = absolute == m_path;
    const int scroll = samePath ? m_textView->verticalScrollBar()->value() : 0;
    m_path = absolute;
    m_kind = Kind::None;
    m_markdownSource = false;
    setNotice(QString());
#ifdef RELAY_HAVE_SYNTAX_HIGHLIGHTING
    delete d->highlighter;
    d->highlighter = nullptr;
#endif
    m_textView->clear();

    const qint64 size = info.size();
    const QMimeDatabase mimes;
    const QMimeType mime = mimes.mimeTypeForFile(info);
    const QString suffix = info.suffix().toLower();
    const QByteArray mimeName = mime.name().toLatin1();

    if (suffix == QStringLiteral("docx")) {
        showDocx(absolute);
    } else if (suffix == QStringLiteral("md") || suffix == QStringLiteral("markdown") || mime.inherits(QStringLiteral("text/markdown"))) {
        showMarkdown(absolute, size);
    } else if (mime.inherits(QStringLiteral("application/pdf"))) {
        showPdf(absolute);
    } else if (QImageReader::supportedMimeTypes().contains(mimeName)) {
        if (!showImage(absolute, size)) showInfo(absolute, mime.name(), m_notice);
    } else if (mime.inherits(QStringLiteral("text/plain")) || mime.name() == QStringLiteral("application/x-zerosize")) {
        showText(absolute, size);
    } else {
        showInfo(absolute, mime.name());
    }
    if (samePath && m_kind == Kind::Text) m_textView->verticalScrollBar()->setValue(scroll);
    watchLocal();
    notifyOpenBuffers();
    updateArtifactDock();

    updateTitleText();
    m_title->setToolTip(absolute);
    m_reload->setEnabled(true);
    m_external->setEnabled(true);
    updateModeButton();
    updateEditButton();
    if (onTitleChanged) onTitleChanged(title());
    return true;
}

// ----- files on the host a terminal pane is logged into (#S5SH) --------------------------------

bool FilePreview::openRemote(const QString &url) {
    const relay::remote::FileRef ref = relay::remote::parseFileUrl(url);
    if (!ref.ok) return false;
    const QString name = relay::remote::displayName(ref.host, ref.path);
    // Reloading is the one thing here that can lose an edit, so it asks first — the ⟳ button and
    // a second click on the same path in the terminal both come through here.
    if (isDirty()
        && QMessageBox::warning(this, QStringLiteral("Reload"),
                                QStringLiteral("Throw away your unsaved edits to %1?").arg(name),
                                QMessageBox::Cancel | QMessageBox::Discard, QMessageBox::Cancel) != QMessageBox::Discard)
        return false;

    if (url != m_path) {
        d->agentChanges.clear();
        d->agentSteps.clear();
        d->held.clear();
        d->agentBar->hide();
    }
    m_path = url;
    m_remoteHost = ref.host;
    m_remotePath = ref.path;
    m_kind = Kind::None;
    m_markdownSource = false;
    setEditable(false);
    hideConflict();
    d->deleted = false;
    d->base = relay::merge::Snapshot();
    d->remotePoll->stop();
    d->remoteGone = false;
    d->answerPatch(false, QStringLiteral("The file was reloaded before the save finished."));
    notifyOpenBuffers();
    watchLocal();   // a remote path: stops watching whatever local file was here
    m_pendingLine = 0;
#ifdef RELAY_HAVE_SYNTAX_HIGHLIGHTING
    delete d->highlighter;
    d->highlighter = nullptr;
#endif
    m_textView->clear();
    m_textView->document()->setModified(false);
    m_stack->setCurrentWidget(m_textView);
    // The ⇄ the pane header's remote chip carries, so the two chips read as one mark.
    m_hostChip->setText(QStringLiteral("⇄ ") + ref.host);
    m_hostChip->setToolTip(QStringLiteral("This file is on %1 · Relay reads and writes it over the ssh connection this pane already has").arg(ref.host));
    m_hostChip->show();
    // The info page's "Open externally" would hand a remote path to this machine's applications.
    if (auto *button = findChild<QPushButton *>(QStringLiteral("filePreviewOpenExternal"))) button->hide();
    setNotice(QStringLiteral("Reading %1 from %2…").arg(QFileInfo(ref.path).fileName(), ref.host));
    m_title->setToolTip(name);
    updateTitleText();
    m_reload->setEnabled(true);
    m_external->setEnabled(false);   // the desktop cannot open a file that is not on this machine
    updateModeButton();
    if (onTitleChanged) onTitleChanged(title());

    if (!m_remote) {
        m_remote = new relay::remote::RemoteFile(this);
        m_remote->onFetched = [this](const QByteArray &content, const relay::remote::FileStat &) {
            // A fetch to reconcile with is not a fetch to show: the buffer is only ever merged into
            // (#F8R7, task cb).
            const Private::RemoteOp op = std::exchange(d->remoteOp, Private::RemoteOp::None);
            if (op == Private::RemoteOp::Refresh || op == Private::RemoteOp::SaveCheck) {
                remoteRefreshed(content, op == Private::RemoteOp::SaveCheck);
                return;
            }
            showRemoteContent(content);
        };
        m_remote->onFailed = [this](const QString &message, relay::remote::Conflict conflict, const relay::remote::FileStat &) {
            remoteFailed(message, int(conflict));
        };
        m_remote->onChecked = [this](const relay::remote::FileStat &now, const QString &error) {
            d->remoteOp = Private::RemoteOp::None;
            // A poll that could not reach the host says nothing; the next save will say why.
            if (!error.isEmpty()) { watchForReconnect(); return; }
            if (!now.ok) {
                if (!d->remoteGone) {
                    d->remoteGone = true;
                    setNotice(QStringLiteral("%1 is gone from %2 · your text is still here, and %3 writes it back.")
                                  .arg(QFileInfo(m_remotePath).fileName(), m_remoteHost,
                                       QKeySequence(QKeySequence::Save).toString(QKeySequence::NativeText)));
                }
                return;
            }
            const bool back = std::exchange(d->remoteGone, false);
            if (back) setNotice(QString());
            if (!back && now == m_remote->fetched()) return;
            d->remoteOp = Private::RemoteOp::Refresh;
            m_remote->fetch(m_remotePath);
        };
        m_remote->onSaved = [this](const relay::remote::FileStat &stat) {
            d->remoteOp = Private::RemoteOp::None;
            d->remoteGone = false;
            // What was written is the new base (#F8R7). Typing that went on during the save is
            // still unsaved, so the buffer is clean only if it is still exactly what was sent.
            relay::merge::Snapshot saved;
            saved.bytes = d->remoteSaving.toUtf8();
            saved.text = d->remoteSaving;
            saved.revision = relay::merge::revisionOf(saved.bytes);
            saved.revision.mtimeMs = stat.mtime > 0 ? stat.mtime * 1000 : -1;
            setBase(saved);
            if (m_textView->toPlainText() == d->remoteSaving) m_textView->document()->setModified(false);
            if (d->patchReply) {
                if (!d->agentChanges.isEmpty()) d->agentChanges.last().saved = true;
                showAgentBar();
                d->answerPatch(true, QString(), sha256Hex(saved.bytes));
            }
            QString said = QStringLiteral("Saved to %1 · %2").arg(m_remoteHost, QLocale().toString(QTime::currentTime(), QLocale::ShortFormat));
            if (m_teachSaveShortcut) {
                m_teachSaveShortcut = false;
                said += QStringLiteral(" · ") + relay::ShortcutHints::nextTime(
                            QKeySequence(QKeySequence::Save).toString(QKeySequence::NativeText),
                            QStringLiteral("save this file on %1").arg(m_remoteHost));
            }
            setNotice(said);
            updateTitleText();
            if (onTitleChanged) onTitleChanged(title());
            const QString shown = m_notice;
            QTimer::singleShot(6000, this, [this, shown] { if (m_notice == shown) setNotice(QString()); });
        };
    }
    m_remote->setHost(ref.host, relay::remote::loginControlPath(ref.host));
    d->remoteOp = Private::RemoteOp::Open;
    m_remote->fetch(ref.path);
    return true;
}

// The host's bytes, shown the way this pane would show the same file from this disk. The choice
// is made from the name *and* the bytes (QMimeDatabase::mimeTypeForFileNameAndData), which is more
// than a local open can do from the name and a peek at the file, and it costs nothing here because
// the bytes are already in hand.
void FilePreview::showRemoteContent(const QByteArray &content) {
    const QMimeDatabase mimes;
    const QString name = QFileInfo(m_remotePath).fileName();
    const QMimeType mime = mimes.mimeTypeForFileNameAndData(name, content);
    const QString suffix = QFileInfo(m_remotePath).suffix().toLower();
    const QByteArray mimeName = mime.name().toLatin1();
    setNotice(QString());
    if (suffix == QStringLiteral("docx")) {
        showRemoteInfo(mime.name(), QStringLiteral("DOCX rich editing is available for local files in this build."));
    } else if (suffix == QStringLiteral("md") || suffix == QStringLiteral("markdown") || mime.inherits(QStringLiteral("text/markdown"))) {
        showRemoteText(content, true);
    } else if (mime.inherits(QStringLiteral("application/pdf"))) {
        if (!showRemotePdf(content)) showRemoteInfo(mime.name(), m_notice);
    } else if (QImageReader::supportedMimeTypes().contains(mimeName)) {
        if (!showRemoteImage(content)) showRemoteInfo(mime.name(), m_notice);
    } else if (mime.inherits(QStringLiteral("text/plain")) || mime.name() == QStringLiteral("application/x-zerosize")) {
        showRemoteText(content, false);
    } else {
        showRemoteInfo(mime.name());
    }
    if (m_pendingLine > 0 && (m_kind == Kind::Text || m_kind == Kind::Markdown)) {
        const int line = m_pendingLine;
        m_pendingLine = 0;
        goToLine(line);
    }
    m_pendingLine = 0;
    updateModeButton();
    updateTitleText();
    updateArtifactDock();
    if (onTitleChanged) onTitleChanged(title());
}

void FilePreview::showRemoteText(const QByteArray &content, bool markdown) {
    // Binary is the text viewer's problem alone: an image and a PDF are binary and are meant to
    // be. A file that says it is text and is not gets the same answer a local one does.
    if (relay::remote::looksBinary(content)) {
        const QMimeDatabase mimes;
        showRemoteInfo(mimes.mimeTypeForFileNameAndData(QFileInfo(m_remotePath).fileName(), content).name(),
                       QStringLiteral("This file contains binary data."));
        return;
    }
    const QString text = QString::fromUtf8(content);
    m_textView->setPlainText(text);
    m_textView->document()->setModified(false);
    // The base, as a local file's is (#F8R7): these bytes and the host's stat, the text in the
    // form the buffer holds it. Every later change on the host is reconciled against it.
    {
        relay::merge::Snapshot base;
        base.bytes = content;
        base.text = m_textView->toPlainText();
        base.revision = relay::merge::revisionOf(content);
        const relay::remote::FileStat stat = m_remote ? m_remote->fetched() : relay::remote::FileStat();
        base.revision.mtimeMs = stat.mtime > 0 ? stat.mtime * 1000 : -1;
        setBase(base);
    }
    d->remotePoll->start();
#ifdef RELAY_HAVE_SYNTAX_HIGHLIGHTING
    delete d->highlighter;
    d->highlighter = nullptr;
    const KSyntaxHighlighting::Definition definition = d->repository.definitionForFileName(m_remotePath);
    // A host's file is not capped the way a local one is (readCapped), so this is where the limit
    // bites: past it the file is shown plain rather than colouring for minutes (#MDSG).
    if (definition.isValid() && content.size() <= kMaxHighlightBytes) {
        d->highlighter = new LazyHighlighter(m_textView->document());
        KSyntaxHighlighting::Theme theme = d->repository.theme(QStringLiteral("Breeze Dark"));
        if (!theme.isValid()) theme = d->repository.defaultTheme(KSyntaxHighlighting::Repository::DarkTheme);
        d->highlighter->setTheme(theme);
        d->highlighter->setDefinition(definition);
        d->highlighter->start();
    }
#endif
    // Markdown opens rendered, as a local .md does, and "Source (MD)" is where it is edited —
    // the same one button, and the render is rebuilt from the buffer each time it comes back.
    // Relative links and images cannot be resolved against this disk, so the base URL is left
    // empty and followLink() turns a relative link into the host's own file (#S5SH).
    if (markdown) {
        m_markdownView->setSearchPaths({});
        m_markdownView->document()->setBaseUrl(QUrl());
        m_markdownView->setMarkdown(text);
        m_kind = Kind::Markdown;
        m_markdownSource = false;
        m_stack->setCurrentWidget(m_markdownView);
    } else {
        m_kind = Kind::Text;
        m_stack->setCurrentWidget(m_textView);
    }
    setEditable(true);
    setNotice(QString());
}

bool FilePreview::showRemoteImage(const QByteArray &content) {
    QImage image;
    if (!image.loadFromData(content)) {
        setNotice(QStringLiteral("The image could not be decoded."));
        return false;
    }
    d->pixmap = QPixmap::fromImage(image);
    m_kind = Kind::Image;
    setEditable(false);
    m_stack->setCurrentWidget(m_imageArea);
    updateImage();
    return true;
}

bool FilePreview::showRemotePdf(const QByteArray &content) {
#ifdef RELAY_HAVE_QTPDF
    // QPdfDocument reads from a QIODevice for as long as the document is open, so the bytes and
    // the buffer over them are kept in the pane, not on the stack.
    d->pdf->close();
    d->pdfBytes = content;
    delete d->pdfBuffer;
    d->pdfBuffer = new QBuffer(&d->pdfBytes);
    d->pdfBuffer->open(QIODevice::ReadOnly);
    d->pdf->load(d->pdfBuffer);
    if (d->pdf->status() == QPdfDocument::Status::Ready || d->pdf->pageCount() > 0) {
        m_kind = Kind::Pdf;
        setEditable(false);
        m_stack->setCurrentWidget(m_pdfPage);
        return true;
    }
    setNotice(QStringLiteral("The PDF could not be opened."));
    return false;
#else
    Q_UNUSED(content);
    setNotice(QStringLiteral("PDF preview is not available in this build."));
    return false;
#endif
}

void FilePreview::showRemoteInfo(const QString &mime, const QString &message) {
    const relay::remote::FileStat stat = m_remote ? m_remote->fetched() : relay::remote::FileStat();
    QString text = QFileInfo(m_remotePath).fileName() + QStringLiteral("\n\n");
    text += QStringLiteral("Type: %1\nSize: %2\nModified: %3\nFolder: %4\nHost: %5")
                .arg(mime, stat.ok ? humanSize(stat.size) : QStringLiteral("unknown"),
                     stat.mtime > 0 ? QLocale().toString(QDateTime::fromSecsSinceEpoch(stat.mtime), QLocale::ShortFormat)
                                    : QStringLiteral("unknown"),
                     relay::remote::parentPath(m_remotePath), m_remoteHost);
    m_info->setText(text);
    if (!message.isEmpty()) setNotice(message);
    m_kind = Kind::Info;
    setEditable(false);
    m_stack->setCurrentWidget(m_infoPage);
}

void FilePreview::remoteFailed(const QString &message, int conflict) {
    const Private::RemoteOp op = std::exchange(d->remoteOp, Private::RemoteOp::None);
    // An agent's patch whose save this was: the text is in the buffer either way (#F8R7).
    d->answerPatch(false, message);
    if (conflict == int(relay::remote::Conflict::Changed)) {
        // The host's copy moved on between the fetch and the save, and nothing was written. Fetch
        // what is there now and put the same choice a local save puts (#F8R7, task cb): Merge,
        // Overwrite or Reload, on the bar — the buffer is not touched until one is picked.
        setNotice(message);
        d->remoteOp = Private::RemoteOp::SaveCheck;
        m_remote->fetch(m_remotePath);
        return;
    }
    if (conflict == int(relay::remote::Conflict::Vanished)) {
        // Gone from the host: only the buffer is left, and the next save writes it back.
        d->remoteGone = true;
        setNotice(QStringLiteral("%1 is gone from %2 · your text is still here, and %3 writes it back.")
                      .arg(QFileInfo(m_remotePath).fileName(), m_remoteHost,
                           QKeySequence(QKeySequence::Save).toString(QKeySequence::NativeText)));
        return;
    }
    if (m_editable || op == Private::RemoteOp::Refresh || op == Private::RemoteOp::SaveCheck || op == Private::RemoteOp::Save) {
        // A save, or a look at the host, that could not happen: the buffer stays exactly as it
        // is, and the pane says why.
        setNotice(message);
        watchForReconnect();
        return;
    }
    // A fetch that could not happen: there is nothing to show but the reason.
    m_info->setText(QStringLiteral("%1\n\n%2").arg(relay::remote::displayName(m_remoteHost, m_remotePath), message));
    m_kind = Kind::Info;
    m_stack->setCurrentWidget(m_infoPage);
    setNotice(message);
}

void FilePreview::setEditable(bool on) {
    m_editable = on;
    m_textView->setReadOnly(!on);
    m_save->setVisible(on);
    // "to the host" is a remote save's wording; a local save writes the file's own path.
    m_save->setToolTip(isRemote() ? QStringLiteral("Save to the host (Ctrl+S)")
                                  : QStringLiteral("Save (Ctrl+S)"));
    // The pane opens ready to type in, and Ctrl+S (a WidgetWithChildren shortcut) has a focused
    // widget to fire from: the host's focusInput() sets the focus on this widget, and the proxy
    // passes it to the editor. A read-only preview keeps the focus itself, as it always did.
    setFocusProxy(on ? (m_kind == Kind::Docx ? static_cast<QWidget *>(m_docxView) : static_cast<QWidget *>(m_textView)) : nullptr);
    updateEditButton();
}

void FilePreview::updateEditButton() {
    // The ✎ button is the read-only local preview's way into editing (card #SEJ2). A remote file
    // is editable the moment its bytes land, and an image, PDF or info page has nothing to edit.
    // A file shown only up to kMaxTextBytes is not offered: saving the head would cut off the tail.
    m_edit->setVisible(!isRemote() && !m_editable && !d->base.truncated && (m_kind == Kind::Text || m_kind == Kind::Markdown));
}

void FilePreview::startEditing() {
    if (m_editable || isRemote()) return;
    if (m_kind != Kind::Text && m_kind != Kind::Markdown) return;
    if (d->base.truncated) {
        setNotice(QStringLiteral("Only the first %1 of this file is shown, so it cannot be edited here.").arg(humanSize(kMaxTextBytes)));
        return;
    }
    // Markdown is edited as source, the same view the "Source (MD)" button shows.
    if (m_kind == Kind::Markdown && !m_markdownSource) {
        m_markdownSource = true;
        m_stack->setCurrentWidget(m_textView);
        updateModeButton();
    }
    setEditable(true);
    m_textView->setFocus(Qt::OtherFocusReason);
}

void FilePreview::watchForReconnect() {
    if (!isRemote() || (m_remote && m_remote->live())) return;
    if (!m_reconnect) {
        m_reconnect = new QTimer(this);
        m_reconnect->setInterval(2000);
        // The login can come back — the same host, a new control socket. When it does, the pane
        // says so rather than leaving the user to guess whether Ctrl+S would work now.
        connect(m_reconnect, &QTimer::timeout, this, [this] {
            if (!isRemote() || !m_remote) { m_reconnect->stop(); return; }
            if (!m_remote->live()) return;
            m_reconnect->stop();
            if (isDirty())
                setNotice(QStringLiteral("%1 is reachable again · %2 saves your edits.")
                              .arg(m_remoteHost, QKeySequence(QKeySequence::Save).toString(QKeySequence::NativeText)));
            else
                setNotice(QString());
        });
    }
    m_reconnect->start();
}

bool FilePreview::save() {
    if (!m_editable) return false;
    if (m_kind == Kind::Docx) {
        QString error;
        if (!m_docxView->save(&error)) { setNotice(error); return false; }
        setNotice(QStringLiteral("Saved · %1").arg(QLocale().toString(QTime::currentTime(), QLocale::ShortFormat)));
        rewatch();
        return true;
    }
    if (!m_textView->document()->isModified()) return true;
    if (!isRemote()) {
        // Never over a change nobody has seen (#F8R7): the disk is compared with the base first,
        // and a difference goes to the conflict bar as Merge / Overwrite / Reload. A file that is
        // gone is written back — there is nothing on disk to lose.
        if (d->conflict != ConflictMode::None) {
            showConflict(ConflictMode::Save);
            return false;
        }
        const relay::merge::Snapshot disk = relay::merge::readLocalFile(m_path, kMaxTextBytes);
        if (disk.revision.exists && !disk.revision.sameContent(d->base.revision)) {
            if (disk.text == m_textView->toPlainText()) {
                // Someone already wrote exactly this text.
                setBase(disk);
                m_textView->document()->setModified(false);
                setNotice(QStringLiteral("%1 on disk already has this text.").arg(QFileInfo(m_path).fileName()));
                return true;
            }
            d->disk = disk;
            showConflict(ConflictMode::Save);
            return false;
        }
        return writeBuffer();
    }
    if (!m_remote) return false;
    if (d->conflict != ConflictMode::None) {
        showConflict(ConflictMode::Save);
        return false;
    }
    if (m_remote->busy() && d->remoteOp == Private::RemoteOp::Save) {
        setNotice(QStringLiteral("Still saving to %1…").arg(m_remoteHost));
        return false;
    }
    if (m_remote->busy() && d->remoteOp == Private::RemoteOp::SaveCheck) {
        setNotice(QStringLiteral("Still checking what changed on %1…").arg(m_remoteHost));
        return false;
    }
    if (!m_remote->live()) {
        setNotice(QStringLiteral("The connection to %1 has ended · your edits are safe in this pane. Log in to %1 again "
                                 "in the terminal pane and press %2, or copy the text out.")
                      .arg(m_remoteHost, QKeySequence(QKeySequence::Save).toString(QKeySequence::NativeText)));
        watchForReconnect();
        return false;
    }
    saveRemote();
    return true;
}

void FilePreview::saveRemote(bool force) {
    if (!m_remote) return;
    // A stat poll or a refresh in flight gives way: the save compares the host with the base
    // itself, on the host (#F8R7).
    if (m_remote->busy() && (d->remoteOp == Private::RemoteOp::Check || d->remoteOp == Private::RemoteOp::Refresh))
        m_remote->cancel();
    setNotice(QStringLiteral("Saving to %1…").arg(m_remoteHost));
    d->remoteSaving = m_textView->toPlainText();
    d->remoteOp = Private::RemoteOp::Save;
    // A file the host no longer has is written back: there is nothing there to lose.
    m_remote->save(d->remoteSaving.toUtf8(), force || d->remoteGone);
}

bool FilePreview::writeOut() {
    if (!isRemote()) return writeBuffer();
    if (!m_remote || !m_remote->live()) {
        setNotice(QStringLiteral("The connection to %1 has ended · your edits are safe in this pane.").arg(m_remoteHost));
        watchForReconnect();
        return false;
    }
    saveRemote();
    return true;
}

void FilePreview::checkRemote() {
    if (!isRemote() || !m_remote || (m_kind != Kind::Text && m_kind != Kind::Markdown)) return;
    if (m_remote->busy() || !m_remote->live()) return;
    d->remoteOp = Private::RemoteOp::Check;
    m_remote->check();
}

void FilePreview::remoteRefreshed(const QByteArray &content, bool forSave) {
    // The host's bytes, after a poll saw its stat move or a save was refused because it had
    // (#F8R7, task cb): the same decisions a local file's watcher makes.
    relay::merge::Snapshot host;
    host.bytes = content;
    host.text = relay::merge::editorText(QString::fromUtf8(content));
    host.revision = relay::merge::revisionOf(content);
    const relay::remote::FileStat stat = m_remote->fetched();
    host.revision.mtimeMs = stat.mtime > 0 ? stat.mtime * 1000 : -1;
    if (relay::remote::looksBinary(content)) host.text += QChar(0);   // not mergeable, whatever it says
    if (forSave) {
        if (host.text == m_textView->toPlainText()) {
            setBase(host);
            m_textView->document()->setModified(false);
            setNotice(QStringLiteral("%1 on %2 already has this text.").arg(QFileInfo(m_remotePath).fileName(), m_remoteHost));
            return;
        }
        d->disk = host;
        showConflict(ConflictMode::Save);
        return;
    }
    if (host.revision.sameContent(d->base.revision)) {
        d->base.revision = host.revision;   // touched, not changed
        if (d->conflict == ConflictMode::Changed) hideConflict();
        return;
    }
    if (d->conflict != ConflictMode::None && host.revision.sameContent(d->disk.revision)) return;
    reconcileWith(host);
}

bool FilePreview::writeBuffer() {
    // A local save (card #SEJ2): atomically, so a crash or a full disk leaves the old file
    // whole. The buffer is UTF-8 because the read path decoded it as UTF-8.
    const QString text = m_textView->toPlainText();
    const QByteArray bytes = text.toUtf8();
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        setNotice(QStringLiteral("Could not save: %1").arg(file.errorString()));
        return false;
    }
    file.write(bytes);
    if (!file.commit()) {
        setNotice(QStringLiteral("Could not save: %1").arg(file.errorString()));
        return false;
    }
    // What was written is the new base; the watcher's event for this write then finds
    // nothing to do.
    relay::merge::Snapshot saved;
    saved.bytes = bytes;
    saved.text = text;
    saved.revision = relay::merge::revisionOf(bytes);
    const relay::merge::Revision stat = relay::merge::statLocalFile(m_path);
    saved.revision.mtimeMs = stat.mtimeMs;
    saved.revision.inode = stat.inode;
    setBase(saved);
    d->deleted = false;
    rewatch();
    m_textView->document()->setModified(false);
    QString said = QStringLiteral("Saved · %1").arg(QLocale().toString(QTime::currentTime(), QLocale::ShortFormat));
    if (m_teachSaveShortcut) {
        m_teachSaveShortcut = false;
        said += QStringLiteral(" · ") + relay::ShortcutHints::nextTime(
                    QKeySequence(QKeySequence::Save).toString(QKeySequence::NativeText),
                    QStringLiteral("save this file"));
    }
    setNotice(said);
    updateTitleText();
    if (onTitleChanged) onTitleChanged(title());
    const QString shown = m_notice;
    QTimer::singleShot(6000, this, [this, shown] { if (m_notice == shown) setNotice(QString()); });
    return true;
}

// ----- the open file changing on disk (#F8R7) ----------------------------------------------------
//
// The disk is the other editor. Every look goes: stat (cheap, and enough to ignore a folder event
// about a sibling), then read and hash, and the hash against the base decides. A clean buffer
// follows the disk; an edited one is reconciled three ways (relay::merge::reconcile) and only a
// conflict needs the user.

namespace {
// Text the buffer can hold and a merge can work on: not cut off at kMaxTextBytes, not binary.
bool mergeable(const relay::merge::Snapshot &snapshot) {
    return !snapshot.truncated && !snapshot.text.contains(QChar(0));
}
}  // namespace

const relay::merge::Snapshot &FilePreview::base() const {
    return d->base;
}

bool FilePreview::deletedOnDisk() const {
    return d->deleted;
}

bool FilePreview::hasConflict() const {
    return d->conflict != ConflictMode::None;
}

void FilePreview::watchLocal() {
    const QStringList watched = d->watcher->files() + d->watcher->directories();
    if (!watched.isEmpty()) d->watcher->removePaths(watched);
    d->settle->stop();
    d->fileEvent = false;
    d->seen = relay::merge::Revision();
    if (isRemote() || m_path.isEmpty()) return;
    d->watcher->addPath(QFileInfo(m_path).absolutePath());
    d->watcher->addPath(m_path);
    // The stat the text was read under, not the stat now: a write between the read and the watch
    // above sent no event, and this way it still differs and gets a look.
    const relay::merge::Revision now = relay::merge::statLocalFile(m_path);
    d->seen = d->base.revision.exists ? d->base.revision : now;
    if (!now.sameStat(d->seen)) d->settle->start();
}

void FilePreview::rewatch() {
    if (isRemote() || m_path.isEmpty()) return;
    const QString folder = QFileInfo(m_path).absolutePath();
    if (!d->watcher->directories().contains(folder)) d->watcher->addPath(folder);
    if (QFileInfo::exists(m_path) && !d->watcher->files().contains(m_path)) d->watcher->addPath(m_path);
}

void FilePreview::checkDisk() {
    if (isRemote()) { checkRemote(); return; }
    if (m_path.isEmpty() || m_kind == Kind::None) return;
    d->settle->stop();
    const bool fileEvent = std::exchange(d->fileEvent, false);
    rewatch();
    const relay::merge::Revision stat = relay::merge::statLocalFile(m_path);
    if (!stat.exists) {
        d->seen = stat;
        if (!d->deleted) {
            d->deleted = true;
            showDeleted();
        }
        return;
    }
    const bool recreated = std::exchange(d->deleted, false);
    if (!fileEvent && !recreated && stat.sameStat(d->seen)) return;
    d->seen = stat;
    const bool text = m_kind == Kind::Text || m_kind == Kind::Markdown;
    if (!text || d->base.truncated) {
        // An image, a PDF, an info page or a file too long to edit: never dirty, so it is simply
        // shown again (open() keeps a text file's scroll).
        if (!isDirty()) open(m_path);
        return;
    }
    const relay::merge::Snapshot disk = relay::merge::readLocalFile(m_path, kMaxTextBytes);
    if (!disk.revision.exists) {
        d->deleted = true;
        showDeleted();
        return;
    }
    if (recreated) setNotice(QString());   // the "deleted on disk" line
    if (disk.revision.sameContent(d->base.revision)) {
        d->base.revision = disk.revision;   // the same bytes, a newer stat
        if (d->conflict == ConflictMode::Changed) hideConflict();   // it went back to what was loaded
        return;
    }
    // The conflict on screen is about this very text: nothing new to say.
    if (d->conflict != ConflictMode::None && disk.revision.sameContent(d->disk.revision)) return;
    reconcileWith(disk);
}

void FilePreview::reconcileWith(const relay::merge::Snapshot &disk) {
    using relay::merge::Outcome;
    const QString name = QFileInfo(isRemote() ? m_remotePath : m_path).fileName();
    const QString where = sourceOf(this);
    QTextDocument *document = m_textView->document();
    if (!mergeable(disk)) {
        // Grew past the cap or turned binary: a clean preview shows it as open() would; an edited
        // buffer is kept, and only Keep mine or Take disk make sense.
        if (!isDirty()) { open(m_path); return; }
        d->disk = disk;
        showConflict(ConflictMode::Changed);
        return;
    }
    if (!isDirty()) {
        replaceBuffer(disk.text);
        setBase(disk);
        document->setModified(false);
        if (d->conflict == ConflictMode::Changed) hideConflict();
        return;
    }
    const relay::merge::Reconciliation r = relay::merge::reconcile(d->base.text, m_textView->toPlainText(), disk.text);
    switch (r.outcome) {
    case Outcome::Unchanged:
        // Different bytes, the same text in the buffer's form (line endings, say).
        setBase(disk);
        if (d->conflict == ConflictMode::Changed) hideConflict();
        return;
    case Outcome::Converged:
        setBase(disk);
        document->setModified(false);
        hideConflict();
        setNotice(QStringLiteral("%1 on %2 now has exactly your text.").arg(name, where));
        return;
    case Outcome::TakeDisk:
        replaceBuffer(r.text);
        setBase(disk);
        document->setModified(false);
        hideConflict();
        return;
    case Outcome::Merged:
        replaceBuffer(r.text);
        setBase(disk);
        hideConflict();
        setNotice(QStringLiteral("%1 changed on %2 · merged into your unsaved edits (Ctrl+Z takes the merge back out).").arg(name, where));
        return;
    case Outcome::Conflict:
        d->disk = disk;
        showConflict(ConflictMode::Changed, int(r.merge.conflicts.size()));
        return;
    }
}

void FilePreview::replaceBuffer(const QString &text) {
    const QVector<relay::merge::TextEdit> edits = relay::merge::editsBetween(m_textView->toPlainText(), text);
    QScrollBar *vertical = m_textView->verticalScrollBar(), *horizontal = m_textView->horizontalScrollBar();
    const int top = vertical->value(), left = horizontal->value();
    if (!edits.isEmpty()) {
        // One edit block: one undo step, and QTextDocument moves every cursor (the view's, with
        // its selection) past the edits before it, so the caret stays on the text it was on.
        QTextCursor cursor(m_textView->document());
        cursor.beginEditBlock();
        for (qsizetype k = edits.size() - 1; k >= 0; --k) {
            cursor.setPosition(edits[k].position);
            cursor.setPosition(edits[k].position + edits[k].removed, QTextCursor::KeepAnchor);
            cursor.insertText(edits[k].inserted);
        }
        cursor.endEditBlock();
    }
    vertical->setValue(top);
    horizontal->setValue(left);
    if (m_kind == Kind::Markdown) {
        QScrollBar *rendered = m_markdownView->verticalScrollBar();
        const int at = rendered->value();
        m_markdownView->setMarkdown(text);
        rendered->setValue(at);
    }
}

void FilePreview::setBase(const relay::merge::Snapshot &snapshot) {
    const bool moved = !snapshot.revision.sameContent(d->base.revision);
    d->base = snapshot;
    d->seen = snapshot.revision;
    if (moved) notifyOpenBuffers();   // `sha256` is in the agents' list (#F8R7)
}

void FilePreview::showConflict(ConflictMode mode, int overlaps) {
    d->conflict = mode;
    const QString name = QFileInfo(isRemote() ? m_remotePath : m_path).fileName();
    const QString where = sourceOf(this);
    if (mode == ConflictMode::Changed) {
        d->conflictText->setText(
            overlaps > 0 ? QStringLiteral("%1 changed on %4 where you have unsaved edits (%2 overlapping %3). "
                                          "Your text is untouched.")
                               .arg(name).arg(overlaps).arg(overlaps == 1 ? QStringLiteral("change") : QStringLiteral("changes"), where)
                         : QStringLiteral("%1 changed on %2 and cannot be merged with your unsaved edits here. "
                                          "Your text is untouched.").arg(name, where));
        d->conflictMerge->setText(QStringLiteral("Show merge"));
        d->conflictMerge->setToolTip(QStringLiteral("Put your text, the disk's and the version you loaded into the buffer "
                                                    "between conflict markers (one undo step)"));
        d->conflictMine->setText(QStringLiteral("Keep mine"));
        d->conflictMine->setToolTip(QStringLiteral("Keep your text; saving then writes it over the disk's version"));
        d->conflictDisk->setText(isRemote() ? QStringLiteral("Take %1's").arg(where) : QStringLiteral("Take disk"));
        d->conflictDisk->setToolTip(QStringLiteral("Replace your text with the %1's version (one undo step)").arg(where));
    } else {
        d->conflictText->setText(QStringLiteral("%1 changed on %2 since you loaded it, so saving now would overwrite "
                                                "that change.").arg(name, where));
        d->conflictMerge->setText(QStringLiteral("Merge"));
        d->conflictMerge->setToolTip(QStringLiteral("Merge the disk's changes into your text first, then save again"));
        d->conflictMine->setText(QStringLiteral("Overwrite"));
        d->conflictMine->setToolTip(QStringLiteral("Write your text over the disk's version"));
        d->conflictDisk->setText(QStringLiteral("Reload"));
        d->conflictDisk->setToolTip(QStringLiteral("Replace your text with the disk's version (one undo step)"));
    }
    d->conflictMerge->setEnabled(mergeable(d->disk));
    d->conflictBar->show();
}

void FilePreview::hideConflict() {
    d->conflict = ConflictMode::None;
    d->disk = relay::merge::Snapshot();
    d->conflictBar->hide();
}

void FilePreview::showDeleted() {
    hideConflict();   // there is no disk version left to choose
    const QString name = QFileInfo(m_path).fileName();
    setNotice(m_editable ? QStringLiteral("%1 was deleted on disk · your text is still here, and %2 writes it back.")
                               .arg(name, QKeySequence(QKeySequence::Save).toString(QKeySequence::NativeText))
                         : QStringLiteral("%1 was deleted on disk · this is the last version Relay read.").arg(name));
}

bool FilePreview::resolveConflict(Resolution how) {
    using relay::merge::Outcome;
    if (d->conflict == ConflictMode::None) return false;
    const ConflictMode mode = d->conflict;
    QTextDocument *document = m_textView->document();
    // A host's file is not read again here: the choice is about the version the host sent, and a
    // save that follows is checked against that version's stat by the host itself (#F8R7).
    const relay::merge::Snapshot disk = isRemote() ? d->disk : relay::merge::readLocalFile(m_path, kMaxTextBytes);
    if (!disk.revision.exists) {
        // Gone since the bar went up: only the buffer is left, and saving writes it back.
        d->deleted = true;
        showDeleted();
        return how == Resolution::KeepMine && mode == ConflictMode::Save ? writeOut() : true;
    }
    if (!disk.revision.sameContent(d->disk.revision)) {
        // The disk moved again while the bar was up, so the choice was about text that is no
        // longer there. Ask again about what is.
        hideConflict();
        if (mode == ConflictMode::Save) {
            d->disk = disk;
            showConflict(ConflictMode::Save);
        } else {
            reconcileWith(disk);
        }
        return false;
    }
    hideConflict();
    const QString name = QFileInfo(isRemote() ? m_remotePath : m_path).fileName();
    switch (how) {
    case Resolution::TakeDisk:
        if (!mergeable(disk)) {
            document->setModified(false);   // the user asked for the disk's copy
            open(m_path);
            return true;
        }
        replaceBuffer(disk.text);
        setBase(disk);
        document->setModified(false);
        setNotice(QStringLiteral("Took %1 from %2 · Ctrl+Z brings your text back.").arg(name, sourceOf(this)));
        return true;
    case Resolution::KeepMine:
        setBase(disk);
        if (mode == ConflictMode::Save) return writeOut();
        setNotice(QStringLiteral("Keeping your text · %1 writes it over the disk's version.")
                      .arg(QKeySequence(QKeySequence::Save).toString(QKeySequence::NativeText)));
        return true;
    case Resolution::Merge: {
        if (!mergeable(disk)) {
            d->disk = disk;
            showConflict(mode);
            return false;
        }
        const relay::merge::Reconciliation r = relay::merge::reconcile(d->base.text, m_textView->toPlainText(), disk.text);
        setBase(disk);
        switch (r.outcome) {
        case Outcome::Unchanged:
            break;
        case Outcome::Converged:
            document->setModified(false);
            break;
        case Outcome::TakeDisk:
            replaceBuffer(r.text);
            document->setModified(false);
            break;
        case Outcome::Merged:
            replaceBuffer(r.text);
            setNotice(QStringLiteral("Merged the disk's changes into your text · %1 saves it.")
                          .arg(QKeySequence(QKeySequence::Save).toString(QKeySequence::NativeText)));
            break;
        case Outcome::Conflict: {
            replaceBuffer(r.merge.text);
            const int count = int(r.merge.conflicts.size());
            setNotice(QStringLiteral("%1 marked between <<<<<<< mine and >>>>>>> disk, with the version you loaded "
                                     "after |||||||. Resolve, then save · Ctrl+Z takes the markers back out.")
                          .arg(count == 1 ? QStringLiteral("1 conflict") : QStringLiteral("%1 conflicts").arg(count)));
            goToLine(r.merge.conflicts.first().line + 1);
            break;
        }
        }
        return true;
    }
    }
    return false;
}

// ----- agent edits to the open buffer (#F8R7, task v5; protocol §35) ------------------------------
//
// An agent's worker is told which files are open in its pane's window (`open_buffers`), and its
// write_file / edit_file on one of them arrives here as a `buffer_request` instead of going to the
// disk. What was the agent's text worked out against? Its `base_sha256` says: the buffer as it is
// now (the agent read the unsaved buffer), the base this pane loaded or last saved, or — when the
// watcher has not caught up yet — the disk. The first is applied exactly; the other two are
// three-way merged with the buffer, so the user's unsaved edits elsewhere in the file stay. An
// overlap is refused with the lines as they are in the buffer, and so is a base nobody here knows,
// unless an edit_file's old_string still names exactly one place in the buffer.

bool FilePreview::patchable() const {
    if (m_kind != Kind::Text && m_kind != Kind::Markdown) return false;
    if (!d->base.revision.exists || d->base.truncated || d->base.text.contains(QChar(0))) return false;
    // A file whose line endings (CR LF, a lone CR) or spaces the editor would not keep as they
    // are is left to the disk write and the watcher, which already handles it.
    return QString::fromUtf8(d->base.bytes) == d->base.text;
}

QJsonObject FilePreview::openBufferEntry() const {
    if (!patchable()) return {};
    // A host's file by host and path as the agent's `host` tool names it — not the
    // percent-encoded URL the pane keeps.
    const QString path = isRemote() ? QStringLiteral("ssh://") + m_remoteHost + m_remotePath : m_path;
    return QJsonObject{{QStringLiteral("path"), path},
                       {QStringLiteral("sha256"), QString::fromLatin1(d->base.revision.hash.toHex())},
                       {QStringLiteral("dirty"), m_textView->document()->isModified()}};
}

QVector<FilePreview::AgentChange> FilePreview::agentChanges() const {
    return d->agentChanges;
}

void FilePreview::answerBufferRequest(const QJsonObject &request, const std::function<void(const QJsonObject &)> &reply) {
    using relay::merge::Outcome;
    QJsonObject out{{QStringLiteral("type"), QStringLiteral("buffer_result")},
                    {QStringLiteral("id"), request.value(QStringLiteral("id"))},
                    {QStringLiteral("path"), request.value(QStringLiteral("path"))}};
    const auto refuse = [&](const QString &error, const QString &message) {
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("error"), error);
        if (!message.isEmpty()) out.insert(QStringLiteral("message"), message);
        reply(out);
    };
    const QString op = request.value(QStringLiteral("op")).toString();
    const QString name = QFileInfo(isRemote() ? m_remotePath : m_path).fileName();
    // A local pane catches up with the disk first, so "the base" below is the disk's text unless
    // the user has unsaved edits the change could not be merged into.
    if (!isRemote() && op == QStringLiteral("patch")) checkDisk();
    if (!patchable()) return refuse(QStringLiteral("not_open"), QString());
    QTextDocument *document = m_textView->document();
    const QString buffer = m_textView->toPlainText();
    if (op == QStringLiteral("read")) {
        out.insert(QStringLiteral("ok"), true);
        out.insert(QStringLiteral("text"), buffer);
        out.insert(QStringLiteral("dirty"), document->isModified());
        out.insert(QStringLiteral("sha256"), QString::fromLatin1(d->base.revision.hash.toHex()));
        return reply(out);
    }
    if (op != QStringLiteral("patch")) return refuse(QStringLiteral("unsupported"), QStringLiteral("Unknown buffer_request op."));

    const QJsonValue contentValue = request.value(QStringLiteral("content"));
    const QString content = contentValue.toString();
    if (!contentValue.isString() || relay::merge::editorText(content) != content || content.contains(QChar(0)))
        return refuse(QStringLiteral("not_representable"),
                      QStringLiteral("The new text has line endings or characters the editor would change, so it goes to the disk instead."));

    // Review before apply (#PBZ4, owner decision D1): the patch waits on the agent bar for the
    // person and the agent's turn goes on, told plainly that nothing is in the file yet. Apply
    // runs it through the path below, marked so it is not held a second time.
    if (d->dock && d->dock->reviewBeforeApply() && !request.value(QStringLiteral("reviewed")).toBool()) {
        d->held.append(request);
        showAgentBar();
        out.insert(QStringLiteral("ok"), true);
        out.insert(QStringLiteral("applied"), QStringLiteral("held"));
        out.insert(QStringLiteral("saved"), false);
        out.insert(QStringLiteral("sha256"), QString::fromLatin1(d->base.revision.hash.toHex()));
        out.insert(QStringLiteral("message"), QStringLiteral("%1 is open in Relay with Review before apply on: the change waits "
                                                             "for the person to Apply it, and is not in the file yet.").arg(name));
        return reply(out);
    }

    const QString baseHex = request.value(QStringLiteral("base_sha256")).toString();
    const relay::merge::Labels labels{QStringLiteral("editor"), QStringLiteral("base"), QStringLiteral("agent")};
    QString target, applied;
    bool conflicted = false;
    relay::merge::MergeResult overlap;
    const auto against = [&](const QString &base) {
        const relay::merge::Reconciliation r = relay::merge::reconcile(base, buffer, content, labels);
        switch (r.outcome) {
        case Outcome::Unchanged:   // the agent's text is the base: nothing to do
        case Outcome::Converged:   // the buffer already says it
            target = buffer;
            applied = QStringLiteral("unchanged");
            break;
        case Outcome::TakeDisk:
            target = content;
            applied = QStringLiteral("exact");
            break;
        case Outcome::Merged:
            target = r.text;
            applied = QStringLiteral("merged");
            break;
        case Outcome::Conflict:
            conflicted = true;
            overlap = r.merge;
            break;
        }
    };
    if (!baseHex.isEmpty() && baseHex == sha256Hex(buffer.toUtf8())) {
        target = content;
        applied = content == buffer ? QStringLiteral("unchanged") : QStringLiteral("exact");
    } else if (!baseHex.isEmpty() && baseHex == QString::fromLatin1(d->base.revision.hash.toHex())) {
        against(d->base.text);
    } else if (!baseHex.isEmpty() && !isRemote()) {
        const relay::merge::Snapshot disk = relay::merge::readLocalFile(m_path, kMaxTextBytes);
        if (disk.revision.exists && !disk.truncated && QString::fromLatin1(disk.revision.hash.toHex()) == baseHex)
            against(disk.text);
    }
    // Overlapping, or worked out against text that is no longer anywhere: an edit_file whose
    // old_string still names exactly one place in the buffer is still exactly what it says.
    if ((conflicted || applied.isEmpty()) && request.value(QStringLiteral("tool")).toString() == QStringLiteral("edit_file")) {
        const QString from = relay::merge::editorText(request.value(QStringLiteral("old_string")).toString());
        const QString to = relay::merge::editorText(request.value(QStringLiteral("new_string")).toString());
        const bool all = request.value(QStringLiteral("replace_all")).toBool();
        const qsizetype found = from.isEmpty() ? 0 : buffer.count(from);
        if (found == 1 || (all && found > 1)) {
            target = buffer;
            if (all) target.replace(from, to);
            else target.replace(buffer.indexOf(from), from.size(), to);
            applied = QStringLiteral("merged");
            conflicted = false;
        }
    }
    QJsonArray conflictRegions;
    if (conflicted) {
        // Owner decision D2 of #P2W8 (card #PBZ4): an overlap is no longer refused. The three-way
        // merge goes into the buffer with each contested region between markers — the person's
        // lines, the base, the agent's — as one undo step, and the agent bar says how many there
        // are. The agent is told the same, with each region, so it does not redo the edit.
        for (const relay::merge::Conflict &c : overlap.conflicts) {
            conflictRegions.append(QJsonObject{{QStringLiteral("buffer"), c.ours}, {QStringLiteral("base"), c.base},
                                               {QStringLiteral("agent"), c.theirs}, {QStringLiteral("line"), c.line + 1}});
        }
        target = overlap.text;
        applied = QStringLiteral("conflict");
    }
    if (applied.isEmpty())
        return refuse(QStringLiteral("stale"),
                      QStringLiteral("%1 changed in the editor since this was worked out, so nothing was changed. Read it "
                                     "again and redo the edit against what is there now.").arg(name));

    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("applied"), applied);
    if (!conflictRegions.isEmpty()) {
        out.insert(QStringLiteral("conflicts"), conflictRegions);
        out.insert(QStringLiteral("message"),
                   QStringLiteral("%1 is open in Relay and the user has unsaved edits on the same lines: both versions are "
                                  "in the buffer between conflict markers (%2 %3) for them to resolve.")
                       .arg(name).arg(conflictRegions.size())
                       .arg(conflictRegions.size() == 1 ? QStringLiteral("place") : QStringLiteral("places")));
    }
    if (applied == QStringLiteral("unchanged")) {
        out.insert(QStringLiteral("saved"), !document->isModified());
        out.insert(QStringLiteral("sha256"), QString::fromLatin1(d->base.revision.hash.toHex()));
        return reply(out);
    }
    const bool wasClean = !document->isModified();
    replaceBuffer(target);
    noteAgentChange(buffer, target, request, applied, false);
    out.insert(QStringLiteral("buffer_sha256"), sha256Hex(target.toUtf8()));
    // Conflict markers are never saved for the person: they resolve them, then save.
    if (!wasClean || applied == QStringLiteral("conflict")) {
        // The user's unsaved edits are theirs to save: the agent's change joins them.
        out.insert(QStringLiteral("saved"), false);
        out.insert(QStringLiteral("sha256"), QString::fromLatin1(d->base.revision.hash.toHex()));
        return reply(out);
    }
    // A clean buffer was the disk's text, so it is saved again at once: the disk, the agent's
    // next read and every command it runs then agree with what is on screen.
    if (!isRemote()) {
        const relay::merge::Snapshot disk = relay::merge::readLocalFile(m_path, kMaxTextBytes);
        bool saved = false;
        if (disk.revision.exists && !disk.revision.sameContent(d->base.revision)) {
            out.insert(QStringLiteral("save_error"), QStringLiteral("%1 changed on disk at the same moment; the change is in the "
                                                                    "editor, unsaved.").arg(name));
            checkDisk();
        } else {
            saved = writeBuffer();
            if (!saved) out.insert(QStringLiteral("save_error"), m_notice);
        }
        if (saved) d->agentChanges.last().saved = true;
        showAgentBar();
        out.insert(QStringLiteral("saved"), saved);
        out.insert(QStringLiteral("sha256"), QString::fromLatin1(d->base.revision.hash.toHex()));
        return reply(out);
    }
    if (!m_remote || !m_remote->live() || (m_remote->busy() && d->remoteOp != Private::RemoteOp::Check
                                           && d->remoteOp != Private::RemoteOp::Refresh)) {
        out.insert(QStringLiteral("saved"), false);
        out.insert(QStringLiteral("sha256"), QString::fromLatin1(d->base.revision.hash.toHex()));
        out.insert(QStringLiteral("save_error"), m_remote && m_remote->live()
                                                     ? QStringLiteral("Relay is busy with %1; the change is in the editor, unsaved.").arg(m_remoteHost)
                                                     : QStringLiteral("The connection to %1 has ended; the change is in the editor, unsaved.").arg(m_remoteHost));
        return reply(out);
    }
    d->answerPatch(false, QStringLiteral("A newer change replaced this save."));
    d->patchResult = out;
    d->patchReply = reply;
    saveRemote();
}

void FilePreview::noteAgentChange(const QString &before, const QString &after, const QJsonObject &request,
                                  const QString &applied, bool saved) {
    QTextDocument *document = m_textView->document();
    const QVector<relay::merge::TextEdit> edits = relay::merge::editsBetween(before, after);
    AgentChange change;
    change.intent = request.value(QStringLiteral("intent")).toString();
    if (change.intent.isEmpty()) change.intent = QStringLiteral("Edit");
    change.turnId = request.value(QStringLiteral("turn_id")).toString();
    change.model = request.value(QStringLiteral("model")).toString();
    change.at = QDateTime::currentDateTime();
    change.applied = applied;
    change.saved = saved;
    // Where the change landed, in the buffer as it is now: positions in `after` are the edits'
    // positions shifted by what the edits before them added or removed.
    QList<QTextEdit::ExtraSelection> marks;
    qsizetype shift = 0;
    int first = -1, last = -1;
    for (const relay::merge::TextEdit &edit : edits) {
        const qsizetype start = edit.position + shift;
        const qsizetype end = start + edit.inserted.size();
        shift += edit.inserted.size() - edit.removed;
        QTextCursor cursor(document);
        cursor.setPosition(int(start));
        const int fromLine = cursor.blockNumber();
        // The last character inserted, or the place a pure deletion closed up.
        cursor.setPosition(int(end > start ? end - 1 : start), QTextCursor::KeepAnchor);
        const int toLine = cursor.blockNumber();
        first = first < 0 ? fromLine : std::min(first, fromLine);
        last = std::max(last, toLine);
        QTextEdit::ExtraSelection mark;
        mark.cursor = cursor;
        QColor tint = palette().color(QPalette::Highlight);
        tint.setAlpha(60);
        mark.format.setBackground(tint);
        mark.format.setProperty(QTextFormat::FullWidthSelection, true);
        marks.append(mark);
    }
    change.firstLine = std::max(first, 0) + 1;
    change.lastLine = std::max(last, first) + 1;
    d->agentChanges.append(change);
    d->agentSteps.append({before, after, document->availableUndoSteps()});
    // Keep the list to a screenful: the oldest go first.
    while (d->agentChanges.size() > 50) {
        d->agentChanges.removeFirst();
        d->agentSteps.removeFirst();
    }
    m_textView->setExtraSelections(marks);
    d->agentFade->start();
    showAgentBar();
}

void FilePreview::showAgentBar() {
    updateArtifactDock();
    const bool holding = !d->held.isEmpty();
    d->agentApply->setVisible(holding);
    d->agentDiscard->setVisible(holding);
    d->agentUndo->setVisible(!holding);
    d->agentList->setVisible(!holding && !d->agentChanges.isEmpty());
    if (holding) {
        // Review before apply (#PBZ4, D1): what waits, in the agent's own words.
        const QString intent = d->held.first().value(QStringLiteral("intent")).toString();
        d->agentText->setText(d->held.size() == 1
                                  ? QStringLiteral("Agent proposes: %1 · review, then Apply").arg(intent.isEmpty() ? QStringLiteral("an edit") : intent)
                                  : QStringLiteral("Agent proposes %1 changes, first: %2 · review, then Apply")
                                        .arg(d->held.size()).arg(intent.isEmpty() ? QStringLiteral("an edit") : intent));
        d->agentText->setToolTip(QStringLiteral("Review before apply is on for this project: Apply puts each proposed change "
                                                "in the buffer as its own undo step, merged around your typing."));
        d->agentBar->show();
        return;
    }
    if (d->agentChanges.isEmpty()) {
        d->agentBar->hide();
        return;
    }
    const AgentChange &change = d->agentChanges.last();
    QString text = QStringLiteral("Agent: %1 · %2").arg(change.intent,
        change.firstLine == change.lastLine ? QStringLiteral("line %1").arg(change.firstLine)
                                            : QStringLiteral("lines %1–%2").arg(change.firstLine).arg(change.lastLine));
    if (change.applied == QStringLiteral("merged")) text += QStringLiteral(" · merged with your edits");
    // D2: the chip over an inline conflict — the markers are in the buffer, the person resolves.
    if (change.applied == QStringLiteral("conflict"))
        text += QStringLiteral(" · ⚠ conflicts with your edits: both versions are between <<<<<<< editor and >>>>>>> agent");
    if (!change.saved) text += QStringLiteral(" · unsaved");
    d->agentText->setText(text);
    QStringList tip;
    if (!change.model.isEmpty()) tip << QStringLiteral("Model: %1").arg(change.model);
    if (!change.turnId.isEmpty()) tip << QStringLiteral("Turn: %1").arg(change.turnId);
    tip << QLocale().toString(change.at, QLocale::ShortFormat);
    d->agentText->setToolTip(tip.join(QLatin1Char('\n')));
    d->agentList->setText(d->agentChanges.size() == 1 ? QStringLiteral("Changes") : QStringLiteral("Changes (%1)").arg(d->agentChanges.size()));
    d->agentUndo->setEnabled(true);
    d->agentBar->show();
}

bool FilePreview::undoAgentChange() {
    using relay::merge::Outcome;
    if (d->agentChanges.isEmpty()) return false;
    QTextDocument *document = m_textView->document();
    const Private::AgentStep step = d->agentSteps.last();
    const bool wasSaved = d->agentChanges.last().saved;
    const bool clean = !document->isModified();
    const QString now = m_textView->toPlainText();
    if (document->availableUndoSteps() == step.undoSteps && now == step.after) {
        document->undo();   // nothing since: the step itself, cursor and all
    } else {
        // Typed since: take the agent's change back out around what was typed, or say why not.
        const relay::merge::Reconciliation r = relay::merge::reconcile(step.after, now, step.before);
        if (r.outcome == Outcome::Conflict) {
            setNotice(QStringLiteral("The agent's change cannot be undone on its own: you have edited the same lines since. "
                                     "Ctrl+Z steps back through both."));
            return false;
        }
        if (r.outcome == Outcome::Merged || r.outcome == Outcome::TakeDisk) replaceBuffer(r.text);
    }
    d->agentChanges.removeLast();
    d->agentSteps.removeLast();
    m_textView->setExtraSelections({});
    // The change had gone to disk with nothing else unsaved: its undo goes to disk the same way.
    bool written = true;
    if (wasSaved && clean) {
        if (isRemote()) written = writeOut();
        else {
            const relay::merge::Snapshot disk = relay::merge::readLocalFile(m_path, kMaxTextBytes);
            written = !disk.revision.exists || disk.revision.sameContent(d->base.revision) ? writeBuffer() : false;
        }
    }
    showAgentBar();
    if (written) setNotice(QStringLiteral("Undid the agent's change."));
    return true;
}

// ----- the docked agent (card #PBZ4) -------------------------------------------------------------

ArtifactDock *FilePreview::artifactDock() const { return d->dock; }

void FilePreview::setPluginSearch(const relay::agent::PluginSearch &search) { pluginSearch() = search; }

int FilePreview::heldAgentChanges() const { return int(d->held.size()); }

int FilePreview::applyHeldAgentChanges() {
    const QVector<QJsonObject> held = std::exchange(d->held, {});
    int landed = 0;
    QStringList failed;
    for (QJsonObject request : held) {
        request.insert(QStringLiteral("reviewed"), true);
        answerBufferRequest(request, [&](const QJsonObject &result) {
            if (result.value(QStringLiteral("ok")).toBool()) ++landed;
            else failed << result.value(QStringLiteral("message")).toString(result.value(QStringLiteral("error")).toString());
        });
    }
    showAgentBar();
    if (!failed.isEmpty())
        setNotice(QStringLiteral("%1 of the agent's proposed changes could not be applied: %2")
                      .arg(failed.size()).arg(failed.first()));
    return landed;
}

void FilePreview::discardHeldAgentChanges() {
    if (d->held.isEmpty()) return;
    const int count = int(d->held.size());
    d->held.clear();
    showAgentBar();
    setNotice(count == 1 ? QStringLiteral("Discarded the agent's proposed change.")
                         : QStringLiteral("Discarded the agent's %1 proposed changes.").arg(count));
}

// The dock follows the pane: shown for text and Markdown only, about this file and the plugin its
// name activates, with the change list drawn from the agent changes this buffer holds.
void FilePreview::updateArtifactDock() {
    if (!d->dock) return;
    const bool text = m_kind == Kind::Text || m_kind == Kind::Markdown;
    d->dock->setVisible(text && !m_path.isEmpty());
    relay::agent::ArtifactContext *context = d->dock->context();
    if (context->file() != m_path) {
        context->setFile(m_path);
        context->setPlugin(relay::agent::pluginForFile(m_path, pluginSearch()));
        d->dock->refreshTitle();
    }
    QList<ArtifactDock::Change> changes;
    for (const AgentChange &change : std::as_const(d->agentChanges)) {
        QStringList parts{change.firstLine == change.lastLine ? QStringLiteral("line %1").arg(change.firstLine)
                                                              : QStringLiteral("lines %1–%2").arg(change.firstLine).arg(change.lastLine),
                          change.intent};
        if (change.applied == QStringLiteral("merged")) parts << QStringLiteral("merged");
        if (change.applied == QStringLiteral("conflict")) parts << QStringLiteral("⚠ conflict");
        if (!change.saved) parts << QStringLiteral("unsaved");
        changes << ArtifactDock::Change{change.turnId, parts.join(QStringLiteral(" · ")), change.firstLine};
    }
    d->dock->setChanges(changes);
}

// ----- which files are open, for the agents' workers ----------------------------------------------

void FilePreview::notifyOpenBuffers() {
    if (buffersNotifyPending || !QCoreApplication::instance()) return;
    buffersNotifyPending = true;
    // Coalesced: an open is a base, a title and a modification change in one go, and the workers
    // want the list once.
    QTimer::singleShot(100, QCoreApplication::instance(), [] {
        buffersNotifyPending = false;
        QVector<BufferListener> &listeners = bufferListeners();
        listeners.erase(std::remove_if(listeners.begin(), listeners.end(),
                                       [](const BufferListener &l) { return l.context.isNull(); }),
                        listeners.end());
        const QVector<BufferListener> now = listeners;   // a listener may add another
        for (const BufferListener &listener : now)
            if (listener.context && listener.changed) listener.changed();
    });
}

QList<FilePreview *> FilePreview::livePreviews() {
    return livePreviewList();
}

QJsonArray FilePreview::openBuffers(const QWidget *window) {
    QJsonArray files;
    for (FilePreview *preview : livePreviewList()) {
        if (window && preview->window() != window) continue;
        const QJsonObject entry = preview->openBufferEntry();
        if (!entry.isEmpty()) files.append(entry);
    }
    return files;
}

void FilePreview::answerBufferRequestIn(const QWidget *window, const QJsonObject &request,
                                        const std::function<void(const QJsonObject &)> &reply) {
    const QString path = request.value(QStringLiteral("path")).toString();
    // Every console of a tab hears every event of the tab's one worker, so a patch its agent sends
    // reaches this window once per console (#PBZ4). The first answers; the rest are the same
    // request again — same id, same text, within moments — and are dropped, or a change held for
    // review would wait on the bar twice. Two workers minting the same id for the same text in
    // the same five seconds is not a case that exists.
    if (request.value(QStringLiteral("op")).toString() == QStringLiteral("patch")) {
        static QHash<QByteArray, qint64> recent;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (auto it = recent.begin(); it != recent.end();) it = now - it.value() > 5000 ? recent.erase(it) : std::next(it);
        const QByteArray key = QCryptographicHash::hash(
            (request.value(QStringLiteral("id")).toString() + QChar(0x1f) + path + QChar(0x1f)
             + request.value(QStringLiteral("content")).toString()).toUtf8(), QCryptographicHash::Sha1);
        if (recent.contains(key)) return;
        recent.insert(key, now);
    }
    for (FilePreview *preview : livePreviewList()) {
        if (window && preview->window() != window) continue;
        if (preview->openBufferEntry().value(QStringLiteral("path")).toString() != path || path.isEmpty()) continue;
        preview->answerBufferRequest(request, reply);
        return;
    }
    reply(QJsonObject{{QStringLiteral("type"), QStringLiteral("buffer_result")},
                      {QStringLiteral("id"), request.value(QStringLiteral("id"))},
                      {QStringLiteral("path"), path},
                      {QStringLiteral("ok"), false},
                      {QStringLiteral("error"), QStringLiteral("not_open")}});
}

void FilePreview::onOpenBuffersChanged(QObject *context, std::function<void()> changed) {
    bufferListeners().append({QPointer<QObject>(context), std::move(changed)});
}

QString FilePreview::readCapped(const QString &path, qint64 size) {
    // What is read here is the base every later change on disk is reconciled with (#F8R7).
    d->base = relay::merge::readLocalFile(path, kMaxTextBytes);
    if (!d->base.revision.exists) {
        setNotice(QStringLiteral("The file could not be read."));
        return QString();
    }
    if (d->base.truncated)
        setNotice(QStringLiteral("Showing the first %1 of %2.").arg(humanSize(kMaxTextBytes), humanSize(std::max(size, d->base.revision.size))));
    return QString::fromUtf8(d->base.bytes);
}

void FilePreview::showText(const QString &path, qint64 size) {
    const QString content = readCapped(path, size);
    // A "text" file with NUL bytes is really binary; show its details instead.
    if (content.contains(QChar(0))) {
        const QMimeDatabase mimes;
        showInfo(path, mimes.mimeTypeForFile(path).name(), QStringLiteral("This file contains binary data."));
        return;
    }
    m_textView->setPlainText(content);
    // The base in the form the buffer holds it, so "the buffer is still the base" is an exact test.
    d->base.text = m_textView->toPlainText();
#ifdef RELAY_HAVE_SYNTAX_HIGHLIGHTING
    const KSyntaxHighlighting::Definition definition = d->repository.definitionForFileName(path);
    // The text first, its colours after: the highlighter is installed on the document that is
    // already filled and works through it a slice at a time (#MDSG). readCapped() has already
    // held the text to kMaxTextBytes, so the limit below only turns a file away when the two
    // figures are the same.
    if (definition.isValid() && content.size() <= kMaxHighlightBytes) {
        d->highlighter = new LazyHighlighter(m_textView->document());
        KSyntaxHighlighting::Theme theme = d->repository.theme(QStringLiteral("Breeze Dark"));
        if (!theme.isValid()) theme = d->repository.defaultTheme(KSyntaxHighlighting::Repository::DarkTheme);
        d->highlighter->setTheme(theme);
        d->highlighter->setDefinition(definition);
        d->highlighter->start();
    }
#endif
    m_kind = Kind::Text;
    m_stack->setCurrentWidget(m_textView);
}

void FilePreview::showMarkdown(const QString &path, qint64 size) {
    const QString content = readCapped(path, size);
    m_textView->setPlainText(content);
    d->base.text = m_textView->toPlainText();
    // Relative links and images resolve against the file's folder.
    m_markdownView->setSearchPaths({QFileInfo(path).absolutePath()});
    m_markdownView->document()->setBaseUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath() + QLatin1Char('/')));
    m_markdownView->setMarkdown(content);
    m_kind = Kind::Markdown;
    m_stack->setCurrentWidget(m_markdownView);
}

bool FilePreview::showImage(const QString &path, qint64 size) {
    if (size > kMaxImageBytes) {
        setNotice(QStringLiteral("Images larger than %1 are not previewed.").arg(humanSize(kMaxImageBytes)));
        return false;
    }
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        setNotice(QStringLiteral("The image could not be decoded: %1").arg(reader.errorString()));
        return false;
    }
    d->pixmap = QPixmap::fromImage(image);
    m_kind = Kind::Image;
    m_stack->setCurrentWidget(m_imageArea);
    updateImage();
    return true;
}

void FilePreview::showPdf(const QString &path) {
#ifdef RELAY_HAVE_QTPDF
    d->pdf->close();
    d->pdf->load(path);
    if (d->pdf->status() == QPdfDocument::Status::Ready || d->pdf->pageCount() > 0) {
        m_kind = Kind::Pdf;
        m_stack->setCurrentWidget(m_pdfPage);
        return;
    }
    showInfo(path, QStringLiteral("application/pdf"), QStringLiteral("The PDF could not be opened."));
#else
    showInfo(path, QStringLiteral("application/pdf"), QStringLiteral("PDF preview is not available in this build."));
#endif
}

void FilePreview::showDocx(const QString &path) {
    QString error;
    if (!m_docxView->open(path, &error)) {
        showInfo(path, QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.document"), error);
        return;
    }
    m_kind = Kind::Docx;
    m_stack->setCurrentWidget(m_docxView);
    setEditable(true);
}

void FilePreview::showInfo(const QString &path, const QString &mime, const QString &message) {
    const QFileInfo info(path);
    QString text = info.fileName() + QStringLiteral("\n\n");
    text += QStringLiteral("Type: %1\nSize: %2\nModified: %3\nFolder: %4")
                .arg(mime, humanSize(info.size()), QLocale().toString(info.lastModified(), QLocale::ShortFormat), info.absolutePath());
    m_info->setText(text);
    if (!message.isEmpty()) setNotice(message);
    m_kind = Kind::Info;
    m_stack->setCurrentWidget(m_infoPage);
}

void FilePreview::setNotice(const QString &text) {
    m_notice = text;
    m_noticeLabel->setText(text);
    m_noticeLabel->setVisible(!text.isEmpty());
}

void FilePreview::updateImage() {
    if (m_kind != Kind::Image || d->pixmap.isNull()) return;
    const int available = std::max(64, m_imageArea->viewport()->width() - 8);
    QPixmap shown = d->pixmap;
    if (!m_imageActualSize && d->pixmap.width() > available)
        shown = d->pixmap.scaledToWidth(available, Qt::SmoothTransformation);
    m_image->setPixmap(shown);
    m_image->resize(shown.size());
}

void FilePreview::updateModeButton() {
    m_wrap->setVisible(m_kind == Kind::Text || (m_kind == Kind::Markdown && m_markdownSource));
    if (m_kind == Kind::Markdown) {
        // The format is in the label, not only in the tooltip (issue #VXTF, owner 2026-09-18:
        // "in markdown, it should probably say 'source (MD)' rather than 'source'"): "Source" on
        // its own reads as the source of whatever the pane happens to hold.
        m_mode->setText(m_markdownSource ? QStringLiteral("Rendered (MD)") : QStringLiteral("Source (MD)"));
        m_mode->setToolTip(m_markdownSource ? QStringLiteral("Show rendered Markdown") : QStringLiteral("Show Markdown source"));
        m_mode->show();
    } else if (m_kind == Kind::Image) {
        m_mode->setText(m_imageActualSize ? QStringLiteral("Fit") : QStringLiteral("100%"));
        m_mode->setToolTip(m_imageActualSize ? QStringLiteral("Fit to width") : QStringLiteral("Actual size"));
        m_mode->show();
    } else {
        m_mode->hide();
    }
}

// Where a link inside the rendered Markdown goes (issue S1JP). A `#section` link scrolls this
// document, a local file or folder is handed to the host for a pane of its own, and anything else
// (http, mailto) goes to the desktop. Nothing loads into this pane, so the file that carried the
// link is still there when the reader comes back to it.
void FilePreview::followLink(const QUrl &url) {
    if (url.isEmpty()) return;
    if (url.path().isEmpty() && url.hasFragment()) { m_markdownView->scrollToAnchor(url.fragment()); return; }
    // A relative link in a document that came from a host points at that host's disk, not at this
    // one (#S5SH): it opens as `ssh://host/path`, in a pane of its own, like any other link here.
    if (isRemote() && url.isRelative() && !url.path().isEmpty()) {
        const QString path = url.path().startsWith(QLatin1Char('/'))
                                 ? url.path()
                                 : QDir::cleanPath(relay::remote::parentPath(m_remotePath) + QLatin1Char('/') + url.path());
        if (onOpenLink) onOpenLink(relay::remote::fileUrl(m_remoteHost, path));
        else setNotice(QStringLiteral("%1 is on %2.").arg(path, m_remoteHost));
        return;
    }
    const QUrl resolved = m_markdownView->document()->baseUrl().resolved(url);
    if (resolved.isLocalFile()) {
        const QString target = resolved.toLocalFile();
        if (QFileInfo::exists(target)) {
            if (onOpenLink) onOpenLink(target);
            else QDesktopServices::openUrl(QUrl::fromLocalFile(target));
            return;
        }
        setNotice(QStringLiteral("That link points at %1, which is not there.").arg(target));
        return;
    }
    QDesktopServices::openUrl(resolved);
}

QList<FileMenuItem> FilePreview::menu() const {
    if (m_path.isEmpty()) return {};
    if (isRemote()) {
        // The same two entries the remote explorer offers: reopen it here, or copy the path as
        // the host spells it. Nothing on this machine can open a file that is not on it (#S5SH).
        return {FileMenuItem{QStringLiteral("openInternal"), QStringLiteral("Reload from %1").arg(m_remoteHost), true},
                FileMenuItem{QStringLiteral("copyPath"), QStringLiteral("Copy path on %1").arg(m_remoteHost), true}};
    }
    FileMenuHost host;
    host.canPreview = true;   // "Open internal" reopens the file here, which always works
    host.writable = false;
    return previewMenu(host);
}

bool FilePreview::eventFilter(QObject *object, QEvent *event) {
    if (event->type() == QEvent::ContextMenu) {
        QWidget *source = nullptr;
        if (object == m_textView->viewport()) source = m_textView;
        else if (object == m_markdownView->viewport()) source = m_markdownView;
        if (showMenu(static_cast<QContextMenuEvent *>(event)->globalPos(), source)) return true;
    }
    return QWidget::eventFilter(object, event);
}

void FilePreview::contextMenuEvent(QContextMenuEvent *event) {
    // The header, the notice line and the empty room around a viewer: the same menu, with no
    // viewer entries to add.
    if (showMenu(event->globalPos(), nullptr)) event->accept();
    else QWidget::contextMenuEvent(event);
}

bool FilePreview::showMenu(const QPoint &globalPos, QWidget *source) {
    const QList<FileMenuItem> items = menu();
    if (items.isEmpty()) return false;
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    for (const FileMenuItem &item : items) {
        if (item.isSeparator()) { menu->addSeparator(); continue; }
        QAction *action = menu->addAction(item.label);
        action->setEnabled(item.enabled);
        const QString id = item.id;
        connect(action, &QAction::triggered, this, [this, id] { runMenuAction(id); });
    }
    // The viewer's own entries (Copy, Select all, and over a link Copy link location) follow, so
    // taking the context menu over does not take them away. The actions move to this menu, which
    // then owns them; the throwaway menu they came in goes. Reparenting that menu instead would
    // clear its Qt::Popup flag and draw it as a child widget over these entries.
    QMenu *standard = nullptr;
    if (source == m_textView) standard = m_textView->createStandardContextMenu();
    else if (source == m_markdownView) standard = m_markdownView->createStandardContextMenu();
    if (standard) {
        const auto actions = standard->actions();
        for (QAction *action : actions) action->setParent(menu);
        menu->addSeparator();
        menu->addActions(actions);
        delete standard;
    }
    menu->popup(globalPos);
    return true;
}

void FilePreview::runMenuAction(const QString &id) {
    if (m_path.isEmpty()) return;
    if (isRemote()) {
        // Nothing on this machine can open a file that is on another one; what is useful is the
        // path itself, as the host spells it, to paste into the terminal pane next door.
        if (id == QLatin1String("openInternal")) openRemote(m_path);
        else if (id == QLatin1String("copyPath")) QApplication::clipboard()->setText(m_remotePath);
        else setNotice(QStringLiteral("%1 is on %2 · this machine's applications cannot open it.")
                           .arg(QFileInfo(m_remotePath).fileName(), m_remoteHost));
        return;
    }
    if (id == QLatin1String("openInternal")) open(m_path);
    else if (id == QLatin1String("openExternal")) QDesktopServices::openUrl(QUrl::fromLocalFile(m_path));
    else if (id == QLatin1String("openFolder")) openContainingFolder(m_path);
    else if (id == QLatin1String("copyPath")) QApplication::clipboard()->setText(m_path);
}

void FilePreview::setHeaderRightInset(int pixels) {
    if (!m_header || m_header->contentsMargins().right() == pixels) return;
    m_header->setContentsMargins(0, 0, pixels, 0);
    updateTitleText();
}

void FilePreview::updateTitleText() {
    m_title->setText(m_title->fontMetrics().elidedText(title(), Qt::ElideMiddle, std::max(40, m_title->contentsRect().width() - 8)));
}

void FilePreview::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    QTimer::singleShot(0, this, [this] { updateTitleText(); });
    updateImage();
}

// Alt+Z (files.toggleWrap): the same switch the header's Word wrap button moves, so the state the
// user sees and the state the key sets can never disagree. A rendered Markdown file has no wrap
// to toggle; the key then returns false and the window may act on another preview instead.
bool FilePreview::toggleWrap() {
    if (!(m_kind == Kind::Text || showingSource())) return false;
    m_wrap->setChecked(!m_wrap->isChecked());
    return true;
}

void FilePreview::goToLine(int line) {
    // A remote file is still on its way when the click that carried the line number arrives
    // (#S5SH): the line waits for the bytes.
    if (line > 0 && isRemote() && m_kind == Kind::None) { m_pendingLine = line; return; }
    if (line <= 0 || (m_kind != Kind::Text && m_kind != Kind::Markdown)) return;
    // A line number counts lines in the source, so a Markdown preview has to leave the rendered
    // view to point at one. It used to do that without recording the switch, which left the button
    // still offering "Source" over a pane that was already showing it: Ctrl+clicking `notes.md:9`
    // in the output gave raw Markdown with no obvious way back (issue #3W58, owner 2026-09-18:
    // "MD's arent printing markdown"). Go through the same state the button uses, so it reads
    // "Rendered (MD)" and one click renders the file.
    if (m_kind == Kind::Markdown && !m_markdownSource) {
        m_markdownSource = true;
        m_stack->setCurrentWidget(m_textView);
        updateModeButton();
    }
    QTextBlock block = m_textView->document()->findBlockByNumber(line - 1);
    if (!block.isValid()) return;
    QTextCursor cursor(block);
    cursor.select(QTextCursor::LineUnderCursor);
    m_textView->setTextCursor(cursor);
    m_textView->centerCursor();
}

// ----- PlanEditor ---------------------------------------------------------------------------

namespace {
// Minimal Markdown colouring when KSyntaxHighlighting is not built in.
class SimpleMarkdownHighlighter final : public QSyntaxHighlighter {
public:
    explicit SimpleMarkdownHighlighter(QTextDocument *document) : QSyntaxHighlighter(document) {}
protected:
    void highlightBlock(const QString &text) override {
        // The live theme tokens, not Relay Dark's values: cyan and amber on a light theme's paper
        // were under 2:1 (docs/ARCHITECTURE.md, "Legible text").
        QTextCharFormat heading; heading.setForeground(relay::theme::Accent); heading.setFontWeight(QFont::Bold);
        QTextCharFormat bullet; bullet.setForeground(relay::theme::TextMuted);   // not amber: amber means "waiting on you" (be81edb)
        QTextCharFormat code; code.setForeground(relay::theme::Success);
        const bool inFence = previousBlockState() == 1;
        const bool fence = text.trimmed().startsWith(QStringLiteral("```"));
        if (inFence || fence) {
            setFormat(0, text.size(), code);
            setCurrentBlockState(inFence != fence ? 1 : 0);
            return;
        }
        setCurrentBlockState(0);
        if (text.startsWith('#')) { setFormat(0, text.size(), heading); return; }
        static const QRegularExpression list(QStringLiteral("^\\s*([-*+]|\\d+\\.)\\s"));
        const auto match = list.match(text);
        if (match.hasMatch()) setFormat(0, match.capturedLength(), bullet);
        static const QRegularExpression inlineCode(QStringLiteral("`[^`]+`"));
        auto it = inlineCode.globalMatch(text);
        while (it.hasNext()) { const auto m = it.next(); setFormat(m.capturedStart(), m.capturedLength(), code); }
    }
};
}  // namespace

struct PlanEditor::Private {
#ifdef RELAY_HAVE_SYNTAX_HIGHLIGHTING
    KSyntaxHighlighting::Repository repository;
    LazyHighlighter *highlighter = nullptr;
#endif
    QSyntaxHighlighter *fallback = nullptr;
    // The docked agent (card #PBZ4), and the watch that shows its writes: a plan's edits by the
    // agent go to the disk, so a clean editor follows the file as one undo step.
    ArtifactDock *dock = nullptr;
    QFileSystemWatcher *watcher = nullptr;
    QString loaded;   // what was last read from or written to the disk
};

PlanEditor::PlanEditor(QWidget *parent) : QWidget(parent), d(new Private) {
    setObjectName(QStringLiteral("planEditor"));
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(8, 6, 8, 8); layout->setSpacing(6);
    auto *header = new QHBoxLayout;
    m_title = new QLabel; m_title->setObjectName(QStringLiteral("planTitle"));
    m_title->setTextFormat(Qt::PlainText);
    header->addWidget(m_title, 1);
    auto *saveButton = new QToolButton; saveButton->setText(QStringLiteral("Save")); saveButton->setToolTip(QStringLiteral("Save (Ctrl+S)"));
    auto *reload = new QToolButton; reload->setText(QStringLiteral("Reload"));
    header->addWidget(saveButton); header->addWidget(reload);
    layout->addLayout(header);
    m_notice = new QLabel; m_notice->setObjectName(QStringLiteral("planNotice")); m_notice->setWordWrap(true); m_notice->hide();
    layout->addWidget(m_notice);
    m_editor = new QPlainTextEdit;
    m_editor->setObjectName(QStringLiteral("planText"));
    m_editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_editor->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    layout->addWidget(m_editor, 1);
    m_planActions = new QWidget;
    auto *actions = new QHBoxLayout(m_planActions); actions->setContentsMargins(0, 0, 0, 0);
    auto *execute = new QPushButton(QStringLiteral("Run"));
    execute->setObjectName(QStringLiteral("planExecute"));
    execute->setToolTip(QStringLiteral("Switch to build mode and carry out this plan (your edits are saved first)"));
    auto *fresh = new QPushButton(QStringLiteral("Run in fresh context"));
    fresh->setToolTip(QStringLiteral("Start a new conversation that only has this plan"));
    auto *keep = new QPushButton(QStringLiteral("Keep planning"));
    actions->addWidget(execute); actions->addWidget(fresh); actions->addStretch(1); actions->addWidget(keep);
    layout->addWidget(m_planActions);
    d->dock = new ArtifactDock(this);
    {
        relay::agent::ArtifactContext *context = d->dock->context();
        QPointer<PlanEditor> self(this);
        context->state = [self] {
            relay::agent::ArtifactState state;
            if (!self) return state;
            const QTextCursor cursor = self->m_editor->textCursor();
            state.line = cursor.blockNumber() + 1;
            state.column = cursor.positionInBlock() + 1;
            if (cursor.hasSelection()) {
                QTextCursor first(self->m_editor->document()), last(self->m_editor->document());
                first.setPosition(cursor.selectionStart());
                last.setPosition(cursor.selectionEnd());
                state.firstLine = first.blockNumber() + 1;
                state.lastLine = last.blockNumber() + 1;
                state.selection = cursor.selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
            }
            state.dirty = self->isDirty();
            state.editable = true;
            state.mode = QStringLiteral("plan");
            return state;
        };
        context->save = [self] { return self && self->save(); };
        context->revert = [self] { return self && self->isDirty() && self->open(self->m_path); };
        context->goToLine = [self](int line) {
            if (!self) return;
            QTextCursor cursor(self->m_editor->document()->findBlockByNumber(std::max(0, line - 1)));
            self->m_editor->setTextCursor(cursor);
            self->m_editor->centerCursor();
        };
        d->dock->onGoToLine = context->goToLine;
        context->onTurnFinished = [self](const relay::agent::TurnRecord &record) {
            if (self) self->d->dock->nameTurn(record.turnId, record.prompt);
        };
    }
    layout->addWidget(d->dock, 0);
    d->watcher = new QFileSystemWatcher(this);
    connect(d->watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &) {
        QTimer::singleShot(80, this, [this] {
            if (m_path.isEmpty()) return;
            if (!d->watcher->files().contains(m_path) && QFileInfo::exists(m_path)) d->watcher->addPath(m_path);
            QFile file(m_path);
            if (!file.open(QIODevice::ReadOnly) || file.size() > FilePreview::kMaxTextBytes) return;
            const QString now = QString::fromUtf8(file.readAll());
            if (now == d->loaded || now == m_editor->toPlainText()) { d->loaded = now; return; }
            if (isDirty()) {
                m_notice->setText(QStringLiteral("%1 changed on disk. Reload shows it; Save keeps yours.").arg(title()));
                m_notice->show();
                return;
            }
            // Clean: follow the disk as one undo step, keeping the cursor where it was.
            const int position = m_editor->textCursor().position();
            QTextCursor all(m_editor->document());
            all.select(QTextCursor::Document);
            all.insertText(now);
            QTextCursor back(m_editor->document());
            back.setPosition(std::min(position, int(now.size())));
            m_editor->setTextCursor(back);
            m_editor->document()->setModified(false);
            d->loaded = now;
        });
    });
    connect(m_editor->document(), &QTextDocument::modificationChanged, this, [this](bool) { d->dock->context()->changed(); });
    connect(saveButton, &QToolButton::clicked, this, [this] { save(); });
    connect(reload, &QToolButton::clicked, this, [this] { if (!m_path.isEmpty()) open(m_path); });
    connect(execute, &QPushButton::clicked, this, [this] { if (onExecute) onExecute(false); });
    connect(fresh, &QPushButton::clicked, this, [this] { if (onExecute) onExecute(true); });
    connect(keep, &QPushButton::clicked, this, [this] { if (onKeepPlanning) onKeepPlanning(); });
    connect(m_editor->document(), &QTextDocument::modificationChanged, this, [this](bool) { updateTitle(); });
    auto *shortcut = new QShortcut(QKeySequence::Save, this);
    shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(shortcut, &QShortcut::activated, this, [this] { save(); });
#ifdef RELAY_HAVE_SYNTAX_HIGHLIGHTING
    const KSyntaxHighlighting::Definition definition = d->repository.definitionForName(QStringLiteral("Markdown"));
    if (definition.isValid()) {
        d->highlighter = new LazyHighlighter(m_editor->document());
        KSyntaxHighlighting::Theme theme = d->repository.theme(QStringLiteral("Breeze Dark"));
        if (!theme.isValid()) theme = d->repository.defaultTheme(KSyntaxHighlighting::Repository::DarkTheme);
        d->highlighter->setTheme(theme);
        d->highlighter->setDefinition(definition);
    }
    if (!d->highlighter) d->fallback = new SimpleMarkdownHighlighter(m_editor->document());
#else
    d->fallback = new SimpleMarkdownHighlighter(m_editor->document());
#endif
}

PlanEditor::~PlanEditor() {
    delete d->dock;   // the console's wrapper reads the context, which the dock owns
    delete d;
}

ArtifactDock *PlanEditor::artifactDock() const { return d->dock; }

bool PlanEditor::open(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { m_notice->setText(QStringLiteral("Could not open %1").arg(path)); m_notice->show(); return false; }
    if (file.size() > FilePreview::kMaxTextBytes) { m_notice->setText(QStringLiteral("File is too large to edit here.")); m_notice->show(); return false; }
    m_path = QFileInfo(path).absoluteFilePath();
    d->loaded = QString::fromUtf8(file.readAll());
    m_editor->setPlainText(d->loaded);
    m_editor->document()->setModified(false);
    if (!d->watcher->files().isEmpty()) d->watcher->removePaths(d->watcher->files());
    d->watcher->addPath(m_path);
    if (d->dock->context()->file() != m_path) {
        d->dock->context()->setFile(m_path);
        d->dock->context()->setPlugin(relay::agent::pluginForFile(m_path, pluginSearch()));
        d->dock->refreshTitle();
    }
#ifdef RELAY_HAVE_SYNTAX_HIGHLIGHTING
    // A new document, so the colouring starts again at the top (#MDSG).
    if (d->highlighter) d->highlighter->start();
#endif
    m_notice->hide();
    updateTitle();
    return true;
}

bool PlanEditor::save() {
    if (m_path.isEmpty()) return false;
    QSaveFile out(m_path);
    if (!out.open(QIODevice::WriteOnly)) { m_notice->setText(QStringLiteral("Could not save: %1").arg(out.errorString())); m_notice->show(); return false; }
    out.write(m_editor->toPlainText().toUtf8());
    if (!out.commit()) { m_notice->setText(QStringLiteral("Could not save: %1").arg(out.errorString())); m_notice->show(); return false; }
    d->loaded = m_editor->toPlainText();
    if (!d->watcher->files().contains(m_path)) d->watcher->addPath(m_path);   // an atomic save replaced the inode
    m_editor->document()->setModified(false);
    m_notice->hide();
    updateTitle();
    return true;
}

bool PlanEditor::isDirty() const { return m_editor->document()->isModified(); }
QString PlanEditor::text() const { return m_editor->toPlainText(); }
QString PlanEditor::title() const { return QFileInfo(m_path).fileName(); }
void PlanEditor::setPlanActions(bool enabled) { m_planActions->setVisible(enabled); }

void PlanEditor::updateTitle() {
    m_title->setText((isDirty() ? QStringLiteral("● ") : QString()) + title());
    m_title->setToolTip(m_path);
    if (onTitleChanged) onTitleChanged(title());
}

}  // namespace relay
