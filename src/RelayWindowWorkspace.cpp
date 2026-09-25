// SPDX-License-Identifier: AGPL-3.0-or-later
// The artifact workspace's window side (card #E85D): which leaf of a tab is the group's editor,
// console and preview, the two layout presets, following the output files, the status strip on
// the preview pane, and sending source file:line to the linked editor. The model and its rules are
// src/ArtifactWorkspace (QtCore, tested alone); docs/ARCHITECTURE.md section 10b is the overview.
#include "RelayWindow.h"
#include "ArtifactWorkspace.h"
#include "Logging.h"

#include <QDesktopServices>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>

namespace ws = relay::workspace;

namespace {

// A leaf's id in its tab's workspace group. It is saved in the leaf's layout node as "member", so
// a restart, a closed tab brought back and Ctrl+Shift+Z all give the pane its role back.
constexpr const char *kMemberProperty = "relayWorkspaceMember";
constexpr const char *kHolderProperty = "relayWorkspace";
constexpr const char *kStripProperty = "relayWorkspaceStrip";

// The tab's group and what watches its files. A child of the tab page, so it goes where the page
// goes (a tab moved to another window) and dies with it.
class WorkspaceHolder final : public QObject {
public:
    explicit WorkspaceHolder(QWidget *page) : QObject(page) {
        debounce.setSingleShot(true);
        debounce.setInterval(250);
        QObject::connect(&watcher, &QFileSystemWatcher::fileChanged, &debounce, [this] { debounce.start(); });
        // The directories too: an editor or a build that writes a temporary file and renames it
        // over the old one ends the file watch, and an output that does not exist yet has no file
        // to watch at all.
        QObject::connect(&watcher, &QFileSystemWatcher::directoryChanged, &debounce, [this] { debounce.start(); });
    }

    void rewatch() {
        QSet<QString> want;
        const auto add = [&want](const QString &path) {
            const QFileInfo info(path);
            if (info.isFile()) want.insert(info.absoluteFilePath());
            if (info.absoluteDir().exists()) want.insert(info.absolutePath());
        };
        for (const QString &source : group.sources) add(source);
        for (const ws::Output &out : group.outputs) add(out.path);
        const QStringList watched = watcher.files() + watcher.directories();
        QStringList drop, take;
        for (const QString &path : watched) if (!want.contains(path)) drop << path;
        for (const QString &path : want) if (!watched.contains(path)) take << path;
        if (!drop.isEmpty()) watcher.removePaths(drop);
        if (!take.isEmpty()) watcher.addPaths(take);
    }

    ws::Group group;
    QFileSystemWatcher watcher;
    QTimer debounce;
};

WorkspaceHolder *holderOf(QWidget *page) {
    return page ? dynamic_cast<WorkspaceHolder *>(page->property(kHolderProperty).value<QObject *>()) : nullptr;
}

QString memberOf(QWidget *leaf) { return leaf ? leaf->property(kMemberProperty).toString() : QString(); }

QString mintMember(QWidget *leaf) {
    QString id = memberOf(leaf);
    if (id.isEmpty() && leaf) {
        id = QStringLiteral("m-") + ws::Group::newId();
        leaf->setProperty(kMemberProperty, id);
    }
    return id;
}

bool isLocalPreview(QWidget *leaf) {
    auto *tool = dynamic_cast<ToolPane *>(leaf);
    return tool && tool->kind() == ToolPane::Kind::Preview && tool->preview() && !tool->preview()->isRemote();
}

// Whether `leaf` can hold `role`. A saved preview whose file was gone at restore comes back as a
// terminal pane (buildNode), and must not keep the preview's role.
bool fits(QWidget *leaf, ws::Role role) {
    switch (role) {
    case ws::Role::Console: return dynamic_cast<Pane *>(leaf) != nullptr;
    case ws::Role::Editor:
    case ws::Role::Preview: return isLocalPreview(leaf);
    case ws::Role::Variables: return dynamic_cast<ToolPane *>(leaf) != nullptr;
    }
    return false;
}

qint64 modifiedMs(const QString &path) {
    const QFileInfo info(path);
    return info.exists() ? info.lastModified().toMSecsSinceEpoch() : 0;
}

QString shortRevision(const QString &revision) { return revision.left(8); }

// One line over the preview: the output, which build of it this is, and whether it is current.
class StatusStrip final : public QWidget {
public:
    StatusStrip() {
        setObjectName(QStringLiteral("workspaceStatusStrip"));
        setAttribute(Qt::WA_StyledBackground);
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(8, 3, 6, 3);
        row->setSpacing(8);
        m_state = new QLabel(this);
        m_state->setObjectName(QStringLiteral("workspaceState"));
        m_text = new QLabel(this);
        m_text->setObjectName(QStringLiteral("workspaceStatusText"));
        m_text->setMinimumWidth(1);   // never hold the pane open (card #SDXE)
        m_text->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_external = new QToolButton(this);
        m_external->setObjectName(QStringLiteral("workspaceOpenExternally"));
        m_external->setText(QStringLiteral("Open externally"));
        m_external->setAutoRaise(true);
        QObject::connect(m_external, &QToolButton::clicked, this, [this] {
            if (!m_path.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(m_path));
        });
        row->addWidget(m_state);
        row->addWidget(m_text, 1);
        row->addWidget(m_external);
    }

