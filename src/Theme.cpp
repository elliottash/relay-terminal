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
#include <QSaveFile>
#include <QSettings>
#include <QSplitter>
#include <QStyle>
#include <QStyleFactory>
#include <QTextStream>

#include <cmath>

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

// Relay's own settings file, addressable before QApplication has set the organisation name.
// NativeFormat, not IniFormat: on Unix the two differ in the file extension (relay.conf against
// relay.ini), and the rest of the app reads the native one.
QSettings relaySettings() {
    return QSettings(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("RelayTerminal"),
                     QStringLiteral("relay"));
}

QString configHomeDir() {
    const QString fromEnv = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (!fromEnv.isEmpty()) return fromEnv;
    const QString home = QDir::homePath();
    return home.isEmpty() ? QString() : home + QStringLiteral("/.config");
}

// Ink that stays legible on a filled chip: a very dark or very light tint of the fill itself,
// so a chip reads the same way in a dark and in a light theme.
QColor inkOn(const QColor &fill) {
    QColor out = fill.toHsl();
    const bool light = (fill.red() * 299 + fill.green() * 587 + fill.blue() * 114) / 1000 > 140;
    out.setHsl(out.hslHue(), qMin(out.hslSaturation(), 200), light ? 22 : 242);
    return out.toRgb();
}

QColor blend(const QColor &a, const QColor &b, double weightOfA) {
    const double w = qBound(0.0, weightOfA, 1.0);
    return QColor(int(std::lround(a.red() * w + b.red() * (1 - w))),
                  int(std::lround(a.green() * w + b.green() * (1 - w))),
                  int(std::lround(a.blue() * w + b.blue() * (1 - w))));
}

QString rgba(const QColor &c) {
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

QColor withAlpha(const QColor &c, int alpha) { QColor out = c; out.setAlpha(alpha); return out; }

// A colour from a table the reader does not know (ThemeSpec::extra), e.g. `[bevel] light`.
QColor extraColor(const ThemeSpec &spec, const QString &key, const QColor &fallback) {
    const QStringList raw = spec.extra.value(key);
    const QColor color = raw.isEmpty() ? QColor() : QColor(raw.first().trimmed());
    return color.isValid() ? color : fallback;
}

// `[flags] bevel = true` (IBM Beige): two-tone moulded edges instead of 1px hairlines, because a
// hairline on a light ground reads as a stray mark. Light on the top and left, dark on the bottom
// and right; inputs, the composer and the pane are sunken (the edges swap), which is what makes a
// text field read as a well rather than a button. Only the surfaces a person touches are
// bevelled; the other rules keep their hairline. Appended after the main sheet so it wins on
// equal specificity, and only for a theme that asks, so every other theme's sheet is unchanged.
QString bevelStylesheet(const ThemeSpec &spec) {
    const QString light = hex(extraColor(spec, QStringLiteral("bevel.light"), SurfaceRaised.lighter(125)));
    const QString dark = hex(extraColor(spec, QStringLiteral("bevel.dark"), SurfaceRaised.darker(160)));
    QString css = QStringLiteral(R"(
QPushButton, QComboBox, QToolButton#stripChip, QLabel#stripChipLabel, QLabel#keyCap,
QFrame#paneChrome[hot="true"], QWidget#sidebar, QFrame#helpCard, QMenu, QFrame#notificationsPopup {
    border-top: 2px solid %1; border-left: 2px solid %1; border-bottom: 2px solid %2; border-right: 2px solid %2; }
QPushButton:pressed, QToolButton#stripChip:pressed {
    border-top: 2px solid %2; border-left: 2px solid %2; border-bottom: 2px solid %1; border-right: 2px solid %1; }
QLineEdit, QSpinBox, QPlainTextEdit, QTextEdit, QFrame#composer, QWidget#pane,
QTreeView#fileExplorerView, QTreeWidget#turnTools, QScrollArea#filePreviewImageArea {
    border-top: 2px solid %2; border-left: 2px solid %2; border-bottom: 2px solid %1; border-right: 2px solid %1; }
