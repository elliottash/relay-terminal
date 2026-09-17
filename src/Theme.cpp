// SPDX-License-Identifier: GPL-3.0-or-later
#include "Theme.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QList>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QLabel>
#include <QLayout>
#include <QPalette>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSplitter>
#include <QStyle>
#include <QStyleFactory>

#ifndef RELAY_DATA_DIR
#define RELAY_DATA_DIR "/usr/local/share/relay"
#endif
#ifndef RELAY_SOURCE_DIR
#define RELAY_SOURCE_DIR "."
#endif

namespace relay::theme {
namespace {
QString hex(const QColor &c) { return c.name(QColor::HexRgb); }

QString monoFamily() {
    const auto families = QFontDatabase().families();
    for (const auto &name : {QStringLiteral("Hack"), QStringLiteral("JetBrains Mono"), QStringLiteral("DejaVu Sans Mono")})
        if (families.contains(name)) return name;
    return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
}
}

QString themeDataDir() {
    // QCoreApplication::applicationDirPath() needs an application object; /proc does not.
    const QString appDir = QFileInfo(QFileInfo(QStringLiteral("/proc/self/exe")).symLinkTarget()).absolutePath();
    const QStringList choices{qEnvironmentVariable("RELAY_THEME_DIR"),
        appDir + QStringLiteral("/../share/relay/theme"),
        QStringLiteral(RELAY_DATA_DIR "/theme"), QStringLiteral(RELAY_SOURCE_DIR "/data/theme")};
    for (const auto &path : choices) {
        if (!path.isEmpty() && QFileInfo::exists(path + QStringLiteral("/konsole/RelayDark.colorscheme")))
            return QDir(path).absolutePath();
    }
    return {};
}

namespace {
struct SavedVariable { QByteArray name, value; bool set; };
QList<SavedVariable> &savedXdg() { static QList<SavedVariable> saved; return saved; }

void prepend(const char *name, const QString &dir, const char *fallback) {
    const bool set = qEnvironmentVariableIsSet(name);
    const QByteArray value = qgetenv(name);
    savedXdg().append({QByteArray(name), value, set});
    const QByteArray base = value.isEmpty() ? QByteArray(fallback) : value;
    qputenv(name, QFile::encodeName(dir) + ':' + base);
}
}

bool exposeKonsoleProfile() {
    const QString dir = themeDataDir();
    if (dir.isEmpty() || !savedXdg().isEmpty()) return false;
    prepend("XDG_CONFIG_DIRS", dir, "/etc/xdg");
    prepend("XDG_DATA_DIRS", dir, "/usr/local/share:/usr/share");
    return true;
}

void restoreXdgEnvironment() {
    for (const auto &variable : savedXdg()) {
        if (variable.set) qputenv(variable.name.constData(), variable.value);
        else qunsetenv(variable.name.constData());
    }
    savedXdg().clear();
}

void applyDarkTheme(QApplication &app) {
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette p;
    p.setColor(QPalette::Window, Background);
    p.setColor(QPalette::WindowText, Text);
    p.setColor(QPalette::Base, Surface);
    p.setColor(QPalette::AlternateBase, SurfaceRaised);
    p.setColor(QPalette::Text, Text);
    p.setColor(QPalette::PlaceholderText, TextMuted);
    p.setColor(QPalette::Button, SurfaceRaised);
    p.setColor(QPalette::ButtonText, Text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::ToolTipBase, SurfaceRaised);
    p.setColor(QPalette::ToolTipText, Text);
    p.setColor(QPalette::Highlight, Accent.darker(160));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, Accent);
    p.setColor(QPalette::Light, Border.lighter(130));
    p.setColor(QPalette::Midlight, Border);
    p.setColor(QPalette::Mid, Border);
    p.setColor(QPalette::Dark, Background.darker(130));
    p.setColor(QPalette::Shadow, Qt::black);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, TextMuted.darker(140));
    app.setPalette(p);

    const QString mono = monoFamily();
    QString css = QStringLiteral(R"(
* { outline: none; }
QMainWindow, QDialog, QMessageBox { background: @bg; color: @text; }
QToolTip { background: @raised; color: @text; border: 1px solid @border; padding: 4px 6px; }

QToolBar { background: @bg; border: none; border-bottom: 1px solid @border; padding: 6px 10px; spacing: 8px; }
QToolBar::separator { background: @border; width: 1px; margin: 6px 4px; }
QToolBar QToolButton { background: transparent; color: @muted; border: 1px solid transparent; border-radius: 6px; padding: 5px 10px; }
QToolBar QToolButton:hover { background: @raised; color: @text; border-color: @border; }
QToolBar QToolButton:checked { background: @accentSoft; color: @accent; border-color: @accentBorder; }
QLabel#brand { color: @text; letter-spacing: 3px; }

QLabel { color: @text; background: transparent; }
QLabel#muted, QLabel#cwd, QLabel#help, QLabel#privacy { color: @muted; }
QLabel#route { color: @accent; font-family: "@mono"; }

QPushButton { background: @raised; color: @text; border: 1px solid @border; border-radius: 6px; padding: 5px 14px; }
QPushButton:hover { border-color: @muted; }
QPushButton:pressed { background: @surface; }
QPushButton:default, QPushButton#primary { background: @accent; color: @accentText; border-color: @accent; font-weight: 600; }
QPushButton:default:hover, QPushButton#primary:hover { background: @accentHover; }
QPushButton:disabled { color: @disabled; border-color: @surface; }

QComboBox { background: @raised; color: @text; border: 1px solid @border; border-radius: 6px; padding: 4px 28px 4px 10px; min-height: 20px; }
QComboBox:hover { border-color: @muted; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox::down-arrow { image: url(@icons/chevron-down.svg); width: 12px; height: 12px; margin-right: 8px; }
QComboBox QAbstractItemView { background: @raised; color: @text; border: 1px solid @border; selection-background-color: @accentSoft; selection-color: @text; padding: 4px; outline: none; }

QLineEdit, QSpinBox, QPlainTextEdit, QTextEdit { background: @surface; color: @text; border: 1px solid @border; border-radius: 6px; padding: 5px 8px; selection-background-color: @selection; selection-color: #ffffff; }
QLineEdit:focus, QSpinBox:focus, QPlainTextEdit:focus, QTextEdit:focus { border-color: @accentBorder; }
QSpinBox::up-button, QSpinBox::down-button { background: transparent; border: none; width: 16px; }
QSpinBox::up-arrow { image: url(@icons/chevron-up.svg); width: 10px; height: 10px; }
QSpinBox::down-arrow { image: url(@icons/chevron-down.svg); width: 10px; height: 10px; }

QDialog QPlainTextEdit { min-height: 64px; font-family: "@mono"; }
QFrame#composer { background: @surface; border: 1px solid @border; border-radius: 10px; }
QPlainTextEdit#composerEditor { background: transparent; border: none; padding: 2px 4px; font-family: "@mono"; font-size: 11pt; }
QPlainTextEdit#agentLog { background: @bg; border: none; font-family: "@mono"; font-size: 10pt; padding: 8px 4px; }
QWidget#agentPanel { background: @bg; border-left: 1px solid @border; }

QCheckBox { color: @text; spacing: 8px; }
QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid @border; border-radius: 4px; background: @surface; }
QCheckBox::indicator:hover { border-color: @muted; }
QCheckBox::indicator:checked { background: @accent; border-color: @accent; image: url(@icons/check.svg); }

QSplitter::handle { background: @bg; }
/* Agent queue strip */
QFrame#queueStrip { background: @surface; border: 1px solid @border; border-radius: 8px; }
QLabel#queueTitle { color: @muted; font-weight: 600; letter-spacing: 1px; }
QLabel#queueRunning { color: @accent; }
QLabel#queueItem { color: @text; }
QLabel#queueSteer { color: #c9a6ff; }
QLabel#opaqueHint { color: #e5c07b; }
/* Agent sessions: plan chip, context indicator, plan editor */
QLabel#planChip { color: #1b1530; background: #c9a6ff; border-radius: 4px; padding: 1px 6px; font-weight: 700; letter-spacing: 1px; font-size: 8pt; }
QLabel#contextLabel { color: @muted; font-family: "@mono"; font-size: 9pt; padding: 0 4px; }
QLabel#contextLabel[warn="true"] { color: #e5c07b; }
QToolButton#requestsChip { color: @muted; border: 1px solid @border; border-radius: 4px; padding: 0 6px; font-size: 9pt; background: transparent; }
QToolButton#requestsChip[state="running"] { color: @text; }
QToolButton#requestsChip[state="done"] { color: #7ec88c; border-color: #3f6b48; }
QToolButton#requestsChip[state="attention"] { color: #e5c07b; border-color: #6b5a33; }
QToolButton#requestsChip:hover { color: @text; border-color: @accent; }
QPlainTextEdit#planText { background: @bg; border: none; font-family: "@mono"; font-size: 10pt; padding: 8px; }
QLabel#planNotice { color: @muted; }
QFrame#queueStrip QToolButton { color: @muted; border: 1px solid transparent; border-radius: 4px; padding: 1px 6px; }
QFrame#queueStrip QToolButton:hover { color: @text; border-color: @border; }
QFrame#paneBanner { background: @raised; border: 1px solid #b0603a; border-radius: 8px; }
QFrame#paneBanner QLabel { color: @text; }
QFrame#transcript { background: @surface; border: 1px solid @accentBorder; border-radius: 8px; }
/* Thinking floats over the terminal, so it stays quiet: dark gray chrome, not the accent, and a
   background below @surface so it reads as behind the output rather than on top of it. */
QFrame#thinkingOverlay { background: @bg; border: 1px solid @border; border-radius: 8px; }
QLabel#transcriptHeader { color: @muted; }
QPlainTextEdit#transcriptView { background: transparent; border: none; }
QPlainTextEdit#thinkingView { background: transparent; border: none; color: @muted; font-family: "@mono"; font-size: 10pt; }
QLabel#toast { background: @raised; color: @text; border: 1px solid @accentBorder; border-radius: 8px; padding: 6px 12px; }
QWidget#sidebar { background: @surface; border: 1px solid @border; border-radius: 10px; }
QLabel#paletteTitle { color: @muted; font-weight: 600; letter-spacing: 1px; padding: 2px 4px; }
QTreeWidget#paletteList { background: transparent; border: none; outline: none; font-size: 10pt; }
QTreeWidget#paletteList::item { padding: 6px 4px; color: @text; }
QTreeWidget#paletteList::item:selected { background: @raised; color: @text; }
QToolButton#interruptButton { border: 1px solid @border; border-radius: 6px; padding: 4px; background: transparent; }
QToolButton#interruptButton:hover { border-color: @accent; }
QWidget#pane { background: @bg; border: 1px solid @border; border-radius: 6px; }
QWidget#pane[relayActive="true"] { border: 1px solid @accent; }
/* Pane button row, drop zones, tab bar controls */
QFrame#helpCard { background: @raised; border: 1px solid @border; border-radius: 8px; }
QLabel#keyCap { background: @surface; border: 1px solid @border; border-radius: 4px; padding: 1px 6px; color: @text; font-size: 11px; min-width: 14px; }
QLabel#helpText { color: @muted; font-size: 12px; }
QLabel#helpFooter { color: @muted; font-size: 11px; padding-top: 6px; border-top: 1px solid @border; }

/* Warp-style chips in the composer's status strip: a slightly raised rectangle each. */
QToolButton#stripChip { background: @raised; border: 1px solid @border; border-radius: 6px; padding: 2px 8px;
                        color: @muted; font-size: 11px; }
QToolButton#stripChip:hover { color: @text; border-color: @accent; }
/* The mode chip takes the destination's colour, like the caret. */
QToolButton#stripChip[dest="shell"] { color: #3ec5f0; border-color: #3ec5f0; }
QToolButton#stripChip[dest="agent"] { color: #b48ef7; border-color: #b48ef7; }
QToolButton#stripChip::menu-indicator { image: none; width: 0; }
/* Voice: the microphone chip while a recording is running, with the elapsed time beside it. */
QToolButton#stripChip[recording="true"] { color: #f7768e; border-color: #f7768e; }
QLabel#stripChipLabel { background: @raised; border: 1px solid @border; border-radius: 6px; padding: 2px 8px;
                        color: @muted; font-size: 11px; }