    void display(const ws::Output &out, const ws::Adapter *adapter) {
        m_path = out.path;
        const relay::panestatus::Tokens t = relay::chrome::tokens();
        const QString name = QFileInfo(out.path).fileName();
        QString word = ws::stateName(out.state);
        QColor colour = t.muted;
        QStringList parts{name};
        const QString shown = out.generation > 0 ? QStringLiteral("generation %1").arg(out.generation) : QString();
        const QString revision = out.sourceRevision.isEmpty() ? QStringLiteral("revision unknown")
                                                              : QStringLiteral("rev %1").arg(shortRevision(out.sourceRevision));
        switch (out.state) {
        case ws::OutputState::Idle:
            parts << QStringLiteral("no output yet");
            break;
        case ws::OutputState::Live:
            colour = t.success;
            parts << shown << revision;
            break;
        case ws::OutputState::Building:
            colour = t.warning;
            parts << (out.building > 0 ? QStringLiteral("building generation %1").arg(out.building) : QStringLiteral("building"));
            if (out.generation > 0) parts << QStringLiteral("showing %1").arg(shown);
            break;
        case ws::OutputState::Stale:
            colour = t.warning;
            word = QStringLiteral("stale");
            parts << shown << revision << QStringLiteral("sources saved since — showing the last output");
            break;
        case ws::OutputState::Failed:
            colour = t.error;
            parts << (out.message.isEmpty() ? QStringLiteral("build failed") : QStringLiteral("build failed: ") + out.message);
            if (out.generation > 0) parts << QStringLiteral("showing %1").arg(shown);
            break;
        }
        if (out.authority == ws::Authority::Generated) parts << QStringLiteral("read only");
        const bool unavailable = adapter && !adapter->available;
        if (unavailable) parts << adapter->unavailableReason;
        parts.removeAll(QString());
        m_state->setText(QStringLiteral("● ") + word);
        m_state->setProperty("workspaceState", ws::stateName(out.state));
        m_state->setStyleSheet(QStringLiteral("QLabel { color: %1; font-weight: 600; }").arg(colour.name()));
        m_text->setText(parts.join(QStringLiteral(" · ")));
        m_text->setToolTip(out.path);
        // A stale or failed preview says so on its whole strip, not only in its word: the output
        // under it is older than the sources, and the reader must not take it for current.
        QColor ground = t.background;
        if (out.state == ws::OutputState::Stale || out.state == ws::OutputState::Failed) {
            ground = colour;
            ground.setAlpha(46);
        }
        setStyleSheet(QStringLiteral("#workspaceStatusStrip { background: %1; border-bottom: 1px solid %2; }"
                                     " #workspaceStatusText { color: %3; }")
                          .arg(ground.name(QColor::HexArgb), colour.name(), t.text.name()));
        m_external->setVisible(unavailable && QFileInfo(out.path).isFile());
    }

