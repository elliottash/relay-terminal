#include "PaneTabNavigation.h"
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SettingsPane.h"

#include "OutputLinks.h"     // `option:` in an answer is this pane's own link (#AGNT step 8)
#include "SettingsCache.h"   // a write here drops the hot-path settings cache (#057J)
#include "Theme.h"           // the collapsed row's ink

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QToolButton>
#include <QUrl>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QEvent>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QHash>
#include <QKeyEvent>
#include <QLabel>
#include <QCompleter>
#include <QLineEdit>
#include <QMessageBox>
#include <QFontMetrics>
#include <QIcon>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QTabBar>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

namespace relay {

QString actionSlashCommands(const QString &key) {
    // Keep this list aligned with Pane::slashCommands and runSlashCommand. Hidden aliases
    // (such as /todos) are intentionally not taught in the menu.
    static const QHash<QString, QString> commands{
        {QStringLiteral("agent:skills"), QStringLiteral("/skills")},
        {QStringLiteral("agent.swap"), QStringLiteral("/swap")},
        {QStringLiteral("agent.newChat"), QStringLiteral("/new · /clear")},
        {QStringLiteral("agent.flashAgent"), QStringLiteral("/flash")},
        {QStringLiteral("agent.highAgent"), QStringLiteral("/high")},
        {QStringLiteral("agent.localAgent"), QStringLiteral("/local")},
        {QStringLiteral("agent.modelOptions"), QStringLiteral("/models")},
        {QStringLiteral("agent.compact"), QStringLiteral("/compact")},
        {QStringLiteral("agent.rewind"), QStringLiteral("/rewind")},
        {QStringLiteral("agent.rewindCode"), QStringLiteral("/rewind-code")},
        {QStringLiteral("agent.fork"), QStringLiteral("/fork")},
        {QStringLiteral("agent.resume"), QStringLiteral("/resume · /sessions · /conversations")},
        {QStringLiteral("conversations.open"), QStringLiteral("/conversations · /sessions · /resume")},
        {QStringLiteral("agent.info"), QStringLiteral("/info · /status")},
        {QStringLiteral("find.inView"), QStringLiteral("/find")},
        {QStringLiteral("agent.planToggle"), QStringLiteral("/plan")},
        {QStringLiteral("agent.recap"), QStringLiteral("/recap")},
        {QStringLiteral("agent.requests"), QStringLiteral("/tasks · /requests")},
        {QStringLiteral("agent.continue"), QStringLiteral("/continue")},
        {QStringLiteral("agent.instructions"), QStringLiteral("/instructions")},
        {QStringLiteral("agent.export"), QStringLiteral("/export")},
        {QStringLiteral("speech.readAloud"), QStringLiteral("/speak")},
        {QStringLiteral("agent.subagentPane"), QStringLiteral("/agents")},
        {QStringLiteral("menu:agents"), QStringLiteral("/agents")},
        {QStringLiteral("agent.agentsMenu"), QStringLiteral("/agents")},
        {QStringLiteral("board.open"), QStringLiteral("/board")},
        {QStringLiteral("project.init"), QStringLiteral("/init")},
        {QStringLiteral("remote.join"), QStringLiteral("/join · /connect")},
        {QStringLiteral("app.update"), QStringLiteral("/update")},
        {QStringLiteral("menu:effort"), QStringLiteral("/effort · /reasoning")},
    };
    if (key.startsWith(QStringLiteral("effort:")))
        return QStringLiteral("/effort ") + key.mid(7);
    return commands.value(key);
}


namespace {

// A row that toggles when clicked anywhere on it, not only on the 14 px box at its right.
void repolish(QWidget *widget);

const char *const kRowMime = "application/x-relay-settings-row";

class ClickRow final : public QFrame {
public:
    std::function<void()> onClick;
    // Set for a row of a reorderable group (SettingRow::dragGroup): press and drag it onto another
    // row of the same group, and that row's onDropBefore gets this row's id.
    QString dragGroup, rowId;
    std::function<void(const QString &)> onDropBefore;
    // Told its new width, so a row of several buttons can put them under its words when the pane
    // is too narrow for both side by side.
    std::function<void(int width)> onResized;
    void enableDrag() { setAcceptDrops(true); setCursor(Qt::OpenHandCursor); }
protected:
    void resizeEvent(QResizeEvent *event) override {
        QFrame::resizeEvent(event);
        if (onResized) onResized(event->size().width());
    }
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) m_pressed = event->pos();
        QFrame::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (dragGroup.isEmpty() || !(event->buttons() & Qt::LeftButton)
            || (event->pos() - m_pressed).manhattanLength() < QApplication::startDragDistance()) {
            QFrame::mouseMoveEvent(event); return;
        }
        auto *mime = new QMimeData;
        mime->setData(QLatin1String(kRowMime), (dragGroup + QLatin1Char('\n') + rowId).toUtf8());
        auto *drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->setPixmap(grab().scaledToWidth(qMin(width(), 420), Qt::SmoothTransformation));
        drag->setHotSpot(QPoint(12, 12));
        m_pressed = QPoint(-1, -1);
        drag->exec(Qt::MoveAction);
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()) && onClick) onClick();
        QFrame::mouseReleaseEvent(event);
    }
    bool accepts(const QMimeData *mime, QString *draggedId = nullptr) const {
        if (dragGroup.isEmpty() || !mime->hasFormat(QLatin1String(kRowMime))) return false;
        const QString payload = QString::fromUtf8(mime->data(QLatin1String(kRowMime)));
        const QString group = payload.section(QLatin1Char('\n'), 0, 0), id = payload.section(QLatin1Char('\n'), 1);
        if (group != dragGroup || id == rowId) return false;
        if (draggedId) *draggedId = id;
        return true;
    }
    void dragEnterEvent(QDragEnterEvent *event) override {
        if (!accepts(event->mimeData())) { event->ignore(); return; }
        setProperty("dropTarget", true); repolish(this);
        event->acceptProposedAction();
    }
    void dragLeaveEvent(QDragLeaveEvent *event) override {
        setProperty("dropTarget", false); repolish(this);
        QFrame::dragLeaveEvent(event);
    }
    void dropEvent(QDropEvent *event) override {
        setProperty("dropTarget", false); repolish(this);
        QString id;
        if (!accepts(event->mimeData(), &id)) { event->ignore(); return; }
        event->acceptProposedAction();
        if (onDropBefore) onDropBefore(id);
    }
private:
    QPoint m_pressed{-1, -1};
};

// A completer's popup opens as wide as the box it completes, which for a model id box is a
// few dozen characters: "google/gemini-3.5-flash-lite" cut off mid-word (owner report,
// 2026-09-20). This widens it as it shows, so the whole id is readable.
class WidenOnShow final : public QObject {
public:
    WidenOnShow(QWidget *popup, int width) : QObject(popup), m_width(width) {}
protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::Show)
            if (auto *widget = qobject_cast<QWidget *>(object); widget && widget->width() < m_width)
                widget->resize(m_width, widget->height());
        return false;
    }
private:
    int m_width;
};

void repolish(QWidget *widget) {
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

// ----- rows an agent changed (card #FEJQ, protocol §30.6) ------------------------------------
// One mark per row, process-wide: the Options pane a person is looking at is rarely the pane the
// change came through, and there may be several open in several windows.
struct AgentMark { QString previous, value; QDateTime when; };
QHash<QString, AgentMark> &agentMarks() {
    static QHash<QString, AgentMark> marks;
    return marks;
}

QLabel *mutedLabel(const QString &text, const char *name) {
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    label->setObjectName(QLatin1String(name));
    return label;
}

QLabel *keyCap(const QString &text) {
    auto *chip = new QLabel(text);
    chip->setObjectName(QStringLiteral("keyCap"));
    chip->setTextFormat(Qt::PlainText);
    return chip;
}

constexpr int kRecentRows = 6;
constexpr int kMaxResults = 60;

// A reset cannot be undone, so it is the one thing in this pane that asks first. The page is named
// in the question and on the button itself, because the same row sits on nearly every page and the
// one you meant is the one you were looking at. Cancel is the default button, so Enter on the
// dialog — the key that opened it — cannot be the key that throws the page away.
bool askBeforeReset(const QString &sectionTitle) {
    QMessageBox box(QApplication::activeWindow());
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(QStringLiteral("Reset %1").arg(sectionTitle));
    box.setText(QStringLiteral("Reset the %1 options to what Relay ships with?").arg(sectionTitle));
    box.setInformativeText(QStringLiteral("Only this page changes. It cannot be undone."));
    QPushButton *reset = box.addButton(QStringLiteral("Reset %1").arg(sectionTitle), QMessageBox::AcceptRole);
    QPushButton *cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancel);
    box.setEscapeButton(cancel);
    box.exec();
    return box.clickedButton() == reset;
}

// A page carries a dot on its tab when something on it is not what Relay ships with — the same
// question the ↺ on a row answers, asked of the whole page, so a changed option is visible from a
// tab you are not looking at.
bool sectionChanged(const SettingsSection &section) {
    for (const SettingRow &row : section.rows)
        if (row.changed && row.reset) return true;
    return false;
}

// The Browse… button's picker, replaced by tests. It starts where the box points, so Browse… on a
// folder you already chose opens there rather than at home.
std::function<QString(QWidget *, const QString &)> g_folderChooser;

QString chooseFolder(QWidget *parent, const QString &start) {
    const QString where = start.isEmpty() ? QDir::homePath() : start;
    if (g_folderChooser) return g_folderChooser(parent, where);
    return QFileDialog::getExistingDirectory(parent, QStringLiteral("Choose a folder"), where,
                                             QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
}

}  // namespace

