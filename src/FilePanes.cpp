// SPDX-License-Identifier: GPL-3.0-or-later
#include "FilePanes.h"
#include "Hints.h"
#include "RemoteFiles.h"
#include "Theme.h"
#include <QBuffer>
#include <QStandardItemModel>
#include <QTextBlock>
#include <QTextCursor>

#include <QSaveFile>
#include <QShortcut>
#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QTextDocument>
#include <QTextCharFormat>
#include <QDateTime>
#include <QResizeEvent>
#include <algorithm>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
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
        QDesktopServices::openUrl(QUrl::fromLocalFile(target));
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
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && mods == Qt::NoModifier) {
            const QModelIndex index = m_view->currentIndex();
            if (index.isValid()) activate(pathAt(index));
            return true;
        }
        if ((key->key() == Qt::Key_Backspace && mods == Qt::NoModifier) || (key->key() == Qt::Key_Up && mods == Qt::AltModifier)) {
            goUp();
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
            const QModelIndex index = m_view->currentIndex();
            if (index.isValid()) activate(pathAt(index));
            return true;
        }
        if (key->key() == Qt::Key_Escape && !m_filter->text().isEmpty()) {
            m_filter->clear();
            return true;
        }
        if (key->key() == Qt::Key_Up && mods == Qt::AltModifier) { goUp(); return true; }
    }
    return QWidget::eventFilter(object, event);
}

// ----- FilePreview -------------------------------------------------------------------------------

