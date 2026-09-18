// SPDX-License-Identifier: GPL-3.0-or-later
#include "FilePanes.h"
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

QString tildePath(const QString &path) {
    const QString home = QDir::homePath();
    if (path == home) return QStringLiteral("~");
    if (path.startsWith(home + QLatin1Char('/'))) return QStringLiteral("~") + path.mid(home.size());
    return path;
}
}  // namespace

// ----- explorer right-click menu ------------------------------------------------------------------

QList<FileMenuItem> explorerMenu(FileMenuTarget target, const FileMenuHost &host) {
    QList<FileMenuItem> items;
    auto add = [&items](const char *id, const QString &label, bool enabled = true) {
        items.append({QString::fromLatin1(id), label, enabled});
    };
    auto separate = [&items] {
        if (!items.isEmpty() && !items.last().isSeparator()) items.append({QStringLiteral("-"), QString(), true});
    };

    if (target == FileMenuTarget::Folder) {
        add("open", QStringLiteral("Open"));
        if (host.canNavigateTerminal) add("navigate", QStringLiteral("Navigate here"));
    } else if (target == FileMenuTarget::File) {
        add("open", QStringLiteral("Open"));
        if (host.canPreview) add("preview", QStringLiteral("Open in a preview pane"));
        if (host.canNavigateTerminal) add("navigate", QStringLiteral("Navigate here"));
    } else if (host.canNavigateTerminal) {
        // The empty space below the rows acts on the folder the explorer is showing.
        add("navigate", QStringLiteral("Navigate here"));
    }

    if (target != FileMenuTarget::None) {
        separate();
        add("copyPath", QStringLiteral("Copy path"));
        add("copyRelativePath", QStringLiteral("Copy relative path"));
    }
    separate();
    add("reveal", QStringLiteral("Reveal in file manager"));

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
        if (index.isValid()) activate(m_model->filePath(index));
    });
    // Dolphin-style single click (issue #0C7V). clicked() only fires when press and release land
    // on the same row without a drag, so dragging still selects; the modifiers are the ones from
    // the press, so Ctrl+click and Shift+click only extend the selection.
    connect(m_view, &QTreeView::clicked, this, [this](const QModelIndex &index) {
        if (!m_singleClick || !index.isValid()) return;
        if (m_clickModifiers & (Qt::ControlModifier | Qt::ShiftModifier)) return;
        activate(m_model->filePath(index));
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
    const QFileInfo info(path);
    if (!info.isDir()) return;
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
}

void FileExplorer::setFilter(const QString &text) {
    if (m_filter->text() != text) { const QSignalBlocker block(m_filter); m_filter->setText(text); }
    QString needle = text.trimmed();
    // Wildcard characters would be interpreted by the model; match them literally as "any char".
    needle.replace(QLatin1Char('*'), QLatin1Char('?')).replace(QLatin1Char('['), QLatin1Char('?')).replace(QLatin1Char(']'), QLatin1Char('?'));
    m_model->setNameFilters(needle.isEmpty() ? QStringList() : QStringList{QLatin1Char('*') + needle + QLatin1Char('*')});
    hideUnmatchedFolders();
    const QStringList visible = visiblePaths();
    if (!visible.isEmpty()) m_view->setCurrentIndex(m_model->index(visible.first()));
}

QStringList FileExplorer::visiblePaths() const {
    QStringList paths;
    const QModelIndex rootIndex = m_view->rootIndex();
    for (int row = 0; row < m_model->rowCount(rootIndex); ++row)
        if (!m_view->isRowHidden(row, rootIndex)) paths << m_model->filePath(m_model->index(row, 0, rootIndex));
    return paths;
}

// QFileSystemModel applies name filters to files only; folders stay visible. Hide folders
// that do not match the filter text in the view, and re-apply as rows load.
void FileExplorer::hideUnmatchedFolders() {
    const QString needle = m_filter->text().trimmed();
    const QModelIndex rootIndex = m_view->rootIndex();
    for (int row = 0; row < m_model->rowCount(rootIndex); ++row) {
        const QModelIndex index = m_model->index(row, 0, rootIndex);
        const bool hide = !needle.isEmpty() && m_model->isDir(index)
                          && !m_model->fileName(index).contains(needle, Qt::CaseInsensitive);
        m_view->setRowHidden(row, rootIndex, hide);
    }
}

bool FileExplorer::activateRow(int row) {
    const QModelIndex rootIndex = m_view->rootIndex();
    if (row < 0 || row >= m_model->rowCount(rootIndex)) return false;
    activate(m_model->filePath(m_model->index(row, 0, rootIndex)));
    return true;
}

void FileExplorer::activate(const QString &path) {
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
    m_path->setText(m_path->fontMetrics().elidedText(tildePath(m_root), Qt::ElideLeft, std::max(40, m_path->contentsRect().width() - 8)));
    m_path->setToolTip(m_root);
    m_up->setEnabled(!QDir(m_root).isRoot());
}

QList<FileMenuItem> FileExplorer::menuFor(const QString &path) const {
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
    const QString path = index.isValid() ? m_model->filePath(index) : QString();
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
    if (id == QLatin1String("open")) {
        if (QFileInfo(target).isDir()) setRoot(target);
        // A file "opens" the way the desktop would open it; the preview pane is its own entry.
        else QDesktopServices::openUrl(QUrl::fromLocalFile(target));
    } else if (id == QLatin1String("preview")) {
        if (onOpenInPreview) onOpenInPreview(target);
    } else if (id == QLatin1String("navigate")) {
        const QString directory = QFileInfo(target).isDir() ? target : QFileInfo(target).absolutePath();
        if (onNavigateHere) onNavigateHere(directory);
    } else if (id == QLatin1String("copyPath")) {
        QApplication::clipboard()->setText(target);
    } else if (id == QLatin1String("copyRelativePath")) {
        QApplication::clipboard()->setText(QDir(m_root).relativeFilePath(target));
    } else if (id == QLatin1String("reveal")) {
        // Ask the desktop's file manager to select the entry; fall back to opening the folder.
        const QString folder = QFileInfo(target).isDir() ? target : QFileInfo(target).absolutePath();
        if (!QProcess::startDetached(QStringLiteral("dbus-send"),
                                     {QStringLiteral("--session"), QStringLiteral("--print-reply"),
                                      QStringLiteral("--dest=org.freedesktop.FileManager1"),
                                      QStringLiteral("--type=method_call"), QStringLiteral("/org/freedesktop/FileManager1"),
                                      QStringLiteral("org.freedesktop.FileManager1.ShowItems"),
                                      QStringLiteral("array:string:") + QUrl::fromLocalFile(target).toString(),
                                      QStringLiteral("string:")}))
            QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
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
    const QModelIndex index = m_model->index(path);
    if (index.isValid()) m_view->setCurrentIndex(index);
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
            if (index.isValid()) activate(m_model->filePath(index));
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
            if (index.isValid()) activate(m_model->filePath(index));
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
    header->addWidget(m_title, 1);
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
    m_markdownView->setOpenExternalLinks(true);
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
        if (!m_path.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(m_path));
    });
    connect(m_mode, &QToolButton::clicked, this, [this] {
        if (m_kind == Kind::Markdown) {
            m_markdownSource = !m_markdownSource;
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
}

FilePreview::~FilePreview() {
    delete d;
}

QString FilePreview::title() const {
    return m_path.isEmpty() ? QStringLiteral("Preview") : QFileInfo(m_path).fileName();
}

QString FilePreview::text() const {
    return m_textView->toPlainText();
}

bool FilePreview::open(const QString &path) {
    const QFileInfo info(path);
    if (path.isEmpty() || !info.exists() || !info.isFile() || !info.isReadable()) return false;
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
        QTextCharFormat heading; heading.setForeground(QColor(0x3e, 0xc5, 0xf0)); heading.setFontWeight(QFont::Bold);
        QTextCharFormat bullet; bullet.setForeground(QColor(0xe5, 0xc0, 0x7b));
        QTextCharFormat code; code.setForeground(QColor(0x9a, 0xd1, 0x8b));
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