void SettingsPane::setFolderChooser(std::function<QString(QWidget *parent, const QString &start)> chooser) {
    g_folderChooser = std::move(chooser);
}

// ----- the watch ------------------------------------------------------------------------------------

SettingsWatch &SettingsWatch::instance() {
    static SettingsWatch watch;
    return watch;
}

void SettingsWatch::listen(QObject *context, std::function<void()> changed) {
    if (!context || !changed) return;
    m_listeners.append({QPointer<QObject>(context), std::move(changed)});
}

void SettingsWatch::notify() {
    // Before anything else, and before the delivery is collapsed away: the values read on hot
    // paths are cached (card #057J) and a write has just happened, so the cache is stale from this
    // instant. Dropping it here rather than in the delivery below means a setting changed in
    // Options is in effect for the very next reader, not for the next turn of the event loop.
    relay::settings::invalidate();
    if (m_scheduled) return;            // one delivery for a burst: a page reset writes many rows
    m_scheduled = true;
    QTimer::singleShot(0, [this] {
        m_scheduled = false;
        for (int i = int(m_listeners.size()) - 1; i >= 0; --i)
            if (!m_listeners.at(i).first) m_listeners.removeAt(i);
        const auto listeners = m_listeners;   // a callback may close a pane, and so drop a listener
        for (const auto &listener : listeners)
            if (listener.first) listener.second();
    });
}

// ----- rows an agent changed (card #FEJQ, protocol §30.6) --------------------------------------------

void SettingsPane::markAgentChanged(const QString &rowId, const QString &previous, const QString &value) {
    if (rowId.isEmpty()) return;
    agentMarks().insert(rowId, AgentMark{previous, value, QDateTime::currentDateTime()});
    SettingsWatch::instance().notify();     // every open Options pane draws the note
}

void SettingsPane::clearAgentChanged(const QString &rowId) {
    if (agentMarks().remove(rowId) > 0) SettingsWatch::instance().notify();
}

QString SettingsPane::agentChangeNote(const QString &rowId) {
    const auto found = agentMarks().constFind(rowId);
    if (found == agentMarks().constEnd()) return {};
    // "just now" for the first minute, then minutes and hours: the same reading the notification
    // centre gives its entries, so the two never disagree about when something happened.
    const qint64 seconds = found->when.secsTo(QDateTime::currentDateTime());
    const QString when = seconds < 60 ? QStringLiteral("just now")
                       : seconds < 3600 ? QStringLiteral("%1 min ago").arg((seconds + 30) / 60)
                                        : QStringLiteral("%1 h ago").arg(seconds / 3600);
    return QStringLiteral("changed by the agent %1: %2 → %3").arg(when, found->previous, found->value);
}

void SettingsPane::forgetAgentChanges() {
    if (agentMarks().isEmpty()) return;
    agentMarks().clear();
    SettingsWatch::instance().notify();
}

// ----- reset to defaults ---------------------------------------------------------------------------

// One button per page, and it knows nothing about settings keys: it holds the resets of the rows it
// was built from, so it covers exactly what is on that page and a row added to the page later is
// covered the day it declares its default. Nothing else here reads QSettings on its behalf.
SettingRow resetRow(const SettingsSection &section, std::function<void(int count)> after,
                    std::function<bool(const QString &sectionTitle)> ask) {
    QList<std::function<void()>> resets;
    for (const SettingRow &row : section.rows)
        if (row.reset) resets << row.reset;
    if (resets.isEmpty()) return {};        // nothing on this page has a default: no button
    SettingRow row;
    row.kind = SettingRow::Button;
    row.id = QStringLiteral("reset:") + section.id;
    row.label = QStringLiteral("Reset to defaults");
    row.detail = (resets.size() == 1 ? QStringLiteral("Puts the one option on this page")
                                     : QStringLiteral("Puts the %1 options on this page").arg(resets.size()))
                 // What it leaves alone is worth saying, because those are what a reset button is
                 // feared for: the Keyboard page puts the preset back but never your own bindings,
                 // and no page here can reach a key or a saved server, which are not values.
                 + QStringLiteral(" back to what Relay ships with, at once. No other page changes, and your "
                                  "API keys, saved servers and custom shortcuts are left alone");
    row.aliases = QStringLiteral("reset defaults factory restore revert undo original fresh start over");
    row.buttonText = QStringLiteral("Reset…");
    row.run = [resets, title = section.title, after = std::move(after), ask = std::move(ask)] {
        if (!(ask ? ask(title) : askBeforeReset(title))) return;
        for (const std::function<void()> &reset : resets) reset();
        if (after) after(int(resets.size()));
    };
    return row;
}


// ----- the helper agent's context (card #AGNT step 7) -----------------------------------------
//
// What the agent in this pane is *about*, and nothing else: which of Options and Actions it is
// (one class, because they are one widget and `setMode` swaps the answer), what is on screen, and
// where an `option:` link in an answer goes. The console — the prompt box, the queue, the
// transcript — is a no-shell `Pane` the window builds; it is the same surface a terminal has, and
// nothing about it is written here (src/AgentContext.h).
class OptionsContext final : public agent::Context {
  public:
    explicit OptionsContext(SettingsPane *pane) : m_pane(pane) {}

    agent::ContextSpec spec() const override {
        const bool actions = m_pane->mode() == SettingsPane::Mode::Actions;
        agent::ContextSpec spec;
        // The mode decides both: a turn asked in Actions is answered by the Actions helper, with
        // the Actions brief, on the same conversation. That is what "the helper follows the mode"
        // has always meant here (#FEJQ) — only now it is one field rather than a `pane` tag.
        spec.name = actions ? QStringLiteral("actions") : QStringLiteral("options");
        spec.surface = spec.name;
        spec.agentRole = QStringLiteral("switchboard");
        spec.workspace = m_workspace;
        // Named, never inferred: a board-less helper that falls through the worker's inference
        // gets the *pane* branch and the full executor (§33.3, card #AGNT finding).
        spec.scope = QStringLiteral("console");
        // `persist.scope` is a wire enum — "", "pane" or "helper" — not a path. No tab id means no
        // store, which is a supported state and how a pane with no window says so.
        if (!m_tabId.isEmpty()) {
            spec.persistScope = QStringLiteral("helper");
            spec.persistKey = m_tabId;
        }
        spec.briefKey = spec.name;
        spec.briefTitle = title();
        spec.screen = screen();
        spec.shell = false;          // every context but the terminal's
        spec.routing = QStringLiteral("agent");   // everything typed here is a prompt
        return spec;
    }

    QString placeholder() const override {
        return QStringLiteral("Ask the %1 helper…").arg(
            m_pane->mode() == SettingsPane::Mode::Actions ? QStringLiteral("Actions")
                                                          : QStringLiteral("Options"));
    }

    // `option:<section>/<row>` is this pane's own business: the helper was asked about a setting
    // and answered with the way to it, so the row is revealed *here* rather than the window being
    // asked for a second Options pane. Everything else — a card, a path, a URL, a `session:` —
    // is the window's, and false is how this says so.
    bool resolveLink(const relay::links::Target &target) override {
        QString section, row;
        if (!relay::links::optionOf(target.target, &section, &row)) return false;
        m_pane->revealOption(section, row);
        return true;
    }

    QString title() const {
        return m_pane->mode() == SettingsPane::Mode::Actions ? QStringLiteral("Actions helper")
                                                             : QStringLiteral("Options helper");
    }

    void setTabId(const QString &tabId) { if (tabId != m_tabId) { m_tabId = tabId; changed(); } }
    void setWorkspace(const QString &workspace) {
        if (workspace != m_workspace) { m_workspace = workspace; changed(); }
    }

  private:
    // What is being read right now, for the "On screen now:" line above the prompt (§33). It is a
    // hint, not a context dump: the page and the search first, because those are what a question
    // is nearly always about, then as many of the visible row ids as fit. The catalog itself is
    // never pasted in — the agent reads the rows live.
    QString screen() const {
        QStringList lines;
        if (m_pane->mode() == SettingsPane::Mode::Actions) {
            lines << QStringLiteral("Actions, the whole list");
        } else {
            lines << QStringLiteral("Options › %1").arg(m_pane->currentTab());
        }
        const QString search = m_pane->search().trimmed();
        if (!search.isEmpty()) lines << QStringLiteral("Search: %1").arg(search);
        QStringList ids;
        int room = agent::kScreenLimit - lines.join(QLatin1Char('\n')).size() - 16;
        for (const QString &id : m_pane->visibleRowIds()) {
            room -= id.size() + 2;
            if (room <= 0) { ids << QStringLiteral("…"); break; }
            ids << id;
        }
        if (!ids.isEmpty()) lines << QStringLiteral("Rows: %1").arg(ids.join(QStringLiteral(", ")));
        return lines.join(QLatin1Char('\n'));
    }

    SettingsPane *m_pane = nullptr;
    QString m_tabId, m_workspace;
};

// The collapsed row's mark (owner, 2026-09-20: "it should have a question mark icon next to it").
// Painted rather than typed for the reason the helper panel's copy gave: a glyph from the button's
// font is whatever the desktop's font has at 14 px, which on this row is a "?" a third of the
// height of the word beside it. A ringed question mark reads as "ask" at that size.
static QIcon askIcon(const QColor &ink) {
    constexpr int kSize = 14;
    QPixmap pixmap(kSize * 2, kSize * 2);        // 2x, so it stays crisp on a scaled desktop
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(ink, 1.2);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QRectF(1.2, 1.2, 11.6, 11.6));
    painter.drawArc(QRectF(4.3, 3.4, 5.4, 4.6), 200 * 16, -250 * 16);
    painter.drawLine(QPointF(7.0, 7.4), QPointF(7.0, 9.0));
    QPen dot(ink, 1.6);
    dot.setCapStyle(Qt::RoundCap);
    painter.setPen(dot);
    painter.drawPoint(QPointF(7.0, 10.8));
    painter.end();
    return QIcon(pixmap);
}