    QString text() const { return m_state->text() + QStringLiteral(" · ") + m_text->text(); }

private:
    QLabel *m_state = nullptr;
    QLabel *m_text = nullptr;
    QToolButton *m_external = nullptr;
    QString m_path;
};

StatusStrip *stripOf(QWidget *leaf) {
    return leaf ? dynamic_cast<StatusStrip *>(leaf->property(kStripProperty).value<QObject *>()) : nullptr;
}

}  // namespace

// ----- membership --------------------------------------------------------------------------------

QJsonObject RelayWindow::withWorkspaceMember(QWidget *leaf, QJsonObject node) {
    const QString id = memberOf(leaf);
    if (!node.isEmpty() && !id.isEmpty()) node.insert(QStringLiteral("member"), id);
    return node;
}

void RelayWindow::tagWorkspaceMember(QWidget *leaf, const QJsonObject &node) {
    const QString id = node.value(QStringLiteral("member")).toString();
    if (leaf && !id.isEmpty() && isLeaf(leaf)) leaf->setProperty(kMemberProperty, id);
}

// The group's members that are in this tab now, as the leaves that can hold their roles.
static QSet<QString> liveMembers(const QList<QWidget *> &leaves, const ws::Group &group) {
    QSet<QString> live;
    for (QWidget *leaf : leaves) {
        const QString id = memberOf(leaf);
        if (id.isEmpty()) continue;
        std::optional<ws::Role> role = group.roleOf(id);
        if (!role && group.departed.contains(id)) role = group.departed.value(id);
        if (role && fits(leaf, *role)) live.insert(id);
    }
    return live;
}

static QWidget *leafFor(const QList<QWidget *> &leaves, const ws::Group &group, ws::Role role) {
    const QString id = group.memberFor(role);
    if (id.isEmpty()) return nullptr;
    for (QWidget *leaf : leaves)
        if (memberOf(leaf) == id && fits(leaf, role)) return leaf;
    return nullptr;
}

QJsonObject RelayWindow::workspaceGroupJson(QWidget *page) const {
    WorkspaceHolder *holder = holderOf(page);
    if (!holder) return {};
    // Saved without a member that is not in the tab: nothing on disk names a pane that is gone.
    holder->group.reconcile(liveMembers(leavesIn(page), holder->group));
    return holder->group.toJson();
}

// Where Alt+click material goes (card #7BYT): the pane it came from when its prompt box can take
// it, then the pane this one is linked to — the workspace group's Console member — then the most
// recently opened pane that qualifies. The owner's rule, 2026-09-25: "it works in the current
// pane or linked pane where there is a prompt box. after that, the most recently opened pane."
// Null means nothing in this window can take it, and the pane says so in its status line.
Pane *RelayWindow::promptTargetPane(Pane *from) {
    if (from && from->acceptsPromptContext())
        return from;
    if (from && pageOf(from)) {
        if (WorkspaceHolder *holder = holderOf(pageOf(from))) {
            if (QWidget *leaf = leafFor(leavesIn(pageOf(from)), holder->group, ws::Role::Console)) {
                if (auto *console = dynamic_cast<Pane *>(leaf);
                        console && console != from && console->acceptsPromptContext())
                    return console;
            }
        }
    }
    for (const QPointer<Pane> &entry : m_paneOpenOrder) {
        Pane *pane = entry.data();
        if (pane && pane != from && pane->acceptsPromptContext())
            return pane;
    }
    return nullptr;
}

