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
    // The chain: the chip in every member's chrome, and a placeholder for a member whose file
    // is gone (#R660) — the member keeps its place in the chain until the file is back.
    reconcileMissingMembers(page);
    refreshChainChips(page);
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
    if (group.id.isEmpty()) group.id = ws::Group::newId();   // an unsaved group was never a group
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

// A member whose file is gone keeps its pane and its place in the chain, with a placeholder in
// it (#R660 t:fy): "main.tex is gone — Reopen…" re-picks the file (moved, renamed); the chain is
// not broken by a file that is merely missing. A generated output that has not been built yet is
// not "gone" — the preview's own not-yet-built state covers it — so this is the editor's member.
void RelayWindow::reconcileMissingMembers(QWidget *page) {
    WorkspaceHolder *holder = holderOf(page);
    if (!holder || pageOf(page) != page) return;
    for (QWidget *leaf : leavesIn(page)) {
        auto *tool = dynamic_cast<ToolPane *>(leaf);
        if (!tool || !tool->preview()) continue;
        const auto role = holder->group.roleOf(memberOf(leaf));
        if (!role || *role != ws::Role::Editor) continue;
        const QString path = tool->preview()->path();
        QWidget *placeholder = tool->findChild<QWidget *>(QStringLiteral("workspaceMissingFile"));
        const bool missing = !path.isEmpty() && !QFileInfo(path).exists();
        if (!missing) {
            if (placeholder) placeholder->deleteLater();
            continue;
        }
        if (placeholder) continue;
        auto *box = new QWidget(tool);
        box->setObjectName(QStringLiteral("workspaceMissingFile"));
        auto *column = new QVBoxLayout(box);
        column->setContentsMargins(16, 16, 16, 16);
        column->addStretch();
        auto *label = new QLabel(QStringLiteral("%1 is gone.").arg(QDir::toNativeSeparators(path)), box);
        label->setAlignment(Qt::AlignCenter);
        label->setWordWrap(true);
        auto *reopen = new QPushButton(QStringLiteral("Reopen…"), box);
        reopen->setObjectName(QStringLiteral("workspaceMissingReopen"));
        column->addWidget(label, 0, Qt::AlignCenter);
        column->addWidget(reopen, 0, Qt::AlignHCenter);
        column->addStretch();
        QPointer<RelayWindow> guard(this);
        QPointer<QWidget> pageGuard(page);
        QPointer<ToolPane> toolGuard(tool);
        QObject::connect(reopen, &QPushButton::clicked, box, [guard, pageGuard, toolGuard, path] {
            if (!guard || !toolGuard) return;
            WorkspaceHolder *holder2 = holderOf(pageGuard);
            const QString again = QFileDialog::getOpenFileName(guard, QStringLiteral("Reopen"),
                QFileInfo(path).exists() ? path : (holder2 ? holder2->group.root : QString()),
                QStringLiteral("LaTeX (*.tex *.ltx);;All files (*)"));
            if (again.isEmpty() || !toolGuard->preview()->open(again)) return;
            if (!toolGuard->preview()->isEditable()) toolGuard->preview()->startEditing();
            if (holder2) {
                holder2->group.sources.removeAll(again);
                holder2->group.addSource(again);
            }
            guard->refreshWorkspace(pageGuard);
        });
        box->show();
    }
}

// ----- the chain (#R660): shell -> TeX editor -> PDF preview -------------------------------------

// The chain chip's label for a member: the console reads "shell", a file pane its file's name —
// the card's "⛓ shell › main.tex › main.pdf".
static QString chainLabel(QWidget *leaf, ws::Role role) {
    if (role == ws::Role::Console) return QStringLiteral("shell");
    auto *tool = dynamic_cast<ToolPane *>(leaf);
    if (tool && tool->preview()) {
        const QString path = tool->preview()->path();
        if (!path.isEmpty()) return QFileInfo(path).fileName();
    }
    return ws::roleName(role);
}

// The chain's live members, head-first, with their leaves: the group's `order` pruned to the
// panes on screen that really hold the role.
static QList<QPair<QWidget *, ws::Role>> chainMembers(const QList<QWidget *> &leaves, const ws::Group &group) {
    QList<QPair<QWidget *, ws::Role>> members;
    for (const QString &id : group.order) {
        const auto role = group.roleOf(id);
        if (!role) continue;
        for (QWidget *leaf : leaves)
            if (memberOf(leaf) == id) { members.append({leaf, *role}); break; }
    }
    return members;
}