// ----- construction ------------------------------------------------------------------------------

SettingsPane::SettingsPane(Mode mode, std::function<QList<SettingsSection>()> sections,
                           std::function<QList<ActionItem>()> actions, QWidget *parent)
    : QWidget(parent), m_sections(std::move(sections)), m_actions(std::move(actions)), m_mode(mode) {
    setObjectName(QStringLiteral("settingsPane"));
    setAttribute(Qt::WA_StyledBackground);
    setFocusPolicy(Qt::StrongFocus);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 8);
    layout->setSpacing(8);

    auto *header = new QHBoxLayout;
    header->setSpacing(8);
    // No title of its own: the pane's header names it (Actions / Options), as it names every pane.
    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("settingsSearch"));
    m_search->setClearButtonEnabled(true);
    m_search->installEventFilter(this);
    // Ctrl+N / Ctrl+P walk the results (eventFilter below); the window's own Ctrl+N (New window)
    // gives way while the search has the keyboard, because the widget says so here (#KYPR).
    m_search->setProperty("relayLocalKeys", QStringList{QStringLiteral("Ctrl+N"), QStringLiteral("Ctrl+P")});
    header->addWidget(m_search, 1);
    // No close button of its own: the pane's × in the chrome row closes it, as it closes every
    // other pane. It used to keep the ✕ from its days as an overlay, and the two landed on top of
    // each other in the top-right corner (owner report, 2026-09-18).
    m_header = header;
    layout->addLayout(header);

    m_tabs = new QTabBar(this);
    relay::paneTabs::registerTabs(this, m_tabs);
    m_tabs->setObjectName(QStringLiteral("settingsTabs"));
    m_tabs->setExpanding(false);
    m_tabs->setDrawBase(false);
    m_tabs->setUsesScrollButtons(true);
    m_tabs->setElideMode(Qt::ElideNone);
    m_tabs->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(m_tabs);

    m_pages = new QStackedWidget;
    layout->addWidget(m_pages, 1);

    m_footer = new QLabel;
    m_footer->setObjectName(QStringLiteral("settingsFooter"));
    m_footer->setTextFormat(Qt::PlainText);
    layout->addWidget(m_footer);

    // The helper agent, under the footer and collapsed to one row at the bottom right (#FEJQ,
    // card #AGNT step 7): a context this pane owns, and a console the window builds for it on the
    // first expand. The context outlives the console, which is what §33 requires of a host.
    m_context = new OptionsContext(this);
    buildHelperRow(layout);
    applyMode();

    connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) {
        if (m_building || index < 0) return;
        m_wantedTab = m_tabIds.value(index);
        if (m_search->text().trimmed().isEmpty()) {
            m_pages->setCurrentIndex(index);
            m_rows = m_pageRows.value(m_pages->widget(index));
            setCurrent(-1, false);
        }
        announceTab();
    });
    connect(m_search, &QLineEdit::textChanged, this, [this] {
        const QString needle = m_search->text().trimmed();
        if (needle.isEmpty()) {
            const int index = std::max(0, m_tabs->currentIndex());
            m_pages->setCurrentIndex(index);
            m_rows = m_pageRows.value(m_pages->widget(index));
            setCurrent(-1, false);
            return;
        }
        buildResults(needle);
    });
    // Every pane redraws when any of them writes a value, so two Options panes — in two tabs or two
    // windows — never disagree about what a setting is.
    SettingsWatch::instance().listen(this, [this] { rebuild(); });
    build();
}

// ----- mode -----------------------------------------------------------------------------------------

// The words that say which pane this is; the rows themselves come from build().
void SettingsPane::applyMode() {
    const bool actions = m_mode == Mode::Actions;
    const QString hint = actions ? QStringLiteral("Search actions") : QStringLiteral("Search options");
    m_search->setPlaceholderText(hint);
    m_search->setAccessibleName(hint);
    // Actions is one list; there is nothing to tab between. Embedded, the host's own tab row is
    // already saying where you are and its footer already spells the keys.
    m_tabs->setVisible(!actions && !m_embedded);
    m_footer->setVisible(!m_embedded);
    m_footer->setText(actions ? QStringLiteral("↑ ↓ move   ·   Enter runs   ·   Esc closes")
                              : QStringLiteral("↑ ↓ move   ·   Enter changes   ·   ← → tabs   ·   Esc closes"));
}

void SettingsPane::setEmbedded(bool embedded) {
    if (embedded == m_embedded) return;
    m_embedded = embedded;
    // The host has its own margins; this one would be a second frame inside the first.
    if (auto *box = layout()) box->setContentsMargins(embedded ? 0 : 12, embedded ? 0 : 10,
                                                      embedded ? 0 : 12, embedded ? 0 : 8);
    applyMode();
}

void SettingsPane::setMode(Mode mode) {
    if (mode == m_mode) return;
    m_mode = mode;
    // The helper is about the pane in front of you, and the pane has just become the other one:
    // from here `spec()` answers "actions" rather than "options" and the worker picks the other
    // brief (§33). The console is left alone — it is one conversation with one worker, and the
    // turns already in it were answers to this person.
    if (m_context) m_context->changed();
    updateHelperRow();
    m_wantedTab.clear();
    {
        const QSignalBlocker blocker(m_search);
        m_search->clear();
    }
    applyMode();
    build();
    if (onModeChanged) onModeChanged();
}

void SettingsPane::revealOption(const QString &sectionId, const QString &rowId) {
    setMode(Mode::Options);
    {
        const QSignalBlocker blocker(m_search);
        m_search->clear();
    }
    showTab(sectionId);
    const int index = std::max(0, m_tabs->currentIndex());
    m_pages->setCurrentIndex(index);
    m_rows = m_pageRows.value(m_pages->widget(index));
    setCurrent(-1, false);
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].id != rowId) continue;
        setCurrent(i, false);
        // The page may never have been shown, so its layout settles on the next pass.
        QPointer<QWidget> row(m_rows[i].widget);
        QTimer::singleShot(0, this, [this, row] {
            if (QScrollArea *area = currentScroll(); area && row) area->ensureWidgetVisible(row, 0, 24);
        });
        break;
    }
}

// ----- catalog → widgets -------------------------------------------------------------------------

void SettingsPane::build() {
    m_building = true;
    const QString tab = m_wantedTab.isEmpty() ? m_tabIds.value(m_tabs->currentIndex()) : m_wantedTab;
    m_sectionCache = m_sections ? m_sections() : QList<SettingsSection>();
    m_actionCache = m_actions ? m_actions() : QList<ActionItem>();
    m_pageRows.clear();
    m_groups.clear();
    m_rows.clear();
    m_current = -1;
    while (m_pages->count() > 0) {
        QWidget *page = m_pages->widget(0);
        m_pages->removeWidget(page);
        page->deleteLater();
    }
    {
        const QSignalBlocker blocker(m_tabs);
        while (m_tabs->count() > 0) m_tabs->removeTab(0);
    }
    m_tabIds.clear();
    if (m_mode == Mode::Actions) {
        // One page, no tabs: the list. The option sections stay cached for the search.
        m_tabIds << actionsTabId();
        m_tabs->addTab(QStringLiteral("Actions"));
        auto *scroll = new QScrollArea;
        scroll->setObjectName(QStringLiteral("settingsPage"));
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *body = new QWidget;
        body->setObjectName(QStringLiteral("settingsPageBody"));
        body->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto *list = new QVBoxLayout(body);
        list->setContentsMargins(2, 0, 8, 12);
        list->setSpacing(2);
        addActionsList(list);
        list->addStretch(1);
        scroll->setWidget(body);
        m_pages->addWidget(scroll);
        m_pageRows.insert(scroll, m_rows);
    }
    for (const SettingsSection &section : std::as_const(m_sectionCache)) {
        if (m_mode == Mode::Actions) break;
        m_tabIds << section.id;
        const bool touched = sectionChanged(section);
        m_tabs->addTab(touched ? section.title + QStringLiteral(" •") : section.title);
        if (touched)
            m_tabs->setTabToolTip(m_tabs->count() - 1,
                                  QStringLiteral("Something on this page is not what Relay ships with"));
        auto *scroll = new QScrollArea;
        scroll->setObjectName(QStringLiteral("settingsPage"));
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *body = new QWidget;
        body->setObjectName(QStringLiteral("settingsPageBody"));
        // Exactly as wide as the viewport, never wider: a long combo label must squeeze and a
        // long row label must wrap, or the controls at the right edge end up clipped.
        body->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_rows.clear();
        buildPage(body, section);
        scroll->setWidget(body);
        m_pages->addWidget(scroll);
        m_pageRows.insert(scroll, m_rows);
    }
    m_results = new QScrollArea;
    m_results->setObjectName(QStringLiteral("settingsPage"));
    m_results->setWidgetResizable(true);
    m_results->setFrameShape(QFrame::NoFrame);
    m_results->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_pages->addWidget(m_results);
    m_rows.clear();

    int index = std::max(0, int(m_tabIds.indexOf(tab)));
    {
        const QSignalBlocker blocker(m_tabs);
        m_tabs->setCurrentIndex(index);
    }
    m_wantedTab = m_tabIds.value(index);
    m_building = false;
    const QString needle = m_search->text().trimmed();
    if (needle.isEmpty()) {
        m_pages->setCurrentIndex(index);
        m_rows = m_pageRows.value(m_pages->widget(index));
        setCurrent(-1, false);
    } else {
        buildResults(needle);
    }
    announceTab();
}