QLabel#stripChipLabel[warn="true"] { color: #e0af68; border-color: #e0af68; }

/* The composer's status strip: dim, flat, no dropdown chrome (Warp keeps its chips quiet). */
QComboBox#statusPicker { background: @raised; border: 1px solid @border; border-radius: 6px; color: @muted; padding: 2px 18px 2px 8px; font-size: 11px; }
QComboBox#statusPicker:hover { color: @text; border-color: @accent; }
QComboBox#statusPicker::drop-down { border: none; width: 12px; }
QComboBox#statusPicker QAbstractItemView { background: @raised; color: @text; selection-background-color: @accent; }
QFrame#paneChrome { background: @raised; border: 1px solid @border; border-radius: 6px; }
QLabel#paneGrip { color: @muted; padding: 0 4px; font-size: 11pt; }
QLabel#paneGrip:hover { color: @text; }
QToolButton#paneChromeButton { color: @muted; border: 1px solid transparent; border-radius: 4px; padding: 0 5px; min-width: 16px; }
QToolButton#paneChromeButton:hover { color: @text; border-color: @border; background: @surface; }
QFrame#dropZone { background: @accentSoft; border: 2px solid @accent; border-radius: 6px; }
/* Window header: Relay's own title bar (frameless window). The tab row carries the Relay icon
   on the left and the bell, the actions gear and the window buttons on the right. */
