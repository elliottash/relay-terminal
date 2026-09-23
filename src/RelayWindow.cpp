// SPDX-License-Identifier: AGPL-3.0-or-later
#include "RelayWindow.h"
#include "WindowManagerImpl.h"



QList<RelayWindow::PaletteItem> RelayWindow::rootItems() {
        QList<PaletteItem> items;
        Pane *pane = m_active;
        const QString agent = QStringLiteral("Agent"), terminal = QStringLiteral("Terminal"), panes = QStringLiteral("Panes and tabs"),
                      app = QStringLiteral("Relay"), keys = QStringLiteral("Shortcuts");
        // The model, by its name and its provider (card #MDL1, rule 1) — "glm-5.3 · z.ai (glm)",
        // not the preset label "z.ai · glm-5.3 · coding plan", which is a plan and not a model.
        QString currentModel;
        if (pane) {
            currentModel = pane->presetModelDisplay(pane->currentPreset());
            if (currentModel.isEmpty())
                for (const auto &model : pane->storedModels())
                    if (model.first == pane->currentPreset()) currentModel = model.second;
        }
        items << submenu(QStringLiteral("menu:model"), agent, QStringLiteral("Model"), currentModel.isEmpty() ? QStringLiteral("No stored keys") : currentModel, [this] {
            QList<PaletteItem> children;
            if (!m_active) return children;
            for (const auto &model : m_active->storedModels()) {
                PaletteItem item;
                const QString id = model.first;
                item.key = QStringLiteral("model:") + id; item.section = QStringLiteral("Model");
                const QString named = m_active->presetModelDisplay(id);
                item.label = named.isEmpty() ? model.second : named;
                item.detail = QStringLiteral("This pane; starts a new conversation");
                item.checked = m_active->currentPreset() == id;
                item.run = [this, id] { if (m_active) m_active->selectModel(id); };
                children << item;
            }
            return children;
        });
        if (pane) {
            // Model roles (protocol 13): flip this pane between the Main agent and the Flash agent.
            const bool flash = pane->agentRole() == QStringLiteral("flash");
            const QString flashModel = pane->roleModel(QStringLiteral("flash"));
            items << actionItem(agent, QStringLiteral("Flash agent for this pane"),
                                (flash ? QStringLiteral("On · ") : QStringLiteral("Off · "))
                                    + (flashModel.isEmpty() ? QStringLiteral("the Flash model for this pane; the conversation is kept") : flashModel),
                                QStringLiteral("agent.flashAgent"), flash);
        }
        if (pane && (pane->hasLocalEndpoint() || pane->agentRole() == QStringLiteral("local"))) {
            // The Local agent (card #JH22), listed only when this machine serves a model. /local
            // does the same thing from the prompt box.
            const bool local = pane->agentRole() == QStringLiteral("local");
            const QString localModel = pane->roleModel(QStringLiteral("local"));
            items << actionItem(agent, QStringLiteral("Local agent for this pane"),
                                (local ? QStringLiteral("On · ") : QStringLiteral("Off · "))
                                    + (localModel.isEmpty() ? QStringLiteral("a model served on this machine; the conversation is kept · /local") : localModel),
                                QStringLiteral("agent.localAgent"), local);
        }
        const QString mode = pane ? pane->mode() : QStringLiteral("auto");
        const QString modeName = mode == QStringLiteral("shell") ? QStringLiteral("Terminal") : mode == QStringLiteral("agent") ? QStringLiteral("Agent") : QStringLiteral("Auto");
        items << submenu(QStringLiteral("menu:mode"), agent, QStringLiteral("Input mode"), modeName, [this, mode] {
            return QList<PaletteItem>{
                actionItem(QStringLiteral("Input mode"), QStringLiteral("Auto"), QStringLiteral("Commands to the terminal, everything else to the agent"), QStringLiteral("input.modeAuto"), mode == QStringLiteral("auto")),
                actionItem(QStringLiteral("Input mode"), QStringLiteral("Terminal"), QStringLiteral("Always the terminal; the agent fixes failures"), QStringLiteral("input.modeTerminal"), mode == QStringLiteral("shell")),
                actionItem(QStringLiteral("Input mode"), QStringLiteral("Agent"), QStringLiteral("Always the agent"), QStringLiteral("input.modeAgent"), mode == QStringLiteral("agent"))};
        });
        items << actionItem(agent, QStringLiteral("Toggle terminal / agent input"), QStringLiteral("From the prompt box"), QStringLiteral("input.toggle"));
        items << actionItem(agent, QStringLiteral("Clear the prompt box"), QStringLiteral("One undo step: Ctrl+Z brings it back"), QStringLiteral("prompt.clear"));
        {
            // Voice transcription: the same action the microphone chip runs.
            const bool recording = pane && pane->voiceRecording();
            const QString hold = Pane::voiceHoldKey();
            items << actionItem(agent, recording ? QStringLiteral("Finish the recording") : QStringLiteral("Voice transcription"),
                                recording ? QStringLiteral("Transcribe what you said into the prompt box")
                                          : QStringLiteral("Record and transcribe into the prompt box%1").arg(
                                                hold == QStringLiteral("off") ? QString()
                                                    : QStringLiteral(" · hold %1").arg(relay::voice::holdKeyLabel(hold))),
                                QStringLiteral("voice.toggle"));
        }
        {
            // Read aloud (#MDA7): the same action as /speak; while this pane reads, it stops.
            const bool reading = pane && pane->readingAloud();
            items << actionItem(agent, reading ? QStringLiteral("Stop reading aloud") : QStringLiteral("Read aloud"),
                                reading ? QStringLiteral("Stop the system voice (Esc in the prompt box)")
                                        : QStringLiteral("Read the last agent reply in the system voice"),
                                QStringLiteral("speech.readAloud"));
        }
        if (pane) {
            const QString effort = pane->effort();
            // The levels this pane's provider has, not Relay's four: on GLM medium is the same
            // request as high, and Relay Free stops at medium (presets.py effort_levels).
            const QStringList levels = pane->offeredEfforts();
            const QString effortNote = pane->effortNote();
            // The note goes in the submenu's own detail line. It is a sentence about the provider,
            // not something to run, and a row here is a thing you can choose.
            items << submenu(QStringLiteral("menu:effort"), agent, QStringLiteral("Reasoning effort"),
                             levels.isEmpty()
                                 ? QStringLiteral("this model has no reasoning setting")
                                 : Pane::nearestEffort(levels, effort)
                                       + (effortNote.isEmpty() ? QString()
                                                               : QStringLiteral(" · ") + effortNote),
                             [this, effort, levels] {
                QList<PaletteItem> children;
                for (const QString &level : levels) {
                    PaletteItem item;
                    item.key = QStringLiteral("effort:") + level; item.section = QStringLiteral("Reasoning effort"); item.label = level;
                    item.detail = QStringLiteral("This pane; keeps the conversation");
                    item.checked = Pane::nearestEffort(levels, effort) == level; item.stayOpen = true;
                    item.run = [this, level] { if (m_active) m_active->setEffort(level); };
                    children << item;
                }
                return children;
            });
            if (!pane->lastTurnId().isEmpty()) {
                PaletteItem turn; turn.key = QStringLiteral("agent:last_turn"); turn.section = agent;
                turn.label = QStringLiteral("Open last agent turn"); turn.detail = QStringLiteral("tool calls, arguments and full outputs");
                turn.run = [this, guard = QPointer<Pane>(pane), id = pane->lastTurnId()] { if (guard) openTurnPane(guard, id); };
                items << turn;
            }
            {
                PaletteItem skills; skills.key = QStringLiteral("agent:skills"); skills.section = agent;
                skills.label = QStringLiteral("Skills…"); skills.detail = QStringLiteral("list, exclude, refine, import · /skills");
                skills.run = [guard = QPointer<Pane>(pane)] { if (guard) guard->openSkills(); };
                items << skills;
            }
            // Aliases (issue G8DK): saved commands and prompts. The submenu is the slow way to run
            // one, so running from here teaches `/name` and the name typed in terminal mode.
            {
                const int count = relay::aliases::names(pane->aliases()).size();
                items << submenu(QStringLiteral("menu:aliases"), agent, QStringLiteral("Aliases…"),
                                 count ? QStringLiteral("%1 saved command%2 and prompt%3 · /name to run one")
                                             .arg(count).arg(count == 1 ? QString() : QStringLiteral("s"))
                                             .arg(count == 1 ? QString() : QStringLiteral("s"))
                                       : QStringLiteral("Saved commands and prompts — none yet"),
                                 [this] { return aliasMenuItems(); });
                PaletteItem save; save.key = QStringLiteral("alias:save"); save.section = agent;
                save.label = QStringLiteral("Save the prompt box as an alias…");
                save.detail = QStringLiteral("Keep this command or prompt and run it later by name");
                save.run = [guard = QPointer<Pane>(pane)] { if (guard) guard->saveComposerAsAlias(); };
                items << save;
                PaletteItem import; import.key = QStringLiteral("alias:import"); import.section = agent;
                import.label = QStringLiteral("Import Warp workflows and shell aliases…");
                import.detail = QStringLiteral("Preview what would be saved; nothing runs and nothing is written until you confirm");
                import.run = [guard = QPointer<Pane>(pane)] { if (guard) guard->openAliasImport(); };
                items << import;
            }
            items << actionItem(agent, pane->agentMode() == QStringLiteral("plan") ? QStringLiteral("Leave plan mode") : QStringLiteral("Plan mode"),
                                QStringLiteral("Investigate and write a plan before changing anything"), QStringLiteral("agent.planToggle"),
                                pane->agentMode() == QStringLiteral("plan"));
        }
        items << actionItem(agent, QStringLiteral("Swap models"), QStringLiteral("Switch to the main model and back"), QStringLiteral("agent.swap"));
        items << actionItem(agent, QStringLiteral("Compact conversation"), QStringLiteral("Summarize older turns to free context"), QStringLiteral("agent.compact"));
        items << actionItem(agent, QStringLiteral("Rewind chat…"), QStringLiteral("Conversation back to an earlier turn; files unchanged · Esc Esc"), QStringLiteral("agent.rewind"));
        items << actionItem(agent, QStringLiteral("Rewind code…"), QStringLiteral("Restore files the agent changed since a turn · /rewind-code"), QStringLiteral("agent.rewindCode"));
        items << actionItem(agent, QStringLiteral("Fork conversation"), QStringLiteral("Continue this conversation in a new pane"), QStringLiteral("agent.fork"));
        {
            // One row for the shared pane (#SPSG): Background and Recently closed are pages
            // inside Sessions; Projects and Globals are tabs with their own direct keys.
            PaletteItem sessions = submenu(QStringLiteral("sessions.open"), agent, QStringLiteral("Sessions & Projects"),
                                           QStringLiteral("Find and resume a session, open a project, reopen background work, "
                                                          "global memories · /resume"),
                                           [this] { return sessionsMenuItems(); });
            sessions.shortcut = Keymap::instance().shortcutText(QStringLiteral("sessions.open"));
            sessions.run = [this] { runAction(QStringLiteral("sessions.open")); };
            sessions.aliases = QStringLiteral("resume session sessions reopen continue conversations search find chats threads "
                                              "history full text past old grep manager projects globals background");
            items << sessions;
            PaletteItem info = actionItem(agent, QStringLiteral("Conversation info"),
                                          QStringLiteral("This conversation's model, tokens, file and history, with its subagent "
                                                         "threads · /status"),
                                          QStringLiteral("agent.info"));
            info.aliases = QStringLiteral("status info details usage cost context about session");
            items << info;
        }
        items << actionItem(agent, QStringLiteral("Find in this pane…"),
                            QStringLiteral("Search this conversation and the terminal scrollback"),
                            QStringLiteral("find.inView"));
        {
            PaletteItem rebuild;
            rebuild.key = QStringLiteral("conversations.rebuild"); rebuild.section = agent;
            rebuild.label = QStringLiteral("Rebuild the conversation index");
            rebuild.detail = QStringLiteral("The index is a cache of the saved conversations; this recreates it");
            rebuild.run = [this] { if (m_active) m_active->rebuildConversationIndex(); };
            items << rebuild;
        }
        items << actionItem(agent, QStringLiteral("Recap"), QStringLiteral("Summarize what happened in this session"), QStringLiteral("agent.recap"));
        items << actionItem(agent, QStringLiteral("Tasks…"),
                            pane && !pane->tasksProgress().isEmpty() ? pane->tasksProgress() + QStringLiteral(" · the agent's task list · /tasks")
                                                                     : QStringLiteral("Task list: what the agent is working on · /tasks"), QStringLiteral("agent.requests"));
        items << actionItem(agent, QStringLiteral("Reasoning fold"),
                            pane && pane->thinkingFoldVisible()
                                ? QStringLiteral("Fold the agent's reasoning away in this pane")
                                : QStringLiteral("Unfold the agent's reasoning · the last turn's, between turns"),
                            QStringLiteral("agent.thinkingPanel"), pane && pane->thinkingFoldVisible());
        items << actionItem(agent, QStringLiteral("Activity"),
                            pane && pane->internals()
                                ? QStringLiteral("Bring this pane's Activity pane forward")
                                : QStringLiteral("Watch the reasoning and the tool calls in a pane beside the terminal"),
                            QStringLiteral("agent.internalsPane"), pane && pane->internals());
        // The row is a slow path, so it teaches the fast one (#SXF1): agent.continue's own key
        // when it has one, else agent.interrupt's empty-box send-now that sends Continue.
        QString continueKeys = Keymap::instance().shortcutText(QStringLiteral("agent.continue"));
        if (continueKeys.isEmpty()) continueKeys = Keymap::instance().shortcutText(QStringLiteral("agent.interrupt"));
        const QString continueHow = continueKeys.isEmpty() ? QStringLiteral("/continue")
                                                           : continueKeys + QStringLiteral(" on an empty box or /continue");
        items << actionItem(agent, QStringLiteral("Continue agent turn"),
                            pane && (pane->limitReached() || pane->turnCutOff())
                                ? QStringLiteral("The last turn stopped early · %1").arg(continueHow)
                                : QStringLiteral("Send “Continue” to the agent · %1").arg(continueHow),
                            QStringLiteral("agent.continue"));
        items << actionItem(agent, QStringLiteral("Export conversation"), QStringLiteral("Save the conversation as Markdown"), QStringLiteral("agent.export"));
        // Image context: reaching this from the palette is the slow path, so the palette's own hint
        // teaches its shortcut (issue EM1E).
        items << actionItem(agent, QStringLiteral("Screenshot this pane"),
                            QStringLiteral("Attach a picture of this pane to your next prompt"),
                            QStringLiteral("agent.screenshotPane"));
        items << actionItem(agent, QStringLiteral("API keys…"),
                            QStringLiteral("Add, replace, remove or test a provider key"),
                            QStringLiteral("agent.modelKeys"));
        items << actionItem(agent, QStringLiteral("per-job models…"),
                            QStringLiteral("What each job — plan mode, subagents, summaries, chores — runs on"),
                            QStringLiteral("agent.modelRoles"));
        items << actionItem(agent, QStringLiteral("New chat"), QStringLiteral("Start a new conversation in this pane and clear the terminal"), QStringLiteral("agent.newChat"));
        items << actionItem(agent, QStringLiteral("Stop agent"), pane && pane->agentBusy() ? QStringLiteral("Cancel the running turn") : QStringLiteral("Agent is idle"), QStringLiteral("agent.stop"));
        // --- subagents UI ---
        {
            const int live = pane ? pane->subagents().liveCount() : 0;
            items << submenu(QStringLiteral("menu:agents"), agent, QStringLiteral("Agents…"),
                             live ? QStringLiteral("%1 running").arg(live) : QStringLiteral("Definitions and running agents"),
                             [this] { return agentsMenuItems(); });
            items << actionItem(agent, QStringLiteral("Stop all agents"), live ? QStringLiteral("%1 running subagent(s)").arg(live) : QStringLiteral("No running subagents"),
                                QStringLiteral("agent.stopAllSubagents"));
        }
        // --- end subagents UI ---
        if (pane && pane->queuedPrompts() > 0)
            items << actionItem(agent, QStringLiteral("Clear queue"), QStringLiteral("%1 queued item(s)").arg(pane->queuedPrompts()), QStringLiteral("agent.clearQueue"));
        if (pane && pane->queuePaused())
            items << actionItem(agent, QStringLiteral("Resume queue"), QStringLiteral("Paused after a stop, failure or edit"), QStringLiteral("agent.resumeQueue"));
        {
            // Sharing (#W5N2). Two rows, because they are two different things: hand out a way in,
            // and look after the people who came in.
            relay::RemoteShare &share = relay::RemoteShare::instance();
            const bool shared = pane && share.isSharing(pane->sessionToken());
            const int guests = pane ? share.sharingModel().guestsOn(pane->sessionToken()) : 0;
            const int waiting = share.sharingModel().waiting();
            // Pairing your own phone first: it is the common case, and it is the one that turns
            // remote control on by itself (#FR1C).
            items << actionItem(terminal, QStringLiteral("Pair a phone…"),
                                relay::RemoteShare::instance().alwaysOn()
                                    ? QStringLiteral("The code to type on it, and the QR · remote control is on")
                                    : QStringLiteral("Turns remote control on, then shows the code to type on the phone"),
                                QStringLiteral("remote.pair"));
            items << actionItem(terminal, QStringLiteral("Share this pane…"),
                                shared ? QStringLiteral("Already shared · pair another phone, or invite someone")
                                       : QStringLiteral("Pair your phone, or make a link for somebody else"),
                                QStringLiteral("pane.share"));
            if (share.sharingModel().anyShared())
                items << actionItem(terminal, QStringLiteral("Sharing"),
                                    waiting > 0 ? QStringLiteral("%1 waiting for you").arg(waiting)
                                    : guests > 0 ? QStringLiteral("%1 here · invites, roles and what is waiting").arg(guests)
                                                 : QStringLiteral("Who is here, live invites, and what is waiting for you"),
                                    QStringLiteral("pane.sharing"));
            items << actionItem(terminal, QStringLiteral("Open a shared pane…"),
                                QStringLiteral("A pane your other desktop shares, here as one of your devices"),
                                QStringLiteral("remote.openShared"));
            items << actionItem(terminal, QStringLiteral("Join a shared session…"),
                                QStringLiteral("The meeting code and PIN someone gave you · /join CODE"),
                                QStringLiteral("remote.join"));
        }
        items << actionItem(terminal, QStringLiteral("Interrupt"), pane && pane->processBusy() ? QStringLiteral("Stop the running program · Esc in the prompt box") : QStringLiteral("Nothing is running"), QStringLiteral("terminal.interrupt"));
        // One toggle, one row (#QWAS): control.prompt is the same key's other direction.
        items << actionItem(terminal, pane && pane->isNative() ? QStringLiteral("Back to the prompt box") : QStringLiteral("Take control"),
                            pane && pane->isNative()
                                ? QStringLiteral("The prompt box becomes the input again")
                                : QStringLiteral("Hide the prompt box and type into the terminal · the only way keys reach it"),
                            QStringLiteral("control.human"), pane && pane->isNative());
        {
            // Hand the running program to the agent, or take it back (card C1HH).
            const bool driving = pane && pane->agentDriving();
            const QString detail = !pane                     ? QStringLiteral("No pane")
                : !pane->canShowAgentTheScreen()             ? QStringLiteral("This pane's engine cannot show the agent the screen · open a Relay-engine pane")
                : !pane->processBusy()                       ? QStringLiteral("Nothing is running in this pane")
                : driving                                    ? QStringLiteral("The agent may type into %1 · this hands it back").arg(pane->foregroundProgramName())
                : QStringLiteral("The agent may type into %1 until you take control").arg(pane->foregroundProgramName());
            items << actionItem(terminal, driving ? QStringLiteral("Take the program back from the agent")
                                                  : QStringLiteral("Let the agent drive this program"),
                                detail, QStringLiteral("program.delegate"), driving);
        }
        {
            const QString program = pane ? pane->foregroundProgramName() : QString();
            if (!program.isEmpty()) {
                const QString override = Pane::programControl(program);
                PaletteItem agentItem;
                agentItem.key = QStringLiteral("control.program.agent"); agentItem.section = terminal;
                agentItem.label = QStringLiteral("Keep the prompt box when %1 starts").arg(program);
                agentItem.detail = QStringLiteral("%1 gets a \"Take control\" button instead of the keyboard").arg(program);
                agentItem.checked = override == QStringLiteral("agent"); agentItem.stayOpen = true;
                agentItem.run = [program, override] { Pane::setProgramControl(program, override == QStringLiteral("agent") ? QString() : QStringLiteral("agent")); };
                items << agentItem;
                PaletteItem humanItem;
                humanItem.key = QStringLiteral("control.program.human"); humanItem.section = terminal;
                humanItem.label = QStringLiteral("Always take control of %1").arg(program);
                humanItem.detail = QStringLiteral("The prompt box hides as soon as %1 starts").arg(program);
                humanItem.checked = override == QStringLiteral("human"); humanItem.stayOpen = true;
                humanItem.run = [program, override] { Pane::setProgramControl(program, override == QStringLiteral("human") ? QString() : QStringLiteral("human")); };
                items << humanItem;
            }
        }
        if (m_active) {
            PaletteItem clear;
            clear.key = QStringLiteral("terminal.clear"); clear.section = terminal;
            clear.label = QStringLiteral("Clear terminal");
            clear.detail = QStringLiteral("Screen and scrollback · %1").arg(m_active->engineLabel());
            clear.run = [this] { if (m_active) m_active->clearTerminal(); };
            items << clear;
            if (m_active->terminalCan(relay::TerminalBackend::PromptMarks)) {
                for (const int direction : {-1, 1}) {
                    PaletteItem jump;
                    jump.key = QStringLiteral("terminal.prompt%1").arg(direction < 0 ? QStringLiteral("Previous") : QStringLiteral("Next"));
                    jump.section = terminal;
                    jump.label = direction < 0 ? QStringLiteral("Jump to previous prompt") : QStringLiteral("Jump to next prompt");
                    jump.detail = QStringLiteral("Needs the shell integration (OSC 133)");
                    jump.run = [this, direction] {
                        if (m_active && !m_active->jumpToPrompt(direction))
                            notice(QStringLiteral("No prompt mark in that direction. Enable the shell integration (OSC 7/133)."));
                    };
                    items << jump;
                }
            }
            if (m_active->canWalkOutputLinks()) {
                PaletteItem walk;
                walk.key = QStringLiteral("links.step"); walk.section = terminal;
                walk.label = QStringLiteral("Step through links in the output");
                walk.detail = QStringLiteral("Files, folders and URLs · Enter opens, Esc leaves · %1")
                                  .arg(Keymap::instance().shortcutText(QStringLiteral("links.step")));
                walk.run = [this] { if (m_active) m_active->stepOutputLink(-1); };
                items << walk;
            }
            if (m_active->terminalCan(relay::TerminalBackend::Search)) {
                PaletteItem find;
                find.key = QStringLiteral("terminal.find"); find.section = terminal;
                find.label = QStringLiteral("Search terminal…");
                find.detail = QStringLiteral("Screen and scrollback · %1").arg(m_active->engineLabel());
                find.run = [this] {
                    if (!m_active) return;
                    bool ok = false;
                    const QString text = QInputDialog::getText(this, QStringLiteral("Search terminal"),
                                                               QStringLiteral("Find:"), QLineEdit::Normal, QString(), &ok);
                    if (!ok || text.isEmpty() || !m_active) return;
                    const int matches = m_active->findInTerminal(text, false);
                    notice(matches > 0 ? QStringLiteral("%1 match(es) for \"%2\"").arg(matches).arg(text)
                                                         : QStringLiteral("No match for \"%1\"").arg(text));
                };
                items << find;
            }
        }

        items << actionItem(panes, QStringLiteral("File explorer"), QStringLiteral("Open this pane's directory, or close the explorer again"), QStringLiteral("files.explorer"));
        {
            // Board: the board of cards, threads, plans and memory (design 4.1).
            PaletteItem board = actionItem(panes, QStringLiteral("Board"),
                                           QStringLiteral("Cards, threads, plans and project memory"),
                                           QStringLiteral("board.open"));
            board.aliases = QStringLiteral("board switchboard issues cards todo trello kanban scratchpad tickets tracker");
            items << board;
        }
        {
            // The Board's tooling sibling (card #7BM4): the project's tests, their history
            // and their runs. No key of its own — Ctrl+Shift+T is New tab everywhere — so the
            // palette and the board's Tests button are how it is reached.
            PaletteItem tests = actionItem(panes, QStringLiteral("Test suites"),
                                           QStringLiteral("This project's tests, their history and their runs, beside the Board"),
                                           QStringLiteral("tests.open"));
            tests.aliases = QStringLiteral("tests test suites ctest unittest flaky slow failing suite coverage runs");
            items << tests;
        }
        // Offered only while this tab is attached to a project (#JN7X): with no project there is
        // nothing to detach from, and the quiet state must not advertise itself. No shortcut —
        // detaching is rare, so there is no fast path to teach and no hint entry.
        if (const QString project = tabProject(m_tabs->currentWidget()); !project.isEmpty()) {
            PaletteItem detach;
            detach.key = QStringLiteral("project.detach"); detach.section = panes;
            detach.label = QStringLiteral("Detach this tab from %1").arg(relay::projects::nameFor(project));
            detach.detail = QStringLiteral("%1 · its panes lose the card tools; an open Board stays open").arg(project);
            detach.aliases = QStringLiteral("project board switchboard attach unattach");
            detach.run = [this] { detachTab(m_tabs->currentWidget()); };
            items << detach;
        } else {
            // The shared Projects tab owns project selection and initialization.
            PaletteItem pick = actionItem(panes, QStringLiteral("Attach this tab to a project…"),
                                          QStringLiteral("The projects Relay knows, most recent first, or initialize one here"),
                                          QStringLiteral("project.pick"));
            pick.aliases = QStringLiteral("project board switchboard attach known picker initialize init");
            pick.run = [this] {
                openProjectsFor(m_active, QString());
                hint(QStringLiteral("project.pick.palette"),
                     relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(QStringLiteral("sessions.open"))));
            };
            items << pick;
        }
        // The one passive entry point to "Initialize a project and create a Board here?"
        // (protocol 19.12). Offered only while the active pane is standing in a project that has
        // no board: with a board there is nothing to create, and outside a project there is
        // nothing to create it in. Choosing it here is the slow path, so it teaches `/init`.
        if (const QString candidate = candidateProject();
            !candidate.isEmpty() && relay::projects::boardDirOf(candidate).isEmpty()) {
            PaletteItem init;
            init.key = QStringLiteral("project.init"); init.section = panes;
            init.label = QStringLiteral("Initialize a project here…");
            init.detail = QStringLiteral("%1 · asks first, then creates %2/")
                              .arg(candidate, relay::projectinit::boardFolderFor(candidate));
            init.aliases = QStringLiteral("switchboard board project init cards start setup");
            init.run = [this] {
                if (!m_active) return;
                m_active->askProjectInit(relay::projectinit::Trigger::InitCommand);
                hint(QStringLiteral("init.palette"), QStringLiteral("Next time: type /init in any prompt box"));
            };
            items << init;
        }
        items << actionItem(panes, QStringLiteral("Open file…"), QStringLiteral("Preview a file in a pane"), QStringLiteral("files.open"));
        items << actionItem(panes, QStringLiteral("Toggle word wrap"), QStringLiteral("Word wrap in the file preview or editor pane"), QStringLiteral("files.toggleWrap"));
        // The one key makes a pane on the right; all four directions keep an action of their own
        // so they can be run from here or bound (issue #78BN).
        items << actionItem(panes, QStringLiteral("New pane to the right"),
                            QStringLiteral("Then ← ↑ ↓ within two seconds places it on that side"), QStringLiteral("pane.splitRight"));
        items << actionItem(panes, QStringLiteral("New pane below"), QString(), QStringLiteral("pane.splitDown"));
        items << actionItem(panes, QStringLiteral("New pane to the left"), QString(), QStringLiteral("pane.splitLeft"));
        items << actionItem(panes, QStringLiteral("New pane above"), QString(), QStringLiteral("pane.splitUp"));
        // SSH (#S5SH): the split is offered only while the pane is in a session it can re-run.
        if (pane && !pane->remoteCommandLine().isEmpty()) {
            QString host;
            const QString again = relay::ssh::rerunCommand(relay::ssh::processArgv(pane->foregroundPid()), &host);
            if (!again.isEmpty())
                items << actionItem(panes, QStringLiteral("Split on the same host"),
                                    QStringLiteral("A pane to the right running %1; a shared connection needs no second login").arg(again),
                                    QStringLiteral("ssh.splitSameHost"));
        }
        {
            PaletteItem hosts = actionItem(panes, QStringLiteral("Connect to SSH…"),
                                           QStringLiteral("Choose a saved or recent host, or enter a new one"),
                                           QStringLiteral("ssh.connect"));
            hosts.aliases = QStringLiteral("ssh mosh remote server login");
            hosts.typed = [this](const QString &search) { return sshTypedItems(search); };
            items << hosts;
        }
        items << actionItem(panes, QStringLiteral("New tab"), QString(), QStringLiteral("tab.new"));
        items << actionItem(panes, QStringLiteral("New window"), QString(), QStringLiteral("window.new"));
        items << actionItem(panes, QStringLiteral("Toggle fullscreen"), QStringLiteral("Hide the desktop taskbar and window borders"), QStringLiteral("window.fullscreen"));
        items << actionItem(panes, QStringLiteral("Dim this pane"), QStringLiteral("Toggle manual dimming; Alt+wheel adjusts strength"), QStringLiteral("pane.dimToggle"));
        items << actionItem(panes, QStringLiteral("Brighten pane"), QStringLiteral("Reduce dimming by 5%"), QStringLiteral("pane.brighten"));
        items << actionItem(panes, QStringLiteral("Dim pane more"), QStringLiteral("Increase dimming by 5%"), QStringLiteral("pane.darken"));
        items << actionItem(panes, QStringLiteral("Focus mode"), QStringLiteral("Dim other panes"), QStringLiteral("pane.focusMode"), relay::settings::boolValue(QStringLiteral("appearance/focus_mode"), false));
        items << actionItem(panes, QStringLiteral("Dim while working"), QStringLiteral("Dim working agents"), QStringLiteral("pane.autoDim"), relay::settings::boolValue(QStringLiteral("appearance/auto_dim"), false));
        items << actionItem(panes, QStringLiteral("Close pane"), QStringLiteral("Then the tab, then the window"), QStringLiteral("pane.close"));
        items << actionItem(panes, QStringLiteral("Move pane to new tab"), QStringLiteral("Keeps the shell and agent running"), QStringLiteral("pane.moveToNewTab"));
        items << actionItem(panes, QStringLiteral("Move to background"), QStringLiteral("Keep the agent running outside the layout"), QStringLiteral("pane.moveToBackground"));
        items << actionItem(panes, QStringLiteral("Run in background"), QStringLiteral("Send the prompt, then free this pane's space"), QStringLiteral("pane.runInBackground"));
        {
            // "Auto-resize" is what the owner calls it (#GSJ7), so the name is searchable and the
            // detail says it: this is the entry the "?" list shows, and the drag hint teaches it.
            PaletteItem equalize = actionItem(panes, QStringLiteral("Equalize pane sizes (auto-resize)"),
                                              QStringLiteral("Every splitter in this tab back to equal shares"), QStringLiteral("pane.equalize"));
            equalize.aliases = QStringLiteral("auto resize auto-resize reset sizes even balance tidy layout rearrange");
            items << equalize;
        }
        items << actionItem(panes, QStringLiteral("Move tab to new window"), QStringLiteral("Keeps its panes running"), QStringLiteral("tab.moveToNewWindow"));
        items << actionItem(panes, QStringLiteral("Move pane left"), QStringLiteral("Then ↓ docks it beneath · or drag the ⠿ grip"), QStringLiteral("pane.moveLeft"));
        items << actionItem(panes, QStringLiteral("Move pane right"), QStringLiteral("Then ↓ docks it beneath"), QStringLiteral("pane.moveRight"));
        items << actionItem(panes, QStringLiteral("Move pane up"), QString(), QStringLiteral("pane.moveUp"));
        items << actionItem(panes, QStringLiteral("Move pane down"), QStringLiteral("Straight after a left/right move, beneath that neighbor"), QStringLiteral("pane.moveDown"));
        items << actionItem(panes, QStringLiteral("Restore closed"), QStringLiteral("Last closed pane, tab or window"), QStringLiteral("closed.restore"));
        items << actionItem(panes, QStringLiteral("Recently closed…"), QStringLiteral("Choose from the last 25 closed panes, tabs and windows"), QStringLiteral("closed.list"));
        // "Reopen windows on start" is a row in Options › General.
        items << actionItem(panes, QStringLiteral("Start a fresh window set"),
                            QStringLiteral("Forget the saved layout; the next start opens one new window"),
                            QStringLiteral("windows.fresh"));
        items << actionItem(panes, QStringLiteral("Next tab"), QString(), QStringLiteral("tab.next"));
        items << actionItem(panes, QStringLiteral("Previous tab"), QString(), QStringLiteral("tab.previous"));

        // Relay itself. Options is an action like any other: it is how you get there from here.
        items << actionItem(app, QStringLiteral("Options…"),
                            QStringLiteral("What persists: appearance, models, terminal, agent, voice, privacy, keyboard"),
                            QStringLiteral("app.settings"));
        items << actionItem(app, QStringLiteral("Restart Relay"),
                            QStringLiteral("Save this workspace, exit, then reopen it · /restart"),
                            QStringLiteral("app.restart"));
        {
            // "Which build am I on" is a question you ask in words, not by hunting through tabs, and
            // the page it opens is all Info rows — which the Options search skips on purpose, since
            // a search result there is something you press. So the palette is where it is findable.
            const relay::buildinfo::Running &build = relay::buildinfo::running();
            PaletteItem item; item.key = QStringLiteral("app.about"); item.section = app;
            item.label = QStringLiteral("About Relay");
            item.detail = QStringLiteral("Version %1 · build %2 · licence · what it is built on")
                              .arg(QStringLiteral(RELAY_VERSION), build.id);
            item.aliases = QStringLiteral("about version build number licence license agpl qt engine platform diagnostics");
            item.run = [this] { openSettingsPane(relay::SettingsPane::Mode::Options, QStringLiteral("about")); };
            items << item;
        }
        {
            PaletteItem themes; themes.key = QStringLiteral("theme.reload"); themes.section = app;
            themes.label = QStringLiteral("Reload themes"); themes.detail = QStringLiteral("Pick up a theme file you added or edited");
            themes.run = [this] {
                relay::theme::refreshThemes();
                relay::theme::setActiveTheme(relay::theme::activeThemeId(), false);
                notice(QStringLiteral("Themes reloaded. A theme added while Relay runs reaches new terminal panes; "
                                      "restart to give it to the ones already open."), 8000);
            };
            items << themes;
            PaletteItem folder; folder.key = QStringLiteral("theme.folder"); folder.section = app;
            folder.label = QStringLiteral("Open your themes folder"); folder.detail = QStringLiteral("~/.config/relay/themes");
            folder.run = [this] {
                const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
                                        .absoluteFilePath(QStringLiteral("relay/themes"));
                QDir().mkpath(dir);
                QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
                notice(dir, 8000);
            };
            items << folder;
            PaletteItem logs; logs.key = QStringLiteral("logs.open"); logs.section = app;
            logs.label = QStringLiteral("Open the log folder");
            logs.detail = relay::log::directory().isEmpty() ? QStringLiteral("No writable data directory") : relay::log::directory();
            logs.aliases = QStringLiteral("log logs diagnostics debug troubleshoot relay.log worker.log");
            logs.run = [this] {
                const QString dir = relay::log::directory();
                if (dir.isEmpty()) { notice(QStringLiteral("No writable data directory for logs."), 6000); return; }
                QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
                notice(dir, 8000);
            };
            items << logs;
            PaletteItem hints; hints.key = QStringLiteral("hints.reset"); hints.section = app;
            hints.label = QStringLiteral("Reset shortcut hints"); hints.detail = QStringLiteral("Show every tip again");
            hints.run = [this] {
                relay::ShortcutHints::instance().resetAll();
                notice(QStringLiteral("Shortcut hints reset."), 4000);
            };
            items << hints;
            // The prompt box's Up/Down history outlives a restart (src/PromptHistory.h), so there
            // has to be a way to forget it. Every pane's file goes, and so does the copy every
            // open prompt box holds — including one part-way through a browse.
            PaletteItem history; history.key = QStringLiteral("history.clear"); history.section = app;
            history.label = QStringLiteral("Clear prompt history");
            history.detail = QStringLiteral("Forget pane recall and saved command suggestions");
            history.aliases = QStringLiteral("prompt history up arrow forget clear erase commands prompts");
            history.run = [this] {
                if (QMessageBox::question(this, QStringLiteral("Clear prompt history"),
                                          QStringLiteral("Forget every line the prompt box recalls with Up and all saved command suggestions?"),
                                          QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
                    return;
                QString error;
                if (!relay::prompthistory::clearAll(&error)) { notice(error, 8000); return; }
                RichEditor::forgetAllHistory();
                notice(QStringLiteral("Prompt history cleared."), 4000);
            };
            items << history;
        }

        // "Shortcut hints", "Shortcut preset" and "Shortcuts inside programs" are rows in Options.
        {
            // The full list with every key, in a pane that stays open beside the work (#MAGP):
            // what the palette's chord opened before the palette existed.
            PaletteItem list; list.key = QStringLiteral("help.shortcutList"); list.section = keys;
            list.label = QStringLiteral("Shortcut list");
            list.detail = QStringLiteral("Every action and its keys, in a pane beside this one");
            list.aliases = QStringLiteral("help keys keyboard shortcuts cheat sheet reference hotkeys list");
            list.agentSafe = true;
            list.run = [this] { toggleSettingsPane(true); };
            items << list;
        }
        items << actionItem(keys, QStringLiteral("Edit keyboard shortcuts…"), Keymap::instance().path(), QStringLiteral("keybindings.edit"));
        items << actionItem(keys, QStringLiteral("Reload keyboard shortcuts"), QString(), QStringLiteral("keybindings.reload"));
        if (Keymap::instance().hasOverrides()) {
            PaletteItem clear; clear.key = QStringLiteral("keybindings.clearOverrides"); clear.section = keys;
            clear.label = QStringLiteral("Clear custom overrides"); clear.detail = QStringLiteral("Use the preset's keys only");
            clear.run = [] { Keymap::instance().clearOverrides(); };
            items << clear;
        }
        // Every registered action is findable by name (#ACDG): what the rows above do not reach,
        // children included, gets a plain row from the registry.
        appendRegisteredActions(items);
        // Browse by task, with everyday actions before setup and maintenance. Keys are stable
        // even when labels change with pane state; absent contextual actions simply drop out.
        const QList<QPair<QString, QStringList>> groups{
            {agent, QStringLiteral("agent.newChat agent.continue agent.stop agent.planToggle agent.requests "
                                   "menu:agents agent.stopAllSubagents agent.resumeQueue agent.clearQueue "
                                   "agent:skills menu:aliases alias:save alias:import agent.internalsPane "
                                   "agent.thinkingPanel agent:last_turn").split(' ')},
            {QStringLiteral("Conversations"), QStringLiteral("sessions.open find.inView agent.info agent.recap "
                                   "agent.fork agent.compact agent.rewind agent.rewindCode agent.export").split(' ')},
            {QStringLiteral("Models"), QStringLiteral("menu:model agent.swap agent.flashAgent agent.localAgent "
                                   "menu:effort agent.modelRoles agent.modelKeys").split(' ')},
            {terminal, QStringLiteral("menu:mode input.toggle prompt.clear voice.toggle agent.screenshotPane "
                                   "control.human terminal.interrupt program.delegate "
                                   "control.program.agent control.program.human terminal.find "
                                   "terminal.promptPrevious terminal.promptNext links.step terminal.clear").split(' ')},
            {panes, QStringLiteral("tab.new window.new pane.splitRight pane.splitDown pane.splitLeft pane.splitUp "
                                   "tab.next tab.previous pane.equalize pane.moveLeft pane.moveRight pane.moveUp pane.moveDown "
                                   "pane.moveToNewTab pane.moveToBackground pane.runInBackground tab.moveToNewWindow pane.close closed.restore closed.list").split(' ')},
            {QStringLiteral("Files and projects"), QStringLiteral("files.explorer files.open board.open tests.open "
                                   "project.pick project.init project.detach").split(' ')},
            {QStringLiteral("Remote and sharing"), QStringLiteral("ssh.connect ssh.splitSameHost remote.pair "
                                   "pane.share pane.sharing remote.openShared remote.join").split(' ')},
            {QStringLiteral("Appearance"), QStringLiteral("pane.focusMode pane.autoDim pane.dimToggle pane.brighten "
                                   "pane.darken theme.folder theme.reload").split(' ')},
            {keys, QStringLiteral("help.shortcutList keybindings.edit keybindings.reload keybindings.clearOverrides hints.reset").split(' ')},
            {app, QStringLiteral("app.settings app.about logs.open conversations.rebuild history.clear windows.fresh").split(' ')}
        };
        QList<PaletteItem> ordered;
        for (const auto &group : groups) {
            for (const QString &key : group.second) {
                for (qsizetype i = 0; i < items.size(); ++i) {
                    if (items.at(i).key != key) continue;
                    PaletteItem item = items.takeAt(i);
                    item.section = group.first;
                    ordered << item;
                    break;
                }
            }
        }
        // New actions remain discoverable until they receive an explicit position above.
        ordered.append(items);
        return ordered;
    }