// Once per arrival, so a rebuild (any control writing its value) never fires it again.
void SettingsPane::announceTab() {
    const QString id = currentTab();
    if (id.isEmpty() || id == m_shownTab) return;
    m_shownTab = id;
    // The page under the helper moved, so what `spec()` answers for `screen` moved with it.
    if (m_context) m_context->changed();
    if (onSectionShown) onSectionShown(id);
}

void SettingsPane::buildPage(QWidget *page, const SettingsSection &section) {
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(2, 6, 8, 12);
    layout->setSpacing(2);
    if (!section.blurb.isEmpty()) {
        layout->addWidget(mutedLabel(section.blurb, "settingsBlurb"));
        layout->addSpacing(6);
    }
    bool folded = false;   // under a collapsed heading, until the next heading
    int rowIndex = 0;
    for (const SettingRow &row : section.rows) {
        if (row.kind == SettingRow::Heading) folded = row.collapsible && headingCollapsed(row);
        // The divider between two groups of rows (`SettingRow::ruleAbove`): a 1px line in the
        // theme's `@border`, the colour a section heading is underlined in. It belongs to the row
        // under it, so it folds with it and never leads a page.
        QWidget *rule = nullptr;
        if (row.ruleAbove && rowIndex > 0) { rule = sectionRule(); layout->addWidget(rule); }
        QWidget *widget = settingRow(row);
        if (row.kind == SettingRow::Heading || row.kind == SettingRow::Subheading)
            m_groups.insert(QStringLiteral("option-section:") + section.id + QLatin1Char(':') + QString::number(rowIndex), widget);
        ++rowIndex;
        layout->addWidget(widget);
        if (folded && row.kind != SettingRow::Heading) { widget->hide(); if (rule) rule->hide(); }
    }
    layout->addStretch(1);
}

// One divider. A styled QWidget rather than a QFrame line, so the colour is the theme's `@border`
// token and follows a theme change like every other rule in the app.
QWidget *SettingsPane::sectionRule() {
    auto *line = new QWidget;
    line->setObjectName(QStringLiteral("settingsSectionRule"));
    line->setAttribute(Qt::WA_StyledBackground);
    line->setFixedHeight(1);
    line->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return line;
}

bool SettingsPane::headingCollapsed(const SettingRow &row) {
    return QSettings().value(QStringLiteral("options/collapsed/") + row.id, row.collapsedByDefault).toBool();
}

QWidget *SettingsPane::groupHeader(const QString &text, const QString &key) {
    auto *label = mutedLabel(text, "settingsHeading");   // as written, not shouted (owner, 2026-09-20)
    if (!key.isEmpty()) m_groups.insert(key, label);
    return label;
}

// The actions with their keys: Recent first, then each section in the order the caller listed
// them, submenus opened inline under their own header.
void SettingsPane::addActionsList(QVBoxLayout *into) {
    into->addSpacing(6);
    const QStringList recent = QSettings().value(QStringLiteral("palette/recent")).toStringList();
    QList<ActionItem> recentItems;
    for (const QString &key : recent) {
        for (const ActionItem &item : std::as_const(m_actionCache))
            if (item.key == key && !item.children && recentItems.size() < kRecentRows) recentItems << item;
    }
    if (!recentItems.isEmpty()) {
        into->addWidget(groupHeader(QStringLiteral("Recent"), QStringLiteral("action-section:Recent")));
        for (const ActionItem &item : std::as_const(recentItems)) into->addWidget(actionRow(item));
    }
    QStringList order;
    for (const ActionItem &item : std::as_const(m_actionCache))
        if (!order.contains(item.section)) order << item.section;
    for (const QString &section : std::as_const(order)) {
        into->addWidget(groupHeader(section, QStringLiteral("action-section:") + section));
        for (const ActionItem &item : std::as_const(m_actionCache)) {
            if (item.section != section) continue;
            if (!item.children) { into->addWidget(actionRow(item)); continue; }
            const QString commands = actionSlashCommands(item.key);
            into->addWidget(groupHeader(item.label
                + (commands.isEmpty() ? QString() : QStringLiteral("  ·  ") + commands)
                + (item.detail.isEmpty() ? QString() : QStringLiteral("  ·  ") + item.detail), item.key));
            for (const ActionItem &child : item.children()) into->addWidget(actionRow(child));
        }
    }
}