QWidget#pane[relayActive="true"] { border: 2px solid @borderStrong; }
QFrame#composer[relayActive="true"] { border: 2px solid @accent; }
)");
    return css.arg(light, dark);
}

// `[flags] metal = true` (Dark Copper): the raised chrome is a milled metal face rather than a
// flat fill. Metal is a gradient and a specular edge — a flat fill of any copper reads as brown
// paint — so every raised surface runs from a lit top to a shaded bottom with one bright line
// along its top edge, and a press turns the light round. The grid is never touched
// (docs/SWITCHBOARD-AESTHETIC.md 2.2), and neither is any surface text is read on: the composer
// and the file panes stay flat, because a gradient behind a caret is a distraction, not a
// material. Colours come from `[metal]`, and both ends of every face carry the theme's contrast
// contract (tests/theme_test.cpp).
QString metalStylesheet(const ThemeSpec &spec) {
    const QString light = hex(extraColor(spec, QStringLiteral("metal.light"), SurfaceRaised.lighter(125)));
    const QString mid = hex(extraColor(spec, QStringLiteral("metal.mid"), SurfaceRaised));
    const QString dark = hex(extraColor(spec, QStringLiteral("metal.dark"), SurfaceRaised.darker(130)));
    const QString edge = hex(extraColor(spec, QStringLiteral("metal.edge"), SurfaceRaised.lighter(190)));
    const QString chromeTop = hex(extraColor(spec, QStringLiteral("metal.chrome_light"), Background.lighter(135)));
    const QString chromeBottom = hex(extraColor(spec, QStringLiteral("metal.chrome_dark"), Background.darker(115)));
    QString css = QStringLiteral(R"(
QPushButton, QComboBox, QToolButton#stripChip, QLabel#stripChipLabel, QLabel#keyCap,
QToolButton#workChip, QFrame#paneChrome[hot="true"], QMenu, QFrame#notificationsPopup,
QFrame#helpCard, QLabel#toast {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 %4, stop:0.09 %1, stop:0.55 %2, stop:1 %3); }
QPushButton:hover, QComboBox:hover, QToolButton#stripChip:hover, QToolButton#workChip:hover {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 %4, stop:0.14 %1, stop:0.6 %1, stop:1 %2); }
QPushButton:pressed, QToolButton#stripChip:pressed, QToolButton#workChip:pressed {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %3, stop:0.9 %2, stop:1 %4); }
QPushButton:disabled { background: %3; }
/* The frame the panes sit in: a shallower sheen, so the chrome reads as one sheet of metal
   with the chips raised out of it rather than as a second set of buttons. */
QToolBar, QTabBar {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %5, stop:1 %6); }
)");
    return css.arg(light, mid, dark, edge, chromeTop, chromeBottom);
}

// --- the theme registry -------------------------------------------------------------------------

struct Registry {
    QMap<QString, QString> files;      // id -> path
    QMap<QString, ThemeSpec> loaded;   // id -> parsed
    bool scanned = false;
};
Registry &registry() { static Registry r; return r; }

ThemeSpec &activeSpec() { static ThemeSpec spec = builtinDark(); return spec; }

void scan() {
    Registry &r = registry();
    r.files = discoverThemeFiles(themeSearchDirs(themeDataDir(), configHomeDir()));
    r.loaded.clear();
    r.scanned = true;
}

const ThemeSpec *loadTheme(const QString &id) {
    Registry &r = registry();
    if (!r.scanned) scan();
    const auto cached = r.loaded.constFind(id);
    if (cached != r.loaded.constEnd()) return &*cached;
    const QString path = r.files.value(id);
    if (path.isEmpty()) return nullptr;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return nullptr;
    QString error;
    ThemeSpec spec = parseTheme(QString::fromUtf8(file.readAll()), id, builtinDark(), &error);
    spec.path = path;
    spec.builtin = !path.startsWith(configHomeDir() + QStringLiteral("/relay/"));
    if (!error.isEmpty())
        fprintf(stderr, "relay: theme %s: %s\n", qPrintable(id), qPrintable(error));
    return &*r.loaded.insert(id, spec);
}