void RelayWindow::refreshChainChips(QWidget *page) {
    if (!page) return;
    WorkspaceHolder *holder = holderOf(page);
    if (!holder || pageOf(page) != page) return;
    const QList<QPair<QWidget *, ws::Role>> members = chainMembers(leavesIn(page), holder->group);
    QStringList labels;
    for (const auto &member : members) labels << chainLabel(member.first, member.second);
    for (QWidget *leaf : leavesIn(page)) {
        PaneChrome *chrome = chromeOf(leaf);
        if (!chrome) continue;
        int mine = -1;
        for (int i = 0; i < members.size(); ++i)
            if (members.at(i).first == leaf) mine = i;
        if (mine < 0 || members.size() < 2) {
            chrome->setChain({}, -1);
            continue;
        }
        chrome->setChain(labels, mine);
        QPointer<RelayWindow> guard(this);
        QPointer<QWidget> pageGuard(page);
        const QList<QPointer<QWidget>> chainLeaves = [members] { QList<QPointer<QWidget>> out; for (const auto &m : members) out << m.first; return out; }();
        chrome->onChainPick = [guard, pageGuard, chainLeaves](int at) {
            if (!guard || at < 0 || at >= chainLeaves.size() || !chainLeaves.at(at)) return;
            guard->m_tabs->setCurrentWidget(pageGuard);
            guard->setActiveLeaf(chainLeaves.at(at));
            guard->focusLeaf(chainLeaves.at(at));
        };
    }
}

// The preset's shape for the members the chain has (#R660 t:qr): the columns the preset names,
// with the members present, head-first; a column whose members have not joined yet is skipped, so
// a chain of two takes the preset's prefix (console | editor) and grows into the full shape when
// the preview joins. Any other pane of the tab keeps one more column on the right.
void RelayWindow::dockWorkspaceChain(QWidget *page) {
    WorkspaceHolder *holder = holderOf(page);
    if (!holder) return;
    ws::Group &group = holder->group;
    group.reconcile(liveMembers(leavesIn(page), group));
    const QList<QPair<QWidget *, ws::Role>> members = chainMembers(leavesIn(page), group);
    if (members.isEmpty()) return;
    const QString layout = group.layout.isEmpty() ? QStringLiteral("1:1:1") : group.layout;
    const QList<int> weights = ws::presetWeights(layout);
    const QList<QList<ws::Role>> columns = ws::presetColumns(layout);
    if (columns.isEmpty()) return;

    const QMap<ws::Role, QWidget *> byRole = [&members] {
        QMap<ws::Role, QWidget *> map;
        for (const auto &member : members) map.insert(member.second, member.first);
        return map;
    }();
    QSet<QWidget *> moving;
    for (const auto &member : members) moving.insert(member.first);

    const QList<QWidget *> leaves = leavesIn(page);
    QList<QWidget *> others;
    for (QWidget *leaf : leaves)
        if (!moving.contains(leaf)) others << leaf;
    for (QWidget *leaf : leaves) { leaf->hide(); leaf->setParent(nullptr); }
    while (QLayoutItem *item = page->layout()->takeAt(0)) {
        if (QWidget *old = item->widget()) { old->hide(); old->deleteLater(); }
        delete item;
    }
    QSplitter *root = newSplitter(Qt::Horizontal);
    QList<QSplitter *> stacks;
    QList<int> rootWeights;
    for (int c = 0; c < columns.size(); ++c) {
        QList<QWidget *> columnLeaves;
        for (ws::Role role : columns.at(c))
            if (QWidget *leaf = byRole.value(role, nullptr)) columnLeaves << leaf;
        if (columnLeaves.isEmpty()) continue;
        if (columnLeaves.size() == 1) {
            root->addWidget(columnLeaves.first());
        } else {
            QSplitter *stack = newSplitter(Qt::Vertical);
            for (QWidget *leaf : columnLeaves) stack->addWidget(leaf);
            root->addWidget(stack);
            stacks << stack;
        }
        if (c < weights.size()) rootWeights << weights.at(c);
        else rootWeights << 1;
    }
    if (others.isEmpty() && root->count() == 0) { root->deleteLater(); return; }
    if (!others.isEmpty()) {
        QSplitter *rest = newSplitter(Qt::Vertical);
        for (QWidget *leaf : others) rest->addWidget(leaf);
        root->addWidget(rest);
        rootWeights << 1;
    }
    page->layout()->addWidget(root);
    for (QWidget *leaf : leavesIn(page)) leaf->show();
    root->show();
    // The weights once Qt has laid the new splitters out; sizes set on a hidden splitter follow
    // the size hints instead (same dance as applyWorkspacePreset).
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
}