struct FilePreview::Private {
#ifdef RELAY_HAVE_SYNTAX_HIGHLIGHTING
    KSyntaxHighlighting::Repository repository;
    KSyntaxHighlighting::SyntaxHighlighter *highlighter = nullptr;
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
};

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
    header->addWidget(m_title, 1);
    header->addWidget(m_hostChip);
    header->addWidget(m_save);
    header->addWidget(m_mode);
    header->addWidget(m_reload);
    header->addWidget(m_external);
    layout->addLayout(header);

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
    m_textView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
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
    m_stack->addWidget(m_markdownView);

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
        // The slow path teaches the fast one (WARP.md, "Shortcut hints"). The registry keeps the
        // count and the cooldown, so it is shown a few times and then never again.
        m_teachSaveShortcut = relay::ShortcutHints::instance().shouldShow(QStringLiteral("files.remoteSave"));
        save();
    });
    auto *saveShortcut = new QShortcut(QKeySequence::Save, this);
    saveShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(saveShortcut, &QShortcut::activated, this, [this] { save(); });
    connect(m_textView->document(), &QTextDocument::modificationChanged, this, [this](bool) {
        updateTitleText();
        if (onTitleChanged) onTitleChanged(title());   // the ● reaches the tab as well as the header
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
}

FilePreview::~FilePreview() {
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
    return m_editable && m_textView->document()->isModified();
}

QString FilePreview::text() const {
    return m_textView->toPlainText();
}

bool FilePreview::open(const QString &path) {
    if (relay::remote::isFileUrl(path)) return openRemote(path);
    const QFileInfo info(path);
    if (path.isEmpty() || !info.exists() || !info.isFile() || !info.isReadable()) return false;
    setEditable(false);
    m_remoteHost.clear();
    m_remotePath.clear();
    m_hostChip->hide();
    m_pendingLine = 0;
    if (m_remote) m_remote->cancel();
    if (m_reconnect) m_reconnect->stop();
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

    if (suffix == QStringLiteral("md") || suffix == QStringLiteral("markdown") || mime.inherits(QStringLiteral("text/markdown"))) {
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

    updateTitleText();
    m_title->setToolTip(absolute);
    m_reload->setEnabled(true);
    m_external->setEnabled(true);
    updateModeButton();
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

    m_path = url;
    m_remoteHost = ref.host;
    m_remotePath = ref.path;
    m_kind = Kind::None;
    m_markdownSource = false;
    setEditable(false);
    m_pendingLine = 0;
#ifdef RELAY_HAVE_SYNTAX_HIGHLIGHTING
    delete d->highlighter;
    d->highlighter = nullptr;
#endif
    m_textView->clear();
    m_textView->document()->setModified(false);
    m_stack->setCurrentWidget(m_textView);
    m_hostChip->setText(ref.host);
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
        m_remote->onFetched = [this](const QByteArray &content, const relay::remote::FileStat &) { showRemoteContent(content); };
        m_remote->onFailed = [this](const QString &message, relay::remote::Conflict conflict, const relay::remote::FileStat &) {
            remoteFailed(message, int(conflict));
        };
        m_remote->onSaved = [this](const relay::remote::FileStat &) {
            m_textView->document()->setModified(false);
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
    if (suffix == QStringLiteral("md") || suffix == QStringLiteral("markdown") || mime.inherits(QStringLiteral("text/markdown"))) {
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
#ifdef RELAY_HAVE_SYNTAX_HIGHLIGHTING
    delete d->highlighter;
    d->highlighter = nullptr;
    const KSyntaxHighlighting::Definition definition = d->repository.definitionForFileName(m_remotePath);
    if (definition.isValid()) {
        d->highlighter = new KSyntaxHighlighting::SyntaxHighlighter(m_textView->document());
        KSyntaxHighlighting::Theme theme = d->repository.theme(QStringLiteral("Breeze Dark"));
        if (!theme.isValid()) theme = d->repository.defaultTheme(KSyntaxHighlighting::Repository::DarkTheme);
        d->highlighter->setTheme(theme);
        d->highlighter->setDefinition(definition);
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
    if (conflict == int(relay::remote::Conflict::Changed) || conflict == int(relay::remote::Conflict::Vanished)) {
        // The host's copy moved on between the fetch and the save, and nothing was written. The
        // choice is the user's: their text over the top, or the host's file back in the pane.
        QMessageBox box(QMessageBox::Warning, QStringLiteral("Save"), message, QMessageBox::NoButton, this);
        box.setInformativeText(QStringLiteral("Your edits are still in the pane either way."));
        QPushButton *overwrite = box.addButton(QStringLiteral("Overwrite anyway"), QMessageBox::DestructiveRole);
        QPushButton *reload = box.addButton(QStringLiteral("Reload from %1").arg(m_remoteHost), QMessageBox::ResetRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == overwrite && m_remote) {
            setNotice(QStringLiteral("Saving to %1…").arg(m_remoteHost));
            m_remote->save(m_textView->toPlainText().toUtf8(), true);
        } else if (box.clickedButton() == reload) {
            m_textView->document()->setModified(false);   // the user asked for the host's copy
            openRemote(m_path);
        } else {
            setNotice(message);
        }
        return;
    }
    if (m_editable) {
        // A save that could not happen: the buffer stays exactly as it is, and the pane says why.
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
    // The pane opens ready to type in, and Ctrl+S (a WidgetWithChildren shortcut) has a focused
    // widget to fire from: the host's focusInput() sets the focus on this widget, and the proxy
    // passes it to the editor. A read-only preview keeps the focus itself, as it always did.
    setFocusProxy(on ? m_textView : nullptr);
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
    if (!m_editable || !isRemote() || !m_remote) return false;
    if (!m_textView->document()->isModified()) return true;
    if (m_remote->busy()) { setNotice(QStringLiteral("Still saving to %1…").arg(m_remoteHost)); return false; }
    if (!m_remote->live()) {
        setNotice(QStringLiteral("The connection to %1 has ended · your edits are safe in this pane. Log in to %1 again "
                                 "in the terminal pane and press %2, or copy the text out.")
                      .arg(m_remoteHost, QKeySequence(QKeySequence::Save).toString(QKeySequence::NativeText)));
        watchForReconnect();
        return false;
    }
    setNotice(QStringLiteral("Saving to %1…").arg(m_remoteHost));
    m_remote->save(m_textView->toPlainText().toUtf8());
    return true;
}

QString FilePreview::readCapped(const QString &path, qint64 size) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setNotice(QStringLiteral("The file could not be read."));
        return QString();
    }
    const QByteArray bytes = file.read(kMaxTextBytes);
    if (size > kMaxTextBytes)
        setNotice(QStringLiteral("Showing the first %1 of %2.").arg(humanSize(kMaxTextBytes), humanSize(size)));
    return QString::fromUtf8(bytes);
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
#ifdef RELAY_HAVE_SYNTAX_HIGHLIGHTING
    const KSyntaxHighlighting::Definition definition = d->repository.definitionForFileName(path);
    if (definition.isValid()) {
        d->highlighter = new KSyntaxHighlighting::SyntaxHighlighter(m_textView->document());
        KSyntaxHighlighting::Theme theme = d->repository.theme(QStringLiteral("Breeze Dark"));
        if (!theme.isValid()) theme = d->repository.defaultTheme(KSyntaxHighlighting::Repository::DarkTheme);
        d->highlighter->setTheme(theme);
        d->highlighter->setDefinition(definition);
    }
#endif
    m_kind = Kind::Text;
    m_stack->setCurrentWidget(m_textView);
}

void FilePreview::showMarkdown(const QString &path, qint64 size) {
    const QString content = readCapped(path, size);
    m_textView->setPlainText(content);
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
        QTextCharFormat bullet; bullet.setForeground(relay::theme::Warning);
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
    KSyntaxHighlighting::SyntaxHighlighter *highlighter = nullptr;
#endif
    QSyntaxHighlighter *fallback = nullptr;
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
    auto *execute = new QPushButton(QStringLiteral("Execute"));
    execute->setObjectName(QStringLiteral("planExecute"));
    execute->setToolTip(QStringLiteral("Switch to build mode and carry out this plan (your edits are saved first)"));
    auto *fresh = new QPushButton(QStringLiteral("Execute in fresh context"));
    fresh->setToolTip(QStringLiteral("Start a new conversation that only has this plan"));
    auto *keep = new QPushButton(QStringLiteral("Keep planning"));
    actions->addWidget(execute); actions->addWidget(fresh); actions->addStretch(1); actions->addWidget(keep);
    layout->addWidget(m_planActions);
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
        d->highlighter = new KSyntaxHighlighting::SyntaxHighlighter(m_editor->document());
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

PlanEditor::~PlanEditor() { delete d; }

bool PlanEditor::open(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { m_notice->setText(QStringLiteral("Could not open %1").arg(path)); m_notice->show(); return false; }
    if (file.size() > FilePreview::kMaxTextBytes) { m_notice->setText(QStringLiteral("File is too large to edit here.")); m_notice->show(); return false; }
    m_path = QFileInfo(path).absoluteFilePath();
    m_editor->setPlainText(QString::fromUtf8(file.readAll()));
    m_editor->document()->setModified(false);
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
