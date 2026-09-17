// SPDX-License-Identifier: GPL-3.0-or-later
#include "FilePanes.h"
#include <QTextBlock>
#include <QTextCursor>

#include <QDateTime>
#include <QResizeEvent>
#include <algorithm>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMimeDatabase>
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

// ----- FileExplorer ------------------------------------------------------------------------------

FileExplorer::FileExplorer(const QString &root, QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("fileExplorer"));
    setAttribute(Qt::WA_StyledBackground);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->setSpacing(6);

    auto *header = new QHBoxLayout;
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
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
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
        if (index.isValid()) activate(m_model->filePath(index));
    });
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
    m_filter->installEventFilter(this);
    setRoot(root.isEmpty() ? QDir::homePath() : root);
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

void FileExplorer::updateHeader() {
    m_path->setText(m_path->fontMetrics().elidedText(tildePath(m_root), Qt::ElideLeft, std::max(40, m_path->contentsRect().width() - 8)));
    m_path->setToolTip(m_root);
    m_up->setEnabled(!QDir(m_root).isRoot());
}

bool FileExplorer::eventFilter(QObject *object, QEvent *event) {
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

bool FilePreview::hasSyntaxHighlighting() {
#ifdef RELAY_HAVE_SYNTAX_HIGHLIGHTING
    return true;
#else
    return false;
#endif
}

bool FilePreview::hasPdfSupport() {
#ifdef RELAY_HAVE_QTPDF
    return true;
#else
    return false;
#endif
}

FilePreview::FilePreview(QWidget *parent) : QWidget(parent), d(new Private) {
    setObjectName(QStringLiteral("filePreview"));
    setAttribute(Qt::WA_StyledBackground);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->setSpacing(6);

    auto *header = new QHBoxLayout;
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
        m_mode->setText(m_markdownSource ? QStringLiteral("Rendered") : QStringLiteral("Source"));
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
    if (m_kind == Kind::Markdown) m_stack->setCurrentWidget(m_textView);
    QTextBlock block = m_textView->document()->findBlockByNumber(line - 1);
    if (!block.isValid()) return;
    QTextCursor cursor(block);
    cursor.select(QTextCursor::LineUnderCursor);
    m_textView->setTextCursor(cursor);
    m_textView->centerCursor();
}

}  // namespace relay