void RelayWindow::restoreWorkspaceGroup(QWidget *page, const QJsonObject &json) {
    if (!page || json.isEmpty()) return;
    QString error;
    ws::Group group = ws::Group::fromJson(json, &error);
    if (!group.isValid()) {
        relay::log::info(QStringLiteral("workspace: tab restored without its workspace group: %1").arg(error));
        return;
    }
    ensureWorkspace(page)->setProperty("restored", true);
    holderOf(page)->group = group;
    // buildNode() has tagged the leaves; the strips are drawn once the page is in the window.
    QPointer<QWidget> guard(page);
    QTimer::singleShot(0, page, [guard] { if (auto *w = windowOf(guard)) w->refreshWorkspace(guard); });
}

QObject *RelayWindow::ensureWorkspace(QWidget *page) {
    if (WorkspaceHolder *holder = holderOf(page)) return holder;
    auto *holder = new WorkspaceHolder(page);
    page->setProperty(kHolderProperty, QVariant::fromValue<QObject *>(holder));
    QPointer<QWidget> guard(page);
    QObject::connect(&holder->debounce, &QTimer::timeout, page, [guard] {
        if (auto *w = windowOf(guard)) w->refreshWorkspace(guard);
    });
    return holder;
}

bool RelayWindow::isWorkspaceViewer(QWidget *leaf) const {
    WorkspaceHolder *holder = holderOf(pageOf(leaf));
    if (!holder) return false;
    const std::optional<ws::Role> role = holder->group.roleOf(memberOf(leaf));
    return role && (*role == ws::Role::Editor || *role == ws::Role::Preview);
}

// ----- following the files ---------------------------------------------------------------------

void RelayWindow::refreshWorkspace(QWidget *page) {
    WorkspaceHolder *holder = holderOf(page);
    if (!holder || pageOf(page) != page) return;
    ws::Group &group = holder->group;
    bool changed = group.reconcile(liveMembers(leavesIn(page), group));
    const QString revision = ws::sourceRevisionOnDisk(group.sources, group.root);
    qint64 newest = 0;
    for (const QString &source : group.sources) newest = std::max(newest, modifiedMs(source));
    QWidget *previewLeaf = leafFor(leavesIn(page), group, ws::Role::Preview);
    for (ws::Output &out : group.outputs) {
        const qint64 before = out.generation;
        changed |= ws::observe(out, {ws::fileSignature(out.path), modifiedMs(out.path), newest, revision});
        // A new generation replaces what the preview shows; a stale one leaves the last output up.
        if (out.generation != before && out.state == ws::OutputState::Live)
            if (auto *tool = dynamic_cast<ToolPane *>(previewLeaf);
                tool && tool->preview()->path() == out.path && !tool->preview()->isDirty())
                tool->preview()->open(out.path);
    }
    holder->rewatch();
    // The strip: on the group's preview pane, and on no other.
    for (QWidget *leaf : leavesIn(page)) {
        StatusStrip *strip = stripOf(leaf);
        auto *tool = dynamic_cast<ToolPane *>(leaf);
        const ws::Output *out = tool && leaf == previewLeaf ? group.output(tool->preview()->path()) : nullptr;
        if (!out) {
            if (strip) { leaf->setProperty(kStripProperty, QVariant()); strip->deleteLater(); }
            continue;
        }
        if (!strip) {
            auto *layout = qobject_cast<QVBoxLayout *>(tool->layout());
            if (!layout) continue;
            strip = new StatusStrip;
            layout->insertWidget(0, strip);
            leaf->setProperty(kStripProperty, QVariant::fromValue<QObject *>(strip));
        }
        strip->display(*out, ws::AdapterRegistry::instance().byId(out->adapter, group.kind));
    }
    if (changed) m_manager->scheduleSave();
}

// ----- the presets -----------------------------------------------------------------------------

