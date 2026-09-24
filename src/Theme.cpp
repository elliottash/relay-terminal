// SPDX-License-Identifier: AGPL-3.0-or-later
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
#include <QRandomGenerator>
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
QFrame#paneChrome, QFrame#helpPopup, QFrame#paneInfoPopover, QMenu, QFrame#notificationsPopup {
    border-top: 2px solid %1; border-left: 2px solid %1; border-bottom: 2px solid %2; border-right: 2px solid %2; }
QPushButton:pressed, QToolButton#stripChip:pressed {
    border-top: 2px solid %2; border-left: 2px solid %2; border-bottom: 2px solid %1; border-right: 2px solid %1; }
QLineEdit, QSpinBox, QPlainTextEdit, QTextEdit, QFrame#composer, QWidget#pane,
QTreeView#fileExplorerView, QTreeWidget#turnTools, QScrollArea#filePreviewImageArea {
    border-top: 2px solid %2; border-left: 2px solid %2; border-bottom: 2px solid %1; border-right: 2px solid %1; }
QWidget#pane[relayActive="true"] { border: 2px solid @muted; }
QFrame#composer[relayActive="true"] { border: 2px solid @accent; }
)");
    return css.arg(light, dark);
}

// --- chrome materials: `[flags] metal` and `[flags] plastic` -------------------------------------
//
// A material is how a surface takes light, and both treatments say it the same way: one
// `[material]` table (light, mid, dark, edge, chrome_light, chrome_dark) and a gradient per
// raised face. Metal gets a specular line and a wide range; plastic gets a broad soft highlight
// and a narrow one. Both leave two things alone: the grid (no chrome colour reaches it,
// docs/SWITCHBOARD-AESTHETIC.md 2.2) and any surface text is typed on — a gradient behind a
// caret is a distraction, not a material.
//
// A tiled grain was tried over all of this and cut (owner, 2026-09-18: the texture on the
// buttons "looks crap"). Qt only paints a stylesheet background-image on some widget classes,
// so it landed on chips and never on the tab row, which is exactly the inconsistency that made
// it read as dirt rather than as a material.

struct Material {
    QString light, mid, dark, edge, chromeTop, chromeBottom;
};

Material materialOf(const ThemeSpec &spec) {
    Material m;
    m.light = hex(extraColor(spec, QStringLiteral("material.light"), SurfaceRaised.lighter(125)));
    m.mid = hex(extraColor(spec, QStringLiteral("material.mid"), SurfaceRaised));
    m.dark = hex(extraColor(spec, QStringLiteral("material.dark"), SurfaceRaised.darker(130)));
    m.edge = hex(extraColor(spec, QStringLiteral("material.edge"), SurfaceRaised.lighter(190)));
    m.chromeTop = hex(extraColor(spec, QStringLiteral("material.chrome_light"), Background.lighter(135)));
    m.chromeBottom = hex(extraColor(spec, QStringLiteral("material.chrome_dark"), Background.darker(115)));
    return m;
}

// Metal (Dark Copper): milled. A flat fill of any copper reads as brown paint, so a raised face
// is a specular line along its top edge, a lit upper half and shade below — and a press turns
// the light round, because a pressed key faces away from the lamp.
QString metalStylesheet(const ThemeSpec &spec) {
    const Material m = materialOf(spec);
    QString css = QStringLiteral(R"(
QPushButton, QComboBox, QToolButton#stripChip, QLabel#stripChipLabel, QLabel#keyCap,
QToolButton#workChip, QFrame#paneChrome, QMenu, QFrame#notificationsPopup,
QFrame#helpPopup, QFrame#paneInfoPopover, QLabel#toast {
    background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 %4, stop:0.09 %1, stop:0.55 %2, stop:1 %3); }
QPushButton:hover, QComboBox:hover, QToolButton#stripChip:hover, QToolButton#workChip:hover {
    background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 %4, stop:0.14 %1, stop:0.6 %1, stop:1 %2); }
QPushButton:pressed, QToolButton#stripChip:pressed, QToolButton#workChip:pressed {
    background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %3, stop:0.9 %2, stop:1 %4); }
QPushButton:disabled { background: %3; }
/* The chassis the panes are bolted to: one sheet, with a shallower sheen than a chip, so the
   chips read as raised out of it rather than as a second set of buttons. The tab row and the
   toolbar are cleared so that sheet runs behind them unbroken. */
QMainWindow#relayWindow, QDialog {
    background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %5, stop:1 %6); }
QTabBar, QToolBar { background: transparent; }
)");
    return css.arg(m.light, m.mid, m.dark, m.edge, m.chromeTop, m.chromeBottom);
}

// Plastic (IBM Beige): moulded ABS, and the opposite of metal in the two ways that matter. The
// highlight is broad and soft instead of a line, because the surface scatters light rather than
// reflecting it; and the range is narrow, because a beige case in a lit room is nearly one
// colour — it is the *shape* that shows, not a shine. The two-tone bevel ([flags] bevel) is
// what supplies the moulded edge; this supplies the face inside it.
QString plasticStylesheet(const ThemeSpec &spec) {
    const Material m = materialOf(spec);
    QString css = QStringLiteral(R"(
QPushButton, QComboBox, QToolButton#stripChip, QLabel#stripChipLabel, QLabel#keyCap,
QToolButton#workChip, QFrame#paneChrome, QMenu, QFrame#notificationsPopup,
QFrame#helpPopup, QFrame#paneInfoPopover, QLabel#toast {
    background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 %1, stop:0.45 %2, stop:1 %3); }
QPushButton:hover, QComboBox:hover, QToolButton#stripChip:hover, QToolButton#workChip:hover {
    background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %4, stop:0.5 %1, stop:1 %2); }
/* A pressed key is the same piece of plastic with the light coming from the other side. */
QPushButton:pressed, QToolButton#stripChip:pressed, QToolButton#workChip:pressed {
    background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %3, stop:0.5 %2, stop:1 %1); }
QPushButton:disabled { background: %3; }
/* The case: the largest moulded surface here, and the one that has to read as a machine rather
   than a light grey web page. One gentle top light across the whole window and the deck the
   panes sit on, with the tab row and toolbar cleared so the moulding runs behind them. */
QMainWindow#relayWindow, QDialog, QWidget#pane {
    background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %5, stop:1 %6); }
QTabBar, QToolBar { background: transparent; }
)");
    return css.arg(m.light, m.mid, m.dark, m.edge, m.chromeTop, m.chromeBottom);
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
    // One line for everything the file did not say and parseTheme could not work out from the
    // theme's own colours. Worth saying: what is used instead is Relay Dark's, which in a light
    // theme is very likely the wrong colour rather than merely a different one.
    if (!spec.borrowed.isEmpty())
        fprintf(stderr, "relay: theme %s: %s not set; used Relay Dark's — name them in %s\n",
                qPrintable(id), qPrintable(spec.borrowed.join(QStringLiteral(", "))), qPrintable(path));
    return &*r.loaded.insert(id, spec);
}