QString settingsThemeId() {
    return relaySettings().value(QStringLiteral("theme/name"), QStringLiteral("relay-dark")).toString();
}

// The theme to use now: the chosen one, Relay Dark, or the compiled-in fallback.
const ThemeSpec &resolveTheme(const QString &wanted) {
    if (const ThemeSpec *spec = loadTheme(wanted)) return *spec;
    if (const ThemeSpec *spec = loadTheme(QStringLiteral("relay-dark"))) return *spec;
    return builtinDark();
}

void adoptTokens(const ThemeSpec &spec) {
    const auto ui = [&spec](const char *name, const QColor &fallback) {
        return spec.uiColor(QString::fromLatin1(name), fallback);
    };
    Background = ui("background", Background);
    Surface = ui("surface", Surface);
    SurfaceRaised = ui("surface_raised", SurfaceRaised);
    Border = ui("border", Border);
    BorderStrong = ui("border_strong", BorderStrong);
    Text = ui("text", Text);
    TextMuted = ui("text_muted", TextMuted);
    Accent = ui("accent", Accent);
    AccentText = ui("accent_text", AccentText);
    Success = ui("success", Success);
    Warning = ui("warning", Warning);
    Error = ui("error", Error);
    Shell = ui("shell", Accent);
    Agent = ui("agent", Agent);

    const auto syntax = [&spec](const char *name, const QColor &fallback) {
        return spec.syntaxColor(QString::fromLatin1(name), fallback);
    };
    SyntaxCommand = syntax("command", Shell);
    SyntaxUnknown = syntax("unknown", Error);
    SyntaxFlag = syntax("flag", Warning);
    SyntaxString = syntax("string", Success);
    SyntaxPath = syntax("path", SyntaxPath);
    SyntaxOperator = syntax("operator", TextMuted);
    SyntaxVariable = syntax("variable", Agent);
    SyntaxAgent = syntax("agent", Agent);
    SyntaxToken = syntax("token", Shell);
    activeSpec() = spec;
}

void applyPalette(QApplication &app, const ThemeSpec &spec) {
    QPalette p;
    p.setColor(QPalette::Window, Background);
    p.setColor(QPalette::WindowText, Text);
    p.setColor(QPalette::Base, Surface);
    p.setColor(QPalette::AlternateBase, SurfaceRaised);
    p.setColor(QPalette::Text, Text);
    p.setColor(QPalette::PlaceholderText, TextMuted);
    p.setColor(QPalette::Button, SurfaceRaised);
    p.setColor(QPalette::ButtonText, Text);
    p.setColor(QPalette::BrightText, spec.isLight() ? QColor(Qt::black) : QColor(Qt::white));
    p.setColor(QPalette::ToolTipBase, SurfaceRaised);
    p.setColor(QPalette::ToolTipText, Text);
    p.setColor(QPalette::Highlight, spec.uiColor(QStringLiteral("selection"), Accent.darker(160)));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, Accent);
    p.setColor(QPalette::Light, Border.lighter(130));
    p.setColor(QPalette::Midlight, Border);
    p.setColor(QPalette::Mid, Border);
    p.setColor(QPalette::Dark, spec.isLight() ? Border.darker(115) : Background.darker(130));
    p.setColor(QPalette::Shadow, spec.isLight() ? QColor(0xb0, 0xb5, 0xc0) : QColor(Qt::black));
    const QColor disabled = spec.uiColor(QStringLiteral("disabled"), TextMuted.darker(140));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, disabled);
    app.setPalette(p);
}

