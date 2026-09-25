// SPDX-License-Identifier: AGPL-3.0-or-later
#include "RelayWindow.h"
#include "TextRedoShortcut.h"
#include "WindowManagerImpl.h"

bool RelayWindow::eventFilter(QObject *object, QEvent *event) {
        // Tab labels (issue JRWQ): double click a tab to name it by hand; Esc leaves it alone.
        if (object == m_tabs->tabBar() && event->type() == QEvent::MouseButtonDblClick) {
            const int index = m_tabs->tabBar()->tabAt(static_cast<QMouseEvent *>(event)->pos());
            if (index >= 0) {
                renameTab(QString(), true, m_tabs->widget(index));
                hint(QStringLiteral("tab.rename"), QStringLiteral("Next time: /rename-tab <name> · /rename names the pane"));
                return true;
            }
        }
        if (m_tabEdit && object == m_tabEdit) {
            if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
                endTabRename();
                return true;
            }
            if (event->type() == QEvent::FocusOut) endTabRename();
        }
        // "New pane, then ← ↑ ↓ places it" (issue #78BN). The window closes on the first key or
        // click; a bare arrow acts, and so does one with Ctrl still held from the split key when
        // the keymap leaves that chord free (card #JXWT), and everything else is passed on
        // untouched. Which widget has the keyboard does not matter: with no focus at all the key
        // reaches the window itself, and the arrow still places the pane (see #4PW5).
        if (m_placement.armed(m_placementClock.elapsed())
            && (event->type() == QEvent::KeyPress || event->type() == QEvent::MouseButtonPress)
            && [&] { auto *w = qobject_cast<QWidget *>(object); return !w || w->window() == this; }()) {
            using Placement = relay::panes::PlacementWindow;
            const auto *key = event->type() == QEvent::KeyPress ? static_cast<QKeyEvent *>(event) : nullptr;
            const Placement::Response response =
                key ? m_placement.keyPress(key->key(), key->modifiers(), m_placementClock.elapsed(),
                                           Keymap::instance().match(key))
                    : m_placement.mousePress(m_placementClock.elapsed());
            if (response.action == Placement::Action::Place) {
                const auto direction = response.direction;
                // Move once the key event has been dealt with: re-parenting panes underneath a
                // delivery in progress is what made the drag path crash before.
                QTimer::singleShot(0, this, [this, direction] { placeNewPane(direction); });
                return true;
            }
            if (response.action == Placement::Action::Dismiss) endPlacement();
        }
        // The chord's window (#Q7Y9) stays open only for the keys that are part of the chord:
        // a modifier held on its own, and whatever the keymap binds to Move-left/right/down —
        // the Move-down key passes through here on its way to the shortcut that runs it. Every
        // other key closes it, Ctrl+C, Ctrl+D and Ctrl+L included: those are the shell's keys,
        // and treating any modified key as "half a shortcut" left the window armed while the
        // user typed on, so a Move-down minutes later docked a pane out of the blue.
        // A click or a scroll closes it too (the wheel over the tab bar changes tab).
        if (m_beneath.armed(m_beneathClock.elapsed())
            && (event->type() == QEvent::KeyPress || event->type() == QEvent::MouseButtonPress
                || event->type() == QEvent::Wheel)
            && [&] { auto *w = qobject_cast<QWidget *>(object); return !w || w->window() == this; }()) {
            const auto *pressed = event->type() == QEvent::KeyPress ? static_cast<QKeyEvent *>(event) : nullptr;
            if (!pressed || !relay::panes::chordKeyKeepsWindow(pressed->key(), Keymap::instance().match(pressed)))
                endBeneathDock();
        }
        if (event->type() == QEvent::Wheel) {
            auto *wheel = static_cast<QWheelEvent *>(event);
            if (wheel->modifiers() == Qt::ControlModifier) {
                auto *widget = qobject_cast<QWidget *>(object);
                QWidget *leaf = leafOf(widget);
                if (leaf && leaf->window() == this && !dynamic_cast<Pane *>(leaf)) {
                    auto *view = relay::auxiliaryZoom::target(leaf, widget);
                    if (view && (widget == view || view->isAncestorOf(widget))) {
                        relay::auxiliaryZoom::rememberFont(view);
                        hint(QStringLiteral("auxiliary.zoom.wheel"), relay::ShortcutHints::nextTime(
                            Keymap::instance().shortcutText(QStringLiteral("terminal.zoomIn")), QStringLiteral("zoom this pane")));
                    }
                }
            }
            if (wheel->modifiers() == Qt::AltModifier) {
                QWidget *leaf = leafOf(qobject_cast<QWidget *>(object));
                if (leaf && leaf->window() == this) {
                    if (auto *chrome = chromeOf(leaf)) {
                        const int steps = chrome->dimming.wheelSteps(wheel->angleDelta());
                        if (steps) adjustPaneDimming(leaf, -5 * steps);
                        wheel->accept();
                        return true;
                    }
                }
            }
        }
        if (event->type() == QEvent::MouseButtonPress) {
            QWidget *leaf = leafOf(qobject_cast<QWidget *>(object));
            if (leaf && leaf->window() == this)
                if (auto *chrome = chromeOf(leaf)) { chrome->dimming.revealed = true; refreshPaneDimming(); }
        }
        activateOnPress(object, event);
        if (headerDrag(object, event)) return true;
        if (toolHeaderDrag(object, event)) return true;
        // A tab-label drag that leaves this window (#W6ES). Runs for its side effects only and
        // never consumes: Qt's own tab drag owns the bar's mouse grab and has to see every
        // event through to its release.
        tabDrag(object, event);
        if (event->type() == QEvent::Resize && isLeaf(qobject_cast<QWidget *>(object)))
            if (auto *chrome = chromeOf(static_cast<QWidget *>(object))) chrome->place();
        if (object == m_tabs->tabBar() && (event->type() == QEvent::Resize || event->type() == QEvent::MouseMove || event->type() == QEvent::Leave
                                           || event->type() == QEvent::Enter || event->type() == QEvent::LayoutRequest)) {
            const bool resized = event->type() == QEvent::Resize;
            QTimer::singleShot(0, this, [this, resized] {
                placeTabBarControls();
                if (resized) relabelTabsForWidth();
            });
        }
        if (event->type() == QEvent::MouseButtonPress) {
            auto *bar = qobject_cast<QTabBar *>(object);
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (bar && bar->window() == this && bar->property("paneTabNavigationOwner").isValid()
                && mouse->button() == Qt::LeftButton && bar->tabAt(mouse->pos()) >= 0
                && bar->tabAt(mouse->pos()) != bar->currentIndex())
                hint(QStringLiteral("pane.tabs.mouse"),
                     relay::ShortcutHints::nextTime(QStringLiteral("Tab / Shift+Tab"), QStringLiteral("pane tabs")));
        }
        if (event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride)
            return QMainWindow::eventFilter(object, event);
        auto *widget = qobject_cast<QWidget *>(object);
        if (!widget || widget->window() != this) return QMainWindow::eventFilter(object, event);
        auto *key = static_cast<QKeyEvent *>(event);
        if (relay::paneTabs::handle(widget, key)) return true;
        // Keyboard walk over the links in the output (issue GWXM). While it runs, Enter opens
        // the highlighted link, Esc leaves and the arrows move; the keys are taken here, so the
        // walk works with the prompt box focused, which is the normal state. Anything else ends
        // the walk and is handled as usual, so typing is never swallowed.
        if (m_active && m_active->outputLinkWalkActive()) {
            Pane *walking = m_active;
            switch (key->key()) {
            case Qt::Key_Return: case Qt::Key_Enter:
            case Qt::Key_Escape:
            case Qt::Key_Up: case Qt::Key_Left:
            case Qt::Key_Down: case Qt::Key_Right: {
                event->accept();
                if (event->type() != QEvent::KeyPress || key->isAutoRepeat()) return true;
                const int pressed = key->key();
                const auto modifiers = key->modifiers();
                QTimer::singleShot(0, walking, [walking, pressed, modifiers] {
                    if (pressed == Qt::Key_Return || pressed == Qt::Key_Enter) walking->openOutputLink(modifiers);
                    else if (pressed == Qt::Key_Escape) walking->endOutputLinkWalk();
                    else if (pressed == Qt::Key_Down || pressed == Qt::Key_Right) walking->stepOutputLink(1);
                    else walking->stepOutputLink(-1);
                });
                return true;
            }
            default:
                if (event->type() == QEvent::KeyPress && !key->text().isEmpty()
                    && Keymap::instance().match(key) != QStringLiteral("links.step"))
                    walking->endOutputLinkWalk();
                break;
            }
        }
        // The embedded priorities editor owns Alt+Up/Down for reordering (#MDL1).
        // This application-wide filter runs before the picker's own event filter;
        // let both the override and key press reach it instead of navigating panes.
        if (key->modifiers() == Qt::AltModifier
            && (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down)) {
            for (QWidget *parent = widget; parent; parent = parent->parentWidget())
                if (auto *picker = dynamic_cast<relay::ModelPicker *>(parent);
                    picker && picker->sectionsPage())
                    return QMainWindow::eventFilter(object, event);
        }
        // A widget's own keys come before the window's where it declares them (#KYPR): the
        // Actions and Options search's Ctrl+N / Ctrl+P walk its results rather than open a window,
        // and the explorer's Alt+Up is the parent folder rather than the pane above. The widget
        // lists them, in PortableText, in its `relayLocalKeys` property beside the handler.
        if (const QStringList local = widget->property("relayLocalKeys").toStringList(); !local.isEmpty()) {
            const QString pressed = QKeySequence(int(key->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier))
                                                 | key->key()).toString(QKeySequence::PortableText);
            if (local.contains(pressed)) return QMainWindow::eventFilter(object, event);
        }
        const QString id = Keymap::instance().match(key);
        if (key->key() == Qt::Key_Z && (key->modifiers() & Qt::ControlModifier)) {
            auto *state = static_cast<TextRedoShortcut *>(
                findChild<QObject *>(QStringLiteral("recentTextRedo"), Qt::FindDirectChildrenOnly));
            if (!state) {
                state = new TextRedoShortcut(this);
                state->setObjectName(QStringLiteral("recentTextRedo"));
            }
            if (state->handle(widget, key, id, event->type())) return true;
        }
        // The Actions palette answers its own keys, the chord that opened it included (#MAGP).
        if (m_palette && m_palette->isAncestorOf(widget) && id != QStringLiteral("closed.restore"))
            return QMainWindow::eventFilter(object, event);
        if (id.isEmpty()) return QMainWindow::eventFilter(object, event);
        // A program such as vim owns its keys, unless the program_keys rule lets this shortcut act.
        Pane *pane = paneOf(widget);
        // Ctrl+H only takes control from the prompt box; in the terminal it stays Backspace.
        // Ctrl+F only opens the find bar from the prompt box; in the terminal it stays Readline's
        // forward-char, the way Ctrl+H stays Backspace there. Plain Ctrl+Q clears the prompt box
        // only from the prompt box (#CPRQ). Ctrl+Shift+H and Ctrl+Shift+Q act from anywhere, a
        // program's keyboard included, since Ctrl+Shift is Relay's layer (#QWAS).
        const bool shifted = key->modifiers() & Qt::ShiftModifier;
        if ((((id == QStringLiteral("control.human") || id == QStringLiteral("prompt.clear")) && !shifted)
             || id == QStringLiteral("input.toggle") || id == QStringLiteral("agent.interrupt")
             || id == QStringLiteral("pane.runInBackground")
             || id == QStringLiteral("agent.planToggle") || id == QStringLiteral("agent.effortUp") || id == QStringLiteral("agent.effortDown")
             || id == QStringLiteral("find.inView"))
            && !(pane && pane->ownsComposerWidget(widget)))
            return QMainWindow::eventFilter(object, event);
        if (pane && pane->ownsTerminalWidget(widget) && pane->processBusy() && !Keymap::instance().actsInsidePrograms(key))
            return QMainWindow::eventFilter(object, event);
        // Accept the override so neither the composer nor the terminal consumes the key,
        // then act on the key press itself. Auto-repeat does not open a burst of tabs.
        event->accept();
        if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()) {
            // Remember the combination as the desktop writes it, so an action can name the key
            // that actually reached it (the shortcuts overlay does, #T9ZS).
            m_lastShortcut = {id, QKeySequence(int(key->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier))
                                               | key->key()).toString(QKeySequence::NativeText)};
            QTimer::singleShot(0, this, [this, id] { runAction(id); });
        }
        return true;
    }

