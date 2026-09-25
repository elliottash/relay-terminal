// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Pane.h"

#include <utility>

bool Pane::handleSessionEvent(const QString &type, const QJsonObject &event) {
        if (type == QStringLiteral("queued") && event.value(QStringLiteral("when")).toString() == QStringLiteral("steer")) {
            const QString requestId = event.value(QStringLiteral("request_id")).toString();
            for (auto &entry : m_steering) {
                if (entry.requestId != requestId) continue;
                entry.itemId = event.value(QStringLiteral("id")).toString();
                // × was clicked before the worker had named the item: withdraw it now.
                if (entry.withdraw) {
                    QJsonObject remove = queueOp(QStringLiteral("queue_remove"));
                    remove.insert(QStringLiteral("item"), entry.itemId);
                    remove.insert(QStringLiteral("id"), QStringLiteral("withdraw-") + requestId);
                    send(remove);
                }
            }
            return true;
        }
        if (type == QStringLiteral("steer_removed")) {
            // Withdrawn before the turn took it. Where it goes now is what the withdraw was for:
            // nowhere (× or Shift+Delete), the prompt box (edited, already there), or the head of
            // the queue (Ctrl+Down).
            const QString requestId = event.value(QStringLiteral("request_id")).toString();
            for (int i = 0; i < m_steering.size(); ++i) {
                if (m_steering[i].requestId != requestId) continue;
                const SteerEntry steer = m_steering[i];
                forgetSteer(i);
                if (steer.then == SteerEntry::ToQueue) {
                    requeueSteer(steer);
                    toast(QStringLiteral("Back in the queue · it runs after this turn"));
                } else if (steer.then == SteerEntry::Edit) {
                    toast(QStringLiteral("Taken back · the agent never saw it · Enter queues it again"));
                } else {
                    toast(QStringLiteral("Withdrawn · the agent never saw it"));
                }
                break;
            }
            rebuildQueueStrip(); changed();
            QTimer::singleShot(0, this, [this] { pumpQueue(); });
            return true;
        }
        if (type == QStringLiteral("steer_delivered")) {
            const QJsonArray requestIds = event.value(QStringLiteral("request_ids")).toArray();
            // A console surface's steered item has no request id the pane could match: the
            // event's item ids pin the tombstone for it (card #XCXD).
            if (!m_enterSteerWorkerItem.isEmpty())
                for (const auto &value : event.value(QStringLiteral("ids")).toArray())
                    if (m_enterSteerWorkerItem == value.toString())
                        m_enterSteerStep = relay::queuesubmit::EnterStep::Delivered;
            for (const auto &value : requestIds) {
                // The prompt this empty-Enter sequence is pinned to reached the turn (card
                // #XCXD): the sequence stays on it — the next Enter answers that the agent
                // already has it, never steering the next queued prompt instead.
                if (m_enterSteerRequest == value.toString())
                    m_enterSteerStep = relay::queuesubmit::EnterStep::Delivered;
                for (int i = 0; i < m_steering.size(); ++i) {
                    if (m_steering[i].requestId != value.toString()) continue;
                    const SteerEntry steer = m_steering[i];
                    ensureLineStart();
                    // Delivered here, not queued: no "at the next tool call" suffix, which only
                    // belongs on a row still waiting in the strip.
                    printInline(steer.text + QLatin1Char('\n'), Ink::UserAgent);
                    printAttachmentThumbnails(steer.text);   // #1MGS
                    forgetSteer(i);
                    if (steer.withdraw) {
                        // The withdraw lost the race: the turn took it first, so the transcript line
                        // above is where it went. The worker's "not queued" answer is expected now.
                        m_withdrawnOnReturn.insert(steer.requestId);
                        if (steer.then == SteerEntry::Edit) {
                            // Its text was already put back in the prompt box. Untouched, that copy
                            // goes; edited, it stays, because it is the user's words now.
                            if (m_editor->toPlainText() == steer.editText) {
                                m_editor->clear();
                                status(QStringLiteral("Too late to edit · the agent already had it at its tool call"));
                            } else {
                                status(QStringLiteral("Too late · the agent already had the original at its tool call"
                                                      " · your edit is still in the prompt box"));
                            }
                        } else {
                            status(QStringLiteral("Too late to withdraw · the agent already had it at its tool call"));
                        }
                    }
                    break;
                }
            }
            rebuildQueueStrip();
            return true;
        }
        if (type == QStringLiteral("steer_escalated")) {
            const QString requestId = event.value(QStringLiteral("request_id")).toString();
            if (event.value(QStringLiteral("escalated")).toBool()) {
                for (int i = 0; i < m_steering.size(); ++i)
                    if (m_steering[i].requestId == requestId) { forgetSteer(i); break; }
                ensureLineStart();
                printInline(QStringLiteral("Interrupting the current turn; completed actions are not rolled back.\n"), Ink::Note);
            } else {
                // Delivered (or already back in the queue) before the third Enter reached the worker.
                m_interruptPending = false;
                m_pendingPrompts.remove(event.value(QStringLiteral("new_request_id")).toString());
                toast(QStringLiteral("The agent already has it · nothing was interrupted"));
            }
            rebuildQueueStrip(); changed();
            return true;
        }
        if (type == QStringLiteral("steer_returned")) {
            const QString requestId = event.value(QStringLiteral("request_id")).toString();
            if (m_enterSteerRequest == requestId) resetEnterSteerSequence();
            if (!m_enterSteerWorkerItem.isEmpty()
                && event.value(QStringLiteral("id")).toString() == m_enterSteerWorkerItem)
                resetEnterSteerSequence();
            for (int i = 0; i < m_steering.size(); ++i) {
                if (m_steering[i].requestId != requestId) continue;
                const SteerEntry steer = m_steering[i];
                forgetSteer(i);
                // A withdraw in flight as the turn ended: the worker's "not queued" answer to it is
                // expected, not news.
                if (steer.withdraw) m_withdrawnOnReturn.insert(requestId);
                if (steer.withdraw && steer.then == SteerEntry::Drop) {
                    // × was clicked as the turn ended: it stays withdrawn rather than coming back.
                    toast(QStringLiteral("Withdrawn · the agent never saw it"));
                } else if (steer.withdraw && steer.then == SteerEntry::Edit) {
                    toast(QStringLiteral("Taken back · the agent never saw it · Enter queues it again"));   // it is in the prompt box
                } else if (!event.value(QStringLiteral("requeued")).toBool()) {
                    // The turn ended before another tool call (or Ctrl+Down asked for exactly this):
                    // the prompt becomes the next queue item.
                    requeueSteer(steer);
                    toast(steer.withdraw ? QStringLiteral("Back in the queue · it runs next")
                                         : QStringLiteral("The agent finished first · your message is next in the queue"));
                }
                break;
            }
            rebuildQueueStrip(); changed();
            QTimer::singleShot(0, this, [this] { pumpQueue(); });
            return true;
        }
        if (type == QStringLiteral("model_roles")) {   // protocol 13
            m_roleSummary = event.value(QStringLiteral("roles")).toObject();
            m_tierSummary = event.value(QStringLiteral("tiers")).toObject();
            if (onRolesResolved) onRolesResolved();
            const QString role = event.value(QStringLiteral("agent_role")).toString();
            if (!role.isEmpty()) m_agentRole = role;
            const QJsonArray warnings = event.value(QStringLiteral("warnings")).toArray();
            for (const auto &warning : warnings) {
                ensureLineStart();
                printInline(warning.toString() + '\n', Ink::Note);
                closeInline();
            }
            changed();
            return true;
        }
        if (type == QStringLiteral("model_changed")) {
            m_model = event.value(QStringLiteral("model")).toString();
            const QString preset = event.value(QStringLiteral("preset")).toString();
            const QString role = event.value(QStringLiteral("agent_role")).toString();   // protocol 13
            if (!role.isEmpty()) {
                m_agentRole = role;
                if (role != QStringLiteral("main") && !preset.isEmpty())
                    m_modePick.insert(relay::modelrows::roleTier(role),
                        {preset + QLatin1Char('|') + m_model, event.value(QStringLiteral("effort")).toString()});
            }
            // A role's turn reports that role's model; only this pane's own agent moves the model
            // the pane *is on* (card #MDL1, design 1.4.3).
            if (role.isEmpty() || role == QStringLiteral("main")) m_paneModel = m_model;
            const QString warning = event.value(QStringLiteral("warning")).toString();
            if (!warning.isEmpty()) { ensureLineStart(); printInline(warning + '\n', Ink::Note); closeInline(); }
            // A role switch keeps the pane's main preset: only a set_model changes it.
            if (!preset.isEmpty() && role.isEmpty()) { m_currentPreset = preset; rememberPreset(preset); discloseHosted(); }
            // Onto or off a guest preset (29.4): which tool answers now, or none.
            if (role.isEmpty()) noteGuestPreset(event);
            // Mid-turn (issue 3ES1) the chip moves now; the model still answering keeps its window as
            // m_ctxWindow until `model_applied`, and the bar shows the new one from the `context`
            // event's `next`, which follows this event.
            const QString applies = event.value(QStringLiteral("applies")).toString();
            const bool afterCompaction = applies == QStringLiteral("after_compaction");
            const bool later = applies == QStringLiteral("next_step") || applies == QStringLiteral("turn_end") || afterCompaction;
            const bool willCompact = event.value(QStringLiteral("will_compact")).toBool();
            const qint64 window = event.value(QStringLiteral("context_window")).toVariant().toLongLong();
            if (window > 0 && !later) m_ctxWindow = window;
            const QString effort = event.value(QStringLiteral("effort")).toString();
            if (!effort.isEmpty()) m_effort = effort;
            const QString inFlight = event.value(QStringLiteral("in_flight_model")).toString();
            // Names, not ids, and lower-case (card #MDL1, rule 1): "model: kimi-k3 · conversation
            // kept", "flash: glm-5.3-flash · conversation kept". The worker sends the name it
            // computed (`model_name`, protocol 13) and the catalog answers for a worker that does
            // not. The preset is this report's own when it carries one: a role switch takes its
            // model from another provider than the pane's.
            const QString namingPreset = preset.isEmpty() ? m_currentPreset : preset;
            const QString sentName = event.value(QStringLiteral("model_name")).toString();
            const QString modelName = sentName.isEmpty() ? modelNameFor(namingPreset, m_model) : sentName;
            const QString sentInFlight = event.value(QStringLiteral("in_flight_model_name")).toString();
            const QString inFlightName = sentInFlight.isEmpty() ? modelNameFor(m_currentPreset, inFlight) : sentInFlight;
            QString what = later
                ? (afterCompaction
                       ? QStringLiteral("model: %1 once the conversation is compacted to fit its window").arg(modelName)
                       : applies == QStringLiteral("turn_end")
                       ? QStringLiteral("model: %1 from the next turn · this turn finishes on %2").arg(modelName, inFlightName)
                       : QStringLiteral("model: %1 from the next step · %2 is not interrupted").arg(modelName, inFlightName))
                : role.isEmpty() || role == QStringLiteral("main")
                ? QStringLiteral("model: %1 · conversation kept").arg(modelName)
                : QStringLiteral("%1: %2 · conversation kept").arg(roleLabel(role), modelName);
            // This report *is* the switch a command asked for arriving, so it says what that
            // command did instead of overwriting its line 100–300 ms later with a generic one
            // (`sayAndSwitch`; card #MDL1, design 1.4.2). One shot: whatever happens next, the
            // next report speaks for itself.
            if (!m_switchSentence.isEmpty() && !later && (role.isEmpty() || role == QStringLiteral("main")))
                what = m_switchSentence;
            m_switchSentence.clear();
            // The level this switch moved, when it moved one (card #MDL1, rule 3): one line, not
            // two that overwrite each other. Built here, where the model it names is known.
            if (!later)
                if (const QString note = takeEffortSnapNote(); !note.isEmpty())
                    what += QStringLiteral(" · ") + note;
            status(what); toast(what);
            if (later) {
                // The clock owns the status line while a turn runs, so the "not now, next step" part
                // goes in the transcript too; `model_applied` marks where it landed.
                ensureLineStart();
                if (afterCompaction)
                    printInline(QStringLiteral("↻ %1 takes over once the conversation is compacted to fit its window · "
                                               "%2 summarises it\n").arg(modelName, inFlightName), Ink::Note);
                else
                    printInline(QStringLiteral("↻ %1 takes over %2 · %3 is not interrupted%4\n")
                        .arg(modelName, applies == QStringLiteral("turn_end") ? QStringLiteral("after this turn")
                                                                            : QStringLiteral("at the next step"),
                             inFlightName, willCompact ? QStringLiteral(" · will compact to fit") : QString()), Ink::Note);
                if (!m_agentBusy && !moreTurnsPending()) closeInline();
            }
            changed();
            return true;
        }
        if (type == QStringLiteral("model_applied")) {
            // The moment a mid-turn switch takes effect (issue 3ES1): at a step boundary, before the
            // next request, or once the turn is over. One line in the transcript, where it happened.
            const QString model = event.value(QStringLiteral("model")).toString();
            const qint64 window = event.value(QStringLiteral("context_window")).toVariant().toLongLong();
            if (window > 0) m_ctxWindow = window;
            // By name (card #MDL1, rule 1), the worker's own when it sent one.
            const QString appliedName = event.value(QStringLiteral("model_name")).toString().isEmpty()
                ? modelNameFor(event.value(QStringLiteral("preset")).toString(), model)
                : event.value(QStringLiteral("model_name")).toString();
            QString line = QStringLiteral("→ now on %1").arg(appliedName);
            if (event.value(QStringLiteral("at")).toString() == QStringLiteral("turn_end"))
                line += QStringLiteral(" · from the next turn");
            if (event.value(QStringLiteral("history_converted")).toBool())
                line += QStringLiteral(" · conversation converted from %1")
                            .arg(modelNameFor(QString(), event.value(QStringLiteral("from_model")).toString()));
            if (event.value(QStringLiteral("compacted")).toBool())
                line += QStringLiteral(" · compacted to fit its window");
            ensureLineStart();
            printInline(line + '\n', Ink::Note);
            if (!m_agentBusy && !moreTurnsPending()) closeInline();
            clearNextContext();
            updateContextLabel();
            changed();
            return true;
        }
        if (type == QStringLiteral("model_switch_withdrawn")) {
            // The × on a steered `/model` row (card #7QH0). Withdrawn: the pane goes back to the
            // model in force, as after a refusal but quietly — nobody failed. Not withdrawn: it
            // landed first, and its `model_applied` line already says so.
            if (!event.value(QStringLiteral("withdrawn")).toBool()) {
                status(QStringLiteral("Too late to withdraw · the switch already landed"));
                changed();
                return true;
            }
            const QString current = event.value(QStringLiteral("current_model")).toString();
            const QString preset = event.value(QStringLiteral("preset")).toString();
            if (!current.isEmpty()) m_model = m_paneModel = current;
            if (!preset.isEmpty()) { m_currentPreset = preset; rememberPreset(preset); }
            const qint64 window = event.value(QStringLiteral("context_window")).toVariant().toLongLong();
            if (window > 0) m_ctxWindow = window;
            clearNextContext();
            const QString what = QStringLiteral("Withdrawn · staying on %1").arg(modelNameFor(m_currentPreset, m_model));
            ensureLineStart();
            printInline(QStringLiteral("↻ /model %1 withdrawn · staying on %2\n")
                            .arg(modelNameFor(QString(), event.value(QStringLiteral("model")).toString()),
                                 modelNameFor(m_currentPreset, m_model)), Ink::Note);
            if (!m_agentBusy && !moreTurnsPending()) closeInline();
            status(what); toast(what);
            updateContextLabel();
            changed();
            return true;
        }
        if (type == QStringLiteral("model_switch_refused")) {
            // A switch the new window cannot hold, even compacted (issue 3ES1): the pane stays on the
            // model in force, so the chip, the role and the provider settings go back to it.
            const QString refusedId = event.value(QStringLiteral("model")).toString();
            const QString refused = modelNameFor(event.value(QStringLiteral("preset")).toString(), refusedId);
            const QString current = event.value(QStringLiteral("current_model")).toString();
            if (!current.isEmpty()) m_model = current;
            const QString role = event.value(QStringLiteral("agent_role")).toString();
            const QString preset = event.value(QStringLiteral("preset")).toString();
            if (!role.isEmpty()) m_agentRole = role;
            if (!preset.isEmpty() && (role.isEmpty() || role == QStringLiteral("main"))) { m_currentPreset = preset; rememberPreset(preset); }
            if (role.isEmpty() || role == QStringLiteral("main")) m_paneModel = m_model;
            const qint64 window = event.value(QStringLiteral("context_window")).toVariant().toLongLong();
            if (window > 0) m_ctxWindow = window;
            clearNextContext();
            const QString reason = event.value(QStringLiteral("reason")).toString();
            ensureLineStart();
            // The reason is a whole sentence that names both models.
            printInline(QStringLiteral("✗ %1\n").arg(reason.isEmpty() ? refused + QStringLiteral(" did not take over.") : reason),
                        Ink::Error);
            if (!m_agentBusy && !moreTurnsPending()) closeInline();
            const QString what = event.value(QStringLiteral("code")).toString() == QStringLiteral("board_model_unavailable")
                || event.value(QStringLiteral("code")) == QStringLiteral("model_switch_failed")
                ? QStringLiteral("Still on %1 · %2").arg(modelNameFor(m_currentPreset, m_model), reason)
                : QStringLiteral("Still on %1 · %2's window is too small for this conversation")
                      .arg(modelNameFor(m_currentPreset, m_model), refused);
            status(what); toast(what);
            updateContextLabel();
            changed();
            return true;
        }
        if (type == QStringLiteral("effort_changed")) {
            const QString effort = event.value(QStringLiteral("effort")).toString();
            if (!effort.isEmpty()) m_effort = effort;
            changed();
            return true;
        }
        if (type == QStringLiteral("hosted_quota")) {
            // Relay Free's allowance (protocol 13.9): after every gateway call, and in reply to a
            // hosted_quota request. The chip follows it; so does the keys modal's row when open.
            setHostedQuota(event);
            if (m_keysDialog) m_keysDialog->handleEvent(event);
            return true;
        }
        if (type == QStringLiteral("usage_limits")) {
            // A guest's subscription windows (protocol 29.3), fresh from its harness: the picker's
            // "left" column and the priority follow at once rather than at the next presets answer.
            QList<relay::models::LimitWindow> windows;
            for (const auto &value : event.value(QStringLiteral("windows")).toArray()) {
                const QJsonObject window = value.toObject();
                relay::models::LimitWindow w;
                w.kind = window.value(QStringLiteral("kind")).toString();
                w.usedPercent = window.contains(QStringLiteral("used_percent")) ? window.value(QStringLiteral("used_percent")).toDouble() : -1;
                w.resetsAt = window.value(QStringLiteral("resets_at")).toVariant().toLongLong();
                if (!w.kind.isEmpty()) windows << w;
            }
            // Usage resets are optional on the wire (protocol 29.3): only a real number counts,
            // and a bool is not one.
            const QJsonValue resets = event.value(QStringLiteral("resets_available"));
            const int resetsAvailable = resets.isDouble() ? qMax(0, resets.toInt()) : -1;
            noteLimits(event.value(QStringLiteral("preset")).toString(), windows,
                       event.value(QStringLiteral("status")).toString(), resetsAvailable,
                       event.value(QStringLiteral("resets_expire_at")).toVariant().toLongLong());
            return true;
        }
        if (type == QStringLiteral("usage_reset")) {
            // `/usage-reset`'s answer (protocol 29.3, #KQNP). `confirm` is Codex asking before it
            // spends one: yes sends the request again with confirm; `web` is Claude, whose resets
            // are spent on claude.ai, so its page opens. Everything else is a sentence to show.
            const QString outcome = event.value(QStringLiteral("outcome")).toString();
            const QString message = event.value(QStringLiteral("message")).toString();
            if (outcome == QStringLiteral("confirm")) {
                if (QMessageBox::question(this, QStringLiteral("Use a usage reset?"), message,
                                          QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
                    send({{"type", "usage_reset"}, {"confirm", true}});
                else status(QStringLiteral("Reset kept."));
                return true;
            }
            const QUrl url(event.value(QStringLiteral("url")).toString());
            if (outcome == QStringLiteral("web") && url.scheme() == QStringLiteral("https"))
                QDesktopServices::openUrl(url);
            if (!message.isEmpty()) status(message);
            return true;
        }
        if (type == QStringLiteral("usage")) {
            // One provider call's token report (protocol § 4). The context chip is drawn from the
            // `context` event that follows; this is the per-call line the Activity pane shows, and
            // with it what the provider's prefix cache served (#GMCF decision 5). Nothing is drawn
            // in the terminal: the pane is where the per-request numbers live.
            if (m_internals) m_internals->noteUsage(event.value(QStringLiteral("usage")).toObject());
            return true;
        }
        if (type == QStringLiteral("prefix_changed")) {
            QStringList parts;
            for (const auto &part : event.value(QStringLiteral("parts")).toArray()) parts << part.toString();
            if (m_internals)
                m_internals->note(QStringLiteral("Prompt prefix changed at request %1: %2%3")
                    .arg(event.value(QStringLiteral("request")).toInt())
                    .arg(parts.join(QStringLiteral(", ")),
                         event.value(QStringLiteral("reason")).toString().isEmpty() ? QString()
                             : QStringLiteral(" · ") + event.value(QStringLiteral("reason")).toString()));
            return true;
        }
        if (type == QStringLiteral("tool_results_cleared")) {
            if (m_internals)
                m_internals->note(QStringLiteral("Shortened %1 old tool results (%2 characters)")
                    .arg(event.value(QStringLiteral("count")).toInt())
                    .arg(event.value(QStringLiteral("chars")).toInt()));
            return true;
        }
        if (type == QStringLiteral("context")) {
            const auto reading = relay::context::Reading::fromEvent(event);
            m_ctxUsed = reading.used;
            m_ctxWindow = reading.window;
            m_ctxLimit = reading.limit;
            m_ctxPercent = reading.percent;
            m_ctxEstimated = reading.estimated;
            m_ctxGuest = reading.guest;
            // A switch waiting to land (issue 3ES1): the bar measures against its window instead.
            const QJsonObject next = event.value(QStringLiteral("next")).toObject();
            if (next.isEmpty() || !m_ctxGuest.isEmpty()) clearNextContext();
            else {
                m_ctxNextUsed = next.value(QStringLiteral("used_tokens")).toVariant().toLongLong();
                m_ctxNextWindow = next.value(QStringLiteral("window")).toVariant().toLongLong();
                m_ctxNextLimit = next.value(QStringLiteral("limit_tokens")).toVariant().toLongLong();
                m_ctxNextPercent = next.value(QStringLiteral("percent")).toDouble();
                // By name (card #MDL1, rule 1): the bar's tooltip is a sentence about two models.
                m_ctxNextModel = modelNameFor(m_currentPreset, next.value(QStringLiteral("model")).toString());
                m_ctxInFlightModel = modelNameFor(m_currentPreset,
                                                  next.value(QStringLiteral("in_flight_model")).toString());
            }
            if (m_contextNotePending) {
                m_contextNotePending = false;
                ensureLineStart();
                if (!m_ctxGuest.isEmpty())
                    printInline(reading.tooltip() + '\n', Ink::Note);
                else printInline(QStringLiteral("Context: %1 of %2 tokens (%3%) · compacts at %4%5\n")
                    .arg(compactTokens(m_ctxUsed), compactTokens(m_ctxWindow), QString::number(m_ctxPercent, 'f', 1), compactTokens(m_ctxLimit),
                         m_ctxEstimated ? QStringLiteral(" · estimated") : QString()), Ink::Note);
                if (!m_agentBusy && !moreTurnsPending()) closeInline();
            }
            updateContextLabel();
            return true;
        }
        if (type == QStringLiteral("compaction_started")) {
            m_compacting = true; m_compactPercent = -1; m_compactThinking = false;
            updateContextLabel();
            status(QStringLiteral("Compacting the conversation…"));
            return true;
        }
        if (type == QStringLiteral("compaction_progress")) {
            // Streaming the summary (compaction is one completion): chars against an estimated
            // total, clamped below 100% until the compacted event lands — it is a live guess, not
            // a promise. Reasoning deltas count only as "still working" (no denominator exists).
            if (m_compacting) {
                const bool thinking = event.value(QStringLiteral("phase")).toString() == QStringLiteral("thinking");
                const double estimate = event.value(QStringLiteral("estimate")).toDouble();
                const double chars = event.value(QStringLiteral("chars")).toDouble();
                m_compactPercent = (!thinking && estimate > 0) ? qBound(0, int(100.0 * chars / estimate), 95) : -1;
                m_compactThinking = thinking;
                updateContextLabel();
            }
            return true;
        }
        if (type == QStringLiteral("compacted")) {
            m_compacting = false; m_compactPercent = -1; m_compactThinking = false;
            status(QStringLiteral("Conversation compacted"));
            ensureLineStart();
            const QString forModel = event.value(QStringLiteral("for_model")).toString();   // issue 3ES1
            printInline(QStringLiteral("Conversation compacted (%1) · %2 → %3 tokens\n")
                .arg(forModel.isEmpty() ? event.value(QStringLiteral("reason")).toString(QStringLiteral("manual"))
                                        : QStringLiteral("to fit %1's window").arg(forModel),
                     compactTokens(event.value(QStringLiteral("before_tokens")).toVariant().toLongLong()),
                     compactTokens(event.value(QStringLiteral("after_tokens")).toVariant().toLongLong())), Ink::Note);
            if (!m_agentBusy && !moreTurnsPending()) closeInline();
            updateContextLabel();
            return true;
        }
        if (type == QStringLiteral("mode_changed")) {
            const QString mode = event.value(QStringLiteral("mode")).toString();
            // Also covers exit_plan_mode and Execute, which leave Plan in the worker.
            if (mode == QStringLiteral("build")) restorePrePlanSelection();
            const bool changedMode = mode != m_agentMode;
            m_agentMode = mode;
            if (changedMode)
                toast(mode == QStringLiteral("plan") ? QStringLiteral("Plan mode · the agent investigates and writes a plan")
                                                     : QStringLiteral("Build mode"));
            changed();
            return true;
        }
        if (type == QStringLiteral("question")) {
            showQuestion(event);
            return true;
        }
        if (type == QStringLiteral("question_closed")) {
            closeQuestion(event.value(QStringLiteral("reason")).toString() == QStringLiteral("cancelled")
                              ? QStringLiteral("the turn was stopped") : QString());
            return true;
        }
        if (type == QStringLiteral("plan_written")) {
            const QString path = event.value(QStringLiteral("path")).toString();
            ensureLineStart();
            printInline(QStringLiteral("Plan written: %1\n").arg(QDir(m_workspace).relativeFilePath(path)), Ink::Note);
            if (onPlanWritten) QTimer::singleShot(0, this, [this, path] { if (onPlanWritten) onPlanWritten(path, this); });
            return true;
        }
        if (type == QStringLiteral("checkpoints")) {
            if (!m_rewindPending) return true;
            m_rewindPending = false;
            const QJsonArray items = event.value(QStringLiteral("items")).toArray();
            QTimer::singleShot(0, this, [this, items] { showRewindPicker(items); });
            return true;
        }
        if (type == QStringLiteral("rewound")) {
            // Before anything else prints: what the rewind undid is still on the screen, and the
            // lines below would land inside the block that is being kept (#0TJ9).
            const QString restore = event.value(QStringLiteral("restore")).toString();
            const bool collapsed = saveRewoundText(event.value(QStringLiteral("rewound_n")).toInt(),
                            event.value(QStringLiteral("prompt")).toString(), restore != QStringLiteral("files"));
            const QJsonArray restored = event.value(QStringLiteral("restored_files")).toArray();
            const QJsonArray conflicts = event.value(QStringLiteral("conflicts")).toArray();
            ensureLineStart();
            const QString what = restore == QStringLiteral("files") ? QStringLiteral("code") : restore == QStringLiteral("both") ? QStringLiteral("code and chat") : QStringLiteral("chat");
            if (!collapsed || restore == QStringLiteral("both")) printInline(QStringLiteral("Rewound %1 to turn %2%3\n").arg(what).arg(event.value(QStringLiteral("turn")).toInt())
                .arg(restore == QStringLiteral("conversation") ? QStringLiteral(" · files unchanged") : QStringLiteral(" · %1 file(s) restored").arg(restored.size())), Ink::Note);
            if (!conflicts.isEmpty()) {
                QStringList names;
                for (const auto &value : conflicts) names << (value.isString() ? value.toString() : value.toObject().value(QStringLiteral("path")).toString());
                printInline(QStringLiteral("Changed since, not restored: %1\n").arg(names.join(QStringLiteral(", "))), Ink::Error);
            }
            const QString note = event.value(QStringLiteral("note")).toString();
            if (!note.isEmpty()) printInline(note + '\n', Ink::Note);
            closeInline();
            // The rebuild moved Readline's old prompt. Like saved-text replay, ask the idle
            // shell for a fresh prompt so a later resize cannot redraw at its stale position.
            if (collapsed && hasShell() && shellIdleAtPrompt()) sendShellInput(QStringLiteral("\n"));
            const QString prompt = event.value(QStringLiteral("prompt")).toString();
            // Rewind code keeps the chat, so the turn's prompt is not put back.
            if (!prompt.isEmpty() && restore != QStringLiteral("files") && m_editor->toPlainText().isEmpty()) {
                m_editor->setPlainText(prompt);
                m_editor->moveCursor(QTextCursor::End);
                m_editor->setFocus();
            }
            m_turnsCompleted = std::max(0, event.value(QStringLiteral("turn")).toInt() - 1);
            return true;
        }
        if (type == QStringLiteral("fork_state")) {
            if (!m_forkPending) return true;
            m_forkPending = false;
            // The fork takes this conversation's terminal text with it (#0TJ9). Stashed here
            // because the fork's own session id is not minted until the new pane loads the state;
            // that pane picks it up at its `state_loaded`. This pane's file is untouched.
            pendingForkText() = ForkText{sessionTextLines(),
                                             m_backend ? m_backend->proseBlocks()
                                                       : QVector<relay::ProseBlock>(),
                                             QDateTime::currentDateTimeUtc()};
            const QJsonObject state = event.value(QStringLiteral("state")).toObject();
            const QString title = state.value(QStringLiteral("title")).toString();
            if (onForkState) QTimer::singleShot(0, this, [this, state, title] { if (onForkState) onForkState(state, title); });
            return true;
        }
        // ----- pane title (protocol section 18) ------------------------------------------------
        if (type == QStringLiteral("session_title")) {
            setTitleFromWorker(event.value(QStringLiteral("title")).toString(),
                               event.value(QStringLiteral("source")).toString() == QStringLiteral("user"));
            return true;
        }
        if (type == QStringLiteral("state_loaded")) {
            const bool restoring = !m_restoreRequest.isEmpty();
            m_sessionId = event.value(QStringLiteral("session_id")).toString(m_sessionId);
            if (!m_restoreRequest.isEmpty()) {
                m_restoreRequest.clear();
                m_restoreSession.clear();
            }
            // The conversation's own guest, when it ran on one (#PCJY): more precise than the
            // layout's copy, and it is what a later guest configure resumes. A conversation that
            // ran on no guest releases whatever the layout remembered.
            if (const QString guest = event.value(QStringLiteral("guest")).toString(); !guest.isEmpty()) {
                m_restoreGuestKey = guest;
                if (const QString account = event.value(QStringLiteral("guest_account")).toString(); !account.isEmpty())
                    m_restoreGuestKey += QLatin1Char(':') + account;
            } else {
                m_restoreGuestKey.clear();
            }
            m_restoreGuestSession = event.value(QStringLiteral("guest_session")).toString();
            if (m_restoreGuestSession.isEmpty()) m_restoreGuestKey.clear();
            syncSessionText();   // the conversation this pane's text belongs to, from here on (#0TJ9)
            // The saved pane text was replayed before the worker loaded this session. The normal
            // adoption boundary would put all of it before the conversation and overwrite its
            // sidecar with only the new shell's rows at the next save.
            if (restoring) m_sessionTextMark = 0;
            // A fork's id exists only now: the text its parent stashed is written under it and
            // replayed here, so the fork opens showing what it was forked from (#0TJ9).
            if (m_forkLoadPending) adoptForkText();
            m_turnsCompleted = event.value(QStringLiteral("turns")).toInt();
            // A resumed session whose last turn never ended: the pane may offer to continue it (#SXF1).
            m_turnCutOff = event.value(QStringLiteral("turn_open")).toBool();
            const QString title = event.value(QStringLiteral("title")).toString();
            if (m_forkLoadPending) {
                ensureLineStart();
                printInline(QStringLiteral("Forked from “%1” · %2 turn(s)\n").arg(m_forkTitle.isEmpty() ? title : m_forkTitle).arg(m_turnsCompleted), Ink::Note);
                closeInline();
            }
            m_forkLoadPending = false;
            clearAgentQueue();
            return true;
        }
        // ----- conversation list and search (protocol section 14) -----------------------------
        if (type == QStringLiteral("conversations")) {
            noteRemoteSessions(event);   // pane_state (relay-terminal-71)
            if (event.value(QStringLiteral("id")).toString() == QStringLiteral("palette-conversations")) {
                if (onPaletteConversations) onPaletteConversations(event);
                return true;
            }
            // Only the manager's own queries (another client may ask this worker too).
            if (m_conversations && event.value(QStringLiteral("id")).toString() == QStringLiteral("conv-list"))
                m_conversations->setResults(event);
            return true;
        }
        if (type == QStringLiteral("conversation_open")) {
            const QJsonObject item = event.value(QStringLiteral("item")).toObject();
            if (onOpenSessionRow && !item.isEmpty()) onOpenSessionRow(item);
            return true;
        }
        if (type == QStringLiteral("conversation")) {
            // A conversation resumed with no saved terminal text, drawn from its entries (#0TJ9).
            // Its own request id, so the sessions manager's preview never sees this answer.
            if (!m_transcriptRequest.isEmpty()
                && event.value(QStringLiteral("id")).toString() == m_transcriptRequest) {
                m_transcriptRequest.clear();
                printSavedTranscript(event.value(QStringLiteral("items")).toArray());
                return true;
            }
            // The fill for a saved text that no longer covers its conversation (#KDB4): the turns
            // above the file's window print here, then the replay the request was holding runs.
            if (!m_transcriptFillRequest.isEmpty()
                && event.value(QStringLiteral("id")).toString() == m_transcriptFillRequest) {
                m_transcriptFillRequest.clear();
                m_transcriptFillSessionId.clear();
                const QStringList saved = std::exchange(m_transcriptFillSavedLines, QStringList());
                const QJsonArray items = event.value(QStringLiteral("items")).toArray();
                const int covered = relay::transcriptreplay::coveredFrom(saved, items);
                if (covered != 0) printTranscriptFill(items, covered);
                replayRestoredScrollback();
                return true;
            }
            if (event.value(QStringLiteral("id")).toString() == QStringLiteral("find-count")) {
                if (m_findBar) m_findBar->setConversationMatches(event.value(QStringLiteral("match_count")).toInt());
                return true;
            }
            if (m_conversations) m_conversations->setPreview(event);
            return true;
        }
        if (type == QStringLiteral("conversation_deleted") || type == QStringLiteral("conversation_renamed")
            || type == QStringLiteral("conversation_pinned")) {
            if (m_conversations) m_conversations->removed(event.value(QStringLiteral("session_id")).toString());
            if (type == QStringLiteral("conversation_deleted")) status(QStringLiteral("Conversation deleted."));
            return true;
        }
        // ----- summaries (protocol section 18.4): the session manager's rows and its batch -------
        if (type == QStringLiteral("session_summary")) {
            if (m_conversations) m_conversations->setSessionSummary(event);
            return true;
        }
        if (type == QStringLiteral("conversation_summary")) {
            if (m_conversations) m_conversations->setSummary(event);
            return true;
        }
        if (type == QStringLiteral("conversations_summarize_estimate")) {
            if (m_conversations) m_conversations->setSummariseEstimate(event);
            return true;
        }
        if (type == QStringLiteral("conversations_summarize_progress")) {
            if (m_conversations) m_conversations->setSummariseProgress(event);
            return true;
        }
        if (type == QStringLiteral("conversations_summarize_cancelled")) return true;
        if (type == QStringLiteral("terminal_context_preview")) {
            auto *dialog = new QDialog(this); dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->setWindowTitle(QStringLiteral("Terminal context for next prompt"));
            auto *layout = new QVBoxLayout(dialog);
            auto *text = new QPlainTextEdit(dialog); text->setReadOnly(true);
            text->setPlainText(event.value(QStringLiteral("text")).toString());
            layout->addWidget(text); dialog->resize(720, 460); dialog->show();
            return true;
        }
        if (type == QStringLiteral("terminal_history_indexed") || type == QStringLiteral("index_rebuilt")) {
            if (type == QStringLiteral("index_rebuilt"))
                status(QStringLiteral("Conversation index rebuilt: %1 conversation(s), %2 entries, %3 ms")
                           .arg(event.value(QStringLiteral("sessions")).toInt())
                           .arg(event.value(QStringLiteral("entries")).toInt())
                           .arg(event.value(QStringLiteral("ms")).toInt()));
            return true;
        }
        // The resume picker's list (protocol section 5) is not asked for any more: /resume opens
        // the session manager. An answer to another client's request is simply dropped.
        if (type == QStringLiteral("sessions")) return true;
        // ----- conversation info, the ⓘ view (protocol section 25) --------------------------
        if (type == QStringLiteral("session_info")) {
            if (m_infoView) {
                // Card #FYEY: the pane knows which backend pin its worker runs from, and can
                // tell whether the checkout has moved on since; the worker's own report cannot.
                // Both are stamped here — hashing the tree again is a cost an opening info view
                // can pay, and a spawn must not.
                QJsonObject stamped = event;
                // Only the pane's own live conversation has this worker's backend; a saved
                // session shown in the same view does not.
                stamped.insert(QStringLiteral("backend_rev"),
                               event.value(QStringLiteral("live")).toBool() ? m_backendRev : QString());
                stamped.insert(QStringLiteral("backend_changed"),
                               !m_backendRev.isEmpty() && m_backendRev != QStringLiteral("live")
                                   && relay::runtimedirs::backendTreeHash(m_data) != m_backendRev);
                m_infoView->setInfo(stamped);
            }
            return true;
        }
        if (type == QStringLiteral("context_breakdown")) {
            ensureLineStart();
            printInline(QStringLiteral("Context components (estimated tokens):\n"), Ink::Note);
            for (const auto &value : event.value(QStringLiteral("parts")).toArray()) {
                const QJsonObject part = value.toObject();
                printInline(QStringLiteral("  %1: %2\n")
                    .arg(part.value(QStringLiteral("name")).toString(),
                         compactTokens(part.value(QStringLiteral("tokens")).toVariant().toLongLong())), Ink::Note);
            }
            printInline(QStringLiteral("Total estimate: %1 tokens · window %2\n")
                .arg(compactTokens(event.value(QStringLiteral("total_tokens")).toVariant().toLongLong()),
                     compactTokens(event.value(QStringLiteral("window")).toVariant().toLongLong())), Ink::Note);
            if (!event.value(QStringLiteral("note")).toString().isEmpty())
                printInline(event.value(QStringLiteral("note")).toString() + QLatin1Char('\n'), Ink::Note);
            if (!m_agentBusy && !moreTurnsPending()) closeInline();
            return true;
        }
        if (type == QStringLiteral("recap")) {
            const QString reason = event.value(QStringLiteral("reason")).toString();
            if (event.contains(QStringLiteral("skipped"))) {
                if (reason == QStringLiteral("manual") || m_recapManual)
                    status(event.value(QStringLiteral("skipped")).toString() == QStringLiteral("failed")
                           ? QStringLiteral("Recap failed: ") + event.value(QStringLiteral("error")).toString()
                           : QStringLiteral("Not enough conversation for a recap yet."));
                m_recapManual = false;
                m_recapInFlight = false;   // answered, skipped or failed: the pane may ask again
                return true;
            }
            m_recapManual = false;
            m_recapInFlight = false;
            m_lastRecapTurns = event.value(QStringLiteral("turns_covered")).toInt();
            // The pane already replayed what was on screen before a restart. Printing the
            // worker's resume recap here appends another summary on every reload.
            if (reason == QStringLiteral("resume")) return true;
            ensureLineStart();
            // The block opens by marking where the agent's last message ended and when it
            // finished (owner request, 2026-09-19, #MVGR), then states the stretch of work the
            // recap covers ("Recap · 09:12 → 11:47 · 2h 35m"), from the worker's recorded turn
            // stamps (owner request, 2026-09-17). `finished_text` is absent when the last turn
            // never ended (a recap asked for mid-run) or the session predates stamps, and the
            // marker then stands alone: the time is unknown, not guessed. A session with no
            // stamps sends no `span_text` either, and the summary follows "Recap · " on one line
            // as before: no span reads better than a guessed one.
            const QString finished = event.value(QStringLiteral("finished_text")).toString();
            printInline(QStringLiteral("[end of message]\n"), Ink::Recap);
            if (!finished.isEmpty())
                printInline(QStringLiteral("finished at %1\n").arg(finished), Ink::Recap);
            // A blank line between the block's sections (owner request, 2026-09-23): the
            // marker, the covered stretch, the summary and the follow-ups each stand on their
            // own instead of running together as one wall of lines.
            const QString span = event.value(QStringLiteral("span_text")).toString();
            const QString summary = event.value(QStringLiteral("text")).toString();
            const auto indent = [](const QString &s, int n) -> QString {
                const QString prefix(n, QChar(' '));
                QString out = prefix + s;
                out.replace('\n', '\n' + prefix);
                return out;
            };
            if (span.isEmpty()) {
                printInline(QStringLiteral("\nRecap ·\n"), Ink::Recap);
                printInline(indent(summary, 2) + '\n', Ink::RecapBody);
            } else {
                printInline(QStringLiteral("\nRecap · ") + span + '\n', Ink::Recap);
                printInline(indent(summary, 2) + '\n', Ink::RecapBody);
            }
            const QString next = event.value(QStringLiteral("next_action")).toString();
            if (!next.isEmpty()) printInline(QStringLiteral("\nNext · ") + next + '\n', Ink::Recap);
            const QString openLine = relay::RequestLedgerModel::openItemsLine(relay::RequestLedgerModel::parseOpenItems(event.value(QStringLiteral("open_items")).toArray()));
            if (!openLine.isEmpty()) printInline(QStringLiteral("\nOpen · ") + openLine + QStringLiteral("  · /tasks\n"), Ink::Recap);
            if (!m_agentBusy && !moreTurnsPending()) closeInline();
            if (reason == QStringLiteral("away")) toast(QStringLiteral("Welcome back · recap above"));
            return true;
        }
        if (type == QStringLiteral("instructions_found")) {
            if (!m_instructionsDialogPending) return true;
            m_instructionsDialogPending = false;
            const QJsonArray items = event.value(QStringLiteral("items")).toArray();
            QTimer::singleShot(0, this, [this, items] { showInstructionsDialog(items); });
            return true;
        }
        if (type == QStringLiteral("instructions_synthesized")) {
            const QString path = event.value(QStringLiteral("path")).toString();
            QSettings().setValue(QStringLiteral("instructions/files"), QStringList{path});
            status(QStringLiteral("Created %1").arg(path));
            ensureLineStart();
            printInline(QStringLiteral("Created %1 from your instruction files\n").arg(path), Ink::Note);
            closeInline();
            applyConfigureChange(QStringLiteral("relay.md created"));
            if (onOpenDocument) QTimer::singleShot(0, this, [this, path] { if (onOpenDocument) onOpenDocument(path); });
            return true;
        }
        if (type == QStringLiteral("suggestion")) {
            const QString kind = event.value(QStringLiteral("kind")).toString();
            const QString text = event.value(QStringLiteral("text")).toString().trimmed();
            // A side call that failed says so, on the status line, naming the call and the model it
            // ran on. Without this the only sign was a bare provider error and no ghost text, which
            // reads as "suggestions do not work" (#308N).
            const QString error = event.value(QStringLiteral("error")).toString();
            if (!error.isEmpty()) {
                if (event.value(QStringLiteral("id")).toString() != m_suggestionId) return true;
                m_suggestionId.clear();
                // By name (card #MDL1, rule 1): this is a role's model, from another provider
                // than the pane's as often as not.
                const QString model = modelNameFor(event.value(QStringLiteral("preset")).toString(),
                                                   event.value(QStringLiteral("model")).toString());
                status(QStringLiteral("%1 suggestion failed%2: %3")
                           .arg(kind == QStringLiteral("next_prompt") ? QStringLiteral("Next-prompt") : QStringLiteral("Next-command"),
                                model.isEmpty() ? QString() : QStringLiteral(" (") + model + ')', error));
                return true;
            }
            if (event.value(QStringLiteral("id")).toString() != m_suggestionId || text.isEmpty() || !m_editor->toPlainText().isEmpty()) return true;
            if (kind == QStringLiteral("next_command") && m_modeValue == QStringLiteral("agent")) return true;
            if (kind == QStringLiteral("next_prompt") && m_modeValue == QStringLiteral("shell")) return true;
            m_aiGhost = text;
            m_aiGhostKind = kind;
            updateGhost();
            return true;
        }
        if (type == QStringLiteral("agent_options")) {
            status(QStringLiteral("Automatic turns from background agents: up to %1").arg(event.value(QStringLiteral("max_auto_turns")).toInt()));
            return true;
        }
        if (type == QStringLiteral("reset")) {
            m_turnsCompleted = 0; m_lastRecapTurns = -1;
            // A new conversation has a new id (protocol 25); an older worker does not say it, and
            // then the old one must not be taken for what this pane still holds.
            m_sessionId = event.value(QStringLiteral("session_id")).toString();
            // /new or "clear the conversation": the one that just ended keeps its terminal text,
            // and the new one starts with none of it (#0TJ9).
            syncSessionText();
            // The context event that reset_conversation emitted just before this one already
            // carries the new conversation's numbers, so those are not touched here (issue 5PY9) —
            // but a switch still waiting to land, or a compaction the old conversation started,
            // must not survive into the new one's chip.
            clearNextContext();
            m_compacting = false;
            m_compactPercent = -1; m_compactThinking = false;
            updateContextLabel();
            if (m_reconfigureOnNewChat) {
                m_reconfigureOnNewChat = false;
                QTimer::singleShot(0, this, [this] { if (!m_agentBusy) configurePreset(m_currentPreset, false); });
            }
            return true;
        }
        return false;
    }