QWidget *SettingsPane::settingRow(const SettingRow &row) {
    if (row.kind == SettingRow::Heading) {
        if (!row.collapsible) return groupHeader(row.label);
        // A folding heading: the disclosure mark, the words, and a click that flips the fold and
        // redraws every Options pane (the rows under it are hidden in buildPage).
        const bool folded = headingCollapsed(row);
        auto *head = new ClickRow;
        head->setObjectName(QStringLiteral("settingsRow"));
        head->setAttribute(Qt::WA_StyledBackground);
        head->setProperty("rowId", row.id);
        head->setCursor(Qt::PointingHandCursor);
        auto *box = new QHBoxLayout(head);
        box->setContentsMargins(0, 0, 0, 0);
        auto *label = mutedLabel((folded ? QStringLiteral("▸ ") : QStringLiteral("▾ ")) + row.label, "settingsHeading");
        box->addWidget(label, 1);
        head->onClick = [id = row.id, folded, dflt = row.collapsedByDefault] {
            QSettings settings;
            const QString key = QStringLiteral("options/collapsed/") + id;
            if (!folded == dflt) settings.remove(key); else settings.setValue(key, !folded);
            SettingsWatch::instance().notify();
        };
        return head;
    }
    if (row.kind == SettingRow::Subheading) {
        // A group inside a section (one provider's models): the words as written, bold, with a
        // little air above — a heading is the section's own name and stays the louder of the two.
        auto *sub = new QLabel(row.label);
        sub->setObjectName(QStringLiteral("settingsSubheading"));
        sub->setWordWrap(true);
        sub->setTextFormat(Qt::PlainText);
        sub->setContentsMargins(10, 12, 10, 2);
        return sub;
    }
    if (row.kind == SettingRow::Info) {
        auto *info = mutedLabel(row.label, "settingsInfo");
        info->setContentsMargins(8, 4, 8, 4);
        return info;
    }
    auto *line = new ClickRow;
    line->setObjectName(QStringLiteral("settingsRow"));
    line->setAttribute(Qt::WA_StyledBackground);
    line->setProperty("rowId", row.id);
    if (!row.dragGroup.isEmpty()) {
        line->dragGroup = row.dragGroup;
        line->rowId = row.id;
        line->onDropBefore = row.onDropBefore;
        line->enableDrag();
    }
    auto *box = new QHBoxLayout(line);
    // A row under a group's own row is single-spaced (owner, 2026-09-20: "make them single
    // spaced within a provider"): the models of one provider read as one list, not as cards.
    const bool nested = row.indent > 0;
    if (nested) line->setProperty("nested", true);
    box->setContentsMargins(10 + 28 * qMax(0, row.indent), nested ? 0 : 6, 10, nested ? 0 : 6);
    box->setSpacing(12);
    if (!row.dragGroup.isEmpty()) {
        // A grip, so a row that can be dragged looks like one (owner, 2026-09-20: the priority
        // list "should also be draggable, not just arrows" — it was, and nothing said so).
        auto *grip = mutedLabel(QStringLiteral("⠿"), "settingsGrip");
        grip->setToolTip(QStringLiteral("Drag to reorder"));
        grip->setCursor(Qt::OpenHandCursor);
        box->addWidget(grip);
    }
    auto *text = new QVBoxLayout;
    text->setSpacing(1);
    auto *label = new QLabel(row.label);
    label->setObjectName(row.strong ? QStringLiteral("settingsRowLabelStrong") : QStringLiteral("settingsRowLabel"));
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    text->addWidget(label);
    if (!row.detail.isEmpty()) text->addWidget(mutedLabel(row.detail, "settingsRowDetail"));
    // An agent wrote this row and the person has not touched it since: say so under the row, in
    // the detail's own muted voice (#FEJQ, §30.6 — "the row is marked … so 'what did it do to my
    // settings' is answerable by looking"). The mark goes at the first hand edit, below.
    if (const QString note = agentChangeNote(row.id); !note.isEmpty()) {
        auto *marker = mutedLabel(note, "settingsRowAgentNote");
        marker->setProperty("agentChanged", true);
        text->addWidget(marker);
    }
    // A wrapping label will shrink to its longest word, so in a narrow pane the control beside it
    // took the row and "Log detail" and its detail came out a word or two per line. The words keep
    // a column of about eighteen characters (less when they are shorter); the control gives way.
    const QFontMetrics metrics(label->font());
    const int words = std::max(metrics.horizontalAdvance(row.label), metrics.horizontalAdvance(row.detail) * 9 / 10);
    label->setMinimumWidth(std::min(words + 2, metrics.averageCharWidth() * 18));
    // A row with no words (a lone "+ add a model…" button under a list) keeps its control at the
    // left, where the list's rows begin, rather than out at the right edge.
    const bool wordless = row.label.isEmpty() && row.detail.isEmpty();
    box->addLayout(text, wordless ? 0 : 1);

    if (!row.tooltip.isEmpty()) { line->setToolTip(row.tooltip); label->setToolTip(row.tooltip); }
    if (!row.infoUrl.isEmpty()) {
        auto *info = new QToolButton;
        info->setObjectName(QStringLiteral("settingsInfoLink"));
        info->setText(QStringLiteral("ⓘ"));
        info->setAutoRaise(true);
        info->setCursor(Qt::PointingHandCursor);
        info->setFocusPolicy(Qt::NoFocus);
        info->setToolTip(QStringLiteral("About this: %1").arg(row.infoUrl));
        info->setAccessibleName(QStringLiteral("About %1").arg(row.label));
        if (row.indent > 0) info->setFixedHeight(18);   // the link must not make a nested row taller than its text
        connect(info, &QToolButton::clicked, this, [url = row.infoUrl] { QDesktopServices::openUrl(QUrl(url)); });
        box->addWidget(info);
    }
    // Every control writes through the row's callback and then says so, which redraws this pane —
    // so rows that describe other rows ("Flash · glm-5.3-flash") never go stale — and every other
    // Options pane with it. Focus and scroll survive the rebuild, and the rebuild happens on the
    // event loop, never inside the signal of the control it is about to delete.
    // The person has answered whatever an agent did to this row, so its marker goes with the edit
    // (#FEJQ, §30.6: the mark lasts "until the person touches the row"). Every control on the row
    // writes through here, so there is one place that has to remember.
    auto after = [id = row.id] { clearAgentChanged(id); SettingsWatch::instance().notify(); };

    // The changed indicator and the way back in one mark: a row whose value is not what Relay
    // ships with carries a ↺ between its words and its control (finding 8 of card #XZZB). It asks
    // nothing first, where the page's "Reset to defaults" does — one row is one value, in front of
    // you, and set again in a click; a page is everything on it and cannot be undone row by row.
    if (row.changed && row.reset) {
        auto *undo = new QPushButton(QStringLiteral("↺"));
        undo->setObjectName(QStringLiteral("settingsRowReset"));
        undo->setFlat(true);
        undo->setCursor(Qt::PointingHandCursor);
        undo->setFocusPolicy(Qt::TabFocus);
        undo->setToolTip(QStringLiteral("Back to what Relay ships with"));
        undo->setAccessibleName(QStringLiteral("Reset %1 to what Relay ships with").arg(row.label));
        connect(undo, &QPushButton::clicked, this, [fn = row.reset, after] { fn(); after(); });
        box->addWidget(undo);
    }

    Row entry;
    entry.id = row.id;
    entry.widget = line;
    switch (row.kind) {
    case SettingRow::Toggle: {
        auto *check = new QCheckBox;
        check->setChecked(row.checked);
        check->setAccessibleName(row.label);
        check->setFocusPolicy(Qt::TabFocus);
        connect(check, &QCheckBox::toggled, this, [fn = row.onToggle, after](bool on) {
            if (fn) fn(on);
            after();
        });
        line->onClick = [check] { check->toggle(); };
        line->setCursor(Qt::PointingHandCursor);
        entry.activate = [check] { check->toggle(); };
        box->addWidget(check);
        break;
    }
    case SettingRow::Choice: {
        auto *combo = new QComboBox;
        combo->setAccessibleName(row.label);
        combo->setFocusPolicy(Qt::TabFocus);
        // Sized to its entries but capped, so a long option label (a voice model with its
        // price) cannot squeeze the label column; the popup still shows every entry in full.
        combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        combo->setMaximumWidth(340);
        for (int i = 0; i < row.options.size(); ++i)
            combo->addItem(i < row.optionLabels.size() ? row.optionLabels.at(i) : row.options.at(i), row.options.at(i));
        const int index = combo->findData(row.current);
        if (index >= 0) combo->setCurrentIndex(index);
        // It may shrink below its widest entry (about ten characters and the arrow) when the pane
        // is narrow, rather than squeeze the label column; the popup still shows every entry.
        combo->setMinimumWidth(std::min(combo->sizeHint().width(), combo->fontMetrics().averageCharWidth() * 10 + 40));
        connect(combo, QOverload<int>::of(&QComboBox::activated), this, [combo, fn = row.onChoose, after](int i) {
            if (fn) fn(combo->itemData(i).toString());
            after();
        });
        entry.activate = [combo] { combo->setFocus(Qt::OtherFocusReason); combo->showPopup(); };
        box->addWidget(combo);
        // A dropdown row may carry small buttons after it (a tier list's row: its level, then ×).
        for (int i = 0; i < row.buttonTexts.size(); ++i) {
            auto *button = new QPushButton(row.buttonTexts.at(i));
            button->setObjectName(QStringLiteral("settingsRowSmallButton"));
            button->setFocusPolicy(Qt::TabFocus);
            connect(button, &QPushButton::clicked, this, [fn = row.onButton, after, i] { if (fn) fn(i); after(); });
            box->addWidget(button);
        }
        break;
    }
    case SettingRow::Text: {
        auto *edit = new QLineEdit(row.text);
        edit->setPlaceholderText(row.placeholder);
        edit->setAccessibleName(row.label);
        if (!row.completions.isEmpty()) {
            // opencode's model box: the ids the provider serves, offered as you type, matched
            // anywhere in the id ("flash" finds z-ai/glm-5.3-flash). Enter on a suggestion fills
            // the box; the row's own editingFinished then writes it.
            auto *completer = new QCompleter(row.completions, edit);
            completer->setCaseSensitivity(Qt::CaseInsensitive);
            completer->setFilterMode(Qt::MatchContains);
            completer->setCompletionMode(QCompleter::PopupCompletion);
            edit->setCompleter(completer);
            // Room for a full id in the box and under it.
            edit->setMaximumWidth(520);
            completer->popup()->installEventFilter(new WidenOnShow(completer->popup(), 560));
        }
        edit->setMinimumWidth(140);   // narrower than that, the label column goes first
        edit->setMaximumWidth(360);
        connect(edit, &QLineEdit::editingFinished, this, [edit, fn = row.onText, after, was = row.text] {
            if (edit->text().trimmed() == was) return;
            if (fn) fn(edit->text().trimmed());
            after();
        });
        entry.activate = [edit] { edit->setFocus(Qt::OtherFocusReason); edit->selectAll(); };
        box->addWidget(edit);
        // A path row is browsed to, not only typed: the box keeps working exactly as it did, and
        // the button fills it in. The write goes through the row's own callback, so a folder picked
        // here and a folder typed there are the same edit.
        if (row.browse) {
            auto *pick = new QPushButton(QStringLiteral("Browse…"));
            pick->setObjectName(QStringLiteral("settingsBrowse"));
            pick->setFocusPolicy(Qt::TabFocus);
            pick->setAccessibleName(QStringLiteral("Choose a folder for %1").arg(row.label));
            connect(pick, &QPushButton::clicked, this, [this, edit, fn = row.onText, after] {
                const QString picked = chooseFolder(this, edit->text().trimmed());
                if (picked.isEmpty()) return;             // cancelled: the row keeps what it had
                edit->setText(picked);
                if (fn) fn(picked);
                after();
            });
            box->addWidget(pick);
        }
        break;
    }
    case SettingRow::Number: {
        auto *spin = new QSpinBox;
        spin->setRange(row.minimum, row.maximum);
        spin->setValue(row.number);
        spin->setAccessibleName(row.label);
        spin->setKeyboardTracking(false);   // one write per finished edit or arrow press, not per keystroke
        if (!row.suffix.isEmpty()) spin->setSuffix(row.suffix);
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [fn = row.onNumber](int value) { if (fn) fn(value); });
        entry.activate = [spin] { spin->setFocus(Qt::OtherFocusReason); spin->selectAll(); };
        box->addWidget(spin);
        break;
    }
    case SettingRow::Button: {
        auto *button = new QPushButton(row.buttonText.isEmpty() ? QStringLiteral("Open…") : row.buttonText);
        button->setFocusPolicy(Qt::TabFocus);
        connect(button, &QPushButton::clicked, this, [fn = row.run, after] {
            if (fn) fn();
            after();
        });
        entry.activate = [button] { button->click(); };
        box->addWidget(button);
        break;
    }
    case SettingRow::Buttons: {
        // The buttons live in one host so they can move as a block. In a wide pane they sit
        // beside the words; when that would leave the words a column a few characters wide
        // (owner report, 2026-09-20: "key / stored / in the / keyring" down the page and a
        // clipped "replace key…"), the block drops under the words and the text gets the width.
        auto *host = new QWidget;
        auto *hostBox = new QHBoxLayout(host);
        hostBox->setContentsMargins(0, 0, 0, 0);
        hostBox->setSpacing(8);
        QPushButton *first = nullptr;
        for (int i = 0; i < row.buttonTexts.size(); ++i) {
            auto *button = new QPushButton(row.buttonTexts.at(i));
            button->setFocusPolicy(Qt::TabFocus);
            button->setMinimumWidth(button->sizeHint().width());   // a label is never clipped
            connect(button, &QPushButton::clicked, this, [fn = row.onButton, after, i] {
                if (fn) fn(i);
                after();
            });
            if (!first) first = button;
            hostBox->addWidget(button);
        }
        box->addWidget(host);
        if (wordless) box->addStretch(1);
        constexpr int kWordsWant = 300;   // narrower than this beside the buttons, and they go below
        // The block goes back to the slot it came from, not to the end of the row: a first, narrow
        // resize moved it under the words and the wide one that followed appended it after the
        // trailing stretch, which is how a lone "+ add a model…" ended up at the right edge.
        const int slot = box->indexOf(host);
        if (!wordless) line->onResized = [box, text, host, slot](int width) {
            const bool below = width - host->sizeHint().width() - 90 < kWordsWant;
            if (below == host->property("below").toBool()) return;
            host->setProperty("below", below);
            if (below) { box->removeWidget(host); text->addSpacing(4); text->addWidget(host, 0, Qt::AlignLeft); }
            else { text->removeWidget(host); box->insertWidget(slot, host); }
        };
        if (first) entry.activate = [first] { first->click(); };
        break;
    }
    case SettingRow::Info:
    case SettingRow::Heading:
    case SettingRow::Subheading:
        break;
    }
    m_rows.append(entry);
    return line;
}