void RelayWindow::applyWorkspacePreset(const QString &layout, const QString &sourceHint) {
    const QList<int> weights = ws::presetWeights(layout);
    const QList<QList<ws::Role>> columns = ws::presetColumns(layout);
    QWidget *page = m_tabs->currentWidget();
    if (!page || columns.isEmpty()) return;
    auto *holder = static_cast<WorkspaceHolder *>(ensureWorkspace(page));
    ws::Group &group = holder->group;
    group.reconcile(liveMembers(leavesIn(page), group));
    const QList<QWidget *> leaves = leavesIn(page);

    // The console: the group's, else the terminal pane last used in this tab, else a new one.
    QWidget *console = leafFor(leavesIn(page), group, ws::Role::Console);
    if (!console) {
        if (m_active && pageOf(m_active) == page) console = m_active;
        else if (const auto panes = panesIn(page); !panes.isEmpty()) console = panes.first();
    }
    // The editor: the group's, else the file pane in front (the active one, then the first in the
    // tab) that is not the preview and holds something editable, else the group's first source,
    // else whatever the person picks.
    QWidget *previewLeaf = leafFor(leavesIn(page), group, ws::Role::Preview);
    QWidget *editor = leafFor(leavesIn(page), group, ws::Role::Editor);
    const auto editable = [&](QWidget *leaf) {
        if (!isLocalPreview(leaf) || leaf == previewLeaf) return false;
        const ws::Adapter *adapter = ws::AdapterRegistry::instance().adapterFor(
            static_cast<ToolPane *>(leaf)->preview()->path(), group.kind);
        return adapter && adapter->canEdit;
    };
    if (!editor && !sourceHint.isEmpty()) {
        for (QWidget *leaf : leaves)
            if (editable(leaf) && static_cast<ToolPane *>(leaf)->preview()->path() == sourceHint) editor = leaf;
    }
    if (!editor && sourceHint.isEmpty() && editable(m_activeLeaf) && pageOf(m_activeLeaf) == page) editor = m_activeLeaf;
    if (!editor && sourceHint.isEmpty())
        for (QWidget *leaf : leaves) if (editable(leaf)) { editor = leaf; break; }
    QString source = editor ? static_cast<ToolPane *>(editor)->preview()->path()
                            : !sourceHint.isEmpty() ? sourceHint : group.sources.value(0);
    if (!editor) {
        if (source.isEmpty() || !QFileInfo(source).isFile()) source = pickFileForPreview();
        if (source.isEmpty()) return;
        source = QFileInfo(source).absoluteFilePath();
        editor = createToolPane(ToolPane::Kind::Preview, source);
    }
    group.addSource(source);
    if (group.kind == QLatin1String("plain")) group.kind = ws::kindForSource(source);
    if (group.root.isEmpty()) {
        group.root = tabProject(page);
        if (group.root.isEmpty()) group.root = QFileInfo(source).absolutePath();
    }
    if (!console) {
        try { console = createPane({{"cwd", group.root}, {"workspace", group.root}}); }
        catch (const std::exception &error) { QMessageBox::critical(this, QStringLiteral("Relay"), QString::fromUtf8(error.what())); return; }
    }
    // The output, and the preview showing it.
    const QString outPath = group.outputs.isEmpty() ? ws::defaultOutputFor(source) : group.outputs.first().path;
    const ws::Adapter *adapter = ws::AdapterRegistry::instance().adapterFor(outPath, group.kind);
    group.ensureOutput(outPath, adapter->id, adapter->authority);
    if (!previewLeaf)
        for (QWidget *leaf : leaves)
            if (leaf != editor && isLocalPreview(leaf) && static_cast<ToolPane *>(leaf)->preview()->path() == outPath) {
                previewLeaf = leaf;
                break;
            }
    if (!previewLeaf) previewLeaf = createToolPane(ToolPane::Kind::Preview, outPath);

    group.setMember(mintMember(console), ws::Role::Console);
    group.setMember(mintMember(editor), ws::Role::Editor);
    group.setMember(mintMember(previewLeaf), ws::Role::Preview);
    group.layout = layout;

    // Take every leaf out of the tab's splitters, put the three in the preset's columns, and keep
    // any other pane of the tab in one more column on the right rather than closing it.
    const QMap<ws::Role, QWidget *> byRole{{ws::Role::Console, console}, {ws::Role::Editor, editor},
                                            {ws::Role::Preview, previewLeaf}};
    QList<QWidget *> others;
    for (QWidget *leaf : leaves)
        if (leaf != console && leaf != editor && leaf != previewLeaf) others << leaf;
    for (QWidget *leaf : leavesIn(page)) { leaf->hide(); leaf->setParent(nullptr); }
    while (QLayoutItem *item = page->layout()->takeAt(0)) {
        if (QWidget *old = item->widget()) { old->hide(); old->deleteLater(); }
        delete item;
    }
    QSplitter *root = newSplitter(Qt::Horizontal);
    QList<QSplitter *> stacks;
    for (const QList<ws::Role> &column : columns) {
        if (column.size() == 1) { root->addWidget(byRole.value(column.first())); continue; }
        QSplitter *stack = newSplitter(Qt::Vertical);
        for (ws::Role role : column) stack->addWidget(byRole.value(role));
        root->addWidget(stack);
        stacks << stack;
    }
    QList<int> rootWeights = weights;
    if (!others.isEmpty()) {
        QSplitter *rest = newSplitter(Qt::Vertical);
        for (QWidget *leaf : others) rest->addWidget(leaf);
        root->addWidget(rest);
        stacks << rest;
        rootWeights << 1;
    }
    page->layout()->addWidget(root);
    for (QWidget *leaf : leavesIn(page)) leaf->show();
    root->show();
    // The weights once Qt has laid the new splitters out; sizes set on a hidden splitter follow
    // the size hints instead. From here on a drag changes them and the layout saves what it did.
    QPointer<QSplitter> rootGuard(root);
    QList<QPointer<QSplitter>> stackGuards;
    for (QSplitter *stack : stacks) stackGuards << stack;
    QTimer::singleShot(0, root, [rootGuard, stackGuards, rootWeights] {
        if (!rootGuard) return;
        const QList<int> sizes = ws::weightedSizes(rootWeights, rootGuard->width());
        if (sizes.size() == rootGuard->count()) rootGuard->setSizes(sizes);
        for (const QPointer<QSplitter> &stack : stackGuards) {
            if (!stack) continue;
            QList<int> equal;
            for (int i = 0; i < stack->count(); ++i) equal << 1;
            stack->setSizes(ws::weightedSizes(equal, stack->height()));
        }
    });

    auto *editorTool = static_cast<ToolPane *>(editor);
    if (!editorTool->preview()->isEditable()) editorTool->preview()->startEditing();
    refreshWorkspace(page);
    setActiveLeaf(editor);
    focusLeaf(editor);
    updateTitles();
    m_manager->scheduleSave();
    notice(QStringLiteral("Workspace layout %1: %2").arg(layout, columns.size() >= 3
                                                                   ? QStringLiteral("console | editor | preview")
                                                                   : QStringLiteral("editor over console | preview")), 3000);
}