QString settingsThemeId() {
    return relaySettings().value(QStringLiteral("theme/name"), defaultThemeId()).toString();
}

// The theme to use now: the chosen one, then the default, then Relay Dark — whose values the
// compiled-in fallback carries — and last that fallback itself, for an install with no theme files.
const ThemeSpec &resolveTheme(const QString &wanted) {
    if (const ThemeSpec *spec = loadTheme(wanted)) return *spec;
    if (const ThemeSpec *spec = loadTheme(defaultThemeId())) return *spec;
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
    // Always present: parseTheme() turns one out of the theme's own red when the file is silent.
    Action = ui("action", Action);
    // Always present too: parseTheme() dulls one out of the theme's own amber when it is silent.
    Tool = ui("tool", Tool);
    // Always present as well: parseTheme() derives one from the theme's own ANSI 12 when silent.
    Link = ui("link", Link);
    // parseTheme() has already put this theme's own accent here when the file was silent; the
    // fallback stands for the compiled-in spec, which is not parsed.
    Shell = ui("shell", Accent);
    Agent = ui("agent", Agent);

    // The Switchboard's materials (docs/SWITCHBOARD-AESTHETIC.md 3.4). parseTheme() always fills
    // `[board]`, deriving it from this theme's own chrome when the file is silent. `[flags]
    // board_material = false` asks for the hairline form instead, and that substitution is made
    // here, once: the face becomes the text surface, lit hardware the accent, unlit hardware the
    // resting border. Everything downstream — the `@board*` rules below, RowDelegate, the empty
    // board — then reads the same three tokens whichever form the theme asked for.
    BoardMaterial = spec.flag(QStringLiteral("board_material"), true);
    BoardFace = BoardMaterial ? spec.boardColor(QStringLiteral("face"), Surface) : Surface;
    BoardMetal = BoardMaterial ? spec.boardColor(QStringLiteral("metal"), Accent) : Accent;
    BoardMetalDim = BoardMaterial ? spec.boardColor(QStringLiteral("metal_dim"), Border) : Border;
    // The flag's four (#VKFV) are colours, not material: a hairline board keeps the same ones a
    // material one does, because "yellow" and "green" mean the same thing on either face.
    BoardPriorityLow = spec.boardColor(QStringLiteral("priority_low"), BoardPriorityLow);
    BoardPriorityOne = spec.boardColor(QStringLiteral("priority_one"), BoardPriorityOne);
    BoardPriorityTwo = spec.boardColor(QStringLiteral("priority_two"), BoardPriorityTwo);
    BoardPriorityThree = spec.boardColor(QStringLiteral("priority_three"), BoardPriorityThree);

    const auto syntax = [&spec](const char *name, const QColor &fallback) {
        return spec.syntaxColor(QString::fromLatin1(name), fallback);
    };
    SyntaxCommand = syntax("command", Shell);
    SyntaxUnknown = syntax("unknown", Error);
    SyntaxFlag = syntax("flag", Warning);
    SyntaxString = syntax("string", Success);
    SyntaxPath = syntax("path", Link);
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
    p.setColor(QPalette::Link, Link);
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
/* Which pane is the active one (issue #H3TQ): a brighter outline on it, and a header that reads
   at full strength there and a step back everywhere else. Grey, not accent — the accent means
   "shell" here, so a pane frame must not compete with the composer (relay-dark.toml). */
QLabel#paneTitle { color: @muted; font-weight: 600; }
QLabel#paneTitle[relayActive="true"] { color: @text; }
QLabel#paneCwd { color: @muted; font-size: 9pt; }
QLabel#paneCardChip { color: @muted; font-size: 9pt; font-weight: 600; }
QLabel#paneAuto { color: @muted; font-size: 9pt; border: 1px solid @border; border-radius: 4px; padding: 0 4px; }
QLineEdit#paneTitleEdit { background: @surface; color: @text; border: 1px solid @accentBorder; border-radius: 4px; padding: 1px 6px; }

QPushButton { background: @raised; color: @text; border: 1px solid @border; border-radius: 6px; padding: 5px 14px; }
QPushButton:hover { border-color: @muted; }
QPushButton:pressed { background: @surface; }
/* Never put font-weight (or any other metric) on :default, :hover or another pseudo-state:
   QStyleSheetStyle folds a rule's font into the widget's font only when the rule has no
   pseudo-state, so the button would paint bold while sizeHint() still measured the label at
   regular weight — the label then runs past its own edge ("Add / replace…" in the API keys
   modal, owner report 2026-09-18). Inside a dialog push buttons are autoDefault, so :default
   follows the focus and any label could hit it. The accent fill carries the emphasis instead;
   #primary is an id selector with no pseudo-state, so its bold does reach the size hint.
   tests/buttonfit_test.cpp renders every label and fails on a clipped one. */
QPushButton:default { background: @accent; color: @accentText; border-color: @accent; }
QPushButton#primary { background: @accent; color: @accentText; border-color: @accent; font-weight: 600; }
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
/* Who owns the terminal, in the colour of what it is saying: amber only when a program is waiting
   for the person, violet while the agent is driving one, the terminal's blue while one merely
   runs. Its `state` property is set in Pane::refreshProgramHint. */
QLabel#opaqueHint { color: @muted; }
QLabel#opaqueHint[state="needs-you"] { color: @warning; }
QLabel#opaqueHint[state="agent"] { color: @agent; }
QLabel#opaqueHint[state="running"] { color: @shell; }
/* Agent sessions: plan chip, context indicator, plan editor */)");
    css += QStringLiteral(R"(
QToolButton#planChip { color: @onAgent; background: @agent; border: none; border-radius: 4px; padding: 1px 6px; font-weight: 700; letter-spacing: 1px; font-size: 9pt; }
QLabel#contextLabel { color: @muted; font-family: "@mono"; font-size: 9pt; padding: 0 4px; }
QLabel#contextLabel[warn="true"] { color: @warning; }
QToolButton#workChip { color: @muted; border: 1px solid @border; border-radius: 6px; padding: 2px 8px; font-size: 9pt; background: @raised; min-height: 17px; }
QToolButton#workChip[state="running"] { color: @text; }
QToolButton#workChip[state="done"] { color: @success; border-color: @successBorder; }
QToolButton#workChip[state="attention"] { color: @warning; border-color: @warningBorder; }
QToolButton#workChip:hover { color: @text; border-color: @accent; }
QToolButton#workChip::menu-indicator { image: none; width: 0; }
QPlainTextEdit#planText { background: @bg; border: none; font-family: "@mono"; font-size: 10pt; padding: 8px; }
QLabel#planNotice { color: @muted; }
QFrame#queueStrip QToolButton { color: @muted; border: 1px solid transparent; border-radius: 4px; padding: 1px 6px; }
QFrame#queueStrip QToolButton:hover { color: @text; border-color: @border; }
/* "Initialize a project and create a Switchboard here?" — a row of the pane's own column under the
   terminal, never a dialog and never an overlay (src/ProjectInitBlock.h). */
QFrame#projectInit { background: @surface; border: 1px solid @accentBorder; border-radius: 8px; }
QLabel#projectInitTitle { color: @text; font-weight: 600; }
QLabel#projectInitPath { color: @muted; font-family: "@mono"; font-size: 9pt; }
QLabel#projectInitHeading { color: @muted; font-weight: 600; letter-spacing: 1px; font-size: 9pt; }
QLabel#projectInitNote { color: @muted; font-size: 9pt; }
QCheckBox#projectInitBox { color: @text; font-size: 9pt; }
QPushButton#projectInitButton { color: @muted; background: @raised; border: 1px solid @border; border-radius: 6px; padding: 3px 12px; }
QPushButton#projectInitButton:hover { color: @text; border-color: @muted; }
QPushButton#projectInitButton:focus { color: @text; border-color: @accent; }
QPushButton#projectInitYes { color: @text; border-color: @accentBorder; }
QFrame#paneBanner { background: @raised; border: 1px solid @caution; border-radius: 8px; }
QFrame#paneBanner QLabel { color: @text; }
QFrame#transcript { background: @surface; border: 1px solid @accentBorder; border-radius: 8px; }
QLabel#transcriptHeader { color: @muted; }
QPlainTextEdit#transcriptView { background: transparent; border: none; font-family: "@mono"; font-size: 10pt; }
QLabel#toast { background: @raised; color: @text; border: 1px solid @accentBorder; border-radius: 8px; padding: 6px 12px; }
/* The share dialog's link and its note (src/RemoteShare.cpp). */
QLabel#shareNote { color: @muted; font-size: 9.5pt; }
/* Settings pane (src/SettingsPane.cpp): a full pane, engraved headers like the Switchboard's,
   rows that light up under the pointer and under the keyboard highlight. */
QWidget#settingsPane { background: @bg; }
QLineEdit#settingsSearch { padding: 6px 10px; font-size: 10.5pt; }
QTabBar#settingsTabs::tab { padding: 4px 10px; margin: 0 2px 0 0; }
QScrollArea#settingsPage, QWidget#settingsPageBody { background: transparent; border: none; }
QLabel#settingsBlurb, QLabel#settingsInfo { color: @muted; }
QLabel#settingsHeading { color: @text; font-size: 11pt; font-weight: 700; padding: 16px 10px 4px 10px; border-bottom: 1px solid @border; }
QLabel#settingsSubheading { color: @text; font-size: 10pt; font-weight: 600; }
QWidget#settingsSectionRule { background: @border; border: none; margin: 8px 10px 4px 10px; }
QFrame#settingsRow { background: transparent; border: 1px solid transparent; border-radius: 8px; }
QFrame#settingsRow:hover { background: @surface; }
QFrame#settingsRow[nested="true"] { border-radius: 4px; }
QToolButton#settingsInfoLink { padding: 0 2px; border: none; }
QLabel#settingsRowLabelStrong { font-weight: 600; }
QLabel#settingsGrip { color: @muted; font-size: 13pt; padding: 0 2px; }
QFrame#settingsRow[dropTarget="true"] { border-top: 2px solid @accent; }
QFrame#settingsRow[current="true"] { background: @surface; border-color: @accentBorder; }
QLabel#settingsRowLabel { color: @text; }
QLabel#settingsRowDetail { color: @muted; font-size: 9.5pt; }
/* The ↺ on a row that is not at Relay's default: a mark first and a button second, so it reads as
   a dot beside the words until the pointer is on it. */
QPushButton#settingsRowReset { color: @muted; background: transparent; border: none; padding: 0 4px; font-size: 12pt; }
QPushButton#settingsRowReset:hover { color: @text; background: @surface; border-radius: 6px; }
QLabel#settingsFooter { color: @muted; font-size: 9pt; padding-top: 6px; border-top: 1px solid @border; }
/* The requests ledger and a subagent transcript float over a pane; both are opaque on purpose. */
QWidget#requestsPanel, QWidget#subagentTranscript { background: @bg; border: 1px solid @border; border-radius: 8px; }
QWidget#requestsPanel QLabel#panelKeys, QWidget#subagentTranscript QLabel#panelKeys { color: @muted; }
QTreeWidget#requestsList { background: transparent; border: none; outline: none; }
QToolButton#interruptButton { border: 1px solid @border; border-radius: 6px; padding: 4px; background: transparent; }
QToolButton#interruptButton:hover { border-color: @accent; }
QWidget#pane { background: @bg; border: 1px solid @border; border-radius: 8px; }
QWidget#pane[relayActive="true"] { border: 1px solid @muted; }
/* An agent console is a `Pane`, and `theme::polishWindow` names it `pane` like any other, so it
   wears the frame above wherever a host embeds it — right on the Switchboard's list page, in
   Options and in Sessions, where the transcript stands inside the frame and the prompt box
   inside that, exactly as in a pane. On a **card** the transcript is hidden until it is used
   (`Pane::setTranscriptHiddenUntilUsed`, owner decision 2), so the frame came down to eight
   pixels outside the composer's own and the card page showed a border inside a border. One)");
    css += QStringLiteral(R"(
   frame there, and it is the prompt box's: `CardDetail::setConsole` stamps the property, and no
   other console has it. */
QWidget#pane[cardConsole="true"] { background: transparent; border: none; }
/* Pane button row, drop zones, tab bar controls */
QFrame#helpPopup { background: @raised; border: 1px solid @border; border-radius: 8px; }
QLabel#keyCap { background: @surface; border: 1px solid @border; border-radius: 4px; padding: 1px 6px; color: @text; font-size: 9pt; min-width: 14px; }
QLabel#helpText { color: @muted; font-size: 9.5pt; }
QLabel#helpFooter { color: @muted; font-size: 9pt; padding-top: 6px; border-top: 1px solid @border; }

/* Warp-style chips in the composer's status strip: a slightly raised rectangle each. The
   min-height is a line of the chips' 9pt text, so an icon-only chip (Switchboard, tasks,
   microphone) is exactly as tall as the text chip beside it (2026-09-17). */
QToolButton#stripChip { background: @raised; border: 1px solid @border; border-radius: 6px; padding: 2px 8px;
                        color: @muted; font-size: 9pt; min-height: 17px; }
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
                        color: @muted; font-size: 9pt; }