void RelayWindow::runActionNow(const QString &id, Pane *target) {
        // The pane every `pane->…` branch below acts on. A `target` is the pane an `app_command`
        // was aimed at; unset means the focused pane, which is what every keyboard, palette and
        // chrome caller means and what this line said before there was a target at all.
        //
        // The window and layout branches deliberately do **not** follow it. The splits, the
        // explorer, Equalize, moving a pane or a tab and the focus keys anchor on `m_activeLeaf`
        // — where the person is looking — and they keep doing so: a new pane appearing beside a
        // pane in a tab nobody is watching, or a focus that jumps out of the tab someone is
        // typing in, is a worse surprise than the one being fixed here, and those are all
        // window-scoped keys in the first place (`appcommands::actionIsPaneScoped()` names the
        // set that is not). Aiming them would mean teaching `splitToward()` and its neighbours to
        // take a leaf, which is a change to the layout code and not this card's.
        Pane *pane = target ? target : m_active.data();
        // "Move left/right, then ↓ docks it beneath" (#Q7Y9): only Move-down may land inside the
        // chord's window; any other action closes it, so a stale chord never grabs a later one.
        if (id != QStringLiteral("pane.moveDown")) endBeneathDock();
        if (id == QStringLiteral("window.new")) m_manager->newWindowAt(activeCwd());
        else if (id == QStringLiteral("window.fullscreen")) toggleFullscreen();
        else if (id == QStringLiteral("window.next")) m_manager->cycle(this, 1);
        else if (id == QStringLiteral("window.previous")) m_manager->cycle(this, -1);
        else if (id == QStringLiteral("windows.fresh")) startFreshWindowSet();
        else if (id == QStringLiteral("tab.new")) {
            if (addTab(paneNode(activeCwd()), m_tabs->currentIndex() + 1)) startNewTabTheme(m_tabs->currentWidget());
            markForSshHint(m_active);   // an ssh typed here soon teaches Connect to host
        }
        else if (id == QStringLiteral("ssh.connect")) openSshMenu();
        else if (id == QStringLiteral("ssh.remoteSessions")) openRemoteSessions();
        else if (id == QStringLiteral("ssh.splitSameHost")) splitOnHost(relay::panes::Direction::Right, true, true);
        else if (id == QStringLiteral("tab.next")) cycleTab(1);
        else if (id == QStringLiteral("tab.previous")) cycleTab(-1);
        // One key, one new pane on the right, then ← ↑ ↓ within two seconds to place it (#78BN).
        // Every split goes through splitOnHost, which lands it on the host when the focused pane
        // is on one (#XQ8F) and splits here otherwise, so only the placement differs by direction.
        else if (id == QStringLiteral("pane.splitRight")) splitOnHost(relay::panes::Direction::Right, true);
        else if (id == QStringLiteral("pane.splitDown")) { splitOnHost(relay::panes::Direction::Down); hintPlacement(QStringLiteral("↓")); }
        else if (id == QStringLiteral("pane.splitLeft")) { splitOnHost(relay::panes::Direction::Left); hintPlacement(QStringLiteral("←")); }
        else if (id == QStringLiteral("pane.splitUp")) { splitOnHost(relay::panes::Direction::Up); hintPlacement(QStringLiteral("↑")); }
        // The split that stays on this machine when the focused pane is on a host (#XQ8F).
        else if (id == QStringLiteral("pane.splitLocal")) splitToward(relay::panes::Direction::Right, true);
        // The pane chrome's one ⊞ button (#803C): a pane on the right at once, and no arrow window —
        // the pointer is in hand, so the pane is placed by dragging its header.
        else if (id == QStringLiteral("pane.newByMouse")) {
            splitOnHost(relay::panes::Direction::Right);
            notice(QStringLiteral("New pane · drag its header to place it"), 4000);
            hint(QStringLiteral("pane.new.mouse"),
                 relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(QStringLiteral("pane.splitRight")), QStringLiteral("new pane")));
        }
        else if (id == QStringLiteral("pane.focusLeft")) navigate(relay::panes::Direction::Left);
        else if (id == QStringLiteral("pane.focusRight")) navigate(relay::panes::Direction::Right);
        else if (id == QStringLiteral("pane.focusUp")) navigate(relay::panes::Direction::Up);
        else if (id == QStringLiteral("pane.focusDown")) navigate(relay::panes::Direction::Down);
        else if (id == QStringLiteral("pane.close")) closeActive();
        else if (id == QStringLiteral("pane.closeEndRemote")) closeEndRemote();
        else if (id == QStringLiteral("pane.brighten") || id == QStringLiteral("pane.darken"))
            adjustPaneDimming(m_activeLeaf, id == QStringLiteral("pane.brighten") ? -5 : 5);
        else if (id == QStringLiteral("pane.dimToggle")) {
            if (auto *chrome = chromeOf(m_activeLeaf)) {
                chrome->dimming.toggle(relay::settings::intValue(QStringLiteral("appearance/dim_strength"), 90));
                refreshPaneDimming();
                hint(QStringLiteral("pane.dimmer"), relay::ShortcutHints::nextTime(
                    Keymap::instance().shortcutText(QStringLiteral("pane.brighten")), QStringLiteral("brighten pane · Alt+wheel adjusts this pane")));
            }
        }
        else if (id == QStringLiteral("pane.focusMode") || id == QStringLiteral("pane.autoDim")) {
            const QString key = id == QStringLiteral("pane.focusMode") ? QStringLiteral("appearance/focus_mode") : QStringLiteral("appearance/auto_dim");
            QSettings().setValue(key, !QSettings().value(key, false).toBool());
            relay::SettingsWatch::instance().notify();
            refreshPaneDimming();
        }
        else if (id.startsWith(QStringLiteral("terminal.zoom"))) {
            if (!target && m_activeLeaf && !dynamic_cast<Pane *>(m_activeLeaf.data())) {
                const int step = id == QStringLiteral("terminal.zoomIn") ? 1
                               : id == QStringLiteral("terminal.zoomOut") ? -1 : 0;
                relay::auxiliaryZoom::zoom(m_activeLeaf, QApplication::focusWidget(), step);
            } else if (pane) pane->runTerminalMenuAction(id.mid(9), {}, {}, {});
        }
        else if (id == QStringLiteral("pane.moveLeft")) moveActive(relay::panes::Direction::Left);
        else if (id == QStringLiteral("pane.moveRight")) moveActive(relay::panes::Direction::Right);
        else if (id == QStringLiteral("pane.moveUp")) moveActive(relay::panes::Direction::Up);
        // Inside the chord's window, Move-down docks the pane beneath the neighbor it moved
        // toward (#Q7Y9); otherwise it moves the pane down as it always did.
        else if (id == QStringLiteral("pane.moveDown")) { if (!dockBeneathNeighbor()) moveActive(relay::panes::Direction::Down); }
        else if (id == QStringLiteral("pane.moveToNewTab")) { if (m_activeLeaf) moveLeafToNewTab(m_activeLeaf); }
        else if (id == QStringLiteral("pane.moveToBackground")) moveBackgroundPane(pane);
        else if (id == QStringLiteral("pane.runInBackground")) runBackgroundPane(pane);
        else if (id == QStringLiteral("pane.equalize")) equalizeActivePage();
        // Artifact workspace presets (#E85D): terminal | editor | preview, and editor over
        // terminal beside the preview.
        else if (id == QStringLiteral("workspace.layoutColumns")) applyWorkspacePreset(QStringLiteral("1:1:1"));
        else if (id == QStringLiteral("workspace.layoutEditorOverConsole")) applyWorkspacePreset(QStringLiteral("2:1"));
        else if (id == QStringLiteral("tab.moveToNewWindow")) moveTabToNewWindow(m_tabs->currentIndex());
        else if (id == QStringLiteral("closed.restore")) m_manager->restore(this);
        else if (id == QStringLiteral("closed.list")) openClosedList();
        else if (id == QStringLiteral("files.explorer")) toggleExplorer(activeCwd(), m_activeLeaf);
        else if (id == QStringLiteral("files.open")) {
            const QString file = pickFileForPreview();
            if (!file.isEmpty()) openPath(file, 0, m_activeLeaf);
        }
        else if (id == QStringLiteral("files.toggleWrap")) toggleWrapNear(m_activeLeaf);
        else if (id == QStringLiteral("board.open")) toggleBoardPane();
        else if (id == QStringLiteral("review.open")) openReviewPane();
        else if (id == QStringLiteral("tests.open")) openTestSuitesPane();   // card #7BM4
        else if (id == QStringLiteral("helper.ask")) focusHelperOfActiveLeaf();
        else if (id == QStringLiteral("notifications.jump")) jumpToNotification();   // #NQP9
        // Sessions and Projects share a pane, with direct keys for their tabs.
        else if (id == QStringLiteral("sessions.open")) toggleSessionsPane(pane, QStringLiteral("sessions"));
        else if (id == QStringLiteral("projects.open")) toggleSessionsPane(pane, QStringLiteral("projects"));
        else if (id == QStringLiteral("globals.open")) toggleSessionsPane(pane, QStringLiteral("globals"));
        else if (id == QStringLiteral("agent.resume") || id == QStringLiteral("conversations.open"))
            toggleSessionsPane(pane, QStringLiteral("sessions"));
        else if (id == QStringLiteral("project.pick")) openProjectsFor(m_active, QString());
        else if (id == QStringLiteral("palette.open")) togglePalette();   // the Actions palette, #MAGP
        else if (id == QStringLiteral("help.shortcuts")) openShortcutsTab();
        else if (id == QStringLiteral("keybindings.reload")) Keymap::instance().reload();
        else if (id == QStringLiteral("app.settings")) toggleSettingsPane(false);
        else if (id == QStringLiteral("app.update")) {
            updateApp();
            hint(QStringLiteral("update.palette"), QStringLiteral("Next time: type /update in any prompt box"));
        }
        else if (id == QStringLiteral("app.restart")) restartApp();
        else if (id == QStringLiteral("keybindings.edit")) {
            Keymap::instance().ensureFile();
            const QString editor = qEnvironmentVariable("VISUAL", qEnvironmentVariable("EDITOR", QStringLiteral("nano")));
            const QString quoted = QStringLiteral("'") + QString(Keymap::instance().path()).replace('\'', QStringLiteral("'\\''")) + '\'';
            if (!pane || !pane->runCommand(editor + ' ' + quoted))
                notice(QStringLiteral("Shortcuts file: ") + Keymap::instance().path());
        }
        else if (id == QStringLiteral("agent.subagentPane")) toggleSubagentPane();   // card #WD83
        else if (id == QStringLiteral("remote.openShared")) openSharedPaneDialog();   // Relay-to-Relay
        else if (id == QStringLiteral("remote.pair")) pairPhone();   // #FR1C: one entry point
        else if (id == QStringLiteral("remote.join")) joinSharedSession();
        // Alt+M and Ctrl+Shift+M in a *helper* prompt box (#PK5Q). The two keys belong to a prompt
        // box rather than to a terminal pane: the helper's composer carries the same model box
        // now, so the same keys reach it. Before the `!pane` guard, because Options, Actions,
        // Sessions and the Switchboard are not terminal panes and this is where they answer.
        else if ((id == QStringLiteral("agent.modelBox") || id == QStringLiteral("agent.modelOptions"))
                 && helperComposerHasFocus()) {
            if (id == QStringLiteral("agent.modelBox")) openConsoleModelBox();
            else toggleModelsPane(focusedConsole());
        }
        // The Board answers `find.inView` before the `!pane` guard too, for the same reason as
        // the model box just above: the Switchboard holds no terminal pane, so Ctrl+F used to
        // fall through the guard and do nothing at all (#9NBZ). Asked of the focus widget, so
        // an open card is the document searched, the list page's filter is focused instead, and
        // a terminal pane — never inside a board — keeps the key and finds in itself.
        else if (id == QStringLiteral("find.inView") && !target && focusedBoardView())
            focusedBoardView()->openFind();
        else if (!pane) return;
        else if (id == QStringLiteral("terminal.native")) pane->toggleNative();
        else if (id == QStringLiteral("pane.restartShell")) pane->restartStopped();
        else if (id == QStringLiteral("links.step")) pane->stepOutputLink(-1);
        // One key for both directions (#QWAS): take control, or give the keyboard back.
        else if (id == QStringLiteral("control.human")) { if (pane->isNative()) pane->showPrompt(); else pane->takeControl(); }
        else if (id == QStringLiteral("control.prompt")) pane->showPrompt();
        else if (id == QStringLiteral("prompt.clear")) pane->clearPrompt();   // #CPRQ
        else if (id == QStringLiteral("program.delegate")) pane->delegateProgram();
        else if (id == QStringLiteral("input.toggle")) pane->toggleInputMode();
        else if (id == QStringLiteral("agent.swap")) pane->swapModel();
        else if (id == QStringLiteral("agent.flashAgent")) pane->toggleFlashAgent();   // model roles
        else if (id == QStringLiteral("agent.highAgent")) pane->toggleHighAgent();     // /high, card #MDL1
        else if (id == QStringLiteral("agent.modelBox")) pane->openModelBox();          // Alt+M
        else if (id == QStringLiteral("agent.effortBox")) pane->openEffortBox();        // Alt+E
        else if (id == QStringLiteral("agent.modelOptions")) toggleModelsPane(pane);    // Ctrl+Shift+M, /model, /models
        else if (id == QStringLiteral("agent.localAgent")) pane->toggleLocalAgent();   // /local
        else if (id == QStringLiteral("agent.planToggle")) pane->togglePlanMode();
        else if (id == QStringLiteral("agent.effortUp")) pane->effortStep(1);
        else if (id == QStringLiteral("agent.effortDown")) pane->effortStep(-1);
        else if (id == QStringLiteral("agent.compact")) pane->compactNow();
        else if (id == QStringLiteral("agent.rewind")) {
            pane->openRewind();
            hint(QStringLiteral("rewind.chat.mouse"), QStringLiteral("Next time: Esc Esc in an empty prompt box rewinds the chat"));
        } else if (id == QStringLiteral("agent.rewindCode")) {
            pane->openRewind(QStringLiteral("code"));
            hint(QStringLiteral("rewind.code.mouse"), QStringLiteral("Tip: /rewind-code in the prompt box restores the agent's file changes"));
        }
        else if (id == QStringLiteral("agent.fork")) pane->requestFork();
        // The key (Alt+I), the ⓘ button, the Actions pane and /status all land here. Nothing is
        // taught from here: this is also the keyboard path, and the two slow paths teach the key
        // themselves (the button below in syncChrome, the Actions pane through runFromSettings).
        else if (id == QStringLiteral("agent.info")) {
            // The focused info view can belong to a different terminal from the last active
            // one. Toggle that view's owner, while explicit action targets keep their owner.
            if (!target)
                if (auto *info = dynamic_cast<ToolPane *>(m_activeLeaf.data());
                    info && info->kind() == ToolPane::Kind::Info)
                    if (auto *owner = dynamic_cast<Pane *>(info->property("infoOwner").value<QObject *>()))
                        pane = owner;
            if (ToolPane *info = infoPaneOf(pane)) {
                closePane(info, false);
                setActiveLeaf(pane);
                focusLeaf(pane);
            } else pane->openInfo();
        }
        else if (id == QStringLiteral("find.inView")) pane->openFindInView();
        else if (id == QStringLiteral("agent.recap")) pane->requestRecap();
        else if (id == QStringLiteral("agent.requests")) pane->toggleRequests();
        else if (id == QStringLiteral("agent.thinkingPanel")) pane->toggleThinkingPanel();
        else if (id == QStringLiteral("agent.internalsPane")) openInternalsPane(pane);
        else if (id == QStringLiteral("agent.continue")) pane->continueTurn(Keymap::instance().shortcutText(id).isEmpty());
        else if (id == QStringLiteral("agent.instructions")) pane->openInstructions();
        else if (id == QStringLiteral("agent.export")) pane->exportConversation();
        else if (id == QStringLiteral("agent.screenshotPane")) pane->screenshotPane();   // image context
        else if (id == QStringLiteral("agent.interrupt")) pane->interruptAgentWithPrompt();
        else if (id == QStringLiteral("agent.clearQueue")) pane->clearAgentQueue();
        else if (id == QStringLiteral("agent.resumeQueue")) pane->resumeAgentQueue();
        else if (id == QStringLiteral("terminal.interrupt")) pane->forceInterruptShell();   // #234Z: exits a remote session
        else if (id == QStringLiteral("agent.newChat")) pane->newChat();
        else if (id == QStringLiteral("agent.stop")) pane->stopAgent();
        else if (id == QStringLiteral("agent.stopAllSubagents")) pane->stopAllSubagents();   // subagents UI
        else if (id == QStringLiteral("agent.agentsMenu")) openAgentsMenu();
        else if (id == QStringLiteral("voice.toggle")) pane->toggleVoice(true);
        else if (id == QStringLiteral("speech.readAloud")) pane->toggleReadAloud();
        else if (id == QStringLiteral("pane.share")) shareThisPane(pane);
        else if (id == QStringLiteral("pane.sharing")) openSharingPane(pane, true);
        else if (id == QStringLiteral("agent.modelKeys")) {
            pane->openKeysDialog();
            hint(QStringLiteral("model.keys.slow"),
                 relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(QStringLiteral("app.settings")),
                                                QStringLiteral("settings, including keys and model roles")));
        }
        // "per-job models": the models pane's **jobs** tab (card #MDL1, design 5.9). It was a
        // modal of its own until 2026-09-21; every door into model picking is the one pane now.
        else if (id == QStringLiteral("agent.modelRoles")) {
            pane->openModelPicker(relay::ModelsPane::jobsTab());
            hint(QStringLiteral("model.roles.slow"),
                 relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(QStringLiteral("agent.modelOptions")),
                                                QStringLiteral("the models pane, whose fourth tab this is")));
        }
        // Settings › Local models › "Set up a model with the agent…" (card #24XJ). The agent pane
        // comes to the front and is handed one prompt; the skill it names does the rest.
        else if (id == QStringLiteral("agent.localModelSetup")) {
            setActive(pane);
            focusLeaf(pane);
            pane->askAgent(QStringLiteral("Load the local-model-setup skill and set up a local model on this machine for me."),
                           QStringLiteral("Settings · Local models"));
        }
        else if (id == QStringLiteral("input.modeAuto")) pane->setMode(QStringLiteral("auto"));
        else if (id == QStringLiteral("input.modeTerminal")) pane->setMode(QStringLiteral("shell"));
        else if (id == QStringLiteral("input.modeAgent")) pane->setMode(QStringLiteral("agent"));
        else if (id == QStringLiteral("input.modeProgram")) pane->setMode(QStringLiteral("program"));
        // The Model and Reasoning-effort submenus' children, `model:<id>` and `effort:<level>`.
        // Their palette rows run a closure over `m_active` (rootItems()), which is the focused
        // pane and nothing else; these two branches are how the same two setters are reached with
        // a pane named — "put *this* pane on the local model" (#AG7R groups 2 and 3). Matched by
        // prefix, as `appcommands::isOnePickedModel()` does, because the id is a stored preset and
        // the level is whatever the provider offers: there is no fixed set to compare against.
        // A key nothing stored answers to never gets here — it is not in the catalog, so the
        // executor refuses it `unknown_action` first.
        else if (id.startsWith(QStringLiteral("model:")) && id.size() > 6) pane->selectModel(id.mid(6));
        else if (id.startsWith(QStringLiteral("effort:")) && id.size() > 7) pane->setEffort(id.mid(7));
    }

