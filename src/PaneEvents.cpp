// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Pane.h"

void Pane::handle(const QJsonObject &event) {
        const QString type = event.value(QStringLiteral("event")).toString();
        logEvent(type, event);
        // A shared pane's agent is watched from elsewhere too. The sidecar's allow-list decides
        // what actually reaches a device; this only offers it.
        relay::RemoteShare::instance().paneEvent(m_token, event);
        // --- subagents UI: subagent_* events are consumed; main-agent state is observed first ---
        if (type == QStringLiteral("configured"))
            QTimer::singleShot(0, this, [this] {
                refreshAgentDefinitions();
                m_lastProgramState = QJsonObject();   // a fresh worker knows nothing about the pane
                sendProgramState();
            });
        if (handleBoardEvent(type, event)) return;   // Switchboard (protocol 17)
        if (m_subagents.handle(event)) return;
        // --- end subagents UI ---
        if (m_jobs.handle(event)) return;   // the jobs list; reset/ready are observed and passed on
        if (type == QStringLiteral("job_output")) { openJobOutput(event); return; }
        if (handleProgramEvent(type, event)) return;    // the agent typing into this pane's program
        if (type == QStringLiteral("terminal_command")) { handleTerminalCommand(event); return; }   // protocol 22
        if (handleRequestsEvent(type, event)) return;   // request ledger UI
        if (handleObservabilityEvent(type, event)) return;
        if (handleSessionEvent(type, event)) return;
        if (handleAliasEvent(type, event)) return;   // aliases (issue G8DK)
        if (handleMemorySuggestionEvent(type, event)) return;   // Keep / No answered, and imports (#MEMS)
        if (type == QStringLiteral("ready")) {
            m_workerReady = true; requestRoute(false, QStringLiteral("auto"));
            // A conversation opened in a new pane with no saved text: its transcript comes off the
            // shared index, so it is asked for now. A pane whose harness is deferred is not
            // configured until its first prompt, and waiting for that left it empty (#0TJ9).
            if (!m_transcriptPending.isEmpty()) requestSavedTranscript(std::exchange(m_transcriptPending, QString()));
            if (m_restartConfigure) {
                m_restartConfigure = false;
                m_configuring = true;
                send(m_lastConfigure);
            }
            send({{"type", "presets"}});
            // Protocol 34 (#MEMS): Claude Code and Codex memories are offered when Relay starts,
            // not at this pane's first configure, which a guest ranked first defers to its first prompt.
            send({{"type", "memory_import"}, {"enabled", QSettings().value(QStringLiteral("memory/import_guests"), true).toBool()}});
            refreshAliases();   // the palette and `/name` need the list before anything is typed
        } else if (type == QStringLiteral("route")) {
            const QString id = event.value(QStringLiteral("id")).toString();
            if (takeRemoteRoute(id, event)) return;
            const QString route = event.value(QStringLiteral("route")).toString();
            const bool needsAssist = event.value(QStringLiteral("needs_assist")).toBool()
                && (id == m_pendingSubmit ? m_submitMode : m_modeValue) == QStringLiteral("auto");
            const QString routedText = event.value(QStringLiteral("text")).toString(id == m_pendingSubmit ? m_submittedDraft : m_editor->toPlainText());
            const bool haveAssist = needsAssist && m_assistText == routedText && !m_assistRoute.isEmpty();
            if (id == m_previewId && needsAssist) {
                if (haveAssist) showAssistLabel();
                else {
                    // Show the local guess now; "checking…" only if the model has not answered within ~150 ms.
                    const QString why = event.value(QStringLiteral("assist_reason")).toString(event.value(QStringLiteral("reason")).toString());
                    const QString guess = route == QStringLiteral("shell") ? QStringLiteral("TERMINAL") : QStringLiteral("AGENT");
                    setRouteText(QStringLiteral("%1 · local guess · %2").arg(guess, why));
                    m_routeLabel->setToolTip(why);
                    QTimer::singleShot(150, this, [this, text = routedText, guess, why] {
                        if (m_editor->toPlainText() == text && !(m_assistText == text && !m_assistRoute.isEmpty()) && m_assistFailedText != text)
                            setRouteText(QStringLiteral("AUTO · checking… · local guess: %1 · %2").arg(guess.toLower(), why));
                    });
                    m_assistQueuedText = routedText;
                    m_assistLocalGuess = route;
                    m_assistDebounce.start();
                }
            } else if (id == m_pendingSubmit && haveAssist) {
                showAssistLabel();
            } else if (id == m_previewId || id == m_pendingSubmit) {
                if (route == QStringLiteral("shell") && !event.value(QStringLiteral("valid")).toBool(true))
                    setRouteText(QStringLiteral("TERMINAL · ") + event.value(QStringLiteral("invalid_reason")).toString()
                                         + (event.value(QStringLiteral("agent_signal")).toBool()
                                                ? QStringLiteral(" · this reads like a request for the agent")
                                                : QStringLiteral(" · the agent will fix it")));
                else
                    setRouteText(route.toUpper() + QStringLiteral(" · ") + event.value(QStringLiteral("reason")).toString());
                m_routeLabel->setToolTip(event.value(QStringLiteral("syntax_error")).toString());
            }
            if (id == m_pendingSubmit) {
                m_pendingSubmit.clear();
                if (m_editor->toPlainText() != m_submittedDraft) {
                    status(QStringLiteral("Input changed during routing; submit again to use the current text.")); return;
                }
                if (haveAssist) { dispatch(withAssistedRoute(event), m_submitMode); return; }
                if (needsAssist) {
                    // Wait briefly for the model's opinion; the local guess wins after 400 ms.
                    m_heldDecision = event; m_heldMode = m_submitMode;
                    if (m_assistInflightText != routedText) sendRouteAssist(routedText);
                    m_assistHold.start();
                    return;
                }
                dispatch(event, m_submitMode);
            }
        } else if (type == QStringLiteral("configured")) {
            m_configured = true; m_configuring = false;
            syncTerminalContext();
            m_configureAutoRetried = false;
            m_model = event.value(QStringLiteral("model")).toString();
            m_skillCount = event.value(QStringLiteral("skills")).toInt();
            setSkillCommands(event.value(QStringLiteral("skill_commands")).toArray());
            // Model roles (protocol 13): the worker reports the effective model of every role and
            // which role this pane runs (a role that could not be used falls back to "main").
            m_roleSummary = event.value(QStringLiteral("roles")).toObject();
            // What this pane was configured on — its OWN model, which is `roles.main` (13.4), not
            // the model of whatever role it started on. `model_changed` has followed that rule
            // since #MDL1 ("a role's model never rewrites the pane's own key"); `configure` did
            // not, so a pane restored on /flash came back with the flash model as its main row.
            const QString ownModel = m_roleSummary.value(QStringLiteral("main")).toObject()
                                         .value(QStringLiteral("model")).toString();
            m_paneModel = ownModel.isEmpty() ? m_model : ownModel;
            m_tierSummary = event.value(QStringLiteral("tiers")).toObject();
            if (onRolesResolved) onRolesResolved();
            m_agentRole = event.value(QStringLiteral("agent_role")).toString(QStringLiteral("main"));
            // Protocol 33: the worker echoes the context block back, which is how a console
            // confirms that the surface it is drawn on was understood — the role it settled on,
            // the tool scope it named, the store it chose. The role is read from it when the
            // top-level field is missing, and the rest is kept for `session_info` and the log.
            m_workerContext = event.value(QStringLiteral("context")).toObject();
            if (!m_workerContext.isEmpty() && !event.contains(QStringLiteral("agent_role")))
                m_agentRole = m_workerContext.value(QStringLiteral("agent_role"))
                                  .toString(QStringLiteral("main"));
            // A restored pane's own model for the mode it came back on (card #MDL1): `configure`
            // carries the role, and the role alone resolves off the tier list, so the pick has to
            // follow. It keeps the conversation, like every other model switch.
            if (const QString mode = relay::modelrows::roleTier(m_agentRole);
                mode != QStringLiteral("main") && m_modePick.contains(mode)) {
                const relay::modelrows::ModePick pick = m_modePick.value(mode);
                if (!pick.key.isEmpty() && pick.key != currentEntryKey()) sendAgentRole(m_agentRole, pick);
            }
            onSessionConfigured(event);
            if (!m_pendingAgentMode.isEmpty()) {
                m_agentMode = m_pendingAgentMode;
                m_pendingAgentMode.clear();
                if (m_agentMode == QStringLiteral("plan")
                    && relay::rolestore::rankedOverrideSet(QStringLiteral("planning"))) {
                    setAgentRole(QStringLiteral("high"), false);
                } else if (m_agentMode == QStringLiteral("plan")) {
                    const relay::models::Entry drawn = relay::models::drawTier(modelCatalog(), QStringLiteral("high"));
                    if (!drawn.key.isEmpty()) {
                        QString effort;
                        for (const auto &item : relay::models::curation::activeTierList(QStringLiteral("high")))
                            if (item.key == drawn.key) { effort = item.effort; break; }
                        setAgentRole(QStringLiteral("high"), false, {drawn.key, effort});
                    } else setAgentRole(QStringLiteral("high"), false);
                }
                send({{"type", "set_mode"}, {"mode", m_agentMode}});
            }
            noteGuestPreset(event);   // Tier A (29.4): the guest is this pane's agent from here on
            discloseHosted();   // Relay Free: where the prompts go, said once per installation
            runBoardTask();   // a card handed over by the Switchboard's Execute, if any (#XS6Q)
            // The prompt that started a deferred harness is sitting in the queue waiting for
            // exactly this event (card #MDL1); pumpQueue does nothing when there is none.
            QTimer::singleShot(0, this, [this] { pumpQueue(); });
            // No "Agent ready · <model>" here: the composer's own chips carry the model and the
            // agent role, so announcing it again only filled the window with a permanent line.
            changed();
        } else if (type == QStringLiteral("presets")) {
            m_presets = event.value(QStringLiteral("presets")).toArray();
            // Tier defaults and the Advanced action list come from the worker so the GUI never has
            // to keep a second copy of backend/relay_core/presets.py in step (protocol 13.7).
            // The two defaults the worker computed for the five lists (owner, 2026-09-20). A fresh
            // install takes one at once — with the OpenRouter twins when that key is stored, which
            // is the recommended one — so no list is ever empty by accident.
            m_tierListDefaults = event.value(QStringLiteral("tier_list_defaults")).toObject();
            if (!relay::models::curation::tierListsSet() && !m_tierListDefaults.isEmpty()) {
                const QJsonObject withOpenrouter = m_tierListDefaults.value(QStringLiteral("openrouter")).toObject();
                const QJsonObject plain = m_tierListDefaults.value(QStringLiteral("plain")).toObject();
                const QJsonObject chosen = withOpenrouter.isEmpty() ? plain : withOpenrouter;
                if (!chosen.isEmpty()) relay::models::curation::applyTierDefaults(chosen);
            }
            m_stored.clear();
            bool hostedUnavailable = false;
            for (const auto &item : m_presets) {
                const auto preset = item.toObject();
                // A model server on this machine (card #24XJ) needs no key, so `local` makes a row
                // selectable just as a stored key does. The worker appends those rows after the
                // keyed presets, so they stay last in this list. Relay Free (`hosted`, protocol
                // 13.8) needs none either, while the worker reports it `available`.
                const bool hosted = preset.value(QStringLiteral("hosted")).toBool();
                if (preset.value(QStringLiteral("id")).toString() == QStringLiteral("relay-free")
                    && !preset.value(QStringLiteral("available")).toBool()) hostedUnavailable = true;
                // A guest (protocol 29.3) needs no key either: `harness` is the worker saying it
                // can run this guest's headless harness here, which is what makes the row usable.
                // A guest row without it is not a row of this box at all — the picker offers that
                // guest the Tier B way (26.9) instead.
                if (hosted ? preset.value(QStringLiteral("available")).toBool()
                    : (preset.value(QStringLiteral("has_stored_key")).toBool()
                       || preset.value(QStringLiteral("local")).toBool()
                       || preset.value(QStringLiteral("harness")).toBool()))
                    m_stored.append({preset.value(QStringLiteral("id")).toString(), preset.value(QStringLiteral("label")).toString()});
                // The allowance the worker last saw, so the chip has a figure before the first call.
                if (hosted && preset.value(QStringLiteral("quota")).isObject()) setHostedQuota(preset.value(QStringLiteral("quota")).toObject());
            }
            rememberFallback(modelCatalog());   // the failover's first candidate follows the catalog
            if (m_keysDialog) m_keysDialog->setPresets(providerPresets());
            // A presets event that arrives after the first one can change what a settings pane
            // shows — the codex catalogue landing turns Options' Codex Model row from its text
            // field into the dropdown (#E516) — so an open Options re-renders from the fresh rows.
            relay::SettingsWatch::instance().notify();
            changed();
            // A pane opened for a guest sessions row (29.4) could not know until now whether the
            // guest can be its agent. It does now: the preset, or the Tier B launch.
            if (!m_pendingGuestResume.guest.isEmpty()) {
                const PendingGuestResume pending = m_pendingGuestResume;
                m_pendingGuestResume = PendingGuestResume();
                startGuestPreset(pending.guest, pending.extra, pending.cwd);
            }
            // …and so could a pane opened by Verify for a guest verifier (#T71W).
            if (!m_pendingGuestTask.guest.isEmpty()) {
                const PendingGuestTask pending = m_pendingGuestTask;
                m_pendingGuestTask = PendingGuestTask();
                startGuestBoardTask(pending.guest, pending.task, pending.card);
            }
            if (m_stored.isEmpty()) {
                status(hostedUnavailable
                           ? QStringLiteral("No stored provider keys, and Relay Free needs python3-cryptography. "
                                            "Open Options › Models › API keys… to add a key or import from Warp.")
                           : QStringLiteral("No stored provider keys. Open Options › Models › API keys… to add one or import from Warp."));
                return;
            }
            if (!m_configured && !m_configuring) {
                auto usable = [this](const QString &id) {
                    return std::any_of(m_stored.cbegin(), m_stored.cend(), [&](const auto &entry) { return entry.first == id; });
                };
                // Card #MDL1, rule 3: "a pane runs on rank 1 of the main list until you pick
                // something else in that pane". `startEntry` is that rule — this pane's own saved
                // entry while it can still run, else rank 1 of the main list with **its model and
                // its level**, guests included (owner, 2026-09-21: a harness he ranked first is
                // what a new pane starts on). Until now this read `provider/preset`, which every
                // switch in every pane rewrites, and then took that preset's *default* model: pick
                // glm-5.3-flash in one pane and the next one opened on glm-5.3.
                const relay::models::StartChoice start =
                    relay::models::startEntry(modelCatalog(), m_restorePreset, m_restoreModel);
                QString choice = start.entry.preset, startModel = start.entry.model;
                m_restorePreset.clear();
                m_restoreModel.clear();
                if (!usable(choice)) { choice.clear(); startModel.clear(); }
                // No catalog can answer yet — an older worker, or an install with a key for
                // nothing it lists. Then the ladder that always answered: the saved choice, Warp's
                // default agent model, the first stored key, Relay Free, a local endpoint. A key
                // of the user's own always wins over the included allowance, so a user with any
                // BYOK key is untouched and a fresh install lands on Relay Free and configures at
                // once. A local endpoint never wins over either (card #24XJ): it is picked on its
                // own only when it is the saved preset, or when nothing else is usable at all.
                // This is the one reader of `provider/preset` left in a pane: the last provider
                // this install used, for an install whose main list is empty.
                if (choice.isEmpty()) choice = QSettings().value(QStringLiteral("provider/preset")).toString();
                if (!usable(choice)) choice = event.value(QStringLiteral("warp_default")).toString();
                if (!usable(choice)) {
                    auto firstWhere = [this](auto predicate) {
                        const auto found = std::find_if(m_stored.cbegin(), m_stored.cend(), [&](const auto &entry) {
                            return predicate(presetById(entry.first));
                        });
                        return found == m_stored.cend() ? QString() : found->first;
                    };
                    // A guest row is never picked for a pane that asked for nothing (29.4):
                    // starting Claude Code because it happens to be installed is not a default.
                    // It is reached only as the restored or saved choice above, or by hand.
                    auto guestRow = [](const QJsonObject &preset) { return preset.value(QStringLiteral("harness")).toBool(); };
                    choice = firstWhere([&](const QJsonObject &preset) {
                        return !preset.value(QStringLiteral("local")).toBool() && !preset.value(QStringLiteral("hosted")).toBool()
                               && !guestRow(preset);
                    });
                    if (choice.isEmpty()) choice = firstWhere([](const QJsonObject &preset) { return preset.value(QStringLiteral("hosted")).toBool(); });
                    if (choice.isEmpty()) choice = firstWhere([&](const QJsonObject &preset) { return !guestRow(preset); });
                }
                if (choice.isEmpty()) {   // guests only: the pane has no provider of its own yet
                    status(QStringLiteral("No stored provider keys. Open Options › Models › API keys… to add one or import from Warp."));
                    return;
                }
                // The level rank 1 carries in the main list. With none, the pane keeps the global
                // default it read when it was built (`agent/effort`), exactly as before.
                if (!start.effort.isEmpty()) m_effort = start.effort;
                // A harness the owner ranked first is what a new pane starts on — "with the
                // harness process starting on the first turn, not when the pane opens" (owner,
                // 2026-09-21, card #MDL1). The worker spawns the CLI inside `configure`
                // (backend/worker.py, protocol 29.3), so deferring the spawn is deferring the
                // configure: the pane takes the preset now, says so, and configures when there is
                // something to ask. Every other door — the box, /model, the picker, a guest
                // sessions row — configures at once, because each of those *is* somebody asking.
                if (const QString guest = guestOfPreset(choice); !guest.isEmpty() && m_initialState.isEmpty()) {
                    m_deferredPreset = choice;
                    m_deferredModel = startModel;
                    m_paneModel = startModel;   // show the ranked model before the guest starts
                    m_currentPreset = choice;   // the box says what this pane is on
                    relay::log::info(QStringLiteral("harness_deferred pane=%1 preset=%2 model=%3").arg(paneLogId(), choice, startModel));
                    status(QStringLiteral("%1 is this pane's agent; it starts on your first prompt.")
                               .arg(guestName(guest)));
                    changed();
                    // Board Run may have handed this new pane its first task before presets
                    // arrived. That task is the first prompt, so start the held harness now.
                    if (!m_boardTask.isEmpty()) startDeferred();
                    return;
                }
                configurePreset(choice, false, startModel);
            }
        } else if (type == QStringLiteral("warp_imported") || type == QStringLiteral("opencode_imported")) {
            const auto imported = event.value(QStringLiteral("imported")).toArray();
            const auto skipped = event.value(QStringLiteral("skipped")).toArray();
            QStringList names;
            for (const auto &item : imported) names << item.toObject().value(QStringLiteral("name")).toString();
            const QString source = type == QStringLiteral("warp_imported") ? QStringLiteral("Warp") : QStringLiteral("OpenCode");
            QMessageBox::information(this, QStringLiteral("Import keys"),
                QStringLiteral("Imported %1 key(s) into the keyring: %2\nSkipped: %3").arg(imported.size())
                    .arg(names.join(QStringLiteral(", ")), skipped.isEmpty() ? QStringLiteral("none") : QString::number(skipped.size())));
            status(source + QStringLiteral(" import finished. Leave the key field empty to use stored keys."));
            send({{"type", "presets"}});
        } else if (type == QStringLiteral("transcribed")) {
            onTranscribed(event);
        } else if (type == QStringLiteral("app_command")) {
            // The window does it and answers; the answer goes back down this pane's own pipe
            // (#FEJQ, §30.3). A pane whose window has gone still answers, so the worker's tool
            // call ends in an error rather than in the 20-second `no_reply` deadline. The tab's
            // helper worker is answered the same way from its own pipe (RelayWindow::boardWorker),
            // which is why building the answer is `appcommands::answerFor` and not two copies.
            send(relay::appcommands::answerFor(event, onAppCommand));
        } else if (type == QStringLiteral("keybindings_updated")) {
        } else if (type == QStringLiteral("custom_provider_saved") || type == QStringLiteral("custom_provider_deleted")) {
            const QString error = event.value(QStringLiteral("error")).toString();
            const QString name = event.value(QStringLiteral("provider")).toObject().value(QStringLiteral("name")).toString();
            if (!error.isEmpty()) status(QStringLiteral("Custom provider: ") + error);
            else if (type == QStringLiteral("custom_provider_saved")) status(QStringLiteral("Custom provider saved: %1").arg(name));
            else status(QStringLiteral("Custom provider removed."));
            relay::SettingsWatch::instance().notify();   // the row appears or goes as the fresh presets land
        } else if (type == QStringLiteral("key_stored")) {
            status(event.value(QStringLiteral("preset")).toString() == QStringLiteral("relay-pro")
                ? QStringLiteral("Relay Pro access confirmed; code saved to the keyring.")
                : QStringLiteral("API key saved to the keyring for ") + event.value(QStringLiteral("preset")).toString());
            refreshPresets();   // Options › Models: the provider's models become usable rows
        } else if (type == QStringLiteral("queued")) {
            const QString requestId = event.value(QStringLiteral("request_id")).toString();
            const QString itemId = event.value(QStringLiteral("id")).toString();
            if (m_pendingPrompts.contains(requestId)) m_itemPrompts.insert(itemId, m_pendingPrompts.take(requestId));
            // A line this console handed to its context has no request id of the pane's to come
            // back on — `board_ask` is the context's message, not the pane's — so the item it
            // became is matched in order, oldest first, and only on this console's own surface.
            // It is kept apart from `m_itemPrompts`, which is what `agent_started` prints its
            // "✦ …" line from: a card's question is already on the thread above the console.
            else if (!itemId.isEmpty() && !m_contextSubmits.isEmpty() && !queueSurface().isEmpty()
                     && event.value(QStringLiteral("surface")).toString() == queueSurface())
                m_workerPrompts.insert(itemId, m_contextSubmits.takeFirst());
            if (requestId == m_turnCardAsk) m_turnCardItem = itemId;   // the chip's turn has an item now (#C7PF)
        } else if (type == QStringLiteral("queue_changed")) {
            m_runningItem = event.value(QStringLiteral("running")).toString();
            m_queuePaused = event.value(QStringLiteral("paused")).toBool();
            m_workerItems = workerRowsIn(event, QStringLiteral("items"));
            m_workerSteering = workerRowsIn(event, QStringLiteral("steering"));
            for (const QList<WorkerRow> *group : {&m_workerItems, &m_workerSteering})
                for (const WorkerRow &row : *group) m_workerPreviews.insert(row.id, row.preview);
            // Forget prompts that are neither running nor queued any more (removed or cleared).
            const auto gone = [this](const QString &id) {
                const auto named = [&id](const WorkerRow &row) { return row.id == id; };
                return !std::any_of(m_workerItems.cbegin(), m_workerItems.cend(), named)
                    && !std::any_of(m_workerSteering.cbegin(), m_workerSteering.cend(), named)
                    && id != m_runningItem && id != m_currentItem;
            };
            for (auto it = m_itemPrompts.begin(); it != m_itemPrompts.end();)
                if (gone(it.key())) it = m_itemPrompts.erase(it); else ++it;
            for (auto it = m_workerPrompts.begin(); it != m_workerPrompts.end();)
                if (gone(it.key())) it = m_workerPrompts.erase(it); else ++it;
            for (auto it = m_workerPreviews.begin(); it != m_workerPreviews.end();)
                if (gone(it.key())) it = m_workerPreviews.erase(it); else ++it;
            // The selected row may have started, been withdrawn or been taken by another device.
            if (!m_selectedWorkerRow.isEmpty() && gone(m_selectedWorkerRow)) {
                m_selectedWorkerRow.clear();
                m_editor->clear();   // what was in the box was the row's, not the user's
            }
            rebuildQueueStrip();
            changed();
        } else if (type == QStringLiteral("agents") && m_agentsListPending) {
            m_agentsListPending = false;
            ensureLineStart();
            const QJsonArray items = event.value(QStringLiteral("items")).toArray();
            printInline(QStringLiteral("%1 agent definition(s):\n").arg(items.size()), Ink::Note);
            for (const auto &value : items) {
                const QJsonObject item = value.toObject();
                printInline(QStringLiteral("  %1 · %2 (%3)\n").arg(item.value(QStringLiteral("name")).toString(),
                            item.value(QStringLiteral("description")).toString().left(90), item.value(QStringLiteral("source")).toString()), Ink::ToolOutput);
            }
            closeInline();
        } else if (type == QStringLiteral("interrupting")) {
            status(QStringLiteral("Interrupting the current agent turn…"));
        } else if (type == QStringLiteral("agent_started")) {
            // Busy follows agent_started/agent_finished: the next queued turn may start right after done.
            m_agentBusy = true; m_turnHeader = false; m_turnText.clear();
            m_idleRecap.stop(); m_idleSince.invalidate();   // a running turn is not idle work to recap
            m_shareFailed = m_shareFinished = false;   // the pane is in use again; the last turn is history
            startTurnClock();
            m_currentItem = event.value(QStringLiteral("id")).toString();
            QTimer::singleShot(0, this, [this] { rebuildQueueStrip(); });
            const PendingPrompt prompt = m_itemPrompts.value(m_currentItem);
            m_fixAwaitingAgent = prompt.fix;
            m_currentRequest = prompt.text;   // the Activity pane names the turn by it (#QT8C)
            // Wrong-mode hints: the shell command this turn was submitted with, if any, so a
            // failing run_command of the same text can suggest the terminal (see tool_result).
            m_turnShellPrompt = prompt.shellText;
            m_modeHintShown = false;
            m_runCommands.clear();
            if (!prompt.program.isEmpty()) m_transcriptProgram = prompt.program;
            if (prompt.handoff) {
                ensureLineStart();
                printInline(QStringLiteral("✦ the command finished · its result went to the agent\n"), Ink::Note);
            } else if (!prompt.fix && !prompt.text.isEmpty()) {
                ensureLineStart();
                printInline(QStringLiteral("✦ ") + prompt.text + '\n', Ink::UserAgent);
                printAttachmentThumbnails(prompt.text);   // #1MGS
                if (!prompt.why.isEmpty()) printInline(prompt.why + '\n', Ink::Note);
            }
        } else if (type == QStringLiteral("agent_finished")) {
            const QString outcome = event.value(QStringLiteral("outcome")).toString();
            const bool stopped = outcome == QStringLiteral("cancelled") || outcome == QStringLiteral("error");
            if (m_interruptPending && outcome == QStringLiteral("cancelled")) {
                m_interruptPending = false;   // replaced by the interrupting prompt; keep the queue going
            } else if (stopped) {
                pauseQueue(outcome == QStringLiteral("error") ? QStringLiteral("the agent turn failed") : QStringLiteral("the agent was stopped"));
            }
            if (m_activeValid && m_active.agent) m_activeValid = false;
            QTimer::singleShot(0, this, [this] { pumpQueue(); rebuildQueueStrip(); });
            // The turn, handed to whatever this console is about (#AGNT step 3). A terminal
            // context wants nothing — the answer is already in the transcript — so this does
            // nothing at all here; it is the seam a card's Discuss turn is written to its thread
            // through (card #AGNT decision 2), and it is built before m_itemPrompts drops the
            // prompt below.
            {
                relay::agent::TurnRecord record;
                record.id = event.value(QStringLiteral("id")).toString();
                record.surface = contextSpec().surface;
                record.prompt = m_itemPrompts.value(record.id).text;
                record.answer = m_turnText;
                record.model = m_model;
                record.sessionId = m_sessionId;
                record.turnId = m_lastTurnId;
                record.outcome = outcome;
                contextTurnFinished(record);
            }
            // The reply /speak reads, and Options › Voice › "Read replies aloud automatically" (#MDA7).
            if (!m_turnText.trimmed().isEmpty()) {
                m_lastReply = m_turnText;
                if (outcome == QStringLiteral("done") && readRepliesAloud()) readAloud(m_lastReply);
            }
            m_itemPrompts.remove(event.value(QStringLiteral("id")).toString());
            m_turnShellPrompt.clear();
            m_runCommands.clear();
            if (m_currentItem == event.value(QStringLiteral("id")).toString()) m_currentItem.clear();
            m_agentBusy = !m_runningItem.isEmpty() && m_runningItem != event.value(QStringLiteral("id")).toString();
            if (!m_agentBusy) { stopTurnClock(); m_idleTip.start(); }
            // Status glyphs (#XM0T): the window reads these to show done / failed / needs you
            // until the user has looked at the pane.
            ++m_finishSerial; m_lastOutcome = outcome;
            // The header's card chip goes with the turn it named (#C7PF). Matched on the item so
            // that only this turn's finish takes it down, never the finish of a turn an
            // interrupting prompt has already replaced.
            if (!m_turnCardItem.isEmpty() && event.value(QStringLiteral("id")).toString() == m_turnCardItem) {
                m_turnCard.clear(); m_turnCards.clear(); m_turnCardAsk.clear(); m_turnCardItem.clear();
                refreshCardChip();
            }
            // What a phone is told (shareStatus, protocol 6.3) and what rings it
            // (remote/notify.py's `failed` trigger). "cancelled" is the user stopping their own
            // turn, which is not news to them.
            m_shareFailed = outcome == QStringLiteral("error");
            m_shareFinished = outcome == QStringLiteral("done");
            // "Asked" also covers a command the agent left in the prompt box and waits on.
            m_lastAsked = outcome == QStringLiteral("done")
                          && (relay::panestatus::endsWithQuestion(m_turnText) || (m_handoffOffered && m_handoffPrefill));
            if (outcome == QStringLiteral("done")
                && (event.value(QStringLiteral("awaiting_reply")).toBool()
                    || relay::panestatus::endsWithQuestion(m_turnText)))
                pauseQueue(QStringLiteral("the agent asked a question · reply to continue"));
            // Notified only for news the user was not watching (#XM0T): a turn that ended by
            // asking them something, a finished turn, a failed one.
            if (outcome == QStringLiteral("done")) {
                ++m_turnsCompleted;
                if (window() && !window()->isActiveWindow()) m_finishedWhileAway = true;
                if (!watched() && !moreTurnsPending()
                    && !(window() && window()->property("backgroundSession").toBool()))
                    notify(m_lastAsked ? QStringLiteral("Agent needs you") : QStringLiteral("Agent finished"), turnSummary(),
                           m_lastAsked ? relay::NotificationCenter::kindWarning : relay::NotificationCenter::kindSuccess);
            } else if (outcome == QStringLiteral("error") && !watched()
                       && !(window() && window()->property("backgroundSession").toBool())) {
                notify(QStringLiteral("Agent turn failed"), turnSummary(), relay::NotificationCenter::kindError);
            }
            if (!m_agentBusy && !moreTurnsPending()) { ensureLineStart(); closeInline(); }
            // The idle-pane recap's anchor (card #D54R): the wait starts when the work ended, not
            // when the pane stopped being watched, so looking away late still fires on time.
            if (outcome == QStringLiteral("done") && !m_agentBusy && !moreTurnsPending()) {
                m_idleSince.start();
                updateIdleRecap();
            }
            if (outcome == QStringLiteral("done") && !m_agentBusy && !moreTurnsPending()
                && QSettings().value(QStringLiteral("suggestions/next_prompt"), true).toBool())
                QTimer::singleShot(300, this, [this] { requestSuggestion(QStringLiteral("next_prompt")); });
            changed();
        } else if (type == QStringLiteral("delta")) {
            const QString text = event.value(QStringLiteral("text")).toString();
            m_turnText += text;
            turnHeader(); printInline(text, Ink::Agent);
        } else if (type == QStringLiteral("tool_output")) {
            // Collapsed by default (BOARD-DESIGN.md 4.3): a tool's output is counted, not
            // poured into the pane. Card #X5D1 read an earlier owner decision as "print all of it";
            // the owner corrected that on 2026-09-17. What the count is for changed with #TK9C: the
            // call's own line carries it live, and its fold holds the text. Agent options › Show
            // tool output brings the stream back — and then the line is final where it stands,
            // because the output below it is where the cursor now is.
            // Two shapes on the wire since #PPR4 (§ 23.10): the text, or — while nothing here is
            // reading it, which needsToolOutputText() decides and the worker is told — the counts
            // the worker made from exactly that text. toolOutputCount() reads either, so the row's
            // live count is the same number in both and no surface has to know which arrived.
            const relay::calllines::OutputCount count = relay::calllines::toolOutputCount(event);
            m_toolLines += count.lines;
            m_toolPartialLine = count.partial;
            // The shape that arrived is not the shape this pane needs: ask again. Options tells
            // only the *active* pane that Show tool output was toggled (`toggleRow`,
            // src/RelayWindow.h), so a second pane would otherwise keep counting — or keep being
            // sent text nobody reads — until its next configure. The setting is
            // read once for both decisions — it is a QSettings construction on this path and that
            // was 5 % of a tool-heavy turn (#057J) — and sendToolStreamOption() sends nothing when
            // the worker already agrees, so a worker too old to know the option is not nagged.
            const bool show = showToolOutput();
            if (count.counted == needsToolOutputText(show)) sendToolStreamOption();
            if (m_internals) internalsToolOutput(count.lines);   // the row lives in the Activity pane (#QT8C)
            // `!count.counted` is the moment the switch is thrown mid-call: the worker has not read
            // the new option yet, so this chunk has no text to print and the row below counts it.
            else if (show && !count.counted) {
                turnHeader(); printInline(event.value(QStringLiteral("text")).toString(), Ink::ToolOutput);
            }
            else if (!m_liveCall.isEmpty() && shellIdleAtPrompt()) {
                // The running row counts what the command has printed. Throttled to about ten
                // rewrites a second: a build that prints a thousand lines must not repaint a row a
                // thousand times.
                if (!m_liveTick.isValid() || m_liveTick.elapsed() >= 100) {
                    m_liveTick.restart();
                    const int lines = m_toolLines + (m_toolPartialLine ? 1 : 0);
                    const relay::calllines::Step step =
                        m_callCursor.live(m_liveCall, m_liveLabel, lines, callLineCells());
                    if (!step.nothing) drawCallRow(step, m_liveTurn, callAnchor(step, m_liveTurn, m_liveLabel), m_liveLabel);
                }
            }
        } else if (type == QStringLiteral("tool_started")) {
            turnHeader();
            m_toolLines = 0; m_toolPartialLine = false;
            const QString call = event.value(QStringLiteral("call_id")).toString();
            const QString turn = event.value(QStringLiteral("turn_id")).toString();
            const relay::toollabel::Label label = relay::toollabel::fromEvent(event);
            // Wrong-mode hints are unchanged: they key off this run_command's full text, kept by
            // call_id for the tool_result handler. § 23.1 still sends `preview`, which is where the
            // text is; nothing else is parsed out of it any more.
            if (event.value(QStringLiteral("tool")).toString() == QStringLiteral("run_command")
                || label.kind == QStringLiteral("run") || label.kind == QStringLiteral("job")) {
                const QString command = runCommandFromPreview(event.value(QStringLiteral("preview")).toString());
                if (!command.isEmpty()) m_runCommands.insert(call, command);
            }
            m_liveCall = call; m_liveTurn = turn; m_liveLabel = label;
            m_liveTick.invalidate();
            if (m_agentBusy) tickTurnClock();   // the busy line says this call's gerund now (#4E13)
            // The two calls that block the main agent on the background work it started, so that
            // the prompt box can say so until the call lands: agent_wait (#V7QD) and command_output,
            // which waits on a job (#KP4M).
            const QString liveTool = event.value(QStringLiteral("tool")).toString();
            if (liveTool == QStringLiteral("agent_wait") || liveTool == QStringLiteral("command_output")) {
                (liveTool == QStringLiteral("agent_wait") ? m_waitCall : m_jobWaitCall) = call;
                refreshBackgroundWait();
                tickTurnClock();
            }
            // Deferred while a program owns the terminal: a pending line can only be replayed in
            // its final form, so nothing is drawn until the result arrives. With the internals
            // pane open the row is drawn there instead, and nothing here (#QT8C).
            if (m_internals) internalsToolStarted(event);
            else if (shellIdleAtPrompt()) {
                m_callCursor.setCells(callLineCells());
                beginBlock(relay::gaps::Block::Call);   // a blank line after prose, none inside a run (#5AWD)
                const relay::calllines::Step step = m_callCursor.start(call, label);
                if (!step.nothing) drawCallRow(step, turn, callAnchor(step, turn, label), label);
                // With the stream on, the output goes under the line: the row is finished here.
                if (showToolOutput()) endCallRun();
            }
        } else if (type == QStringLiteral("tool_result")) {
            const auto result = event.value(QStringLiteral("result")).toObject();
            const QString call = event.value(QStringLiteral("call_id")).toString();
            const QString turn = event.value(QStringLiteral("turn_id")).toString();
            // Wrong-mode hints: a run_command that failed, while in agent mode, whose text is the
            // prompt the turn started from, means the submission was a shell command in the wrong
            // mode. At most once per turn.
            if (event.value(QStringLiteral("tool")).toString() == QStringLiteral("run_command")) {
                const QString runText = m_runCommands.take(call);
                if (!m_modeHintShown && !runText.isEmpty() && result.value(QStringLiteral("exit_code")).toInt() != 0
                    && m_modeValue == QStringLiteral("agent") && relay::input::commandMatchesPrompt(runText, m_turnShellPrompt)) {
                    m_modeHintShown = true;
                    wrongModeHint(false);
                }
            }
            const relay::toollabel::Label label = relay::toollabel::fromEvent(event);
            const QString diff = event.value(QStringLiteral("diff")).toString();
            m_toolLines = 0; m_toolPartialLine = false;
            m_liveCall.clear();
            if (m_agentBusy) tickTurnClock();   // between calls, the busy line says "thinking" again (#4E13)
            if (!call.isEmpty() && (m_waitCall == call || m_jobWaitCall == call)) {
                // The agent_wait (#V7QD) or the command_output (#KP4M) returned.
                if (m_waitCall == call) m_waitCall.clear(); else m_jobWaitCall.clear();
                refreshBackgroundWait();
                tickTurnClock();
            }
            if (m_internals) {
                internalsToolResult(event, call, turn, label, diff);   // the pane's row, and the ledger's (#QT8C)
            } else if (!shellIdleAtPrompt()) {
                // Deferred: the finished line, as plain text. It carries no anchor, because a line
                // replayed by flushInline() cannot be rewritten and nothing would fold under it.
                ensureLineStart();
                printInline(QStringLiteral("▸ ") + label.line() + QLatin1Char('\n'),
                            label.failed() && !label.refused ? Ink::Error : Ink::Tool);
            } else {
                beginBlock(relay::gaps::Block::Call);   // already Call after its start: no gap (#5AWD)
                m_callCursor.setCells(callLineCells());
                const relay::calllines::Step step = m_callCursor.result(call, label, callLineCells());
                const QString anchor = callAnchor(step, turn, label);
                drawCallRow(step, turn, anchor, label);
                CallRecord record;
                record.turnId = turn;
                record.label = label;
                record.diff = diff;
                record.merged = step.merged;
                record.members = m_callCursor.members();
                record.callIds.clear();
                for (relay::calllines::RunMember &member : record.members) {
                    record.callIds << member.call;
                    if (!member.path.isEmpty() && !member.path.startsWith(QLatin1Char('/')))
                        member.path = QDir(m_workspace).filePath(member.path);   // the fold's links open files
                }
                if (record.callIds.isEmpty()) record.callIds << call;
                rememberCall(anchor, record);
                // A small diff (at most 12 changed lines, § 23.2) stays folded under the row
                // (#WXT6): foldRequested() answers from the stored diff when it is clicked, so
                // nothing is printed here and the row is one collapsed line.
            }
            // A fact the agent learned waits for the user here, under its call (#MEMS).
            printMemorySuggestion(relay::globals::suggestionFromToolResult(event));
        } else if (type == QStringLiteral("vision_route")) {
            // Image context (protocol 17): this turn runs on another model because the pane's own
            // cannot read images. Said plainly, because the answer comes from a different model.
            ensureLineStart();
            printInline(QStringLiteral("🖼 ") + event.value(QStringLiteral("text")).toString() + '\n', Ink::Note);
            pushServingModel(QStringLiteral("vision"), event);
        } else if (type == QStringLiteral("vision_route_ended")) {
            popServingModel(QStringLiteral("vision"));
        } else if (type == QStringLiteral("vision_unavailable")) {
            // Refused, not failed: the `error` that follows carries the same text, so only the
            // "what to do about it" line is added here.
            ensureLineStart();
            printInline(QStringLiteral("🖼 No vision model · Options › Models › Vision model\n"), Ink::Error);
        } else if (type == QStringLiteral("plan_route")) {
            // Plan mode's own model role (protocol 13): this turn runs on the planning model — by
            // default the pane's own at max reasoning. Said plainly, like the image routing.
            ensureLineStart();
            printInline(QStringLiteral("◆ ") + event.value(QStringLiteral("text")).toString() + '\n', Ink::Note);
            pushServingModel(QStringLiteral("plan"), event);
        } else if (type == QStringLiteral("plan_route_ended")) {
            popServingModel(QStringLiteral("plan"));
        } else if (type == QStringLiteral("provider_retry")) {
            // The model went silent, the request was refused, or the provider keeps failing and the
            // turn has moved to another one (#G9VE). Say which in the transcript: a failover is not
            // a warning — the turn is running, on a provider that answers — and its closing line
            // only says the pane has the model the user chose back.
            const QString reason = event.value(QStringLiteral("reason")).toString();
            const QString mark = reason == QStringLiteral("failover")        ? QStringLiteral("⇄ ")
                               : reason == QStringLiteral("failover_ended")  ? QStringLiteral("↩ ")
                                                                            : QStringLiteral("⚠ ");
            ensureLineStart();
            printInline(mark + event.value(QStringLiteral("text")).toString() + '\n', Ink::Note);
            // A failover is the third way the turn leaves the pane's model, and the picker shows it
            // exactly as it shows the other two (C5). The retries that are not a move — a stall, a
            // truncated step, an HTTP retry — are the same model trying again, so they change
            // nothing here.
            if (reason == QStringLiteral("failover")) pushServingModel(reason, event);
            else if (reason == QStringLiteral("failover_ended")) popServingModel(QStringLiteral("failover"));
            // A provider can report exhaustion through 429 even before its quota poll lands:
            // repeated refusals until the transport's retries are spent and the turn moves on (#YJG7 —
            // fifteen hours of six retries a turn on a plan out of quota until Tuesday). Only that
            // shape — a 429 was retried on this turn, and now the turn is leaving the provider —
            // earns the cool-off; a stall, a 5xx or a single refusal that the retry cleared does not.
            if (reason == QStringLiteral("http")) {
                const int status = event.value(QStringLiteral("status")).toInt();
                if (status == 429 || (status == 0 && event.value(QStringLiteral("text")).toString().contains(QStringLiteral("HTTP 429"))))
                    m_turnSaw429 = true;
            } else if (reason == QStringLiteral("failover")) {
                const QString from = event.value(QStringLiteral("attempt")).toInt() <= 1 || m_failoverTarget.isEmpty()
                    ? m_currentPreset : m_failoverTarget;
                if (m_turnSaw429 && !presetById(from).value(QStringLiteral("hosted")).toBool())
                    markExhausted(from, QStringLiteral("rate limit"), QDateTime::currentSecsSinceEpoch() + kRateLimitCoolOffSeconds);
                m_turnSaw429 = false;
                m_failoverTarget = event.value(QStringLiteral("to_preset")).toString();
            } else if (reason == QStringLiteral("switch") || reason == QStringLiteral("route_dropped")) {
                m_turnSaw429 = false;
            }
        } else if (type == QStringLiteral("status")) {
            const QString text = event.value(QStringLiteral("text")).toString();
            if (m_internals && event.contains(QStringLiteral("handover_chars")))
                m_internals->note(QStringLiteral("Guest handover: %1 characters (~%2 tokens)")
                    .arg(event.value(QStringLiteral("handover_chars")).toInt())
                    .arg(event.value(QStringLiteral("handover_tokens")).toInt()));
            // While a turn runs the clock owns the status line; model-request updates refresh
            // it without displaying their step count.
            if (m_agentBusy && text.startsWith(QStringLiteral("Requesting model · "))) {
                tickTurnClock();
            } else {
                status(text);
            }
        } else if (type == QStringLiteral("done") || type == QStringLiteral("cancelled")) {
            stopTurnClock();
            m_turnSaw429 = false; m_failoverTarget.clear();
            if (m_infoView) m_infoView->refreshIfLive();   // the ⓘ pane follows the turns it lists
            if (type == QStringLiteral("cancelled")) {
                ensureLineStart(); printInline(QStringLiteral("Stopped. Actions that already ran are not rolled back.\n"), Ink::Error);
            }
            printTurnEndRequests(type, event);   // request ledger UI
            ensureLineStart();
            // Readline redraws its prompt asynchronously; closing between queued turns would drop the
            // redrawn prompt into the middle of the next turn's output. Close once the queue is idle.
            if (!moreTurnsPending()) closeInline();
            status(moreTurnsPending() ? QStringLiteral("Next queued prompt…") : QStringLiteral("Ready"));
            if (!m_skillHintPending.isEmpty() && !moreTurnsPending()) {
                hint(QStringLiteral("skills.slash"), QStringLiteral("Next time: /%1 runs that skill").arg(m_skillHintPending));
                m_skillHintPending.clear();
            }
            finishFixTurn(type == QStringLiteral("done"));
        } else if (type == QStringLiteral("error")) {
            if (event.value(QStringLiteral("id")).toString() == QStringLiteral("models-import")) {
                QMessageBox::warning(this, QStringLiteral("Import keys"), event.value(QStringLiteral("text")).toString());
                return;
            }
            const auto text = event.value(QStringLiteral("text")).toString();
            if (event.value(QStringLiteral("code")) == QStringLiteral("configure_failed")
                && event.value(QStringLiteral("restart_worker")).toBool()
                && !sharesWorker() && !m_agentBusy && !m_lastConfigure.isEmpty()) {
                m_configured = false;
                // Keep presets refreshes from choosing a different provider while this
                // failed configuration waits for a fresh process or the person's retry.
                m_configuring = true;
                auto retry = [this] {
                    hideBanner();
                    if (m_pendingAgentMode.isEmpty()) m_pendingAgentMode = m_agentMode;
                    m_restartConfigure = true;
                    if (m_worker.state() == QProcess::NotRunning) startWorker();
                    else m_worker.kill();  // finished() starts the replacement asynchronously
                };
                status(QStringLiteral("Agent configuration failed: ") + text);
                ensureLineStart();
                printInline(QStringLiteral("✗ Agent configuration failed: ") + text + '\n', Ink::Error);
                if (!m_configureAutoRetried) {
                    m_configureAutoRetried = true;
                    retry();
                } else {
                    showBanner(QStringLiteral("Agent configuration failed: ") + text,
                               QStringLiteral("Retry agent"), retry);
                }
                return;
            }
            if (event.value(QStringLiteral("id")).toString() == m_pendingSubmit) m_pendingSubmit.clear();
            const bool wasBusy = m_agentBusy;
            // A rejected ask (queue full, invalid prompt) never started; forget it.
            m_pendingPrompts.remove(event.value(QStringLiteral("id")).toString());
            if (m_activeValid && m_active.agent && event.value(QStringLiteral("id")).toString() == m_activeRequest) {
                m_activeValid = false;
                m_entries.prepend(m_active);
                pauseQueue(QStringLiteral("the worker refused the prompt: ") + text);
            }
            // Route errors do not cancel a concurrent agent turn.
            m_agentBusy = event.value(QStringLiteral("agent_busy")).toBool(false);
            if (!m_agentBusy) stopTurnClock();
            // A guest that could not start (29.3): the pane stays on the model it had, rather than
            // on a row whose harness never came up. `m_presetBeforeGuest` is set on the way in and
            // cleared the moment the guest reports itself configured.
            if (onGuestPreset() && !m_presetBeforeGuest.isEmpty()) {
                m_currentPreset = m_presetBeforeGuest;
                m_presetBeforeGuest.clear();
                m_guestSession.clear();
                m_guestRequest = QJsonObject();
                changed();
            }
            m_configuring = false;
            status(text);
            if (wasBusy && !m_agentBusy) {
                // Relay Free's refusals (protocol 13.9) get their own wording: what to use instead.
                // rate_limited and token_expired keep the worker's sentence, like any provider error.
                const QString code = event.value(QStringLiteral("code")).toString();
                if (code == QStringLiteral("quota_exhausted") || code == QStringLiteral("free_unavailable")) onHostedRefusal(code, event);
                else { ensureLineStart(); printInline(QStringLiteral("✗ ") + text + '\n', Ink::Error); }
                // The turn died on the 429s the transport had been retrying (the text is the pane's
                // own provider's, even after a failover chain: agent._failover_failure): a cool-off.
                if (m_turnSaw429 && text.contains(QStringLiteral("HTTP 429")) && !onHostedPreset())
                    markExhausted(m_currentPreset, QStringLiteral("rate limit"), QDateTime::currentSecsSinceEpoch() + kRateLimitCoolOffSeconds);
                m_turnSaw429 = false; m_failoverTarget.clear();
                printTurnEndRequests(type, event);   // request ledger UI
                if (!moreTurnsPending()) closeInline();
                finishFixTurn(false);
            }
        }
    }