QMainWindow#relayWindow { background: @bg; border: 1px solid @border; }
QWidget#windowChromeLeft, QWidget#windowChromeRight { background: @bg; }
QLabel#windowIcon { color: @accent; font-size: 13pt; padding: 0 2px; }
/* ChromeButton paints its own glyph and hover; the stylesheet only clears the tool-button frame. */
QToolButton#windowChromeButton, QToolButton#windowCloseButton { background: transparent; border: none; padding: 0; }
/* Notification centre (the bell) */
QFrame#notificationsPopup { background: @surface; border: 1px solid @border; border-radius: 10px; }
QScrollArea#notificationsScroll { background: transparent; border: none; }
QScrollArea#notificationsScroll > QWidget > QWidget { background: transparent; }
QFrame#notificationRow { background: @bg; border: 1px solid @border; border-radius: 8px; }
QFrame#notificationRow:hover { border-color: @accentBorder; }
QLabel#notificationTitle { color: @text; font-weight: 600; }
QLabel#notificationBody { color: @muted; font-size: 9pt; }
QLabel#notificationTime { color: @muted; font-size: 8pt; padding-left: 8px; }
QLabel#notificationDot { color: @muted; font-size: 8pt; }
QLabel#notificationDot[kind="success"] { color: #7ec88c; }
QLabel#notificationDot[kind="warning"] { color: #e5c07b; }
QLabel#notificationDot[kind="error"] { color: #e06c75; }
QToolButton#notificationDismiss { color: @muted; border: none; background: transparent; padding: 0 4px; font-size: 9pt; }
QToolButton#notificationDismiss:hover { color: @text; }
QToolButton#popupTextButton { color: @muted; border: 1px solid transparent; border-radius: 4px; padding: 2px 8px; font-size: 9pt; }
QToolButton#popupTextButton:hover { color: @text; border-color: @border; background: @raised; }
QToolButton#popupTextButton:disabled { color: @disabled; }
QToolButton#newTabButton { color: @muted; border: 1px solid transparent; border-radius: 6px; font-size: 12pt; padding: 0; }
QToolButton#newTabButton:hover { color: @text; border-color: @border; background: @raised; }
QToolButton#tabDetachButton { color: @muted; border: none; background: transparent; padding: 0; }
QToolButton#tabDetachButton:hover { color: @accent; }
/* Composer prefix chip (! terminal, * agent) */
QLabel#prefixChip { border-radius: 4px; padding: 1px 6px; font-weight: 700; font-size: 8pt; letter-spacing: 1px; }
QLabel#prefixChip[kind="shell"] { color: #221a08; background: #e5c07b; }
QLabel#prefixChip[kind="agent"] { color: #06222b; background: #3ec5f0; }
/* Prompt-box-only input: masked password field and the take-control button over the terminal */
QLabel#secretChip { color: #2a1206; background: #e5a06b; border-radius: 4px; padding: 1px 6px; font-weight: 700; font-size: 8pt; letter-spacing: 1px; }
QLineEdit#secretEditor { background: @bg; color: @text; border: 1px solid #e5a06b; border-radius: 6px; padding: 8px; font-family: "@mono"; font-size: 10pt; }
QPushButton#takeControlChip { color: @text; background: @raised; border: 1px solid @accentBorder; border-radius: 6px; padding: 3px 10px; font-size: 9pt; }
QPushButton#takeControlChip:hover { border-color: @accent; }
/* Turn details pane */
QLabel#turnHeader { color: @text; font-weight: 600; padding: 4px 6px; }
QTreeWidget#turnTools { background: @bg; color: @text; border: 1px solid @border; border-radius: 6px; outline: none; }
QTreeWidget#turnTools::item { padding: 3px 2px; }
QTreeWidget#turnTools::item:selected { background: @raised; color: @text; }
QPlainTextEdit#turnLog { background: @surface; color: @text; border: 1px solid @border; border-radius: 6px; font-family: "@mono"; font-size: 9pt; }
QLabel#skillsStatus { color: @muted; }
QSplitter::handle:horizontal { width: 4px; }
QSplitter::handle:vertical { height: 4px; }
QSplitter::handle:hover { background: @border; }