QLabel#stripChipLabel[warn="true"] { color: @warning; border-color: @warning; }

/* The composer's status strip: dim, flat, no dropdown chrome (Warp keeps its chips quiet). */
QComboBox#statusPicker { background: @raised; border: 1px solid @border; border-radius: 6px; color: @muted; padding: 2px 18px 2px 8px; font-size: 9pt; }
QComboBox#statusPicker:hover { color: @text; border-color: @accent; }
/* A picker whose value is not this pane's to set: the level box on a model with no reasoning knob,
   and on Relay Free, where the gateway picks the level for the role (owner, 2026-09-21: "for no
   knob models, the effort box should be grayed out. same for relay free"). The rule above sets an
   explicit colour, so Qt's disabled palette never reaches this widget and `setEnabled(false)` alone
   changed nothing anyone could see; the tooltip says why it is grey. */
QComboBox#statusPicker:disabled, QComboBox#statusPicker:disabled:hover {
    color: @disabled; border-color: @border; background: transparent; }
QComboBox#statusPicker::drop-down { border: none; width: 12px; }
QComboBox#statusPicker QAbstractItemView { background: @raised; color: @text; selection-background-color: @accent; }
/* The pane's button row. It is on screen in every pane, always the same: nothing appears, lifts or
   rearranges under the pointer. It keeps the raised tile and the outline the hover row used to have
   (card #0T2R) — without them the permanent row read as three grey glyphs floating on the header,
   dimmer than the row it replaced, and nothing said they were buttons. */