ToolPane *RelayWindow::createSessionsPane(const QString &cwd) {
        auto *view = new relay::conversations::SessionManager;
        auto *tool = new ToolPane(ToolPane::Kind::Sessions, view, view, cwd);
        tool->setProperty("paneType", QStringLiteral("sessions"));
        relay::theme::polishWindow(tool);
        auto *projects = new relay::projects::ProjectsPane;
        projects->setObjectName(QStringLiteral("workspaceProjects"));
        auto *globals = new relay::globals::GlobalsPane;
        globals->setObjectName(QStringLiteral("workspaceGlobals"));
        view->addTab(QStringLiteral("projects"), QStringLiteral("Projects"), projects);
        view->addTab(QStringLiteral("globals"), QStringLiteral("Globals"), globals);
        QPointer<ToolPane> toolGuard(tool);
        QPointer<relay::globals::GlobalsPane> globalGuard(globals);
        globals->onRequest = [toolGuard, globalGuard](const QJsonObject &request) {
            auto *window = windowOf(toolGuard);
            if (!window || !globalGuard) return;
            QWidget *currentPage = window->pageOf(toolGuard);
            // A leaf can move to another tab/window. Rebind its worker subscription before
            // each request, dropping the former one so only this page receives the reply.
            for (QWidget *top : QApplication::topLevelWidgets())
                if (auto *w = dynamic_cast<RelayWindow *>(top))
                    for (int i = w->m_helperListeners.size() - 1; i >= 0; --i)
                        if (w->m_helperListeners.at(i).owner == globalGuard)
                            w->m_helperListeners.removeAt(i);
            window->listenToHelper(currentPage, globalGuard, [globalGuard](const QJsonObject &event) {
                if (globalGuard) globalGuard->handleEvent(event);
            });
            QJsonObject currentRequest = request;
            const QString project = window->boardWorkspaceOfTab(currentPage);
            Pane *currentOwner = window->workspaceOwner(toolGuard);
            currentRequest.insert(QStringLiteral("workspace"), project.isEmpty() && currentOwner
                ? currentOwner->workspace() : project);
            window->sendToHelper(currentPage, currentRequest);
        };
        QPointer<relay::projects::ProjectsPane> projectGuard(projects);
        QPointer<relay::conversations::SessionManager> sessionsGuard(view);
        auto *refresh = new QTimer(projects);
        refresh->setInterval(2000);
        connect(refresh, &QTimer::timeout, projects, [toolGuard, projectGuard, sessionsGuard] {
            auto *window = windowOf(toolGuard);
            if (window && projectGuard && sessionsGuard && projectGuard->isVisible())
                window->feedProjects(projectGuard, sessionsGuard);
        });
        refresh->start();
        for (const SessionsTab &extra : sessionsTabs())
            if (QWidget *widget = extra.make ? extra.make(this) : nullptr) view->addTab(extra.id, extra.label, widget);
        // The agent console at the foot of the pane (#AGNT step 5, replacing #FEJQ's panel):
        // the window makes it on first expand, the row stays this pane's. A `session:` link is
        // the manager's own business; an `option:` one is the console's, which opens Options
        // on the row exactly as a terminal pane's transcript does.
        wireConsoleHost(view, tool, QStringLiteral("sessions.ask"));
        globals->onInterview = [globalGuard, sessionsGuard] {
            if (!globalGuard || !sessionsGuard) return;
            sessionsGuard->focusHelper();
            if (auto *console = dynamic_cast<Pane *>(sessionsGuard->agentConsole().widget)) {
                // createAgentConsole attaches to the tab on the next event-loop turn.
                // Submit after that attachment, including on the first interview click.
                QTimer::singleShot(0, console, [console] {
                    // The ordinary agent queue preserves an existing composer draft.
                    console->askAgent(QStringLiteral(
                        "Interview me to help Relay learn useful things about me. Review my saved user memories first, "
                        "then ask one Relay-relevant question at a time, starting with my work and goals. "
                        "Let me skip or stop. Before saving, show the proposed facts and let me choose what to remember "
                        "in Globals > User memory. Do not save guesses or secrets."));
                });
                if (sessionsGuard->onHelperHint) sessionsGuard->onHelperHint();
            }
        };
        // What is open and what was closed is the window's knowledge, not the list's: it is pushed
        // in here, and again whenever the recently-closed list changes, so the "open" and
        // "closed 5 min ago" tags on the rows stay true (card #R6J0).
        QPointer<RelayWindow> windowGuard(this);
        auto feed = [sessionsGuard, windowGuard] {
            if (!sessionsGuard || !windowGuard) return;
            sessionsGuard->setOpenSessions(windowGuard->openSessionIds());
            QHash<QString, QPair<QString, qint64>> closed;
            const QList<relay::closed::Record> records = windowGuard->m_manager->closedRecords();
            for (const relay::closed::Record &record : records)
                for (const QString &sessionId : relay::closed::sessionIds(record))
                    if (!closed.contains(sessionId) || closed.value(sessionId).second < record.closedAt)
                        closed.insert(sessionId, {record.id, record.closedAt});
            sessionsGuard->setClosedSessions(closed);
        };
        m_manager->watchClosed(view, feed);
        feed();
        // The "Project" chooser (#916B): the projects Relay knows, most recently attached first.
        {
            QList<QPair<QString, QString>> known;
            for (const relay::projects::Record &record : m_manager->projects().knownProjects())
                known.append({record.name.isEmpty() ? relay::projects::nameFor(record.path) : record.name, record.path});
            view->setKnownProjects(known);
        }
        view->onReopenClosed = [windowGuard](const QString &closedId) {
            if (windowGuard) windowGuard->m_manager->restoreClosed(windowGuard, closedId);
        };
        QPointer<ToolPane> guard(tool);
        view->onClose = [guard] {
            auto *w = windowOf(guard);
            if (!w) return;
            w->closeSessionsPane(guard, w->workspaceOwner(guard));
        };
        view->onResumeHint = [windowGuard] {
            if (windowGuard) windowGuard->hint(QStringLiteral("sessions.resume"),
                relay::ShortcutHints::nextTime(QKeySequence(Qt::Key_Return).toString(QKeySequence::NativeText),
                                               QStringLiteral("resume selected session")));
        };
        view->onPreviewHint = [windowGuard] {
            if (windowGuard) windowGuard->hint(QStringLiteral("sessions.preview"),
                relay::ShortcutHints::nextTime(QStringLiteral("P"), QStringLiteral("preview selected session")));
        };
        auto refreshProjects = [guard, projectGuard, sessionsGuard] {
            auto *w = windowOf(guard);
            if (w && projectGuard && sessionsGuard) w->feedProjects(projectGuard, sessionsGuard);
        };
        projects->onOpenProject = [guard](const QString &path) { auto *w = windowOf(guard); if (w) w->openProjectTab(path); };
        projects->onAttachProject = [guard, refreshProjects](const QString &path) {
            auto *w = windowOf(guard);
            if (!w) return;
            if (!QFileInfo(path).isDir()) { w->notice(QStringLiteral("That project folder is no longer available.")); return; }
            w->attachTab(w->pageOf(guard), path, QString::fromLatin1(relay::projects::kReasonPicker));
            refreshProjects();
            const QString card = guard->property("pendingProjectCard").toString();
            if (!card.isEmpty()) if (auto *owner = w->workspaceOwner(guard)) {
                guard->setProperty("pendingProjectCard", QVariant());
                owner->fileCard(card);
            }
        };
        projects->onOpenBoard = [guard](const QString &path) { auto *w = windowOf(guard); if (w) w->openProjectTab(path, true); };
        projects->onShowSessions = [sessionsGuard](const QString &path) {
            if (sessionsGuard) { sessionsGuard->selectProject(path); sessionsGuard->showTab(QStringLiteral("sessions")); sessionsGuard->focusSearch(); }
        };
        projects->onForget = [guard, refreshProjects](const QString &path) {
            auto *w = windowOf(guard);
            if (!w) return;
            QString error;
            if (!w->m_manager->projects().forget(path, &error)) w->notice(error);
            else w->notice(QStringLiteral("Forgot the project; its files and open tabs are unchanged."));
            refreshProjects();
        };
        projects->onUndecline = [guard, refreshProjects](const QString &path) {
            auto *w = windowOf(guard);
            if (!w) return;
            QString error;
            if (!w->m_manager->projects().undecline(path, &error)) w->notice(error);
            refreshProjects();
        };
        projects->onResume = [sessionsGuard](const QJsonObject &row) {
            for (QWidget *top : QApplication::topLevelWidgets())
                if (auto *window = dynamic_cast<RelayWindow *>(top))
                    if (Pane *pane = window->findPaneByToken(row.value(QStringLiteral("pane_token")).toString())) {
                        window->revealPane(pane); return;
                    }
            if (sessionsGuard && sessionsGuard->onResume && !row.value(QStringLiteral("session_id")).toString().isEmpty())
                sessionsGuard->onResume(row, false, false);
        };
        projects->onBrowse = [guard] {
            auto *w = windowOf(guard);
            if (!w) return;
            const QString folder = QFileDialog::getExistingDirectory(w, QStringLiteral("Open project"));
            if (!folder.isEmpty()) w->openProjectTab(folder);
        };
        projects->onInit = [guard] {
            if (auto *w = windowOf(guard)) {
                if (auto *owner = w->workspaceOwner(guard)) {
                    const QString card = guard->property("pendingProjectCard").toString();
                    guard->setProperty("pendingProjectCard", QVariant());
                    owner->initProjectHere(card);
                }
                else w->notice(QStringLiteral("Open a terminal in the folder you want to initialize."));
            }
        };
        view->onTabScreen = [projectGuard, globalGuard](const QString &id) {
            if (id == QStringLiteral("projects") && projectGuard) return projectGuard->agentScreen();
            if (id == QStringLiteral("globals") && globalGuard) return globalGuard->agentScreen();
            return QString();
        };
        view->onTabActivated = [guard, refreshProjects, globalGuard](const QString &id) {
            auto *w = windowOf(guard);
            if (!w) return;
            w->m_sessionsTab = id;
            if (id == QStringLiteral("projects")) refreshProjects();
            if (id == QStringLiteral("globals") && globalGuard) {
                Pane *owner = w->workspaceOwner(guard);
                const QString workspace = w->boardWorkspaceOfTab(w->pageOf(guard));
                globalGuard->setWorkspace(workspace.isEmpty() && owner ? owner->workspace() : workspace);
                globalGuard->refresh();
            }
            if (id == QStringLiteral("sessions") && (!w->workspaceOwner(guard)
                || guard->property("workspaceOwner").value<QObject *>() != w->workspaceOwner(guard)))
                QTimer::singleShot(0, guard, [guard] {
                    if (auto *current = windowOf(guard)) current->openSessions(QStringLiteral("sessions"));
                });
            w->updateTitles();
        };
        view->onTabSelectedByUser = [guard](const QString &id) {
            auto *w = windowOf(guard);
            if (!w) return;
            const QString action = id == QStringLiteral("projects") ? QStringLiteral("projects.open")
                : id == QStringLiteral("globals") ? QStringLiteral("globals.open")
                : QStringLiteral("sessions.open");
            w->hint(QStringLiteral("workspace.tab.") + id,
                relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(action)));
        };
        refreshProjects();
        return tool;
    }