// The offer, on `relay open main.tex` from a shell (or a click on the file in its output): open
// the editor beside it, linked. Declining opens the file the old way — the caller's plain open.
bool RelayWindow::openWorkspaceChainSource(const QString &path, int line, QWidget *anchor) {
    Q_UNUSED(line);
    if (relay::remote::isFileUrl(path)) return false;
    const QFileInfo info(path);
    // Today the chain is the TeX one: a .tex opens its editor and, downstream, the PDF. Kinds
    // grow here, not in the offer (a Markdown chain would offer the same, later).
    if (!info.isFile() || info.suffix() != QLatin1String("tex")) return false;
    if (!anchor || !isLeaf(anchor) || anchor->window() != this) anchor = m_activeLeaf;
    if (!anchor) return false;
    QWidget *page = pageOf(anchor);
    if (!page) return false;
    // A pane that already belongs to a group never re-offers: the group's editor takes the file
    // (openInWorkspaceEditor below in openPath).
    if (!memberOf(anchor).isEmpty()) return false;
    auto *shell = dynamic_cast<Pane *>(anchor);
    if (!shell) return false;                       // the chain's head is a shell pane
    if (holderOf(page)) return false;               // this tab is already a workspace

    QPointer<RelayWindow> guard(this);
    QPointer<Pane> shellGuard(shell);
    const QString absolute = info.absoluteFilePath();
    QMessageBox offer(this);
    offer.setIcon(QMessageBox::Question);
    offer.setWindowTitle(QStringLiteral("Relay"));
    offer.setText(QStringLiteral("Open %1 beside this shell, linked?\n"
                                 "The shell, its editor and the PDF preview become a chain that "
                                 "builds, refreshes and closes together.")
                      .arg(info.fileName()));
    QPushButton *linked = offer.addButton(QStringLiteral("Open beside, linked"), QMessageBox::AcceptRole);
    offer.addButton(QStringLiteral("Just open"), QMessageBox::RejectRole);
    offer.setDefaultButton(linked);
    offer.exec();
    if (!guard || !shellGuard || offer.clickedButton() != linked) return false;

    // Form the chain: the shell is adopted as the head, the editor joins beside it.
    auto *holder = static_cast<WorkspaceHolder *>(ensureWorkspace(page));
    ws::Group &group = holder->group;
    if (group.id.isEmpty()) group.id = ws::Group::newId();
    group.kind = ws::kindForSource(absolute);
    group.root = info.absolutePath();
    group.layout = group.layout.isEmpty() ? QStringLiteral("1:1:1") : group.layout;
    group.sources.removeAll(absolute);
    group.addSource(absolute);
    const ws::Adapter *adapter = ws::AdapterRegistry::instance().adapterFor(absolute, group.kind);
    group.ensureOutput(ws::defaultOutputFor(absolute), adapter->id, adapter->authority);
    group.setMember(mintMember(shellGuard), ws::Role::Console);
    ToolPane *editor = createToolPane(ToolPane::Kind::Preview, absolute);
    group.setMember(mintMember(editor), ws::Role::Editor);
    if (!editor->preview()->isEditable()) editor->preview()->startEditing();
    // Park the newcomer in the tab so the chain layout can see it, then let the preset arrange it.
    page->layout()->addWidget(editor);
    dockWorkspaceChain(page);
    refreshWorkspace(page);
    setActiveLeaf(editor);
    focusLeaf(editor);
    updateTitles();
    m_manager->scheduleSave();
    return true;
}