QWidget *SettingsPane::actionRow(const ActionItem &item, const QString &prefix) {
    auto *line = new ClickRow;
    line->setObjectName(QStringLiteral("settingsRow"));
    line->setAttribute(Qt::WA_StyledBackground);
    line->setProperty("rowId", item.key);
    line->setCursor(Qt::PointingHandCursor);
    auto *box = new QHBoxLayout(line);
    box->setContentsMargins(10, 5, 10, 5);
    box->setSpacing(12);
    auto *text = new QVBoxLayout;
    text->setSpacing(1);
    auto *label = new QLabel((item.checked ? QStringLiteral("✓  ") : QString()) + prefix + item.label);
    label->setObjectName(QStringLiteral("settingsRowLabel"));
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    text->addWidget(label);
    if (!item.detail.isEmpty()) text->addWidget(mutedLabel(item.detail, "settingsRowDetail"));
    const QString commands = actionSlashCommands(item.key);
    if (!commands.isEmpty()) text->addWidget(mutedLabel(commands, "settingsActionCommands"));
    box->addLayout(text, 1);
    if (!item.shortcut.isEmpty()) box->addWidget(keyCap(item.shortcut));
    Row entry;
    entry.id = item.key;
    entry.widget = line;
    entry.activate = [this, item] { runAction(item); };
    line->onClick = entry.activate;
    m_rows.append(entry);
    return line;
}

// An option, found from the Actions pane: not the control itself (options are changed in Options)
// but a row that takes you to it.
QWidget *SettingsPane::optionJumpRow(const SettingsSection &section, const SettingRow &row) {
    ActionItem item;
    item.key = QStringLiteral("option-jump:") + section.id + QLatin1Char('/') + row.id;
    item.label = QStringLiteral("Options › %1 › %2").arg(section.title, row.label);
    item.detail = row.detail;
    QWidget *line = actionRow(item);
    Row &entry = m_rows.last();
    entry.activate = [this, sectionId = section.id, rowId = row.id] {
        // Queued: the row that was clicked is deleted by the rebuild this starts.
        QMetaObject::invokeMethod(this, [this, sectionId, rowId] { revealOption(sectionId, rowId); focusSearch(); },
                                  Qt::QueuedConnection);
    };
    static_cast<ClickRow *>(line)->onClick = entry.activate;
    return line;
}

// Navigation results stay inside this pane; they must not run through the owner's
// action callback, which closes Actions before executing a command.
QWidget *SettingsPane::sectionJumpRow(const ActionItem &item, const std::function<void()> &jump) {
    QWidget *line = actionRow(item);
    auto activate = [this, jump] {
        QMetaObject::invokeMethod(this, jump, Qt::QueuedConnection);
    };
    m_rows.last().activate = activate;
    static_cast<ClickRow *>(line)->onClick = activate;
    return line;
}

void SettingsPane::revealSection(Mode mode, const QString &section, const QString &group, int heading) {
    setMode(mode);
    { const QSignalBlocker blocker(m_search); m_search->clear(); }
    if (mode == Mode::Options && heading >= 0) {
        for (const auto &page : std::as_const(m_sectionCache)) {
            if (page.id != section) continue;
            // A subheading can sit inside a folded parent heading.
            for (int i = std::min(heading, int(page.rows.size()) - 1); i >= 0; --i) {
                const auto &row = page.rows.at(i);
                if (row.kind != SettingRow::Heading) continue;
                if (row.collapsible) QSettings().setValue(QStringLiteral("options/collapsed/") + row.id, false);
                break;
            }
        }
    }
    build();
    if (mode == Mode::Options) showTab(section);
    if (!group.isEmpty()) scrollToHeader(group);
    else if (auto *area = currentScroll()) area->verticalScrollBar()->setValue(0);
    focusSearch();
}

// ----- search -------------------------------------------------------------------------------------

int SettingsPane::fuzzyScore(const QString &needle, const QString &haystack) {
    if (needle.isEmpty()) return 1;
    const QString n = needle.toLower(), h = haystack.toLower();
    const int direct = h.indexOf(n);
    if (direct >= 0) return 10000 - direct - (direct > 0 && h.at(direct - 1).isLetterOrNumber() ? 500 : 0);
    int pos = 0, first = -1, last = -1;
    for (const QChar c : n) {
        if (c.isSpace()) continue;
        pos = h.indexOf(c, pos);
        if (pos < 0) return 0;
        if (first < 0) first = pos;
        last = pos++;
    }
    // Letters scattered across a whole sentence are not a match: "reset" is not in "Reasoning
    // effort", nor "theme" in "Cards, threads, plans and project memory". An abbreviation
    // ("thm", "nwpn") still fits, because its letters sit close together.
    if (last - first >= 2 * n.size() + 2) return 0;
    return std::max(1, 5000 - (last - first) * 10 - first);
}

// One flat list, best match first, each row saying where it lives ("General · …"), because the
// search includes navigable section headings as well as individual rows. Both catalogs are searched so the search
// never dead-ends, but the pane's own kind comes first: the other kind's scores are halved, and
// in Actions an option is a row that jumps to it rather than the control.
void SettingsPane::buildResults(const QString &needle) {
    struct Hit { int score; int order; SettingRow row; ActionItem action; QString where; bool isAction; SettingsSection section; std::function<void()> jump = {}; };
    const bool actionsMode = m_mode == Mode::Actions;
    // Each word is matched on its own, against whichever field it fits best, and every word has
    // to land somewhere: "appearance theme" and "theme appearance" both find Appearance › Theme.
    const QStringList words = needle.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    auto scoreOf = [&words](const QString &label, const QString &detail, const QString &where, const QString &aliases) {
        int total = 0;
        for (const QString &word : words) {
            const int best = std::max({fuzzyScore(word, label), fuzzyScore(word, detail) / 3, fuzzyScore(word, where) / 4,
                                       word.size() >= 2 ? fuzzyScore(word, aliases) / 2 : 0});
            if (best <= 0) return 0;
            total += best;
        }
        return words.isEmpty() ? 0 : total / int(words.size());
    };
    QList<Hit> hits;
    int order = 0;
    auto addSection = [&](const QString &title, Mode mode, const QString &section, const QString &group, int heading = -1) {
        const int score = scoreOf(title, QString(), QString(), QString());
        if (score <= 0) return;
        ActionItem item;
        item.key = QStringLiteral("section-jump:") + (mode == Mode::Options ? QStringLiteral("options:") : QStringLiteral("actions:"))
                   + section + QLatin1Char(':') + group;
        item.label = title;
        item.detail = QStringLiteral("Go to section");
        const bool ownMode = mode == m_mode;
        hits.append({ownMode ? score : score / 2, order++, {}, item,
                     mode == Mode::Options ? QStringLiteral("Options") : QStringLiteral("Actions"), true, {},
                     [this, mode, section, group, heading] { revealSection(mode, section, group, heading); }});
    };
    for (const SettingsSection &section : std::as_const(m_sectionCache)) {
        addSection(section.title, Mode::Options, section.id, QString());
        for (int i = 0; i < section.rows.size(); ++i) {
            const auto &row = section.rows.at(i);
            if (row.kind == SettingRow::Heading || row.kind == SettingRow::Subheading)
                addSection(row.label, Mode::Options, section.id,
                           QStringLiteral("option-section:") + section.id + QLatin1Char(':') + QString::number(i), i);
        }
        for (const SettingRow &row : section.rows) {
            if (row.kind == SettingRow::Info || row.kind == SettingRow::Heading || row.kind == SettingRow::Subheading) continue;
            const int score = scoreOf(row.label, row.detail, section.title, row.aliases);
            if (score > 0) hits.append({actionsMode ? score / 2 : score, order++, row, {}, section.title, false,
                                        actionsMode ? section : SettingsSection()});
        }
    }
    const QStringList recent = QSettings().value(QStringLiteral("palette/recent")).toStringList();
    if (std::any_of(m_actionCache.cbegin(), m_actionCache.cend(), [&](const ActionItem &item) {
            return !item.children && recent.contains(item.key);
        })) addSection(QStringLiteral("Recent"), Mode::Actions, QString(), QStringLiteral("action-section:Recent"));
    QList<ActionItem> flat;
    QSet<QString> sections;
    for (const ActionItem &item : std::as_const(m_actionCache)) {
        if (!sections.contains(item.section)) {
            sections.insert(item.section);
            addSection(item.section, Mode::Actions, QString(), QStringLiteral("action-section:") + item.section);
        }
        if (item.children) addSection(item.label, Mode::Actions, QString(), item.key);
        if (!item.children) { flat << item; continue; }
        for (ActionItem child : item.children()) {
            child.label = item.label + QStringLiteral(" › ") + child.label;
            child.aliases += QLatin1Char(' ') + item.aliases;
            flat << child;
        }
    }
    for (const ActionItem &item : std::as_const(flat)) {
        const int score = scoreOf(item.label, item.detail, item.section, item.aliases + QLatin1Char(' ') + actionSlashCommands(item.key));
        if (score > 0) hits.append({actionsMode ? score : score / 2, order++, {}, item, item.section, true, {}});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) { return a.score > b.score; });
    // Rows made from the search text itself come first: the text was typed to mean exactly them.
    QList<Hit> typedHits;
    for (const ActionItem &item : std::as_const(m_actionCache)) {
        if (!item.typed) continue;
        for (ActionItem row : item.typed(needle)) {
            row.label = item.label + QStringLiteral(" › ") + row.label;
            typedHits.append({0, order++, {}, row, item.section, true, {}});
        }
    }
    hits = typedHits + hits;
    if (hits.size() > kMaxResults) hits = hits.mid(0, kMaxResults);

    QScrollArea *scroll = m_results;
    if (QWidget *old = scroll->takeWidget()) old->deleteLater();
    auto *body = new QWidget;
    body->setObjectName(QStringLiteral("settingsPageBody"));
    body->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(2, 6, 8, 12);
    layout->setSpacing(2);
    m_rows.clear();
    if (hits.isEmpty()) {
        layout->addWidget(mutedLabel(QStringLiteral("Nothing matches “%1”.").arg(needle), "settingsBlurb"));
    }
    for (const Hit &hit : std::as_const(hits)) {
        if (hit.isAction) {
            ActionItem item = hit.action;
            item.detail = hit.where + (item.detail.isEmpty() ? QString() : QStringLiteral("  ·  ") + item.detail);
            layout->addWidget(hit.jump ? sectionJumpRow(item, hit.jump) : actionRow(item));
        } else if (actionsMode) {
            layout->addWidget(optionJumpRow(hit.section, hit.row));
        } else {
            SettingRow row = hit.row;
            row.detail = hit.where + (row.detail.isEmpty() ? QString() : QStringLiteral("  ·  ") + row.detail);
            layout->addWidget(settingRow(row));
        }
    }
    layout->addStretch(1);
    scroll->setWidget(body);
    m_pages->setCurrentWidget(m_results);
    setCurrent(m_rows.isEmpty() ? -1 : 0, true);
}