QFrame#paneChrome { background: @raised; border: 1px solid @border; border-radius: 6px; }
QFrame#paneInfoPopover { background: @surface; border: 1px solid @border; border-radius: 8px; }
QLabel#paneInfoLabel { color: @muted; font-size: 9pt; }
QLabel#paneInfoId { color: @text; font-family: monospace; font-size: 9pt; }
QToolButton#paneChromeButton { color: @muted; border: 1px solid transparent; border-radius: 4px; padding: 0 5px; min-width: 16px; }
/* @raised is the top of the ground stack, so a hovered button cannot lift off the row by ground:
   it lifts by ink and a stronger outline instead, the way projectInitButton does. */
QToolButton#paneChromeButton:hover { color: @text; border-color: @borderStrong; }
/* The share button while the pane is shared: the agent's violet, as the strip's chip wore. */
QToolButton#paneChromeButton[dest="agent"] { color: @agent; }
QFrame#dropZone { background: @accentSoft; border: 2px solid @accent; border-radius: 6px; }
/* Window header: Relay's own title bar (frameless window). The tab row carries the Relay icon
   on the left and, on the right, the bell, a hairline, the tool-pane buttons ending in the
   gear, a hairline, the join plug, a hairline and the window buttons. */
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
QFrame#notificationRow[kind="reminder"] { border: 2px solid @warning; background: @accentSoft; }
QLabel#notificationTitle { color: @text; font-weight: 600; }
QLabel#notificationBody { color: @muted; font-size: 9.5pt; }
QLabel#notificationTime { color: @muted; font-size: 9pt; padding-left: 8px; }
QLabel#notificationDot { color: @muted; font-size: 9pt; }
QLabel#notificationDot[kind="success"] { color: @success; }
QLabel#notificationDot[kind="warning"] { color: @warning; }
QLabel#notificationDot[kind="error"] { color: @error; })");
    css += QStringLiteral(R"(QLabel#notificationDot[kind="reminder"] { color: @warning; })");
    css += QStringLiteral(R"(
QToolButton#notificationDismiss { color: @muted; border: none; background: transparent; padding: 0 4px; font-size: 9pt; }
QToolButton#notificationDismiss:hover { color: @text; }
QToolButton#popupTextButton { color: @muted; border: 1px solid transparent; border-radius: 4px; padding: 2px 8px; font-size: 9pt; }
QToolButton#popupTextButton:hover { color: @text; border-color: @border; background: @raised; }
QToolButton#popupTextButton:disabled { color: @disabled; }
QToolButton#newTabButton, QToolButton#tabCloseButton { background: transparent; border: none; padding: 0; }
QToolButton#tabDetachButton { color: @muted; border: none; background: transparent; padding: 0; }
/* The project chip on an attached tab (#916B): the project's name, one click detaches. */
QToolButton#tabDetachButton:hover { color: @accent; }
/* Composer prefix chip (! terminal, * agent). It is a destination, so it wears the destination
   pair — cyan for the terminal, violet for the agent — the same as the mode chip, the caret and
   the syntax colouring. It did not until 2026-09-19: the chip landed in 004a74f on 2026-09-17 with
   an amber terminal and a cyan agent, hours before b473d45 defined `shell`/`agent` as the two
   destinations, and nothing reconciled them. It was the one surface in the app saying "terminal is
   amber, agent is cyan", which is both meanings wrong. */
QLabel#prefixChip { border-radius: 4px; padding: 1px 6px; font-weight: 700; font-size: 9pt; letter-spacing: 1px; }
QLabel#prefixChip[kind="shell"] { color: @onShell; background: @shell; }
QLabel#prefixChip[kind="agent"] { color: @onAgent; background: @agent; }
/* Prompt-box-only input: masked password field and the take-control button over the terminal */
QLabel#secretChip { color: @onCaution; background: @caution; border-radius: 4px; padding: 1px 6px; font-weight: 700; font-size: 9pt; letter-spacing: 1px; }
QLineEdit#secretEditor { background: @bg; color: @text; border: 1px solid @caution; border-radius: 6px; padding: 8px; font-family: "@mono"; font-size: 10pt; }
QPushButton#takeControlChip { color: @text; background: @raised; border: 1px solid @accentBorder; border-radius: 6px; padding: 3px 10px; font-size: 9pt; }
QPushButton#takeControlChip:hover { border-color: @accent; }
/* The floating banner over the terminal: what the program is asking, and who answers it */
QFrame#programBanner { background: @surface; border: 1px solid @accentBorder; border-radius: 8px; }
QLabel#programBannerLabel { color: @text; font-size: 9.5pt; }
QPushButton#delegateChip { color: @text; background: @raised; border: 1px solid @accentBorder; border-radius: 6px; padding: 3px 10px; font-size: 9pt; }
QPushButton#delegateChip:hover { border-color: @accent; }
QPushButton#delegateChip:disabled { color: @muted; border-color: @border; }
/* Turn details pane */
QLabel#turnHeader { color: @text; font-weight: 600; padding: 4px 6px; }
QTreeWidget#turnTools { background: @bg; color: @text; border: 1px solid @border; border-radius: 6px; outline: none; }
QTreeWidget#turnTools::item { padding: 3px 2px; }
QTreeWidget#turnTools::item:selected { background: @raised; color: @text; }
QPlainTextEdit#turnLog { background: @surface; color: @text; border: 1px solid @border; border-radius: 6px; font-family: "@mono"; font-size: 10pt; }
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
QTabBar::tab { background: transparent; padding: 5px 8px; margin: 3px 1px 0 1px;
               border: none; border-bottom: 2px solid transparent;
               border-top-left-radius: 6px; border-top-right-radius: 6px; }
QTabBar::tab:hover { background: @surface; }
QTabBar::tab:selected { background: @surface; border-bottom: 2px solid @accent; }
/* Text colour only on the tab bars that do not colour their own tabs: the subagent pane's tabs say
   each subagent's state in their text colour (QTabBar::setTabTextColor), which a `color` here
   would override (#XM0T). */
QTabWidget > QTabBar::tab, QTabBar#settingsTabs::tab { color: @muted; }
QTabWidget > QTabBar::tab:hover, QTabBar#settingsTabs::tab:hover,
QTabWidget > QTabBar::tab:selected, QTabBar#settingsTabs::tab:selected { color: @text; }

/* File panes */)");
    css += QStringLiteral(R"(
QWidget#fileExplorer, QWidget#filePreview { background: @bg; }
QLabel#fileExplorerPath, QLabel#filePreviewTitle { color: @text; font-weight: 600; padding: 2px 4px; }
QLabel#filePreviewNotice { color: @muted; background: @surface; border: 1px solid @border; border-radius: 6px; padding: 4px 8px; }
/* The host chip on a file that lives on another machine (#S5SH): the same chip the pane header
   wears when a terminal is logged into a host (PaneChrome::PaneHeaderChip, panestatus::remoteStyle)
   — the error hue's fill and near-solid line, the text colour for the name — so "not this machine"
   is one look wherever a hostname appears, never the accent. Blended here exactly as remoteStyle
   blends it: fill mix(error, background, 0.26), line mix(error, background, 0.8). */
QLabel#filePreviewHost { color: @text; background: @remoteFill; border: 1px solid @remoteLine; border-radius: 4px; padding: 1px 6px; font-weight: 600; font-size: 9pt; }
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

/* Switchboard (src/BoardPane.cpp; docs/SWITCHBOARD-AESTHETIC.md interventions 3 and 5): one list
   of rows on the board's own face, with engraved (uppercase, mono, letter-spaced) section headers.
   The rows themselves are painted by RowDelegate from the same tokens, so they follow a theme
   switch; only the chrome around the list is styled here.

   The face (`@boardFace`, 3.4) is the one ground in Relay that is a material rather than a
   surface: the pane is a board, the cards are mounted on it. It is set once, on #boardView, and
   everything stacked on it is transparent so the sheet runs behind the whole pane unbroken — the
   way the metal theme clears the tab row. `@boardMetalDim` is the unlit brass of the board's own
   hardware, and it replaces @border on the two rules that frame the list, so the frame reads as
   engraved into the face rather than drawn on top of it. Card and thread text stay on @surface and
   @text throughout (2.2): no material ever goes behind words a person reads. */
QWidget#boardView { background: @boardFace; }
QWidget#boardListPane, QWidget#boardHead, QWidget#boardDetail { background: transparent; }
QLabel#boardCount { color: @muted; font-family: "@mono"; font-size: 9pt; padding: 0 2px; }
QLineEdit#boardFilter { padding: 4px 8px; }
QToolButton#boardAddButton { background: @raised; color: @text; border: 1px solid @border; border-radius: 6px; padding: 4px 10px; }
QToolButton#boardAddButton:hover { border-color: @accent; }
/* ---- the action row: one shape for every no-typing button above a prompt box ---------------
   An action row is the row of buttons over a prompt box that need no typing: the card page's
   Plan / Execute / Verify, and the Switchboard agent's Check, Clean up and whatever another
   session puts on the row (`relay::agent::Context::actions()`). Whoever makes the button,
   the row gives it one shape — owner, 2026-09-20: "make the buttons consistent, can you use the
   styling from the card agent", and then, of the colours, "not the colors though". So this rule
   carries the card button's geometry and type **only** and declares no colour: a widget joining
   a row is stamped with the dynamic property `actionRow`, and each button's own object-name rule
   — an id selector, which outranks this one whatever the order — keeps the ground, the border
   colour and the ink its maker gave it. Execute's accent outline stays an outline; a plain button
   stays plain. Keyed on the property and not on three object names, so a session that puts a
   button on the row gets the shape without touching this file, and without renaming its button
   (other sessions' tests find theirs by object name). The border *width* is part of the shape
   and is here, because a button two pixels shorter than its neighbour is the flatness the owner
   was looking at; the border *colour* is not, so a button that declares none is framed in its
   own ink — loud enough to say "give this one a rule", which is what the plain-ground list
   below is for. A button not on a row at all (Clean up lives in the list tools until the panel
   adopts it) never matches this and keeps its own frame. */
QPushButton[actionRow="true"], QToolButton[actionRow="true"] {
    border-width: 1px; border-style: solid; border-radius: 6px; padding: 4px 12px;
    font-weight: normal; }
/* The three pixels are Qt's, not a design choice. QStyleSheetStyle adds QSize(3, 3) to a
   *QToolButton* that has a box rule and nothing to a QPushButton — the comment in
   qstylesheetstyle.cpp reads "### broken QToolButton" — so identical padding paints a tool
   button three pixels taller. The Switchboard's row is tool buttons and the card page's is push
   buttons, so the push buttons carry those three pixels and the two rows come out one height.)");
    css += QStringLiteral(R"(
   It is a constant, not a ratio, so this holds at any desktop font size. The second selector is
   what beats `QPushButton#boardReplyButton` and `#boardExecute`, which are id rules and would
   otherwise put their own padding back; the first catches a push button on any other row. */
QPushButton[actionRow="true"],
QWidget#boardCardActions QPushButton { padding: 5px 12px 6px 12px; }
/* The plain ground the row's ordinary buttons stand on: Check and Clean up (19.9), and the
   Tests and Profile another session puts there (#7BM4) — those two had no rule at all and so
   painted the bare Fusion button, visibly smaller and flatter than their neighbours in
   docs/qa_evidence/2026-09-20-action-rows-left/01-switchboard-idle.png. Colour only; the shape
   is the `actionRow` rule above. One selector list rather than a rule per button, so a plain
   button added to a row joins the list and nothing else changes — and a button that brings its
   own colours (`boardExecute`) is simply not in it. */
/* Every other button on an action row, whoever made it. `Pane::rebuildActionRow` builds the row
   from `relay::agent::Context::actions()` and stamps `actionRow` on each button, so a card's
   Plan and Execute — which are tool buttons on that row now, not the push buttons the id rules
   below were written for — get the same ground as Check and Clean up instead of the bare Fusion
   button. An id rule still outranks this one, so a button that brings its own colours keeps
   them.

   The border colour is here and not in the shape rule above: the shape rule is shared with push
   buttons that bring their own id rule, and a tool button on a row has none to bring. Without it
   Qt frames the button in its own ink, so the card page's Plan — a tool button since the row
   became the console's — came out ringed in `@text` beside Execute's violet, and read as a focus
   ring or a default button on the first action of the row (`docs/qa_evidence/
   2026-09-21-agents-are-consoles/punch/b02-card.png`). The quiet `@border` is what Check, Clean
   up and the old `QPushButton#boardReplyButton` have always worn. */
QToolButton[actionRow="true"] { background: @raised; color: @text; border-color: @border; }
QToolButton[actionRow="true"]:hover { color: @text; border-color: @accent; }
QToolButton[actionRow="true"]:disabled { color: @disabled; border-color: @surface; }
/* …and the one that **leaves the surface**: Execute and Verify hand the card to a terminal pane,
   so they wear the agent's colour outlined, exactly as `QPushButton#boardExecute` did while the
   row was the card page's own. Keyed on the `leaves` property the row already sets, so an action
   a later session adds gets the face by saying what it does rather than by being named here. */
QToolButton[actionRow="true"][leaves="true"] { color: @agent; border-color: @agent; }
QToolButton[actionRow="true"][leaves="true"]:hover { background: @surface; border-color: @agent; }
QToolButton[actionRow="true"][leaves="true"]:disabled { color: @disabled; border-color: @surface; }
QToolButton#boardChatCheck, QToolButton#boardCleanup,
QToolButton#boardTests, QToolButton#boardProfile { background: @raised; color: @text; border: 1px solid @border; border-radius: 6px; padding: 4px 12px; }
QToolButton#boardChatCheck:hover, QToolButton#boardCleanup:hover,
QToolButton#boardTests:hover, QToolButton#boardProfile:hover { color: @text; border-color: @accent; }
QToolButton#boardChatCheck:disabled, QToolButton#boardCleanup:disabled,
QToolButton#boardTests:disabled, QToolButton#boardProfile:disabled { color: @disabled; border-color: @surface; }
/* While a cleanup runs the same button is Stop. Colour and border only: a rule that changed the
   font here would paint one width and measure another (tests/buttonfit_test.cpp). */
/* Violet, not amber: this is the Switchboard's agent working, and amber is reserved for what is
   waiting on a person (docs/ARCHITECTURE.md, "What the colours mean"). */
QToolButton#boardCleanup[running="true"] { color: @agent; border-color: @agentBorder; }
QToolButton#boardCleanup[running="true"]:hover { border-color: @agent; }
/* The cleanup's result, in the list page under its tools — not a floating strip. */
QWidget#boardCleanupPanel { background: @surface; border: 1px solid @accentBorder; border-radius: 8px; }
QWidget#boardCleanupPanel[failed="true"] { border-color: @error; }
QLabel#boardCleanupHead { color: @text; }
QTextBrowser#boardCleanupBody { background: transparent; color: @text; border: none; }
QTextBrowser#boardCleanupBody QScrollBar:vertical { width: 8px; margin: 0; }
/* The helper agent's fold in Options, Actions and Sessions, and the Switchboard's chat area: the
   surface an agent console is embedded in. It reads as part of the page — a hairline over it and
   the pane's own face behind it — rather than as a card or a floating strip, because what is
   above it is what the agent is talking about. The `boardChat*` names are the panel's and are
   kept: the widgets they name are still these (card #AGNT step 9 retired the rest of them with
   src/HelperChat.cpp, and the console inside brings a terminal pane's own rules with it). */
QWidget#boardChatPanel { background: transparent; border-top: 1px solid @boardMetalDim; }
QLabel#boardChatHead { color: @muted; font-family: "@mono"; font-size: 9pt; }
/* The action row's buttons — Check, Clean up, Tests and Profile — wear the card page's
   Plan/Execute face, because they are the same kind of thing in the same place: an action above a
   prompt box that needs no typing (owner, 2026-09-20). Their shape comes from the `actionRow`
   rule and their ground from the plain-ground list, both up beside `boardCleanup` — one look, so
   every action row in the app reads the same.

   The panel's own log, busy strip, Stop and queue rows had rules here and have none now: a
   console is a `Pane`, so its transcript, its busy line, its Esc and its §12 queue strip are the)");
    css += QStringLiteral(R"(
   terminal pane's widgets and the terminal pane's rules paint them. That is the point of the
   card — "the queue doesn't work like the main terminal" — held down in this file as well. */
/* Check and triage findings, and the survey's import offer: lists in the page that a click turns
   into a draft in the composer below them. */
QWidget#boardChatFindings, QWidget#boardChatSurvey { background: @surface; border: 1px solid @border; border-radius: 8px; }
QLabel#boardChatFindingsHead, QLabel#boardChatSurveyHead { color: @text; }
QLabel#boardChatFinding { color: @warning; }
QLabel#boardChatFinding:hover { color: @text; }
QCheckBox#boardChatProposal { color: @text; spacing: 5px; }
QToolButton#boardChatImport { background: @raised; color: @text; border: 1px solid @accentBorder; border-radius: 6px; padding: 4px 10px; }
QToolButton#boardChatImport:hover { border-color: @accent; }
/* The helper's prompt box had ten rules here — a frame, an editor, a mic chip and a context chip
   — copied value for value from `QFrame#composer`, `QPlainTextEdit#composerEditor`,
   `QToolButton#stripChip` and `QLabel#stripChipLabel` above, because "make it like the pane
   agent" (owner, 2026-09-20) and a panel was not a pane. It is a pane now (card #AGNT), so the
   copies are gone and the originals paint it: one control, one rule, and no second place for the
   two to drift apart. */
/* The list page's own tools (the count, the filter, the buttons, the section checkboxes) sit on
   a hairline over the rows; the pane's header carries nothing but the way back from a card. */
QWidget#boardListTools { background: transparent; border-bottom: 1px solid @boardMetalDim; }
QToolButton#boardBack { color: @muted; background: transparent; border: 1px solid transparent; border-radius: 6px; padding: 4px 8px; }
QToolButton#boardBack:hover { color: @text; border-color: @border; background: @raised; }
/* Engraved, like the section headers they switch on and off (SWITCHBOARD-AESTHETIC 3.1). The
   font is set here and in no pseudo-state rule, so sizeHint() measures what actually paints. */
QCheckBox#boardSectionCheck { color: @muted; font-family: "@mono"; font-size: 9pt; spacing: 5px; padding: 0; }
QCheckBox#boardSectionCheck::indicator { width: 11px; height: 11px; border-radius: 3px; }
/* The label chips under the section boxes (card #VKFV): the same engraved mono, lower case where
   the sections are upper, so the two rows read as two different questions. */
QCheckBox#boardLabelCheck { color: @muted; font-family: "@mono"; font-size: 9pt; spacing: 4px; padding: 0; }
QCheckBox#boardLabelCheck::indicator { width: 11px; height: 11px; border-radius: 3px; }
QCheckBox#boardLabelCheck:hover { color: @text; }
/* The list's column header (owner, 2026-09-19: "change switchboard sorting from a sort button to
   adding header columns that you click on"): the Card, Created and Updated cells a click sorts by.
   Engraved like the section headers they sit above — mono, upper case, letter-spaced, and the font
   is set in code so sizeHint() measures what paints — muted until the pointer or the sort is on
   them. The sort that is on wears the board's own brass (#MXMG, not the app-wide accent: this
   header belongs to the switchboard's material palette) and its arrow. */
QWidget#boardColumnHeader { background: transparent; }
QToolButton#boardHeaderCard, QToolButton#boardHeaderStage, QToolButton#boardHeaderCreated, QToolButton#boardHeaderUpdated { color: @muted; background: transparent; border: none; border-radius: 4px; padding: 0; }
QToolButton#boardHeaderCard:hover, QToolButton#boardHeaderStage:hover, QToolButton#boardHeaderCreated:hover, QToolButton#boardHeaderUpdated:hover { color: @text; }
QToolButton#boardHeaderCard[active="true"], QToolButton#boardHeaderStage[active="true"], QToolButton#boardHeaderCreated[active="true"], QToolButton#boardHeaderUpdated[active="true"] { color: @boardMetal; }
/* The ⚑ over the flag column (card #VKFV): same cell styles as its neighbours, and the accent
   alone carries the active state — the cell is one glyph wide, with no room for an arrow. */
QToolButton#boardHeaderPriority { color: @muted; background: transparent; border: none; border-radius: 4px; padding: 0; }
QToolButton#boardHeaderPriority:hover { color: @text; }
QToolButton#boardHeaderPriority[active="true"] { color: @accent; }
QListWidget#boardList { background: transparent; border: none; }
QListWidget#boardList QScrollBar:vertical { width: 8px; margin: 0; }
QLineEdit#boardQuickAdd { background: @raised; border-color: @accentBorder; }
QLabel#boardProblems { color: @warning; border: 1px solid @warningBorder; border-radius: 6px; padding: 4px 8px; }
QFrame#boardNotice { background: @raised; border: 1px solid @accentBorder; border-radius: 6px; }
QFrame#boardNotice[error="true"] { border-color: @error; }
QLabel#boardNoticeText { color: @text; }
QToolButton#boardTextButton { color: @muted; background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 2px 8px; }
QToolButton#boardTextButton:hover { color: @text; border-color: @border; background: @raised; }
QToolButton#boardTextButton:disabled { color: @disabled; }
QLabel#boardEmpty { color: @muted; }
QLabel#boardKeys { color: @muted; font-size: 9pt; padding: 4px 10px; border-top: 1px solid @boardMetalDim; }
QLabel#boardCardRef { color: @muted; font-family: "@mono"; }
/* The ⧉ that copies the reference (#FT77): as quiet as the id it copies, the pane's ink
   under the pointer, so it reads as the button it is. */
QToolButton#boardCardRefCopy { color: @muted; background: transparent; border: none; padding: 0 3px; font-size: 9pt; }
QToolButton#boardCardRefCopy:hover { color: @text; }
QLabel#boardCardTitle { color: @text; font-size: 12pt; font-weight: 600; }
/* The pencil that edits the card sits on the title it edits, outlined in the accent (#VZ69). On a
   card whose other tools are muted text buttons, exactly one control is obviously pressable, and
   it is the one the owner reaches for most. The delete beside it (#CYM9) wears the same outline:
   destructive, but a first-class action of the card, and undoable for 30 s like every write. */)");
    css += QStringLiteral(R"(
QToolButton#boardEditPencil, QToolButton#boardCardDelete { color: @accent; background: transparent; border: 1px solid @accentBorder; border-radius: 6px; padding: 3px 10px; }
QToolButton#boardEditPencil:hover, QToolButton#boardCardDelete:hover { color: @accentText; background: @accent; border-color: @accent; }
QToolButton#boardEditPencil:disabled, QToolButton#boardCardDelete:disabled { color: @disabled; background: transparent; border-color: @surface; }
/* Editing the card's own words: the title in place, and `## Issue` where the document was. */
QLineEdit#boardCardTitleEdit { color: @text; font-size: 12pt; font-weight: 600; background: @surface; border: 1px solid @accentBorder; border-radius: 6px; padding: 2px 6px; }
QFrame#boardEdit { background: @surface; border: 1px solid @accentBorder; border-radius: 8px; }
QLabel#boardEditHint { color: @muted; font-size: 9pt; }
QPlainTextEdit#boardIssueEditor { background: transparent; border: none; padding: 2px; }
QToolButton#boardCardClose { color: @muted; background: transparent; border: 1px solid transparent; border-radius: 4px; font-size: 12pt; padding: 0 6px; }
QToolButton#boardCardClose:hover { color: @text; border-color: @border; background: @raised; }
QComboBox#boardPicker { padding: 2px 26px 2px 8px; min-height: 18px; }
QLabel#boardCardMeta { color: @text; font-size: 9.5pt; }
QTextBrowser#boardCardDocument { background: @surface; color: @text; border: 1px solid @border; border-radius: 8px; padding: 0; }
QLabel#boardCardError { color: @error; }
/* The card page's reply box is the same component as the helper panel's and as a terminal pane's
   (owner, 2026-09-20: "the card agent looks a lot better" — so the other two were made to match
   it, and it was finished into the pane's shape): one rounded frame, an accent border while the
   cursor is in it, a borderless editor in the prompt font, and one chip strip under the text.
   Same radius and same ground as `QFrame#boardChatBox` above, so the list page and the card page
   are visibly one thing. */
QFrame#boardReply { background: @surface; border: 1px solid @border; border-radius: 10px; }
QFrame#boardReply[relayActive="true"] { background: @raised; border: 1px solid @accentBorder; }
/* Unless the box inside it is a console's, which brings its own frame: `QFrame#composer` is the
   pane's prompt box and it is the same control, so two frames drew a border inside a border on
   the card page. The outer one steps back to the page's ground and the console's is the one the
   owner sees — one frame, as in a pane. The busy strip lives in here too, which is why the
   widget stays rather than being taken out of the layout. */
QFrame#boardReply[hasConsole="true"],
QFrame#boardReply[hasConsole="true"][relayActive="true"] { background: transparent; border: none; }
QPlainTextEdit#boardReplyEditor { background: transparent; border: none; padding: 2px 4px; font-family: "@mono"; font-size: 11pt; }
/* Plan, Execute and Verify are above the box now, not in it (owner: "put buttons like that in a
   row above the chat box"), so the row is on the page's ground and wears the panel's Check/Clean
   up face — one look for every no-typing action over a prompt box. */
QWidget#boardCardActions { background: transparent; }
QPushButton#boardReplyButton, QPushButton#boardTestsCheck,
QFrame#boardEdit QPushButton#primary { padding: 4px 12px; }
/* While a turn runs: the line that names it in the agent's colour, and the button that ends it.
   The stop is quiet until the pointer is on it, then it is the error colour — it is the only
   control on the card that throws work away (#VZ69). */
QLabel#boardBusyLabel { color: @agent; font-size: 9.5pt; }
QToolButton#boardStop { color: @muted; background: transparent; border: 1px solid @border; border-radius: 4px; padding: 2px 8px; }
QToolButton#boardStop:hover { color: @error; background: @raised; border-color: @error; }
/* Execute hands the card to a terminal pane's agent (#XS6Q): the agent's colour, outlined, so
   it reads as the step that leaves the board rather than a third way of asking. */
QPushButton#boardExecute { padding: 4px 12px; color: @agent; border-color: @agent; }
QPushButton#boardExecute:hover { background: @surface; }
QPushButton#boardExecute:disabled { color: @disabled; border-color: @surface; }
)");
    const QColor selection = spec.uiColor(QStringLiteral("selection"), Accent.darker(200));
    const QColor caution = blend(Warning, Error, 0.7);
    // The @on… tokens come first: replacing @warning before @onWarning would eat the prefix.
    // Any token added here needs the same check — a `@textures` added at the end of this list
    // came out as "#ece6e0ures/…", because `@text` had already taken its head.
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
        {QStringLiteral("@agentBorder"), hex(blend(Agent, Background, 0.5))},
        {QStringLiteral("@success"), hex(Success)}, {QStringLiteral("@warning"), hex(Warning)},
        {QStringLiteral("@error"), hex(Error)}, {QStringLiteral("@caution"), hex(caution)},
        {QStringLiteral("@shellSoft"), rgba(withAlpha(Shell, 56))}, {QStringLiteral("@shell"), hex(Shell)},
        {QStringLiteral("@agentSoft"), rgba(withAlpha(Agent, 56))}, {QStringLiteral("@agent"), hex(Agent)},
        {QStringLiteral("@link"), hex(Link)},
        {QStringLiteral("@boardFace"), hex(BoardFace)},
        {QStringLiteral("@boardMetalDim"), hex(BoardMetalDim)}, {QStringLiteral("@boardMetal"), hex(BoardMetal)},
        {QStringLiteral("@remoteFill"), hex(blend(Error, Background, 0.26))},
        {QStringLiteral("@remoteLine"), hex(blend(Error, Background, 0.8))},
        {QStringLiteral("@mono"), mono},
        {QStringLiteral("@icons"), themeDataDir() + QStringLiteral("/icons")}};
    // Per-theme chrome switches ([flags] in the theme file). A theme that sets neither gets the
    // sheet above unchanged.
    if (spec.flag(QStringLiteral("bevel"))) css += bevelStylesheet(spec);
    if (spec.flag(QStringLiteral("metal"))) css += metalStylesheet(spec);
    if (spec.flag(QStringLiteral("plastic"))) css += plasticStylesheet(spec);
    if (themeDataDir().isEmpty()) {
        // Without bundled icons, fall back to the style's own arrows and check marks.
        css.remove(QRegularExpression(QStringLiteral(R"([^\n]*url\(@icons[^\n]*\n)")));
    }
    for (const auto &token : tokens) css.replace(token.first, token.second);
    // The icons are files with a fixed stroke, so the ones whose ground changes with the theme come
    // in two inks (owner, 2026-09-19, on IBM Beige: "the background blue in the options menu
    // checkboxes is too dark … you cant see the check"). The tick sits on @accent — a dark glyph on
    // copper or cyan, a light one on Beige's navy — so it takes whichever ink contrasts more with
    // this theme's accent. The chevrons sit on the chrome: the grey that reads on charcoal is 2.2:1
    // on beige, so a light theme gets the darker pair.
    {
        const auto luminance = [](const QColor &c) {
            const auto channel = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
            return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
        };
        const auto contrast = [&luminance](const QColor &a, const QColor &b) {
            const double la = luminance(a), lb = luminance(b);
            return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
        };
        if (contrast(QColor(0xf4, 0xf2, 0xee), Accent) > contrast(QColor(0x06, 0x1a, 0x22), Accent))
            css.replace(QStringLiteral("/icons/check.svg"), QStringLiteral("/icons/check-light.svg"));
        if (spec.isLight()) {
            css.replace(QStringLiteral("/icons/chevron-down.svg"), QStringLiteral("/icons/chevron-down-dark.svg"));
            css.replace(QStringLiteral("/icons/chevron-up.svg"), QStringLiteral("/icons/chevron-up-dark.svg"));
        }
    }
    // `[flags] square = true`: no rounded corners anywhere, as on the owner's other sites and on
    // a 1995 desktop. One pass over the finished sheet rather than a token in every rule.
    if (spec.flag(QStringLiteral("square")))
        css.replace(QRegularExpression(QStringLiteral(R"(\b(border(?:-(?:top|bottom)-(?:left|right))?-radius):\s*\d+px)")),
                    QStringLiteral("\\1: 0"));
    return css;
}

}  // namespace