relay::BoardWorker *RelayWindow::boardWorker(QWidget *page) {
        if (!page) return nullptr;
        const QString tab = tabIdOf(page);
        if (relay::BoardWorker *existing = m_boardWorkers.value(tab).data()) return existing;
        auto *worker = new relay::BoardWorker(
            relayPython(), dataRoot(), this);
        m_boardWorkers.insert(tab, worker);
        QPointer<RelayWindow> guard(this);
        // Its events reach the views of its own tab and no others. A second tab on the same
        // project has its own worker and its own conversation, and neither redraws the other.
        worker->onEvent = [guard, tab](const QJsonObject &event) {
            if (!guard) return;
            // A worker names its events in `event`, never in `type` — `type` is what the *GUI*
            // calls the messages it sends down (BoardWorker::handleLine, Pane's own reader). This
            // read said `type` from the day the helper route was written (c78c8004), so it matched
            // nothing: every `app_command` the helper sent sat unanswered until its 20-second
            // deadline, and every `presets`/`key_tested`/`configured` below was dropped. The
            // owner's report is what it looks like from the outside — "sessions helper didn't do
            // anything when I asked to open a group of previous sessions in new panes"
            // (2026-09-20): the helper called app_open, nothing answered, and the turn ended with
            // the call still hanging when the tab it was in closed.
            const QString type = event.value(QStringLiteral("event")).toString();
            // An `app_command` out of the *helper* (§30.3). A pane's worker is answered in
            // src/Pane.h; this is the other pipe, and until it was here every write the helper
            // attempted — app_option_set, app_action_run, app_open, app_undo — waited out the
            // 20-second deadline and came back `no_reply`, with nothing on screen to say why.
            // The answer goes back down this worker's own pipe: there is no routing field on the
            // wire. `who` is "helper", which is what the change log and the notification say.
            if (type == QStringLiteral("app_command")) {
                relay::BoardWorker *worker = guard->m_boardWorkers.value(tab).data();
                if (!worker) return;
                QPointer<RelayWindow> window(guard);
                worker->send(relay::appcommands::answerFor(event, [window](const QJsonObject &command) {
                    return window ? window->executeAppCommand(command, QStringLiteral("helper"))
                                  : QJsonObject{};
                }));
                return;
            }
            // Which panels of this tab are waiting on the helper right now (#H6VQ). A worker that
            // dies mid-turn tells nobody: `done` never comes, so the panel that asked keeps its
            // busy strip and its composer refuses a second prompt for ever. This is the list that
            // gets put back — the pane a turn started in, and any pane with a prompt queued
            // behind it.
            // Since card #AGNT the wire says this in one word. The two chat-era events that fed
            // this list are retired; a console's turn is an ordinary pane turn, and `surface`
            // rides on `queued`, on `agent_started` and on `agent_finished` (protocol 33). A
            // finish with no surface is a worker that answered nothing in particular, and clears
            // the list rather than leaving a name in it for ever.
            if (type == QStringLiteral("queued") || type == QStringLiteral("agent_started")) {
                if (const QString surface = event.value(QStringLiteral("surface")).toString(); !surface.isEmpty())
                    guard->m_helperWaiting[tab].insert(surface);
            } else if (type == QStringLiteral("agent_finished")) {
                const QString surface = event.value(QStringLiteral("surface")).toString();
                if (surface.isEmpty()) guard->m_helperWaiting[tab].clear();
                else guard->m_helperWaiting[tab].remove(surface);
            }
            // The handshake, kept for a console made after this worker was already up: it is said
            // once per configure and would otherwise never reach one (attachConsoleToTab).
            if (type == QStringLiteral("ready") || type == QStringLiteral("configured"))
                guard->m_workerHandshake.insert(handshakeKey(tab, type), event);
            // Options › Models on a tab with no pane agent reads the helper's presets, and its key,
            // test and custom-provider requests come back down this pipe (owner, 2026-09-20).
            if (type == QStringLiteral("presets")) {
                guard->m_helperPresets[tab] = event.value(QStringLiteral("presets")).toArray();
                relay::SettingsWatch::instance().notify();
            } else if (type == QStringLiteral("warp_imported") || type == QStringLiteral("opencode_imported")
                       || type == QStringLiteral("agent_tools_imported")) {
                const int count = event.value(QStringLiteral("imported")).toArray().size();
                const int skipped = event.value(QStringLiteral("skipped")).toArray().size();
                QMessageBox::information(guard, QStringLiteral("Import keys"),
                    QStringLiteral("Imported %1 API key(s). Skipped %2.").arg(count).arg(skipped));
                if (relay::BoardWorker *worker = guard->m_boardWorkers.value(tab).data()) worker->send({{"type", "presets"}});
            } else if (type == QStringLiteral("error")
                       && event.value(QStringLiteral("id")).toString() == QStringLiteral("models-import")) {
                QMessageBox::warning(guard, QStringLiteral("Import keys"), event.value(QStringLiteral("text")).toString());
            } else if (type == QStringLiteral("guest_account_saved") || type == QStringLiteral("guest_account_deleted")) {
                // Options › Models asked through the tab's helper (#M8S2): the rows come back with
                // a fresh `presets`, and a sign-in goes to the active pane's terminal when there is one.
                if (relay::BoardWorker *worker = guard->m_boardWorkers.value(tab).data()) worker->send({{"type", "presets"}});
                const QString login = event.value(QStringLiteral("account")).toObject().value(QStringLiteral("login_command")).toString();
                if (type == QStringLiteral("guest_account_saved") && event.value(QStringLiteral("sign_in")).toBool() && !login.isEmpty()) {
                    if (guard->m_active) guard->m_active->runLoginCommand(login);
                    else QMessageBox::information(guard, QStringLiteral("Models"),
                             QStringLiteral("Account added. Sign it in from a terminal pane:\n%1").arg(login));
                }
            } else if (type == QStringLiteral("error") && event.value(QStringLiteral("id")).toString() == QStringLiteral("guest-account")) {
                QMessageBox::warning(guard, QStringLiteral("Models"), event.value(QStringLiteral("text")).toString());
            } else if (type == QStringLiteral("key_stored") || type == QStringLiteral("key_removed")
                       || type == QStringLiteral("custom_provider_saved") || type == QStringLiteral("custom_provider_deleted")) {
                if (relay::BoardWorker *worker = guard->m_boardWorkers.value(tab).data()) worker->send({{"type", "presets"}});
                if (const QString error = event.value(QStringLiteral("error")).toString(); !error.isEmpty() && !guard->m_active)
                    QMessageBox::warning(guard, QStringLiteral("Models"), error);
            } else if (type == QStringLiteral("key_tested") && !guard->m_active) {
                // No pane to carry the status line: the answer is said where the button was.
                const QString preset = event.value(QStringLiteral("preset")).toString();
                // The model that answered, by name (card #MDL1, rule 1).
                const QString answered = relay::models::nameOf(event.value(QStringLiteral("model")).toString());
                QMessageBox::information(guard, QStringLiteral("Models"), event.value(QStringLiteral("ok")).toBool()
                    ? QStringLiteral("%1 answered%2.").arg(preset, answered.isEmpty()
                          ? QString() : QStringLiteral(" on ") + answered)
                    : QStringLiteral("%1: %2").arg(preset, event.value(QStringLiteral("error")).toString()));
            }
            QWidget *page = guard->pageOfTabId(tab);
            if (!page) return;
            for (QWidget *leaf : leavesIn(page))
                if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->board())
                    tool->board()->handleEvent(event);
            guard->deliverToHelperPanels(page, event);
            guard->deliverToConsoles(tab, event);
        };
        // The process has gone and nobody asked it to: every panel of this tab that was waiting
        // on it is put back to idle (#H6VQ, §30.7). Nothing else says so — `done` was the
        // worker's to send — and the next ask starts a fresh one through the first-ask path, so
        // the composer must be usable again the moment this returns.
        worker->onExit = [guard, tab](bool crashed) {
            if (guard) guard->helperWorkerGone(tab, crashed);
        };
        // What the worker says about *itself* — it would not start, it exited, its pipe
        // overflowed. The status bar is not shown in this layout, so until 2026-09-20 a
        // Board whose worker died sat on "Loading the Board…" for ever with the
        // explanation written somewhere nobody can see (#7M6E). It goes to the tab's board panes
        // as well, which put it where the loading line was, with a Retry.
        worker->onStatus = [guard, tab](const QString &text) {
            if (!guard) return;
            guard->statusBar()->showMessage(text, 9000);
            QWidget *page = guard->pageOfTabId(tab);
            if (!page) return;
            const QJsonObject status{{QStringLiteral("event"), QStringLiteral("board_worker_status")},
                                     {QStringLiteral("text"), text}};
            for (QWidget *leaf : leavesIn(page))
                if (auto *tool = dynamic_cast<ToolPane *>(leaf); tool && tool->board())
                    tool->board()->handleEvent(status);
        };
        // The owner's devices see this tab's board through the same worker (#SWPH): the bridge
        // taps `onEvent` — the handler above runs first and unchanged — and never replaces it.
        relay::BoardRemote::instance().tap(worker, this, tab);
        return worker;
    }