QStatusBar { background: @bg; color: @muted; border-top: 1px solid @border; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: @muted; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle { background: @border; border-radius: 3px; min-height: 24px; min-width: 24px; }
QScrollBar::handle:hover { background: @muted; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QMenu { background: @raised; color: @text; border: 1px solid @border; padding: 4px; }
QMenu::item { padding: 5px 18px; border-radius: 4px; }
QMenu::item:selected { background: @accentSoft; }
QMenu::separator { height: 1px; background: @border; margin: 4px 6px; }

QTabWidget::pane { border: 1px solid @border; }
QTabBar::tab { background: @bg; color: @muted; padding: 6px 12px; border: none; }
QTabBar::tab:selected { color: @text; border-bottom: 2px solid @accent; }

/* File panes */
QWidget#fileExplorer, QWidget#filePreview { background: @bg; }
QLabel#fileExplorerPath, QLabel#filePreviewTitle { color: @text; font-weight: 600; padding: 2px 4px; }
QLabel#filePreviewNotice { color: @muted; background: @surface; border: 1px solid @border; border-radius: 6px; padding: 4px 8px; }
QTreeView#fileExplorerView { background: @bg; color: @text; border: 1px solid @border; border-radius: 6px; outline: none; }
QTreeView#fileExplorerView::item { padding: 3px 2px; }
QTreeView#fileExplorerView::item:selected { background: @raised; color: @text; }
QTreeView#fileExplorerView::item:hover { background: @surface; }
QTreeView#fileExplorerView QHeaderView::section { background: @bg; color: @muted; border: none; border-bottom: 1px solid @border; padding: 4px 6px; }
QPlainTextEdit#filePreviewText, QTextBrowser#filePreviewMarkdown { background: @surface; color: @text; border: 1px solid @border; border-radius: 6px; padding: 6px; }
QScrollArea#filePreviewImageArea { background: @surface; border: 1px solid @border; border-radius: 6px; }
QLabel#filePreviewImage { background: transparent; }
QLabel#filePreviewInfo { color: @text; }
QWidget#fileExplorer QToolButton, QWidget#filePreview QToolButton { color: @muted; border: 1px solid transparent; border-radius: 6px; padding: 3px 8px; }
QWidget#fileExplorer QToolButton:hover, QWidget#filePreview QToolButton:hover { color: @text; border-color: @border; background: @raised; }
QToolButton#fileExplorerHidden:checked { color: @accent; border-color: @accentBorder; }
)");
    QColor accentSoft = Accent; accentSoft.setAlpha(40);
    QColor accentBorder = Accent; accentBorder.setAlpha(150);
    const auto rgba = [](const QColor &c) {
        return QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
    };
    const QList<QPair<QString, QString>> tokens{
        {QStringLiteral("@accentText"), hex(AccentText)}, {QStringLiteral("@accentSoft"), rgba(accentSoft)},
        {QStringLiteral("@accentBorder"), rgba(accentBorder)}, {QStringLiteral("@accentHover"), hex(Accent.lighter(115))},
        {QStringLiteral("@accent"), hex(Accent)}, {QStringLiteral("@selection"), hex(Accent.darker(200))},
        {QStringLiteral("@surface"), hex(Surface)}, {QStringLiteral("@raised"), hex(SurfaceRaised)},
        {QStringLiteral("@border"), hex(Border)}, {QStringLiteral("@muted"), hex(TextMuted)},
        {QStringLiteral("@disabled"), hex(TextMuted.darker(150))}, {QStringLiteral("@text"), hex(Text)},
        {QStringLiteral("@bg"), hex(Background)}, {QStringLiteral("@mono"), mono},
        {QStringLiteral("@icons"), themeDataDir() + QStringLiteral("/icons")}};
    if (themeDataDir().isEmpty()) {
        // Without bundled icons, fall back to the style's own arrows and check marks.
        css.remove(QRegularExpression(QStringLiteral(R"([^\n]*url\(@icons[^\n]*\n)")));
    }
    for (const auto &token : tokens) css.replace(token.first, token.second);
    app.setStyleSheet(css);
}