QString stylesheetFor(const ThemeSpec &spec) {
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
/* Pane header (issue JRWQ): the session title, the "auto" badge and the directory beside it. */
QLabel#paneTitle { color: @text; font-weight: 600; }
QLabel#paneCwd { color: @muted; font-size: 11px; }
QLabel#paneAuto { color: @muted; font-size: 9px; letter-spacing: 1px; border: 1px solid @border; border-radius: 4px; padding: 0 4px; }
QLineEdit#paneTitleEdit { background: @surface; color: @text; border: 1px solid @accentBorder; border-radius: 4px; padding: 1px 6px; }

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

QLineEdit, QSpinBox, QPlainTextEdit, QTextEdit { background: @surface; color: @text; border: 1px solid @border; border-radius: 6px; padding: 5px 8px; selection-background-color: @selection; selection-color: @selectionText; }
QLineEdit:focus, QSpinBox:focus, QPlainTextEdit:focus, QTextEdit:focus { border-color: @accentBorder; }
QSpinBox::up-button, QSpinBox::down-button { background: transparent; border: none; width: 16px; }
QSpinBox::up-arrow { image: url(@icons/chevron-up.svg); width: 10px; height: 10px; }
QSpinBox::down-arrow { image: url(@icons/chevron-down.svg); width: 10px; height: 10px; }

QDialog QPlainTextEdit { min-height: 64px; font-family: "@mono"; }
QFrame#composer { background: @surface; border: 1px solid @border; border-radius: 10px; }
QFrame#composer[relayActive="true"] { background: @raised; border: 1px solid @accentBorder; }
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
QLabel#queueSteer { color: @agent; }
QLabel#opaqueHint { color: @warning; }
/* Agent sessions: plan chip, context indicator, plan editor */
QLabel#planChip { color: @onAgent; background: @agent; border-radius: 4px; padding: 1px 6px; font-weight: 700; letter-spacing: 1px; font-size: 8pt; }
QLabel#contextLabel { color: @muted; font-family: "@mono"; font-size: 9pt; padding: 0 4px; }
QLabel#contextLabel[warn="true"] { color: @warning; }
QToolButton#workChip { color: @muted; border: 1px solid @border; border-radius: 6px; padding: 2px 8px; font-size: 11px; background: @raised; min-height: 16px; }
QToolButton#workChip[state="running"] { color: @text; }
QToolButton#workChip[state="done"] { color: @success; border-color: @successBorder; }
QToolButton#workChip[state="attention"] { color: @warning; border-color: @warningBorder; }
QToolButton#workChip:hover { color: @text; border-color: @accent; }
QToolButton#workChip::menu-indicator { image: none; width: 0; }
QPlainTextEdit#planText { background: @bg; border: none; font-family: "@mono"; font-size: 10pt; padding: 8px; }
QLabel#planNotice { color: @muted; }
QFrame#queueStrip QToolButton { color: @muted; border: 1px solid transparent; border-radius: 4px; padding: 1px 6px; }
QFrame#queueStrip QToolButton:hover { color: @text; border-color: @border; }
QFrame#paneBanner { background: @raised; border: 1px solid @caution; border-radius: 8px; }
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
/* The requests ledger and a subagent transcript float over a pane; both are opaque on purpose. */
QWidget#requestsPanel, QWidget#subagentTranscript { background: @bg; border: 1px solid @border; border-radius: 8px; }
QWidget#requestsPanel QLabel#panelKeys, QWidget#subagentTranscript QLabel#panelKeys { color: @muted; }
QTreeWidget#requestsList { background: transparent; border: none; outline: none; }
QToolButton#interruptButton { border: 1px solid @border; border-radius: 6px; padding: 4px; background: transparent; }
QToolButton#interruptButton:hover { border-color: @accent; }
QWidget#pane { background: @bg; border: 1px solid @border; border-radius: 8px; }
QWidget#pane[relayActive="true"] { border: 1px solid @borderStrong; }
/* Pane button row, drop zones, tab bar controls */
QFrame#helpCard { background: @raised; border: 1px solid @border; border-radius: 8px; }
QLabel#keyCap { background: @surface; border: 1px solid @border; border-radius: 4px; padding: 1px 6px; color: @text; font-size: 11px; min-width: 14px; }
QLabel#helpText { color: @muted; font-size: 12px; }
QLabel#helpFooter { color: @muted; font-size: 11px; padding-top: 6px; border-top: 1px solid @border; }

/* Warp-style chips in the composer's status strip: a slightly raised rectangle each. The
   min-height is a line of the chips' 11px text, so an icon-only chip (Switchboard, tasks,
   microphone, share) is exactly as tall as the text chip beside it (2026-09-17). */
QToolButton#stripChip { background: @raised; border: 1px solid @border; border-radius: 6px; padding: 2px 8px;
                        color: @muted; font-size: 11px; min-height: 16px; }