// The chain's next member: the editor beside the head, then the preview beside the editor —
// "open the next" from the shell, the editor's Build, or a chip click on a member that has not
// joined yet. Members already present are focused, not duplicated.
void RelayWindow::openWorkspaceNext(QWidget *page) {
    if (!page) page = m_tabs->currentWidget();
    WorkspaceHolder *holder = holderOf(page);
    if (!holder || pageOf(page) != page) return;
    ws::Group &group = holder->group;
    group.reconcile(liveMembers(leavesIn(page), group));
    // The chain's tail, and the role that joins downstream of it.
    const QList<QPair<QWidget *, ws::Role>> members = chainMembers(leavesIn(page), group);
    ws::Role next = ws::Role::Console;
    QWidget *tail = nullptr;
    if (members.isEmpty()) return;
    for (const auto &member : members) {
        tail = member.first;
        if (member.second == ws::Role::Editor) next = ws::Role::Preview;
        else if (member.second == ws::Role::Console) next = ws::Role::Editor;
    }
    if (next == ws::Role::Console) return;
    if (QWidget *present = leafFor(leavesIn(page), group, next)) {
        setActiveLeaf(present);
        focusLeaf(present);
        return;
    }
    if (next == ws::Role::Editor) {
        const QString source = group.sources.value(0);
        if (source.isEmpty() || !QFileInfo(source).isFile()) return;
        ToolPane *editor = createToolPane(ToolPane::Kind::Preview, source);
        group.setMember(mintMember(editor), ws::Role::Editor);
        if (!editor->preview()->isEditable()) editor->preview()->startEditing();
        page->layout()->addWidget(editor);   // parked so the chain layout can see it
        dockWorkspaceChain(page);
        refreshWorkspace(page);
        setActiveLeaf(editor);
        focusLeaf(editor);
    } else {
        const ws::Output *out = group.outputs.isEmpty() ? nullptr : &group.outputs.first();
        const QString output = out ? out->path : ws::defaultOutputFor(group.sources.value(0));
        ToolPane *preview = createToolPane(ToolPane::Kind::Preview, output);
        group.setMember(mintMember(preview), ws::Role::Preview);
        page->layout()->addWidget(preview);  // parked so the chain layout can see it
        dockWorkspaceChain(page);
        refreshWorkspace(page);
        setActiveLeaf(preview);
        focusLeaf(preview);
    }
    updateTitles();
    m_manager->scheduleSave();
}

// Closing a chain's head asks about the rest (#R660 t:fy): "Close the linked panes too?" — all
// of them, or just the head. Closing any other member is the plain close it always was; the chain
// heals around it.
bool RelayWindow::workspaceChainClose(QWidget *pane) {
    if (!pane) return false;
    const QString member = memberOf(pane);
    if (member.isEmpty()) return false;
    QWidget *page = pageOf(pane);
    WorkspaceHolder *holder = holderOf(page);
    if (!holder) return false;
    ws::Group &group = holder->group;
    group.reconcile(liveMembers(leavesIn(page), group));
    if (group.head() != member || group.members.size() < 2) return false;

    QMessageBox ask(this);
    ask.setIcon(QMessageBox::Question);
    ask.setWindowTitle(QStringLiteral("Relay"));
    ask.setText(QStringLiteral("Close the linked panes too?\n"
                               "This shell is the head of a chain that also has its %1.")
                    .arg(group.members.size() == 2 ? QStringLiteral("editor")
                                                   : QStringLiteral("editor and preview")));
    QPushButton *all = ask.addButton(QStringLiteral("Close them all"), QMessageBox::YesRole);
    QPushButton *just = ask.addButton(QStringLiteral("Just this pane"), QMessageBox::NoRole);
    ask.setDefaultButton(all);
    ask.setEscapeButton(just);            // Escape means: leave the rest of the chain alone
    ask.exec();
    if (!all || ask.clickedButton() != all) return false;   // just this pane: the plain close

    // Close them all: dissolve the group first so each member's close is a plain close, and take
    // the chain downstream-to-head (preview, editor, shell). When the chain is the whole tab of
    // the window's only tab, the head is not closed — the window itself would go with it —
    // it stays as the plain shell it was, unlinked.
    ws::Group dissolved;
    dissolved.id = group.id;              // the group's identity stays for the state file
    const QList<QWidget *> leaves = leavesIn(page);
    QList<QWidget *> chainLeaves;
    for (const QString &id : group.order)
        for (QWidget *leaf : leaves)
            if (memberOf(leaf) == id) chainLeaves << leaf;
    holder->group = dissolved;
    for (QWidget *leaf : chainLeaves)
        if (leaf) leaf->setProperty(kMemberProperty, QVariant());
    const bool headStays = leaves.size() == chainLeaves.size() && m_tabs->count() <= 1;
    for (int i = chainLeaves.size() - 1; i >= 0; --i) {
        QWidget *leaf = chainLeaves.at(i);
        if (!leaf) continue;
        if (i == 0 && headStays) {
            notice(QStringLiteral("The chain is closed; the shell stays as a plain pane."), 6000);
            break;
        }
        closePane(leaf, true);
    }
    m_manager->scheduleSave();
    return true;
}