// ----- navigation --------------------------------------------------------------------------------

bool RelayWindow::openInWorkspaceEditor(const QString &path, int line, QWidget *anchor) {
    if (relay::remote::isFileUrl(path)) return false;
    const QFileInfo info(path);
    if (!info.isFile()) return false;
    if (!anchor || !isLeaf(anchor) || anchor->window() != this) anchor = m_activeLeaf;
    QWidget *page = pageOf(anchor);
    WorkspaceHolder *holder = holderOf(page);
    if (!holder) return false;
    ws::Group &group = holder->group;
    group.reconcile(liveMembers(leavesIn(page), group));
    const QString file = info.absoluteFilePath();
    // An output goes to the preview that shows it, which is already the pane for it.
    if (group.output(file) && !group.hasSource(file)) {
        QWidget *preview = leafFor(leavesIn(page), group, ws::Role::Preview);
        if (!preview || static_cast<ToolPane *>(preview)->preview()->path() != file) return false;
        m_tabs->setCurrentWidget(page);
        setActiveLeaf(preview);
        focusLeaf(preview);
        return true;
    }
    auto *editor = static_cast<ToolPane *>(leafFor(leavesIn(page), group, ws::Role::Editor));
    if (!editor) return false;
    // A source: one of the group's, or an editable file under its root (a chapter, a .bib, a
    // script) — a build diagnostic or a link names those, and they belong in the group's editor.
    const QString root = group.root.isEmpty() ? QString() : QDir(group.root).absolutePath() + QLatin1Char('/');
    const ws::Adapter *adapter = ws::AdapterRegistry::instance().adapterFor(file, group.kind);
    const bool source = group.hasSource(file)
                        || (!root.isEmpty() && file.startsWith(root) && adapter && adapter->canEdit
                            && adapter->authority == ws::Authority::Editable);
    if (!source) return false;
    relay::FilePreview *view = editor->preview();
    if (view->path() != file) {
        // Unsaved edits in the editor are never dropped to follow a link: the file opens the old
        // way, in a preview beside it.
        if (view->isDirty()) return false;
        if (!view->open(file)) return false;
        view->startEditing();
        group.addSource(file);
    }
    if (line > 0) view->goToLine(line);
    m_tabs->setCurrentWidget(page);
    setActiveLeaf(editor);
    focusLeaf(editor);
    refreshWorkspace(page);
    updateTitles();
    m_manager->scheduleSave();
    return true;
}