// ----- navigation ---------------------------------------------------------------------------------

QScrollArea *SettingsPane::currentScroll() const {
    return qobject_cast<QScrollArea *>(m_pages->currentWidget());
}

void SettingsPane::setCurrent(int index, bool scroll) {
    if (m_current >= 0 && m_current < m_rows.size() && m_rows[m_current].widget) {
        m_rows[m_current].widget->setProperty("current", false);
        repolish(m_rows[m_current].widget);
    }
    m_current = (index >= 0 && index < m_rows.size()) ? index : -1;
    if (m_current < 0) return;
    QWidget *widget = m_rows[m_current].widget;
    widget->setProperty("current", true);
    repolish(widget);
    if (scroll) {
        if (QScrollArea *area = currentScroll()) area->ensureWidgetVisible(widget, 0, 24);
    }
}

void SettingsPane::moveCurrent(int steps) {
    if (m_rows.isEmpty()) return;
    int index = m_current;
    if (index < 0) index = steps > 0 ? 0 : int(m_rows.size()) - 1;
    else index = std::clamp(index + steps, 0, int(m_rows.size()) - 1);
    setCurrent(index, true);
}

void SettingsPane::activateCurrent() {
    if (m_current < 0 || m_current >= m_rows.size()) return;
    if (m_rows[m_current].activate) m_rows[m_current].activate();
}

void SettingsPane::switchTab(int delta) {
    if (m_tabs->count() < 2) return;
    int index = (m_tabs->currentIndex() + delta) % m_tabs->count();
    if (index < 0) index += m_tabs->count();
    m_tabs->setCurrentIndex(index);
}

void SettingsPane::runAction(const ActionItem &item) {
    if (onRun) { onRun(item); return; }
    if (item.run) item.run();
    if (item.stayOpen) QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
}

// The pane chrome's buttons float over the top right of the pane; the search row gives up that room.
void SettingsPane::setHeaderRightInset(int pixels) {
    if (m_header && m_header->contentsMargins().right() != pixels) m_header->setContentsMargins(0, 0, pixels, 0);
}

void SettingsPane::closeRequested() {
    if (onClose) onClose();
}

bool SettingsPane::eventFilter(QObject *object, QEvent *event) {
    if (object != m_search) return QWidget::eventFilter(object, event);
    if (event->type() == QEvent::ShortcutOverride) {
        // Keep the navigation keys away from window shortcuts while the search has focus.
        const int k = static_cast<QKeyEvent *>(event)->key();
        if (k == Qt::Key_Escape || k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Up || k == Qt::Key_Down
            || k == Qt::Key_PageUp || k == Qt::Key_PageDown || k == Qt::Key_Left || k == Qt::Key_Right) {
            event->accept();
            return true;
        }
        return false;
    }
    if (event->type() != QEvent::KeyPress) return false;
    auto *key = static_cast<QKeyEvent *>(event);
    const bool ctrl = key->modifiers() & Qt::ControlModifier;
    switch (key->key()) {
    case Qt::Key_Escape:
        if (!m_search->text().isEmpty()) m_search->clear();
        else closeRequested();
        return true;
    case Qt::Key_Return: case Qt::Key_Enter:
        if (m_current < 0 && !m_rows.isEmpty()) setCurrent(0, true);
        activateCurrent();
        return true;
    case Qt::Key_Down: moveCurrent(1); return true;
    case Qt::Key_Up: moveCurrent(-1); return true;
    case Qt::Key_PageDown: moveCurrent(8); return true;
    case Qt::Key_PageUp: moveCurrent(-8); return true;
    case Qt::Key_Left:
        if (!m_search->text().isEmpty()) return false;
        switchTab(-1); return true;
    case Qt::Key_Right:
        if (!m_search->text().isEmpty()) return false;
        switchTab(1); return true;
    default:
        if (ctrl && key->key() == Qt::Key_N) { moveCurrent(1); return true; }
        if (ctrl && key->key() == Qt::Key_P) { moveCurrent(-1); return true; }
        return false;
    }
}

// Esc anywhere else in the pane: back to the search first, then (from the search) close.
void SettingsPane::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        if (focusWidget() != m_search) { focusSearch(); event->accept(); return; }
        if (!m_search->text().isEmpty()) m_search->clear(); else closeRequested();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void SettingsPane::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    // An ask row that does nothing is worse than no ask row (the Sessions pane's rule, and the ⓘ
    // view's `onAskOwner`), so the row is there only once the window has given this pane a way
    // to build a console. By the time the pane is on screen the wiring has happened or never
    // will — and a library with no window (the tests) simply has no helper.
    if (m_helper) m_helper->setVisible(bool(onCreateConsole));
}

void SettingsPane::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    updateConsoleHeight();
}

// ----- the helper agent (#FEJQ; a console since card #AGNT step 7) -----------------------------
//
// The pane holds no worker and builds no console: it owns the *context* — what the agent here is
// about — and the window, which is the only place a `Pane` can be made, turns that into a console
// through `onCreateConsole`. The same seam is the Sessions pane's, name for name
// (src/Conversations.cpp), so the window wires both panes with the same lines.

SettingsPane::~SettingsPane() {
    // The console holds a pointer to the context and clears its own callback in ~Pane, so the
    // widget goes first (Qt destroys the children above) and the context last.
    delete m_context;
}

relay::agent::Context *SettingsPane::agentContext() const { return m_context; }

void SettingsPane::setHelperTabId(const QString &tabId) {
    if (m_context) m_context->setTabId(tabId);
}

void SettingsPane::setHelperWorkspace(const QString &workspace) {
    if (m_context) m_context->setWorkspace(workspace);
}

// The row wears its own shortcut — the key is in the button's own text, "Helper Agent (Alt+Q)" —
// and a *click* on it is still the slow path WARP.md's standing rule is about, so it teaches the
// key once through `onHelperHint`. The pane has no window to show a toast in, which is why the
// hint is the window's to draw; the id it earned is the one the window wired.
void SettingsPane::setHelperShortcut(const QString &hintId, const QString &keys) {
    m_askHintId = hintId;
    m_askKeys = keys;
    updateHelperRow();
}

// Asking for the cursor is asking for the console: the pane's ask key, a click on the row and a
// drafted request all mean the box has to be there to type in (#FEJQ). The console is built here,
// on the first expand, so a tab nobody asks anything never pays for one (§33, owner decision 5).
void SettingsPane::focusHelper() {
    if (!onCreateConsole) return;
    ensureConsole();
    if (m_helperCollapsed) {
        m_helperCollapsed = false;
        applyHelperCollapsed();
    }
    if (m_console.focusComposer) m_console.focusComposer();
}

// A draft, never a send (owner, 2026-09-19: "draft you confirm").
void SettingsPane::helperDraft(const QString &text) {
    focusHelper();
    if (m_console.draftInComposer) m_console.draftInComposer(text);
}