Pane *RelayWindow::createPane(const QJsonObject &spec) {
        // A directory that no longer exists falls back to the pane's workspace, then to this
        // window's workspace, then to $HOME (src/WindowState.h).
        const QString fallback = relay::windowstate::resolveDirectory(m_manager->workspace(), QString(), QDir::homePath());
        const QString workspace = relay::windowstate::resolveDirectory(spec.value(QStringLiteral("workspace")).toString(), fallback, fallback);
        const QString cwd = relay::windowstate::resolveDirectory(spec.value(QStringLiteral("cwd")).toString(), workspace, workspace);
        // The emulator core: the restored session's, else the process default from
        // --engine-core / RELAY_ENGINE_CORE. A session saved before KonsolePart was retired may
        // still carry an "engine" key; it is ignored, and every pane gets Relay's engine.
        const QString core = spec.value(QStringLiteral("engine_core")).toString(relay::defaultEngineCore());
        // The saved spec rides along (#87HB): a pane restoring a local_login line has to know it
        // before its shell starts, and the constructor runs startTerminal before initRestore.
        auto *pane = new Pane(workspace, cwd, m_manager->cleanShell(), core, nullptr, spec);
        // Model roles (protocol 13): panes opened after the first one default to the Flash agent.
        // A layout saved before 2026-09-18 calls that role "fast"; it is read as "flash" and saved
        // back under the new name (Pane::canonicalRole).
        const QString savedRole = Pane::canonicalRole(spec.value(QStringLiteral("agent_role")).toString());
        if (!savedRole.isEmpty()) pane->initAgentRole(savedRole);
        else if (Pane::newPanesUseFlashAgent() && !allPanes().isEmpty()) pane->initAgentRole(QStringLiteral("flash"));
        // Saved window layout: model/effort/mode and the conversation to reattach.
        pane->initRestore(spec);
        QPointer<Pane> guard(pane);
        // Callbacks find the pane's current window, so panes and tabs can move between windows.
        pane->onStatus = [guard](const QString &text) { if (guard) guard->toast(text, 5000); };
        pane->onStateChanged = [guard] {
            auto *w = windowOf(guard);
            if (!w) return;
            w->updateTitles();
            w->m_manager->scheduleSave();
        };
        // `exit` (or the shell dying) closes the pane the way × does, so it can be reopened too.
        pane->onShellExited = [guard] { if (auto *w = windowOf(guard)) w->closePane(guard, true); };
        pane->onOpenPath = [guard](const QString &path, int line) { if (auto *w = windowOf(guard)) w->openPath(path, line, guard); };
        pane->onEditPath = [guard](const QString &path, int line) { if (auto *w = windowOf(guard)) w->openPath(path, line, guard, false, true); };
        pane->onToggleExplorer = [guard](const QString &path) { if (auto *w = windowOf(guard)) { w->setActiveLeaf(guard); w->toggleExplorer(path, guard); } };
        pane->onOpenBoard = [guard] { if (auto *w = windowOf(guard)) { w->setActiveLeaf(guard); w->toggleBoardPane(); } };
        pane->onChooseTheme = [guard](const QString &id) {
            // A theme command also becomes the new-tab default, unless Options says otherwise.
            if (auto *w = windowOf(guard)) {
                const bool asDefault = themeCommandsSetDefault();
                const bool ok = w->chooseTheme(id, w->pageOf(guard), asDefault);
                if (ok && asDefault) w->refreshSettingsPanes();   // an open Options pane shows the new default
                return ok;
            }
            return relay::theme::setActiveTheme(id);
        };
        // The `board` block of every `configure` this pane sends (protocol 19.1). It is read at
        // the moment the configure is built, so a pane that is re-created or switches model comes
        // back on the board its tab is attached to — or, while the tab is attached to nothing, on
        // none at all.
        pane->onBoardSettings = [guard]() -> QJsonObject {
            auto *w = windowOf(guard);
            return w ? w->boardSettingsFor(w->pageOf(guard)) : QJsonObject{{QStringLiteral("attach"), false}};
        };
        // "Has this pane a board it may talk to?" A non-empty `reason` (projects::kReason*) makes
        // it an explicit project action: the tab attaches to the pane's candidate project first,
        // but only when that project already has a board — a candidate with none is answered with
        // one line and nothing is created. An empty reason attaches nothing and answers from the
        // tab alone, so the `#` index, the idle tip and the shortcut hints stay quiet in a pane
        // whose tab is attached to nothing.
        pane->onBoardProject = [guard](const QString &reason, QString *why) -> QString {
            auto *w = windowOf(guard);
            if (!w) return {};
            QWidget *page = w->pageOf(guard);
            if (const QString attached = w->tabProject(page); !attached.isEmpty()) return attached;
            if (reason.isEmpty()) return {};
            const QString candidate = relay::projects::candidateFor(guard->cwd());
            if (candidate.isEmpty() || relay::projects::boardDirOf(candidate).isEmpty()) {
                if (why)
                    *why = candidate.isEmpty()
                               ? QStringLiteral("No Board here: no project above %1 has one.").arg(guard->cwd())
                               : QStringLiteral("%1 has no Board yet.").arg(relay::projects::nameFor(candidate));
                return {};
            }
            w->attachTab(page, candidate, reason);
            return w->tabProject(page);
        };
        // ----- the init question (protocol 19.12): facts in, the pane decides ------------------
        // The rules are in src/ProjectInit.h and the pixels are in the pane; the window answers
        // only what it knows. An attached tab reports its own project rather than the pane's
        // candidate, so a pane that has `cd`-ed into another checkout never raises a question
        // whose yes would silently re-point the tab (#JN7X's rule, unchanged).
        pane->onProjectInitSituation = [guard]() -> relay::projectinit::Situation {
            relay::projectinit::Situation situation;
            auto *w = windowOf(guard);
            if (!w) return situation;
            const QString attached = w->tabProject(w->pageOf(guard));
            situation.project = attached.isEmpty() ? relay::projects::candidateFor(guard->cwd()) : attached;
            situation.cwd = guard->cwd();
            if (situation.project.isEmpty()) return situation;
            situation.hasBoard = !relay::projects::boardDirOf(situation.project).isEmpty();
            situation.declined = w->m_manager->projects().isDeclined(situation.project);
            situation.snoozed = w->m_manager->initSnoozed().contains(relay::projects::normalize(situation.project));
            // A Pane is always the desktop's own; a guest sees somebody else's through
            // relay::RemotePane, which has no worker of its own and never draws this question. The
            // flag is answered all the same so the rule is stated where the question is raised.
            situation.remote = false;
            return situation;
        };
        pane->onProjectAttach = [guard](const QString &project, const QString &reason) {
            if (auto *w = windowOf(guard)) w->attachTab(w->pageOf(guard), project, reason);
        };
        pane->onPickProject = [guard](const QString &heldCard) {
            if (auto *w = windowOf(guard)) { w->setActiveLeaf(guard); w->openProjectsFor(guard, heldCard); }
        };
        pane->onProjectInitEvent = [guard](const QString &project, const QString &what) {
            auto *w = windowOf(guard);
            if (!w || project.isEmpty()) return;
            // "Not now" is not a no: it silences trigger (1) — the agent prompt — for the rest of
            // this Relay session, and every explicit act asks again. Raising the question counts
            // the same way, so one project is asked about once per session however many panes
            // stand in it. A no is written to disk instead and nothing asks again, in any tab,
            // after any restart; `/init` is the one thing that clears it.
            if (what != QStringLiteral("reconsider"))
                w->m_manager->initSnoozed().insert(relay::projects::normalize(project));
            if (what == QStringLiteral("no")) w->m_manager->projects().decline(project);
            else if (what == QStringLiteral("reconsider")) w->m_manager->projects().undecline(project);
        };
        pane->onJoinShared = [guard](const QString &code) { if (auto *w = windowOf(guard)) w->joinSharedSession(code); };
        pane->onUpdateApp = [guard]() { if (auto *w = windowOf(guard)) w->updateApp(); };
        pane->onRestartApp = [guard]() { if (auto *w = windowOf(guard)) w->restartApp(); };
        pane->onOpenCard = [guard](const QString &id) { if (auto *w = windowOf(guard)) { w->setActiveLeaf(guard); w->openBoardCard(id); } };
        // The agent drives the app (#FEJQ, §30): the catalog this pane's worker is configured with,
        // and one `app_command` out of it, executed in the window the pane is in.
        pane->onAppCatalog = [guard]() -> QJsonObject {
            auto *w = windowOf(guard);
            return w ? w->appCatalogFor(w->pageOf(guard)) : QJsonObject{};
        };
        pane->onAppCommand = [guard](const QJsonObject &command) -> QJsonObject {
            auto *w = windowOf(guard);
            // `who` is the pane's session token: the change log says which agent did it, and the
            // notification reads the same either way.
            return w ? w->executeAppCommand(command, guard->sessionToken()) : QJsonObject{};
        };
        // The loop guard's reset (#AG7R group 8, §30.3): a prompt the person typed in this pane
        // ends the chain of agent-to-agent prompts that reached it, and lets its agent send again.
        pane->onPersonPrompt = [guard] {
            if (auto *w = windowOf(guard)) w->appCommands().notePersonPrompt(guard->sessionToken());
        };
        // Right-click menu entries the window owns (issue #X2F1).
        pane->onWindowAction = [guard](const QString &action) {
            auto *w = windowOf(guard);
            if (!w) return;
            w->setActiveLeaf(guard);
            if (action == QStringLiteral("splitRight")) w->runAction(QStringLiteral("pane.splitRight"));
            else if (action == QStringLiteral("splitDown")) w->runAction(QStringLiteral("pane.splitDown"));
            else if (action == QStringLiteral("splitSameHost")) w->runAction(QStringLiteral("ssh.splitSameHost"));
            else if (action == QStringLiteral("splitLocal")) w->runAction(QStringLiteral("pane.splitLocal"));
            else if (action == QStringLiteral("equalize")) w->runAction(QStringLiteral("pane.equalize"));
            else if (action == QStringLiteral("runBackground")) w->runAction(QStringLiteral("pane.runInBackground"));
            else if (action == QStringLiteral("close")) w->closePane(guard, true);
            else if (action == QStringLiteral("closeEndRemote")) w->runAction(QStringLiteral("pane.closeEndRemote"));
        };
        // Grey the menu's "Equalize pane sizes" while this pane is alone in its tab: any other
        // leaf counts (an explorer beside it splits the tab just the same).
        pane->hasPaneSiblings = [guard]() -> bool {
            auto *w = windowOf(guard);
            return w && w->leavesIn(w->pageOf(guard)).size() > 1;
        };
        pane->onPlanWritten = [guard](const QString &path, Pane *) { if (auto *w = windowOf(guard)) w->openDocument(path, guard, true); };
        pane->onOpenDocument = [guard](const QString &path) { if (auto *w = windowOf(guard)) w->openDocument(path, guard, false); };
        pane->onForkState = [guard](const QJsonObject &state, const QString &title) { if (auto *w = windowOf(guard)) w->openFork(guard, state, title); };
        pane->onOpenSessionInNewPane = [guard](const QJsonObject &state, const QString &title) { if (auto *w = windowOf(guard)) w->openFork(guard, state, title, false); };
        pane->onOpenSessionRow = [guard](const QJsonObject &item) {
            if (guard) guard->openSavedSession(item, true);
        };
        // A guest session resumed in a new pane launches through that pane's own launch path (26.9):
        // the guest id and its arguments travel, not a finished command line.
        pane->onOpenGuestPane = [guard](const QString &guest, const QStringList &extra, const QString &cwd, const QString &preset) {
            if (auto *w = windowOf(guard)) w->openGuestPane(guard, guest, extra, cwd, preset);
        };
        pane->onOpenSubagent = [guard](const QString &id) { if (auto *w = windowOf(guard)) w->openSubagentTab(guard, id); };   // subagents UI (#WD83)
        pane->onShowAgents = [guard] { if (auto *w = windowOf(guard)) { w->setActiveLeaf(guard); w->openAgentsMenu(); } };   // /agents → subagents panel menu
        pane->onOpenTurn = [guard](const QString &turnId) { if (auto *w = windowOf(guard)) w->openTurnPane(guard, turnId); };
        pane->onOpenInternals = [guard] { if (auto *w = windowOf(guard)) w->openInternalsPane(guard); };
        // The share chip, once this pane is shared: who is here and what is waiting (#W5N2).
        pane->onOpenSharing = [guard] { if (auto *w = windowOf(guard)) w->openSharingPane(guard, true); };
        // Its two rows (#SMDX): the invite form on this pane, or with the scope picker open.
        pane->onShareThisPane = [guard] { if (auto *w = windowOf(guard)) w->shareThisPane(guard); };
        pane->onShareMore = [guard] { if (auto *w = windowOf(guard)) w->shareMore(guard); };
        pane->onOpenOptions = [guard](const QString &tab) {
            if (auto *w = windowOf(guard)) w->openSettingsPane(relay::SettingsPane::Mode::Options, tab);
        };
        // The models pane beside this one, serving this one (#MDL1 t:a11).
        pane->onOpenModelsPane = [guard](const relay::ModelsPane::Target &target, const QString &tab, const QString &filter) {
            if (auto *w = windowOf(guard)) w->openModelsPaneFor(guard, target, tab, filter);
        };
        // `/profile` swapped the five lists: the same two steps a switch on Options › Models takes.
        pane->onProfileApplied = [guard] {
            if (auto *w = windowOf(guard)) w->modelsCurated();
        };
        // The worker resolved the roles again: an open models pane re-reads its target, so the
        // jobs tab's "runs on" column follows the report (card #MDL1, design 5.9).
        pane->onRolesResolved = [guard] {
            if (auto *w = windowOf(guard)) w->refreshModelsPaneFor(guard);
        };
        pane->onShareTab = [guard](int *panes) {
            auto *w = windowOf(guard);
            return w ? w->shareTabId(w->pageOf(guard), panes) : QString();
        };
        // A pane opened in a tab that is shared whole is shared as soon as it is in the tab.
        QTimer::singleShot(0, pane, [guard] { if (auto *w = windowOf(guard)) w->scheduleTabShareSync(); });
        // Protocol 23's four `local_*` events, and a `test_key` for a `local:` preset, belong to
        // Settings › Local models rather than to this pane's transcript (card #24XJ).
        pane->onLocalModelEvent = [guard](const QJsonObject &event) {
            if (auto *w = windowOf(guard)) w->localModels().handleEvent(event);
        };
        // A tool-call line whose diff is too big to read inline (#TK9C).
        pane->onOpenDiff = [guard](const QString &title, const QString &unifiedDiff) -> relay::DiffView * {
            if (auto *w = windowOf(guard)) return w->openDiffPane(guard, title, unifiedDiff);
            return nullptr;
        };
        // Session manager and ⓘ (cards #R6J0, #Y63Z).
        pane->onOpenSessions = [guard](const QString &query) { if (auto *w = windowOf(guard)) w->openSessionsFor(guard, query); };
        pane->onOpenMemorySuggestion = [guard](const QString &id) { if (auto *w = windowOf(guard)) w->openMemorySuggestion(id); };
        pane->onMemorySuggestionDecided = [] { refreshVisibleGlobals(); };
        // An `option:sec/row` in this pane's output (#AGNT step 8): Options on that section, zoomed
        // to the row. The same two steps `app_open {target: "options", row}` takes, and the same
        // two the helper's own answers took before the link became a kind of the transcript.
        pane->onOpenOption = [guard](const QString &section, const QString &row) {
            auto *w = windowOf(guard);
            if (!w) return;
            w->openSettingsPane(relay::SettingsPane::Mode::Options, section);
            if (ToolPane *tool = w->settingsPaneIn(w->m_tabs->currentWidget(), relay::SettingsPane::Mode::Options);
                tool && tool->settings())
                tool->settings()->revealOption(section, row);
        };
        pane->onOpenInfo = [guard] { if (auto *w = windowOf(guard)) w->openInfoPane(guard); };
        pane->onOpenThreadInfo = [guard](const QString &threadId, const QString &dir, const QString &owner) {
            if (auto *w = windowOf(guard)) w->openInfoPane(guard, QString(), dir, threadId, owner);
        };
        // The first-launch approvals choice (card #K2FV): the approvals pane beside this one,
        // raised by Pane::onSessionConfigured while security/approvals_chosen is unset.
        pane->onApprovalsChoice = [guard] { if (auto *w = windowOf(guard)) w->openApprovalsPane(guard); };
        pane->onSessionOpenElsewhere = [guard](const QString &sessionId, const QString &dir) {
            Pane *other = paneWithSession(sessionId, dir, guard);
            auto *w = other ? dynamic_cast<RelayWindow *>(other->window()) : nullptr;
            if (!w) return false;
            w->m_manager->focusPane(other->sessionToken());
            other->toast(QStringLiteral("This session was already open here"));
            return true;
        };
        // Pane titles (issue JRWQ): a fresh title refreshes the tab's tooltip (and the label of a
        // tab with no terminal pane); /rename-tab is the window's job.
        pane->onTitleChanged = [guard] { if (auto *w = windowOf(guard)) w->updateTitles(); };
        pane->onRenameTab = [guard](const QString &text, bool edit) {
            auto *w = windowOf(guard);
            if (w) w->renameTab(text, edit, w->pageOf(guard));
        };
        relay::theme::polishWindow(pane);
        return pane;
    }