void polishWindow(QWidget *window) {
    // buildUi() creates these widgets without names; tag them for the stylesheet.
    for (auto *editor : window->findChildren<QPlainTextEdit *>(QStringLiteral("composerEditor"))) {
        if (auto *frame = qobject_cast<QFrame *>(editor->parentWidget())) {
            frame->setObjectName(QStringLiteral("composer"));
            frame->setAttribute(Qt::WA_StyledBackground);
            if (auto *layout = frame->layout()) { layout->setContentsMargins(14, 10, 14, 8); layout->setSpacing(4); }
        }
        // Start compact; RichEditor grows the height as lines are added.
        if (editor->height() > editor->minimumHeight() || editor->maximumHeight() > editor->minimumHeight()) {
            editor->setFixedHeight(editor->minimumHeight());
        }
    }
    for (auto *edit : window->findChildren<QPlainTextEdit *>()) {
        if (edit->isReadOnly() && edit->accessibleName().startsWith(QStringLiteral("Agent conversation"))) {
            edit->setObjectName(QStringLiteral("agentLog"));
            if (auto *panel = edit->parentWidget()) { panel->setObjectName(QStringLiteral("agentPanel")); panel->setAttribute(Qt::WA_StyledBackground); }
        }
    }
    for (auto *label : window->findChildren<QLabel *>()) {
        const auto text = label->text();
        if (text.trimmed() == QStringLiteral("RELAY")) label->setObjectName(QStringLiteral("brand"));
        else if (text.startsWith(QStringLiteral("AUTO ·"))) label->setObjectName(QStringLiteral("route"));
        else if (text.startsWith(QStringLiteral("Shift+Enter"))) label->setObjectName(QStringLiteral("help"));
        else if (text.startsWith(QStringLiteral("Only submitted prompts"))) label->setObjectName(QStringLiteral("privacy"));
        else if (label->textInteractionFlags() & Qt::TextSelectableByMouse) label->setObjectName(QStringLiteral("cwd"));
    }
    // Panes: a thin frame, accented on the focused pane (RelayWindow sets relayActive).
    if (window->property("relayActive").isValid() || QString::fromLatin1(window->metaObject()->className()) == QStringLiteral("QWidget")) {
        if (window->findChild<QPlainTextEdit *>(QStringLiteral("composerEditor"))) {
            window->setObjectName(QStringLiteral("pane"));
            window->setAttribute(Qt::WA_StyledBackground);
        }
    }
    // Re-polish so object-name selectors apply to already-created widgets.
    for (auto *widget : window->findChildren<QWidget *>()) {
        widget->style()->unpolish(widget); widget->style()->polish(widget);
    }
}
}