// The row the pane is until the helper is asked for, and the fold back to it. Both are the
// **host's**, not the console's: the console is a `Pane`, and a pane has no business knowing it
// is embedded at the foot of something (card #AGNT step 7).
void SettingsPane::buildHelperRow(QVBoxLayout *into) {
    m_helper = new QWidget(this);
    m_helper->setObjectName(QStringLiteral("boardChatPanel"));
    m_helper->setAttribute(Qt::WA_StyledBackground);
    auto *outer = new QVBoxLayout(m_helper);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ---- collapsed: the one row the helper is until it is asked for -------------------------
    // It is a button and not a label so the keyboard can reach it, and reaching it is enough: a
    // Tab that lands here meant to ask something, so the focus goes on into the composer.
    m_askRow = new QWidget(m_helper);
    m_askRow->setObjectName(QStringLiteral("boardChatAskRow"));
    auto *askLine = new QHBoxLayout(m_askRow);
    askLine->setContentsMargins(10, 6, 10, 6);
    askLine->setSpacing(6);
    // At the **bottom right** of the pane, not the left (owner, 2026-09-20: "it should be at the
    // bottom right rather than bottom left"), so the stretch goes in front of the button.
    askLine->addStretch(1);
    m_ask = new QToolButton(m_askRow);
    m_ask->setObjectName(QStringLiteral("boardChatAsk"));
    // One button that says what it opens and how (owner, 2026-09-20: "make the button say Helper
    // Agent (Alt+Q)"). The key rides in the button's own text rather than on a muted label beside
    // it: two widgets for one offer read as a label with a stray key after it, and the key is part
    // of what the button is. It is also the shortcut hint — the slow path teaches itself here,
    // with no notice to show (WARP.md's standing rule).
    m_ask->setIcon(askIcon(theme::TextMuted));
    m_ask->setIconSize(QSize(14, 14));
    m_ask->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_ask->setCursor(Qt::PointingHandCursor);
    m_ask->setFocusPolicy(Qt::StrongFocus);
    m_ask->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    askLine->addWidget(m_ask, 0);
    outer->addWidget(m_askRow);
    connect(m_ask, &QToolButton::clicked, this, [this] {
        // The mouse, not the key: Alt+Q comes in through `focusHelper()` and teaches nothing.
        if (onHelperHint && !m_askHintId.isEmpty()) onHelperHint();
        focusHelper();
    });

    // ---- expanded: the console, under a row that folds it away ------------------------------
    m_helperBody = new QWidget(m_helper);
    m_helperBody->setObjectName(QStringLiteral("boardChatBody"));
    auto *body = new QVBoxLayout(m_helperBody);
    body->setContentsMargins(10, 8, 10, 8);
    body->setSpacing(6);
    auto *headRow = new QHBoxLayout;
    headRow->setSpacing(6);
    // Which helper this is. The console's placeholder says it too, but the head is the row you
    // fold from and the pane it is in has nothing else that names the agent (#FEJQ decision 4).
    m_helperHead = new QLabel(m_helperBody);
    m_helperHead->setObjectName(QStringLiteral("boardChatHead"));
    headRow->addWidget(m_helperHead, 0);
    headRow->addStretch(1);
    // The way back to one row. At the end of the head, where a pane's own chrome buttons are, and
    // out of the tab order: the composer is what Shift+Tab should reach from here, not the control
    // that would throw the answer off screen.
    auto *fold = new QToolButton(m_helperBody);
    fold->setObjectName(QStringLiteral("boardChatFold"));
    fold->setText(QStringLiteral("⌄"));
    fold->setToolTip(QStringLiteral("Fold the helper back to one row. The conversation is kept — "
                                    "it opens where you left it."));
    fold->setCursor(Qt::PointingHandCursor);
    fold->setFocusPolicy(Qt::NoFocus);
    fold->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    headRow->addWidget(fold, 0);
    connect(fold, &QToolButton::clicked, this, [this] {
        if (m_helperCollapsed) return;
        m_helperCollapsed = true;
        applyHelperCollapsed();
    });
    body->addLayout(headRow);
    m_helperBody->setVisible(false);
    outer->addWidget(m_helperBody);

    into->addWidget(m_helper, 0);
    updateHelperRow();
}

// The window is the only place a `Pane` can be made, so this is the one call that produces one —
// and only when somebody asks, which is what keeps an untouched tab free (§33).
void SettingsPane::ensureConsole() {
    if (m_console || !onCreateConsole || m_helperBody == nullptr) return;
    m_console = onCreateConsole(m_context, m_helperBody);
    if (!m_console) return;
    if (auto *body = qobject_cast<QVBoxLayout *>(m_helperBody->layout())) body->addWidget(m_console.widget, 1);
    m_console.widget->show();
    updateConsoleHeight();
}

// One row, or the row and the console. Nothing is destroyed either way: a helper folded back keeps
// its conversation, its draft and its queue, so opening it again is where you left it.
void SettingsPane::applyHelperCollapsed() {
    if (m_askRow) m_askRow->setVisible(m_helperCollapsed);
    // Folding with the cursor still in the composer: hand the focus to the row that replaces it
    // first. Qt hands it on itself when the widget holding it hides, and it does that as a Tab —
    // and a Tab onto the ask row means "open the helper", which would unfold what was just folded.
    if (m_helperCollapsed && m_ask && m_helperBody
        && m_helperBody->isAncestorOf(QApplication::focusWidget()))
        m_ask->setFocus(Qt::OtherFocusReason);
    if (m_helperBody) m_helperBody->setVisible(!m_helperCollapsed);
    if (m_console.setCollapsed) m_console.setCollapsed(m_helperCollapsed);
    if (!m_helperCollapsed) updateConsoleHeight();
}

// "Helper Agent (Alt+Q)" — the name of what the row opens, with the live key in parentheses. One
// widget: the key rides in the button's own text, so an unbound key simply leaves the name alone
// rather than leaving an empty label behind.
void SettingsPane::updateHelperRow() {
    if (m_ask) {
        m_ask->setText(m_askKeys.isEmpty() ? QStringLiteral("Helper Agent")
                                           : QStringLiteral("Helper Agent (%1)").arg(m_askKeys));
        m_ask->setToolTip(m_askKeys.isEmpty()
            ? QStringLiteral("Ask the helper agent about this pane.")
            : QStringLiteral("Ask the helper agent about this pane (%1).").arg(m_askKeys));
    }
    if (m_helperHead && m_context) m_helperHead->setText(m_context->title());
}

// The console is sized to the pane it is in: at most ~40 % of its height, and never so little that
// the transcript is a slot rather than a conversation. It is the rule the helper panel's log
// already followed, applied a level up now that the console brings its own box with it.
void SettingsPane::updateConsoleHeight() {
    if (m_helperBody == nullptr) return;
    const int line = QFontMetrics(font()).lineSpacing();
    const int cap = std::max(10 * line, height() * 2 / 5);
    m_helperBody->setMaximumHeight(cap);
    // And a floor, because a maximum alone is not a size: the strip is added with no stretch, so
    // it takes the console's own size hint — a `Pane`'s, which is a terminal's and small. The
    // first live run of card #AGNT step 5 drew a whole answer into the three rows that left, and
    // the shot showed the pane header wearing the answer's auto-generated title over an empty
    // vterm. Twenty lines is a conversation — the head, the box and its chips take half of it —
    // and never more than the cap, so a short pane is unchanged.
    m_helperBody->setMinimumHeight(std::min(cap, 20 * line));
}

// ----- public surface ------------------------------------------------------------------------------

void SettingsPane::showTab(const QString &id) {
    const int index = m_tabIds.indexOf(id);
    if (index < 0) return;
    m_wantedTab = id;
    m_tabs->setCurrentIndex(index);
}

QString SettingsPane::currentTab() const { return m_tabIds.value(m_tabs->currentIndex()); }
QStringList SettingsPane::tabIds() const { return m_tabIds; }
void SettingsPane::setSearch(const QString &text) { m_search->setText(text); }
QString SettingsPane::search() const { return m_search->text(); }

void SettingsPane::focusSearch() {
    m_search->setFocus(Qt::ShortcutFocusReason);
    m_search->selectAll();
}

void SettingsPane::scrollToGroup(const QString &key) {
    setMode(Mode::Actions);
    setSearch(QString());
    scrollToHeader(key);
}

void SettingsPane::scrollToHeader(const QString &key) {
    QWidget *header = m_groups.value(key);
    if (!header) return;
    QScrollArea *area = currentScroll();
    if (!area) return;
    // The page has just been shown; its layout settles on the next pass.
    QPointer<QWidget> guard(header);
    QTimer::singleShot(0, this, [this, guard, area] {
        if (!guard) return;
        area->ensureWidgetVisible(guard, 0, 0);
        area->verticalScrollBar()->setValue(guard->pos().y() - 6);
        for (int i = 0; i < m_rows.size(); ++i)
            if (m_rows[i].widget && m_rows[i].widget->pos().y() > guard->pos().y()) { setCurrent(i, false); break; }
    });
}

QStringList SettingsPane::visibleRowIds() const {
    QStringList ids;
    for (const Row &row : m_rows) ids << row.id;
    return ids;
}

void SettingsPane::rebuild() {
    // What to put back afterwards: the tab, the scroll offset, the highlighted row and the focus.
    QScrollArea *area = currentScroll();
    const int offset = area ? area->verticalScrollBar()->value() : 0;
    const QString currentId = (m_current >= 0 && m_current < m_rows.size()) ? m_rows[m_current].id : QString();
    QString focusedId;
    if (QWidget *focus = focusWidget(); focus && focus != m_search && isAncestorOf(focus)) {
        for (QWidget *w = focus; w && w != this; w = w->parentWidget())
            if (w->objectName() == QLatin1String("settingsRow")) { focusedId = w->property("rowId").toString(); break; }
    }
    build();
    for (int i = 0; i < m_rows.size(); ++i) {
        if (!currentId.isEmpty() && m_rows[i].id == currentId) setCurrent(i, false);
        if (!focusedId.isEmpty() && m_rows[i].id == focusedId) {
            QWidget *row = m_rows[i].widget;
            const auto controls = row->findChildren<QWidget *>();
            for (QWidget *control : controls)
                if (control->focusPolicy() != Qt::NoFocus) { control->setFocus(Qt::OtherFocusReason); break; }
        }
    }
    if (QScrollArea *fresh = currentScroll(); fresh && offset > 0) {
        // The new page has not been laid out yet, so its scroll range is still 0: put the offset
        // back as soon as the range exists.
        QScrollBar *bar = fresh->verticalScrollBar();
        if (bar->maximum() >= offset) { bar->setValue(offset); return; }
        auto connection = std::make_shared<QMetaObject::Connection>();
        *connection = connect(bar, &QScrollBar::rangeChanged, this, [bar, offset, connection](int, int maximum) {
            if (maximum <= 0) return;
            bar->setValue(std::min(offset, maximum));
            QObject::disconnect(*connection);
        });
    }
}

}  // namespace relay