void RelayWindow::wireAgentConsole(Pane *console) {
        QPointer<Pane> guard(console);
        console->onStatus = [guard](const QString &text) { if (guard) guard->toast(text, 5000); };
        // Its line goes on the tab's worker, which is started by the first thing it says.
        console->onWorkerLine = [guard](const QJsonObject &message) {
            if (auto *w = windowOf(guard)) w->sendFromConsole(guard, message);
        };
        // The links an answer can carry, resolved the way a terminal pane resolves them — the
        // context gets first refusal inside the pane, and what it declines arrives here.
        console->onOpenPath = [guard](const QString &path, int line) {
            if (auto *w = windowOf(guard)) w->openPath(path, line, hostLeafOf(guard));
        };
        console->onEditPath = [guard](const QString &path, int line) {
            if (auto *w = windowOf(guard)) w->openPath(path, line, hostLeafOf(guard), false, true);
        };
        console->onOpenCard = [guard](const QString &id) { if (auto *w = windowOf(guard)) w->openBoardCard(id); };
        console->onOpenOption = [guard](const QString &section, const QString &row) {
            auto *w = windowOf(guard);
            if (!w) return;
            w->openSettingsPane(relay::SettingsPane::Mode::Options, section);
            if (ToolPane *pane = w->settingsPaneIn(w->m_tabs->currentWidget(), relay::SettingsPane::Mode::Options);
                pane && pane->settings())
                pane->settings()->revealOption(section, row);
        };
        console->onOpenSessions = [guard](const QString &query) {
            if (auto *w = windowOf(guard)) w->openSessions(QString(), query);
        };
        console->onOpenSessionRow = [guard](const QJsonObject &item) {
            if (auto *w = windowOf(guard)) {
                if (auto *owner = w->paneForConsoleOpen(guard)) owner->openSavedSession(item, true);
            }
        };
        console->onOpenMemorySuggestion = [guard](const QString &id) { if (auto *w = windowOf(guard)) w->openMemorySuggestion(id); };
        console->onMemorySuggestionDecided = [] { refreshVisibleGlobals(); };
        console->onOpenDocument = [guard](const QString &path) {
            if (auto *w = windowOf(guard)) w->openDocument(path, w->paneForConsoleOpen(guard), false);
        };
        console->onOpenTurn = [guard](const QString &turnId) {
            if (auto *w = windowOf(guard)) w->openTurnPane(w->paneForConsoleOpen(guard), turnId);
        };
        console->onOpenInternals = [guard] {
            if (auto *w = windowOf(guard)) w->openInternalsPane(w->paneForConsoleOpen(guard));
        };
        console->onOpenDiff = [guard](const QString &title, const QString &unifiedDiff) -> relay::DiffView * {
            if (auto *w = windowOf(guard)) return w->openDiffPane(w->paneForConsoleOpen(guard), title, unifiedDiff);
            return nullptr;
        };
        console->onOpenOptions = [guard](const QString &tab) {
            if (auto *w = windowOf(guard)) w->openSettingsPane(relay::SettingsPane::Mode::Options, tab);
        };
        // A console is a pane with no shell, so its model box, /model and Ctrl+Shift+M open the
        // same models pane, serving the console itself (#MDL1 t:a11, #PK5Q).
        console->onOpenModelsPane = [guard](const relay::ModelsPane::Target &target, const QString &tab, const QString &filter) {
            if (auto *w = windowOf(guard)) w->openModelsPaneFor(guard, target, tab, filter);
        };
        console->onOpenInfo = [guard] { if (auto *w = windowOf(guard)) w->openInfoPane(w->paneForConsoleOpen(guard)); };
        console->onToggleExplorer = [guard](const QString &path) {
            if (auto *w = windowOf(guard)) w->toggleExplorer(path, hostLeafOf(guard));
        };
        console->onShowAgents = [guard] { if (auto *w = windowOf(guard)) w->openAgentsMenu(); };
        console->onOpenSubagent = [guard](const QString &id) {
            if (auto *w = windowOf(guard)) w->openSubagentTab(w->paneForConsoleOpen(guard), id);
        };
        // The app catalog of the tab it is in (protocol 30.2). The *answers* to `app_command` are
        // not this console's: one worker serves every console of the tab, and the window already
        // answers that pipe once, tagged `helper` — four consoles answering the same write would
        // run it four times. That is why `onAppCommand` is deliberately not set here, and why
        // `deliverToConsoles` drops `app_command`.
        console->onAppCatalog = [guard]() -> QJsonObject {
            auto *w = windowOf(guard);
            return w ? w->appCatalogFor(w->pageOf(guard)) : QJsonObject{};
        };
        // Protocol 23's `local_*` events belong to Options › Local models wherever they arrive.
        console->onLocalModelEvent = [guard](const QJsonObject &event) {
            if (auto *w = windowOf(guard)) w->localModels().handleEvent(event);
        };
        console->onChooseTheme = [guard](const QString &id) {
            if (auto *w = windowOf(guard)) {
                const bool asDefault = themeCommandsSetDefault();
                const bool ok = w->chooseTheme(id, w->pageOf(guard), asDefault);
                if (ok && asDefault) w->refreshSettingsPanes();
                return ok;
            }
            return relay::theme::setActiveTheme(id);
        };
        console->onProfileApplied = [guard] {
            if (auto *w = windowOf(guard)) w->modelsCurated();
        };
        console->onRolesResolved = [guard] {
            if (auto *w = windowOf(guard)) w->refreshModelsPaneFor(guard);
        };
        console->onUpdateApp = [guard]() { if (auto *w = windowOf(guard)) w->updateApp(); };
        console->onRestartApp = [guard]() { if (auto *w = windowOf(guard)) w->restartApp(); };
        console->onJoinShared = [guard](const QString &code) { if (auto *w = windowOf(guard)) w->joinSharedSession(code); };
        // The `board` block of its `configure` is the tab's, exactly as a pane's is.
        console->onBoardSettings = [guard]() -> QJsonObject {
            auto *w = windowOf(guard);
            return w ? w->boardSettingsFor(w->pageOf(guard)) : QJsonObject{{QStringLiteral("attach"), false}};
        };
        // What is **not** wired, and why: `onShellExited`, `onOpenGuestPane`, `onForkState`,
        // `onOpenSessionInNewPane`, `onSessionOpenElsewhere`, `onPlanWritten` and the project-init
        // question are a terminal pane's (there is no shell, no guest, no pane to fork into and no
        // directory the console stands in); `onTitleChanged` / `onRenameTab` / `hasPaneSiblings` /
        // `onWindowAction` / `onShareTab` / `onOpenSharing` belong to a leaf, and a console is not
        // one; and `onPersonPrompt` is a pane agent's loop guard, keyed by a pane's session token.
        relay::theme::polishWindow(console);
    }

// A leaf that was in an artifact workspace comes back with its member id, so the tab's group finds
// its editor, console and preview again (#E85D). Split nodes recurse through here too.
QWidget *RelayWindow::buildNode(const QJsonObject &node) {
    QWidget *built = buildNodeWidget(node);
    if (!node.contains(QStringLiteral("split"))) tagWorkspaceMember(built, node);
    return built;
}