// ----- the driver (scripts/relay-drive workspace …) ---------------------------------------------

QJsonObject RelayWindow::driveWorkspace(const QJsonObject &request) {
    const QString name = request.value(QStringLiteral("name")).toString();
    const QString text = request.value(QStringLiteral("text")).toString();
    QWidget *page = m_tabs->currentWidget();
    if (name == QLatin1String("preset")) {
        const QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (words.isEmpty() || ws::presetWeights(words.first()).isEmpty())
            return {{"ok", false}, {"error", "unknown_layout"}};
        const QString source = words.size() > 1 ? QFileInfo(words.mid(1).join(QLatin1Char(' '))).absoluteFilePath() : QString();
        if (source.isEmpty() && !holderOf(page) && leavesIn(page).size() < 2)
            return {{"ok", false}, {"error", "no_source"}};   // would open a file dialog
        applyWorkspacePreset(words.first(), source);
    } else if (name == QLatin1String("status")) {
        // A builder's status (tex_build.BuildStatus.to_dict()) for the group's first output, or
        // the output named by "path" inside it.
        WorkspaceHolder *holder = holderOf(page);
        if (!holder || holder->group.outputs.isEmpty()) return {{"ok", false}, {"error", "no_workspace"}};
        const QJsonObject status = QJsonDocument::fromJson(text.toUtf8()).object();
        ws::Output *out = holder->group.output(status.value(QStringLiteral("path")).toString());
        if (!out) out = &holder->group.outputs.first();
        ws::applyBuildStatus(*out, status, ws::sourceRevisionOnDisk(holder->group.sources, holder->group.root));
        refreshWorkspace(page);
    } else if (name == QLatin1String("refresh")) {
        refreshWorkspace(page);
    } else if (name != QLatin1String("state") && !name.isEmpty()) {
        return {{"ok", false}, {"error", "unknown_workspace_op"}};
    }
    QJsonObject answer{{"ok", true}, {"group", workspaceGroupJson(page)}};
    QJsonArray panes;
    for (QWidget *leaf : leavesIn(page)) {
        QJsonObject row{{"member", memberOf(leaf)}, {"title", leafTitles(leaf).value(0)}};
        if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->preview()) {
            row.insert(QStringLiteral("path"), tool->preview()->path());
            row.insert(QStringLiteral("editing"), tool->preview()->isEditable());
        }
        const QRect rect(leaf->mapTo(page, QPoint(0, 0)), leaf->size());
        row.insert(QStringLiteral("rect"), QJsonArray{rect.x(), rect.y(), rect.width(), rect.height()});
        if (StatusStrip *strip = stripOf(leaf)) row.insert(QStringLiteral("strip"), strip->text());
        panes.append(row);
    }
    answer.insert(QStringLiteral("panes"), panes);
    return answer;
}