QToolButton#stripChip:hover { color: @text; border-color: @accent; }
/* The mode chip takes the destination's colour, like the caret. */
QToolButton#stripChip[dest="shell"] { color: @shell; border-color: @shell; }
QToolButton#stripChip[dest="agent"] { color: @agent; border-color: @agent; }
/* Wrong-mode hints: a blinking fill while the chip suggests the other input mode (Pane::flashModeChip). */
QToolButton#stripChip[flash="agent"] { color: @agent; border-color: @agent; background: @agentSoft; }
QToolButton#stripChip[flash="shell"] { color: @shell; border-color: @shell; background: @shellSoft; }
QToolButton#stripChip::menu-indicator { image: none; width: 0; }
/* Voice: the microphone chip while a recording is running, with the elapsed time beside it. */
QToolButton#stripChip[recording="true"] { color: @error; border-color: @error; }
QLabel#stripChipLabel { background: @raised; border: 1px solid @border; border-radius: 6px; padding: 2px 8px;
                        color: @muted; font-size: 11px; }
QLabel#stripChipLabel[warn="true"] { color: @warning; border-color: @warning; }

/* The composer's status strip: dim, flat, no dropdown chrome (Warp keeps its chips quiet). */
QComboBox#statusPicker { background: @raised; border: 1px solid @border; border-radius: 6px; color: @muted; padding: 2px 18px 2px 8px; font-size: 11px; }
QComboBox#statusPicker:hover { color: @text; border-color: @accent; }
QComboBox#statusPicker::drop-down { border: none; width: 12px; }
QComboBox#statusPicker QAbstractItemView { background: @raised; color: @text; selection-background-color: @accent; }
/* The pane's button row. It is on screen in every pane, so at rest it is three quiet glyphs on
   the pane's own ground; the pane under the mouse ("hot") gets the raised tile, the grip and the
   second split button. */
QFrame#paneChrome { background: transparent; border: 1px solid transparent; border-radius: 6px; }
QFrame#paneChrome[hot="true"] { background: @raised; border-color: @border; }
QLabel#paneGrip { color: @muted; padding: 0 4px; font-size: 11pt; }
QLabel#paneGrip:hover { color: @text; }
QToolButton#paneChromeButton { color: @disabled; border: 1px solid transparent; border-radius: 4px; padding: 0 5px; min-width: 16px; }
QFrame#paneChrome[hot="true"] QToolButton#paneChromeButton { color: @muted; }
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
QLabel#notificationDot[kind="success"] { color: @success; }
QLabel#notificationDot[kind="warning"] { color: @warning; }
QLabel#notificationDot[kind="error"] { color: @error; }
QToolButton#notificationDismiss { color: @muted; border: none; background: transparent; padding: 0 4px; font-size: 9pt; }
QToolButton#notificationDismiss:hover { color: @text; }
QToolButton#popupTextButton { color: @muted; border: 1px solid transparent; border-radius: 4px; padding: 2px 8px; font-size: 9pt; }
QToolButton#popupTextButton:hover { color: @text; border-color: @border; background: @raised; }
QToolButton#popupTextButton:disabled { color: @disabled; }
QToolButton#newTabButton, QToolButton#tabCloseButton { background: transparent; border: none; padding: 0; }
QToolButton#tabDetachButton { color: @muted; border: none; background: transparent; padding: 0; }
QToolButton#tabDetachButton:hover { color: @accent; }
/* Composer prefix chip (! terminal, * agent) */
QLabel#prefixChip { border-radius: 4px; padding: 1px 6px; font-weight: 700; font-size: 8pt; letter-spacing: 1px; }
QLabel#prefixChip[kind="shell"] { color: @onWarning; background: @warning; }
QLabel#prefixChip[kind="agent"] { color: @onShell; background: @shell; }
/* Prompt-box-only input: masked password field and the take-control button over the terminal */
QLabel#secretChip { color: @onCaution; background: @caution; border-radius: 4px; padding: 1px 6px; font-weight: 700; font-size: 8pt; letter-spacing: 1px; }
QLineEdit#secretEditor { background: @bg; color: @text; border: 1px solid @caution; border-radius: 6px; padding: 8px; font-family: "@mono"; font-size: 10pt; }
QPushButton#takeControlChip { color: @text; background: @raised; border: 1px solid @accentBorder; border-radius: 6px; padding: 3px 10px; font-size: 9pt; }
QPushButton#takeControlChip:hover { border-color: @accent; }
/* The floating banner over the terminal: what the program is asking, and who answers it */
QFrame#programBanner { background: @surface; border: 1px solid @accentBorder; border-radius: 8px; }
QLabel#programBannerLabel { color: @text; font-size: 9pt; }
QPushButton#delegateChip { color: @text; background: @raised; border: 1px solid @accentBorder; border-radius: 6px; padding: 3px 10px; font-size: 9pt; }
QPushButton#delegateChip:hover { border-color: @accent; }
QPushButton#delegateChip:disabled { color: @muted; border-color: @border; }
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
QMenuBar { background: @bg; color: @text; }
QMenuBar::item { background: transparent; padding: 4px 8px; }
QMenuBar::item:selected { background: @accentSoft; }
QListWidget { background: @surface; color: @text; border: 1px solid @border; border-radius: 6px; outline: none; }
QListWidget::item { padding: 5px 6px; }
QListWidget::item:selected { background: @accentSoft; color: @text; }
QGroupBox { border: 1px solid @border; border-radius: 6px; margin-top: 8px; padding-top: 6px; }
QGroupBox::title { color: @muted; subcontrol-origin: margin; left: 8px; padding: 0 4px; }