QWidget *RelayWindow::buildNodeWidget(const QJsonObject &node) {
        if (node.contains(QStringLiteral("split"))) {
            auto *splitter = newSplitter(node.value(QStringLiteral("split")).toString() == QStringLiteral("v") ? Qt::Vertical : Qt::Horizontal);
            const auto children = node.value(QStringLiteral("children")).toArray();
            for (const auto &child : children) splitter->addWidget(buildNode(child.toObject()));
            QList<int> sizes;
            for (const auto &size : node.value(QStringLiteral("sizes")).toArray()) sizes.append(size.toInt());
            if (sizes.size() == splitter->count()) splitter->setSizes(sizes);
            return splitter;
        }
        if (node.contains(QStringLiteral("board"))) {
            const QJsonObject board = node.value(QStringLiteral("board")).toObject();
            const QString workspace = board.value(QStringLiteral("workspace")).toString();
            // Either spelling of the board folder (protocol 19.12): a saved Switchboard whose
            // project keeps its cards in `issues/` restores exactly like one that uses
            // `switchboard/`.
            if (!relay::projects::boardDirOf(workspace).isEmpty()) {
                ToolPane *tool = createBoardPane(workspace, board.value(QStringLiteral("collapsed")).toArray(),
                                                 board.value(QStringLiteral("hidden")).toArray(),
                                                 board.value(QStringLiteral("sort")).toString(),
                                                 board.value(QStringLiteral("labels")).toArray(),
                                                 board.value(QStringLiteral("self_closed")).toArray(),
                                                 board.value(QStringLiteral("grouping")).toString());
                // Which of the signals rows were open (#AQ6X), set on the view rather than through
                // one more argument: both are folded by default, so an older node opens nothing.
                if (relay::BoardView *board_view = tool->board()) {
                    board_view->setOpenSignals(board.value(QStringLiteral("signals")).toArray());
                    board_view->restoreNavigation(board.value(QStringLiteral("navigation")).toObject());
                }
                // A restored Switchboard attaches its tab, unless the tab already has a project —
                // the saved `project` on the tab wins, and a tab holds one. Queued, because
                // buildNode() runs before the page the pane will live in exists.
                QPointer<ToolPane> guard(tool);
                QTimer::singleShot(0, tool, [guard, workspace] {
                    auto *w = windowOf(guard);
                    if (!w) return;
                    QWidget *page = w->pageOf(guard);
                    if (page && w->tabProject(page).isEmpty())
                        w->attachTab(page, workspace, QString::fromLatin1(relay::projects::kReasonRestored));
                });
                return tool;
            }
            // The project lost its board (or moved): a terminal in the directory this pane was
            // saved in, which is the project the person was working in. It used to open in the
            // launch directory, quietly moving the pane to another checkout.
            const QString fallback = relay::windowstate::resolveDirectory(
                workspace, m_manager->workspace(), QDir::homePath());
            return createPane({{"cwd", fallback}, {"workspace", fallback}});
        }
        if (node.contains(QStringLiteral("internals"))) {   // card #QT8C: empty until the next event, beside its owner
            const QJsonObject saved = node.value(QStringLiteral("internals")).toObject();
            ToolPane *tool = createInternalsPane(saved.value(QStringLiteral("cwd")).toString());
            QPointer<ToolPane> guard(tool);
            const QString owner = saved.value(QStringLiteral("owner")).toString();
            QTimer::singleShot(0, tool, [guard, owner] { if (auto *w = windowOf(guard)) w->linkRestoredInternalsPane(guard, owner); });
            return tool;
        }
        if (node.contains(QStringLiteral("testsuites"))) {   // card #7BM4: empty until its worker answers
            const QJsonObject saved = node.value(QStringLiteral("testsuites")).toObject();
            ToolPane *tool = createTestSuitesPane(saved.value(QStringLiteral("cwd")).toString());
            QPointer<ToolPane> guard(tool);
            QTimer::singleShot(0, tool, [guard] { if (auto *w = windowOf(guard)) w->linkRestoredTestSuitesPane(guard); });
            return tool;
        }
        if (node.contains(QStringLiteral("review"))) {
            const QJsonObject saved = node.value(QStringLiteral("review")).toObject();
            ToolPane *tool = createReviewPane(saved.value(QStringLiteral("cwd")).toString());
            QPointer<ToolPane> guard(tool);
            QTimer::singleShot(0, tool, [guard] { if (auto *w = windowOf(guard)) w->linkRestoredReviewPane(guard); });
            return tool;
        }
        if (node.contains(QStringLiteral("subagents"))) {   // card #WD83: the tabs' text, then its owner
            const QJsonObject saved = node.value(QStringLiteral("subagents")).toObject();
            ToolPane *tool = createSubagentPane(saved.value(QStringLiteral("cwd")).toString(), saved);
            QPointer<ToolPane> guard(tool);
            const QString owner = saved.value(QStringLiteral("owner")).toString();
            QTimer::singleShot(0, tool, [guard, owner] { if (auto *w = windowOf(guard)) w->linkRestoredSubagentPane(guard, owner); });
            return tool;
        }
        if (node.contains(QStringLiteral("models"))) {   // card #MDL1 t:a11: beside the pane it served
            const QJsonObject saved = node.value(QStringLiteral("models")).toObject();
            ToolPane *tool = createModelsPane(saved.value(QStringLiteral("cwd")).toString());
            QPointer<ToolPane> guard(tool);
            const QString tab = saved.value(QStringLiteral("tab")).toString();
            QTimer::singleShot(0, tool, [guard, tab] { if (auto *w = windowOf(guard)) w->linkRestoredModelsPane(guard, tab); });
            return tool;
        }
        if (node.contains(QStringLiteral("sessions"))) {   // card #8EXS: beside the pane it served
            const QJsonObject saved = node.value(QStringLiteral("sessions")).toObject();
            ToolPane *tool = createSessionsPane(saved.value(QStringLiteral("cwd")).toString());
            QPointer<ToolPane> guard(tool);
            const QString tab = saved.value(QStringLiteral("tab")).toString();
            const QString query = saved.value(QStringLiteral("query")).toString();
            QTimer::singleShot(0, tool, [guard, tab, query] { if (auto *w = windowOf(guard)) w->linkRestoredSessionsPane(guard, tab, query); });
            return tool;
        }
        if (node.contains(QStringLiteral("settings"))) {   // card #XAME: Options or Actions, back where it was read
            const QJsonObject saved = node.value(QStringLiteral("settings")).toObject();
            const auto mode = saved.value(QStringLiteral("mode")).toString() == QStringLiteral("actions")
                                  ? relay::SettingsPane::Mode::Actions : relay::SettingsPane::Mode::Options;
            ToolPane *tool = createSettingsPane(mode);
            const QString tab = saved.value(QStringLiteral("tab")).toString();
            const QString search = saved.value(QStringLiteral("search")).toString();
            const QString row = saved.value(QStringLiteral("row")).toString();
            if (!tab.isEmpty()) tool->settings()->showTab(tab);
            if (!search.isEmpty()) tool->settings()->setSearch(search);
            // A row is revealed only when no search was on: revealOption() clears the search, and
            // with one the row was a result line, not a section row.
            if (!tab.isEmpty() && !row.isEmpty() && search.isEmpty()) tool->settings()->revealOption(tab, row);
            return tool;
        }
        if (node.contains(QStringLiteral("plan"))) {
            const QString path = node.value(QStringLiteral("plan")).toObject().value(QStringLiteral("path")).toString();
            if (QFileInfo::exists(path)) return createToolPane(ToolPane::Kind::Plan, path);
            return createPane({{"cwd", m_manager->workspace()}, {"workspace", m_manager->workspace()}});
        }
        if (node.contains(QStringLiteral("explorer")) || node.contains(QStringLiteral("preview"))) {
            const bool explorer = node.contains(QStringLiteral("explorer"));
            const QString path = node.value(explorer ? QStringLiteral("explorer") : QStringLiteral("preview")).toObject().value(QStringLiteral("path")).toString();
            if (QFileInfo::exists(path)) return createToolPane(explorer ? ToolPane::Kind::Explorer : ToolPane::Kind::Preview, path);
            return createPane({{"cwd", m_manager->workspace()}, {"workspace", m_manager->workspace()}});
        }
        return createPane(node.value(QStringLiteral("pane")).toObject());
    }

void RelayWindow::buildWindowChrome() {
        auto *left = new QWidget;
        left->setObjectName(QStringLiteral("windowChromeLeft"));
        auto *leftRow = new QHBoxLayout(left);
        leftRow->setContentsMargins(12, 0, 10, 6);   // bottom inset centres the mark on the tab labels
        leftRow->setSpacing(0);
        // Keep the launcher's complete app tile, bounded to the chrome's logical size.
        auto *icon = new ChromeAppIcon(QApplication::windowIcon());
        leftRow->addWidget(icon);
        left->installEventFilter(this);
        m_tabs->setCornerWidget(left, Qt::TopLeftCorner);

        auto *right = new QWidget;
        right->setObjectName(QStringLiteral("windowChromeRight"));
        auto *rightRow = new QHBoxLayout(right);
        rightRow->setContentsMargins(6, 0, 6, 0);
        rightRow->setSpacing(2);
        for (const QString &state : {QStringLiteral("working"), QStringLiteral("needs-you"),
                                     QStringLiteral("done"), QStringLiteral("failed")}) {
            auto *count = new QToolButton(right);
            count->setObjectName(QStringLiteral("backgroundCount-") + state);
            count->setFocusPolicy(Qt::StrongFocus);
            count->setAutoRaise(true);
            count->hide();
            connect(count, &QToolButton::clicked, this, [this, state, count] {
                auto *menu = new QMenu(this);
                menu->setAttribute(Qt::WA_DeleteOnClose);
                for (Pane *pane : m_manager->backgroundPanes()) {
                    if (pane->backgroundTaskState() != state
                        && !(state == QStringLiteral("failed")
                             && pane->backgroundTaskState() == QStringLiteral("interrupted"))) continue;
                    const QString token = pane->sessionToken();
                    const QString title = pane->paneTitle().isEmpty() ? pane->cwd() : pane->paneTitle();
                    connect(menu->addAction(title), &QAction::triggered, this,
                            [this, token] { m_manager->focusPane(token); });
                    auto *stop = menu->addAction(QStringLiteral("Stop %1").arg(title));
                    stop->setEnabled(pane->agentBusy());
                    QPointer<Pane> target(pane);
                    connect(stop, &QAction::triggered, this, [target] { if (target) target->stopAgent(); });
                }
                if (menu->actions().isEmpty()) { menu->deleteLater(); return; }
                menu->popup(count->mapToGlobal(QPoint(0, count->height())));
            });
            m_backgroundCountButtons.insert(state, count);
            rightRow->addWidget(count);
        }
        m_bell = new ChromeButton(ChromeButton::Glyph::Bell);
        // The one chrome button something outside this row has to name. Every other one is a
        // `windowChromeButton` and is reached by its neighbours; the bell is what a GUI drive
        // has to *click* to read the "Agent changed … · Undo" notice, and an icon is the one
        // thing OCR cannot find (RELAY_QA_RECTS, startQaRects).
        m_bell->setObjectName(QStringLiteral("windowBellButton"));
        connect(m_bell, &QToolButton::clicked, this, [this] { toggleNotifications(); });
        rightRow->addWidget(m_bell);
        // The row's three hairlines: after the bell, and either side of the plug below, so the
        // groups read as bell ‖ tool panes ending in the gear ‖ plug ‖ window buttons
        // (owner, 2026-09-20).
        rightRow->addSpacing(4);
        rightRow->addWidget(new ChromeSeparator);
        rightRow->addSpacing(4);
        // The tool panes, one button each, ending in the gear (owner, 2026-09-18). The table is
        // relay::panestatus::toolButtons(), so a button's glyph, its light, its tooltip and what its
        // second click closes all come from the one pane type it owns. Each wears its pane's own
        // header band while that pane is open, and closes it when clicked again.
        for (const relay::panestatus::ToolButton &spec : relay::panestatus::toolButtons()) {
            auto *button = new ChromeButton(relay::panestatus::typeStyle(spec.paneType, relay::panestatus::ColourMode::ByType,
                                                                        relay::chrome::tokens()).glyph);
            button->setPaneType(spec.paneType);
            connect(button, &QToolButton::clicked, this, [this, spec] { runToolButton(spec); });
            rightRow->addWidget(button);
            m_toolButtons.insert(spec.paneType, button);
        }
        // The tool buttons end in the gear; the plug sits right of it, between hairlines.
        rightRow->addSpacing(4);
        rightRow->addWidget(new ChromeSeparator);
        rightRow->addSpacing(4);
        // The plug: Sharing, pairing your own devices, and joining somebody else's session.
        m_connect = new ChromeButton(ChromeButton::Glyph::Connect);
        m_connect->setToolTip(QStringLiteral("Sharing and remote connections"));
        connect(m_connect, &QToolButton::clicked, this, [this] {
            QMenu menu(this);
            // The rows and their order are relay::remotesettings::plugMenu (#FR1C), so a test
            // reads them without a window: the status line the plug has carried since #PH0N,
            // "Sharing…", "Pair a phone…", then the switch. Turning the switch off here
            // is what "Disconnect all" was — the phones go and the service goes with them — under
            // the name of the thing it actually moves.
            for (const relay::remotesettings::PlugItem &item :
                 relay::remotesettings::plugMenu(shownRemoteState())) {
                using Item = relay::remotesettings::PlugItem;
                if (item.kind == Item::Separator) { menu.addSeparator(); continue; }
                QAction *action = menu.addAction(item.label);
                if (item.kind == Item::Status) { action->setEnabled(false); continue; }
                if (item.kind == Item::Toggle) {
                    action->setCheckable(true);
                    action->setChecked(item.checked);
                }
                const QString id = item.id;
                connect(action, &QAction::triggered, this, [this, id] { runPlugItem(id); });
            }
            menu.exec(m_connect->mapToGlobal(QPoint(0, m_connect->height())));
        });
        rightRow->addWidget(m_connect);
        updateRemotePlug();
        if (!m_nativeFrame) {
            // The third hairline vanishes with the window buttons it parts the plug from.
            rightRow->addSpacing(4);
            rightRow->addWidget(new ChromeSeparator);
            rightRow->addSpacing(4);
            m_minimize = new ChromeButton(ChromeButton::Glyph::Minimize);
            m_minimize->setToolTip(QStringLiteral("Minimize"));
            connect(m_minimize, &QToolButton::clicked, this, [this] { showMinimized(); });
            rightRow->addWidget(m_minimize);
            m_maximize = new ChromeButton(ChromeButton::Glyph::Maximize);
            connect(m_maximize, &QToolButton::clicked, this, [this] { toggleMaximize(); });
            rightRow->addWidget(m_maximize);
            m_close = new ChromeButton(ChromeButton::Glyph::Close);
            m_close->setToolTip(QStringLiteral("Close window"));
            // close() asks first when panes are busy (closeEvent), exactly like the tab bar's last close.
            connect(m_close, &QToolButton::clicked, this, [this] { close(); });
            rightRow->addWidget(m_close);
        }
        right->installEventFilter(this);
        m_tabs->setCornerWidget(right, Qt::TopRightCorner);

        connect(&relay::NotificationCenter::instance(), &relay::NotificationCenter::changed, this, [this] {
            updateBell();
            // A fresh post redefines "most recent", so the next jump starts at the newest — unless
            // a walk is running already (the reset timer is active), which stays stable so
            // markSeen's own changed() cannot fold the walk back onto itself (#NQP9).
            if (!m_notificationJumpReset.isActive()) m_notificationJumpIndex = 0;
        });
        m_notificationJumpReset.setSingleShot(true);
        m_notificationJumpReset.setInterval(4000);
        connect(&m_notificationJumpReset, &QTimer::timeout, this, [this] { m_notificationJumpIndex = 0; });
        updateBell();
        updateChromeState();
        syncChromeButtons();
    }