// Dark Copper is what every build starts on (owner, 2026-09-18: "use dark copper by default on all
// builds"). Only the default moved: a profile that chose a theme keeps it.
QString defaultThemeId() { return QStringLiteral("dark-copper"); }

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

// A theme picked at random, never `avoid` — Options › Appearance › Randomize and `/theme random`
// (card #R4ND). Excluding the theme you are on is what makes a second press feel like it did
// something: with a handful of themes installed, one press in four would otherwise change nothing.
// Empty when there is nothing else to pick (one theme installed, or none).
QString randomThemeId(const QStringList &avoid) {
    QStringList ids;
    for (const ThemeChoice &choice : availableThemes())
        if (!avoid.contains(choice.id)) ids << choice.id;
    if (ids.isEmpty()) return {};
    return ids.at(QRandomGenerator::global()->bounded(ids.size()));
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
    // The body size is a floor, not a choice: a desktop that asks for more (KDE's Noto Sans 10,
    // GNOME's 11) keeps it; Qt's generic 9pt default (no desktop, Xvfb) is raised to it, because
    // every smaller size in the stylesheet is measured from it (docs/ARCHITECTURE.md, "Legible text").
    if (app.font().pointSizeF() > 0 && app.font().pointSizeF() < BodyPt) app.setFont(legible(app.font(), BodyPt));
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    applyPalette(app, spec);
    app.setStyleSheet(stylesheetFor(spec));
}

void applyDarkTheme(QApplication &app) { applyTheme(app); }

QString startupThemeId() { return resolveTheme(settingsThemeId()).id; }

ThemeSpec specFor(const QString &id) {
    if (!registry().scanned) scan();
    const ThemeSpec *spec = loadTheme(id);
    return spec ? *spec : resolveTheme(id);
}

bool setActiveTheme(const QString &id, bool persist) {
    if (!registry().scanned) scan();
    const ThemeSpec *spec = loadTheme(id);
    if (!spec) return false;
    if (persist) relaySettings().setValue(QStringLiteral("theme/name"), id);
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

QColor chipInk(const QColor &fill) { return inkOn(fill); }
}