/* The tab row is the title bar, so it carries no frame of its own: the pane below draws
   its own outline, and QTabWidget would otherwise trace a box around the corner widgets. */
QTabWidget::pane { border: none; }
QTabBar { background: @bg; }
QTabBar::tab { background: transparent; color: @muted; padding: 5px 8px; margin: 3px 1px 0 1px;
               border: none; border-bottom: 2px solid transparent;
               border-top-left-radius: 6px; border-top-right-radius: 6px; }
QTabBar::tab:hover { color: @text; background: @surface; }
QTabBar::tab:selected { color: @text; background: @surface; border-bottom: 2px solid @accent; }

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

/* Switchboard (src/BoardPane.cpp; docs/SWITCHBOARD-AESTHETIC.md intervention 3): flat @surface
   columns with engraved (uppercase, mono, letter-spaced) headers on the pane's @bg. The cards
   themselves are painted by CardDelegate from the same tokens, so they follow a theme switch. */
QWidget#boardView, QWidget#boardColumns, QScrollArea#boardScroll { background: @bg; }
QTabBar#boardTabs::tab { padding: 4px 9px; margin: 0 2px 0 0; }
QLineEdit#boardFilter { padding: 4px 8px; }
QToolButton#boardAddButton { background: @raised; color: @text; border: 1px solid @border; border-radius: 6px; padding: 4px 10px; }
QToolButton#boardAddButton:hover { border-color: @accent; }
QFrame#boardColumnFrame { background: @surface; border: 1px solid @border; border-radius: 8px; }
QLabel#boardColumnHeader { color: @muted; font-family: "@mono"; font-size: 8pt; font-weight: 600; letter-spacing: 1px; }
QLabel#boardColumnCount { color: @muted; font-family: "@mono"; font-size: 8pt; }
QToolButton#boardColumnAdd { color: @muted; background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 0 5px; }
QToolButton#boardColumnAdd:hover { color: @text; border-color: @border; background: @raised; }
QListWidget#boardColumn { background: transparent; border: none; }
QListWidget#boardColumn QScrollBar:vertical { width: 6px; margin: 0; }
QLineEdit#boardQuickAdd { background: @raised; border-color: @accentBorder; }
QLabel#boardProblems { color: @warning; border: 1px solid @warningBorder; border-radius: 6px; padding: 4px 8px; }
QFrame#boardNotice { background: @raised; border: 1px solid @accentBorder; border-radius: 6px; }
QFrame#boardNotice[error="true"] { border-color: @error; }
QLabel#boardNoticeText { color: @text; }
QToolButton#boardTextButton { color: @muted; background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 2px 8px; }
QToolButton#boardTextButton:hover { color: @text; border-color: @border; background: @raised; }
QToolButton#boardTextButton:disabled { color: @disabled; }
QLabel#boardEmpty { color: @muted; }
QLabel#boardKeys { color: @muted; font-size: 8pt; padding: 4px 10px; border-top: 1px solid @border; }
QWidget#boardDetail { background: @bg; }
QLabel#boardCardRef { color: @muted; font-family: "@mono"; }
QLabel#boardCardTitle { color: @text; font-size: 12pt; font-weight: 600; }
QToolButton#boardCardClose { color: @muted; background: transparent; border: 1px solid transparent; border-radius: 4px; font-size: 12pt; padding: 0 6px; }
QToolButton#boardCardClose:hover { color: @text; border-color: @border; background: @raised; }
QComboBox#boardPicker { padding: 2px 26px 2px 8px; min-height: 18px; }
QLabel#boardCardMeta { color: @text; font-size: 9pt; }
QTextBrowser#boardCardDocument { background: @surface; color: @text; border: 1px solid @border; border-radius: 8px; padding: 0; }
QLabel#boardCardError { color: @error; }
QFrame#boardReply { background: @surface; border: 1px solid @border; border-radius: 8px; }
QPlainTextEdit#boardReplyEditor { background: transparent; border: none; padding: 2px; }
QPushButton#boardReplyButton, QFrame#boardReply QPushButton#primary { padding: 4px 12px; }
)");
    const QColor selection = spec.uiColor(QStringLiteral("selection"), Accent.darker(200));
    const QColor caution = blend(Warning, Error, 0.7);
    // The @on… tokens come first: replacing @warning before @onWarning would eat the prefix.
    const QList<QPair<QString, QString>> tokens{
        {QStringLiteral("@onWarning"), hex(inkOn(Warning))}, {QStringLiteral("@onCaution"), hex(inkOn(caution))},
        {QStringLiteral("@onShell"), hex(inkOn(Shell))}, {QStringLiteral("@onAgent"), hex(inkOn(Agent))},
        {QStringLiteral("@onSuccess"), hex(inkOn(Success))}, {QStringLiteral("@onError"), hex(inkOn(Error))},
        {QStringLiteral("@accentText"), hex(AccentText)},
        {QStringLiteral("@accentSoft"), rgba(withAlpha(Accent, 40))},
        {QStringLiteral("@accentBorder"), rgba(withAlpha(Accent, 150))},
        {QStringLiteral("@accentHover"), hex(spec.uiColor(QStringLiteral("accent_hover"), Accent.lighter(115)))},
        {QStringLiteral("@accent"), hex(Accent)},
        {QStringLiteral("@selectionText"), hex(inkOn(selection))}, {QStringLiteral("@selection"), hex(selection)},
        {QStringLiteral("@surface"), hex(Surface)}, {QStringLiteral("@raised"), hex(SurfaceRaised)},
        {QStringLiteral("@borderStrong"), hex(BorderStrong)}, {QStringLiteral("@border"), hex(Border)},
        {QStringLiteral("@muted"), hex(TextMuted)},
        {QStringLiteral("@disabled"), hex(spec.uiColor(QStringLiteral("disabled"), TextMuted.darker(150)))},
        {QStringLiteral("@text"), hex(Text)}, {QStringLiteral("@bg"), hex(Background)},
        {QStringLiteral("@successBorder"), hex(blend(Success, Background, 0.5))},
        {QStringLiteral("@warningBorder"), hex(blend(Warning, Background, 0.5))},
        {QStringLiteral("@success"), hex(Success)}, {QStringLiteral("@warning"), hex(Warning)},
        {QStringLiteral("@error"), hex(Error)}, {QStringLiteral("@caution"), hex(caution)},
        {QStringLiteral("@shellSoft"), rgba(withAlpha(Shell, 56))}, {QStringLiteral("@shell"), hex(Shell)},
        {QStringLiteral("@agentSoft"), rgba(withAlpha(Agent, 56))}, {QStringLiteral("@agent"), hex(Agent)},
        {QStringLiteral("@mono"), mono},
        {QStringLiteral("@icons"), themeDataDir() + QStringLiteral("/icons")}};
    // Per-theme chrome switches ([flags] in the theme file). A theme that sets neither gets the
    // sheet above unchanged.
    if (spec.flag(QStringLiteral("bevel"))) css += bevelStylesheet(spec);
    if (spec.flag(QStringLiteral("metal"))) css += metalStylesheet(spec);
    if (themeDataDir().isEmpty()) {
        // Without bundled icons, fall back to the style's own arrows and check marks.
        css.remove(QRegularExpression(QStringLiteral(R"([^\n]*url\(@icons[^\n]*\n)")));
    }
    for (const auto &token : tokens) css.replace(token.first, token.second);
    // `[flags] square = true`: no rounded corners anywhere, as on the owner's other sites and on
    // a 1995 desktop. One pass over the finished sheet rather than a token in every rule.
    if (spec.flag(QStringLiteral("square")))
        css.replace(QRegularExpression(QStringLiteral(R"(\b(border(?:-(?:top|bottom)-(?:left|right))?-radius):\s*\d+px)")),
                    QStringLiteral("\\1: 0"));
    return css;
}

}  // namespace