void RelayWindow::refreshPaneStatus() {
        m_manager->refreshBackgroundTasks();
        const bool light = QApplication::cursorFlashTime() <= 0
                           || relay::panestatus::pulsePhaseNow() % 2 == 0;
        for (auto it = m_backgroundCountButtons.begin(); it != m_backgroundCountButtons.end(); ++it) {
            QToolButton *button = it.value();
            if (!button) continue;
            const int count = m_manager->backgroundCount(it.key());
            button->setVisible(count > 0);
            if (!count) continue;
            button->setText(QString::number(count));
            button->setToolTip(QStringLiteral("%1 background task(s)").arg(it.key()));
            QColor color = it.key() == QStringLiteral("working") ? relay::theme::Agent
                         : it.key() == QStringLiteral("needs-you") ? relay::theme::Warning
                         : it.key() == QStringLiteral("failed") ? relay::theme::Error : relay::theme::Success;
            if (it.key() != QStringLiteral("done") && !light) color.setAlpha(130);
            button->setStyleSheet(QStringLiteral("QToolButton { color: %1; font-weight: bold; padding: 2px 5px; }")
                                      .arg(color.name(QColor::HexArgb)));
        }
        refreshPaneDimming();
        namespace ps = relay::panestatus;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const bool focused = isActiveWindow();
        QSet<QWidget *> pages;
        // One read of `appearance/pane_usage` for the whole poll: the chip, the tab label, the
        // tab tooltip and the Sessions row all answer to it (issue #D03W).
        const bool meters = relay::usage::metersEnabled();
        QHash<QString, QString> liveUsage;   // session id → tag, for the Sessions pane (#D03W)
        for (int i = 0; i < m_tabs->count(); ++i) {
            QWidget *page = m_tabs->widget(i);
            pages.insert(page);
            QList<ps::State> states;
            QList<relay::usage::Sample> usage;
            bool remote = false, terminal = false;
            ps::TypeStyle firstType;
            // Walking /proc for every pane in every tab at 2.5 Hz was 3.7 % of the idle profile
            // (card #057J). Only the tab in front has chips on screen and only a window that is
            // not minimised shows them; everything else that reads these numbers — the tab's own
            // label and the Sessions pane's tag — prints the 5 s average and takes it once every
            // kTabUpdateMs. So a tab nobody is looking at is measured on that clock instead, which
            // gives the label exactly the figure it would have had. The front tab is unchanged.
            const bool front = i == m_tabs->currentIndex() && !isMinimized();
            const qint64 sampledAt = m_tabUsage.value(page).sampledAtMs;
            const bool sample = front || sampledAt == 0 || now - sampledAt >= relay::usage::kTabUpdateMs;
            for (QWidget *leaf : leavesIn(page)) {
                auto *pane = dynamic_cast<Pane *>(leaf);
                if (!pane) {
                    if (!firstType.band) firstType = relay::chrome::styleOf(leaf);
                    continue;
                }
                terminal = true;
                PaneChrome *chrome = chromeOf(pane);
                if (!chrome) continue;
                // The pane's resource meter (issue #D03W) is sampled here so every pane is
                // measured over the same interval this poll keeps — one interval for the tab,
                // whichever clock the tab is on (see `sample` above).
                if (sample) pane->refreshUsage();
                chrome->setUsage(pane->usageSample());
                const ps::Facts facts = pane->statusFacts();
                const bool watched = focused && i == m_tabs->currentIndex() && leaf == m_activeLeaf;
                if (!watched) chrome->watchedSince = 0;
                else if (!chrome->watchedSince) chrome->watchedSince = now;
                if (watched && now - chrome->watchedSince >= kSeenAfterMs) chrome->seenSerial = facts.finishSerial;
                ps::State state = ps::resolve(facts, chrome->seenSerial);
                if (!pane->guest().isEmpty() && (state == ps::State::Running || state == ps::State::Idle)) {
                    if (pane->dimmingAgentBusy()) state = ps::State::Working;
                    else if (chrome->dimming.completed) state = ps::State::Done;
                }
                const QString remoteLine = pane->remoteCommandLine();
                const bool shared = pane->sharedWithPhone();
                const relay::sharing::ChipState chip =
                    relay::RemoteShare::instance().sharingModel().chip(pane->sessionToken(), shared);
                chrome->setStatus(state, remoteLine, chip.visible);
                // How many subagents this pane's agent has running (card #YMSR): the same count
                // the state above was resolved from, so the badge and the glyph cannot disagree.
                chrome->setSubagents(facts.liveSubagents);
                // Shared with how many people, and who is driving when it is not the owner.
                chrome->setSharing(chip.text, chip.tooltip, chip.guestDriving);
                if (!remoteLine.isEmpty()) sshSessionSeen(pane);
                states << state;
                usage << pane->usageSample();
                const QString sessionId = pane->sessionId();
                if (meters && !sessionId.isEmpty()) {
                    const QString tag = relay::usage::liveTag(pane->usageSample());
                    if (!tag.isEmpty()) liveUsage.insert(sessionId, tag);
                }
                remote = remote || !remoteLine.isEmpty();
            }
            // The tab label carries the tab's combined usage (issue #D03W) on a clock of its
            // own (card #MERX, owner 2026-09-19: "update only once every ~5 secs or so, using
            // the 5 sec average"): every poll pours the summed sample into a 5 s window, and
            // once every relay::usage::kTabUpdateMs the label takes the window's mean, printed
            // with both halves always and two digits each — so the label's width never moves
            // and the tab bar's layout never shuffles, and a number that ticks once every 5 s
            // is not a flicker. Between takes the text stands still. Only the tab whose text
            // moved is relabelled. It used to call updateTitles(), which re-elides every tab,
            // rewrites every tooltip and runs syncChrome and the share sync.
            const relay::usage::Sample summed = relay::usage::combined(usage);
            const QString previousText = m_tabUsage.value(page).text;
            QString usageText = previousText;
            if (meters && terminal) {
                TabUsageState &state = m_tabUsage[page];
                // Only a fresh reading goes into the window: a hidden tab's sample is already the
                // mean over the 5 s it covers, and pouring the same one in twelve times over would
                // make the label an average of a repeat rather than of an interval.
                if (sample) { state.window.add(summed, now); state.sampledAtMs = now; }
                if (state.labelledAtMs == 0
                    || now - state.labelledAtMs >= relay::usage::kTabUpdateMs)
                    usageText = relay::usage::tabSuffix(state.window.average());
            } else if (!previousText.isEmpty()) {
                usageText = QString();   // no terminal pane to measure, or the meters are off
            }
            if (usageText != previousText) {
                TabUsageState &state = m_tabUsage[page];
                if (usageText.isEmpty())
                    state = TabUsageState{};   // fresh window and fresh clock for panes to come
                else {
                    state.text = usageText;
                    state.labelledAtMs = now;
                }
                const QStringList titles = paneTitlesIn(page);
                m_tabs->setTabText(i, tabLabelText(page, titles));
                m_tabs->setTabToolTip(i, tabTooltipText(page, titles));
            }
            const ps::State top = ps::mostUrgent(states);
            // What is live in the tab whatever its icon is showing: a news icon (done, needs you)
            // must not hide that work is happening in a sibling pane (card #V8KT).
            const ps::State live = ps::liveMarker(states);
            m_tabMark.insert(page, TabMark{terminal, remote, top, live, firstType});
        }
        for (auto it = m_tabIconKey.begin(); it != m_tabIconKey.end();)
            it = pages.contains(it.key()) ? std::next(it) : m_tabIconKey.erase(it);
        for (auto it = m_tabMark.begin(); it != m_tabMark.end();)
            it = pages.contains(it.key()) ? std::next(it) : m_tabMark.erase(it);
        applyTabIcons();   // after the pruning, so a closed tab cannot keep the blink armed
        for (auto it = m_tabUsage.begin(); it != m_tabUsage.end();)
            it = pages.contains(it.key()) ? std::next(it) : m_tabUsage.erase(it);
        // The session manager shows the same reading as a tag on each open conversation's row
        // (issue #D03W). It never looks at a window itself; like setOpenSessions, this feeds it.
        QStringList openIds;
        bool readOpenIds = false;
        for (int i = 0; i < m_tabs->count(); ++i)
            for (QWidget *leaf : leavesIn(m_tabs->widget(i)))
                if (auto *tool = dynamic_cast<ToolPane *>(leaf);
                    tool && tool->kind() == ToolPane::Kind::Sessions)
                    if (auto *manager = dynamic_cast<relay::conversations::SessionManager *>(tool->hosted())) {
                        if (!readOpenIds) { openIds = openSessionIds(); readOpenIds = true; }
                        manager->setOpenSessions(openIds);
                        if (!liveUsage.isEmpty() || m_fedLiveUsage) manager->setLiveUsage(liveUsage);
                    }
        m_fedLiveUsage = !liveUsage.isEmpty();
    }

void RelayWindow::closePane(QWidget *pane, bool record) {
        if (m_closePrompt) return;
        QWidget *page = pageOf(pane);
        if (!page) return;
        bool stopApproved = false;
        if (auto *terminal = dynamic_cast<Pane *>(pane); terminal && terminal->hasCloseWork()) {
            QPointer<QWidget> guard(pane);
            const auto choice = askActiveClose();
            if (choice == relay::paneclose::Choice::Cancel || !guard) return;
            if (choice == relay::paneclose::Choice::Background) { backgroundPane(terminal); return; }
            stopApproved = true;
            page = pageOf(pane);
            if (!page) return;
        }
        // Closing the one pane kind that can hold unsaved edits asks first (card #SEJ2):
        // Save writes them, Discard closes, Cancel keeps the pane.
        if (auto *tool = dynamic_cast<ToolPane *>(pane);
            tool && tool->kind() == ToolPane::Kind::Preview && tool->preview()->isDirty()) {
            const auto choice = QMessageBox::warning(this, QStringLiteral("Unsaved changes"),
                QStringLiteral("Save your changes to %1 before closing?").arg(tool->preview()->title()),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
            if (choice == QMessageBox::Cancel) return;
            // A save that failed (or could not start) keeps the pane, so the edits stay put.
            if (choice == QMessageBox::Save && !tool->preview()->save()) return;
        }
        // The Switchboard's own key (Ctrl+Shift+A) closes it too; a board pane closed any other
        // way — the pane's ×, Ctrl+W — is the slow path that hint names, once.
        if (!m_boardClosedByToggle)
            if (auto *closedBoard = dynamic_cast<ToolPane *>(pane); closedBoard && closedBoard->board()) {
                const QString boardKeys = Keymap::instance().shortcutText(QStringLiteral("board.open"));
                if (!boardKeys.isEmpty())
                    hint(QStringLiteral("board.close"),
                         QStringLiteral("Next time: %1 closes the Board too").arg(boardKeys), 1);
            }
        if (auto *tool = dynamic_cast<ToolPane *>(pane);
            tool && tool->subagent() && !tool->property("closedBySubagentShortcut").toBool()) {
            hint(QStringLiteral("subagents.close"),
                 relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(QStringLiteral("agent.subagentPane")),
                                                QStringLiteral("close the subagent pane")));
        }
        // Once per run, the first time something is closed: say how to get it back (owner, 2026-09-17).
        if (record) {
            static bool toldAboutRestore = false;
            if (!toldAboutRestore) {
                toldAboutRestore = true;
                const QString keys = Keymap::instance().shortcutText(QStringLiteral("closed.restore"));
                hint(QStringLiteral("closed.restore.first"),
                     keys.isEmpty() ? QStringLiteral("Closed. \"Restore closed\" in the palette brings it back, with its conversation.")
                                    : QStringLiteral("Closed. %1 brings it back, with its conversation.").arg(keys), 1);
            }
        }
        if (leavesIn(page).size() <= 1) {
            // Last pane of its tab: close the tab, or the window when it is the last tab.
            if (m_tabs->count() > 1) closeTab(m_tabs->indexOf(page), record, stopApproved);
            else {
                if (record && m_manager->lastVisibleWindow(this) && !m_manager->backgroundPanes().isEmpty()
                    && !confirmClose()) return;
                m_confirmedClose = true;
                if (!record) m_skipRemember = true;
                close();
            }
            return;
        }
        auto *splitter = dynamic_cast<QSplitter *>(pane->parentWidget());
        if (!splitter) return;
        const int index = splitter->indexOf(pane);
        QWidget *neighbor = splitter->widget(index > 0 ? index - 1 : index + 1);
        const auto neighborPanes = leavesIn(neighbor);
        QWidget *focusNext = neighborPanes.isEmpty() ? nullptr : (index > 0 ? neighborPanes.last() : neighborPanes.first());
        if (record && focusNext && !serializeNode(pane).isEmpty()) {
            ClosedItem item;
            item.window = this; item.anchor = neighbor; item.sibling = focusNext;
            item.record.kind = relay::closed::Record::Pane;
            item.record.orientation = splitter->orientation(); item.record.before = index == 0;
            item.record.sizes = splitter->sizes(); item.record.slot = index;
            item.record.layout = serializeNode(pane);
            item.record.titles = leafTitles(pane);
            saveScrollbacksIn(pane);
            m_manager->remember(item);
        }
        if (auto *terminal = dynamic_cast<Pane *>(pane)) {
            terminal->onStatus = nullptr; terminal->onStateChanged = nullptr; terminal->onShellExited = nullptr; terminal->onOpenPath = nullptr; terminal->onEditPath = nullptr;
        }
        pane->hide();
        pane->setParent(nullptr);
        pane->deleteLater();
        if (splitter->count() == 1) {
            // Collapse a splitter that now holds a single child into its parent.
            QWidget *only = splitter->widget(0);
            QWidget *outerWidget = splitter->parentWidget();
            if (auto *outer = dynamic_cast<QSplitter *>(outerWidget)) outer->replaceWidget(outer->indexOf(splitter), only);
            else if (outerWidget && outerWidget->layout()) delete outerWidget->layout()->replaceWidget(splitter, only);
            only->show();
            splitter->hide();
            splitter->deleteLater();
        }
        if (m_active.data() == pane) m_active = nullptr;
        if (m_activeLeaf.data() == pane) m_activeLeaf = nullptr;
        if (m_lastActive.value(page) == pane) m_lastActive.remove(page);
        if (focusNext) { setActiveLeaf(focusNext); focusLeaf(focusNext); }
        updateTitles();
    }