// A member that lands in another tab of this window brings the chain with it, in order (#R660
// t:fy): the shell, then the editor, then the preview, docked beside each other as the preset
// lays them. Same tab (a nudge inside the splitter) is not a move; cross-window drags are the
// adopting window's business (adoptLeaf), and leave the chain to heal.
void RelayWindow::moveWorkspaceChain(QWidget *member) {
    if (!member) return;
    const QString id = memberOf(member);
    if (id.isEmpty()) return;
    QWidget *page = pageOf(member);
    if (!page || pageOf(page) != page) return;
    WorkspaceHolder *holder = holderOf(page);
    if (!holder || !holder->group.roleOf(id)) {
        // The group lives on another tab of this window: the chain follows the member (#R660).
        // Its state moves with it — the tab it left stops being the workspace. A member whose
        // group is in another window (a cross-window drag) is left alone: the chain heals there.
        WorkspaceHolder *old = nullptr;
        for (int t = 0; t < m_tabs->count() && !old; ++t) {
            WorkspaceHolder *candidate = holderOf(m_tabs->widget(t));
            if (candidate && candidate->group.roleOf(id)) old = candidate;
        }
        if (!old) return;
        ws::Group group = old->group;
        old->group = ws::Group{};
        if (QWidget *oldPage = qobject_cast<QWidget *>(old->parent()))
            oldPage->setProperty(kHolderProperty, QVariant());
        old->deleteLater();
        holder = static_cast<WorkspaceHolder *>(ensureWorkspace(page));
        holder->group = group;
    }
    ws::Group &group = holder->group;
    group.reconcile(liveMembers(leavesIn(page), group));
    if (group.members.size() < 2) return;

    // The chain's other members that are live but not in this tab: strays to bring home, in
    // chain order so they dock head-first.
    const QList<QWidget *> here = leavesIn(page);
    QList<QWidget *> strays;
    for (const QString &other : group.order) {
        if (other == id) continue;
        bool inTab = false;
        for (QWidget *leaf : here)
            if (memberOf(leaf) == other) inTab = true;
        if (inTab) continue;
        QWidget *found = nullptr;
        for (int t = 0; t < m_tabs->count() && !found; ++t)
            for (QWidget *leaf : leavesIn(m_tabs->widget(t)))
                if (memberOf(leaf) == other) found = leaf;
        if (found) strays << found;
    }
    if (strays.isEmpty()) { refreshWorkspace(page); return; }
    QWidget *anchor = member;
    for (QWidget *leaf : strays) {
        if (!takeLeaf(leaf)) continue;
        insertBeside(anchor, leaf, Qt::Horizontal, false);
        anchor = leaf;
    }
    dockWorkspaceChain(page);
    refreshWorkspace(page);
    updateTitles();
    m_manager->scheduleSave();
}

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
    // A different source opens beside this editor through openPath. Keep the linked source and
    // its edit state in place even when it has no unsaved changes (#10RD).
    if (view->path() != file) return false;
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
    } else if (name == QLatin1String("next")) {
        // #R660: open the chain's next member — editor beside the head, then preview beside the
        // editor — the same step the shell's "Open beside, linked" offer takes, drivable.
        openWorkspaceNext(page);
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