Notifier *notifier() { static Notifier n; return &n; }

const ThemeSpec &active() { return activeSpec(); }
QString activeThemeId() { return activeSpec().id; }
void refreshThemes() { scan(); }

QList<ThemeChoice> availableThemes() {
    if (!registry().scanned) scan();
    QList<ThemeChoice> out;
    QStringList ids = registry().files.keys();
    if (ids.isEmpty()) ids << QStringLiteral("relay-dark");
    for (const QString &id : ids) {
        const ThemeSpec *spec = loadTheme(id);
        const ThemeSpec &theme = spec ? *spec : builtinDark();
        out.append({id, theme.name.isEmpty() ? id : theme.name, theme.description, theme.builtin});
    }
    return out;
}

QString themeDataDir() {
    // QCoreApplication::applicationDirPath() needs an application object; /proc does not.
    const QString appDir = QFileInfo(QFileInfo(QStringLiteral("/proc/self/exe")).symLinkTarget()).absolutePath();
    const QStringList choices{qEnvironmentVariable("RELAY_THEME_DIR"),
        appDir + QStringLiteral("/../share/relay/theme"),
        QStringLiteral(RELAY_DATA_DIR "/theme"), QStringLiteral(RELAY_SOURCE_DIR "/data/theme")};
    for (const auto &path : choices) {
        if (!path.isEmpty() && QFileInfo::exists(path + QStringLiteral("/themes/relay-dark.toml")))
            return QDir(path).absolutePath();
    }
    return {};
}

void applyTheme(QApplication &app) {
    const ThemeSpec spec = resolveTheme(settingsThemeId());   // a copy: GCC cannot see the reference outlives the call
    adoptTokens(spec);
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    applyPalette(app, spec);
    app.setStyleSheet(stylesheetFor(spec));
}

void applyDarkTheme(QApplication &app) { applyTheme(app); }

bool setActiveTheme(const QString &id) {
    if (!registry().scanned) scan();
    const ThemeSpec *spec = loadTheme(id);
    if (!spec) return false;
    relaySettings().setValue(QStringLiteral("theme/name"), id);
    adoptTokens(*spec);
    if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance())) {
        applyPalette(*app, *spec);
        app->setStyleSheet(stylesheetFor(*spec));
    }
    repolishAll();
    notifier()->emitChanged();
    return true;
}

void repolishAll() {
    for (QWidget *top : QApplication::topLevelWidgets()) {
        QList<QWidget *> all = top->findChildren<QWidget *>();
        all.prepend(top);
        for (QWidget *widget : all) {
            widget->style()->unpolish(widget);
            widget->style()->polish(widget);
            widget->update();
        }
    }
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
